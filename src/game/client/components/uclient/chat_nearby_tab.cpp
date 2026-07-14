#include "chat_nearby_tab.h"

#include <algorithm>
#include <vector>

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/gameclient.h>

void CUClientChatNearbyTab::BuildCompletionList(CGameClient *pGameClient, SEntry *pOut, int &OutLength, int MaxOut)
{
	OutLength = 0;
	if(!pGameClient || !pOut || MaxOut <= 0)
		return;

	const int LocalId = pGameClient->m_Snap.m_LocalClientId;

	// Always sort by proximity to the camera center (what the player is actually
	// looking at), regardless of whether we are playing, spectating someone, or on
	// another team. Using the local player's own position breaks ordering while
	// spectating other players.
	// Render positions are used for the targets so other DDRace teams are included
	// (prediction skips other teams, so m_Predicted.m_Pos is unreliable for them).
	const vec2 Origin = pGameClient->m_Camera.m_Center;

	struct SPair
	{
		float m_Dist;
		int m_ClientId;
	};

	std::vector<SPair> vList;
	vList.reserve(MAX_CLIENTS);
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(ClientId == LocalId)
			continue;
		if(!pGameClient->m_aClients[ClientId].m_Active)
			continue;
		if(!pGameClient->m_Snap.m_aCharacters[ClientId].m_Active)
			continue;

		const CNetObj_PlayerInfo *pInfo = pGameClient->m_Snap.m_apPlayerInfos[ClientId];
		if(pInfo && pInfo->m_Team == TEAM_SPECTATORS)
			continue;

		const vec2 Pos = pGameClient->m_aClients[ClientId].m_RenderPos;
		const float Dx = Pos.x - Origin.x;
		const float Dy = Pos.y - Origin.y;
		vList.push_back({Dx * Dx + Dy * Dy, ClientId});
	}

	std::sort(vList.begin(), vList.end(), [](const SPair &a, const SPair &b) { return a.m_Dist < b.m_Dist; });
	for(const SPair &Entry : vList)
	{
		pOut[OutLength].m_ClientId = Entry.m_ClientId;
		pOut[OutLength].m_Score = (int)Entry.m_Dist;
		++OutLength;

		if(OutLength >= MaxOut)
			break;
	}
}
