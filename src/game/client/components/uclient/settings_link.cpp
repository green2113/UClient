/* Copyright © 2026 UClient contributors */
#include "settings_link.h"

#include <engine/config.h>
#include <engine/shared/config.h>

#include <base/system.h>

#include <game/localization.h>

#include <cstring>
#include <string>
#include <unordered_map>

namespace CUClientSettingsLink
{
namespace
{
void AppendEncoded(char *pOut, int OutSize, int &Pos, const char *pText)
{
	if(!pText)
		return;
	for(const unsigned char *p = (const unsigned char *)pText; *p; ++p)
	{
		if(Pos + 4 >= OutSize)
			return;
		const unsigned char C = *p;
		if((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' || C == '-' || C == '.')
			pOut[Pos++] = (char)C;
		else if(C == ' ')
		{
			pOut[Pos++] = '%';
			pOut[Pos++] = '2';
			pOut[Pos++] = '0';
		}
		else
		{
			static const char aHex[] = "0123456789ABCDEF";
			pOut[Pos++] = '%';
			pOut[Pos++] = aHex[(C >> 4) & 0xF];
			pOut[Pos++] = aHex[C & 0xF];
		}
	}
}

int HexNibble(char C)
{
	if(C >= '0' && C <= '9')
		return C - '0';
	if(C >= 'a' && C <= 'f')
		return C - 'a' + 10;
	if(C >= 'A' && C <= 'F')
		return C - 'A' + 10;
	return -1;
}

void UrlDecode(const char *pIn, char *pOut, int OutSize)
{
	if(!pOut || OutSize <= 0)
		return;
	int Pos = 0;
	for(int i = 0; pIn[i] && Pos + 1 < OutSize; ++i)
	{
		if(pIn[i] == '%' && HexNibble(pIn[i + 1]) >= 0 && HexNibble(pIn[i + 2]) >= 0)
		{
			pOut[Pos++] = (char)((HexNibble(pIn[i + 1]) << 4) | HexNibble(pIn[i + 2]));
			i += 2;
		}
		else if(pIn[i] == '+')
			pOut[Pos++] = ' ';
		else
			pOut[Pos++] = pIn[i];
	}
	pOut[Pos] = '\0';
}

bool ParseQuery(const char *pQuery, SParsed &Out)
{
	if(!pQuery || pQuery[0] == '\0')
		return true;
	const char *p = pQuery;
	while(*p)
	{
		while(*p == '&')
			++p;
		if(!*p)
			break;
		const char *pKeyStart = p;
		while(*p && *p != '=' && *p != '&')
			++p;
		char aKey[64];
		const int KeyLen = minimum((int)(p - pKeyStart), (int)sizeof(aKey) - 1);
		mem_copy(aKey, pKeyStart, KeyLen);
		aKey[KeyLen] = '\0';
		char aVal[MAX_LABEL] = "";
		if(*p == '=')
		{
			++p;
			const char *pValStart = p;
			while(*p && *p != '&')
				++p;
			// Encoded values are much longer than decoded ones; don't truncate mid-%XX.
			char aRaw[MAX_URI_LENGTH];
			const int ValLen = minimum((int)(p - pValStart), (int)sizeof(aRaw) - 1);
			mem_copy(aRaw, pValStart, ValLen);
			aRaw[ValLen] = '\0';
			UrlDecode(aRaw, aVal, sizeof(aVal));
			if(!str_utf8_check(aVal))
				aVal[0] = '\0';
		}
		if(str_comp(aKey, "label") == 0)
			str_copy(Out.m_aLabel, aVal, sizeof(Out.m_aLabel));
		else if(str_comp(aKey, "page") == 0)
			str_copy(Out.m_aPage, aVal, sizeof(Out.m_aPage));
		else if(str_comp(aKey, "tab") == 0)
			str_copy(Out.m_aTab, aVal, sizeof(Out.m_aTab));
		else if(str_comp(aKey, "p") == 0 && Out.m_NumParents < MAX_PARENTS)
		{
			str_copy(Out.m_aaParents[Out.m_NumParents], aVal, sizeof(Out.m_aaParents[0]));
			Out.m_aaParentLabels[Out.m_NumParents][0] = '\0';
			Out.m_NumParents++;
		}
		else if(str_comp(aKey, "pl") == 0 && Out.m_NumParents > 0)
		{
			str_copy(Out.m_aaParentLabels[Out.m_NumParents - 1], aVal, sizeof(Out.m_aaParentLabels[0]));
		}
	}
	return true;
}
} // namespace

bool IsSettingsLinkUri(const char *pText)
{
	return pText && str_startswith(pText, "settings://");
}

bool TryParse(const char *pUri, SParsed &Out)
{
	Out = {};
	if(!IsSettingsLinkUri(pUri))
		return false;

	const char *pPath = pUri + str_length("settings://");
	char aWork[MAX_URI_LENGTH];
	str_copy(aWork, pPath, sizeof(aWork));

	char *pQuery = (char *)str_find(aWork, "?");
	if(pQuery)
	{
		*pQuery = '\0';
		++pQuery;
	}

	if(str_startswith(aWork, "var/"))
	{
		Out.m_Kind = EKind::VAR;
		str_copy(Out.m_aScriptName, aWork + 4, sizeof(Out.m_aScriptName));
		if(Out.m_aScriptName[0] == '\0')
			return false;
		ParseQuery(pQuery, Out);
		return true;
	}
	if(str_startswith(aWork, "page/"))
	{
		Out.m_Kind = EKind::PAGE;
		const char *pRest = aWork + 5;
		char aPagePath[MAX_URI_LENGTH];
		str_copy(aPagePath, pRest, sizeof(aPagePath));
		char *pSlash = (char *)str_find(aPagePath, "/");
		if(pSlash)
		{
			*pSlash = '\0';
			str_copy(Out.m_aPage, aPagePath, sizeof(Out.m_aPage));
			str_copy(Out.m_aTab, pSlash + 1, sizeof(Out.m_aTab));
		}
		else
		{
			str_copy(Out.m_aPage, aPagePath, sizeof(Out.m_aPage));
		}
		if(Out.m_aPage[0] == '\0')
			return false;
		ParseQuery(pQuery, Out);
		return true;
	}
	return false;
}

bool BuildVarUri(char *pOut, int OutSize, const char *pScriptName, const char *pLabel, const char *pPage, const char *pTab, const char *const *ppParents, int NumParents)
{
	if(!pOut || OutSize <= 0 || !pScriptName || pScriptName[0] == '\0')
		return false;
	int Pos = 0;
	const char *pPrefix = "settings://var/";
	for(const char *p = pPrefix; *p && Pos + 1 < OutSize; ++p)
		pOut[Pos++] = *p;
	AppendEncoded(pOut, OutSize, Pos, pScriptName);

	bool First = true;
	auto AddQuery = [&](const char *pKey, const char *pVal) {
		if(!pVal || pVal[0] == '\0' || Pos + 2 >= OutSize)
			return;
		pOut[Pos++] = First ? '?' : '&';
		First = false;
		AppendEncoded(pOut, OutSize, Pos, pKey);
		if(Pos + 1 < OutSize)
			pOut[Pos++] = '=';
		AppendEncoded(pOut, OutSize, Pos, pVal);
	};

	AddQuery("label", pLabel);
	AddQuery("page", pPage);
	AddQuery("tab", pTab);
	for(int i = 0; i < NumParents; ++i)
	{
		if(ppParents && ppParents[i] && ppParents[i][0] != '\0')
			AddQuery("p", ppParents[i]);
	}
	pOut[minimum(Pos, OutSize - 1)] = '\0';
	return true;
}

bool BuildPageUri(char *pOut, int OutSize, const char *pPage, const char *pTab)
{
	if(!pOut || OutSize <= 0 || !pPage || pPage[0] == '\0')
		return false;
	if(pTab && pTab[0] != '\0')
		str_format(pOut, OutSize, "settings://page/%s/%s", pPage, pTab);
	else
		str_format(pOut, OutSize, "settings://page/%s", pPage);
	return true;
}

bool FindUriInText(const char *pText, int &OutStart, int &OutLength, char *pUriOut, int UriOutSize)
{
	OutStart = -1;
	OutLength = 0;
	if(!pText)
		return false;
	const char *pFound = str_find(pText, "settings://");
	if(!pFound)
		return false;
	OutStart = (int)(pFound - pText);
	int Len = 0;
	while(pFound[Len] && pFound[Len] > ' ' && pFound[Len] != '|' && pFound[Len] != ')')
		++Len;
	OutLength = Len;
	if(pUriOut && UriOutSize > 0)
	{
		const int Copy = minimum(Len, UriOutSize - 1);
		mem_copy(pUriOut, pFound, Copy);
		pUriOut[Copy] = '\0';
	}
	return Len > 0;
}

void StripUriFromDisplay(const char *pText, int UriStart, int UriLength, char *pOut, int OutSize)
{
	if(!pOut || OutSize <= 0)
		return;
	if(!pText || UriStart < 0 || UriLength <= 0)
	{
		str_copy(pOut, pText ? pText : "", OutSize);
		return;
	}
	char aBuf[2048];
	const int Before = UriStart;
	const int After = UriStart + UriLength;
	int Pos = 0;
	for(int i = 0; i < Before && pText[i] && Pos + 1 < (int)sizeof(aBuf); ++i)
		aBuf[Pos++] = pText[i];
	while(Pos > 0 && aBuf[Pos - 1] == ' ')
		--Pos;
	bool NeedSpace = Pos > 0;
	int j = After;
	while(pText[j] == ' ')
		++j;
	if(NeedSpace && pText[j])
		aBuf[Pos++] = ' ';
	for(; pText[j] && Pos + 1 < (int)sizeof(aBuf); ++j)
		aBuf[Pos++] = pText[j];
	aBuf[Pos] = '\0';
	str_copy(pOut, aBuf, OutSize);
}

const SConfigVariable *FindVariable(IConfigManager *pConfigManager, const char *pScriptName)
{
	if(!pConfigManager || !pScriptName || pScriptName[0] == '\0')
		return nullptr;
	return pConfigManager->FindVariable(pScriptName);
}

const SConfigVariable *FindVariableByPointer(IConfigManager *pConfigManager, const void *pVariable)
{
	if(!pConfigManager || !pVariable)
		return nullptr;
	return pConfigManager->FindVariableByPointer(pVariable);
}

bool IsGraphicsAllowlisted(const char *pScriptName)
{
	if(!pScriptName)
		return false;
	static const char *const apAllowed[] = {
		"gfx_vsync",
		"gfx_fsaa_samples",
		"gfx_high_detail",
		"gfx_refresh_rate",
		"cl_refresh_rate",
		"ui_color",
	};
	for(const char *pName : apAllowed)
	{
		if(str_comp(pScriptName, pName) == 0)
			return true;
	}
	return false;
}

bool PageAllowsVarLinks(int SettingsPage)
{
	// SETTINGS_TEE = 3, SETTINGS_CONFIGS = 14 (see menus.h)
	return SettingsPage != 3 && SettingsPage != 14 && SettingsPage != 0; // Tee, Configs, Language
}

const char *PageTokenFromSettingsPage(int SettingsPage)
{
	switch(SettingsPage)
	{
	case 1: return "General";
	case 3: return "Tee";
	case 4: return "Appearance";
	case 5: return "Controls";
	case 6: return "Graphics";
	case 7: return "Sound";
	case 8: return "DDNet";
	case 9: return "Assets";
	case 10: return "TClient";
	case 11: return "BestClient";
	case 12: return "UClient";
	case 13: return "Profiles";
	case 14: return "Configs";
	default: return "";
	}
}

int SettingsPageFromPageToken(const char *pPageToken)
{
	if(!pPageToken)
		return -1;
	if(str_comp_nocase(pPageToken, "General") == 0) return 1;
	if(str_comp_nocase(pPageToken, "Tee") == 0) return 3;
	if(str_comp_nocase(pPageToken, "Appearance") == 0 || str_comp_nocase(pPageToken, "Display") == 0) return 4;
	if(str_comp_nocase(pPageToken, "Controls") == 0) return 5;
	if(str_comp_nocase(pPageToken, "Graphics") == 0) return 6;
	if(str_comp_nocase(pPageToken, "Sound") == 0) return 7;
	if(str_comp_nocase(pPageToken, "DDNet") == 0) return 8;
	if(str_comp_nocase(pPageToken, "Assets") == 0) return 9;
	if(str_comp_nocase(pPageToken, "TClient") == 0) return 10;
	if(str_comp_nocase(pPageToken, "BestClient") == 0) return 11;
	if(str_comp_nocase(pPageToken, "UClient") == 0) return 12;
	if(str_comp_nocase(pPageToken, "Profiles") == 0) return 13;
	if(str_comp_nocase(pPageToken, "Configs") == 0) return 14;
	return -1;
}

const char *BestClientTabToken(int Tab)
{
	switch(Tab)
	{
	case 0: return "Visuals";
	case 1: return "Gameplay";
	case 2: return "Others";
	case 3: return "Fun";
	case 4: return "Info";
	default: return "";
	}
}

int BestClientTabFromToken(const char *pTab)
{
	if(!pTab) return -1;
	if(str_comp_nocase(pTab, "Visuals") == 0) return 0;
	if(str_comp_nocase(pTab, "Gameplay") == 0 || str_comp_nocase(pTab, "Game_Play") == 0) return 1;
	if(str_comp_nocase(pTab, "Others") == 0) return 2;
	if(str_comp_nocase(pTab, "Fun") == 0) return 3;
	if(str_comp_nocase(pTab, "Info") == 0) return 4;
	return -1;
}

const char *TClientTabToken(int Tab)
{
	switch(Tab)
	{
	case 0: return "Settings";
	case 1: return "BindWheel";
	case 2: return "WarList";
	case 3: return "BindChat";
	case 4: return "StatusBar";
	case 5: return "Info";
	default: return "";
	}
}

int TClientTabFromToken(const char *pTab)
{
	if(!pTab) return -1;
	if(str_comp_nocase(pTab, "Settings") == 0) return 0;
	if(str_comp_nocase(pTab, "BindWheel") == 0) return 1;
	if(str_comp_nocase(pTab, "WarList") == 0) return 2;
	if(str_comp_nocase(pTab, "BindChat") == 0) return 3;
	if(str_comp_nocase(pTab, "StatusBar") == 0) return 4;
	if(str_comp_nocase(pTab, "Info") == 0) return 5;
	return -1;
}

const char *UClientTabToken(int Tab)
{
	switch(Tab)
	{
	case 0: return "Gameplay";
	case 1: return "Others";
	default: return "";
	}
}

int UClientTabFromToken(const char *pTab)
{
	if(!pTab) return -1;
	if(str_comp_nocase(pTab, "Gameplay") == 0 || str_comp_nocase(pTab, "Game_Play") == 0) return 0;
	if(str_comp_nocase(pTab, "Others") == 0) return 1;
	return -1;
}

const char *AssetsTabToken(int Tab)
{
	switch(Tab)
	{
	case 0: return "Entities";
	case 1: return "Game";
	case 2: return "Emoticons";
	case 3: return "Particles";
	case 4: return "HUD";
	case 5: return "Extras";
	case 6: return "Cursor";
	case 7: return "Arrow";
	case 8: return "Audio";
	default: return "";
	}
}

int AssetsTabFromToken(const char *pTab)
{
	if(!pTab) return -1;
	if(str_comp_nocase(pTab, "Entities") == 0) return 0;
	if(str_comp_nocase(pTab, "Game") == 0) return 1;
	if(str_comp_nocase(pTab, "Emoticons") == 0) return 2;
	if(str_comp_nocase(pTab, "Particles") == 0) return 3;
	if(str_comp_nocase(pTab, "HUD") == 0) return 4;
	if(str_comp_nocase(pTab, "Extras") == 0) return 5;
	if(str_comp_nocase(pTab, "Cursor") == 0) return 6;
	if(str_comp_nocase(pTab, "Arrow") == 0) return 7;
	if(str_comp_nocase(pTab, "Audio") == 0) return 8;
	return -1;
}

const char *ControlsModeToken(int Mode)
{
	return Mode == 1 ? "Keyboard" : "ActionList";
}

int ControlsModeFromToken(const char *pTab)
{
	if(!pTab) return -1;
	if(str_comp_nocase(pTab, "Keyboard") == 0) return 1;
	if(str_comp_nocase(pTab, "ActionList") == 0 || str_comp_nocase(pTab, "Action_list") == 0) return 0;
	return -1;
}

namespace
{
std::unordered_map<std::string, SVarLocation> &VarLocationMap()
{
	static std::unordered_map<std::string, SVarLocation> s_Map;
	return s_Map;
}

const char *LocalizedPageToken(const char *pToken)
{
	if(!pToken || pToken[0] == '\0')
		return "";
	if(str_comp_nocase(pToken, "General") == 0)
		return Localize("General");
	if(str_comp_nocase(pToken, "Tee") == 0)
		return Localize("Tee");
	if(str_comp_nocase(pToken, "Appearance") == 0 || str_comp_nocase(pToken, "Display") == 0)
		return Localize("Appearance");
	if(str_comp_nocase(pToken, "Controls") == 0)
		return Localize("Controls");
	if(str_comp_nocase(pToken, "Graphics") == 0)
		return Localize("Graphics");
	if(str_comp_nocase(pToken, "Sound") == 0)
		return Localize("Sound");
	if(str_comp_nocase(pToken, "DDNet") == 0)
		return "DDNet";
	if(str_comp_nocase(pToken, "Assets") == 0)
		return Localize("Assets");
	if(str_comp_nocase(pToken, "TClient") == 0)
		return "TClient";
	if(str_comp_nocase(pToken, "BestClient") == 0)
		return "BestClient";
	if(str_comp_nocase(pToken, "UClient") == 0)
		return "UClient";
	if(str_comp_nocase(pToken, "Profiles") == 0)
		return Localize("Profiles");
	if(str_comp_nocase(pToken, "Configs") == 0)
		return Localize("Settings File");
	if(str_comp_nocase(pToken, "Browser") == 0)
		return Localize("Browser");
	return pToken;
}

const char *LocalizedTabToken(const char *pPage, const char *pTab)
{
	if(!pTab || pTab[0] == '\0')
		return "";
	if(str_comp_nocase(pTab, "ServerFilter") == 0 || str_comp_nocase(pTab, "Filter") == 0)
		return Localize("Server filter");
	if(str_comp_nocase(pTab, "Visuals") == 0)
		return Localize("Visuals");
	if(str_comp_nocase(pTab, "Gameplay") == 0 || str_comp_nocase(pTab, "Game_Play") == 0)
		return Localize("Gameplay");
	if(str_comp_nocase(pTab, "Others") == 0)
		return Localize("Others");
	if(str_comp_nocase(pTab, "Fun") == 0)
		return Localize("Fun");
	if(str_comp_nocase(pTab, "Info") == 0)
		return Localize("Info");
	if(str_comp_nocase(pTab, "Settings") == 0)
		return Localize("Settings");
	if(str_comp_nocase(pTab, "Keyboard") == 0)
		return Localize("Keyboard");
	if(str_comp_nocase(pTab, "ActionList") == 0 || str_comp_nocase(pTab, "Action_list") == 0)
		return Localize("Action list");
	if(str_comp_nocase(pTab, "Entities") == 0)
		return Localize("Entities");
	if(str_comp_nocase(pTab, "Game") == 0)
		return Localize("Game");
	if(str_comp_nocase(pTab, "Emoticons") == 0)
		return Localize("Emoticons");
	if(str_comp_nocase(pTab, "Particles") == 0)
		return Localize("Particles");
	if(str_comp_nocase(pTab, "HUD") == 0)
		return Localize("HUD");
	if(str_comp_nocase(pTab, "Extras") == 0)
		return Localize("Extras");
	if(str_comp_nocase(pTab, "Cursor") == 0)
		return Localize("Cursor");
	if(str_comp_nocase(pTab, "Arrow") == 0)
		return Localize("Arrow");
	if(str_comp_nocase(pTab, "Audio") == 0)
		return Localize("Audio");
	(void)pPage;
	return pTab;
}

void FormatPageTab(const char *pPage, const char *pTab, char *pOut, int OutSize)
{
	if(!pOut || OutSize <= 0)
		return;
	pOut[0] = '\0';
	const char *pPageName = LocalizedPageToken(pPage);
	if(!pPageName || pPageName[0] == '\0')
		return;
	const char *pTabName = LocalizedTabToken(pPage, pTab);
	if(pTabName && pTabName[0] != '\0')
		str_format(pOut, OutSize, "%s > %s", pPageName, pTabName);
	else
		str_copy(pOut, pPageName, OutSize);
}
} // namespace

void RegisterVarLocation(const char *pScriptName, const char *pLabel, const char *pPage, const char *pTab, const char *const *ppParents, int NumParents)
{
	if(!pScriptName || pScriptName[0] == '\0' || !pPage || pPage[0] == '\0')
		return;
	SVarLocation Loc;
	str_copy(Loc.m_aPage, pPage, sizeof(Loc.m_aPage));
	if(pTab)
		str_copy(Loc.m_aTab, pTab, sizeof(Loc.m_aTab));
	if(pLabel)
		str_copy(Loc.m_aLabel, pLabel, sizeof(Loc.m_aLabel));
	Loc.m_NumParents = 0;
	if(ppParents && NumParents > 0)
	{
		Loc.m_NumParents = minimum(NumParents, MAX_PARENTS);
		for(int i = 0; i < Loc.m_NumParents; ++i)
		{
			if(ppParents[i])
				str_copy(Loc.m_aaParents[i], ppParents[i], sizeof(Loc.m_aaParents[i]));
		}
	}
	VarLocationMap()[pScriptName] = Loc;
}

bool LookupVarLocation(const char *pScriptName, SVarLocation &Out)
{
	Out = {};
	if(!pScriptName || pScriptName[0] == '\0')
		return false;
	const auto It = VarLocationMap().find(pScriptName);
	if(It == VarLocationMap().end())
		return false;
	Out = It->second;
	return true;
}

const char *LookupVarLabel(const char *pScriptName)
{
	if(!pScriptName || pScriptName[0] == '\0')
		return nullptr;
	const auto It = VarLocationMap().find(pScriptName);
	if(It == VarLocationMap().end() || It->second.m_aLabel[0] == '\0' || !str_utf8_check(It->second.m_aLabel))
		return nullptr;
	return It->second.m_aLabel;
}

void FormatBreadcrumb(const SParsed &Parsed, char *pOut, int OutSize)
{
	if(!pOut || OutSize <= 0)
		return;
	pOut[0] = '\0';

	// Prefer this client's discovered location for VAR links (version-safe path).
	if(Parsed.m_Kind == EKind::VAR && Parsed.m_aScriptName[0] != '\0')
	{
		SVarLocation Loc;
		if(LookupVarLocation(Parsed.m_aScriptName, Loc))
		{
			FormatPageTab(Loc.m_aPage, Loc.m_aTab, pOut, OutSize);
			return;
		}
	}

	if(Parsed.m_aPage[0] == '\0')
		return;
	FormatPageTab(Parsed.m_aPage, Parsed.m_aTab, pOut, OutSize);
}

bool BuildNavigateRequest(const SParsed &Parsed, SNavigateRequest &Out)
{
	Out = {};
	const char *pPage = Parsed.m_aPage;
	const char *pTab = Parsed.m_aTab;

	// Local layout wins for VAR links when this client has seen the setting in its menus.
	SVarLocation Loc;
	if(Parsed.m_Kind == EKind::VAR && Parsed.m_aScriptName[0] != '\0' && LookupVarLocation(Parsed.m_aScriptName, Loc))
	{
		pPage = Loc.m_aPage;
		pTab = Loc.m_aTab;
	}

	if(pPage && str_comp_nocase(pPage, "Browser") == 0)
	{
		Out.m_Valid = true;
		Out.m_BrowserServerFilter = true;
		Out.m_SettingsPage = -1;
		if(Parsed.m_Kind == EKind::VAR && Parsed.m_aScriptName[0] != '\0')
		{
			str_copy(Out.m_aHighlightScript, Parsed.m_aScriptName, sizeof(Out.m_aHighlightScript));
			Out.m_Highlight = true;
		}
		return true;
	}

	Out.m_SettingsPage = SettingsPageFromPageToken(pPage);
	if(Out.m_SettingsPage < 0 && Parsed.m_Kind == EKind::VAR)
	{
		// Fall back: unknown page, still open settings if we have a script to highlight.
		Out.m_SettingsPage = 1; // General as last resort only when navigating with highlight
	}
	if(Out.m_SettingsPage < 0)
		return false;

	Out.m_Valid = true;
	if(Out.m_SettingsPage == 11) // BestClient
		Out.m_BestClientTab = BestClientTabFromToken(pTab);
	else if(Out.m_SettingsPage == 10)
		Out.m_TClientTab = TClientTabFromToken(pTab);
	else if(Out.m_SettingsPage == 12)
		Out.m_UClientTab = UClientTabFromToken(pTab);
	else if(Out.m_SettingsPage == 9)
		Out.m_AssetsTab = AssetsTabFromToken(pTab);
	else if(Out.m_SettingsPage == 5)
		Out.m_ControlsMode = ControlsModeFromToken(pTab);

	if(Parsed.m_Kind == EKind::VAR && Parsed.m_aScriptName[0] != '\0')
	{
		str_copy(Out.m_aHighlightScript, Parsed.m_aScriptName, sizeof(Out.m_aHighlightScript));
		Out.m_Highlight = true;

		// Prefer locally discovered parents; fall back to URI parents so nested
		// rows (e.g. AntiPing children) can be force-enabled on navigate.
		SVarLocation ParentLoc;
		if(LookupVarLocation(Parsed.m_aScriptName, ParentLoc) && ParentLoc.m_NumParents > 0)
		{
			Out.m_NumParents = ParentLoc.m_NumParents;
			for(int i = 0; i < ParentLoc.m_NumParents; ++i)
				str_copy(Out.m_aaParents[i], ParentLoc.m_aaParents[i], sizeof(Out.m_aaParents[i]));
		}
		else if(Parsed.m_NumParents > 0)
		{
			Out.m_NumParents = minimum(Parsed.m_NumParents, MAX_PARENTS);
			for(int i = 0; i < Out.m_NumParents; ++i)
				str_copy(Out.m_aaParents[i], Parsed.m_aaParents[i], sizeof(Out.m_aaParents[i]));
		}
	}
	return true;
}
} // namespace CUClientSettingsLink
