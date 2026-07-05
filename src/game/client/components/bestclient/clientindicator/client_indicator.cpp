/* Copyright © 2026 BestProject Team */
#include "client_indicator.h"

#include "protocol.h"
#include "../version.h"

#include <base/logger.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/network.h>
#include <engine/shared/uclient_presence_protocol.h>

#include <game/client/gameclient.h>
#include <game/version.h>

#include <algorithm>
#include <cstdarg>

namespace
{
	constexpr const char *LOG_SCOPE = "clientindicator-cl";
	constexpr const char *OLD_BC_BROWSER_URL = "http://150.241.70.188:8779/users.json";
	constexpr const char *OLD_BC_TOKEN_URL = "http://150.241.70.188:8779/token.json";
	constexpr const char *NEW_BC_BROWSER_URL = "https://150.241.70.188:8779/users.json";
	constexpr const char *NEW_BC_TOKEN_URL = "https://150.241.70.188:8779/token.json";
	constexpr int PACKET_DUMP_BYTES_PER_LINE = 64;

	bool NormalizePresenceServerAddress(const char *pAddress, char *pBuffer, int BufferSize)
	{
		if(!pAddress || pAddress[0] == '\0')
			return false;

		while(*pAddress != '\0' && str_isspace(*pAddress))
			++pAddress;

		char aToken[MAX_SERVER_ADDRESSES * NETADDR_MAXSTRSIZE];
		int Length = 0;
		while(pAddress[Length] != '\0' && pAddress[Length] != ',')
			++Length;
		while(Length > 0 && str_isspace(pAddress[Length - 1]))
			--Length;
		str_truncate(aToken, sizeof(aToken), pAddress, Length);
		if(aToken[0] == '\0')
			return false;

		NETADDR Addr;
		if(net_addr_from_url(&Addr, aToken, nullptr, 0) == 0 || net_addr_from_str(&Addr, aToken) == 0)
		{
			net_addr_str(&Addr, pBuffer, BufferSize, true);
			return true;
		}

		str_copy(pBuffer, aToken, BufferSize);
		return true;
	}

	void ParseUcPresenceList(json_value *pJson, std::unordered_map<std::string, std::unordered_set<std::string>> &Out)
	{
		Out.clear();
		if(!pJson || pJson->type != json_array)
			return;

		for(unsigned int EntryIndex = 0; EntryIndex < pJson->u.array.length; ++EntryIndex)
		{
			const json_value *pEntry = pJson->u.array.values[EntryIndex];
			if(!pEntry || pEntry->type != json_object)
				continue;

			for(unsigned int ObjectIndex = 0; ObjectIndex < pEntry->u.object.length; ++ObjectIndex)
			{
				const char *pServerKey = pEntry->u.object.values[ObjectIndex].name;
				const json_value *pServerValue = pEntry->u.object.values[ObjectIndex].value;
				if(!pServerKey || !pServerValue || pServerValue->type != json_object)
					continue;

				const json_value *pPlayers = json_object_get(pServerValue, "players");
				if(!pPlayers || pPlayers->type != json_array)
					continue;

				char aNormalizedServer[NETADDR_MAXSTRSIZE];
				if(!NormalizePresenceServerAddress(pServerKey, aNormalizedServer, sizeof(aNormalizedServer)))
					continue;

				auto &Names = Out[aNormalizedServer];
				for(unsigned int PlayerIndex = 0; PlayerIndex < pPlayers->u.array.length; ++PlayerIndex)
				{
					const json_value *pPlayer = pPlayers->u.array.values[PlayerIndex];
					if(!pPlayer || pPlayer->type != json_object)
						continue;
					const json_value *pName = json_object_get(pPlayer, "name");
					if(pName && pName->type == json_string && pName->u.string.ptr[0] != '\0')
						Names.insert(pName->u.string.ptr);
				}
			}
		}
	}

	void TrimConfigString(char *pValue, int Size)
	{
		if(!pValue || Size <= 0)
			return;

		const int Length = str_length(pValue);
		int Start = 0;
		while(Start < Length && str_isspace(pValue[Start]))
			++Start;

		int End = Length;
		while(End > Start && str_isspace(pValue[End - 1]))
			--End;

		if(Start == 0 && End == Length)
			return;

		const std::string Trimmed(pValue + Start, End - Start);
		str_copy(pValue, Trimmed.c_str(), Size);
	}

	int64_t SlowPacketProcessTicks()
	{
		return time_freq() / 500; // ~2ms
	}

	void NormalizeBestClientIndicatorConfig()
	{
		TrimConfigString(g_Config.m_BcClientIndicatorServerAddress, sizeof(g_Config.m_BcClientIndicatorServerAddress));
		TrimConfigString(g_Config.m_BcClientIndicatorBrowserUrl, sizeof(g_Config.m_BcClientIndicatorBrowserUrl));
		TrimConfigString(g_Config.m_BcClientIndicatorTokenUrl, sizeof(g_Config.m_BcClientIndicatorTokenUrl));
		TrimConfigString(g_Config.m_BcClientIndicatorSharedToken, sizeof(g_Config.m_BcClientIndicatorSharedToken));
		TrimConfigString(g_Config.m_BcClientIndicatorSecretKey, sizeof(g_Config.m_BcClientIndicatorSecretKey));

		if(g_Config.m_BcClientIndicatorBrowserUrl[0] == '\0' || str_comp(g_Config.m_BcClientIndicatorBrowserUrl, OLD_BC_BROWSER_URL) == 0)
			str_copy(g_Config.m_BcClientIndicatorBrowserUrl, NEW_BC_BROWSER_URL, sizeof(g_Config.m_BcClientIndicatorBrowserUrl));
		if(g_Config.m_BcClientIndicatorTokenUrl[0] == '\0' || str_comp(g_Config.m_BcClientIndicatorTokenUrl, OLD_BC_TOKEN_URL) == 0)
			str_copy(g_Config.m_BcClientIndicatorTokenUrl, NEW_BC_TOKEN_URL, sizeof(g_Config.m_BcClientIndicatorTokenUrl));
	}

	bool IsBlockedIndicatorAddress(const NETADDR &Addr)
	{
		return net_addr_is_local(&Addr);
	}

	bool EnsureUcInstallUuid()
	{
		if(g_Config.m_UcInstallUuid[0] != '\0')
			return false;
		FormatUuid(RandomUuid(), g_Config.m_UcInstallUuid, sizeof(g_Config.m_UcInstallUuid));
		return true;
	}

	const char *PacketTypeName(int PacketType)
	{
		switch(PacketType)
		{
		case BestClientIndicator::PACKET_JOIN:
			return "join";
		case BestClientIndicator::PACKET_HEARTBEAT:
			return "heartbeat";
		case BestClientIndicator::PACKET_LEAVE:
			return "leave";
		case BestClientIndicator::PACKET_PEER_STATE:
			return "peer_state";
		case BestClientIndicator::PACKET_PEER_REMOVE:
			return "peer_remove";
		case BestClientIndicator::PACKET_PEER_LIST:
			return "peer_list";
		case BestClientIndicator::PACKET_DEV_AUTH:
			return "dev_auth";
		case BestClientIndicator::PACKET_PEER_DEV_STATE:
			return "peer_dev_state";
		case BestClientIndicator::PACKET_PEER_DEV_LIST:
			return "peer_dev_list";
		case BestClientIndicator::PACKET_DEV_AUTH_RESULT:
			return "dev_auth_result";
		case BestClientIndicator::PACKET_VERSION_ANNOUNCE:
			return "version_announce";
		case BestClientIndicator::PACKET_PEER_VERSION_STATE:
			return "peer_version_state";
		default:
			return "unknown";
		}
	}

	const char *UcPacketTypeName(int PacketType)
	{
		switch(PacketType)
		{
		case UClientPresence::PACKET_JOIN:
			return "join";
		case UClientPresence::PACKET_HEARTBEAT:
			return "heartbeat";
		case UClientPresence::PACKET_LEAVE:
			return "leave";
		case UClientPresence::PACKET_SWITCH:
			return "switch";
		case UClientPresence::PACKET_PEER_STATE:
			return "uc_peer_state";
		case UClientPresence::PACKET_PEER_REMOVE:
			return "uc_peer_remove";
		case UClientPresence::PACKET_PEER_LIST:
			return "uc_peer_list";
		default:
			return "unknown";
		}
	}

	void DumpUdpPacketBytes(const char *pDirection, const NETADDR &Addr, const void *pData, int DataSize)
	{
		if(!pDirection || !pData || DataSize <= 0)
			return;

		char aAddr[NETADDR_MAXSTRSIZE];
		net_addr_str(&Addr, aAddr, sizeof(aAddr), true);
		log_info(LOG_SCOPE, "%s udp packet bytes=%d addr=%s", pDirection, DataSize, aAddr);

		const auto *pBytes = static_cast<const uint8_t *>(pData);
		for(int Offset = 0; Offset < DataSize; Offset += PACKET_DUMP_BYTES_PER_LINE)
		{
			const int ChunkSize = minimum(PACKET_DUMP_BYTES_PER_LINE, DataSize - Offset);
			char aHex[PACKET_DUMP_BYTES_PER_LINE * 2 + 1];
			str_hex(aHex, sizeof(aHex), pBytes + Offset, ChunkSize);
			log_info(LOG_SCOPE, "%s udp dump offset=%d size=%d hex=%s", pDirection, Offset, ChunkSize, aHex);
		}
	}
}

CClientIndicator::CClientIndicator()
{
	OnReset();
}

void CClientIndicator::OnInit()
{
	NormalizeBestClientIndicatorConfig();
	if(m_ClientInstanceId == UUID_ZEROED)
		m_ClientInstanceId = RandomUuid();
	if(EnsureUcInstallUuid())
	{
		DebugLogF("generated uc install uuid=%s", g_Config.m_UcInstallUuid);
		ConfigManager()->Save();
	}
	DebugLogF("init server=%s token_url=%s browser_url=%s", g_Config.m_BcClientIndicatorServerAddress, g_Config.m_BcClientIndicatorTokenUrl, g_Config.m_BcClientIndicatorBrowserUrl);
}

void CClientIndicator::OnReset()
{
	const int ClientState = Client()->State();
	const bool MapReload = ClientState == IClient::STATE_ONLINE || ClientState == IClient::STATE_LOADING;

	if(MapReload && (IsUcPresenceUdpEnabled() || g_Config.m_BcClientIndicator))
	{
		ResetPresenceStateForMapReload();
		return;
	}

	ResetPresenceState();
	ResetTokenState();
	ResetUcPresenceTask();
	m_UcPresenceByServer.clear();
	m_UcPeersByServer.clear();
	m_LastUcPresenceRefreshTick = 0;
	InvalidateUcPresenceLookupCache();
	InvalidateUcPeerLookupCache();
}

void CClientIndicator::OnMapLoad()
{
	NotifyUcPresenceMapReload();
}

void CClientIndicator::NotifyUcPresenceMapReload()
{
	if(!IsUcPresenceUdpEnabled())
		return;

	EnsurePresenceSocket();

	const bool HadJoinedSlots = std::any_of(m_aUcLocalSlots.begin(), m_aUcLocalSlots.end(), [](const BestClientIndicatorClient::SUcLocalSlotState &Slot) {
		return Slot.m_Joined && Slot.m_LastClientId >= 0;
	});
	if(HadJoinedSlots)
	{
		SendUcLeaveForAllLocalClients();
		DebugLog("uc presence map reload: sent leave for previous client ids");
	}

	char aNormalizedServer[NETADDR_MAXSTRSIZE];
	const char *pServer = m_aLastKnownPresenceServerAddr[0] != '\0' ? m_aLastKnownPresenceServerAddr : EffectivePresenceServerAddress();
	if(pServer[0] != '\0' && NormalizePresenceServerAddress(pServer, aNormalizedServer, sizeof(aNormalizedServer)))
		ClearUcPeersForServer(aNormalizedServer);

	m_UcPresenceMapReloadPending = true;
	m_LastHeartbeatTick = 0;
}

