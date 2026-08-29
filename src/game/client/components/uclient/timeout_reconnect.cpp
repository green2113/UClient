#include "timeout_reconnect.h"

#include <base/process.h>
#include <base/system.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/gameclient.h>

#include <cerrno>
#include <cstdio>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#elif defined(CONF_FAMILY_UNIX)
#include <signal.h>
#endif

namespace
{
constexpr const char *SESSION_FILE_PREFIX = "timeout_reconnect.";
constexpr const char *SESSION_FILE_SUFFIX = ".session";

bool IsProcessIdAlive(int ProcessId)
{
	if(ProcessId <= 0 || ProcessId == process_id())
		return false;
#if defined(CONF_FAMILY_WINDOWS)
	HANDLE Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)ProcessId);
	if(!Process)
		return false;
	DWORD ExitCode = 0;
	const bool Alive = GetExitCodeProcess(Process, &ExitCode) && ExitCode == STILL_ACTIVE;
	CloseHandle(Process);
	return Alive;
#elif defined(CONF_FAMILY_UNIX)
	return kill(ProcessId, 0) == 0 || errno == EPERM;
#else
	return false;
#endif
}

bool ReadTimeoutSession(IStorage *pStorage, const char *pPath, char *pServer, int ServerSize, int64_t *pLastUnix, int *pTimeoutSec)
{
	IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return false;
	char aBuf[512] = "";
	const unsigned Read = io_read(File, aBuf, sizeof(aBuf) - 1);
	io_close(File);
	aBuf[Read] = '\0';

	char aServer[NETADDR_MAXSTRSIZE] = "";
	long long LastUnix = 0;
	int TimeoutSec = 100;
	if(Read == 0 || sscanf(aBuf, "%255s %lld %d", aServer, &LastUnix, &TimeoutSec) < 2 ||
		aServer[0] == '\0' || LastUnix <= 0)
		return false;
	if(TimeoutSec <= 0)
		TimeoutSec = 100;
	str_copy(pServer, aServer, ServerSize);
	*pLastUnix = (int64_t)LastUnix;
	*pTimeoutSec = TimeoutSec;
	return true;
}

struct STimeoutSessionScan
{
	IStorage *m_pStorage;
	char *m_pServer;
	int m_ServerSize;
	int64_t *m_pLastUnix;
	int *m_pTimeoutSec;
	bool m_Found = false;
};

int ScanTimeoutSession(const char *pName, int IsDir, int DirType, void *pUser)
{
	(void)DirType;
	auto *pScan = static_cast<STimeoutSessionScan *>(pUser);
	const char *pProcessId = IsDir ? nullptr : str_startswith(pName, SESSION_FILE_PREFIX);
	if(!pProcessId)
		return 0;
	const char *pSuffix = str_find(pProcessId, SESSION_FILE_SUFFIX);
	if(!pSuffix || pSuffix[str_length(SESSION_FILE_SUFFIX)] != '\0' || pSuffix == pProcessId)
		return 0;

	int ProcessId = 0;
	for(const char *pDigit = pProcessId; pDigit < pSuffix; ++pDigit)
	{
		if(*pDigit < '0' || *pDigit > '9')
			return 0;
		ProcessId = ProcessId * 10 + (*pDigit - '0');
	}
	if(IsProcessIdAlive(ProcessId))
		return 0;

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "uclient/%s", pName);
	char aServer[NETADDR_MAXSTRSIZE];
	int64_t LastUnix;
	int TimeoutSec;
	if(ReadTimeoutSession(pScan->m_pStorage, aPath, aServer, sizeof(aServer), &LastUnix, &TimeoutSec) &&
		(!pScan->m_Found || LastUnix > *pScan->m_pLastUnix))
	{
		str_copy(pScan->m_pServer, aServer, pScan->m_ServerSize);
		*pScan->m_pLastUnix = LastUnix;
		*pScan->m_pTimeoutSec = TimeoutSec;
		pScan->m_Found = true;
	}
	pScan->m_pStorage->RemoveFile(aPath, IStorage::TYPE_SAVE);
	return 0;
}
}

void CTimeoutReconnect::OnInit()
{
	// Snapshot any leftover session from a previous process before heartbeats run.
	LoadCrashSnapshotOnce();
}

