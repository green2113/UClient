#include "translator.h"

#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <cstring>

#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include <game/client/components/chat.h>

bool CUcTranslator::IsEnabled() const
{
	return g_Config.m_UcTranslate != 0;
}

static constexpr int TRANSLATE_MAX_LENGTH = 512;

bool CUcTranslator::TranslateAsync(int Team, const char *pText, CChat *pChat)
{
	if(!IsEnabled() || !pChat || !pText || !*pText)
		return false;

	std::string Prefix;
	const char *pTextToTranslate = pText;
	const char *pColon = str_find(pText, ":");
	if(pColon)
	{
		const char *pAfterColon = pColon + 1;
		Prefix.assign(pText, pAfterColon - pText);
		while(*pAfterColon == ' ' || *pAfterColon == '\t')
		{
			Prefix.push_back(*pAfterColon);
			++pAfterColon;
		}
		if(*pAfterColon)
		{
			pTextToTranslate = pAfterColon;
		}
		else
		{
			Prefix.clear();
		}
	}

	return TranslateAsyncImpl(pTextToTranslate, g_Config.m_UcTranslateTarget, pChat, Team, std::move(Prefix));
}

void CUcTranslator::OnRender()
{
	std::deque<CResult> Results;
	{
		std::lock_guard<std::mutex> Lock(m_ResultLock);
		if(m_vResults.empty())
			return;
		Results.swap(m_vResults);
	}

	for(auto &Result : Results)
	{
		if(Result.m_pChat)
		{
			Result.m_pChat->SendChatTranslated(Result.m_Team, Result.m_Text.c_str());
		}
	}
}

bool CUcTranslator::TranslateAsyncImpl(const char *pText, const char *pTarget, CChat *pChat, int Team, std::string Prefix)
{
	if(!pChat || !pText || !*pText)
		return false;

	std::string Input(pText);
	std::string TargetString = pTarget && *pTarget ? pTarget : "";
	std::thread([this, Team, Input = std::move(Input), pChat, TargetString = std::move(TargetString), Prefix = std::move(Prefix)]() {
		char aBuffer[TRANSLATE_MAX_LENGTH];
		const char *pTargetOverride = TargetString.empty() ? nullptr : TargetString.c_str();
		bool Success = TranslateInternal(Input.c_str(), aBuffer, sizeof(aBuffer), pTargetOverride);
		CResult Result;
		Result.m_Team = Team;
		Result.m_pChat = pChat;
		if(Success)
			Result.m_Text = Prefix.empty() ? std::string(aBuffer) : Prefix + aBuffer;
		else
			Result.m_Text = Prefix.empty() ? Input : Prefix + Input;
		{
			std::lock_guard<std::mutex> Lock(m_ResultLock);
			m_vResults.emplace_back(std::move(Result));
		}
	}).detach();
	return true;
}

bool CUcTranslator::TranslateInternal(const char *pText, char *pOut, int OutSize, const char *pTarget)
{
	if(str_comp_nocase(g_Config.m_UcTranslateBackend, "deepl") == 0)
		return TranslateUsingDeepL(pText, pOut, OutSize, pTarget);

	if(str_comp_nocase(g_Config.m_UcTranslateBackend, "google") == 0)
		return TranslateUsingDefault(pText, pOut, OutSize, pTarget);

	if(g_Config.m_UcTranslateApi[0])
		return TranslateUsingCustom(pText, pOut, OutSize, pTarget);
	return TranslateUsingDefault(pText, pOut, OutSize, pTarget);
}

bool CUcTranslator::TranslateUsingDefault(const char *pText, char *pOut, int OutSize, const char *pTarget)
{
	char aEscaped[2048];
	EscapeUrl(aEscaped, sizeof(aEscaped), pText);

	char aTarget[16];
	BuildTargetCode(aTarget, sizeof(aTarget), pTarget);

	char aUrl[4096];
	str_format(aUrl, sizeof(aUrl), "https://translate.googleapis.com/translate_a/single?client=gtx&sl=auto&tl=%s&dt=t&q=%s", aTarget, aEscaped);
	return PerformRequest(aUrl, pOut, OutSize);
}

