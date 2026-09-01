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
	if(m_PendingRefreshAt > 0 && Now >= m_PendingRefreshAt && !m_pRequest)
	{
		m_PendingRefreshAt = 0;
		Refresh();
		return;
	}
	// Slow safety net if a UDP room-list push was lost.
	if(!m_InitialRefresh || (!m_pRequest && Now - m_LastRefresh > 300 * time_freq()))
		Refresh();
}

void CUClientChatRooms::RequestRefreshSoon()
{
	if(!GameClient()->m_UClientAccount.IsReady())
		return;
	const int64_t Due = time_get() + time_freq(); // debounce ~1s
	if(m_PendingRefreshAt <= 0 || Due < m_PendingRefreshAt)
		m_PendingRefreshAt = Due;
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

ColorRGBA CUClientChatRooms::RoomNameColorById(const char *pRoomId) const
{
	for(const SRoom &Room : m_vRooms)
	{
		if(!str_comp(Room.m_aId, pRoomId))
		{
			if(Room.m_NameColor != 0)
				return color_cast<ColorRGBA>(ColorHSLA(Room.m_NameColor, false));
			break;
		}
	}
	return DefaultNameColor();
}

void CUClientChatRooms::SelectSendRoom(const char *pRoomId)
{
	str_copy(g_Config.m_UcChatSendRoom, pRoomId ? pRoomId : "", sizeof(g_Config.m_UcChatSendRoom));
	if(g_Config.m_UcChatSendRoom[0])
		g_Config.m_UcChatSendSameServerOnly = 0;
	ConfigManager()->Save();
}

void CUClientChatRooms::AuthHeader(CHttpRequest *pRequest) const
{
	char aAuthorization[192];
	str_format(aAuthorization, sizeof(aAuthorization), "Bearer %s", GameClient()->m_UClientAccount.Secret());
	pRequest->HeaderString("Authorization", aAuthorization);
	pRequest->HeaderString("x-uclient-install-id", GameClient()->m_UClientAccount.InstallId());
}

bool CUClientChatRooms::Begin(std::shared_ptr<CHttpRequest> pRequest, ERequest Request)
{
	if(!GameClient()->m_UClientAccount.IsReady())
	{
		str_copy(m_aError, Localize("UClient account is not ready yet. Try again in a moment."), sizeof(m_aError));
		return false;
	}
	if(m_pRequest)
	{
		str_copy(m_aError, Localize("Another room request is already in progress. Try again shortly."), sizeof(m_aError));
		return false;
	}
	AuthHeader(pRequest.get());
	pRequest->FailOnErrorStatus(false);
	pRequest->LogProgress(HTTPLOG::FAILURE);
	pRequest->Timeout(CTimeout{5000, 15000, 500, 5});
	m_pRequest = std::move(pRequest);
	m_Request = Request;
	m_aError[0] = '\0';
	Http()->Run(m_pRequest);
	return true;
}

void CUClientChatRooms::Refresh()
{
	if(m_pRequest || !GameClient()->m_UClientAccount.IsReady())
		return;
	char aUrl[384];
	str_format(aUrl, sizeof(aUrl), "%s/rooms", g_Config.m_UcApiBaseUrl);
	if(Begin(HttpGet(aUrl), ERequest::REFRESH))
	{
		m_InitialRefresh = true;
		m_LastRefresh = time_get();
	}
}

void CUClientChatRooms::RefreshIfStale(int MaxAgeSeconds)
{
	if(MaxAgeSeconds <= 0 || m_pRequest || !GameClient()->m_UClientAccount.IsReady())
		return;
	if(!m_InitialRefresh || time_get() - m_LastRefresh >= (int64_t)MaxAgeSeconds * time_freq())
		Refresh();
}

bool CUClientChatRooms::BeginJsonPost(const char *pPath, const char *pJson)
{
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s%s", g_Config.m_UcApiBaseUrl, pPath);
	return Begin(HttpPostJson(aUrl, pJson), ERequest::MUTATE);
}

void CUClientChatRooms::BeginDelete(const char *pPath)
{
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s%s", g_Config.m_UcApiBaseUrl, pPath);
	auto pRequest = HttpGet(aUrl);
	pRequest->Delete();
	Begin(std::move(pRequest), ERequest::MUTATE);
}

bool CUClientChatRooms::Create(const char *pName, const char *pDisplayName)
{
	if(!pDisplayName || !pDisplayName[0])
	{
		str_copy(m_aError, Localize("Set a player name before creating or joining a room."), sizeof(m_aError));
		return false;
	}
	const std::string Name = JsonEscape(pName);
	const std::string DisplayName = JsonEscape(pDisplayName);
	char aJson[384];
	str_format(aJson, sizeof(aJson), "{\"name\":\"%s\",\"display_name\":\"%s\"}", Name.c_str(), DisplayName.c_str());
	return BeginJsonPost("/rooms", aJson);
}

void CUClientChatRooms::Rename(const char *pRoomId, const char *pName)
{
	UpdateSettings(pRoomId, pName, true, 0, false, false, false);
}

void CUClientChatRooms::UpdateSettings(const char *pRoomId, const char *pName, bool HasName, unsigned NameColor, bool HasColor, bool InviteCodePublic, bool HasInviteCodePublic)
{
	if(!HasName && !HasColor && !HasInviteCodePublic)
		return;
	char aPath[160];
	str_format(aPath, sizeof(aPath), "/rooms/%s", pRoomId);

	std::string Json = "{";
	bool First = true;
	auto AppendField = [&](const char *pField) {
		if(!First)
			Json += ",";
		First = false;
		Json += pField;
	};
	if(HasName)
	{
		char aBuf[192];
		str_format(aBuf, sizeof(aBuf), "\"name\":\"%s\"", JsonEscape(pName).c_str());
		AppendField(aBuf);
	}
	if(HasColor)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "\"name_color\":%u", NameColor);
		AppendField(aBuf);
	}
	if(HasInviteCodePublic)
		AppendField(InviteCodePublic ? "\"invite_code_public\":true" : "\"invite_code_public\":false");
	Json += "}";
	BeginJsonPost(aPath, Json.c_str());
}