void CTimeoutReconnect::OnReset()
{
	m_aServerAddr[0] = '\0';
	m_TimeoutSec = DEFAULT_TIMEOUT_SEC;
	m_TimeoutKnown = false;
	m_QuerySent = false;
	m_OnlineSinceTick = 0;
	m_LastHeartbeatTick = 0;
	m_LastHeartbeatUnix = 0;
	// Keep crash snapshot / memory disconnect / pending across map resets.
}

void CTimeoutReconnect::OnStateChange(int NewState, int OldState)
{
	// Quit/restart never go through STATE_OFFLINE — clear the session file here.
	if(NewState == IClient::STATE_QUITTING || NewState == IClient::STATE_RESTARTING)
	{
		m_IntentionalLeave = true;
		EndOnlineSession(true);
		return;
	}

	if(NewState == IClient::STATE_ONLINE && OldState < IClient::STATE_ONLINE)
	{
		BeginOnlineSession();
		return;
	}

	if(NewState == IClient::STATE_OFFLINE)
	{
		const bool ClearFile = m_IntentionalLeave;
		m_IntentionalLeave = false;
		EndOnlineSession(ClearFile);
	}
}

void CTimeoutReconnect::OnUpdate()
{
	LoadCrashSnapshotOnce();
	TryAutoReconnect();

	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	// Address can become valid slightly after STATE_ONLINE; keep trying.
	TryActivatePending();
	TrySendTimeoutQuery();
	PersistHeartbeat();
	UpdatePendingDisplay();
}

void CTimeoutReconnect::RenderHud()
{
	if(!ShouldShowHud())
		return;

	const int Remaining = RemainingSeconds();
	if(Remaining <= 0)
		return;

	// Always draw in HUD screen space so camera/world mapping cannot pin this to the map.
	float PrevScreenX0, PrevScreenY0, PrevScreenX1, PrevScreenY1;
	Graphics()->GetScreen(&PrevScreenX0, &PrevScreenY0, &PrevScreenX1, &PrevScreenY1);

	const float Width = 300.0f * Graphics()->ScreenAspect();
	const float Height = 300.0f;
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Timeout in: %ds remaining", Remaining);

	const float FontSize = 5.5f;
	const float Margin = 6.0f;
	const float TextX = Margin;
	const float TextY = Height - FontSize - Margin;

	TextRender()->TextColor(1.0f, 0.35f, 0.1f, 1.0f);
	TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.75f);
	TextRender()->Text(TextX, TextY, FontSize, aBuf, -1.0f);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());

	Graphics()->MapScreen(PrevScreenX0, PrevScreenY0, PrevScreenX1, PrevScreenY1);
}

void CTimeoutReconnect::MarkIntentionalLeave()
{
	m_IntentionalLeave = true;
	// Clear immediately so a delayed quit cannot rewrite the file via heartbeat.
	ClearSession();
	m_HasCrashSnapshot = false;
	m_aCrashServerAddr[0] = '\0';
	m_CrashLastUnix = 0;
	m_HasMemoryDisconnect = false;
	m_aMemoryServerAddr[0] = '\0';
	m_MemoryDisconnectUnix = 0;
	ClearPending();
	ClearAutoReconnect();
}

bool CTimeoutReconnect::TryConsumeTimeoutSettingsMessage(int ClientId, const char *pMessage)
{
	if(ClientId != -1 || !pMessage)
		return false;

	static const char *const s_pPrefix = "The Server Timeout is currently set to ";
	if(!str_startswith(pMessage, s_pPrefix))
		return false;

	int Seconds = 0;
	if(sscanf(pMessage, "The Server Timeout is currently set to %d seconds", &Seconds) != 1)
		return false;
	if(Seconds <= 0)
		return false;

	m_TimeoutSec = Seconds;
	m_TimeoutKnown = true;
	PersistHeartbeat();
	return true;
}

int CTimeoutReconnect::RemainingSeconds() const
{
	if(!m_ShowPending || m_PendingUntilUnix <= 0)
		return 0;
	const int64_t Now = time_timestamp();
	if(Now >= m_PendingUntilUnix)
		return 0;
	return (int)(m_PendingUntilUnix - Now);
}

bool CTimeoutReconnect::ShouldShowHud() const
{
	return g_Config.m_UcShowTimeoutReconnect &&
		Client()->State() == IClient::STATE_ONLINE &&
		RemainingSeconds() > 0;
}

