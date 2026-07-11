#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_AI_CATALOG_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_AI_CATALOG_H

#include <string>
#include <vector>

class CConfig;
class CJsonStringWriter;

namespace CUClientAiCatalog
{
enum class ESettingType
{
	Bool,
	Int,
	Enum,
};

struct SEnumValue
{
	const char *m_pValue;
	const char *m_pLabel;
};

struct SSettingEntry
{
	const char *m_pId;
	const char *m_pCommand;
	ESettingType m_Type;
	int m_Min;
	int m_Max;
	const char *const *m_ppAliases;
	int m_AliasCount;
	const SEnumValue *m_pEnumValues;
	int m_EnumCount;
};

struct SProposedCommand
{
	char m_aOp[16] = "";
	char m_aCommand[64] = "";
	char m_aValue[256] = "";
	char m_aDisplay[320] = "";
	char m_aUndoLine[320] = "";
	bool m_Valid = false;
	char m_aError[128] = "";
};

const std::vector<SSettingEntry> &Settings();
const std::vector<const char *> &AllowedBindActions();
const std::vector<const char *> &CommonBindKeys();

const SSettingEntry *FindSettingByCommand(const char *pCommand);
const SSettingEntry *FindSettingById(const char *pId);
bool IsAllowedBindAction(const char *pAction);
bool IsAllowedBindKey(const char *pKey);

void WriteCatalogJson(CJsonStringWriter &Writer, const CConfig &Config);
std::string BuildCatalogJson(const CConfig &Config);

bool ReadCurrentSettingValue(const CConfig &Config, const SSettingEntry &Entry, char *pOut, int OutSize);
bool ValidateAndNormalizeCommand(const CConfig &Config, SProposedCommand &Cmd);
bool TryLocalFallback(const char *pMessage, std::vector<SProposedCommand> &vOut, char *pReply, int ReplySize);
}

#endif
