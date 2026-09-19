#include "uclient_ai_settings.h"

#include <base/system.h>

#include <engine/config.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <cstdio>
#include <cstring>
#include <string>

static bool NameLooksSecret(const char *pName)
{
	if(!pName || !pName[0])
		return true;
	return str_find_nocase(pName, "password") ||
		str_find_nocase(pName, "token") ||
		str_find_nocase(pName, "secret") ||
		str_find_nocase(pName, "_key") ||
		str_find_nocase(pName, "apikey") ||
		str_find_nocase(pName, "uuid");
}

static void AppendEscaped(std::string &Out, const char *pText)
{
	if(!pText)
		return;
	for(; *pText; ++pText)
	{
		const unsigned char Ch = (unsigned char)*pText;
		if(Ch == '\\' || Ch == '"')
		{
			Out += '\\';
			Out += (char)Ch;
		}
		else if(Ch == '\n')
			Out += "\\n";
		else if(Ch == '\r')
			Out += "\\r";
		else if(Ch == '\t')
			Out += "\\t";
		else if(Ch < 0x20)
		{
			char aHex[8];
			str_format(aHex, sizeof(aHex), "\\u%04x", Ch);
			Out += aHex;
		}
		else
			Out += (char)Ch;
	}
}

struct SDumpContext
{
	std::string m_Json;
	bool m_First = true;
};

static void DumpOneVariable(const SConfigVariable *pVariable, void *pUser)
{
	if(!pVariable || (pVariable->m_Flags & CFGFLAG_CLIENT) == 0)
		return;
	if((pVariable->m_Flags & CFGFLAG_SAVE) == 0)
		return;
	if(NameLooksSecret(pVariable->m_pScriptName))
		return;
	if(pVariable->IsDefault())
		return;

	auto *pCtx = static_cast<SDumpContext *>(pUser);
	if(!pCtx->m_First)
		pCtx->m_Json += ",";
	pCtx->m_First = false;
	pCtx->m_Json += "\"";
	AppendEscaped(pCtx->m_Json, pVariable->m_pScriptName);
	pCtx->m_Json += "\":";

	if(pVariable->m_Type == SConfigVariable::VAR_INT)
	{
		const auto *pInt = static_cast<const SIntConfigVariable *>(pVariable);
		char aNum[32];
		str_format(aNum, sizeof(aNum), "%d", *pInt->m_pVariable);
		pCtx->m_Json += aNum;
	}
	else if(pVariable->m_Type == SConfigVariable::VAR_COLOR)
	{
		const auto *pColor = static_cast<const SColorConfigVariable *>(pVariable);
		char aNum[32];
		str_format(aNum, sizeof(aNum), "%u", *pColor->m_pVariable);
		pCtx->m_Json += aNum;
	}
	else if(pVariable->m_Type == SConfigVariable::VAR_STRING)
	{
		const auto *pStr = static_cast<const SStringConfigVariable *>(pVariable);
		pCtx->m_Json += "\"";
		AppendEscaped(pCtx->m_Json, pStr->m_pStr);
		pCtx->m_Json += "\"";
	}
	else
		pCtx->m_Json += "null";
}

void UClientAi_PollLiveSettingsDump(IConfigManager *pConfig, IStorage *pStorage, const char *pBindsJson)
{
	if(!pConfig || !pStorage)
		return;
	if(!pStorage->FileExists(UCLIENT_AI_SETTINGS_REQUEST_FILE, IStorage::TYPE_SAVE))
		return;

	SDumpContext Ctx;
	Ctx.m_Json = "{\"source\":\"memory\",\"values\":{";
	pConfig->PossibleConfigVariables("", CFGFLAG_CLIENT, DumpOneVariable, &Ctx);
	Ctx.m_Json += "},\"binds\":";
	Ctx.m_Json += (pBindsJson && pBindsJson[0] == '[') ? pBindsJson : "[]";
	Ctx.m_Json += "}";

	IOHANDLE File = pStorage->OpenFile(UCLIENT_AI_SETTINGS_LIVE_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(File)
	{
		io_write(File, Ctx.m_Json.c_str(), Ctx.m_Json.size());
		io_sync(File);
		io_close(File);
	}
	pStorage->RemoveFile(UCLIENT_AI_SETTINGS_REQUEST_FILE, IStorage::TYPE_SAVE);
}