void CTimeoutReconnect::SessionPath(char *pBuf, int BufSize) const
{
	str_format(pBuf, BufSize, "%s/%s%d%s", SESSION_DIR, SESSION_FILE_PREFIX, process_id(), SESSION_FILE_SUFFIX);
}

void CTimeoutReconnect::CurrentServerAddr(char *pBuf, int BufSize) const
{
	if(!pBuf || BufSize <= 0)
		return;
	pBuf[0] = '\0';
	net_addr_str(&Client()->ServerAddress(), pBuf, BufSize, true);
}

bool CTimeoutReconnect::LoadSession(char *pServer, int ServerSize, int64_t *pLastUnix, int *pTimeoutSec) const
{
	char aFallbackServer[NETADDR_MAXSTRSIZE] = "";
	int64_t FallbackLastUnix = 0;
	int FallbackTimeoutSec = DEFAULT_TIMEOUT_SEC;
	if(pServer && ServerSize > 0)
		pServer[0] = '\0';
	if(pLastUnix)
		*pLastUnix = 0;
	if(pTimeoutSec)
		*pTimeoutSec = DEFAULT_TIMEOUT_SEC;

	// The old shared file cannot distinguish a crash from another live client.
	Storage()->RemoveFile(LEGACY_SESSION_PATH, IStorage::TYPE_SAVE);
	STimeoutSessionScan Scan = {
		Storage(),
		pServer && ServerSize > 0 ? pServer : aFallbackServer,
		pServer && ServerSize > 0 ? ServerSize : (int)sizeof(aFallbackServer),
		pLastUnix ? pLastUnix : &FallbackLastUnix,
		pTimeoutSec ? pTimeoutSec : &FallbackTimeoutSec};
	Storage()->ListDirectory(IStorage::TYPE_SAVE, SESSION_DIR, ScanTimeoutSession, &Scan);
	return Scan.m_Found;
}

void CTimeoutReconnect::SaveSession(const char *pServer, int64_t LastUnix, int TimeoutSec) const
{
	if(!pServer || pServer[0] == '\0' || LastUnix <= 0 || TimeoutSec <= 0)
		return;

	Storage()->CreateFolder(SESSION_DIR, IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	SessionPath(aPath, sizeof(aPath));
	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "%s %lld %d\n", pServer, (long long)LastUnix, TimeoutSec);
	io_write(File, aBuf, str_length(aBuf));
	io_close(File);
}

void CTimeoutReconnect::ClearSession() const
{
	char aPath[IO_MAX_PATH_LENGTH];
	SessionPath(aPath, sizeof(aPath));
	Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE);
}

void CTimeoutReconnect::LoadCrashSnapshotOnce()
{
	if(m_CrashSnapshotLoaded)
		return;

	m_CrashSnapshotLoaded = true;
	m_HasCrashSnapshot = LoadSession(m_aCrashServerAddr, sizeof(m_aCrashServerAddr), &m_CrashLastUnix, &m_CrashTimeoutSec);
	if(!m_HasCrashSnapshot)
	{
		m_aCrashServerAddr[0] = '\0';
		m_CrashLastUnix = 0;
		m_CrashTimeoutSec = DEFAULT_TIMEOUT_SEC;
	}
}

bool CTimeoutReconnect::TryActivateFrom(const char *pServer, int64_t LastUnix, int TimeoutSec)
{
	if(!pServer || pServer[0] == '\0' || LastUnix <= 0 || TimeoutSec <= 0)
		return false;

	char aCurrent[NETADDR_MAXSTRSIZE];
	CurrentServerAddr(aCurrent, sizeof(aCurrent));
	if(aCurrent[0] == '\0' || str_comp(aCurrent, pServer) != 0)
		return false;

	const int64_t Now = time_timestamp();
	const int64_t Until = LastUnix + TimeoutSec;
	if(Until <= Now)
		return false;

	str_copy(m_aPendingServerAddr, pServer, sizeof(m_aPendingServerAddr));
	m_PendingUntilUnix = Until;
	m_ShowPending = true;
	m_TimeoutSec = TimeoutSec;
	m_TimeoutKnown = true;
	return true;
}

