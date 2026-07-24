/* Copyright © 2026 UClient contributors */
#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_SETTINGS_LINK_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_SETTINGS_LINK_H

#include <engine/shared/config.h>

#include <base/types.h>

#include <vector>

class IConfigManager;

namespace CUClientSettingsLink
{
constexpr int MAX_URI_LENGTH = 512;
constexpr int MAX_SCRIPT_NAME = 128;
constexpr int MAX_LABEL = 128;
constexpr int MAX_PARENTS = 4;
constexpr int MAX_PAGE_TOKEN = 64;
constexpr int MAX_TAB_TOKEN = 64;

enum class EKind
{
	NONE,
	VAR,
	PAGE,
};

struct SParsed
{
	EKind m_Kind = EKind::NONE;
	char m_aScriptName[MAX_SCRIPT_NAME] = "";
	char m_aLabel[MAX_LABEL] = "";
	char m_aPage[MAX_PAGE_TOKEN] = "";
	char m_aTab[MAX_TAB_TOKEN] = "";
	char m_aaParents[MAX_PARENTS][MAX_SCRIPT_NAME] = {};
	char m_aaParentLabels[MAX_PARENTS][MAX_LABEL] = {};
	int m_NumParents = 0;
};

struct SNavigateRequest
{
	bool m_Valid = false;
	int m_SettingsPage = -1; // CMenus::SETTINGS_*
	int m_BestClientTab = -1;
	int m_TClientTab = -1;
	int m_UClientTab = -1;
	int m_AssetsTab = -1;
	int m_ControlsMode = -1; // 0 = Action list, 1 = Keyboard
	char m_aHighlightScript[MAX_SCRIPT_NAME] = "";
	bool m_Highlight = false;
	char m_aaParents[MAX_PARENTS][MAX_SCRIPT_NAME] = {};
	int m_NumParents = 0;
};

bool IsSettingsLinkUri(const char *pText);
bool TryParse(const char *pUri, SParsed &Out);
bool BuildVarUri(char *pOut, int OutSize, const char *pScriptName, const char *pLabel, const char *pPage, const char *pTab, const char *const *ppParents, int NumParents);
bool BuildPageUri(char *pOut, int OutSize, const char *pPage, const char *pTab);

// Extract first settings:// URI from a chat line; returns offset/length into pText.
bool FindUriInText(const char *pText, int &OutStart, int &OutLength, char *pUriOut, int UriOutSize);
// Remove the URI from display text (collapse surrounding whitespace).
void StripUriFromDisplay(const char *pText, int UriStart, int UriLength, char *pOut, int OutSize);

const SConfigVariable *FindVariable(IConfigManager *pConfigManager, const char *pScriptName);
const SConfigVariable *FindVariableByPointer(IConfigManager *pConfigManager, const void *pVariable);

bool IsGraphicsAllowlisted(const char *pScriptName);
bool PageAllowsVarLinks(int SettingsPage); // false for Tee / Configs

const char *PageTokenFromSettingsPage(int SettingsPage);
int SettingsPageFromPageToken(const char *pPageToken);
const char *BestClientTabToken(int Tab);
int BestClientTabFromToken(const char *pTab);
const char *TClientTabToken(int Tab);
int TClientTabFromToken(const char *pTab);
const char *UClientTabToken(int Tab);
int UClientTabFromToken(const char *pTab);
const char *AssetsTabToken(int Tab);
int AssetsTabFromToken(const char *pTab);
const char *ControlsModeToken(int Mode);
int ControlsModeFromToken(const char *pTab);

// Local client map: filled while rendering settings UI (this client's layout/version).
struct SVarLocation
{
	char m_aPage[MAX_PAGE_TOKEN] = "";
	char m_aTab[MAX_TAB_TOKEN] = "";
	char m_aLabel[MAX_LABEL] = "";
	char m_aaParents[MAX_PARENTS][MAX_SCRIPT_NAME] = {};
	int m_NumParents = 0;
};

void RegisterVarLocation(const char *pScriptName, const char *pLabel, const char *pPage, const char *pTab, const char *const *ppParents, int NumParents);
bool LookupVarLocation(const char *pScriptName, SVarLocation &Out);

// Prefer local LookupVarLocation for VAR links; fall back to URI page/tab.
void FormatBreadcrumb(const SParsed &Parsed, char *pOut, int OutSize);
bool BuildNavigateRequest(const SParsed &Parsed, SNavigateRequest &Out);
}

#endif
