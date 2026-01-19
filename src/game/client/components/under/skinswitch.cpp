#include "skinswitch.h"

#include <game/client/gameclient.h>
#include <generated/protocol.h>

#include <engine/console.h>
#include <engine/sqlite.h>
#include <sqlite3.h>

#include <base/system.h>

#include <vector>

void CSkinswitch::OnConsoleInit()
{
	Console()->Register(
		"skin", "r[playername]", CFGFLAG_CLIENT | CFGFLAG_CHAT,
		ConSkinChange, this,
		"해당 플레이어와 같은 스킨으로 변경합니다");
	Console()->Register(
		"skindb", "r[playername] ?s[date]", CFGFLAG_CLIENT | CFGFLAG_CHAT,
		ConSkinChangeDb, this,
		"데이터베이스에서 스킨을 불러옵니다");
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

void CSkinswitch::ConSkinChangeDb(IConsole::IResult *pResult, void *pUserData)
{
	CSkinswitch *pSelf = static_cast<CSkinswitch *>(pUserData);
	const char *pTargetName = pResult->GetString(0);
	if(!pTargetName[0])
	{
		return;
	}

	const char *pDate = pResult->NumArguments() > 1 ? pResult->GetString(1) : "";
	pSelf->ChangeSkinByDb(pTargetName, pDate);
}

struct SSkinDbRow
{
	char m_aDate[11];
	char m_aSkinName[64];
	int m_UseCustomColor;
	int m_ColorBody;
	int m_ColorFeet;
};

static void ApplySkinRow(CGameClient *pGameClient, const SSkinDbRow &Row)
{
	str_copy(pGameClient->Config()->m_ClPlayerSkin, Row.m_aSkinName, sizeof(pGameClient->Config()->m_ClPlayerSkin));
	pGameClient->Config()->m_ClPlayerUseCustomColor = Row.m_UseCustomColor;
	pGameClient->Config()->m_ClPlayerColorBody = Row.m_ColorBody;
	pGameClient->Config()->m_ClPlayerColorFeet = Row.m_ColorFeet;
	pGameClient->SendInfo(false);
}

static void RowFromStmt(SSkinDbRow &Row, sqlite3_stmt *pStmt)
{
	const char *pSkinName = (const char *)sqlite3_column_text(pStmt, 0);
	const char *pTimestamp = (const char *)sqlite3_column_text(pStmt, 4);
	int useCustomColor = sqlite3_column_int(pStmt, 1);
	int colorBody = sqlite3_column_int(pStmt, 2);
	int colorFeet = sqlite3_column_int(pStmt, 3);

	str_copy(Row.m_aSkinName, pSkinName ? pSkinName : "", sizeof(Row.m_aSkinName));
	if(pTimestamp && str_length(pTimestamp) >= 10)
	{
		str_copy(Row.m_aDate, pTimestamp, sizeof(Row.m_aDate));
		Row.m_aDate[10] = '\0';
	}
	else
	{
		str_copy(Row.m_aDate, pTimestamp ? pTimestamp : "", sizeof(Row.m_aDate));
	}
	Row.m_UseCustomColor = useCustomColor;
	Row.m_ColorBody = colorBody;
	Row.m_ColorFeet = colorFeet;
}

static bool ApplySkinFromDbLatest(IConsole *pConsole, IStorage *pStorage, CGameClient *pGameClient, const char *pTargetName)
{
	const char *pQuery = "SELECT skinName, custom_color, color_body, color_feet, timestamp FROM playerData WHERE name = ? ORDER BY timestamp DESC LIMIT 1";
	CSqlite pSqlite = SqliteOpen(pConsole, pStorage, "player.sqlite");
	if(!pSqlite)
		return false;

	sqlite3 *pSqliteRaw = pSqlite.get();
	CSqliteStmt pStmt = SqlitePrepare(pConsole, pSqliteRaw, pQuery);
	if(!pStmt)
		return false;

	bool Error = false;
	Error = Error || SQLITE_HANDLE_ERROR(sqlite3_bind_text(pStmt.get(), 1, pTargetName, -1, SQLITE_TRANSIENT)) != SQLITE_OK;
	int StepResult = Error ? SQLITE_ERROR : SQLITE_HANDLE_ERROR(sqlite3_step(pStmt.get()));
	if(!Error && StepResult == SQLITE_ROW)
	{
		SSkinDbRow Row{};
		RowFromStmt(Row, pStmt.get());
		ApplySkinRow(pGameClient, Row);

		char aBuf[128];
		std::snprintf(aBuf, sizeof(aBuf), "sqliteSkin='%s' useColor=%d body=%d feet=%d",
			pGameClient->Config()->m_ClPlayerSkin,
			pGameClient->Config()->m_ClPlayerUseCustomColor,
			pGameClient->Config()->m_ClPlayerColorBody,
			pGameClient->Config()->m_ClPlayerColorFeet);

		pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "skinswitch", aBuf);
		return true;
	}
	if(Error || (StepResult != SQLITE_DONE && StepResult != SQLITE_ROW))
	{
		pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "skinswitch", "스킨 DB 조회 중 오류가 발생했습니다.");
		return true;
	}
	return false;
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

	if(ApplySkinFromDbLatest(Console(), Storage(), pGameClient, pTargetName))
		return;

	pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "skinswitch", "데이터베이스에서 해당 유저를 찾을 수 없습니다.");
}