void CClientIndicator::ResetPresenceStateForMapReload()
{
	SendUcLeaveForAllLocalClients();

	m_aUcLocalSlots = {};
	m_RegisteredClientIds.clear();
	m_DeveloperClientIds.clear();
	m_ClientVersions.clear();
	m_PresenceCache.Replace({});
	m_LastHeartbeatTick = 0;
	m_LastRegistrationSyncTick = 0;
	m_UcPresenceMapReloadPending = true;
	InvalidateUcPresenceLookupCache();
	InvalidateUcPeerLookupCache();
}

void CClientIndicator::InvalidateUcPresenceLookupCache()
{
	m_aUcPresenceLookupServer[0] = '\0';
	m_pUcPresenceLookupNames = nullptr;
}

void CClientIndicator::InvalidateUcPeerLookupCache()
{
	m_aUcPeerLookupServer[0] = '\0';
	m_pUcPeersOnCurrentServer = nullptr;
}

void CClientIndicator::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState == IClient::STATE_OFFLINE)
	{
		StopPresence(true);
		ResetTokenState();
	}
	else if(NewState == IClient::STATE_ONLINE)
	{
		DebugLog("state -> online, refreshing token/browser cache");
		if(g_Config.m_BcClientIndicator)
		{
			RefreshBrowserCache(false);
			RefreshToken(false);
		}
		RefreshUcPresenceList(false);
	}
}

void CClientIndicator::OnShutdown()
{
	StopPresence(true);
	ResetBrowserTask();
	ResetTokenTask();
}

bool CClientIndicator::IsPlayerBestClient(int ClientId) const
{
	const CGameClient *pGameClient = GameClient();
	if(pGameClient && pGameClient->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_OTHERS_CLIENT_INDICATOR))
		return false;

	if(Client()->State() != IClient::STATE_ONLINE || !g_Config.m_BcClientIndicator)
		return false;
	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(LocalId >= 0 && ClientId == LocalId)
			return true;
	}
	if(m_PresenceCache.IsPresent(ClientId))
		return true;

	// Fallback: use browser snapshot data for the current server when presence
	// packets are unavailable (e.g. temporary UDP reachability issues).
	const char *pPlayerName = PlayerNameForClient(ClientId);
	if(pPlayerName[0] == '\0')
		return false;
	char aCurrentServerAddress[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aCurrentServerAddress, sizeof(aCurrentServerAddress), true);
	if(m_BrowserCache.HasPlayer(aCurrentServerAddress, pPlayerName))
		return true;

	const IServerBrowser::CServerEntry *pCurrentServer = ServerBrowser()->Find(Client()->ServerAddress());
	if(!pCurrentServer)
		return false;

	const CServerInfo &Info = pCurrentServer->m_Info;
	for(int Index = 0; Index < minimum(Info.m_NumReceivedClients, (int)MAX_CLIENTS); ++Index)
	{
		const CServerInfo::CClient &Client = Info.m_aClients[Index];
		if(Client.m_BestClient && str_comp(Client.m_aName, pPlayerName) == 0)
			return true;
	}

	return false;
}

bool CClientIndicator::IsPlayerDeveloper(int ClientId) const
{
	const CGameClient *pGameClient = GameClient();
	if(pGameClient && pGameClient->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_OTHERS_CLIENT_INDICATOR))
		return false;

	if(Client()->State() != IClient::STATE_ONLINE || !g_Config.m_BcClientIndicator)
		return false;
	if(m_DeveloperClientIds.find(ClientId) != m_DeveloperClientIds.end())
		return true;

	const char *pPlayerName = PlayerNameForClient(ClientId);
	if(pPlayerName[0] == '\0')
		return false;
	char aCurrentServerAddress[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aCurrentServerAddress, sizeof(aCurrentServerAddress), true);
	bool BrowserDeveloper = false;
	if(m_BrowserCache.HasPlayer(aCurrentServerAddress, pPlayerName, &BrowserDeveloper) && BrowserDeveloper)
		return true;

	const IServerBrowser::CServerEntry *pCurrentServer = ServerBrowser()->Find(Client()->ServerAddress());
	if(!pCurrentServer)
		return false;

	const CServerInfo &Info = pCurrentServer->m_Info;
	for(int Index = 0; Index < minimum(Info.m_NumReceivedClients, (int)MAX_CLIENTS); ++Index)
	{
		const CServerInfo::CClient &Client = Info.m_aClients[Index];
		if(Client.m_BestClientDeveloper && str_comp(Client.m_aName, pPlayerName) == 0)
			return true;
	}

	return false;
}

bool CClientIndicator::GetPlayerVersionLabel(int ClientId, char *pVersion, int VersionSize) const
{
	if(!pVersion || VersionSize <= 0)
		return false;
	pVersion[0] = '\0';

	const CGameClient *pGameClient = GameClient();
	if(pGameClient && pGameClient->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_OTHERS_CLIENT_INDICATOR))
		return false;

	if(Client()->State() != IClient::STATE_ONLINE || !g_Config.m_BcClientIndicator)
		return false;

	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(LocalId >= 0 && ClientId == LocalId)
		{
			str_copy(pVersion, BESTCLIENT_VERSION, VersionSize);
			return true;
		}
	}

	const char *pPlayerName = PlayerNameForClient(ClientId);
	if(pPlayerName[0] == '\0')
		return false;

	char aCurrentServerAddress[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aCurrentServerAddress, sizeof(aCurrentServerAddress), true);
	const auto It = m_ClientVersions.find(ClientId);
	if(It != m_ClientVersions.end() && !It->second.empty())
	{
		str_copy(pVersion, It->second.c_str(), VersionSize);
		return true;
	}
	if(m_BrowserCache.GetPlayerVersion(aCurrentServerAddress, pPlayerName, pVersion, VersionSize))
		return true;

	if(IsPlayerBestClient(ClientId))
	{
		str_copy(pVersion, "under", VersionSize);
		return true;
	}

	return false;
}

void CClientIndicator::RefreshBrowserCache(bool Force)
{
#if defined(CONF_HEADLESS_CLIENT)
	(void)Force;
	return;
#endif
	if(g_Config.m_BcClientIndicator == 0)
		return;
	NormalizeBestClientIndicatorConfig();
	if(g_Config.m_BcClientIndicatorBrowserUrl[0] == '\0')
	{
		DebugLog("browser refresh skipped: browser url is empty");
		return;
	}
	if(m_pBrowserTask && !m_pBrowserTask->Done())
	{
		if(!Force)
		{
			DebugLog("browser refresh skipped: request already running");
			return;
		}
		DebugLog("browser refresh forcing reset of running request");
		ResetBrowserTask();
	}

	m_pBrowserTask = HttpGet(g_Config.m_BcClientIndicatorBrowserUrl);
	m_pBrowserTask->Timeout(CTimeout{10000, 0, 500, 5});
	m_pBrowserTask->IpResolve(IPRESOLVE::V4);
	// The indicator web endpoint is deployed with a self-signed certificate by default.
	m_pBrowserTask->VerifyPeer(false);
	m_pBrowserTask->CloseConnection(true);
	m_pBrowserTask->LogProgress(HTTPLOG::FAILURE);
	DebugLogF("starting browser request url=%s", g_Config.m_BcClientIndicatorBrowserUrl);
	m_LastBrowserRefreshTick = time_get();
	Http()->Run(m_pBrowserTask);
}

void CClientIndicator::RefreshToken(bool Force)
{
#if defined(CONF_HEADLESS_CLIENT)
	(void)Force;
	return;
#endif
	if(g_Config.m_BcClientIndicator == 0 || g_Config.m_BcClientIndicatorSendInfo == 0)
		return;
	NormalizeBestClientIndicatorConfig();
	if(g_Config.m_BcClientIndicatorTokenUrl[0] == '\0')
	{
		DebugLog("token refresh skipped: token url is empty");
		return;
	}
	if(m_pTokenTask && !m_pTokenTask->Done())
	{
		if(!Force)
		{
			DebugLog("token refresh skipped: request already running");
			return;
		}
		DebugLog("token refresh forcing reset of running request");
		ResetTokenTask();
	}

	m_pTokenTask = HttpGet(g_Config.m_BcClientIndicatorTokenUrl);
	m_pTokenTask->Timeout(CTimeout{10000, 0, 500, 5});
	m_pTokenTask->IpResolve(IPRESOLVE::V4);
	// Keep token bootstrap aligned with the self-signed indicator deployment.
	m_pTokenTask->VerifyPeer(false);
	m_pTokenTask->CloseConnection(true);
	m_pTokenTask->LogProgress(HTTPLOG::FAILURE);
	DebugLogF("starting token request url=%s", g_Config.m_BcClientIndicatorTokenUrl);
	m_LastTokenRefreshTick = time_get();
	Http()->Run(m_pTokenTask);
}

