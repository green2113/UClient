/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CLIENTINDICATOR_CLIENT_INDICATOR_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CLIENTINDICATOR_CLIENT_INDICATOR_H

#include "client_indicator_sync.h"
#include "../subsystem_runtime.h"
#include "browser_cache.h"
#include "presence_cache.h"

#include <base/net.h>
#include <base/vmath.h>

#include <engine/client/enums.h>
#include <engine/console.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <engine/shared/http.h>
#include <engine/shared/uuid_manager.h>

#include <array>
#include <memory>
#include <unordered_map>
#include <string>
#include <unordered_set>

namespace UClientPresence
{
struct CPeerState;
struct CPeerList;
struct CReactionBroadcast;
struct CCursorBroadcast;
struct CChatBroadcast;
}

class CClientIndicator : public CComponent
{
public:
	CClientIndicator();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnConsoleInit() override;
	void OnMapLoad() override;
	void OnReset() override;
	void OnUpdate() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnShutdown() override;

	bool IsPlayerBestClient(int ClientId) const;
	bool IsPlayerUClient(int ClientId) const;
	bool IsPlayerBClient(int ClientId) { return IsPlayerBestClient(ClientId); }
	bool IsPlayerDeveloper(int ClientId) const;
	bool GetPlayerVersionLabel(int ClientId, char *pVersion, int VersionSize) const;

	const std::unordered_map<std::string, std::unordered_map<std::string, std::string>> &AllPlayerVersions() const { return m_BrowserCache.PlayerVersionsByServer(); }

	void RefreshBrowserCache(bool Force);
	void RefreshToken(bool Force);
	void RefreshUcPresenceList(bool Force);
	void ReapplyBrowserSnapshot();

	// Chat reactions: send a reaction add/remove for a message authored by TargetClientId,
	// identified across clients by MessageHash. Relayed via the UClient presence UDP server.
	void SendChatReaction(int TargetClientId, uint64_t MessageHash, const char *pEmoji, bool Add);

	// UClient chat channel (cross-server by default; optional same-server scope).
	void SendUClientChat(const char *pMessage);
	bool UcPeerAppliesToCurrentServer(const char *pServerAddress) const;

	// Draws the "Sharing cursor" indicator. Called from the HUD after the timer/music player so
	// it is never hidden behind them (rendered on top, at the bottom-center of the screen).
	void RenderLiveCursorSharingIndicator();

private:
	// Live cursor sharing: while +live_cursor is held, the local aim cursor world position is
	// broadcast to UClient peers on the same server and rendered as their own gun-cursor sprite.
	static void ConLiveCursor(IConsole::IResult *pResult, void *pUserData);
	vec2 LocalCursorWorldPos() const;
	bool IsLiveCursorBlockedByPlayerSpectate() const;
	void SendLiveCursor(bool Active, vec2 WorldPos);
	void UpdateLiveCursorSend(bool UcPresence);
	void RenderLiveCursorIndicatorPill(const char *pText, const ColorRGBA &DotColor);
	void ApplyUcCursorBroadcast(const UClientPresence::CCursorBroadcast &Cursor);
	void ApplyUcChatBroadcast(const UClientPresence::CChatBroadcast &Chat);
	void PruneStaleRemoteCursors();

	bool m_LiveCursorActive = false;
	bool m_LiveCursorWasActive = false;
	int64_t m_LastLiveCursorSendTick = 0;

	struct SRemoteCursor
	{
		vec2 m_TargetPos = vec2(0.0f, 0.0f);
		vec2 m_RenderPos = vec2(0.0f, 0.0f);
		bool m_HasRenderPos = false;
		char m_aName[MAX_NAME_LENGTH] = "";
		int64_t m_LastUpdateTick = 0;
	};
	std::unordered_map<int, SRemoteCursor> m_RemoteCursors;

	NETSOCKET m_Socket = nullptr;
	NETADDR m_ServerAddr{};
	bool m_HasServerAddr = false;
	char m_aLastPresenceServerAddr[256] = "";
	NETADDR m_UcServerAddr{};
	bool m_HasUcServerAddr = false;
	char m_aLastUcPresenceServerAddr[256] = "";
	char m_aLastGameServerAddr[NETADDR_MAXSTRSIZE] = "";
	char m_aLastBlockedGameServerAddr[NETADDR_MAXSTRSIZE] = "";
	int64_t m_GameServerEmptySinceTick = 0;
	char m_aLastKnownPresenceServerAddr[NETADDR_MAXSTRSIZE] = "";
	char m_aPreviousPresenceServerForSwitch[NETADDR_MAXSTRSIZE] = "";
	bool m_WasPresenceEnabled = false;
	bool m_WasUcPresenceActive = false;
	bool m_UcPresenceMapReloadPending = false;
	ESubsystemRuntimeState m_RuntimeState = ESubsystemRuntimeState::DISABLED;
	int64_t m_LastHeartbeatTick = 0;
	int64_t m_LastHttpHeartbeatTick = 0;
	int64_t m_LastPresenceStartAttempt = 0;
	int64_t m_LastBrowserRefreshTick = 0;
	int64_t m_LastTokenRefreshTick = 0;
	int64_t m_NextPresenceBrowserRefreshTick = 0;
	int64_t m_LastPresencePollTick = 0;
	int64_t m_LastRegistrationSyncTick = 0;
	int64_t m_LastPerfReportTick = 0;
	int64_t m_LastUpdateCostTick = 0;
	int64_t m_MaxUpdateCostTick = 0;
	int64_t m_TotalUpdateCostTick = 0;
	int64_t m_UpdateSamples = 0;
	CUuid m_ClientInstanceId = UUID_ZEROED;
	std::unordered_set<int> m_RegisteredClientIds;
	std::array<BestClientIndicatorClient::SUcLocalSlotState, NUM_DUMMIES> m_aUcLocalSlots{};
	std::unordered_set<int> m_DeveloperClientIds;
	std::unordered_map<int, std::string> m_ClientVersions;
	CPresenceCache m_PresenceCache;