bool CUcTranslator::TranslateUsingCustom(const char *pText, char *pOut, int OutSize, const char *pTarget)
{
	if(IsDeepLEndpoint(g_Config.m_UcTranslateApi))
		return TranslateUsingDeepL(pText, pOut, OutSize, pTarget);

	char aEscaped[2048];
	EscapeUrl(aEscaped, sizeof(aEscaped), pText);

	char aTarget[16];
	BuildTargetCode(aTarget, sizeof(aTarget), pTarget);

	const char *pBase = g_Config.m_UcTranslateApi;
	const char *pSep = str_find(pBase, "?") ? "&" : "?";

	char aUrl[4096];
	if(g_Config.m_UcTranslateKey[0])
		str_format(aUrl, sizeof(aUrl), "%s%sq=%s&target=%s&key=%s", pBase, pSep, aEscaped, aTarget, g_Config.m_UcTranslateKey);
	else
		str_format(aUrl, sizeof(aUrl), "%s%sq=%s&target=%s", pBase, pSep, aEscaped, aTarget);

	return PerformRequest(aUrl, pOut, OutSize);
}

bool CUcTranslator::PerformRequest(const char *pUrl, char *pOut, int OutSize)
{
	return PerformRequest(HttpGet(pUrl), pOut, OutSize);
}

bool CUcTranslator::PerformRequest(std::unique_ptr<CHttpRequest> pRequestUnique, char *pOut, int OutSize)
{
	IHttp *pHttp = this->Http();
	if(!pHttp)
		return false;

	pRequestUnique->Timeout(CTimeout{4000, 10000, 500, 5});
	std::shared_ptr<CHttpRequest> pRequest(
		pRequestUnique.release(),
		[](CHttpRequest *p) { delete p; });
	pHttp->Run(std::static_pointer_cast<IHttpRequest>(pRequest));
	pRequest->Wait();

	if(pRequest->State() != EHttpState::DONE || pRequest->StatusCode() >= 400)
		return false;

	unsigned char *pResult = nullptr;
	size_t ResultLength = 0;
	pRequest->Result(&pResult, &ResultLength);
	if(!pResult || ResultLength == 0)
		return false;

	return this->ParseResponse(pResult, ResultLength, pOut, OutSize);
}

bool CUcTranslator::ParseResponse(const unsigned char *pData, size_t Length, char *pOut, int OutSize) const
{
	if(!pData || Length == 0 || OutSize <= 0)
		return false;

	std::string Response(reinterpret_cast<const char *>(pData), Length);

	auto DecodeJsonString = [](const std::string &Json, size_t StartIndex, std::string &Out) -> bool {
		Out.clear();
		bool Escape = false;
		for(size_t i = StartIndex; i < Json.size(); ++i)
		{
			char c = Json[i];
			if(Escape)
			{
				switch(c)
				{
				case 'n': Out.push_back('\n'); break;
				case 't': Out.push_back('\t'); break;
				case 'r': Out.push_back('\r'); break;
				case '"': Out.push_back('"'); break;
				case '\\': Out.push_back('\\'); break;
				default: Out.push_back(c); break;
				}
				Escape = false;
			}
			else if(c == '\\')
			{
				Escape = true;
			}
			else if(c == '"')
			{
				return true;
			}
			else
			{
				Out.push_back(c);
			}
		}
		return false;
	};

	size_t TextPos = Response.find("\"text\"");
	if(TextPos != std::string::npos)
	{
		size_t ColonPos = Response.find(':', TextPos);
		if(ColonPos != std::string::npos)
		{
			size_t QuotePos = Response.find('"', ColonPos);
			if(QuotePos != std::string::npos)
			{
				std::string Extracted;
				if(DecodeJsonString(Response, QuotePos + 1, Extracted))
				{
					str_copy(pOut, Extracted.c_str(), OutSize);
					return true;
				}
			}
		}
	}

	std::string Extracted;
	bool InString = false;
	bool Escape = false;
	for(const char c : Response)
	{
		if(!InString)
		{
			if(c == '"')
			{
				InString = true;
				Extracted.clear();
			}
		}
		else
		{
			if(Escape)
			{
				switch(c)
				{
				case 'n': Extracted.push_back('\n'); break;
				case 't': Extracted.push_back('\t'); break;
				case 'r': Extracted.push_back('\r'); break;
				case '"': Extracted.push_back('"'); break;
				case '\\': Extracted.push_back('\\'); break;
				default: Extracted.push_back(c); break;
				}
				Escape = false;
			}
			else if(c == '\\')
			{
				Escape = true;
			}
			else if(c == '"')
			{
				str_copy(pOut, Extracted.c_str(), OutSize);
				return true;
			}
			else
			{
				Extracted.push_back(c);
			}
		}
	}

	str_copy(pOut, Response.c_str(), OutSize);
	return true;
}