void CClientIndicator::OnUpdate()
{
	const int64_t PerfStart = time_get();
	if(!IsBrowserSnapshotEnabled() && !ShouldRunUcPresence())
	{
		m_RuntimeState = ESubsystemRuntimeState::DISABLED;
		if(m_WasPresenceEnabled || m_WasUcPresenceActive || m_Socket || HasPendingNetworkTask() || m_aWebSharedToken[0] != '\0')
		{
			StopPresence(true);
			ResetBrowserTask();
			ResetTokenState();
		}
		ClearBrowserSnapshot();
		SetPresenceBlockReason("presence update skipped: indicator disabled");
		m_WasPresenceEnabled = false;
		m_WasUcPresenceActive = false;
		return;
	}

	if(!IsBrowserSnapshotEnabled() && ShouldRunUcPresence())
	{
		UpdatePresence();
		m_LastUpdateCostTick = time_get() - PerfStart;
		return;
	}

	const bool PresenceEnabled = IsPresenceEnabled();
	const int64_t Now = time_get();
	const int64_t BrowserRefreshInterval = 30 * time_freq();
	const int64_t TokenRefreshInterval = 60 * time_freq();
	if(PresenceEnabled && !m_WasPresenceEnabled)
	{
		m_RuntimeState = ESubsystemRuntimeState::ARMED;
		RefreshBrowserCache(false);
		RefreshToken(false);
	}
	else if(PresenceEnabled)
	{
		m_RuntimeState = m_Socket ? ESubsystemRuntimeState::ACTIVE : ESubsystemRuntimeState::ARMED;
		const bool NeedsBrowserSnapshot =
			Client()->State() == IClient::STATE_OFFLINE ||
			(Client()->State() == IClient::STATE_ONLINE && GameClient()->m_Menus.IsActive());
		if(NeedsBrowserSnapshot && m_NextPresenceBrowserRefreshTick != 0 && Now >= m_NextPresenceBrowserRefreshTick && !m_pBrowserTask)
		{
			DebugLog("refreshing browser cache after presence update");
			m_NextPresenceBrowserRefreshTick = 0;
			RefreshBrowserCache(false);
		}
		if(NeedsBrowserSnapshot && !m_pBrowserTask && (m_LastBrowserRefreshTick == 0 || Now - m_LastBrowserRefreshTick >= BrowserRefreshInterval))
			RefreshBrowserCache(false);
		if(g_Config.m_BcClientIndicatorSendInfo && !m_pTokenTask && (m_LastTokenRefreshTick == 0 || Now - m_LastTokenRefreshTick >= TokenRefreshInterval))
			RefreshToken(false);
		if(!g_Config.m_BcClientIndicatorSendInfo && !IsUcPresenceUdpEnabled() && m_Socket)
			ClosePresenceSocket();
	}
	else if(!PresenceEnabled && !ShouldRunUcPresence())
	{
		m_RuntimeState = ESubsystemRuntimeState::COOLDOWN;
		if(!m_pBrowserTask && (m_LastBrowserRefreshTick == 0 || Now - m_LastBrowserRefreshTick >= BrowserRefreshInterval))
			RefreshBrowserCache(false);
		if(m_WasPresenceEnabled || m_WasUcPresenceActive || m_Socket || m_HasServerAddr || m_HasUcServerAddr || m_pTokenTask || m_aWebSharedToken[0] != '\0')
		{
			StopPresence(true);
			ResetTokenState();
		}
		SetPresenceBlockReason("presence update skipped: client offline");
		m_WasPresenceEnabled = false;
		m_WasUcPresenceActive = false;
	}

	if(m_pBrowserTask && m_pBrowserTask->State() == EHttpState::DONE)
	{
		DebugLogF("browser request done http_status=%d", m_pBrowserTask->StatusCode());
		FinishBrowserCacheRefresh();
		ResetBrowserTask();
	}
	else if(m_pBrowserTask && (m_pBrowserTask->State() == EHttpState::ERROR || m_pBrowserTask->State() == EHttpState::ABORTED))
	{
		DebugLogF("browser request ended with state=%d", (int)m_pBrowserTask->State());
		ResetBrowserTask();
	}

	// UClient presence list for server browser (5 min HTTP fallback).
	const int64_t UcPresenceRefreshInterval = 300 * time_freq();
	if(g_Config.m_UcPresenceApiBaseUrl[0] != '\0' &&
		!m_pUcPresenceTask &&
		(m_LastUcPresenceRefreshTick == 0 || Now - m_LastUcPresenceRefreshTick >= UcPresenceRefreshInterval))
	{
		RefreshUcPresenceList(false);
	}
	if(m_pUcPresenceTask && m_pUcPresenceTask->State() == EHttpState::DONE)
	{
		FinishUcPresenceRefresh();
		ResetUcPresenceTask();
	}
	else if(m_pUcPresenceTask && (m_pUcPresenceTask->State() == EHttpState::ERROR || m_pUcPresenceTask->State() == EHttpState::ABORTED))
	{
		ResetUcPresenceTask();
	}

	if(m_pTokenTask && m_pTokenTask->State() == EHttpState::DONE)
	{
		DebugLogF("token request done http_status=%d", m_pTokenTask->StatusCode());
		FinishTokenRefresh();
		ResetTokenTask();
	}
	else if(m_pTokenTask && m_pTokenTask->State() == EHttpState::ERROR)
	{
		DebugLog("token request ended with error");
		char aOldEffectiveToken[sizeof(m_aWebSharedToken)];
		str_copy(aOldEffectiveToken, EffectiveSharedToken(), sizeof(aOldEffectiveToken));
		m_aWebSharedToken[0] = '\0';
		if(NeedsBcPresenceUdp() && str_comp(aOldEffectiveToken, EffectiveSharedToken()) != 0)
			StopPresence(true);
		ResetTokenTask();
	}
	else if(m_pTokenTask && m_pTokenTask->State() == EHttpState::ABORTED)
	{
		DebugLog("token request aborted");
		ResetTokenTask();
	}

	if(PresenceEnabled || ShouldRunUcPresence())
		UpdatePresence();

	m_LastUpdateCostTick = time_get() - PerfStart;
	m_MaxUpdateCostTick = maximum(m_MaxUpdateCostTick, m_LastUpdateCostTick);
	m_TotalUpdateCostTick += m_LastUpdateCostTick;
	++m_UpdateSamples;
	if(g_Config.m_DbgClientIndicator >= 2)
	{
		if(m_LastPerfReportTick == 0 || Now - m_LastPerfReportTick >= time_freq())
		{
			DebugLogF("perf last=%.3fms avg=%.3fms max=%.3fms samples=%lld socket=%d",
				m_LastUpdateCostTick * 1000.0 / (double)time_freq(),
				m_UpdateSamples > 0 ? (m_TotalUpdateCostTick * 1000.0 / (double)time_freq()) / (double)m_UpdateSamples : 0.0,
				m_MaxUpdateCostTick * 1000.0 / (double)time_freq(),
				(long long)m_UpdateSamples,
				m_Socket != nullptr ? 1 : 0);
			m_LastPerfReportTick = Now;
			m_TotalUpdateCostTick = 0;
			m_UpdateSamples = 0;
			m_MaxUpdateCostTick = 0;
		}
	}
}

void CClientIndicator::OpenPresenceSocket()
{
	NormalizeBestClientIndicatorConfig();
	if(m_Socket)
	{
		DebugLog("presence socket already open");
		return;
	}
	if(!NeedsAnyPresenceUdp())
	{
		SetPresenceBlockReason("presence socket open skipped: no UDP presence target configured");
		return;
	}

	if(NeedsBcPresenceUdp())
	{
		if(g_Config.m_BcClientIndicatorServerAddress[0] == '\0')
		{
			SetPresenceBlockReason("presence socket open skipped: BC server address is empty");
			return;
		}
		if(!BestClientIndicator::ParseAddress(g_Config.m_BcClientIndicatorServerAddress, BestClientIndicator::DEFAULT_PORT, m_ServerAddr))
		{
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "presence socket open failed: cannot parse BC server address '%s'", g_Config.m_BcClientIndicatorServerAddress);
			SetPresenceBlockReason(aBuf);
			return;
		}
		if(IsBlockedIndicatorAddress(m_ServerAddr))
		{
			char aServerAddr[NETADDR_MAXSTRSIZE];
			net_addr_str(&m_ServerAddr, aServerAddr, sizeof(aServerAddr), true);
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "presence socket open blocked: BC target address %s is local", aServerAddr);
			SetPresenceBlockReason(aBuf);
			return;
		}
		m_HasServerAddr = true;
		str_copy(m_aLastPresenceServerAddr, g_Config.m_BcClientIndicatorServerAddress, sizeof(m_aLastPresenceServerAddr));
	}
	else
	{
		m_HasServerAddr = false;
		m_aLastPresenceServerAddr[0] = '\0';
	}

	if(IsUcPresenceUdpEnabled())
		EnsureUcPresenceSocket();
	if(!NeedsBcPresenceUdp() && !m_HasUcServerAddr)
	{
		SetPresenceBlockReason("presence socket open skipped: UC server address is invalid");
		return;
	}

	NETADDR Bind = NETADDR_ZEROED;
	Bind.type = NETTYPE_ALL;
	Bind.port = 0;
	m_Socket = net_udp_create(Bind);
	if(!m_Socket)
	{
		SetPresenceBlockReason("presence socket open failed: net_udp_create returned null");
		return;
	}
	net_set_non_blocking(m_Socket);
	ClearPresenceBlockReason();
	if(m_HasServerAddr)
	{
		char aServerAddr[NETADDR_MAXSTRSIZE];
		net_addr_str(&m_ServerAddr, aServerAddr, sizeof(aServerAddr), true);
		DebugLogF("presence socket opened, bc udp target=%s", aServerAddr);
	}
	if(m_HasUcServerAddr)
	{
		char aServerAddr[NETADDR_MAXSTRSIZE];
		net_addr_str(&m_UcServerAddr, aServerAddr, sizeof(aServerAddr), true);
		DebugLogF("presence socket opened, uc udp target=%s", aServerAddr);
		m_LastRegistrationSyncTick = 0;
	}
}

void CClientIndicator::ClosePresenceSocket()
{
	if(m_Socket)
	{
		DebugLog("closing presence socket");
		net_udp_close(m_Socket);
		m_Socket = nullptr;
	}
	m_HasServerAddr = false;
	m_HasUcServerAddr = false;
	m_aLastPresenceServerAddr[0] = '\0';
	m_aLastUcPresenceServerAddr[0] = '\0';
}

void CClientIndicator::StopPresence(bool SendLeavePackets)
{
	const bool HadPresenceState = m_Socket || !m_RegisteredClientIds.empty() || m_aLastPresenceServerAddr[0] != '\0' || m_aLastUcPresenceServerAddr[0] != '\0';
	if(HadPresenceState && SendLeavePackets)
	{
		DebugLog("stopping presence and sending leave packets");
		SendLeaveForAll();
	}
	else if(HadPresenceState)
	{
		DebugLog("stopping presence without leave packets");
	}
	ClosePresenceSocket();
	ResetPresenceState();
}

void CClientIndicator::EnsureUcPresenceSocket()
{
	if(!IsUcPresenceUdpEnabled())
	{
		m_HasUcServerAddr = false;
		m_aLastUcPresenceServerAddr[0] = '\0';
		return;
	}

	const bool HadUcServer = m_aLastUcPresenceServerAddr[0] != '\0' || m_HasUcServerAddr;
	const bool ServerChanged = HadUcServer && str_comp(m_aLastUcPresenceServerAddr, g_Config.m_UcPresenceUdpServerAddress) != 0;
	if(ServerChanged)
	{
		DebugLogF("uc presence server address changed, resetting state old=%s new=%s", m_aLastUcPresenceServerAddr, g_Config.m_UcPresenceUdpServerAddress);
		m_aLastUcPresenceServerAddr[0] = '\0';
	}

	if(!UClientPresence::ParseAddress(g_Config.m_UcPresenceUdpServerAddress, UClientPresence::DEFAULT_PORT, m_UcServerAddr))
	{
		SetPresenceBlockReason("uc presence socket skipped: cannot parse server address");
		m_HasUcServerAddr = false;
		return;
	}
	if(IsBlockedIndicatorAddress(m_UcServerAddr))
	{
		char aServerAddr[NETADDR_MAXSTRSIZE];
		net_addr_str(&m_UcServerAddr, aServerAddr, sizeof(aServerAddr), true);
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "uc presence socket blocked: target address %s is local", aServerAddr);
		SetPresenceBlockReason(aBuf);
		m_HasUcServerAddr = false;
		return;
	}

	m_HasUcServerAddr = true;
	str_copy(m_aLastUcPresenceServerAddr, g_Config.m_UcPresenceUdpServerAddress, sizeof(m_aLastUcPresenceServerAddr));
}

