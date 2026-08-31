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
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <string>
#include <unordered_set>
#include <vector>

namespace UClientPresence
{
struct CPeerState;
struct CPeerList;
struct CReactionBroadcast;
struct CCursorBroadcast;
struct CChatBroadcast;
struct CRoomChatBroadcast;
struct CUClientReactionBroadcast;
struct CReadBroadcast;
struct CServerJoinBroadcast;
struct CRoomServerJoinBroadcast;
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
	void SendUClientChatReaction(const CUuid &MessageId, const char *pOriginalServerAddress, uint8_t Scope, const char *pRoomId, const char *pEmoji, bool Add);

	// UClient chat read receipts: broadcast that we have read up to the given message id.
	void SendChatReadMarker(const CUuid &MessageId, const char *pRoomId);

	// UClient server join/leave announcements: broadcast "we joined <server>" / "we left" to all
	// UClient users. Returns true if a packet was actually sent (socket ready, feature enabled).
	bool SendUClientServerAnnounce(uint8_t Kind, const char *pServerAddress);

	// UClient chat channel (cross-server by default; optional same-server scope).
	void SendUClientChat(const char *pMessage);
	// Reason why SendUClientChat would drop a message right now, or nullptr when it can send.
	const char *UClientChatUnavailableReason();
	bool UcPeerAppliesToCurrentServer(const char *pServerAddress) const;
	void CollectOnlineUClientNames(std::vector<std::string> &vNames) const;

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
	void ApplyUcRoomChatBroadcast(const UClientPresence::CRoomChatBroadcast &Chat);
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
	// Server we have announced a UClient join for and not yet announced a leave for. It
	// deliberately survives StopPresence/ResetPresenceState so a map change (which drops to
	// STATE_LOADING and back) neither re-announces a join nor emits a leave. It is only cleared
	// when the client actually goes offline, which is also when the leave is announced.
	char m_aUcLastAnnouncedJoinServer[NETADDR_MAXSTRSIZE] = "";
	// Going offline does not announce the leave right away: a direct server switch disconnects
	// first, so the announcement waits a moment to see whether we come back online elsewhere
	// (one "moved to" message), on the same server (silent reconnect) or not at all (leave).
	char m_aUcPendingLeaveServer[NETADDR_MAXSTRSIZE] = "";
	int64_t m_UcPendingLeaveTick = 0;
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
	// Per-tick cache for IsPlayerUClient (nameplates call this once per visible tee).
	mutable int m_UcClientLookupCacheTick = -1;
	mutable char m_aUcCachedNormalizedServer[NETADDR_MAXSTRSIZE] = "";
	mutable int8_t m_aUcClientLookupCache[MAX_CLIENTS];
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
	void ApplyUClientReactionBroadcast(const UClientPresence::CUClientReactionBroadcast &Reaction);
	void ApplyUcReadBroadcast(const UClientPresence::CReadBroadcast &Read);
	void ApplyUcServerJoinBroadcast(const UClientPresence::CServerJoinBroadcast &Join);
	void ApplyUcRoomServerJoinBroadcast(const UClientPresence::CRoomServerJoinBroadcast &Join);
	void BeginPendingUClientServerLeave();
	void FlushPendingUClientServerLeave(bool Force);
	void LocalSkinSnapshot(const char *&pSkinName, uint8_t &UseCustomColor, int32_t &ColorBody, int32_t &ColorFeet) const;
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
	void InvalidateUcClientLookupCache() const;
	void MarkUcLocalSlotJoined(int ClientId);
	bool EnsureCachedUcNormalizedServer(char *pOut, int OutSize) const;
};

#endif