void CSkinswitch::ChangeSkinByDb(const char *pTargetName, const char *pDate)
{
	CGameClient *pGameClient = GameClient();
	IConsole *pConsole = Console();

	CSqlite pSqlite = SqliteOpen(pConsole, Storage(), "player.sqlite");
	if(!pSqlite)
	{
		pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "skinswitch", "데이터베이스를 열 수 없습니다.");
		return;
	}

	sqlite3 *pSqliteRaw = pSqlite.get();
	bool HasDate = pDate && pDate[0];
	const char *pQuery =
		HasDate ?
		"SELECT skinName, custom_color, color_body, color_feet, timestamp FROM playerData WHERE name = ? AND timestamp LIKE ? ORDER BY timestamp DESC LIMIT 1" :
		"SELECT skinName, custom_color, color_body, color_feet, timestamp FROM playerData WHERE name = ? ORDER BY timestamp DESC";

	CSqliteStmt pStmt = SqlitePrepare(pConsole, pSqliteRaw, pQuery);
	if(!pStmt)
	{
		pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "skinswitch", "스킨 DB 조회 중 오류가 발생했습니다.");
		return;
	}

	bool Error = false;
	Error = Error || SQLITE_HANDLE_ERROR(sqlite3_bind_text(pStmt.get(), 1, pTargetName, -1, SQLITE_TRANSIENT)) != SQLITE_OK;
	if(HasDate)
	{
		char aDateQuery[32];
		str_format(aDateQuery, sizeof(aDateQuery), "%s%%", pDate);
		Error = Error || SQLITE_HANDLE_ERROR(sqlite3_bind_text(pStmt.get(), 2, aDateQuery, -1, SQLITE_TRANSIENT)) != SQLITE_OK;
	}

	if(Error)
	{
		pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "skinswitch", "스킨 DB 조회 중 오류가 발생했습니다.");
		return;
	}

	std::vector<SSkinDbRow> vRows;
	while(true)
	{
		int StepResult = SQLITE_HANDLE_ERROR(sqlite3_step(pStmt.get()));
		if(StepResult == SQLITE_ROW)
		{
			SSkinDbRow Row{};
			RowFromStmt(Row, pStmt.get());
			vRows.push_back(Row);
		}
		else if(StepResult == SQLITE_DONE)
		{
			break;
		}
		else
		{
			pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "skinswitch", "스킨 DB 조회 중 오류가 발생했습니다.");
			return;
		}
	}

	if(vRows.empty())
	{
		pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "skinswitch", "데이터베이스에서 해당 유저를 찾을 수 없습니다.");
		return;
	}

	if(HasDate || vRows.size() == 1)
	{
		ApplySkinRow(pGameClient, vRows[0]);
		return;
	}

	for(const auto &Row : vRows)
	{
		char aBuf[160];
		str_format(aBuf, sizeof(aBuf), "%s %s", Row.m_aDate, Row.m_aSkinName);
		pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "skinswitch", aBuf);
	}
}