void CClientIndicator::EnsurePresenceSocket()
{
	NormalizeBestClientIndicatorConfig();
	if(Client()->State() != IClient::STATE_ONLINE || !NeedsAnyPresenceUdp())
	{
		SetPresenceBlockReason("presence socket skipped: indicator disabled or client offline");
		return;
	}

	const bool HadPresenceServer = m_aLastPresenceServerAddr[0] != '\0' || m_Socket || m_HasServerAddr;
	const bool BcServerChanged = HadPresenceServer && NeedsBcPresenceUdp() &&
		str_comp(m_aLastPresenceServerAddr, g_Config.m_BcClientIndicatorServerAddress) != 0;
	if(BcServerChanged)
	{
		DebugLogF("presence server address changed, resetting bc state old=%s new=%s", m_aLastPresenceServerAddr, g_Config.m_BcClientIndicatorServerAddress);
		m_HasServerAddr = false;
		m_aLastPresenceServerAddr[0] = '\0';
	}

	if(!NeedsAnyPresenceUdp())
	{
		SetPresenceBlockReason("presence socket skipped: no UDP presence target configured");
		return;
	}

	if(m_Socket)
	{
		if(NeedsBcPresenceUdp())
		{
			if(!BestClientIndicator::ParseAddress(g_Config.m_BcClientIndicatorServerAddress, BestClientIndicator::DEFAULT_PORT, m_ServerAddr))
				m_HasServerAddr = false;
			else
			{
				m_HasServerAddr = true;
				str_copy(m_aLastPresenceServerAddr, g_Config.m_BcClientIndicatorServerAddress, sizeof(m_aLastPresenceServerAddr));
			}
		}
		else
		{
			m_HasServerAddr = false;
			m_aLastPresenceServerAddr[0] = '\0';
		}
		EnsureUcPresenceSocket();
		return;
	}

	const int64_t Now = time_get();
	if(m_LastPresenceStartAttempt != 0 && Now - m_LastPresenceStartAttempt < time_freq())
	{
		if(m_LastPresenceBlockReason.empty())
			SetPresenceBlockReason("presence socket skipped: retry throttled");
		return;
	}
	m_LastPresenceStartAttempt = Now;
	OpenPresenceSocket();
}

const char *CClientIndicator::CurrentGameServerAddress()
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		m_aLastBlockedGameServerAddr[0] = '\0';
		DebugLog("current game server address unavailable: client offline");
		return "";
	}
	if(IsBlockedIndicatorAddress(Client()->ServerAddress()))
	{
		char aAddr[NETADDR_MAXSTRSIZE];
		net_addr_str(&Client()->ServerAddress(), aAddr, sizeof(aAddr), true);
		if(str_comp(m_aLastBlockedGameServerAddr, aAddr) != 0)
		{
			DebugLogF("current game server address blocked: %s is local", aAddr);
			str_copy(m_aLastBlockedGameServerAddr, aAddr, sizeof(m_aLastBlockedGameServerAddr));
		}
		return "";
	}
	m_aLastBlockedGameServerAddr[0] = '\0';
	char aAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aAddr, sizeof(aAddr), true);
	str_copy(m_aLastGameServerAddr, aAddr, sizeof(m_aLastGameServerAddr));
	return m_aLastGameServerAddr;
}

const char *CClientIndicator::EffectivePresenceServerAddress()
{
	const char *pCurrent = CurrentGameServerAddress();
	if(pCurrent[0] != '\0')
	{
		char aNormalized[NETADDR_MAXSTRSIZE];
		if(NormalizePresenceServerAddress(pCurrent, aNormalized, sizeof(aNormalized)))
			str_copy(m_aLastKnownPresenceServerAddr, aNormalized, sizeof(m_aLastKnownPresenceServerAddr));
		else
			str_copy(m_aLastKnownPresenceServerAddr, pCurrent, sizeof(m_aLastKnownPresenceServerAddr));
		m_GameServerEmptySinceTick = 0;
		return m_aLastKnownPresenceServerAddr;
	}

	// Still connected to a game server: keep heartbeating with the last known
	// address even if ServerAddress() briefly returns empty (background/AFK/map load).
	if(Client()->State() == IClient::STATE_ONLINE && m_aLastKnownPresenceServerAddr[0] != '\0')
	{
		if(m_GameServerEmptySinceTick == 0)
			m_GameServerEmptySinceTick = time_get();
		return m_aLastKnownPresenceServerAddr;
	}

	return "";
}

const char *CClientIndicator::PlayerNameForClient(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return "";
	if(GameClient()->m_aClients[ClientId].m_Active && GameClient()->m_aClients[ClientId].m_aName[0] != '\0')
		return GameClient()->m_aClients[ClientId].m_aName;
	if(ClientId == GameClient()->m_aLocalIds[IClient::CONN_MAIN])
		return Client()->PlayerName();
	if(ClientId == GameClient()->m_aLocalIds[IClient::CONN_DUMMY])
		return Client()->DummyName();
	return "";
}

void CClientIndicator::SendBcPresencePacket(int ClientId, int PacketType)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	const char *pSharedToken = EffectiveSharedToken();
	if(!NeedsBcPresenceUdp() || !m_Socket || !m_HasServerAddr || !pSharedToken || pSharedToken[0] == '\0')
		return;

	std::vector<uint8_t> vPacket;
	vPacket.reserve(256);
	BestClientIndicator::WriteHeader(vPacket, (BestClientIndicator::EPacketType)PacketType);
	BestClientIndicator::WriteUuid(vPacket, m_ClientInstanceId);
	const CUuid Nonce = RandomUuid();
	BestClientIndicator::WriteUuid(vPacket, Nonce);
	BestClientIndicator::WriteU64(vPacket, (uint64_t)time_timestamp());
	BestClientIndicator::WriteString(vPacket, CurrentGameServerAddress());
	BestClientIndicator::WriteString(vPacket, PlayerNameForClient(ClientId));
	BestClientIndicator::WriteS16(vPacket, (int16_t)ClientId);
	BestClientIndicator::AppendProof(vPacket, pSharedToken);

	if(g_Config.m_DbgClientIndicator >= 2)
		DumpUdpPacketBytes("sent", m_ServerAddr, vPacket.data(), (int)vPacket.size());

	const int Sent = net_udp_send(m_Socket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
	char aServerAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&m_ServerAddr, aServerAddr, sizeof(aServerAddr), true);
	DebugLogF("sent %s packet client_id=%d player='%s' game_server=%s indicator_server=%s bytes=%d result=%d",
		PacketTypeName(PacketType), ClientId, PlayerNameForClient(ClientId), CurrentGameServerAddress(), aServerAddr, (int)vPacket.size(), Sent);
}

void CClientIndicator::SendPresencePacket(int ClientId, int PacketType)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	SendBcPresencePacket(ClientId, PacketType);

	if(PacketType == BestClientIndicator::PACKET_JOIN)
	{
		SendUcPresenceUdpPacket(ClientId, UClientPresence::PACKET_JOIN);
		SendPresenceHttpEvent(ClientId, "join");
	}
	else if(PacketType == BestClientIndicator::PACKET_LEAVE)
	{
		SendUcPresenceUdpPacket(ClientId, UClientPresence::PACKET_LEAVE);
		SendPresenceHttpEvent(ClientId, "leave");
	}
	else if(PacketType == BestClientIndicator::PACKET_HEARTBEAT)
	{
		SendUcPresenceUdpPacket(ClientId, UClientPresence::PACKET_HEARTBEAT);
	}
}

void CClientIndicator::SendUcPresenceUdpPacket(int ClientId, int PacketType, const char *pFromServer)
{
	if(!IsUcPresenceUdpEnabled() || !m_Socket || !m_HasUcServerAddr || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	const char *pServerAddress = EffectivePresenceServerAddress();
	if((PacketType == UClientPresence::PACKET_JOIN ||
		   PacketType == UClientPresence::PACKET_HEARTBEAT ||
		   PacketType == UClientPresence::PACKET_SWITCH) &&
		pServerAddress[0] == '\0')
	{
		return;
	}
	if(PacketType == UClientPresence::PACKET_SWITCH && (!pFromServer || pFromServer[0] == '\0'))
		return;

	std::vector<uint8_t> vPacket;
	vPacket.reserve(256);
	UClientPresence::WriteHeader(vPacket, (UClientPresence::EPacketType)PacketType);
	UClientPresence::WriteString(vPacket, g_Config.m_UcInstallUuid);
	UClientPresence::WriteUuid(vPacket, m_ClientInstanceId);
	const CUuid Nonce = RandomUuid();
	UClientPresence::WriteUuid(vPacket, Nonce);
	UClientPresence::WriteU64(vPacket, (uint64_t)time_timestamp());
	UClientPresence::WriteString(vPacket, pServerAddress);
	UClientPresence::WriteString(vPacket, PlayerNameForClient(ClientId));
	UClientPresence::WriteS16(vPacket, (int16_t)ClientId);
	UClientPresence::WriteString(vPacket, UCLIENT_VERSION);
	if(PacketType == UClientPresence::PACKET_SWITCH)
		UClientPresence::WriteString(vPacket, pFromServer);
	UClientPresence::AppendProof(vPacket, g_Config.m_UcPresenceUdpSharedToken);

	if(g_Config.m_DbgClientIndicator >= 2)
		DumpUdpPacketBytes("sent", m_UcServerAddr, vPacket.data(), (int)vPacket.size());

	const int Sent = net_udp_send(m_Socket, &m_UcServerAddr, vPacket.data(), (int)vPacket.size());
	char aServerAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&m_UcServerAddr, aServerAddr, sizeof(aServerAddr), true);
	DebugLogF("sent uc %s packet client_id=%d player='%s' game_server=%s uc_server=%s bytes=%d result=%d",
		UcPacketTypeName(PacketType), ClientId, PlayerNameForClient(ClientId), pServerAddress, aServerAddr, (int)vPacket.size(), Sent);
}

void CClientIndicator::PresenceHttpPlayerId(char *pBuffer, int BufferSize) const
{
	if(!pBuffer || BufferSize <= 0)
		return;
	pBuffer[0] = '\0';
	if(g_Config.m_UcInstallUuid[0] != '\0')
	{
		str_copy(pBuffer, g_Config.m_UcInstallUuid, BufferSize);
		return;
	}
	FormatUuid(m_ClientInstanceId, pBuffer, BufferSize);
}

void CClientIndicator::PresenceSessionIdForClient(int ClientId, char *pBuffer, int BufferSize) const
{
	if(!pBuffer || BufferSize <= 0)
		return;
	char aInstanceId[UUID_MAXSTRSIZE];
	FormatUuid(m_ClientInstanceId, aInstanceId, sizeof(aInstanceId));
	str_format(pBuffer, BufferSize, "%s:%d", aInstanceId, ClientId);
}

void CClientIndicator::SendPresenceHttpEvent(int ClientId, const char *pEventPath)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pEventPath || pEventPath[0] == '\0')
		return;
	if(g_Config.m_UcPresenceApiBaseUrl[0] == '\0' || g_Config.m_UcInstallUuid[0] == '\0')
		return;
	if(IsUcPresenceUdpEnabled() && (str_comp(pEventPath, "join") == 0 || str_comp(pEventPath, "leave") == 0))
		return;

	std::string Base = g_Config.m_UcPresenceApiBaseUrl;
	while(!Base.empty() && Base.back() == '/')
		Base.pop_back();
	if(Base.empty())
		return;

	const char *pServerAddress = EffectivePresenceServerAddress();
	if((str_comp(pEventPath, "join") == 0 || str_comp(pEventPath, "heartbeat") == 0) && pServerAddress[0] == '\0')
		return;

	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("playerId");
	Writer.WriteStrValue(g_Config.m_UcInstallUuid);
	char aSessionId[UUID_MAXSTRSIZE];
	PresenceSessionIdForClient(ClientId, aSessionId, sizeof(aSessionId));
	Writer.WriteAttribute("sessionId");
	Writer.WriteStrValue(aSessionId);
	if(pServerAddress[0] != '\0')
	{
		Writer.WriteAttribute("server");
		Writer.WriteStrValue(pServerAddress);
	}
	Writer.WriteAttribute("name");
	Writer.WriteStrValue(PlayerNameForClient(ClientId));
	if(UCLIENT_VERSION[0] != '\0')
	{
		Writer.WriteAttribute("version");
		Writer.WriteStrValue(UCLIENT_VERSION);
	}
	Writer.WriteAttribute("clientId");
	char aClientId[16];
	str_format(aClientId, sizeof(aClientId), "%d", ClientId);
	Writer.WriteStrValue(aClientId);
	Writer.EndObject();
	std::string Json = Writer.GetOutputString();

	std::string Url = Base + "/" + pEventPath;

	auto pPost = HttpPostJson(Url.c_str(), Json.c_str());
	pPost->Timeout(CTimeout{5000, 0, 500, 5});
	pPost->IpResolve(IPRESOLVE::V4);
	pPost->LogProgress(HTTPLOG::FAILURE);
	pPost->FailOnErrorStatus(false);
	Http()->Run(std::move(pPost));

	DebugLogF("sent http %s event player='%s' player_id='%s' server=%s", pEventPath, PlayerNameForClient(ClientId), g_Config.m_UcInstallUuid, pServerAddress);
}

