/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "giphy_browser.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <engine/external/json-parser/json.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

const char *CGiphyBrowser::GIPHY_API_URL = "https://api.giphy.com/v1/gifs/search";

namespace
{
void UrlEncodeQuery(const std::string &Input, std::string &Out)
{
	Out.clear();
	Out.reserve(Input.size() * 3);
	for(unsigned char c : Input)
	{
		if(std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
			Out.push_back((char)c);
		else
		{
			char aEncoded[4];
			str_format(aEncoded, sizeof(aEncoded), "%%%02X", c);
			Out.append(aEncoded);
		}
	}
}
int JsonToInt(const json_value *pValue, int DefaultValue)
{
	if(pValue == nullptr)
		return DefaultValue;
	if(pValue->type == json_integer)
		return (int)pValue->u.integer;
	if(pValue->type == json_string)
		return std::max(1, std::atoi(pValue->u.string.ptr));
	return DefaultValue;
}

const json_value *FindObjectValue(const json_value *pObject, const char *pName)
{
	if(pObject == nullptr || pObject->type != json_object)
		return nullptr;
	for(unsigned Index = 0; Index < pObject->u.object.length; ++Index)
	{
		if(str_comp(pObject->u.object.values[Index].name, pName) == 0)
			return pObject->u.object.values[Index].value;
	}
	return nullptr;
}

std::string JsonToString(const json_value *pValue)
{
	if(pValue != nullptr && pValue->type == json_string)
		return pValue->u.string.ptr;
	return {};
}
}

CGiphyBrowser::CGiphyBrowser() :
	m_TotalCount(0),
	m_CurrentPage(0)
{
}

void CGiphyBrowser::SetQuery(const char *pQuery)
{
	m_CurrentQuery = pQuery ? pQuery : "";
	m_CurrentPage = 0;
	m_TotalCount = 0;
	m_vResults.clear();
}

std::string CGiphyBrowser::BuildSearchUrl(int PageOffset) const
{
	std::string EncodedQuery;
	UrlEncodeQuery(m_CurrentQuery, EncodedQuery);

	std::ostringstream Url;
	Url << GIPHY_API_URL << "?api_key=" << g_Config.m_UcChatGiphyApiKey;
	Url << "&q=" << EncodedQuery;
	Url << "&limit=" << RESULTS_PER_PAGE;
	Url << "&offset=" << PageOffset;
	Url << "&rating=g";
	return Url.str();
}

void CGiphyBrowser::ParseGiphyResponse(json_value *pRoot, bool Append)
{
	if(!Append)
	{
		m_vResults.clear();
		m_TotalCount = 0;
	}

	const json_value *pPagination = FindObjectValue(pRoot, "pagination");
	if(const json_value *pTotalCount = FindObjectValue(pPagination, "total_count"))
		m_TotalCount = JsonToInt(pTotalCount, 0);

	const json_value *pData = FindObjectValue(pRoot, "data");
	if(pData == nullptr || pData->type != json_array)
		return;

	if(Append)
		m_vResults.reserve(m_vResults.size() + pData->u.array.length);
	else
		m_vResults.reserve(pData->u.array.length);
	for(unsigned Index = 0; Index < pData->u.array.length; ++Index)
	{
		const json_value *pItem = pData->u.array.values[Index];
		if(pItem == nullptr || pItem->type != json_object)
			continue;

		SGifResult Result;
		Result.m_Id = JsonToString(FindObjectValue(pItem, "id"));
		Result.m_Title = JsonToString(FindObjectValue(pItem, "title"));

		const json_value *pImages = FindObjectValue(pItem, "images");
		const json_value *pOriginal = FindObjectValue(pImages, "original");
		Result.m_Url = JsonToString(FindObjectValue(pOriginal, "url"));

		const json_value *pPreview = FindObjectValue(pImages, "fixed_height_small");
		Result.m_PreviewUrl = JsonToString(FindObjectValue(pPreview, "url"));
		if(Result.m_PreviewUrl.empty())
			Result.m_PreviewUrl = JsonToString(FindObjectValue(pPreview, "webp"));
		if(Result.m_PreviewUrl.empty())
		{
			const json_value *pStill = FindObjectValue(pImages, "fixed_height_small_still");
			Result.m_PreviewUrl = JsonToString(FindObjectValue(pStill, "url"));
			Result.m_Width = JsonToInt(FindObjectValue(pStill, "width"), Result.m_Width);
			Result.m_Height = JsonToInt(FindObjectValue(pStill, "height"), Result.m_Height);
		}
		else
		{
			Result.m_Width = JsonToInt(FindObjectValue(pPreview, "width"), Result.m_Width);
			Result.m_Height = JsonToInt(FindObjectValue(pPreview, "height"), Result.m_Height);
		}

		if(Result.m_PreviewUrl.empty())
			Result.m_PreviewUrl = Result.m_Url;
		if(Result.m_Url.empty())
			Result.m_Url = Result.m_PreviewUrl;

		if(!Result.m_Id.empty() && !Result.m_Url.empty())
		{
			const bool Exists = std::any_of(m_vResults.begin(), m_vResults.end(), [&](const SGifResult &Existing) {
				return Existing.m_Id == Result.m_Id;
			});
			if(!Exists)
				m_vResults.push_back(std::move(Result));
		}
	}
}

void CGiphyBrowser::ClearResults()
{
	m_vResults.clear();
	m_CurrentQuery.clear();
	m_TotalCount = 0;
	m_CurrentPage = 0;
}

void CGiphyBrowser::SetPage(int Page)
{
	m_CurrentPage = std::max(0, Page);
	m_vResults.clear();
}

int CGiphyBrowser::GetTotalPages() const
{
	if(m_TotalCount <= 0)
		return 0;
	return (m_TotalCount + RESULTS_PER_PAGE - 1) / RESULTS_PER_PAGE;
}