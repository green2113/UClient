#include "chat_nearby_tab.h"

#include <algorithm>
#include <vector>

#include <base/vmath.h>

#include <game/client/gameclient.h>

void CUClientChatNearbyTab::BuildCompletionList(CGameClient *pGameClient, SEntry *pOut, int &OutLength, int MaxOut)
{
	OutLength = 0;
	if(!pGameClient || !pOut || MaxOut <= 0)
		return;

	const int LocalId = pGameClient->m_aLocalIds[0];
	const vec2 CamCenter = pGameClient->m_Camera.m_Center;

	struct SPair
	{
		float m_Dist;
		int m_ClientId;
	};

	std::vector<SPair> vList;
	vList.reserve(MAX_CLIENTS);
	for(const CNetObj_PlayerInfo *pInfo : pGameClient->m_Snap.m_apInfoByName)
	{
		if(!pInfo)
			continue;

		const int ClientId = pInfo->m_ClientId;
		if(ClientId == LocalId)
			continue;

		const vec2 Pos = pGameClient->m_aClients[ClientId].m_Predicted.m_Pos;
		const float Dx = Pos.x - CamCenter.x;
		const float Dy = Pos.y - CamCenter.y;
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