void CTimeoutReconnect::TryActivatePending()
{
	if(m_ShowPending)
		return;

	LoadCrashSnapshotOnce();

	// 1) Previous process crashed / was force-killed while on this server.
	if(m_HasCrashSnapshot)
	{
		if(TryActivateFrom(m_aCrashServerAddr, m_CrashLastUnix, m_CrashTimeoutSec))
		{
			m_HasCrashSnapshot = false;
			return;
		}

		// Only drop the crash snapshot once we know we are on a different server.
		char aCurrent[NETADDR_MAXSTRSIZE];
		CurrentServerAddr(aCurrent, sizeof(aCurrent));
		if(aCurrent[0] != '\0' && str_comp(aCurrent, m_aCrashServerAddr) != 0)
			m_HasCrashSnapshot = false;
		else if(aCurrent[0] != '\0')
		{
			// Same server but timer already expired.
			const int64_t Until = m_CrashLastUnix + m_CrashTimeoutSec;
			if(Until <= time_timestamp())
				m_HasCrashSnapshot = false;
		}
	}

	// 2) Same process dropped without an intentional leave.
	if(m_HasMemoryDisconnect)
	{
		if(TryActivateFrom(m_aMemoryServerAddr, m_MemoryDisconnectUnix, m_MemoryTimeoutSec))
		{
			m_HasMemoryDisconnect = false;
			return;
		}

		char aCurrent[NETADDR_MAXSTRSIZE];
		CurrentServerAddr(aCurrent, sizeof(aCurrent));
		if(aCurrent[0] != '\0' && str_comp(aCurrent, m_aMemoryServerAddr) != 0)
			m_HasMemoryDisconnect = false;
		else if(aCurrent[0] != '\0')
		{
			const int64_t Until = m_MemoryDisconnectUnix + m_MemoryTimeoutSec;
			if(Until <= time_timestamp())
				m_HasMemoryDisconnect = false;
		}
	}
}

void CTimeoutReconnect::BeginOnlineSession()
{
	LoadCrashSnapshotOnce();

	char aAddr[NETADDR_MAXSTRSIZE];
	CurrentServerAddr(aAddr, sizeof(aAddr));
	str_copy(m_aServerAddr, aAddr, sizeof(m_aServerAddr));

	m_TimeoutSec = m_ShowPending ? m_TimeoutSec : DEFAULT_TIMEOUT_SEC;
	m_TimeoutKnown = m_ShowPending;
	m_QuerySent = false;
	m_OnlineSinceTick = time_get();
	m_LastHeartbeatTick = 0;
	m_LastHeartbeatUnix = 0;

	TryActivatePending();

	// Do not overwrite the crash file until we have a real server address.
	if(m_aServerAddr[0] != '\0')
		PersistHeartbeat();
}

void CTimeoutReconnect::EndOnlineSession(bool ClearFile)
{
	if(!ClearFile && m_aServerAddr[0] != '\0')
	{
		// Unnatural leave in this process: freeze the last heartbeat time in memory.
		m_HasMemoryDisconnect = true;
		str_copy(m_aMemoryServerAddr, m_aServerAddr, sizeof(m_aMemoryServerAddr));
		m_MemoryDisconnectUnix = m_LastHeartbeatUnix > 0 ? m_LastHeartbeatUnix : time_timestamp();
		m_MemoryTimeoutSec = m_TimeoutSec > 0 ? m_TimeoutSec : DEFAULT_TIMEOUT_SEC;
		// Keep the session file as-is for process-crash recovery too.
	}

	m_aServerAddr[0] = '\0';
	m_TimeoutKnown = false;
	m_QuerySent = false;
	m_OnlineSinceTick = 0;
	m_LastHeartbeatTick = 0;
	m_LastHeartbeatUnix = 0;

	if(ClearFile)
	{
		ClearSession();
		m_HasCrashSnapshot = false;
		m_aCrashServerAddr[0] = '\0';
		m_CrashLastUnix = 0;
		m_HasMemoryDisconnect = false;
		m_aMemoryServerAddr[0] = '\0';
		m_MemoryDisconnectUnix = 0;
		ClearPending();
		ClearAutoReconnect();
	}
}