void CClientIndicator::SendPresenceHttpSwitchEvent(int ClientId, const char *pFromServerAddress, const char *pToServerAddress)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	if(!pFromServerAddress || pFromServerAddress[0] == '\0' || !pToServerAddress || pToServerAddress[0] == '\0')
		return;

	SendUcPresenceUdpPacket(ClientId, UClientPresence::PACKET_SWITCH, pFromServerAddress);
	if(IsUcPresenceUdpEnabled())
		return;

	if(g_Config.m_UcPresenceApiBaseUrl[0] == '\0' || g_Config.m_UcInstallUuid[0] == '\0')
		return;

	std::string Base = g_Config.m_UcPresenceApiBaseUrl;
	while(!Base.empty() && Base.back() == '/')
		Base.pop_back();
	if(Base.empty())
		return;

	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("playerId");
	Writer.WriteStrValue(g_Config.m_UcInstallUuid);
	char aSessionId[UUID_MAXSTRSIZE];
	PresenceSessionIdForClient(ClientId, aSessionId, sizeof(aSessionId));
	Writer.WriteAttribute("sessionId");
	Writer.WriteStrValue(aSessionId);
	Writer.WriteAttribute("server");
	Writer.WriteStrValue(pFromServerAddress);
	Writer.WriteAttribute("toServer");
	Writer.WriteStrValue(pToServerAddress);
	Writer.WriteAttribute("name");
	Writer.WriteStrValue(PlayerNameForClient(ClientId));
	if(UCLIENT_VERSION[0] != '\0')
	{
		Writer.WriteAttribute("version");
		Writer.WriteStrValue(UCLIENT_VERSION);
	}
	Writer.WriteAttribute("clientId");
	char aClientId[16];
	str_format(aClientId, sizeof(aClientId), "%d", ClientId);
	Writer.WriteStrValue(aClientId);
	Writer.EndObject();
	std::string Json = Writer.GetOutputString();

	std::string Url = Base + "/switch";

	auto pPost = HttpPostJson(Url.c_str(), Json.c_str());
	pPost->Timeout(CTimeout{5000, 0, 500, 5});
	pPost->IpResolve(IPRESOLVE::V4);
	pPost->LogProgress(HTTPLOG::FAILURE);
	pPost->FailOnErrorStatus(false);
	Http()->Run(std::move(pPost));

	DebugLogF("sent http switch event player='%s' player_id='%s' from=%s to=%s", PlayerNameForClient(ClientId), g_Config.m_UcInstallUuid, pFromServerAddress, pToServerAddress);
}

void CClientIndicator::SendDevAuthPacket(int ClientId)
{
	NormalizeBestClientIndicatorConfig();
	if(!m_Socket || !m_HasServerAddr || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	if(g_Config.m_BcClientIndicatorSecretKey[0] == '\0')
		return;

	std::vector<uint8_t> vPacket;
	vPacket.reserve(256);
	BestClientIndicator::WriteHeader(vPacket, BestClientIndicator::PACKET_DEV_AUTH);
	BestClientIndicator::WriteUuid(vPacket, m_ClientInstanceId);
	const CUuid Nonce = RandomUuid();
	BestClientIndicator::WriteUuid(vPacket, Nonce);
	BestClientIndicator::WriteU64(vPacket, (uint64_t)time_timestamp());
	BestClientIndicator::WriteString(vPacket, CurrentGameServerAddress());
	BestClientIndicator::WriteString(vPacket, PlayerNameForClient(ClientId));
	BestClientIndicator::WriteS16(vPacket, (int16_t)ClientId);
	BestClientIndicator::AppendHmacSha256(vPacket, g_Config.m_BcClientIndicatorSecretKey);

	if(g_Config.m_DbgClientIndicator >= 2)
		DumpUdpPacketBytes("sent", m_ServerAddr, vPacket.data(), (int)vPacket.size());

	const int Sent = net_udp_send(m_Socket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
	char aServerAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&m_ServerAddr, aServerAddr, sizeof(aServerAddr), true);
	DebugLogF("sent dev_auth packet client_id=%d player='%s' game_server=%s indicator_server=%s bytes=%d result=%d",
		ClientId, PlayerNameForClient(ClientId), CurrentGameServerAddress(), aServerAddr, (int)vPacket.size(), Sent);
}

void CClientIndicator::SendVersionPacket(int ClientId)
{
	if(!m_Socket || !m_HasServerAddr)
		return;

	const char *pSharedToken = EffectiveSharedToken();
	if(pSharedToken[0] == '\0' || BESTCLIENT_VERSION[0] == '\0')
		return;

	std::vector<uint8_t> vPacket;
	vPacket.reserve(256);
	BestClientIndicator::WriteHeader(vPacket, BestClientIndicator::PACKET_VERSION_ANNOUNCE);
	BestClientIndicator::WriteUuid(vPacket, m_ClientInstanceId);
	CUuid Nonce = RandomUuid();
	BestClientIndicator::WriteUuid(vPacket, Nonce);
	BestClientIndicator::WriteU64(vPacket, (uint64_t)time_timestamp());
	BestClientIndicator::WriteString(vPacket, CurrentGameServerAddress());
	BestClientIndicator::WriteString(vPacket, PlayerNameForClient(ClientId));
	BestClientIndicator::WriteS16(vPacket, (int16_t)ClientId);
	BestClientIndicator::WriteString(vPacket, BESTCLIENT_VERSION);
	BestClientIndicator::AppendProof(vPacket, pSharedToken);

	if(g_Config.m_DbgClientIndicator >= 2)
		DumpUdpPacketBytes("sent", m_ServerAddr, vPacket.data(), (int)vPacket.size());

	const int Sent = net_udp_send(m_Socket, &m_ServerAddr, vPacket.data(), (int)vPacket.size());
	char aServerAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&m_ServerAddr, aServerAddr, sizeof(aServerAddr), true);
	DebugLogF("sent %s packet client_id=%d version='%s' player='%s' game_server=%s indicator_server=%s bytes=%d result=%d",
		PacketTypeName(BestClientIndicator::PACKET_VERSION_ANNOUNCE), ClientId, BESTCLIENT_VERSION, PlayerNameForClient(ClientId), CurrentGameServerAddress(), aServerAddr, (int)vPacket.size(), Sent);
}

void CClientIndicator::SendLeaveForAll()
{
	for(const int ClientId : m_RegisteredClientIds)
		SendBcPresencePacket(ClientId, BestClientIndicator::PACKET_LEAVE);
	SendUcLeaveForAllLocalClients();
}

void CClientIndicator::SendUcLeaveForAllLocalClients()
{
	if(!IsUcPresenceUdpEnabled())
		return;
	for(BestClientIndicatorClient::SUcLocalSlotState &SlotState : m_aUcLocalSlots)
	{
		if(!SlotState.m_Joined || SlotState.m_LastClientId < 0)
			continue;
		SendUcPresenceUdpPacket(SlotState.m_LastClientId, UClientPresence::PACKET_LEAVE);
		m_PresenceCache.SetPresent(SlotState.m_LastClientId, false);
		DebugLogF("uc presence leave all slot client_id=%d", SlotState.m_LastClientId);
		SlotState.m_Joined = false;
		SlotState.m_LastClientId = -1;
	}
}

void CClientIndicator::UpdateUcPresenceForLocalClients()
{
	if(!ShouldRunUcPresence() || !m_Socket || !m_HasUcServerAddr)
		return;
	if(EffectivePresenceServerAddress()[0] == '\0')
		return;

	for(int Slot = 0; Slot < NUM_DUMMIES; ++Slot)
	{
		const int ClientId = GameClient()->m_aLocalIds[Slot];
		const BestClientIndicatorClient::SUcLocalSlotTickResult Tick = BestClientIndicatorClient::ComputeUcLocalSlotTick(m_aUcLocalSlots[Slot], ClientId);

		if(Tick.m_SendLeave)
		{
			SendUcPresenceUdpPacket(Tick.m_LeaveClientId, UClientPresence::PACKET_LEAVE);
			m_PresenceCache.SetPresent(Tick.m_LeaveClientId, false);
			if(Tick.m_SendJoin)
				DebugLogF("uc presence slot=%d client_id changed from=%d to=%d", Slot, Tick.m_LeaveClientId, Tick.m_JoinClientId);
			else
				DebugLogF("uc presence left slot=%d client_id=%d", Slot, Tick.m_LeaveClientId);
		}
		if(Tick.m_SendJoin)
		{
			SendUcPresenceUdpPacket(Tick.m_JoinClientId, UClientPresence::PACKET_JOIN);
			m_PresenceCache.SetPresent(Tick.m_JoinClientId, true);
			DebugLogF("uc presence joined slot=%d client_id=%d name='%s'", Slot, Tick.m_JoinClientId, PlayerNameForClient(Tick.m_JoinClientId));
		}
		if(Tick.m_SendHeartbeat)
			SendUcPresenceUdpPacket(Tick.m_HeartbeatClientId, UClientPresence::PACKET_HEARTBEAT);

		m_aUcLocalSlots[Slot] = Tick.m_NextState;
	}
}

void CClientIndicator::NotifyPresenceServerChanged(const char *pFromServer, const char *pToServer)
{
	if(!pToServer || pToServer[0] == '\0')
		return;

	std::vector<int> vClientIds;
	vClientIds.reserve(NUM_DUMMIES);
	for(const int ClientId : GameClient()->m_aLocalIds)
	{
		if(ClientId >= 0 && ClientId < MAX_CLIENTS)
			vClientIds.push_back(ClientId);
	}
	if(vClientIds.empty())
	{
		vClientIds.assign(m_RegisteredClientIds.begin(), m_RegisteredClientIds.end());
	}
	if(vClientIds.empty())
		return;

	const bool CanSwitch = pFromServer && pFromServer[0] != '\0' && str_comp(pFromServer, pToServer) != 0;
	for(const int ClientId : vClientIds)
	{
		if(CanSwitch)
			SendPresenceHttpSwitchEvent(ClientId, pFromServer, pToServer);
		else
			SendPresencePacket(ClientId, BestClientIndicator::PACKET_JOIN);
		m_RegisteredClientIds.insert(ClientId);
		m_PresenceCache.SetPresent(ClientId, true);
	}

	str_copy(m_aPreviousPresenceServerForSwitch, pToServer, sizeof(m_aPreviousPresenceServerForSwitch));
	m_GameServerEmptySinceTick = 0;
	m_LastRegistrationSyncTick = 0;
	m_LastHeartbeatTick = 0;
	m_LastHttpHeartbeatTick = 0;
	char aNormalizedTo[NETADDR_MAXSTRSIZE];
	if(NormalizePresenceServerAddress(pToServer, aNormalizedTo, sizeof(aNormalizedTo)))
		ClearUcPeersForServer(aNormalizedTo);
	InvalidateUcPresenceLookupCache();
	InvalidateUcPeerLookupCache();
	SchedulePresenceBrowserRefresh();
	DebugLogF("presence server changed from=%s to=%s switch=%d clients=%llu",
		CanSwitch ? pFromServer : "(none)", pToServer, CanSwitch ? 1 : 0, (unsigned long long)vClientIds.size());
}

