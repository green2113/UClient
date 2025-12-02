#ifndef GAME_CLIENT_COMPONENTS_UNDER_TRANSLATOR_H
#define GAME_CLIENT_COMPONENTS_UNDER_TRANSLATOR_H

#include <game/client/component.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

class CChat;
class CHttpRequest;

class CUcTranslator : public CComponent
{
public:
	bool IsEnabled() const;
	bool TranslateAsync(int Team, const char *pText, CChat *pChat);
	void OnRender() override;
	int Sizeof() const override { return sizeof(*this); }

private:
	struct CResult
	{
		int m_Team;
		std::string m_Text;
		CChat *m_pChat;
	};

	std::mutex m_ResultLock;
	std::deque<CResult> m_vResults;

	bool TranslateAsyncImpl(const char *pText, const char *pTarget, CChat *pChat, int Team, std::string Prefix);
	bool TranslateInternal(const char *pText, char *pOut, int OutSize, const char *pTarget);
	bool TranslateUsingDefault(const char *pText, char *pOut, int OutSize, const char *pTarget);
	bool TranslateUsingCustom(const char *pText, char *pOut, int OutSize, const char *pTarget);
	bool TranslateUsingDeepL(const char *pText, char *pOut, int OutSize, const char *pTarget);
	bool PerformRequest(const char *pUrl, char *pOut, int OutSize);
	bool PerformRequest(std::unique_ptr<CHttpRequest> pRequestUnique, char *pOut, int OutSize);
	bool ParseResponse(const unsigned char *pData, size_t Length, char *pOut, int OutSize) const;
	void BuildTargetCode(char *pTarget, size_t TargetSize, const char *pPreferred, bool Lowercase = true) const;
	bool IsDeepLEndpoint(const char *pUrl) const;
};

#endif // GAME_CLIENT_COMPONENTS_UNDER_TRANSLATOR_H
