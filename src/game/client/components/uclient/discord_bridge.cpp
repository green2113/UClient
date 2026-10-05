#include "discord_bridge.h"

#include <algorithm>

#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <game/client/components/chat.h>
#include <game/client/gameclient.h>

namespace
{
std::string JsonEscape(const char *pText)
{
	std::string Result;
	for(const unsigned char *pChar = (const unsigned char *)(pText ? pText : ""); *pChar; ++pChar)
	{
		switch(*pChar)
		{
		case '"': Result += "\\\""; break;
		case '\\': Result += "\\\\"; break;
		case '\n': Result += "\\n"; break;
		case '\r': Result += "\\r"; break;
		case '\t': Result += "\\t"; break;
		default:
			if(*pChar >= 0x20)
				Result += (char)*pChar;
		}
	}
	return Result;
}
}

void CDiscordBridge::SubmitServerLine(const char *pLine)
{
	if(!pLine || !pLine[0])
		return;
	if(m_vPending.size() >= 32)
		m_vPending.erase(m_vPending.begin());
	m_vPending.emplace_back(pLine);
}

void CDiscordBridge::Auth(CHttpRequest *pRequest) const
{
	char aAuthorization[192];
	str_format(aAuthorization, sizeof(aAuthorization), "Bearer %s", GameClient()->m_UClientAccount.Secret());
	pRequest->HeaderString("Authorization", aAuthorization);
	pRequest->HeaderString("x-uclient-install-id", GameClient()->m_UClientAccount.InstallId());
	pRequest->FailOnErrorStatus(false);
	pRequest->LogProgress(HTTPLOG::FAILURE);
	pRequest->Timeout(CTimeout{10000, 20000, 0, 0});
}

void CDiscordBridge::BeginIngest()
{
	m_vInflight.clear();
	size_t Count = m_vPending.size();
	if(Count > 8)
		Count = 8;
	m_vInflight.insert(m_vInflight.end(), m_vPending.begin(), m_vPending.begin() + Count);
	m_vPending.erase(m_vPending.begin(), m_vPending.begin() + Count);

	std::string Json = "{\"lines\":[";
	for(size_t i = 0; i < m_vInflight.size(); ++i)
	{
		if(i)
			Json += ",";
		Json += "\"";
		Json += JsonEscape(m_vInflight[i].c_str());
		Json += "\"";
	}
	Json += "]}";

	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s/discord/chat/ingest", g_Config.m_UcApiBaseUrl);
	auto pRequest = HttpPostJson(aUrl, Json.c_str());
	Auth(pRequest.get());
	m_pRequest = std::move(pRequest);
	m_Request = ERequest::INGEST;
	Http()->Run(m_pRequest);
}

bool CDiscordBridge::BuildTopic(std::string &Topic) const
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		Topic.clear();
		return true;
	}

	std::vector<std::string> vNames;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active || !ClientData.m_aName[0])
			continue;
		vNames.emplace_back(ClientData.m_aName);
	}
	if(vNames.empty())
		return false;

	std::sort(vNames.begin(), vNames.end(), [](const std::string &aLeft, const std::string &aRight) {
		return str_comp_nocase(aLeft.c_str(), aRight.c_str()) < 0;
	});

	char aHead[64];
	if(vNames.size() == 1)
		str_copy(aHead, "1 player: ", sizeof(aHead));
	else
		str_format(aHead, sizeof(aHead), "%d players: ", (int)vNames.size());

	Topic = aHead;
	for(size_t i = 0; i < vNames.size(); ++i)
	{
		const std::string Entry = i ? std::string(", ") + vNames[i] : vNames[i];
		if(Topic.size() + Entry.size() > 1020)
		{
			Topic += "...";
			break;
		}
		Topic += Entry;
	}
	return true;
}

bool CDiscordBridge::TopicDue(const std::string &Topic)
{
	if(Topic != m_StableTopic)
	{
		m_StableTopic = Topic;
		m_TopicChangedAt = time_get();
		return false;
	}
	if(Topic == m_SentTopic || time_get() < m_NextTopic)
		return false;
	return time_get() >= m_TopicChangedAt + 2 * time_freq();
}

void CDiscordBridge::BeginTopic()
{
	if(!BuildTopic(m_TopicInflight))
		return;

	std::string Json = std::string("{\"topic\":\"") + JsonEscape(m_TopicInflight.c_str()) + "\"}";
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s/discord/chat/topic", g_Config.m_UcApiBaseUrl);
	auto pRequest = HttpPostJson(aUrl, Json.c_str());
	Auth(pRequest.get());
	m_pRequest = std::move(pRequest);
	m_Request = ERequest::TOPIC;
	Http()->Run(m_pRequest);
}

void CDiscordBridge::BeginControl()
{
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s/discord/control", g_Config.m_UcApiBaseUrl);
	auto pRequest = HttpGet(aUrl);
	Auth(pRequest.get());
	m_pRequest = std::move(pRequest);
	m_Request = ERequest::CONTROL;
	m_NextControl = time_get() + 2 * time_freq();
	Http()->Run(m_pRequest);
}