void CUClientChatRooms::RegenerateCode(const char *pRoomId)
{
	char aPath[192];
	str_format(aPath, sizeof(aPath), "/rooms/%s/invite-code", pRoomId);
	BeginJsonPost(aPath, "{}");
}

bool CUClientChatRooms::Join(const char *pCode, const char *pDisplayName)
{
	if(!pDisplayName || !pDisplayName[0])
	{
		str_copy(m_aError, Localize("Set a player name before creating or joining a room."), sizeof(m_aError));
		return false;
	}
	char aCode[40];
	str_copy(aCode, str_utf8_skip_whitespaces(pCode ? pCode : ""), sizeof(aCode));
	for(int i = str_length(aCode); i > 0 && str_isspace(aCode[i - 1]); --i)
		aCode[i - 1] = '\0';
	if(str_length(aCode) < 6)
	{
		str_copy(m_aError, Localize("Please enter a valid invite code."), sizeof(m_aError));
		return false;
	}
	const std::string Code = JsonEscape(aCode);
	const std::string DisplayName = JsonEscape(pDisplayName);
	char aJson[384];
	str_format(aJson, sizeof(aJson), "{\"code\":\"%s\",\"display_name\":\"%s\"}", Code.c_str(), DisplayName.c_str());
	return BeginJsonPost("/rooms/join", aJson);
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

void CUClientChatRooms::TransferOwnership(const char *pRoomId, const char *pMemberId)
{
	char aPath[192];
	str_format(aPath, sizeof(aPath), "/rooms/%s/transfer", pRoomId);
	const std::string MemberId = JsonEscape(pMemberId);
	char aJson[192];
	str_format(aJson, sizeof(aJson), "{\"member_id\":\"%s\"}", MemberId.c_str());
	BeginJsonPost(aPath, aJson);
}

void CUClientChatRooms::SetMemberAdmin(const char *pRoomId, const char *pMemberId, bool Admin)
{
	char aPath[256];
	str_format(aPath, sizeof(aPath), "/rooms/%s/members/%s/role", pRoomId, pMemberId);
	char aJson[64];
	str_format(aJson, sizeof(aJson), "{\"role\":\"%s\"}", Admin ? "admin" : "member");
	BeginJsonPost(aPath, aJson);
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
		Room.m_Admin = JsonBool(pRoom, "is_admin");
		Room.m_InviteCodePublic = JsonBool(pRoom, "invite_code_public");
		const json_value *pNameColor = json_object_get(pRoom, "name_color");
		if(pNameColor && pNameColor->type == json_integer)
			Room.m_NameColor = (unsigned)pNameColor->u.integer;
		else if(pNameColor && pNameColor->type == json_double)
			Room.m_NameColor = (unsigned)pNameColor->u.dbl;
		else
			Room.m_NameColor = 0;
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
				const char *pRole = JsonString(pMember, "role");
				Member.m_Owner = !str_comp(pRole, "owner");
				Member.m_Admin = !str_comp(pRole, "admin");
				Member.m_Self = JsonBool(pMember, "is_self");
				Room.m_vMembers.push_back(Member);
			}
		}
		if(Room.m_aId[0] && Room.m_aName[0])
			vRooms.push_back(std::move(Room));
	}
	m_vRooms = std::move(vRooms);
	GameClient()->m_Chat.RebuildChat();
	if(g_Config.m_UcChatSendRoom[0] && !RoomNameById(g_Config.m_UcChatSendRoom))
		SelectSendRoom("");
	if(g_Config.m_UcServerJoinSendRoom[0] && !RoomNameById(g_Config.m_UcServerJoinSendRoom))
	{
		g_Config.m_UcServerJoinSendRoom[0] = '\0';
		ConfigManager()->Save();
	}
}
