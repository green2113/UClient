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
		CONTROL,
		CONTROL_RESULT,
		ACK,
	};

	void BeginIngest();
	void BeginPoll();
	void BeginTopic();
	void BeginControl();
	void BeginControlResult();
	void BeginAck();
	void ApplyControl(int Id, const char *pKind, const char *pAddress, const char *pPassword);
	void WatchConnect();
	bool BuildTopic(std::string &Topic) const;
	bool TopicDue(const std::string &Topic);
	void FinishRequest();
	void Auth(CHttpRequest *pRequest);
	void EnsureSession();

	std::shared_ptr<CHttpRequest> m_pRequest;
	ERequest m_Request = ERequest::NONE;
	struct SDiscordMessage
	{
		std::string m_ChannelId;
		std::string m_MessageId;
	};

	std::vector<std::string> m_vPending;
	std::vector<std::string> m_vInflight;
	std::vector<SDiscordMessage> m_vSentDeletes;
	std::vector<SDiscordMessage> m_vAckInflight;
	std::string m_SentTopic;
	std::string m_StableTopic;
	std::string m_TopicInflight;
	int64_t m_NextPoll = 0;
	int64_t m_NextAck = 0;
	int64_t m_NextIngest = 0;
	int64_t m_NextTopic = 0;
	int64_t m_TopicChangedAt = 0;
	int64_t m_NextControl = 0;
	int64_t m_ControlDeadline = 0;
	int64_t m_ControlNotBefore = 0;
	int m_ControlId = 0;
	int m_HeldId = 0;
	bool m_Held = false;
	std::string m_HeldKind;
	std::string m_HeldAddress;
	std::string m_HeldPassword;
	bool m_AwaitingConnect = false;
	bool m_ResultReady = false;
	std::string m_ResultCode;
	std::string m_ResultDetail;
	bool m_Linked = false;
	bool m_Owner = false;
	char m_aSession[33]{};
	int64_t m_StartedAt = 0;
};

#endif