void CTimeoutReconnect::TrySendTimeoutQuery()
{
	if(m_QuerySent || m_OnlineSinceTick == 0)
		return;

	const int64_t Now = time_get();
	if(Now - m_OnlineSinceTick < (QUERY_DELAY_MS * time_freq()) / 1000)
		return;

	static const char *const s_pQuery = "/settings timeout";
	if(Client()->IsSixup())
	{
		protocol7::CNetMsg_Cl_Say Msg7;
		Msg7.m_Mode = protocol7::CHAT_ALL;
		Msg7.m_Target = -1;
		Msg7.m_pMessage = s_pQuery;
		Client()->SendPackMsg(IClient::CONN_MAIN, &Msg7, MSGFLAG_VITAL, true);
	}
	else
	{
		CNetMsg_Cl_Say Msg;
		Msg.m_Team = 0;
		Msg.m_pMessage = s_pQuery;
		Client()->SendPackMsg(IClient::CONN_MAIN, &Msg, MSGFLAG_VITAL);
	}
	m_QuerySent = true;
}

void CTimeoutReconnect::PersistHeartbeat()
{
	if(m_IntentionalLeave)
		return;

	if(m_aServerAddr[0] == '\0')
	{
		CurrentServerAddr(m_aServerAddr, sizeof(m_aServerAddr));
		if(m_aServerAddr[0] == '\0')
			return;
	}

	const int64_t NowTick = time_get();
	if(m_LastHeartbeatTick != 0 && NowTick - m_LastHeartbeatTick < (HEARTBEAT_INTERVAL_MS * time_freq()) / 1000)
		return;

	m_LastHeartbeatTick = NowTick;
	m_LastHeartbeatUnix = time_timestamp();
	SaveSession(m_aServerAddr, m_LastHeartbeatUnix, m_TimeoutSec > 0 ? m_TimeoutSec : DEFAULT_TIMEOUT_SEC);
}

void CTimeoutReconnect::UpdatePendingDisplay()
{
	if(!m_ShowPending)
		return;
	if(RemainingSeconds() > 0)
		return;

	if(g_Config.m_UcShowTimeoutReconnect && g_Config.m_UcAutoTimeoutReconnect &&
		m_aPendingServerAddr[0] != '\0' && m_AutoReconnectAtUnix == 0)
	{
		char aCurrent[NETADDR_MAXSTRSIZE];
		CurrentServerAddr(aCurrent, sizeof(aCurrent));
		if(aCurrent[0] != '\0' && str_comp(aCurrent, m_aPendingServerAddr) == 0)
		{
			str_copy(m_aAutoReconnectAddr, m_aPendingServerAddr, sizeof(m_aAutoReconnectAddr));
			m_AutoReconnectAtUnix = time_timestamp() + AUTO_RECONNECT_DELAY_SEC;
		}
	}

	ClearPending();
}

void CTimeoutReconnect::ClearPending()
{
	m_ShowPending = false;
	m_PendingUntilUnix = 0;
	m_aPendingServerAddr[0] = '\0';
}

void CTimeoutReconnect::ClearAutoReconnect()
{
	m_AutoReconnectAtUnix = 0;
	m_aAutoReconnectAddr[0] = '\0';
}

void CTimeoutReconnect::TryAutoReconnect()
{
	if(m_AutoReconnectAtUnix <= 0)
		return;

	if(!g_Config.m_UcShowTimeoutReconnect || !g_Config.m_UcAutoTimeoutReconnect)
	{
		ClearAutoReconnect();
		return;
	}

	// A pending mandatory update refuses every connect, so drop the schedule instead of retrying.
	if(Client()->UpdateRequired())
	{
		ClearAutoReconnect();
		return;
	}

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		ClearAutoReconnect();
		return;
	}

	char aCurrent[NETADDR_MAXSTRSIZE];
	CurrentServerAddr(aCurrent, sizeof(aCurrent));
	if(aCurrent[0] == '\0' || str_comp(aCurrent, m_aAutoReconnectAddr) != 0)
	{
		ClearAutoReconnect();
		return;
	}

	if(time_timestamp() < m_AutoReconnectAtUnix)
		return;

	char aAddr[NETADDR_MAXSTRSIZE];
	str_copy(aAddr, m_aAutoReconnectAddr, sizeof(aAddr));
	if(aAddr[0] == '\0')
	{
		ClearAutoReconnect();
		return;
	}

	// Avoid treating this reconnect as another unnatural drop.
	// MarkIntentionalLeave also clears the auto-reconnect schedule.
	MarkIntentionalLeave();
	Client()->Connect(aAddr);
}