bool CUcTranslator::TranslateUsingDeepL(const char *pText, char *pOut, int OutSize, const char *pTarget)
{
	if(!g_Config.m_UcTranslateKey[0])
		return false;

	char aEscaped[2048];
	EscapeUrl(aEscaped, sizeof(aEscaped), pText);

	char aTarget[16];
	BuildTargetCode(aTarget, sizeof(aTarget), pTarget, false);

	char aBody[4096];
	str_format(aBody, sizeof(aBody), "text=%s&target_lang=%s", aEscaped, aTarget);
	const size_t BodyLen = str_length(aBody);

	const char *pEndpoint = g_Config.m_UcTranslateApi[0] ? g_Config.m_UcTranslateApi : "https://api-free.deepl.com/v2/translate";

	auto pRequest = HttpPost(pEndpoint, reinterpret_cast<const unsigned char *>(aBody), BodyLen);
	pRequest->HeaderString("Content-Type", "application/x-www-form-urlencoded");
	char aAuth[512];
	str_format(aAuth, sizeof(aAuth), "DeepL-Auth-Key %s", g_Config.m_UcTranslateKey);
	pRequest->HeaderString("Authorization", aAuth);

	return PerformRequest(std::move(pRequest), pOut, OutSize);
}

bool CUcTranslator::IsDeepLEndpoint(const char *pUrl) const
{
	if(!pUrl || !*pUrl)
		return false;
	return str_find_nocase(pUrl, "deepl.com") != nullptr;
}

void CUcTranslator::BuildTargetCode(char *pTarget, size_t TargetSize, const char *pPreferred, bool Lowercase) const
{
	const char *pConfigTarget = (pPreferred && *pPreferred) ? pPreferred : (g_Config.m_UcTranslateTarget[0] ? g_Config.m_UcTranslateTarget : "en");

	size_t Pos = 0;
	for(const char *pChr = pConfigTarget; *pChr && Pos + 1 < TargetSize; ++pChr)
	{
		char c = *pChr;
		if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-')
		{
			if(Lowercase)
			{
				if(c >= 'A' && c <= 'Z')
					c = c - 'A' + 'a';
			}
			else
			{
				if(c >= 'a' && c <= 'z')
					c = c - 'a' + 'A';
			}
			pTarget[Pos++] = c;
		}
	}

	if(Pos == 0)
	{
		if(Lowercase)
		{
			pTarget[Pos++] = 'e';
			if(Pos + 1 < TargetSize)
				pTarget[Pos++] = 'n';
		}
		else
		{
			pTarget[Pos++] = 'E';
			if(Pos + 1 < TargetSize)
				pTarget[Pos++] = 'N';
		}
	}

	pTarget[Pos] = '\0';

	if(!Lowercase)
	{
		if(str_comp(pTarget, "EN") == 0)
		{
			str_copy(pTarget, "EN-US", TargetSize);
		}
		else if(str_comp(pTarget, "PT") == 0)
		{
			str_copy(pTarget, "PT-BR", TargetSize);
		}
	}
}
