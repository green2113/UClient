#include "ai_catalog.h"

#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/jsonwriter.h>

#include <base/mem.h>

#include <cctype>
#include <cstring>
#include <iterator>

namespace CUClientAiCatalog
{
namespace
{
static const char *const s_apDyncamAliases[] = {"다이나믹 카메라", "다이나믹캠", "다이나믹카메라", "dynamic camera", "dyncam", "cl_dyncam"};
static const char *const s_apShowChatAliases[] = {"채팅 표시", "채팅보이기", "show chat", "cl_showchat"};
static const char *const s_apShowHudAliases[] = {"HUD 표시", "허드 표시", "show hud", "cl_showhud"};
static const char *const s_apNameplatesAliases[] = {"네임플레이트", "이름표", "nameplates", "cl_nameplates"};
static const char *const s_apLanguageAliases[] = {"게임 언어", "언어", "language", "cl_languagefile"};
static const char *const s_apChatReplyAliases[] = {"채팅 답장", "답장 기능", "chat reply", "uc_chat_reply"};

static const SEnumValue s_aLanguageEnums[] = {
	{"", "English"},
	{"korean", "Korean"},
	{"japanese", "Japanese"},
	{"chinese", "Chinese"},
	{"simplified_chinese", "Simplified Chinese"},
	{"traditional_chinese", "Traditional Chinese"},
	{"german", "German"},
	{"french", "French"},
	{"spanish", "Spanish"},
	{"russian", "Russian"},
};

static const char *const s_apAllowedBindActions[] = {
	"+fire",
	"+hook",
	"+jump",
	"+left",
	"+right",
	"+showhookcoll",
	"+spectate",
	"+scoreboard",
	"kill",
	"emote",
	"say",
	"say_team",
	"toggle",
};

static const char *const s_apCommonBindKeys[] = {
	"mouse1", "mouse2", "mouse3", "mouse4", "mouse5",
	"space", "return", "tab", "lshift", "rshift", "lctrl", "rctrl", "lalt", "ralt",
	"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
	"n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",
	"1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
	"f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12",
	"ctrl+a", "ctrl+b", "ctrl+c", "ctrl+d", "ctrl+e", "ctrl+f", "ctrl+g", "ctrl+h",
	"ctrl+i", "ctrl+j", "ctrl+k", "ctrl+l", "ctrl+m", "ctrl+n", "ctrl+o", "ctrl+p",
	"ctrl+q", "ctrl+r", "ctrl+s", "ctrl+t", "ctrl+u", "ctrl+v", "ctrl+w", "ctrl+x",
	"ctrl+y", "ctrl+z",
	"shift+a", "shift+b", "shift+c", "shift+d", "shift+e", "shift+f", "shift+g",
	"alt+a", "alt+b", "alt+c", "alt+d", "alt+e", "alt+f", "alt+g",
};

static bool ContainsInsensitive(const char *pHaystack, const char *pNeedle)
{
	if(!pHaystack || !pNeedle || pNeedle[0] == '\0')
		return false;
	const int HayLen = str_length(pHaystack);
	const int NeedleLen = str_length(pNeedle);
	if(NeedleLen > HayLen)
		return false;
	for(int i = 0; i <= HayLen - NeedleLen; ++i)
	{
		int j = 0;
		for(; j < NeedleLen; ++j)
		{
			const unsigned char A = (unsigned char)pHaystack[i + j];
			const unsigned char B = (unsigned char)pNeedle[j];
			if(std::tolower(A) != std::tolower(B))
				break;
		}
		if(j == NeedleLen)
			return true;
	}
	return false;
}

static void TrimInPlace(char *pStr)
{
	if(!pStr)
		return;
	char *pStart = pStr;
	while(*pStart == ' ' || *pStart == '\t')
		++pStart;
	if(pStart != pStr)
		mem_move(pStr, pStart, str_length(pStart) + 1);
	int Len = str_length(pStr);
	while(Len > 0 && (pStr[Len - 1] == ' ' || pStr[Len - 1] == '\t' || pStr[Len - 1] == '\r' || pStr[Len - 1] == '\n'))
	{
		pStr[Len - 1] = '\0';
		--Len;
	}
}

static bool ParseBindActionToken(const char *pToken, char *pNormalized, int NormalizedSize)
{
	if(!pToken || pToken[0] == '\0' || !pNormalized || NormalizedSize <= 0)
		return false;

	char aToken[256];
	str_copy(aToken, pToken, sizeof(aToken));
	TrimInPlace(aToken);
	if(aToken[0] == '\0')
		return false;

	if(str_startswith_nocase(aToken, "say "))
	{
		const char *pText = aToken + 4;
		while(*pText == ' ')
			++pText;
		if(str_length(pText) <= 0 || str_length(pText) > 96)
			return false;
		str_format(pNormalized, NormalizedSize, "say %s", pText);
		return true;
	}
	if(str_startswith_nocase(aToken, "say_team "))
	{
		const char *pText = aToken + 9;
		while(*pText == ' ')
			++pText;
		if(str_length(pText) <= 0 || str_length(pText) > 96)
			return false;
		str_format(pNormalized, NormalizedSize, "say_team %s", pText);
		return true;
	}
	if(str_startswith_nocase(aToken, "emote "))
	{
		const char *pNum = aToken + 6;
		while(*pNum == ' ')
			++pNum;
		if(!str_isallnum(pNum))
			return false;
		const int Emote = str_toint(pNum);
		if(Emote < 0 || Emote > 15)
			return false;
		str_format(pNormalized, NormalizedSize, "emote %d", Emote);
		return true;
	}
	if(str_startswith_nocase(aToken, "toggle "))
	{
		str_copy(pNormalized, aToken, NormalizedSize);
		return true;
	}

	for(const char *pAction : s_apAllowedBindActions)
	{
		if(str_comp_nocase(aToken, pAction) == 0)
		{
			str_copy(pNormalized, pAction, NormalizedSize);
			return true;
		}
	}
	return false;
}

static bool NormalizeBindValue(const char *pValue, char *pOut, int OutSize)
{
	if(!pValue || !pOut || OutSize <= 0)
		return false;
	pOut[0] = '\0';

	char aCopy[256];
	str_copy(aCopy, pValue, sizeof(aCopy));
	TrimInPlace(aCopy);
	if(aCopy[0] == '\0')
		return false;

	char aCombined[256] = "";
	const char *pCursor = aCopy;
	while(*pCursor)
	{
		while(*pCursor == ' ' || *pCursor == ';')
			++pCursor;
		if(*pCursor == '\0')
			break;

		char aPart[192];
		int PartLen = 0;
		while(pCursor[PartLen] != '\0' && pCursor[PartLen] != ';')
			++PartLen;
		if(PartLen >= (int)sizeof(aPart))
			return false;
		mem_copy(aPart, pCursor, PartLen);
		aPart[PartLen] = '\0';
		pCursor += PartLen;

		char aNormalized[192];
		if(!ParseBindActionToken(aPart, aNormalized, sizeof(aNormalized)))
			return false;

		if(aCombined[0] != '\0')
			str_append(aCombined, "; ", sizeof(aCombined));
		str_append(aCombined, aNormalized, sizeof(aCombined));
	}

	if(aCombined[0] == '\0')
		return false;
	str_copy(pOut, aCombined, OutSize);
	return true;
}

static void NormalizeKeyName(const char *pKey, char *pOut, int OutSize)
{
	str_copy(pOut, pKey ? pKey : "", OutSize);
	for(int i = 0; pOut[i]; ++i)
		pOut[i] = (char)std::tolower((unsigned char)pOut[i]);
	TrimInPlace(pOut);
}
}

const std::vector<SSettingEntry> &Settings()
{
	static const std::vector<SSettingEntry> s_vSettings = {
		{"dyncam", "cl_dyncam", ESettingType::Bool, 0, 1, s_apDyncamAliases, (int)std::size(s_apDyncamAliases), nullptr, 0},
		{"showchat", "cl_showchat", ESettingType::Bool, 0, 2, s_apShowChatAliases, (int)std::size(s_apShowChatAliases), nullptr, 0},
		{"showhud", "cl_showhud", ESettingType::Bool, 0, 1, s_apShowHudAliases, (int)std::size(s_apShowHudAliases), nullptr, 0},
		{"nameplates", "cl_nameplates", ESettingType::Bool, 0, 1, s_apNameplatesAliases, (int)std::size(s_apNameplatesAliases), nullptr, 0},
		{"language", "cl_languagefile", ESettingType::Enum, 0, 0, s_apLanguageAliases, (int)std::size(s_apLanguageAliases), s_aLanguageEnums, (int)std::size(s_aLanguageEnums)},
		{"chatreply", "uc_chat_reply", ESettingType::Bool, 0, 1, s_apChatReplyAliases, (int)std::size(s_apChatReplyAliases), nullptr, 0},
	};
	return s_vSettings;
}

const std::vector<const char *> &AllowedBindActions()
{
	static const std::vector<const char *> s_vActions(s_apAllowedBindActions, s_apAllowedBindActions + std::size(s_apAllowedBindActions));
	return s_vActions;
}

const std::vector<const char *> &CommonBindKeys()
{
	static const std::vector<const char *> s_vKeys(s_apCommonBindKeys, s_apCommonBindKeys + std::size(s_apCommonBindKeys));
	return s_vKeys;
}

const SSettingEntry *FindSettingByCommand(const char *pCommand)
{
	if(!pCommand)
		return nullptr;
	for(const auto &Entry : Settings())
	{
		if(str_comp_nocase(Entry.m_pCommand, pCommand) == 0)
			return &Entry;
	}
	return nullptr;
}

const SSettingEntry *FindSettingById(const char *pId)
{
	if(!pId)
		return nullptr;
	for(const auto &Entry : Settings())
	{
		if(str_comp_nocase(Entry.m_pId, pId) == 0)
			return &Entry;
	}
	return nullptr;
}

bool IsAllowedBindAction(const char *pAction)
{
	char aNormalized[192];
	return ParseBindActionToken(pAction, aNormalized, sizeof(aNormalized));
}

bool IsAllowedBindKey(const char *pKey)
{
	char aKey[64];
	NormalizeKeyName(pKey, aKey, sizeof(aKey));
	if(aKey[0] == '\0')
		return false;
	for(const char *pAllowed : CommonBindKeys())
	{
		if(str_comp(aKey, pAllowed) == 0)
			return true;
	}
	// Allow simple single keys / modifier+key even if not listed.
	if(str_find(aKey, "+"))
	{
		const char *pPlus = str_find(aKey, "+");
		if(!pPlus || pPlus == aKey || pPlus[1] == '\0')
			return false;
		return true;
	}
	return str_length(aKey) >= 1 && str_length(aKey) <= 16;
}

bool ReadCurrentSettingValue(const CConfig &Config, const SSettingEntry &Entry, char *pOut, int OutSize)
{
	if(!pOut || OutSize <= 0)
		return false;
	if(str_comp(Entry.m_pCommand, "cl_dyncam") == 0)
		str_format(pOut, OutSize, "%d", Config.m_ClDyncam);
	else if(str_comp(Entry.m_pCommand, "cl_showchat") == 0)
		str_format(pOut, OutSize, "%d", Config.m_ClShowChat);
	else if(str_comp(Entry.m_pCommand, "cl_showhud") == 0)
		str_format(pOut, OutSize, "%d", Config.m_ClShowhud);
	else if(str_comp(Entry.m_pCommand, "cl_nameplates") == 0)
		str_format(pOut, OutSize, "%d", Config.m_ClNamePlates);
	else if(str_comp(Entry.m_pCommand, "cl_languagefile") == 0)
		str_copy(pOut, Config.m_ClLanguagefile, OutSize);
	else if(str_comp(Entry.m_pCommand, "uc_chat_reply") == 0)
		str_format(pOut, OutSize, "%d", Config.m_UcChatReply);
	else
	{
		pOut[0] = '\0';
		return false;
	}
	return true;
}

void WriteCatalogJson(CJsonStringWriter &Writer, const CConfig &Config)
{
	Writer.BeginObject();
	Writer.WriteAttribute("settings");
	Writer.BeginArray();
	for(const auto &Entry : Settings())
	{
		Writer.BeginObject();
		Writer.WriteAttribute("id");
		Writer.WriteStrValue(Entry.m_pId);
		Writer.WriteAttribute("command");
		Writer.WriteStrValue(Entry.m_pCommand);
		Writer.WriteAttribute("type");
		if(Entry.m_Type == ESettingType::Bool)
			Writer.WriteStrValue("bool");
		else if(Entry.m_Type == ESettingType::Int)
			Writer.WriteStrValue("int");
		else
			Writer.WriteStrValue("enum");
		Writer.WriteAttribute("min");
		Writer.WriteIntValue(Entry.m_Min);
		Writer.WriteAttribute("max");
		Writer.WriteIntValue(Entry.m_Max);
		Writer.WriteAttribute("aliases");
		Writer.BeginArray();
		for(int i = 0; i < Entry.m_AliasCount; ++i)
			Writer.WriteStrValue(Entry.m_ppAliases[i]);
		Writer.EndArray();
		if(Entry.m_Type == ESettingType::Enum && Entry.m_pEnumValues)
		{
			Writer.WriteAttribute("values");
			Writer.BeginArray();
			for(int i = 0; i < Entry.m_EnumCount; ++i)
			{
				Writer.BeginObject();
				Writer.WriteAttribute("value");
				Writer.WriteStrValue(Entry.m_pEnumValues[i].m_pValue);
				Writer.WriteAttribute("label");
				Writer.WriteStrValue(Entry.m_pEnumValues[i].m_pLabel);
				Writer.EndObject();
			}
			Writer.EndArray();
		}
		char aCurrent[128];
		if(ReadCurrentSettingValue(Config, Entry, aCurrent, sizeof(aCurrent)))
		{
			Writer.WriteAttribute("current");
			Writer.WriteStrValue(aCurrent);
		}
		Writer.EndObject();
	}
	Writer.EndArray();

	Writer.WriteAttribute("bind");
	Writer.BeginObject();
	Writer.WriteAttribute("id");
	Writer.WriteStrValue("bind");
	Writer.WriteAttribute("type");
	Writer.WriteStrValue("bind");
	Writer.WriteAttribute("keys");
	Writer.BeginArray();
	for(const char *pKey : CommonBindKeys())
		Writer.WriteStrValue(pKey);
	Writer.EndArray();
	Writer.WriteAttribute("allowed_actions");
	Writer.BeginArray();
	for(const char *pAction : AllowedBindActions())
		Writer.WriteStrValue(pAction);
	Writer.EndArray();
	Writer.EndObject();
	Writer.EndObject();
}

std::string BuildCatalogJson(const CConfig &Config)
{
	CJsonStringWriter Writer;
	WriteCatalogJson(Writer, Config);
	return Writer.GetOutputString();
}

bool ValidateAndNormalizeCommand(const CConfig &Config, SProposedCommand &Cmd)
{
	Cmd.m_Valid = false;
	Cmd.m_aError[0] = '\0';
	Cmd.m_aDisplay[0] = '\0';
	Cmd.m_aUndoLine[0] = '\0';
	TrimInPlace(Cmd.m_aOp);
	TrimInPlace(Cmd.m_aCommand);
	TrimInPlace(Cmd.m_aValue);

	if(str_comp_nocase(Cmd.m_aOp, "set") == 0)
	{
		const SSettingEntry *pEntry = FindSettingByCommand(Cmd.m_aCommand);
		if(!pEntry)
			pEntry = FindSettingById(Cmd.m_aCommand);
		if(!pEntry)
		{
			str_copy(Cmd.m_aError, "Unknown setting", sizeof(Cmd.m_aError));
			return false;
		}
		str_copy(Cmd.m_aCommand, pEntry->m_pCommand, sizeof(Cmd.m_aCommand));

		if(pEntry->m_Type == ESettingType::Bool || pEntry->m_Type == ESettingType::Int)
		{
			if(!str_isallnum(Cmd.m_aValue) && !(Cmd.m_aValue[0] == '-' && str_isallnum(Cmd.m_aValue + 1)))
			{
				if(str_comp_nocase(Cmd.m_aValue, "on") == 0 || str_comp_nocase(Cmd.m_aValue, "true") == 0)
					str_copy(Cmd.m_aValue, "1", sizeof(Cmd.m_aValue));
				else if(str_comp_nocase(Cmd.m_aValue, "off") == 0 || str_comp_nocase(Cmd.m_aValue, "false") == 0)
					str_copy(Cmd.m_aValue, "0", sizeof(Cmd.m_aValue));
				else
				{
					str_copy(Cmd.m_aError, "Invalid numeric value", sizeof(Cmd.m_aError));
					return false;
				}
			}
			const int Value = str_toint(Cmd.m_aValue);
			if(Value < pEntry->m_Min || Value > pEntry->m_Max)
			{
				str_copy(Cmd.m_aError, "Value out of range", sizeof(Cmd.m_aError));
				return false;
			}
			str_format(Cmd.m_aValue, sizeof(Cmd.m_aValue), "%d", Value);
		}
		else if(pEntry->m_Type == ESettingType::Enum)
		{
			bool Found = false;
			for(int i = 0; i < pEntry->m_EnumCount; ++i)
			{
				if(str_comp_nocase(Cmd.m_aValue, pEntry->m_pEnumValues[i].m_pValue) == 0 ||
					str_comp_nocase(Cmd.m_aValue, pEntry->m_pEnumValues[i].m_pLabel) == 0)
				{
					str_copy(Cmd.m_aValue, pEntry->m_pEnumValues[i].m_pValue, sizeof(Cmd.m_aValue));
					Found = true;
					break;
				}
			}
			if(!Found)
			{
				str_copy(Cmd.m_aError, "Invalid enum value", sizeof(Cmd.m_aError));
				return false;
			}
		}

		char aCurrent[128];
		ReadCurrentSettingValue(Config, *pEntry, aCurrent, sizeof(aCurrent));
		if(Cmd.m_aValue[0] == '\0')
			str_format(Cmd.m_aDisplay, sizeof(Cmd.m_aDisplay), "%s \"\"", Cmd.m_aCommand);
		else
			str_format(Cmd.m_aDisplay, sizeof(Cmd.m_aDisplay), "%s %s", Cmd.m_aCommand, Cmd.m_aValue);
		if(aCurrent[0] == '\0')
			str_format(Cmd.m_aUndoLine, sizeof(Cmd.m_aUndoLine), "%s \"\"", Cmd.m_aCommand);
		else
			str_format(Cmd.m_aUndoLine, sizeof(Cmd.m_aUndoLine), "%s %s", Cmd.m_aCommand, aCurrent);
		Cmd.m_Valid = true;
		return true;
	}

	if(str_comp_nocase(Cmd.m_aOp, "bind") == 0)
	{
		char aKey[64];
		NormalizeKeyName(Cmd.m_aCommand, aKey, sizeof(aKey));
		if(!IsAllowedBindKey(aKey))
		{
			str_copy(Cmd.m_aError, "Unsupported bind key", sizeof(Cmd.m_aError));
			return false;
		}
		char aNormalizedValue[256];
		if(!NormalizeBindValue(Cmd.m_aValue, aNormalizedValue, sizeof(aNormalizedValue)))
		{
			str_copy(Cmd.m_aError, "Unsupported bind action", sizeof(Cmd.m_aError));
			return false;
		}
		str_copy(Cmd.m_aCommand, aKey, sizeof(Cmd.m_aCommand));
		str_copy(Cmd.m_aValue, aNormalizedValue, sizeof(Cmd.m_aValue));
		str_format(Cmd.m_aDisplay, sizeof(Cmd.m_aDisplay), "bind %s \"%s\"", aKey, aNormalizedValue);
		str_format(Cmd.m_aUndoLine, sizeof(Cmd.m_aUndoLine), "unbind %s", aKey);
		Cmd.m_Valid = true;
		return true;
	}

	if(str_comp_nocase(Cmd.m_aOp, "unbind") == 0)
	{
		char aKey[64];
		NormalizeKeyName(Cmd.m_aCommand, aKey, sizeof(aKey));
		if(!IsAllowedBindKey(aKey))
		{
			str_copy(Cmd.m_aError, "Unsupported bind key", sizeof(Cmd.m_aError));
			return false;
		}
		str_copy(Cmd.m_aCommand, aKey, sizeof(Cmd.m_aCommand));
		Cmd.m_aValue[0] = '\0';
		str_format(Cmd.m_aDisplay, sizeof(Cmd.m_aDisplay), "unbind %s", aKey);
		str_copy(Cmd.m_aUndoLine, "", sizeof(Cmd.m_aUndoLine));
		Cmd.m_Valid = true;
		return true;
	}

	str_copy(Cmd.m_aError, "Unsupported operation", sizeof(Cmd.m_aError));
	return false;
}

bool TryLocalFallback(const char *pMessage, std::vector<SProposedCommand> &vOut, char *pReply, int ReplySize)
{
	vOut.clear();
	if(pReply && ReplySize > 0)
		pReply[0] = '\0';
	if(!pMessage || pMessage[0] == '\0')
		return false;

	const bool WantOff = ContainsInsensitive(pMessage, "꺼") || ContainsInsensitive(pMessage, "off") || ContainsInsensitive(pMessage, "disable") || ContainsInsensitive(pMessage, "끄");
	const bool WantOn = ContainsInsensitive(pMessage, "켜") || ContainsInsensitive(pMessage, "on") || ContainsInsensitive(pMessage, "enable") || ContainsInsensitive(pMessage, "켜줘");

	auto AddSet = [&](const char *pCommand, const char *pValue, const char *pReplyText) {
		SProposedCommand Cmd;
		str_copy(Cmd.m_aOp, "set", sizeof(Cmd.m_aOp));
		str_copy(Cmd.m_aCommand, pCommand, sizeof(Cmd.m_aCommand));
		str_copy(Cmd.m_aValue, pValue, sizeof(Cmd.m_aValue));
		if(ValidateAndNormalizeCommand(g_Config, Cmd))
		{
			vOut.push_back(Cmd);
			if(pReply && ReplySize > 0 && pReply[0] == '\0')
				str_copy(pReply, pReplyText, ReplySize);
		}
	};

	if(ContainsInsensitive(pMessage, "다이나믹") || ContainsInsensitive(pMessage, "dyncam") || ContainsInsensitive(pMessage, "dynamic camera"))
	{
		if(WantOff)
			AddSet("cl_dyncam", "0", "다이나믹 카메라를 끄겠습니다.");
		else if(WantOn)
			AddSet("cl_dyncam", "1", "다이나믹 카메라를 켜겠습니다.");
	}
	if(ContainsInsensitive(pMessage, "채팅 답장") || ContainsInsensitive(pMessage, "chat reply") || ContainsInsensitive(pMessage, "uc_chat_reply"))
	{
		if(WantOff)
			AddSet("uc_chat_reply", "0", "채팅 답장 기능을 끄겠습니다.");
		else if(WantOn)
			AddSet("uc_chat_reply", "1", "채팅 답장 기능을 켜겠습니다.");
	}
	if(ContainsInsensitive(pMessage, "네임플레이트") || ContainsInsensitive(pMessage, "이름표") || ContainsInsensitive(pMessage, "nameplate"))
	{
		if(WantOff)
			AddSet("cl_nameplates", "0", "네임플레이트를 끄겠습니다.");
		else if(WantOn)
			AddSet("cl_nameplates", "1", "네임플레이트를 켜겠습니다.");
	}
	if(ContainsInsensitive(pMessage, "언어") || ContainsInsensitive(pMessage, "language"))
	{
		if(ContainsInsensitive(pMessage, "영어") || ContainsInsensitive(pMessage, "english"))
			AddSet("cl_languagefile", "", "게임 언어를 영어로 바꾸겠습니다.");
		else if(ContainsInsensitive(pMessage, "한국") || ContainsInsensitive(pMessage, "korean"))
			AddSet("cl_languagefile", "korean", "게임 언어를 한국어로 바꾸겠습니다.");
		else if(ContainsInsensitive(pMessage, "일본") || ContainsInsensitive(pMessage, "japanese"))
			AddSet("cl_languagefile", "japanese", "게임 언어를 일본어로 바꾸겠습니다.");
	}

	auto AddBind = [&](const char *pKey, const char *pValue, const char *pReplyText) {
		SProposedCommand Cmd;
		str_copy(Cmd.m_aOp, "bind", sizeof(Cmd.m_aOp));
		str_copy(Cmd.m_aCommand, pKey, sizeof(Cmd.m_aCommand));
		str_copy(Cmd.m_aValue, pValue, sizeof(Cmd.m_aValue));
		if(ValidateAndNormalizeCommand(g_Config, Cmd))
		{
			vOut.push_back(Cmd);
			if(pReply && ReplySize > 0 && pReply[0] == '\0')
				str_copy(pReply, pReplyText, ReplySize);
		}
	};

	if((ContainsInsensitive(pMessage, "마우스") || ContainsInsensitive(pMessage, "mouse1") || ContainsInsensitive(pMessage, "좌클릭") || ContainsInsensitive(pMessage, "왼 클릭") || ContainsInsensitive(pMessage, "왼쪽")) &&
		(ContainsInsensitive(pMessage, "발사") || ContainsInsensitive(pMessage, "fire")))
	{
		AddBind("mouse1", "+fire", "마우스 왼쪽 클릭에 발사를 바인드하겠습니다.");
	}
	if((ContainsInsensitive(pMessage, " g") || ContainsInsensitive(pMessage, "g 키") || ContainsInsensitive(pMessage, "g키") || str_comp_nocase(pMessage, "g") == 0 || ContainsInsensitive(pMessage, "지 키")) &&
		ContainsInsensitive(pMessage, "이모티콘") && ContainsInsensitive(pMessage, "1"))
	{
		AddBind("g", "emote 1", "G 키에 이모티콘 1을 바인드하겠습니다.");
	}
	if((ContainsInsensitive(pMessage, "ctrl+g") || (ContainsInsensitive(pMessage, "컨트롤") && ContainsInsensitive(pMessage, "g"))) &&
		(ContainsInsensitive(pMessage, "안녕") || ContainsInsensitive(pMessage, "이모티콘")))
	{
		AddBind("ctrl+g", "say 안녕하세요; emote 3", "Ctrl+G에 채팅과 이모티콘을 바인드하겠습니다.");
	}

	if(vOut.empty())
	{
		if(pReply && ReplySize > 0)
			str_copy(pReply, "요청을 이해하지 못했습니다. 설정 이름이나 바인드를 더 구체적으로 말해 주세요.", ReplySize);
		return false;
	}
	if(pReply && ReplySize > 0 && pReply[0] == '\0')
		str_copy(pReply, "요청에 맞는 명령을 준비했습니다. 적용할까요?", ReplySize);
	return true;
}
}