void CDiscordBridge::BeginControlResult()
{
	std::string Json = std::string("{\"id\":") + std::to_string(m_ControlId) + ",\"code\":\"" + JsonEscape(m_ResultCode.c_str()) + "\",\"detail\":\"" + JsonEscape(m_ResultDetail.c_str()) + "\"}";
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s/discord/control/result", g_Config.m_UcApiBaseUrl);
	auto pRequest = HttpPostJson(aUrl, Json.c_str());
	Auth(pRequest.get());
	m_pRequest = std::move(pRequest);
	m_Request = ERequest::CONTROL_RESULT;
	Http()->Run(m_pRequest);
}

void CDiscordBridge::ApplyControl(int Id, const char *pKind, const char *pAddress, const char *pPassword)
{
	m_ControlId = Id;
	if(str_comp(pKind, "disconnect") == 0)
	{
		if(Client()->State() == IClient::STATE_ONLINE)
			Client()->Disconnect();
		m_ResultCode = "disconnected";
		m_ResultDetail.clear();
		m_ResultReady = true;
		m_NextControl = time_get();
		return;
	}
	if(str_comp(pKind, "connect") != 0 || !pAddress || !pAddress[0])
		return;
	const char *pPass = pPassword && pPassword[0] ? pPassword : nullptr;
	Client()->Connect(pAddress, pPass);
	if(Client()->State() == IClient::STATE_OFFLINE)
	{
		const char *pError = Client()->ErrorString();
		m_ResultDetail.clear();
		if(pError && str_find_nocase(pError, "password"))
			m_ResultCode = "password";
		else
		{
			m_ResultCode = "failed";
			m_ResultDetail = pError && pError[0] ? pError : "Could not resolve the address.";
		}
		m_ResultReady = true;
		m_NextControl = time_get();
		return;
	}
	m_AwaitingConnect = true;
	m_ControlNotBefore = time_get() + time_freq();
	m_ControlDeadline = time_get() + 60 * time_freq();
}

void CDiscordBridge::WatchConnect()
{
	if(time_get() < m_ControlNotBefore)
		return;
	const int State = Client()->State();
	if(State == IClient::STATE_ONLINE)
	{
		m_AwaitingConnect = false;
		m_ResultCode = "connected";
		m_ResultDetail.clear();
		m_ResultReady = true;
		return;
	}
	if(State == IClient::STATE_OFFLINE)
	{
		const char *pError = Client()->ErrorString();
		m_AwaitingConnect = false;
		m_ResultDetail.clear();
		if(pError && str_find_nocase(pError, "password"))
			m_ResultCode = "password";
		else
		{
			m_ResultCode = "failed";
			m_ResultDetail = pError && pError[0] ? pError : "Could not connect.";
		}
		m_ResultReady = true;
		return;
	}
	if(time_get() >= m_ControlDeadline)
	{
		m_AwaitingConnect = false;
		m_ResultCode = "failed";
		m_ResultDetail = "The connection timed out.";
		m_ResultReady = true;
	}
}

void CDiscordBridge::BeginPoll()
{
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s/discord/chat/outbound", g_Config.m_UcApiBaseUrl);
	auto pRequest = HttpGet(aUrl);
	Auth(pRequest.get());
	m_pRequest = std::move(pRequest);
	m_Request = ERequest::POLL;
	m_NextPoll = time_get() + time_freq();
	Http()->Run(m_pRequest);
}

