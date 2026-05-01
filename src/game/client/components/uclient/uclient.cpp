#include "uclient.h"

#include <base/str.h>

#include <engine/client/enums.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

bool CUClient::ChatDoSkin(const char *pInput)
{
	if(!pInput)
		return false;

	const char *pName = str_startswith_nocase(pInput, "!skin");
	if(!pName)
		return false;
	if(*pName != '\0' && !str_isspace(*pName))
		return false;

	while(*pName != '\0' && str_isspace(*pName))
		++pName;

	if(*pName == '\0')
	{
		GameClient()->Echo("Usage: !skin <player name>");
		return true;
	}

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		GameClient()->Echo("You must be online to use !skin.");
		return true;
	}

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active)
			continue;
		if(str_comp_nocase(ClientData.m_aName, pName) != 0)
			continue;

		str_copy(g_Config.m_ClPlayerSkin, ClientData.m_aSkinName, sizeof(g_Config.m_ClPlayerSkin));
		if(ClientData.m_UseCustomColor != 0)
		{
			g_Config.m_ClPlayerUseCustomColor = ClientData.m_UseCustomColor;
			g_Config.m_ClPlayerColorBody = ClientData.m_ColorBody;
			g_Config.m_ClPlayerColorFeet = ClientData.m_ColorFeet;
		}
		GameClient()->SendInfo(false);

		char aMessage[256];
		str_format(aMessage, sizeof(aMessage), "Applied skin from '%s'.", ClientData.m_aName);
		GameClient()->Echo(aMessage);
		return true;
	}

	GameClient()->Echo("Player not found.");
	return true;
}
