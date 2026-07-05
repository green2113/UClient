#include "uclient.h"

#include <base/str.h>
#include <base/vmath.h>

#include <engine/client/enums.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

namespace
{
constexpr float NTC_MAX_DISTANCE = 1280.0f;

const char *SkinCommandRest(const char *pInput)
{
	if(!pInput)
		return nullptr;

	const char *pRest = str_startswith_nocase(pInput, "/skin");
	if(!pRest)
		pRest = str_startswith_nocase(pInput, "!skin");
	if(!pRest)
		return nullptr;
	if(*pRest != '\0' && !str_isspace((unsigned char)*pRest))
		return nullptr;

	return str_utf8_skip_whitespaces(pRest);
}

void TrimTrailingWhitespace(char *pStr)
{
	if(!pStr)
		return;

	int Length = str_length(pStr);
	while(Length > 0 && str_isspace((unsigned char)pStr[Length - 1]))
		--Length;
	pStr[Length] = '\0';
}

bool ExtractSkinCommandName(const char *pRest, char *pName, int NameSize)
{
	if(!pRest || !pName || NameSize <= 0)
		return false;

	if(*pRest == '"')
	{
		++pRest;
		int Out = 0;
		for(; *pRest != '\0' && Out + 1 < NameSize; ++pRest)
		{
			if(*pRest == '"')
				break;
			pName[Out++] = *pRest;
		}
		pName[Out] = '\0';
		return pName[0] != '\0';
	}

	str_copy(pName, pRest, NameSize);
	TrimTrailingWhitespace(pName);
	return pName[0] != '\0';
}

void ApplySkinFromClient(CGameClient *pGameClient, const CGameClient::CClientData &Source)
{
	if(g_Config.m_ClDummy == 1)
	{
		str_copy(g_Config.m_ClDummySkin, Source.m_aSkinName, sizeof(g_Config.m_ClDummySkin));
		g_Config.m_ClDummyUseCustomColor = Source.m_UseCustomColor;
		g_Config.m_ClDummyColorBody = Source.m_ColorBody;
		g_Config.m_ClDummyColorFeet = Source.m_ColorFeet;
		pGameClient->SendDummyInfo(false);
	}
	else
	{
		str_copy(g_Config.m_ClPlayerSkin, Source.m_aSkinName, sizeof(g_Config.m_ClPlayerSkin));
		g_Config.m_ClPlayerUseCustomColor = Source.m_UseCustomColor;
		g_Config.m_ClPlayerColorBody = Source.m_ColorBody;
		g_Config.m_ClPlayerColorFeet = Source.m_ColorFeet;
		pGameClient->SendInfo(false);
	}
}

int FindClientIdByName(CGameClient *pGameClient, const char *pName)
{
	if(!pGameClient || !pName || pName[0] == '\0')
		return -1;

	int ExactMatch = -1;
	int CaseInsensitiveMatch = -1;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const CGameClient::CClientData &ClientData = pGameClient->m_aClients[ClientId];
		if(!ClientData.m_Active)
			continue;

		if(str_comp(ClientData.m_aName, pName) == 0)
		{
			ExactMatch = ClientId;
			break;
		}

		if(CaseInsensitiveMatch < 0 && str_comp_nocase(ClientData.m_aName, pName) == 0)
			CaseInsensitiveMatch = ClientId;
	}

	return ExactMatch >= 0 ? ExactMatch : CaseInsensitiveMatch;
}

bool IsNearbyPlayableClient(CGameClient *pGameClient, int ClientId, int LocalClientId, const vec2 &MyPos)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || ClientId == LocalClientId)
		return false;

	const CGameClient::CClientData &ClientData = pGameClient->m_aClients[ClientId];
	if(!ClientData.m_Active || !pGameClient->m_Snap.m_aCharacters[ClientId].m_Active)
		return false;

	const CNetObj_PlayerInfo *pInfo = pGameClient->m_Snap.m_apPlayerInfos[ClientId];
	if(pInfo && pInfo->m_Team == TEAM_SPECTATORS)
		return false;

	return distance(MyPos, ClientData.m_RenderPos) <= NTC_MAX_DISTANCE;
}
} // namespace

bool CUClient::ChatDoSkin(const char *pInput)
{
	const char *pRest = SkinCommandRest(pInput);
	if(!pRest)
		return false;

	char aName[MAX_NAME_LENGTH];
	if(!ExtractSkinCommandName(pRest, aName, sizeof(aName)))
	{
		GameClient()->Echo("Usage: /skin <player name>");
		return true;
	}

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		GameClient()->Echo("You must be online to use /skin.");
		return true;
	}

	const int ClientId = FindClientIdByName(GameClient(), aName);
	if(ClientId < 0)
	{
		GameClient()->Echo("Player not found.");
		return true;
	}

	const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
	ApplySkinFromClient(GameClient(), ClientData);

	char aMessage[256];
	str_format(aMessage, sizeof(aMessage), "Applied skin from '%s'.", ClientData.m_aName);
	GameClient()->Echo(aMessage);
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
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(!IsNearbyPlayableClient(pSelf->GameClient(), ClientId, LocalClientId, MyPos))
			continue;

		const float Dist = distance(MyPos, pSelf->GameClient()->m_aClients[ClientId].m_RenderPos);
		if(ClosestId < 0 || Dist < ClosestDist)
		{
			ClosestDist = Dist;
			ClosestId = ClientId;
		}
	}

	if(ClosestId < 0)
	{
		pSelf->GameClient()->Echo("No nearby player found.");
		return;
	}

	const CGameClient::CClientData &Target = pSelf->GameClient()->m_aClients[ClosestId];
	ApplySkinFromClient(pSelf->GameClient(), Target);

	char aMessage[256];
	str_format(aMessage, sizeof(aMessage), "Copied skin from nearby player '%s'.", Target.m_aName);
	pSelf->GameClient()->Echo(aMessage);
}

void CUClient::OnConsoleInit()
{
	Console()->Register("ntc", "", CFGFLAG_CLIENT, ConNtc, this, "Copy skin and colors from the nearest nearby player");
}
