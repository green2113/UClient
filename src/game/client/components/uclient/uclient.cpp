#include "uclient.h"

#include <base/str.h>
#include <base/vmath.h>

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

void CUClient::ConNtc(IConsole::IResult *pResult, void *pUserData)
{
	CUClient *pSelf = static_cast<CUClient *>(pUserData);

	if(pSelf->Client()->State() != IClient::STATE_ONLINE)
	{
		pSelf->GameClient()->Echo("You must be online to use ntc.");
		return;
	}

	const int LocalClientId = pSelf->GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId < 0)
	{
		pSelf->GameClient()->Echo("No local character found.");
		return;
	}

	const vec2 MyPos = pSelf->GameClient()->m_aClients[LocalClientId].m_RenderPos;

	float ClosestDist = -1.0f;
	int ClosestId = -1;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(ClientId == LocalClientId)
			continue;
		const CGameClient::CClientData &ClientData = pSelf->GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active)
			continue;
		const float Dist = distance(MyPos, ClientData.m_RenderPos);
		if(ClosestId < 0 || Dist < ClosestDist)
		{
			ClosestDist = Dist;
			ClosestId = ClientId;
		}
	}

	if(ClosestId < 0)
	{
		pSelf->GameClient()->Echo("No other players found.");
		return;
	}

	const CGameClient::CClientData &Target = pSelf->GameClient()->m_aClients[ClosestId];
	str_copy(g_Config.m_ClPlayerSkin, Target.m_aSkinName, sizeof(g_Config.m_ClPlayerSkin));
	g_Config.m_ClPlayerUseCustomColor = Target.m_UseCustomColor;
	if(Target.m_UseCustomColor != 0)
	{
		g_Config.m_ClPlayerColorBody = Target.m_ColorBody;
		g_Config.m_ClPlayerColorFeet = Target.m_ColorFeet;
	}
	pSelf->GameClient()->SendInfo(false);

	char aMessage[256];
	str_format(aMessage, sizeof(aMessage), "Copied skin from nearest player '%s'.", Target.m_aName);
	pSelf->GameClient()->Echo(aMessage);
}

void CUClient::OnConsoleInit()
{
	Console()->Register("ntc", "", CFGFLAG_CLIENT, ConNtc, this, "Copy skin and colors from the nearest player (near tee copy)");
}
