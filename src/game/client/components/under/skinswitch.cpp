#include "skinswitch.h"

#include <game/client/gameclient.h>
#include <generated/protocol.h>

#include <engine/console.h>
#include <engine/sqlite.h>
#include <sqlite3.h>

#include <base/system.h>

void CSkinswitch::OnConsoleInit()
{
	Console()->Register(
		"skin", "r[playername]", CFGFLAG_CLIENT | CFGFLAG_CHAT,
		ConSkinChange, this,
		"해당 플레이어와 같은 스킨으로 변경합니다");
}

void CSkinswitch::ConSkinChange(IConsole::IResult *pResult, void *pUserData)
{
	CSkinswitch *pSelf = static_cast<CSkinswitch *>(pUserData);
	const char *pTargetName = pResult->GetString(0);
	if(!pTargetName[0])
	{
		return;
	}

	CGameClient *pClient = pSelf->GameClient();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const char *pName = pClient->m_aClients[i].m_aName;
		const char *pSkinName = pClient->m_aClients[i].m_aSkinName;
		const int pSkinUseCustomColor = pClient->m_aClients[i].m_UseCustomColor;
		const int pSkinBody = pClient->m_aClients[i].m_ColorBody;
		const int pSkinFeet = pClient->m_aClients[i].m_ColorFeet;

		if(str_find_nocase(pName, pTargetName))
		{
			str_copy(pSelf->Config()->m_ClPlayerSkin, pSkinName, sizeof(pSelf->Config()->m_ClPlayerSkin));
			if(pSkinUseCustomColor == 0)
			{
				pSelf->Config()->m_ClPlayerUseCustomColor = pSkinUseCustomColor;
				pSelf->GameClient()->SendInfo(false);

				char aBuf[128];
				std::snprintf(aBuf, sizeof(aBuf), "newSkin='%s' useColor=%d",
					pSelf->Config()->m_ClPlayerSkin,
					pSelf->Config()->m_ClPlayerUseCustomColor);
				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "skinswitch", aBuf);

				return;
			}
			else if(pSkinUseCustomColor == 1)
			{
				pSelf->Config()->m_ClPlayerUseCustomColor = pSkinUseCustomColor;
				pSelf->Config()->m_ClPlayerColorBody = pSkinBody;
				pSelf->Config()->m_ClPlayerColorFeet = pSkinFeet;
				pSelf->GameClient()->SendInfo(false);

				char aBuf[128];
				std::snprintf(aBuf, sizeof(aBuf), "newSkin='%s' useColor=%d body=%d feet=%d",
					pSelf->Config()->m_ClPlayerSkin,
					pSelf->Config()->m_ClPlayerUseCustomColor,
					pSelf->Config()->m_ClPlayerColorBody,
					pSelf->Config()->m_ClPlayerColorFeet);

				pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "skinswitch", aBuf);

				return;
			}
		}
	}

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "skin",
		"플레이어를 찾을 수 없습니다.");
}