bool CClientIndicator::UcPeerAppliesToCurrentServer(const char *pServerAddress) const
{
	if(!pServerAddress || pServerAddress[0] == '\0')
		return false;

	char aCurrentServerAddress[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aCurrentServerAddress, sizeof(aCurrentServerAddress), true);
	if(aCurrentServerAddress[0] == '\0')
		return false;

	char aNormalizedCurrent[NETADDR_MAXSTRSIZE];
	char aNormalizedPacket[NETADDR_MAXSTRSIZE];
	if(!NormalizePresenceServerAddress(aCurrentServerAddress, aNormalizedCurrent, sizeof(aNormalizedCurrent)))
		return false;
	if(!NormalizePresenceServerAddress(pServerAddress, aNormalizedPacket, sizeof(aNormalizedPacket)))
		return false;
	return str_comp(aNormalizedCurrent, aNormalizedPacket) == 0;
}

void CClientIndicator::ClearUcPeersForServer(const char *pNormalizedServer)
{
	if(!pNormalizedServer || pNormalizedServer[0] == '\0')
		return;
	m_UcPeersByServer.erase(pNormalizedServer);
	InvalidateUcPeerLookupCache();
}

void CClientIndicator::ApplyUcPeerState(const UClientPresence::CPeerState &State)
{
	if(State.m_ClientId < 0)
		return;

	char aNormalizedServer[NETADDR_MAXSTRSIZE];
	if(!NormalizePresenceServerAddress(State.m_ServerAddress.c_str(), aNormalizedServer, sizeof(aNormalizedServer)))
		return;
	if(!UcPeerAppliesToCurrentServer(aNormalizedServer))
		return;

	auto &Peers = m_UcPeersByServer[aNormalizedServer];
	Peers[State.m_ClientId] = SUcPeerInfo{State.m_PlayerName};
	InvalidateUcPeerLookupCache();
	DebugLogF("uc peer state client_id=%d name='%s' server=%s", State.m_ClientId, State.m_PlayerName.c_str(), aNormalizedServer);
}

void CClientIndicator::ApplyUcPeerRemove(const UClientPresence::CPeerState &State)
{
	if(State.m_ClientId < 0)
		return;

	char aNormalizedServer[NETADDR_MAXSTRSIZE];
	if(!NormalizePresenceServerAddress(State.m_ServerAddress.c_str(), aNormalizedServer, sizeof(aNormalizedServer)))
		return;
	if(!UcPeerAppliesToCurrentServer(aNormalizedServer))
		return;

	auto ItServer = m_UcPeersByServer.find(aNormalizedServer);
	if(ItServer == m_UcPeersByServer.end())
		return;

	ItServer->second.erase(State.m_ClientId);
	if(ItServer->second.empty())
		m_UcPeersByServer.erase(ItServer);
	InvalidateUcPeerLookupCache();
	DebugLogF("uc peer remove client_id=%d name='%s' server=%s", State.m_ClientId, State.m_PlayerName.c_str(), aNormalizedServer);
}

void CClientIndicator::PruneStaleUcPeers()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	char aCurrentServerAddress[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aCurrentServerAddress, sizeof(aCurrentServerAddress), true);
	if(aCurrentServerAddress[0] == '\0')
		return;

	char aNormalizedServer[NETADDR_MAXSTRSIZE];
	if(!NormalizePresenceServerAddress(aCurrentServerAddress, aNormalizedServer, sizeof(aNormalizedServer)))
		return;

	auto ItServer = m_UcPeersByServer.find(aNormalizedServer);
	if(ItServer == m_UcPeersByServer.end())
		return;

	auto &Peers = ItServer->second;
	for(auto It = Peers.begin(); It != Peers.end();)
	{
		const int ClientId = It->first;
		const char *pCurrentName = PlayerNameForClient(ClientId);
		if(pCurrentName[0] != '\0' && str_comp(pCurrentName, It->second.m_PlayerName.c_str()) != 0)
		{
			DebugLogF("uc peer prune stale client_id=%d cached='%s' current='%s' server=%s",
				ClientId, It->second.m_PlayerName.c_str(), pCurrentName, aNormalizedServer);
			It = Peers.erase(It);
		}
		else
		{
			++It;
		}
	}

	if(Peers.empty())
		m_UcPeersByServer.erase(ItServer);
	InvalidateUcPeerLookupCache();
}

void CClientIndicator::ApplyUcPeerList(const UClientPresence::CPeerList &PeerList)
{
	char aNormalizedServer[NETADDR_MAXSTRSIZE];
	if(!NormalizePresenceServerAddress(PeerList.m_ServerAddress.c_str(), aNormalizedServer, sizeof(aNormalizedServer)))
		return;
	if(!UcPeerAppliesToCurrentServer(aNormalizedServer))
		return;

	// PEER_LIST is an authoritative snapshot for this game server.
	auto &Peers = m_UcPeersByServer[aNormalizedServer];
	Peers.clear();
	for(const UClientPresence::CPeerListEntry &Entry : PeerList.m_vPeers)
	{
		if(Entry.m_ClientId < 0)
			continue;
		Peers[Entry.m_ClientId] = SUcPeerInfo{Entry.m_PlayerName};
	}
	if(Peers.empty())
		m_UcPeersByServer.erase(aNormalizedServer);
	InvalidateUcPeerLookupCache();
	DebugLogF("uc peer list server=%s count=%llu", aNormalizedServer, (unsigned long long)PeerList.m_vPeers.size());
}

void CClientIndicator::ProcessUcPresencePacket(const unsigned char *pData, int DataSize)
{
	UClientPresence::CPeerState PeerState;
	if(UClientPresence::ReadPeerStatePacket(pData, DataSize, PeerState))
	{
		ApplyUcPeerState(PeerState);
		return;
	}

	if(UClientPresence::ReadPeerRemovePacket(pData, DataSize, PeerState))
	{
		ApplyUcPeerRemove(PeerState);
		return;
	}

	UClientPresence::CPeerList PeerList;
	if(UClientPresence::ReadPeerListPacket(pData, DataSize, PeerList))
		ApplyUcPeerList(PeerList);
}

void CClientIndicator::ProcessIncomingPackets(bool Force)
{
	if(!m_Socket || (!m_HasServerAddr && !m_HasUcServerAddr))
		return;

	const int64_t StartTick = time_get();
	if(!CSubsystemTicker::ShouldRunPeriodic(StartTick, m_LastPresencePollTick, time_freq() / 20, Force))
		return;
	int ReceivedPackets = 0;
	int ProcessedPackets = 0;

	for(int PacketCount = 0; PacketCount < BestClientIndicator::MAX_RECEIVE_PACKETS_PER_TICK; ++PacketCount)
	{
		NETADDR From = NETADDR_ZEROED;
		unsigned char *pRawData = nullptr;
		const int DataSize = net_udp_recv(m_Socket, &From, &pRawData);
		if(DataSize <= 0 || !pRawData)
			break;
		ReceivedPackets++;

		const bool FromBc = m_HasServerAddr && net_addr_comp(&From, &m_ServerAddr) == 0;
		const bool FromUc = m_HasUcServerAddr && net_addr_comp(&From, &m_UcServerAddr) == 0;
		if(!FromBc && !FromUc)
		{
			if(g_Config.m_DbgClientIndicator >= 2)
			{
				char aFrom[NETADDR_MAXSTRSIZE];
				net_addr_str(&From, aFrom, sizeof(aFrom), true);
				DebugLogF("ignoring udp packet from unexpected addr=%s", aFrom);
			}
			continue;
		}

		if(FromUc)
		{
			if(g_Config.m_DbgClientIndicator >= 2)
			{
				UClientPresence::EPacketType Type = (UClientPresence::EPacketType)0;
				int Offset = 0;
				const bool HasHeader = UClientPresence::ReadHeader(pRawData, DataSize, Type, Offset, nullptr);
				char aFrom[NETADDR_MAXSTRSIZE];
				net_addr_str(&From, aFrom, sizeof(aFrom), true);
				if(HasHeader)
					DebugLogF("received uc udp packet from=%s bytes=%d type=%d(%s)", aFrom, DataSize, (int)Type, UcPacketTypeName((int)Type));
				else
					DebugLogF("received uc udp packet from=%s bytes=%d type=invalid", aFrom, DataSize);
				DumpUdpPacketBytes("recv", From, pRawData, DataSize);
			}
			ProcessedPackets++;
			ProcessUcPresencePacket(pRawData, DataSize);
			continue;
		}

		if(g_Config.m_DbgClientIndicator >= 2)
		{
			char aFrom[NETADDR_MAXSTRSIZE];
			net_addr_str(&From, aFrom, sizeof(aFrom), true);
			BestClientIndicator::EPacketType Type = (BestClientIndicator::EPacketType)0;
			int Offset = 0;
			const bool HasHeader = BestClientIndicator::ReadHeader(pRawData, DataSize, Type, Offset);
			char aServerAddr[NETADDR_MAXSTRSIZE];
			net_addr_str(&m_ServerAddr, aServerAddr, sizeof(aServerAddr), true);
			if(HasHeader)
				DebugLogF("received udp packet from=%s bytes=%d type=%d(%s) expected_indicator_server=%s", aFrom, DataSize, (int)Type, PacketTypeName((int)Type), aServerAddr);
			else
				DebugLogF("received udp packet from=%s bytes=%d type=invalid expected_indicator_server=%s", aFrom, DataSize, aServerAddr);

			DumpUdpPacketBytes("recv", From, pRawData, DataSize);
		}

		ProcessedPackets++;
		BestClientIndicator::CPeerState PeerState;
		if(BestClientIndicator::ReadPeerStatePacket(pRawData, DataSize, PeerState))
		{
			DebugLogF("received peer_state client_id=%d player='%s' server=%s", PeerState.m_ClientId, PeerState.m_PlayerName.c_str(), PeerState.m_ServerAddress.c_str());
			if(PeerState.m_ServerAddress == m_PresenceCache.ServerAddress())
			{
				m_PresenceCache.SetPresent(PeerState.m_ClientId, true);
				SchedulePresenceBrowserRefresh();
			}
			continue;
		}

		if(BestClientIndicator::ReadPeerRemovePacket(pRawData, DataSize, PeerState))
		{
			DebugLogF("received peer_remove client_id=%d player='%s' server=%s", PeerState.m_ClientId, PeerState.m_PlayerName.c_str(), PeerState.m_ServerAddress.c_str());
			if(PeerState.m_ServerAddress == m_PresenceCache.ServerAddress())
			{
				m_PresenceCache.SetPresent(PeerState.m_ClientId, false);
				m_DeveloperClientIds.erase(PeerState.m_ClientId);
				m_ClientVersions.erase(PeerState.m_ClientId);
				SchedulePresenceBrowserRefresh();
			}
			continue;
		}

		BestClientIndicator::CPeerList PeerList;
		if(BestClientIndicator::ReadPeerListPacket(pRawData, DataSize, PeerList) &&
			PeerList.m_ServerAddress == m_PresenceCache.ServerAddress())
		{
			DebugLogF("received peer_list server=%s count=%llu", PeerList.m_ServerAddress.c_str(), (unsigned long long)PeerList.m_vClientIds.size());
			m_PresenceCache.Replace(PeerList.m_vClientIds);
			SchedulePresenceBrowserRefresh();
			continue;
		}

		if(BestClientIndicator::ReadPeerDevStatePacket(pRawData, DataSize, PeerState))
		{
			DebugLogF("received peer_dev_state client_id=%d developer=%d player='%s' server=%s", PeerState.m_ClientId, PeerState.m_Developer ? 1 : 0, PeerState.m_PlayerName.c_str(), PeerState.m_ServerAddress.c_str());
			if(PeerState.m_ServerAddress == m_PresenceCache.ServerAddress())
			{
				if(PeerState.m_Developer)
					m_DeveloperClientIds.insert(PeerState.m_ClientId);
				else
					m_DeveloperClientIds.erase(PeerState.m_ClientId);
			}
			continue;
		}

		if(BestClientIndicator::ReadPeerDevListPacket(pRawData, DataSize, PeerList) &&
			PeerList.m_ServerAddress == m_PresenceCache.ServerAddress())
		{
			DebugLogF("received peer_dev_list server=%s count=%llu", PeerList.m_ServerAddress.c_str(), (unsigned long long)PeerList.m_vClientIds.size());
			m_DeveloperClientIds.clear();
			for(const int ClientId : PeerList.m_vClientIds)
				m_DeveloperClientIds.insert(ClientId);
			continue;
		}

		BestClientIndicator::CPeerVersionState PeerVersionState;
		if(BestClientIndicator::ReadPeerVersionStatePacket(pRawData, DataSize, PeerVersionState))
		{
			DebugLogF("received peer_version_state client_id=%d version='%s' player='%s' server=%s", PeerVersionState.m_ClientId, PeerVersionState.m_ClientVersion.c_str(), PeerVersionState.m_PlayerName.c_str(), PeerVersionState.m_ServerAddress.c_str());
			if(PeerVersionState.m_ServerAddress == m_PresenceCache.ServerAddress())
				m_ClientVersions[PeerVersionState.m_ClientId] = PeerVersionState.m_ClientVersion;
			continue;
		}

		BestClientIndicator::CDevAuthResult DevAuthResult;
		if(BestClientIndicator::ReadDevAuthResultPacket(pRawData, DataSize, DevAuthResult) &&
			DevAuthResult.m_ServerAddress == m_PresenceCache.ServerAddress())
		{
			DebugLogF("received dev_auth_result client_id=%d success=%d server=%s", DevAuthResult.m_ClientId, DevAuthResult.m_Success ? 1 : 0, DevAuthResult.m_ServerAddress.c_str());
			if(DevAuthResult.m_Success)
			{
				m_DeveloperClientIds.insert(DevAuthResult.m_ClientId);
				SchedulePresenceBrowserRefresh();
			}
			else
				m_DeveloperClientIds.erase(DevAuthResult.m_ClientId);
			continue;
		}

		if(g_Config.m_DbgClientIndicator >= 2)
			DebugLogF("received udp packet from indicator server but did not match known packet formats bytes=%d", DataSize);
	}

	const int64_t EndTick = time_get();
	const int64_t Delta = EndTick - StartTick;
	if(g_Config.m_DbgClientIndicator && Delta > SlowPacketProcessTicks())
	{
		const int64_t Ms = (Delta * 1000) / time_freq();
		DebugLogF("ProcessIncomingPackets slow: %lldms received=%d processed=%d", (long long)Ms, ReceivedPackets, ProcessedPackets);
	}
}

