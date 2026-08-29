#include "chat_rooms.h"

#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#include <string>

namespace
{
const char *JsonString(const json_value *pObject, const char *pKey)
{
	const json_value *pValue = json_object_get(pObject, pKey);
	return pValue && pValue->type == json_string ? pValue->u.string.ptr : "";
}

bool JsonBool(const json_value *pObject, const char *pKey)
{
	const json_value *pValue = json_object_get(pObject, pKey);
	return pValue && pValue->type == json_boolean && pValue->u.boolean;
}

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

void CUClientChatRooms::OnInit()
{
}

void CUClientChatRooms::OnShutdown()
{
	if(m_pRequest)
		m_pRequest->Abort();
}

void CUClientChatRooms::OnUpdate()
{
	if(m_pRequest && m_pRequest->Done())
		Finish();
	if(!GameClient()->m_UClientAccount.IsReady())
		return;
	const int64_t Now = time_get();
	if(!m_InitialRefresh || (!m_pRequest && Now - m_LastRefresh > 60 * time_freq()))
		Refresh();
}

const char *CUClientChatRooms::SelectedSendRoomId() const
{
	return g_Config.m_UcChatSendRoom;
}

const char *CUClientChatRooms::RoomNameById(const char *pRoomId) const
{
	for(const SRoom &Room : m_vRooms)
		if(!str_comp(Room.m_aId, pRoomId))
			return Room.m_aName;
	return nullptr;
}

void CUClientChatRooms::SelectSendRoom(const char *pRoomId)
{
	str_copy(g_Config.m_UcChatSendRoom, pRoomId ? pRoomId : "", sizeof(g_Config.m_UcChatSendRoom));
	ConfigManager()->Save();
}

void CUClientChatRooms::AuthHeader(CHttpRequest *pRequest) const
{
	char aAuthorization[192];
	str_format(aAuthorization, sizeof(aAuthorization), "Bearer %s", GameClient()->m_UClientAccount.Secret());
	pRequest->HeaderString("Authorization", aAuthorization);
	pRequest->HeaderString("x-uclient-install-id", GameClient()->m_UClientAccount.InstallId());
}

void CUClientChatRooms::Begin(std::shared_ptr<CHttpRequest> pRequest, ERequest Request)
{
	if(m_pRequest || !GameClient()->m_UClientAccount.IsReady())
		return;
	AuthHeader(pRequest.get());
	pRequest->FailOnErrorStatus(false);
	pRequest->Timeout(CTimeout{5000, 15000, 500, 5});
	m_pRequest = std::move(pRequest);
	m_Request = Request;
	m_aError[0] = '\0';
	Http()->Run(m_pRequest);
}

void CUClientChatRooms::Refresh()
{
	if(m_pRequest || !GameClient()->m_UClientAccount.IsReady())
		return;
	char aUrl[384];
	str_format(aUrl, sizeof(aUrl), "%s/rooms", g_Config.m_UcApiBaseUrl);
	Begin(HttpGet(aUrl), ERequest::REFRESH);
	m_InitialRefresh = true;
	m_LastRefresh = time_get();
}

void CUClientChatRooms::RefreshIfStale(int MaxAgeSeconds)
{
	if(MaxAgeSeconds <= 0 || m_pRequest || !GameClient()->m_UClientAccount.IsReady())
		return;
	if(!m_InitialRefresh || time_get() - m_LastRefresh >= (int64_t)MaxAgeSeconds * time_freq())
		Refresh();
}

void CUClientChatRooms::BeginJsonPost(const char *pPath, const char *pJson)
{
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s%s", g_Config.m_UcApiBaseUrl, pPath);
	Begin(HttpPostJson(aUrl, pJson), ERequest::MUTATE);
}

void CUClientChatRooms::BeginDelete(const char *pPath)
{
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s%s", g_Config.m_UcApiBaseUrl, pPath);
	auto pRequest = HttpGet(aUrl);
	pRequest->Delete();
	Begin(std::move(pRequest), ERequest::MUTATE);
}

void CUClientChatRooms::Create(const char *pName, const char *pDisplayName)
{
	const std::string Name = JsonEscape(pName);
	const std::string DisplayName = JsonEscape(pDisplayName);
	char aJson[384];
	str_format(aJson, sizeof(aJson), "{\"name\":\"%s\",\"display_name\":\"%s\"}", Name.c_str(), DisplayName.c_str());
	BeginJsonPost("/rooms", aJson);
}

void CUClientChatRooms::Rename(const char *pRoomId, const char *pName)
{
	const std::string Name = JsonEscape(pName);
	char aPath[160];
	str_format(aPath, sizeof(aPath), "/rooms/%s", pRoomId);
	char aJson[192];
	str_format(aJson, sizeof(aJson), "{\"name\":\"%s\"}", Name.c_str());
	BeginJsonPost(aPath, aJson);
}

void CUClientChatRooms::RegenerateCode(const char *pRoomId)
{
	char aPath[192];
	str_format(aPath, sizeof(aPath), "/rooms/%s/invite-code", pRoomId);
	BeginJsonPost(aPath, "{}");
}

void CUClientChatRooms::Join(const char *pCode, const char *pDisplayName)
{
	const std::string Code = JsonEscape(pCode);
	const std::string DisplayName = JsonEscape(pDisplayName);
	char aJson[384];
	str_format(aJson, sizeof(aJson), "{\"code\":\"%s\",\"display_name\":\"%s\"}", Code.c_str(), DisplayName.c_str());
	BeginJsonPost("/rooms/join", aJson);
}

void CUClientChatRooms::Kick(const char *pRoomId, const char *pMemberId)
{
	char aPath[256];
	str_format(aPath, sizeof(aPath), "/rooms/%s/members/%s", pRoomId, pMemberId);
	BeginDelete(aPath);
}

void CUClientChatRooms::Leave(const char *pRoomId)
{
	char aPath[192];
	str_format(aPath, sizeof(aPath), "/rooms/%s/members/me", pRoomId);
	BeginDelete(aPath);
}

void CUClientChatRooms::Finish()
{
	const ERequest Request = m_Request;
	const EHttpState State = m_pRequest->State();
	const int Status = State == EHttpState::DONE ? m_pRequest->StatusCode() : 0;
	json_value *pRoot = State == EHttpState::DONE ? m_pRequest->ResultJson() : nullptr;
	m_pRequest = nullptr;
	m_Request = ERequest::NONE;
	if(State != EHttpState::DONE)
	{
		str_copy(m_aError, Localize("Could not contact the UClient rooms service."), sizeof(m_aError));
		return;
	}
	if(Status >= 200 && Status < 300)
	{
		if(Request == ERequest::REFRESH && pRoot && pRoot->type == json_object)
			ParseRooms(pRoot);
		if(pRoot)
			json_value_free(pRoot);
		if(Request == ERequest::MUTATE)
			Refresh();
		return;
	}
	const char *pMessage = pRoot && pRoot->type == json_object ? JsonString(pRoot, "message") : "";
	str_copy(m_aError, pMessage[0] ? pMessage : Localize("The UClient room request failed."), sizeof(m_aError));
	if(pRoot)
		json_value_free(pRoot);
}

void CUClientChatRooms::ParseRooms(const json_value *pRoot)
{
	const json_value *pRooms = json_object_get(pRoot, "rooms");
	if(!pRooms || pRooms->type != json_array)
		return;
	std::vector<SRoom> vRooms;
	for(unsigned RoomIndex = 0; RoomIndex < pRooms->u.array.length; ++RoomIndex)
	{
		const json_value *pRoom = pRooms->u.array.values[RoomIndex];
		if(!pRoom || pRoom->type != json_object)
			continue;
		SRoom Room;
		str_copy(Room.m_aId, JsonString(pRoom, "id"), sizeof(Room.m_aId));
		str_copy(Room.m_aName, JsonString(pRoom, "name"), sizeof(Room.m_aName));
		str_copy(Room.m_aInviteCode, JsonString(pRoom, "invite_code"), sizeof(Room.m_aInviteCode));
		Room.m_Owner = JsonBool(pRoom, "is_owner");
		const json_value *pMembers = json_object_get(pRoom, "members");
		if(pMembers && pMembers->type == json_array)
		{
			for(unsigned MemberIndex = 0; MemberIndex < pMembers->u.array.length; ++MemberIndex)
			{
				const json_value *pMember = pMembers->u.array.values[MemberIndex];
				if(!pMember || pMember->type != json_object)
					continue;
				SMember Member;
				str_copy(Member.m_aId, JsonString(pMember, "member_id"), sizeof(Member.m_aId));
				str_copy(Member.m_aDisplayName, JsonString(pMember, "display_name"), sizeof(Member.m_aDisplayName));
				Member.m_Owner = !str_comp(JsonString(pMember, "role"), "owner");
				Room.m_vMembers.push_back(Member);
			}
		}
		if(Room.m_aId[0] && Room.m_aName[0])
			vRooms.push_back(std::move(Room));
	}
	m_vRooms = std::move(vRooms);
	if(g_Config.m_UcChatSendRoom[0] && !RoomNameById(g_Config.m_UcChatSendRoom))
		SelectSendRoom("");
	if(g_Config.m_UcServerJoinSendRoom[0] && !RoomNameById(g_Config.m_UcServerJoinSendRoom))
	{
		g_Config.m_UcServerJoinSendRoom[0] = '\0';
		ConfigManager()->Save();
	}
}