void CSkinswitch::ChangeSkinByName(const char *pTargetName)
{
	CGameClient *pGameClient = GameClient();

	int LocalID = pGameClient->m_Snap.m_LocalClientId;
	const char *pMyName = pGameClient->m_aClients[LocalID].m_aName;

	if(str_comp_nocase(pTargetName, pMyName) == 0)
	{
		str_copy(pGameClient->Config()->m_ClPlayerSkin,
			g_Config.m_ClSkinSwitchSkinName,
			sizeof(pGameClient->Config()->m_ClPlayerSkin));

		const int bodyColor = str_toint(g_Config.m_ClSkinSwitchBodyColor);
		const int feetColor = str_toint(g_Config.m_ClSkinSwitchFeetColor);

		const int UseColor = (bodyColor >= 1 && feetColor >= 1 && g_Config.m_UcSkinSwitchUseCustomColors) ? 1 : 0;
		Config()->m_ClPlayerUseCustomColor = UseColor;

		if(UseColor == 1)
		{
			Config()->m_ClPlayerColorBody = bodyColor;
			Config()->m_ClPlayerColorFeet = feetColor;
		}

		pGameClient->SendInfo(false);
		return;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CNetObj_PlayerInfo *pInfo = pGameClient->m_Snap.m_apPlayerInfos[i];
		if(!pInfo)
			continue;

		int clientId = pInfo->m_ClientId;

		const char *pName = pGameClient->m_aClients[clientId].m_aName;
		const char *pSkinName = pGameClient->m_aClients[clientId].m_aSkinName;
		const int pSkinUseCustomColor = pGameClient->m_aClients[clientId].m_UseCustomColor;
		const int pSkinBody = pGameClient->m_aClients[clientId].m_ColorBody;
		const int pSkinFeet = pGameClient->m_aClients[clientId].m_ColorFeet;


		if(str_comp_nocase(pName, pTargetName) == 0)
		{
			str_copy(pGameClient->Config()->m_ClPlayerSkin, pSkinName, sizeof(pGameClient->Config()->m_ClPlayerSkin));

			if(pSkinUseCustomColor == 0)
			{
				Config()->m_ClPlayerUseCustomColor = pSkinUseCustomColor;
				pGameClient->SendInfo(false);

				return;
			}
			else if(pSkinUseCustomColor == 1)
			{
				Config()->m_ClPlayerUseCustomColor = pSkinUseCustomColor;
				Config()->m_ClPlayerColorBody = pSkinBody;
				Config()->m_ClPlayerColorFeet = pSkinFeet;
				pGameClient->SendInfo(false);

				return;
			}
		}
	}

	char aDbPath[512];
	str_copy(aDbPath, "player.sqlite", sizeof(aDbPath));

	sqlite3 *db = nullptr;
	if(sqlite3_open(aDbPath, &db) == SQLITE_OK)
	{
		const char *pQuery = "SELECT skinName, custom_color, color_body, color_feet FROM playerData WHERE name = ? ORDER BY rowid DESC LIMIT 1";
		sqlite3_stmt *stmt = nullptr;
		if(sqlite3_prepare_v2(db, pQuery, -1, &stmt, 0) == SQLITE_OK)
		{
			sqlite3_bind_text(stmt, 1, pTargetName, -1, SQLITE_STATIC);

			if(sqlite3_step(stmt) == SQLITE_ROW)
			{
				const char *pSkinName = (const char *)sqlite3_column_text(stmt, 0);
				int useCustomColor = sqlite3_column_int(stmt, 1);
				int colorBody = sqlite3_column_int(stmt, 2);
				int colorFeet = sqlite3_column_int(stmt, 3);

				str_copy(pGameClient->Config()->m_ClPlayerSkin, pSkinName, sizeof(pGameClient->Config()->m_ClPlayerSkin));
				pGameClient->Config()->m_ClPlayerUseCustomColor = useCustomColor;
				pGameClient->Config()->m_ClPlayerColorBody = colorBody;
				pGameClient->Config()->m_ClPlayerColorFeet = colorFeet;
				pGameClient->SendInfo(false);

				char aBuf[128];
				std::snprintf(aBuf, sizeof(aBuf), "sqliteSkin='%s' useColor=%d body=%d feet=%d",
					pGameClient->Config()->m_ClPlayerSkin,
					pGameClient->Config()->m_ClPlayerUseCustomColor,
					pGameClient->Config()->m_ClPlayerColorBody,
					pGameClient->Config()->m_ClPlayerColorFeet);

				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "skinswitch", aBuf);
				sqlite3_finalize(stmt);
				sqlite3_close(db);
				return;
			}
			sqlite3_finalize(stmt);
		}
		sqlite3_close(db);
	}

	pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "skinswitch", "데이터베이스에서 해당 유저를 찾을 수 없습니다.");
}