void CClientIndicator::SyncLocalRegistrations(bool Force)
{
	const int64_t Now = time_get();
	if(!CSubsystemTicker::ShouldRunPeriodic(Now, m_LastRegistrationSyncTick, time_freq() / 4, Force))
		return;

	std::array<bool, MAX_CLIENTS> aClientActive{};
	for(const int ClientId : GameClient()->m_aLocalIds)
	{
		if(ClientId >= 0 && ClientId < MAX_CLIENTS)
		{
			// Keep both local slots (main + dummy) registered even if one slot is
			// briefly marked inactive while switching control between them.
			aClientActive[ClientId] = true;
		}
	}
	const auto DesiredClientIds = BestClientIndicatorClient::CollectActiveLocalClientIds(GameClient()->m_aLocalIds, aClientActive);

	for(const int ClientId : DesiredClientIds.m_vClientIds)
	{
		if(m_RegisteredClientIds.find(ClientId) == m_RegisteredClientIds.end())
		{
			if(IsUcPresenceUdpEnabled() && (!m_Socket || !m_HasUcServerAddr))
				continue;
			SendPresencePacket(ClientId, BestClientIndicator::PACKET_JOIN);
			SendVersionPacket(ClientId);
			SendDevAuthPacket(ClientId);
			m_RegisteredClientIds.insert(ClientId);
			m_PresenceCache.SetPresent(ClientId, true);
			SchedulePresenceBrowserRefresh();
			DebugLogF("registered local client_id=%d name='%s'", ClientId, PlayerNameForClient(ClientId));
		}
	}

	for(auto It = m_RegisteredClientIds.begin(); It != m_RegisteredClientIds.end();)
	{
		if(!DesiredClientIds.Contains(*It))
		{
			SendBcPresencePacket(*It, BestClientIndicator::PACKET_LEAVE);
			m_PresenceCache.SetPresent(*It, false);
			m_DeveloperClientIds.erase(*It);
			DebugLogF("unregistered local client_id=%d", *It);
			It = m_RegisteredClientIds.erase(It);
		}
		else
		{
			++It;
		}
	}
}

void CClientIndicator::UpdatePresence()
{
	const bool BcPresence = IsPresenceEnabled();
	const bool UcPresence = ShouldRunUcPresence();
	if(!BcPresence && !UcPresence)
	{
		if(m_WasPresenceEnabled || m_WasUcPresenceActive || m_Socket)
			StopPresence(true);
		SetPresenceBlockReason("presence update skipped: indicator disabled or client offline");
		m_WasPresenceEnabled = false;
		m_WasUcPresenceActive = false;
		return;
	}

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		if(m_WasPresenceEnabled || m_WasUcPresenceActive || m_Socket)
			StopPresence(true);
		SetPresenceBlockReason("presence update skipped: client offline");
		m_WasPresenceEnabled = false;
		m_WasUcPresenceActive = false;
		return;
	}

	EnsurePresenceSocket();
	if(NeedsBcPresenceUdp() && (!m_Socket || !m_HasServerAddr))
	{
		if(m_LastPresenceBlockReason.empty())
			SetPresenceBlockReason("presence update skipped: bc udp socket is not ready");
	}
	else if(UcPresence && (!m_Socket || !m_HasUcServerAddr))
	{
		if(m_LastPresenceBlockReason.empty())
			SetPresenceBlockReason("presence update skipped: uc udp socket is not ready");
	}

	const char *pEffectiveGameServer = EffectivePresenceServerAddress();
	if(pEffectiveGameServer[0] == '\0')
	{
		SetPresenceBlockReason("presence update skipped: current game server address is empty");
		m_WasPresenceEnabled = BcPresence;
		m_WasUcPresenceActive = UcPresence;
		return;
	}
	const std::string PreviousGameServer = m_PresenceCache.ServerAddress();
	const bool ServerChanged = m_PresenceCache.SetServerAddress(pEffectiveGameServer);
	if(ServerChanged)
	{
		const char *pFromServer = !PreviousGameServer.empty()
			? PreviousGameServer.c_str()
			: m_aPreviousPresenceServerForSwitch;
		NotifyPresenceServerChanged(pFromServer, pEffectiveGameServer);
		m_DeveloperClientIds.clear();
		m_ClientVersions.clear();
	}
	ClearPresenceBlockReason();

	if(BcPresence)
	{
		SyncLocalRegistrations(ServerChanged);
		if(m_RegisteredClientIds.empty())
			SyncLocalRegistrations(true);
	}

	if(BcPresence || UcPresence)
		ProcessIncomingPackets(false);

	if(UcPresence)
		PruneStaleUcPeers();

	if(m_UcPresenceMapReloadPending && UcPresence)
	{
		bool HasLocalClientId = false;
		for(const int LocalId : GameClient()->m_aLocalIds)
		{
			if(LocalId >= 0 && LocalId < MAX_CLIENTS)
			{
				HasLocalClientId = true;
				break;
			}
		}
		if(HasLocalClientId)
		{
			UpdateUcPresenceForLocalClients();
			m_UcPresenceMapReloadPending = false;
			m_LastHeartbeatTick = time_get();
			DebugLog("uc presence map reload: re-joined with new client ids");
		}
	}

	const int64_t Now = time_get();
	if(m_LastHeartbeatTick == 0 || Now - m_LastHeartbeatTick > time_freq() * 5)
	{
		if(UcPresence)
			UpdateUcPresenceForLocalClients();
		if(BcPresence)
		{
			if(m_RegisteredClientIds.empty())
				SyncLocalRegistrations(true);
			for(const int ClientId : m_RegisteredClientIds)
				SendBcPresencePacket(ClientId, BestClientIndicator::PACKET_HEARTBEAT);
		}
		m_LastHeartbeatTick = Now;
		DebugLogF("heartbeat tick sent bc=%d uc=%d clients=%llu", BcPresence ? 1 : 0, UcPresence ? 1 : 0, (unsigned long long)m_RegisteredClientIds.size());
	}

	if(g_Config.m_UcPresenceHttpHeartbeat)
	{
		const int64_t HttpHeartbeatIntervalSeconds = maximum(30, g_Config.m_UcPresenceHttpHeartbeatSeconds);
		if(m_LastHttpHeartbeatTick == 0 || Now - m_LastHttpHeartbeatTick > time_freq() * HttpHeartbeatIntervalSeconds)
		{
		for(const int ClientId : GameClient()->m_aLocalIds)
		{
			if(ClientId >= 0 && ClientId < MAX_CLIENTS)
				SendPresenceHttpEvent(ClientId, "heartbeat");
		}
			m_LastHttpHeartbeatTick = Now;
			DebugLogF("http heartbeat tick sent interval=%llds", (long long)HttpHeartbeatIntervalSeconds);
		}
	}

	m_WasPresenceEnabled = BcPresence;
	m_WasUcPresenceActive = UcPresence;
}

void CClientIndicator::FinishBrowserCacheRefresh()
{
	if(!m_pBrowserTask)
		return;
	json_value *pJson = m_pBrowserTask->ResultJson();
	if(!pJson)
	{
		DebugLog("browser request completed but JSON parsing returned null");
		return;
	}
	m_BrowserCache.Load(*pJson);
	json_value_free(pJson);
	DebugLogF("browser cache loaded %llu entries", (unsigned long long)m_BrowserCache.Players().size());
	ApplyBrowserSnapshot();
}

void CClientIndicator::ResetBrowserTask()
{
	if(m_pBrowserTask)
	{
		m_pBrowserTask->Abort();
		m_pBrowserTask = nullptr;
	}
}

void CClientIndicator::RefreshUcPresenceList(bool Force)
{
	RefreshUcPresenceCache(Force);
}

