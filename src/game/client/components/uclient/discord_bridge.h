#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_DISCORD_BRIDGE_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_DISCORD_BRIDGE_H

#include <engine/shared/http.h>

#include <game/client/component.h>

#include <memory>
#include <string>
#include <vector>

class CDiscordBridge : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnUpdate() override;

	void SubmitServerLine(const char *pLine);

private:
	enum class ERequest
	{
		NONE,
		INGEST,
		POLL,
		TOPIC,
	};

	void BeginIngest();
	void BeginPoll();
	void BeginTopic();
	bool BuildTopic(std::string &Topic) const;
	bool TopicDue(const std::string &Topic);
	void FinishRequest();
	void Auth(CHttpRequest *pRequest) const;

	std::shared_ptr<CHttpRequest> m_pRequest;
	ERequest m_Request = ERequest::NONE;
	std::vector<std::string> m_vPending;
	std::vector<std::string> m_vInflight;
	std::string m_SentTopic;
	std::string m_StableTopic;
	std::string m_TopicInflight;
	int64_t m_NextPoll = 0;
	int64_t m_NextIngest = 0;
	int64_t m_NextTopic = 0;
	int64_t m_TopicChangedAt = 0;
	bool m_Linked = false;
};

#endif
