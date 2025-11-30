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

bool CUcTranslator::TranslateMessage(const char *pText, char *pOut, int OutSize)
{
	if(!IsEnabled() || !pText || !*pText || !pOut || OutSize <= 0)
		return false;

	return TranslateInternal(pText, pOut, OutSize, nullptr);
}

bool CUcTranslator::TranslateAsync(int Team, const char *pText, CChat *pChat)
{
	if(!IsEnabled() || !pChat || !pText || !*pText)
		return false;

	return TranslateAsyncImpl(pText, g_Config.m_UcTranslateTarget, pChat, EResultType::SEND, Team, -1);
}

bool CUcTranslator::TranslateLineAsync(int64_t LineId, const char *pText, const char *pTarget, CChat *pChat)
{
	if(!pChat || !pText || !*pText)
		return false;

	return TranslateAsyncImpl(pText, pTarget, pChat, EResultType::LINE, -1, LineId);
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
			if(Result.m_Type == EResultType::SEND)
				Result.m_pChat->SendChatTranslated(Result.m_Team, Result.m_Text.c_str());
			else if(Result.m_Type == EResultType::LINE)
				Result.m_pChat->HandleManualTranslation(Result.m_LineId, Result.m_Success ? Result.m_Text.c_str() : nullptr, Result.m_Success);
		}
	}
}

bool CUcTranslator::TranslateAsyncImpl(const char *pText, const char *pTarget, CChat *pChat, EResultType Type, int Team, int64_t LineId)
{
	if(!pChat || !pText || !*pText)
		return false;

	std::string Input(pText);
	std::string TargetString = pTarget && *pTarget ? pTarget : "";
	std::thread([this, Team, Input = std::move(Input), pChat, Type, LineId, TargetString = std::move(TargetString)]() {
		char aBuffer[TRANSLATE_MAX_LENGTH];
		const char *pTargetOverride = TargetString.empty() ? nullptr : TargetString.c_str();
		bool Success = TranslateInternal(Input.c_str(), aBuffer, sizeof(aBuffer), pTargetOverride);
		CResult Result;
		Result.m_Type = Type;
		Result.m_Team = Team;
		Result.m_LineId = LineId;
		Result.m_pChat = pChat;
		Result.m_Success = Success;
		Result.m_Text = Success ? std::string(aBuffer) : Input;
		{
			std::lock_guard<std::mutex> Lock(m_ResultLock);
			m_vResults.emplace_back(std::move(Result));
		}
	}).detach();
	return true;
}

bool CUcTranslator::TranslateInternal(const char *pText, char *pOut, int OutSize, const char *pTarget)
{
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

	auto DecodeJsonString = [](const std::string &Response, size_t StartIndex, std::string &Out) -> bool {
		Out.clear();
		bool Escape = false;
		for(size_t i = StartIndex; i < Response.size(); ++i)
		{
			char c = Response[i];
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

	// try to find the first JSON string
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

	// fallback: copy raw response
	str_copy(pOut, Response.c_str(), OutSize);
	return true;
}

bool CUcTranslator::TranslateUsingDeepL(const char *pText, char *pOut, int OutSize, const char *pTarget)
{
	if(!g_Config.m_UcTranslateKey[0] || !g_Config.m_UcTranslateApi[0])
		return false;

	char aEscaped[2048];
	EscapeUrl(aEscaped, sizeof(aEscaped), pText);

	char aTarget[16];
	BuildTargetCode(aTarget, sizeof(aTarget), pTarget, false);

	char aBody[4096];
	str_format(aBody, sizeof(aBody), "text=%s&target_lang=%s", aEscaped, aTarget);
	const size_t BodyLen = str_length(aBody);

	auto pRequest = HttpPost(g_Config.m_UcTranslateApi, reinterpret_cast<const unsigned char *>(aBody), BodyLen);
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