void CDiscordBridge::FinishRequest()
{
	const ERequest Request = m_Request;
	const EHttpState HttpState = m_pRequest->State();
	const int Status = HttpState == EHttpState::DONE ? m_pRequest->StatusCode() : 0;
	json_value *pRoot = HttpState == EHttpState::DONE ? m_pRequest->ResultJson() : nullptr;
	m_pRequest = nullptr;
	m_Request = ERequest::NONE;

	const bool Ok = HttpState == EHttpState::DONE && Status >= 200 && Status < 300 && pRoot && pRoot->type == json_object;
	if(Request == ERequest::INGEST)
	{
		if(!Ok)
		{
			m_vPending.insert(m_vPending.begin(), m_vInflight.begin(), m_vInflight.end());
			m_NextIngest = time_get() + 2 * time_freq();
		}
		else
		{
			const json_value *pLinked = json_object_get(pRoot, "linked");
			m_Linked = pLinked && pLinked->type == json_boolean && pLinked->u.boolean;
			if(!m_Linked)
				m_vPending.clear();
			m_NextIngest = time_get();
			m_NextPoll = time_get();
		}
		m_vInflight.clear();
	}
	else if(Request == ERequest::POLL)
	{
		if(!Ok)
			m_NextPoll = time_get() + 5 * time_freq();
		else
		{
			const json_value *pLinked = json_object_get(pRoot, "linked");
			m_Linked = pLinked && pLinked->type == json_boolean && pLinked->u.boolean;
			m_NextPoll = time_get() + (m_Linked ? time_freq() : 30 * time_freq());
			const json_value *pMessages = json_object_get(pRoot, "messages");
			if(m_Linked && Client()->State() == IClient::STATE_ONLINE && pMessages && pMessages->type == json_array)
			{
				const int Count = json_array_length(pMessages);
				for(int i = 0; i < Count; ++i)
				{
					const json_value *pItem = json_array_get(pMessages, i);
					const json_value *pBody = pItem ? json_object_get(pItem, "body") : nullptr;
					if(!pBody || pBody->type != json_string || !pBody->u.string.ptr[0])
						continue;
					char aText[512];
					str_copy(aText, pBody->u.string.ptr, sizeof(aText));
					const json_value *pMode = json_object_get(pItem, "mode");
					const char *pModeStr = pMode && pMode->type == json_string ? pMode->u.string.ptr : "all";
					const json_value *pRoom = json_object_get(pItem, "room_id");
					const char *pRoomId = pRoom && pRoom->type == json_string ? pRoom->u.string.ptr : "";
					if(str_comp(pModeStr, "team") == 0)
						GameClient()->m_Chat.SendChat(1, aText);
					else if(str_comp(pModeStr, "uclient") == 0)
						GameClient()->m_ClientIndicator.SendUClientChat(aText, nullptr, true);
					else if(str_comp(pModeStr, "room") == 0 && pRoomId[0])
						GameClient()->m_ClientIndicator.SendUClientChat(aText, pRoomId);
					else
						GameClient()->m_Chat.SendChat(0, aText);
				}
			}
		}
	}
	else if(Request == ERequest::TOPIC)
	{
		if(!Ok)
			m_NextTopic = time_get() + 5 * time_freq();
		else
		{
			const json_value *pApplied = json_object_get(pRoot, "applied");
			if(pApplied && pApplied->type == json_boolean && pApplied->u.boolean)
			{
				m_SentTopic = m_TopicInflight;
				m_NextTopic = time_get();
			}
			else
				m_NextTopic = time_get() + 30 * time_freq();
		}
	}
	else if(Request == ERequest::CONTROL)
	{
		m_NextControl = time_get() + 2 * time_freq();
		const json_value *pCommands = Ok ? json_object_get(pRoot, "commands") : nullptr;
		const json_value *pItem = pCommands && pCommands->type == json_array && json_array_length(pCommands) > 0 ? json_array_get(pCommands, 0) : nullptr;
		const json_value *pId = pItem ? json_object_get(pItem, "id") : nullptr;
		const json_value *pKind = pItem ? json_object_get(pItem, "kind") : nullptr;
		if(pId && pId->type == json_integer && pKind && pKind->type == json_string)
		{
			const json_value *pAddress = json_object_get(pItem, "address");
			const json_value *pPassword = json_object_get(pItem, "password");
			const char *pAddressStr = pAddress && pAddress->type == json_string ? pAddress->u.string.ptr : "";
			const char *pPasswordStr = pPassword && pPassword->type == json_string ? pPassword->u.string.ptr : "";
			if(m_AwaitingConnect || m_ResultReady)
			{
				m_Held = true;
				m_HeldId = (int)pId->u.integer;
				m_HeldKind = pKind->u.string.ptr;
				m_HeldAddress = pAddressStr;
				m_HeldPassword = pPasswordStr;
			}
			else
				ApplyControl((int)pId->u.integer, pKind->u.string.ptr, pAddressStr, pPasswordStr);
		}
	}
	else if(Request == ERequest::CONTROL_RESULT)
	{
		if(!Ok)
			m_NextControl = time_get() + 5 * time_freq();
		else
		{
			m_ResultReady = false;
			m_ControlId = 0;
			if(m_Held)
			{
				m_Held = false;
				const std::string Kind = m_HeldKind;
				const std::string Address = m_HeldAddress;
				const std::string Password = m_HeldPassword;
				const int Id = m_HeldId;
				m_HeldKind.clear();
				m_HeldAddress.clear();
				m_HeldPassword.clear();
				m_HeldId = 0;
				ApplyControl(Id, Kind.c_str(), Address.c_str(), Password.c_str());
			}
			else
				m_NextControl = time_get() + 2 * time_freq();
		}
	}
	if(pRoot)
		json_value_free(pRoot);
}

void CDiscordBridge::OnUpdate()
{
	if(m_pRequest && m_pRequest->Done())
		FinishRequest();
	if(m_pRequest || !GameClient()->m_UClientAccount.IsReady())
		return;
	if(m_AwaitingConnect)
		WatchConnect();
	if(m_ResultReady && time_get() >= m_NextControl)
	{
		BeginControlResult();
		return;
	}
	if(!m_Held && time_get() >= m_NextControl)
	{
		BeginControl();
		return;
	}
	if(m_AwaitingConnect || m_Held)
		return;
	std::string Topic;
	const bool SendTopic = BuildTopic(Topic) && TopicDue(Topic);
	if(SendTopic)
		BeginTopic();
	else if(Client()->State() == IClient::STATE_ONLINE && !m_vPending.empty() && time_get() >= m_NextIngest)
		BeginIngest();
	else if(Client()->State() == IClient::STATE_ONLINE && time_get() >= m_NextPoll)
		BeginPoll();
}