void CClientIndicator::RefreshUcPresenceCache(bool Force)
{
	if(g_Config.m_UcPresenceApiBaseUrl[0] == '\0')
		return;

	const int64_t Now = time_get();
	const int64_t RefreshInterval = 300 * time_freq();
	if(!Force && m_LastUcPresenceRefreshTick != 0 && Now - m_LastUcPresenceRefreshTick < RefreshInterval)
		return;

	if(m_pUcPresenceTask && !m_pUcPresenceTask->Done())
	{
		if(!Force)
			return;
		ResetUcPresenceTask();
	}

	m_pUcPresenceTask = HttpGet(g_Config.m_UcPresenceApiBaseUrl);
	m_pUcPresenceTask->Timeout(CTimeout{10000, 0, 500, 5});
	m_pUcPresenceTask->IpResolve(IPRESOLVE::V4);
	m_pUcPresenceTask->LogProgress(HTTPLOG::FAILURE);
	m_LastUcPresenceRefreshTick = Now;
	Http()->Run(m_pUcPresenceTask);
}

void CClientIndicator::FinishUcPresenceRefresh()
{
	if(!m_pUcPresenceTask)
		return;
	json_value *pJson = m_pUcPresenceTask->ResultJson();
	if(pJson)
	{
		ParseUcPresenceList(pJson, m_UcPresenceByServer);
		json_value_free(pJson);
		InvalidateUcPresenceLookupCache();
		DebugLogF("uc presence list loaded for %llu servers", (unsigned long long)m_UcPresenceByServer.size());
	}
	ApplyBrowserSnapshot();
}

void CClientIndicator::ResetUcPresenceTask()
{
	if(m_pUcPresenceTask)
	{
		m_pUcPresenceTask->Abort();
		m_pUcPresenceTask = nullptr;
	}
}

bool CClientIndicator::IsPlayerUClient(int ClientId) const
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;

	for(const int LocalId : GameClient()->m_aLocalIds)
	{
		if(LocalId >= 0 && ClientId == LocalId && g_Config.m_UcInstallUuid[0] != '\0')
			return true;
	}

	const char *pPlayerName = PlayerNameForClient(ClientId);
	if(pPlayerName[0] == '\0')
		return false;

	char aCurrentServerAddress[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aCurrentServerAddress, sizeof(aCurrentServerAddress), true);
	if(aCurrentServerAddress[0] == '\0')
		return false;

	char aNormalizedServer[NETADDR_MAXSTRSIZE];
	if(!NormalizePresenceServerAddress(aCurrentServerAddress, aNormalizedServer, sizeof(aNormalizedServer)))
		return false;

	const std::unordered_map<int, SUcPeerInfo> *pPeers = m_pUcPeersOnCurrentServer;
	if(str_comp(m_aUcPeerLookupServer, aNormalizedServer) != 0)
	{
		const auto ItServer = m_UcPeersByServer.find(aNormalizedServer);
		if(ItServer == m_UcPeersByServer.end())
		{
			m_aUcPeerLookupServer[0] = '\0';
			m_pUcPeersOnCurrentServer = nullptr;
			pPeers = nullptr;
		}
		else
		{
			str_copy(m_aUcPeerLookupServer, aNormalizedServer, sizeof(m_aUcPeerLookupServer));
			m_pUcPeersOnCurrentServer = &ItServer->second;
			pPeers = m_pUcPeersOnCurrentServer;
		}
	}

	if(pPeers && ClientId >= 0)
	{
		const auto ItPeer = pPeers->find(ClientId);
		if(ItPeer != pPeers->end() && str_comp(ItPeer->second.m_PlayerName.c_str(), pPlayerName) == 0)
			return true;
	}

	// When live UDP peer data exists, trust only client_id + name matches above.
	// Name-only HTTP/browser fallbacks are too stale-prone on busy servers.
	const bool HasLiveUdpPeers = pPeers && !pPeers->empty();
	if(HasLiveUdpPeers)
		return false;

	const std::unordered_set<std::string> *pNames = m_pUcPresenceLookupNames;
	if(str_comp(m_aUcPresenceLookupServer, aNormalizedServer) != 0)
	{
		const auto ItServer = m_UcPresenceByServer.find(aNormalizedServer);
		if(ItServer == m_UcPresenceByServer.end())
		{
			m_aUcPresenceLookupServer[0] = '\0';
			m_pUcPresenceLookupNames = nullptr;
			pNames = nullptr;
		}
		else
		{
			str_copy(m_aUcPresenceLookupServer, aNormalizedServer, sizeof(m_aUcPresenceLookupServer));
			m_pUcPresenceLookupNames = &ItServer->second;
			pNames = m_pUcPresenceLookupNames;
		}
	}

	if(pNames && pNames->find(pPlayerName) != pNames->end())
		return true;

	// Fallback: browser snapshot flags when the HTTP list cache is stale.
	const IServerBrowser::CServerEntry *pCurrentServer = ServerBrowser()->Find(Client()->ServerAddress());
	if(!pCurrentServer)
		return false;

	const CServerInfo &Info = pCurrentServer->m_Info;
	for(int Index = 0; Index < minimum(Info.m_NumReceivedClients, (int)MAX_CLIENTS); ++Index)
	{
		const CServerInfo::CClient &Client = Info.m_aClients[Index];
		if(Client.m_UcClient && str_comp(Client.m_aName, pPlayerName) == 0)
			return true;
	}

	return false;
}

void CClientIndicator::FinishTokenRefresh()
{
	if(!m_pTokenTask)
		return;
	json_value *pJson = m_pTokenTask->ResultJson();
	char aOldEffectiveToken[sizeof(m_aWebSharedToken)];
	str_copy(aOldEffectiveToken, EffectiveSharedToken(), sizeof(aOldEffectiveToken));
	m_aWebSharedToken[0] = '\0';

	if(pJson)
	{
		const char *pToken = nullptr;
		if(pJson->type == json_object)
		{
			const json_value &Token = (*pJson)["token"];
			if(Token.type == json_string)
				pToken = Token.u.string.ptr;
		}

		if(pToken && pToken[0] != '\0')
			str_copy(m_aWebSharedToken, pToken, sizeof(m_aWebSharedToken));

		json_value_free(pJson);
	}
	else
	{
		DebugLog("token request completed but JSON parsing returned null");
	}

	if(m_aWebSharedToken[0] != '\0')
		DebugLogF("token refresh succeeded, web token length=%d", str_length(m_aWebSharedToken));
	else
		DebugLogF("token refresh produced empty web token, fallback token length=%d", str_length(g_Config.m_BcClientIndicatorSharedToken));

	if(str_comp(aOldEffectiveToken, EffectiveSharedToken()) != 0)
	{
		DebugLog("effective token changed, restarting presence state");
		StopPresence(true);
	}
}

void CClientIndicator::ResetTokenTask()
{
	if(m_pTokenTask)
	{
		m_pTokenTask->Abort();
		m_pTokenTask = nullptr;
	}
}

void CClientIndicator::ApplyBrowserSnapshot()
{
	// In-game nameplates read presence data directly; rebuilding the entire
	// server browser list here caused periodic hitches on large server lists.
	if(Client()->State() == IClient::STATE_ONLINE && !GameClient()->m_Menus.IsActive())
		return;

	ServerBrowser()->SetBestClientPlayers(m_BrowserCache.Players());
	ServerBrowser()->SetUcClientPlayers(m_UcPresenceByServer);
}

void CClientIndicator::SchedulePresenceBrowserRefresh()
{
	const int64_t Now = time_get();
	const int64_t Delay = time_freq() / 2;
	if(m_NextPresenceBrowserRefreshTick == 0 || m_NextPresenceBrowserRefreshTick > Now + Delay)
		m_NextPresenceBrowserRefreshTick = Now + Delay;
}

void CClientIndicator::ResetPresenceState()
{
	m_LastHeartbeatTick = 0;
	m_LastHttpHeartbeatTick = 0;
	m_LastPresenceStartAttempt = 0;
	m_NextPresenceBrowserRefreshTick = 0;
	m_WasPresenceEnabled = false;
	m_WasUcPresenceActive = false;
	m_UcPresenceMapReloadPending = false;
	m_RegisteredClientIds.clear();
	m_aUcLocalSlots = {};
	m_DeveloperClientIds.clear();
	m_ClientVersions.clear();
	m_PresenceCache.Clear();
	m_aLastGameServerAddr[0] = '\0';
	m_aLastBlockedGameServerAddr[0] = '\0';
	m_GameServerEmptySinceTick = 0;
	m_aLastKnownPresenceServerAddr[0] = '\0';
	m_aPreviousPresenceServerForSwitch[0] = '\0';
	m_LastPresenceBlockReason.clear();
}

void CClientIndicator::ResetTokenState()
{
	ResetTokenTask();
	m_aWebSharedToken[0] = '\0';
}

void CClientIndicator::ClearBrowserSnapshot()
{
	if(m_BrowserCache.Players().empty())
		return;
	m_BrowserCache.Clear();
	ApplyBrowserSnapshot();
}

void CClientIndicator::ReapplyBrowserSnapshot()
{
	ApplyBrowserSnapshot();
}

bool CClientIndicator::HasPendingNetworkTask() const
{
	return (m_pBrowserTask && !m_pBrowserTask->Done()) || (m_pTokenTask && !m_pTokenTask->Done());
}

bool CClientIndicator::IsBrowserSnapshotEnabled() const
{
	const CGameClient *pGameClient = GameClient();
	return g_Config.m_BcClientIndicator != 0 &&
	       (!pGameClient || !pGameClient->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_OTHERS_CLIENT_INDICATOR));
}

bool CClientIndicator::IsPresenceEnabled() const
{
	const CGameClient *pGameClient = GameClient();
	return g_Config.m_BcClientIndicator != 0 &&
	       Client()->State() == IClient::STATE_ONLINE &&
	       (!pGameClient || !pGameClient->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_OTHERS_CLIENT_INDICATOR));
}

bool CClientIndicator::ShouldRunUcPresence() const
{
	return Client()->State() == IClient::STATE_ONLINE && IsUcPresenceUdpEnabled();
}

bool CClientIndicator::IsUcPresenceUdpEnabled() const
{
	return g_Config.m_UcPresenceUdpServerAddress[0] != '\0' &&
	       g_Config.m_UcInstallUuid[0] != '\0' &&
	       g_Config.m_UcPresenceUdpSharedToken[0] != '\0';
}

bool CClientIndicator::NeedsBcPresenceUdp() const
{
	return g_Config.m_BcClientIndicatorSendInfo != 0 &&
	       g_Config.m_BcClientIndicatorServerAddress[0] != '\0' &&
	       EffectiveSharedToken()[0] != '\0';
}

bool CClientIndicator::NeedsAnyPresenceUdp() const
{
	return NeedsBcPresenceUdp() || IsUcPresenceUdpEnabled();
}

const char *CClientIndicator::EffectiveSharedToken() const
{
	if(m_aWebSharedToken[0] != '\0')
		return m_aWebSharedToken;
	return g_Config.m_BcClientIndicatorSharedToken;
}

void CClientIndicator::DebugLog(const char *pText) const
{
	if(!g_Config.m_DbgClientIndicator)
		return;
	log_info(LOG_SCOPE, "%s", pText);
}

void CClientIndicator::DebugLogF(const char *pFormat, ...) const
{
	if(!g_Config.m_DbgClientIndicator)
		return;
	char aBuf[1024];
	va_list Args;
	va_start(Args, pFormat);
	str_format_v(aBuf, sizeof(aBuf), pFormat, Args);
	va_end(Args);
	log_info(LOG_SCOPE, "%s", aBuf);
}

void CClientIndicator::SetPresenceBlockReason(const char *pReason)
{
	if(m_LastPresenceBlockReason == pReason)
		return;
	m_LastPresenceBlockReason = pReason;
	DebugLog(pReason);
}

void CClientIndicator::ClearPresenceBlockReason()
{
	m_LastPresenceBlockReason.clear();
}
