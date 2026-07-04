#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_CHAT_NEARBY_TAB_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_CHAT_NEARBY_TAB_H

#include <engine/shared/protocol.h>

class CGameClient;

// Ctrl+Tab in chat: cycle through player names sorted by distance (all DDRace teams).
class CUClientChatNearbyTab
{
public:
	struct SEntry
	{
		int m_ClientId = -1;
		int m_Score = 0;
	};

	static void BuildCompletionList(CGameClient *pGameClient, SEntry *pOut, int &OutLength, int MaxOut);
};

#endif