	std::shared_ptr<CHttpRequest> m_pBrowserTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pTokenTask = nullptr;
	std::shared_ptr<CHttpRequest> m_pUcPresenceTask = nullptr;
	int64_t m_LastUcPresenceRefreshTick = 0;
	std::unordered_map<std::string, std::unordered_set<std::string>> m_UcPresenceByServer;
	mutable char m_aUcPresenceLookupServer[NETADDR_MAXSTRSIZE] = "";
	mutable const std::unordered_set<std::string> *m_pUcPresenceLookupNames = nullptr;

	struct SUcPeerInfo
	{
		std::string m_PlayerName;
	};
	std::unordered_map<std::string, std::unordered_map<int, SUcPeerInfo>> m_UcPeersByServer;
	mutable char m_aUcPeerLookupServer[NETADDR_MAXSTRSIZE] = "";
	mutable const std::unordered_map<int, SUcPeerInfo> *m_pUcPeersOnCurrentServer = nullptr;
	CBrowserCache m_BrowserCache;
	char m_aWebSharedToken[256] = "";
	std::string m_LastPresenceBlockReason;

	void OpenPresenceSocket();
	void ClosePresenceSocket();
	void StopPresence(bool SendLeavePackets);
	void EnsurePresenceSocket();
	void EnsureUcPresenceSocket();
	void UpdatePresence();
	void ProcessIncomingPackets(bool Force = false);
	void ProcessUcPresencePacket(const unsigned char *pData, int DataSize);
	void ApplyUcPeerState(const UClientPresence::CPeerState &State);
	void ApplyUcPeerRemove(const UClientPresence::CPeerState &State);
	void ApplyUcPeerList(const UClientPresence::CPeerList &PeerList);
	void ApplyUcReactionBroadcast(const UClientPresence::CReactionBroadcast &Reaction);
	void ClearUcPeersForServer(const char *pNormalizedServer);
	void PruneStaleUcPeers();
	void SyncLocalRegistrations(bool Force = false);
	void SendPresencePacket(int ClientId, int PacketType);
	void SendUcPresenceUdpPacket(int ClientId, int PacketType, const char *pFromServer = nullptr);
	void SendPresenceHttpEvent(int ClientId, const char *pEventPath);
	void SendPresenceHttpSwitchEvent(int ClientId, const char *pFromServerAddress, const char *pToServerAddress);
	void PresenceHttpPlayerId(char *pBuffer, int BufferSize) const;
	void PresenceSessionIdForClient(int ClientId, char *pBuffer, int BufferSize) const;
	void SendDevAuthPacket(int ClientId);
	void SendVersionPacket(int ClientId);
	void SendLeaveForAll();
	void SendUcLeaveForAllLocalClients();
	void UpdateUcPresenceForLocalClients();
	void SendBcPresencePacket(int ClientId, int PacketType);
	void NotifyPresenceServerChanged(const char *pFromServer, const char *pToServer);
	const char *CurrentGameServerAddress();
	const char *EffectivePresenceServerAddress();
	const char *PlayerNameForClient(int ClientId) const;

	void FinishBrowserCacheRefresh();
	void ResetBrowserTask();
	void RefreshUcPresenceCache(bool Force);
	void FinishUcPresenceRefresh();
	void ResetUcPresenceTask();
	void FinishTokenRefresh();
	void ResetTokenTask();
	void ResetPresenceState();
	void ResetPresenceStateForMapReload();
	void NotifyUcPresenceMapReload();
	void ResetTokenState();
	void ClearBrowserSnapshot();
	void ApplyBrowserSnapshot();
	void SchedulePresenceBrowserRefresh();
	bool HasPendingNetworkTask() const;
	bool IsBrowserSnapshotEnabled() const;
	bool IsPresenceEnabled() const;
	bool ShouldRunUcPresence() const;
	bool IsUcPresenceUdpEnabled() const;
	bool NeedsBcPresenceUdp() const;
	bool NeedsAnyPresenceUdp() const;
	const char *EffectiveSharedToken() const;
	void DebugLog(const char *pText) const;
	[[gnu::format(printf, 2, 3)]]
	void DebugLogF(const char *pFormat, ...) const;
	void SetPresenceBlockReason(const char *pReason);
	void ClearPresenceBlockReason();
	void InvalidateUcPresenceLookupCache();
	void InvalidateUcPeerLookupCache();
};

#endif
