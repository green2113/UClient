/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CLIENTINDICATOR_CLIENT_INDICATOR_SYNC_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CLIENTINDICATOR_CLIENT_INDICATOR_SYNC_H

#include <engine/client/enums.h>
#include <engine/shared/protocol.h>

#include <array>
#include <vector>

namespace BestClientIndicatorClient
{
struct CLocalClientIdSnapshot
{
	std::array<bool, MAX_CLIENTS> m_aActive{};
	std::vector<int> m_vClientIds;

	bool Contains(int ClientId) const
	{
		return ClientId >= 0 && ClientId < MAX_CLIENTS && m_aActive[ClientId];
	}
};

struct SUcLocalSlotState
{
	int m_LastClientId = -1;
	bool m_Joined = false;
};

struct SUcLocalSlotTickResult
{
	bool m_SendLeave = false;
	int m_LeaveClientId = -1;
	bool m_SendJoin = false;
	int m_JoinClientId = -1;
	bool m_SendHeartbeat = false;
	int m_HeartbeatClientId = -1;
	SUcLocalSlotState m_NextState{};
};

inline SUcLocalSlotTickResult ComputeUcLocalSlotTick(const SUcLocalSlotState &State, int CurrentClientId)
{
	SUcLocalSlotTickResult Result;
	Result.m_NextState = State;

	if(CurrentClientId >= 0 && CurrentClientId < MAX_CLIENTS)
	{
		if(State.m_Joined && State.m_LastClientId >= 0 && State.m_LastClientId != CurrentClientId)
		{
			Result.m_SendLeave = true;
			Result.m_LeaveClientId = State.m_LastClientId;
			Result.m_SendJoin = true;
			Result.m_JoinClientId = CurrentClientId;
			Result.m_SendHeartbeat = true;
			Result.m_HeartbeatClientId = CurrentClientId;
			Result.m_NextState.m_Joined = true;
			Result.m_NextState.m_LastClientId = CurrentClientId;
		}
		else if(!State.m_Joined)
		{
			Result.m_SendJoin = true;
			Result.m_JoinClientId = CurrentClientId;
			Result.m_SendHeartbeat = true;
			Result.m_HeartbeatClientId = CurrentClientId;
			Result.m_NextState.m_Joined = true;
			Result.m_NextState.m_LastClientId = CurrentClientId;
		}
		else
		{
			Result.m_SendHeartbeat = true;
			Result.m_HeartbeatClientId = CurrentClientId;
			Result.m_NextState.m_LastClientId = CurrentClientId;
		}
	}
	else if(State.m_Joined && State.m_LastClientId >= 0)
	{
		Result.m_SendLeave = true;
		Result.m_LeaveClientId = State.m_LastClientId;
		Result.m_NextState.m_Joined = false;
		Result.m_NextState.m_LastClientId = -1;
	}

	return Result;
}

inline CLocalClientIdSnapshot CollectActiveLocalClientIds(const int (&aLocalIds)[NUM_DUMMIES], const std::array<bool, MAX_CLIENTS> &aClientActive)
{
	CLocalClientIdSnapshot Snapshot;
	Snapshot.m_vClientIds.reserve(NUM_DUMMIES);
	for(const int ClientId : aLocalIds)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || !aClientActive[ClientId] || Snapshot.m_aActive[ClientId])
			continue;
		Snapshot.m_aActive[ClientId] = true;
		Snapshot.m_vClientIds.push_back(ClientId);
	}
	return Snapshot;
}
}

#endif
