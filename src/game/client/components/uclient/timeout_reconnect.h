#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_TIMEOUT_RECONNECT_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_TIMEOUT_RECONNECT_H

#include <base/types.h>

#include <game/client/component.h>

/**
 * After an abnormal exit, shows how long remains before the tee can be reclaimed.
 *
 * Live heartbeats are written while online. A clean disconnect/quit deletes the
 * session file. A crash leaves it behind; on the next process start that file is
 * snapshotted into memory once, and reconnecting to the same server shows a HUD
 * countdown. Heartbeat updates cannot erase that snapshot.
 */
class CTimeoutReconnect : public CComponent
{
	static constexpr const char *SESSION_PATH = "uclient/timeout_reconnect.session";
	static constexpr int DEFAULT_TIMEOUT_SEC = 100;
	static constexpr int64_t HEARTBEAT_INTERVAL_MS = 1000;
	static constexpr int64_t QUERY_DELAY_MS = 1500;

	char m_aServerAddr[NETADDR_MAXSTRSIZE] = "";
	int m_TimeoutSec = DEFAULT_TIMEOUT_SEC;
	bool m_TimeoutKnown = false;
	bool m_QuerySent = false;
	int64_t m_OnlineSinceTick = 0;
	int64_t m_LastHeartbeatTick = 0;
	int64_t m_LastHeartbeatUnix = 0;

	// Crash remnant from the previous process. Loaded once; never overwritten by heartbeats.
	bool m_CrashSnapshotLoaded = false;
	bool m_HasCrashSnapshot = false;
	char m_aCrashServerAddr[NETADDR_MAXSTRSIZE] = "";
	int64_t m_CrashLastUnix = 0;
	int m_CrashTimeoutSec = DEFAULT_TIMEOUT_SEC;

	// Same-process unnatural drop (no intentional leave). Frozen in memory on OFFLINE.
	bool m_HasMemoryDisconnect = false;
	char m_aMemoryServerAddr[NETADDR_MAXSTRSIZE] = "";
	int64_t m_MemoryDisconnectUnix = 0;
	int m_MemoryTimeoutSec = DEFAULT_TIMEOUT_SEC;

	char m_aPendingServerAddr[NETADDR_MAXSTRSIZE] = "";
	int64_t m_PendingUntilUnix = 0;
	bool m_ShowPending = false;

	bool m_IntentionalLeave = false;

	void CurrentServerAddr(char *pBuf, int BufSize) const;
	bool LoadSession(char *pServer, int ServerSize, int64_t *pLastUnix, int *pTimeoutSec) const;
	void SaveSession(const char *pServer, int64_t LastUnix, int TimeoutSec) const;
	void ClearSession() const;
	void LoadCrashSnapshotOnce();
	bool TryActivateFrom(const char *pServer, int64_t LastUnix, int TimeoutSec);
	void TryActivatePending();
	void BeginOnlineSession();
	void EndOnlineSession(bool ClearFile);
	void TrySendTimeoutQuery();
	void PersistHeartbeat();
	void UpdatePendingDisplay();
	void ClearPending();

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnInit() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnUpdate() override;

	void MarkIntentionalLeave();
	bool TryConsumeTimeoutSettingsMessage(int ClientId, const char *pMessage);
	int RemainingSeconds() const;
	bool ShouldShowHud() const;
	void RenderHud();
};

#endif
