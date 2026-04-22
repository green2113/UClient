/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_GIPHY_BROWSER_H
#define GAME_CLIENT_COMPONENTS_GIPHY_BROWSER_H

#include <string>
#include <vector>

typedef struct _json_value json_value;

struct SGifResult
{
	std::string m_Id;
	std::string m_Url;
	std::string m_PreviewUrl;
	std::string m_Title;
	int m_Width = 100;
	int m_Height = 100;
};

class CGiphyBrowser
{
public:
	static constexpr int RESULTS_PER_PAGE = 12;

	CGiphyBrowser();

	void SetQuery(const char *pQuery);
	std::string BuildSearchUrl(int PageOffset) const;
	void ParseGiphyResponse(json_value *pRoot);
	void ClearResults();
	void SetPage(int Page);

	const std::vector<SGifResult> &GetResults() const { return m_vResults; }
	const std::string &GetCurrentQuery() const { return m_CurrentQuery; }
	int GetTotalCount() const { return m_TotalCount; }
	int GetCurrentPage() const { return m_CurrentPage; }
	int GetTotalPages() const;

private:
	static const char *GIPHY_API_URL;

	std::vector<SGifResult> m_vResults;
	std::string m_CurrentQuery;
	int m_TotalCount;
	int m_CurrentPage;
};

#endif