#include "automation.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/controls.h>
#include <game/client/gameclient.h>

// Reading the foreground window title needs the Win32 API. It is included last
// and lean so its macros cannot leak into any DDNet header.
#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static constexpr const char *SHORTCUTS_FILE = "uclient_shortcuts.json";
static constexpr const char *TEST_RUN_REQUEST_FILE = "uclient_automation_run.json";
static constexpr const char *TEST_RUN_STATE_FILE = "uclient_automation_state.json";

static const char *TargetPrefix(CAutomation::ETarget Target)
{
	return Target == CAutomation::ETarget::DUMMY ? "dummy" : "player";
}

static bool ReadFileToBuffer(IStorage *pStorage, const char *pPath, char **ppOut, unsigned *pSizeOut)
{
	*ppOut = nullptr;
	*pSizeOut = 0;
	void *pData = nullptr;
	if(!pStorage->ReadFile(pPath, IStorage::TYPE_SAVE, &pData, pSizeOut))
		return false;
	*ppOut = (char *)pData;
	return true;
}

static CAutomation::EChatChannel ParseChannel(const char *pValue)
{
	if(!pValue)
		return CAutomation::EChatChannel::ALL;
	if(str_comp_nocase(pValue, "team") == 0)
		return CAutomation::EChatChannel::TEAM;
	if(str_comp_nocase(pValue, "uclient") == 0)
		return CAutomation::EChatChannel::UCLIENT;
	return CAutomation::EChatChannel::ALL;
}

static CAutomation::ESenderFilter ParseSender(const char *pValue)
{
	if(!pValue)
		return CAutomation::ESenderFilter::EVERYONE;
	if(str_comp_nocase(pValue, "me") == 0)
		return CAutomation::ESenderFilter::ME;
	if(str_comp_nocase(pValue, "specific") == 0)
		return CAutomation::ESenderFilter::SPECIFIC;
	return CAutomation::ESenderFilter::EVERYONE;
}

static CAutomation::ETextMatch ParseMatch(const char *pValue)
{
	if(!pValue)
		return CAutomation::ETextMatch::CONTAINS;
	if(str_comp_nocase(pValue, "equals") == 0)
		return CAutomation::ETextMatch::EQUALS;
	if(str_comp_nocase(pValue, "starts_with") == 0 || str_comp_nocase(pValue, "starts with") == 0)
		return CAutomation::ETextMatch::STARTS_WITH;
	return CAutomation::ETextMatch::CONTAINS;
}

static CAutomation::ETarget ParseTarget(const char *pValue)
{
	if(pValue && str_comp_nocase(pValue, "dummy") == 0)
		return CAutomation::ETarget::DUMMY;
	return CAutomation::ETarget::PLAYER;
}

// Colors may arrive as a double, so accept both number kinds and clamp to RGB.
static int ParseColorValue(const json_value *pColor)
{
	if(!pColor)
		return 0;
	double Value = 0.0;
	if(pColor->type == json_integer)
		Value = (double)pColor->u.integer;
	else if(pColor->type == json_double)
		Value = pColor->u.dbl;
	else
		return 0;
	if(Value < 0.0)
		return 0;
	if(Value > 0xFFFFFF)
		return 0xFFFFFF;
	return (int)(Value + 0.5);
}

static int ParseWeapon(const char *pValue)
{
	if(!pValue)
		return 0;
	if(str_comp_nocase(pValue, "gun") == 0 || str_comp_nocase(pValue, "pistol") == 0)
		return 1;
	if(str_comp_nocase(pValue, "shotgun") == 0)
		return 2;
	if(str_comp_nocase(pValue, "grenade") == 0)
		return 3;
	if(str_comp_nocase(pValue, "laser") == 0)
		return 4;
	return 0; // hammer default
}

static bool ParseChatFilter(const json_value *pFilter, CAutomation::SChatFilter &Out)
{
	if(!pFilter || pFilter->type != json_object)
		return false;
	const json_value *pKind = json_object_get(pFilter, "kind");
	const char *pKindStr = pKind && pKind->type == json_string ? pKind->u.string.ptr : nullptr;
	if(!pKindStr)
		return false;
	if(str_comp(pKindStr, "sender") == 0)
	{
		Out.m_Kind = CAutomation::SChatFilter::EKind::SENDER;
		const json_value *pNames = json_object_get(pFilter, "names");
		if(pNames && pNames->type == json_array)
		{
			const int Count = json_array_length(pNames);
			for(int i = 0; i < Count; i++)
			{
				const json_value *pName = json_array_get(pNames, i);
				if(pName && pName->type == json_string && pName->u.string.ptr && pName->u.string.ptr[0])
					Out.m_SenderNames.emplace_back(pName->u.string.ptr);
			}
			return true;
		}
		const json_value *pSender = json_object_get(pFilter, "sender");
		Out.m_Sender = ParseSender(pSender && pSender->type == json_string ? pSender->u.string.ptr : nullptr);
		const json_value *pSenderName = json_object_get(pFilter, "senderName");
		if(pSenderName && pSenderName->type == json_string && pSenderName->u.string.ptr)
			Out.m_SenderName = pSenderName->u.string.ptr;
		return true;
	}
	if(str_comp(pKindStr, "message") == 0)
	{
		Out.m_Kind = CAutomation::SChatFilter::EKind::MESSAGE;
		const json_value *pMatch = json_object_get(pFilter, "match");
		Out.m_Match = ParseMatch(pMatch && pMatch->type == json_string ? pMatch->u.string.ptr : nullptr);
		const json_value *pText = json_object_get(pFilter, "text");
		if(pText && pText->type == json_string && pText->u.string.ptr)
			Out.m_Text = pText->u.string.ptr;
		return true;
	}
	if(str_comp(pKindStr, "chat_channel") == 0)
	{
		Out.m_Kind = CAutomation::SChatFilter::EKind::CHAT_CHANNEL;
		const json_value *pChannel = json_object_get(pFilter, "channel");
		if(pChannel && pChannel->type == json_string && pChannel->u.string.ptr)
			Out.m_Text = pChannel->u.string.ptr;
		return !Out.m_Text.empty();
	}
	if(str_comp(pKindStr, "uclient_room") == 0)
	{
		Out.m_Kind = CAutomation::SChatFilter::EKind::UCLIENT_ROOM;
		const json_value *pRoom = json_object_get(pFilter, "room");
		if(pRoom && pRoom->type == json_string && pRoom->u.string.ptr)
			Out.m_Text = pRoom->u.string.ptr;
		return true;
	}
	return false;
}

static bool ParseServerTargets(const json_value *pTrigger, CAutomation::STrigger &Out)
{
	const json_value *pTargets = json_object_get(pTrigger, "targets");
	if(!pTargets || pTargets->type != json_array)
		return true;
	const int Count = json_array_length(pTargets);
	for(int i = 0; i < Count; ++i)
	{
		const json_value *pTarget = json_array_get(pTargets, i);
		if(pTarget && pTarget->type == json_string && pTarget->u.string.ptr && pTarget->u.string.ptr[0])
			Out.m_ServerTargets.emplace_back(pTarget->u.string.ptr);
	}
	return true;
}

static bool ParseTrigger(const json_value *pTrigger, CAutomation::STrigger &Out)
{
	if(!pTrigger || pTrigger->type != json_object)
		return false;
	const json_value *pType = json_object_get(pTrigger, "type");
	const char *pTypeStr = pType && pType->type == json_string ? pType->u.string.ptr : nullptr;
	if(!pTypeStr)
		return false;
	Out = CAutomation::STrigger{};
	if(str_comp(pTypeStr, "server_connect") == 0)
	{
		Out.m_Type = CAutomation::ETriggerType::SERVER_CONNECT;
		return ParseServerTargets(pTrigger, Out);
	}
	if(str_comp(pTypeStr, "chat_received") != 0)
		return false;
	Out.m_Type = CAutomation::ETriggerType::CHAT_RECEIVED;
	const json_value *pChannel = json_object_get(pTrigger, "channel");
	Out.m_Channel = ParseChannel(pChannel && pChannel->type == json_string ? pChannel->u.string.ptr : nullptr);

	const json_value *pFilters = json_object_get(pTrigger, "filters");
	if(pFilters && pFilters->type == json_array && json_array_length(pFilters) > 0)
	{
		const int Count = json_array_length(pFilters);
		for(int i = 0; i < Count; ++i)
		{
			CAutomation::SChatFilter Filter;
			if(ParseChatFilter(json_array_get(pFilters, i), Filter))
				Out.m_Filters.push_back(std::move(Filter));
		}
		return true;
	}

	// Legacy flat trigger format.
	CAutomation::SChatFilter SenderFilter;
	SenderFilter.m_Kind = CAutomation::SChatFilter::EKind::SENDER;
	const json_value *pSender = json_object_get(pTrigger, "sender");
	SenderFilter.m_Sender = ParseSender(pSender && pSender->type == json_string ? pSender->u.string.ptr : nullptr);
	const json_value *pSenderName = json_object_get(pTrigger, "senderName");
	if(pSenderName && pSenderName->type == json_string && pSenderName->u.string.ptr)
		SenderFilter.m_SenderName = pSenderName->u.string.ptr;
	Out.m_Filters.push_back(std::move(SenderFilter));

	CAutomation::SChatFilter MessageFilter;
	MessageFilter.m_Kind = CAutomation::SChatFilter::EKind::MESSAGE;
	const json_value *pMatch = json_object_get(pTrigger, "match");
	MessageFilter.m_Match = ParseMatch(pMatch && pMatch->type == json_string ? pMatch->u.string.ptr : nullptr);
	const json_value *pText = json_object_get(pTrigger, "text");
	if(!pText || pText->type != json_string || !pText->u.string.ptr)
		return false;
	MessageFilter.m_Text = pText->u.string.ptr;
	Out.m_Filters.push_back(std::move(MessageFilter));
	return true;
}

static CAutomation::EValueMode ParseValueMode(const char *pValue)
{
	if(pValue && str_comp(pValue, "variable") == 0)
		return CAutomation::EValueMode::VARIABLE;
	return CAutomation::EValueMode::TEXT;
}

static bool ParseMessageField(const json_value *pMessage, CAutomation::SAction &Out)
{
	if(!pMessage)
		return false;
	if(pMessage->type == json_string)
	{
		Out.m_MessageMode = CAutomation::EValueMode::TEXT;
		Out.m_Message = pMessage->u.string.ptr;
		return true;
	}
	if(pMessage->type != json_object)
		return false;
	const json_value *pMode = json_object_get(pMessage, "mode");
	const char *pModeStr = pMode && pMode->type == json_string ? pMode->u.string.ptr : "text";
	Out.m_MessageMode = ParseValueMode(pModeStr);
	if(Out.m_MessageMode == CAutomation::EValueMode::VARIABLE)
	{
		const json_value *pVariable = json_object_get(pMessage, "variable");
		if(!pVariable || pVariable->type != json_string || !pVariable->u.string.ptr)
			return false;
		Out.m_Variable = pVariable->u.string.ptr;
		return true;
	}
	const json_value *pText = json_object_get(pMessage, "text");
	if(!pText || pText->type != json_string)
	{
		Out.m_Message.clear();
		return true;
	}
	Out.m_Message = pText->u.string.ptr;
	return true;
}

static bool ParseChannelField(const json_value *pChannel, CAutomation::SAction &Out)
{
	if(!pChannel)
		return false;
	if(pChannel->type == json_string)
	{
		Out.m_ChannelMode = CAutomation::EValueMode::TEXT;
		Out.m_Channel = ParseChannel(pChannel->u.string.ptr);
		return true;
	}
	if(pChannel->type != json_object)
		return false;
	const json_value *pMode = json_object_get(pChannel, "mode");
	const char *pModeStr = pMode && pMode->type == json_string ? pMode->u.string.ptr : "text";
	Out.m_ChannelMode = ParseValueMode(pModeStr);
	if(Out.m_ChannelMode == CAutomation::EValueMode::VARIABLE)
	{
		const json_value *pVariable = json_object_get(pChannel, "variable");
		if(!pVariable || pVariable->type != json_string || !pVariable->u.string.ptr)
			return false;
		Out.m_ChannelVariable = pVariable->u.string.ptr;
		return true;
	}
	const json_value *pText = json_object_get(pChannel, "text");
	Out.m_Channel = ParseChannel(pText && pText->type == json_string ? pText->u.string.ptr : "all");
	return true;
}

static bool ParseUClientRoomField(const json_value *pRoom, CAutomation::SAction &Out)
{
	if(!pRoom)
	{
		Out.m_UClientRoomMode = CAutomation::EValueMode::TEXT;
		Out.m_UClientRoomId.clear();
		return true;
	}
	if(pRoom->type == json_string)
	{
		Out.m_UClientRoomMode = CAutomation::EValueMode::TEXT;
		Out.m_UClientRoomId = pRoom->u.string.ptr ? pRoom->u.string.ptr : "";
		return true;
	}
	if(pRoom->type != json_object)
		return false;
	const json_value *pMode = json_object_get(pRoom, "mode");
	const char *pModeStr = pMode && pMode->type == json_string ? pMode->u.string.ptr : "text";
	Out.m_UClientRoomMode = ParseValueMode(pModeStr);
	if(Out.m_UClientRoomMode == CAutomation::EValueMode::VARIABLE)
	{
		const json_value *pVariable = json_object_get(pRoom, "variable");
		if(!pVariable || pVariable->type != json_string || !pVariable->u.string.ptr)
			return false;
		Out.m_UClientRoomVariable = pVariable->u.string.ptr;
		return true;
	}
	const json_value *pText = json_object_get(pRoom, "text");
	if(pText && pText->type == json_string && pText->u.string.ptr)
		Out.m_UClientRoomId = pText->u.string.ptr;
	else
		Out.m_UClientRoomId.clear();
	return true;
}

static bool ParseTextParts(const json_value *pParts, std::vector<CAutomation::STextPart> &Out)
{
	if(!pParts || pParts->type != json_array)
		return false;
	const int Count = json_array_length(pParts);
	for(int i = 0; i < Count; ++i)
	{
		const json_value *pPart = json_array_get(pParts, i);
		if(!pPart || pPart->type != json_object)
			continue;
		CAutomation::STextPart Part;
		const json_value *pMode = json_object_get(pPart, "mode");
		const char *pModeStr = pMode && pMode->type == json_string ? pMode->u.string.ptr : "text";
		Part.m_Mode = ParseValueMode(pModeStr);
		if(Part.m_Mode == CAutomation::EValueMode::VARIABLE)
		{
			const json_value *pVariable = json_object_get(pPart, "variable");
			if(!pVariable || pVariable->type != json_string || !pVariable->u.string.ptr)
				continue;
			Part.m_Variable = pVariable->u.string.ptr;
		}
		else
		{
			const json_value *pText = json_object_get(pPart, "text");
			if(pText && pText->type == json_string && pText->u.string.ptr)
				Part.m_Text = pText->u.string.ptr;
		}
		Out.push_back(std::move(Part));
	}
	return !Out.empty();
}

static bool ParseAction(const json_value *pAction, CAutomation::SAction &Out)
{
	if(!pAction || pAction->type != json_object)
		return false;
	const json_value *pType = json_object_get(pAction, "type");
	const char *pTypeStr = pType && pType->type == json_string ? pType->u.string.ptr : nullptr;
	if(!pTypeStr)
		return false;

	if(str_comp(pTypeStr, "send_chat") == 0)
	{
		Out.m_Type = CAutomation::EActionType::SEND_CHAT;
		const json_value *pChannel = json_object_get(pAction, "channel");
		if(!ParseChannelField(pChannel, Out))
			return false;
		const json_value *pUClientRoom = json_object_get(pAction, "uclientRoom");
		if(!ParseUClientRoomField(pUClientRoom, Out))
			return false;
		const json_value *pMessage = json_object_get(pAction, "message");
		return ParseMessageField(pMessage, Out);
	}
	if(str_comp(pTypeStr, "text") == 0)
	{
		Out.m_Type = CAutomation::EActionType::TEXT;
		const json_value *pParts = json_object_get(pAction, "parts");
		if(!ParseTextParts(pParts, Out.m_TextParts))
			return false;
		const json_value *pAs = json_object_get(pAction, "as");
		if(pAs && pAs->type == json_string && pAs->u.string.ptr && pAs->u.string.ptr[0])
			Out.m_OutputVariable = pAs->u.string.ptr;
		else
			Out.m_OutputVariable = "text";
		return true;
	}
	if(str_comp(pTypeStr, "wait") == 0)
	{
		Out.m_Type = CAutomation::EActionType::WAIT;
		const json_value *pSeconds = json_object_get(pAction, "seconds");
		Out.m_Seconds = pSeconds && pSeconds->type == json_double ? pSeconds->u.dbl :
									 pSeconds && pSeconds->type == json_integer ? (double)pSeconds->u.integer :
														 1.0;
		// The editor only allows whole seconds, one or more.
		Out.m_Seconds = Out.m_Seconds < 1.0 ? 1.0 : (double)(int64_t)(Out.m_Seconds + 0.5);
		return true;
	}
	if(str_comp(pTypeStr, "switch_weapon_use") == 0)
	{
		Out.m_Type = CAutomation::EActionType::SWITCH_WEAPON_USE;
		const json_value *pWeapon = json_object_get(pAction, "weapon");
		Out.m_Weapon = ParseWeapon(pWeapon && pWeapon->type == json_string ? pWeapon->u.string.ptr : nullptr);
		return true;
	}
	if(str_comp(pTypeStr, "set_skin") == 0)
	{
		Out.m_Type = CAutomation::EActionType::SET_SKIN;
		const json_value *pTarget = json_object_get(pAction, "target");
		Out.m_Target = ParseTarget(pTarget && pTarget->type == json_string ? pTarget->u.string.ptr : nullptr);
		const json_value *pSkin = json_object_get(pAction, "skin");
		if(!pSkin || pSkin->type != json_string || !pSkin->u.string.ptr)
			return false;
		Out.m_Skin = pSkin->u.string.ptr;
		return true;
	}
	if(str_comp(pTypeStr, "set_custom_color") == 0)
	{
		Out.m_Type = CAutomation::EActionType::SET_CUSTOM_COLOR;
		const json_value *pTarget = json_object_get(pAction, "target");
		Out.m_Target = ParseTarget(pTarget && pTarget->type == json_string ? pTarget->u.string.ptr : nullptr);
		const json_value *pEnabled = json_object_get(pAction, "enabled");
		Out.m_CustomColorEnabled = !pEnabled || json_boolean_get(pEnabled);
		return true;
	}
	if(str_comp(pTypeStr, "set_body_color") == 0)
	{
		Out.m_Type = CAutomation::EActionType::SET_BODY_COLOR;
		const json_value *pTarget = json_object_get(pAction, "target");
		Out.m_Target = ParseTarget(pTarget && pTarget->type == json_string ? pTarget->u.string.ptr : nullptr);
		Out.m_Color = ParseColorValue(json_object_get(pAction, "color"));
		return true;
	}
	if(str_comp(pTypeStr, "set_feet_color") == 0)
	{
		Out.m_Type = CAutomation::EActionType::SET_FEET_COLOR;
		const json_value *pTarget = json_object_get(pAction, "target");
		Out.m_Target = ParseTarget(pTarget && pTarget->type == json_string ? pTarget->u.string.ptr : nullptr);
		Out.m_Color = ParseColorValue(json_object_get(pAction, "color"));
		return true;
	}
	if(str_comp(pTypeStr, "set_name") == 0)
	{
		Out.m_Type = CAutomation::EActionType::SET_NAME;
		const json_value *pTarget = json_object_get(pAction, "target");
		Out.m_Target = ParseTarget(pTarget && pTarget->type == json_string ? pTarget->u.string.ptr : nullptr);
		const json_value *pName = json_object_get(pAction, "name");
		if(!pName || pName->type != json_string || !pName->u.string.ptr)
			return false;
		Out.m_Name = pName->u.string.ptr;
		return true;
	}
	if(str_comp(pTypeStr, "get") == 0)
	{
		Out.m_Type = CAutomation::EActionType::GET;
		const json_value *pProperty = json_object_get(pAction, "property");
		if(!pProperty || pProperty->type != json_string || !pProperty->u.string.ptr)
			return false;
		Out.m_GetProperty = pProperty->u.string.ptr;
		return true;
	}
	if(str_comp(pTypeStr, "get_clipboard") == 0)
	{
		Out.m_Type = CAutomation::EActionType::GET_CLIPBOARD;
		const json_value *pAs = json_object_get(pAction, "as");
		if(pAs && pAs->type == json_string && pAs->u.string.ptr && pAs->u.string.ptr[0])
			Out.m_OutputVariable = pAs->u.string.ptr;
		else
			Out.m_OutputVariable = "clipboard";
		return true;
	}
	if(str_comp(pTypeStr, "repeat") == 0)
	{
		Out.m_Type = CAutomation::EActionType::REPEAT;
		int Count = 1;
		const json_value *pCount = json_object_get(pAction, "count");
		if(pCount)
		{
			if(pCount->type == json_integer)
				Count = (int)pCount->u.integer;
			else if(pCount->type == json_double)
				Count = (int)pCount->u.dbl;
		}
		if(Count < 1)
			Count = 1;
		if(Count > 99)
			Count = 99;
		Out.m_RepeatCount = Count;
		return true;
	}
	if(str_comp(pTypeStr, "end_repeat") == 0)
	{
		Out.m_Type = CAutomation::EActionType::END_REPEAT;
		return true;
	}
	if(str_comp(pTypeStr, "if") == 0)
	{
		Out.m_Type = CAutomation::EActionType::IF;
		const json_value *pConditions = json_object_get(pAction, "conditions");
		if(pConditions && pConditions->type == json_array)
		{
			const json_value *pMatch = json_object_get(pAction, "match");
			if(pMatch && pMatch->type == json_string && pMatch->u.string.ptr && str_comp_nocase(pMatch->u.string.ptr, "any") == 0)
				Out.m_IfMatch = "any";
			else
				Out.m_IfMatch = "all";
			const int Count = json_array_length(pConditions);
			for(int i = 0; i < Count; ++i)
			{
				const json_value *pCond = json_array_get(pConditions, i);
				if(!pCond || pCond->type != json_object)
					continue;
				const json_value *pLeft = json_object_get(pCond, "left");
				const json_value *pOp = json_object_get(pCond, "op");
				const json_value *pRight = json_object_get(pCond, "right");
				if(!pLeft || pLeft->type != json_string || !pLeft->u.string.ptr || !pLeft->u.string.ptr[0])
					continue;
				if(!pOp || pOp->type != json_string || !pOp->u.string.ptr)
					continue;
				CAutomation::SIfCondition Cond;
				Cond.m_Left = pLeft->u.string.ptr;
				Cond.m_Op = pOp->u.string.ptr;
				if(pRight && pRight->type == json_string && pRight->u.string.ptr)
					Cond.m_Right = pRight->u.string.ptr;
				Out.m_IfConditions.push_back(std::move(Cond));
			}
			return true;
		}
		const json_value *pLeft = json_object_get(pAction, "left");
		const json_value *pOp = json_object_get(pAction, "op");
		const json_value *pRight = json_object_get(pAction, "right");
		if(!pLeft || pLeft->type != json_string || !pLeft->u.string.ptr)
			return false;
		if(!pOp || pOp->type != json_string || !pOp->u.string.ptr)
			return false;
		Out.m_IfLeft = pLeft->u.string.ptr;
		Out.m_IfOp = pOp->u.string.ptr;
		if(pRight && pRight->type == json_string && pRight->u.string.ptr)
			Out.m_IfRight = pRight->u.string.ptr;
		return true;
	}
	if(str_comp(pTypeStr, "otherwise") == 0)
	{
		Out.m_Type = CAutomation::EActionType::OTHERWISE;
		return true;
	}
	if(str_comp(pTypeStr, "end_if") == 0)
	{
		Out.m_Type = CAutomation::EActionType::END_IF;
		return true;
	}
	if(str_comp(pTypeStr, "stop") == 0)
	{
		Out.m_Type = CAutomation::EActionType::STOP;
		return true;
	}
	if(str_comp(pTypeStr, "connect_server") == 0)
	{
		Out.m_Type = CAutomation::EActionType::CONNECT_SERVER;
		const json_value *pAddress = json_object_get(pAction, "address");
		if(!pAddress || pAddress->type != json_string || !pAddress->u.string.ptr)
			return false;
		Out.m_ServerAddress = pAddress->u.string.ptr;
		return true;
	}
	if(str_comp(pTypeStr, "leave_server") == 0)
	{
		Out.m_Type = CAutomation::EActionType::LEAVE_SERVER;
		return true;
	}
	if(str_comp(pTypeStr, "run_shortcut") == 0)
	{
		Out.m_Type = CAutomation::EActionType::RUN_SHORTCUT;
		const json_value *pShortcutId = json_object_get(pAction, "shortcutId");
		if(!pShortcutId || pShortcutId->type != json_string || !pShortcutId->u.string.ptr || !pShortcutId->u.string.ptr[0])
			return false;
		Out.m_RunShortcutId = pShortcutId->u.string.ptr;
		return true;
	}
	return false;
}

void CAutomation::OnInit()
{
	TryReloadRules();
	// Adopt whatever run request is already on disk without executing it, so a
	// leftover file from a previous session cannot fire on startup.
	char *pData = nullptr;
	unsigned Size = 0;
	if(ReadFileToBuffer(Storage(), TEST_RUN_REQUEST_FILE, &pData, &Size))
	{
		m_TestRequestRaw.assign(pData, Size);
		free(pData);
	}
}

bool CAutomation::IsActive() const
{
	return Client()->State() == IClient::STATE_ONLINE;
}

void CAutomation::OnUpdate()
{
	const int64_t Now = time_get();
	if(Now - m_LastFileCheck >= time_freq())
	{
		m_LastFileCheck = Now;
		TryReloadRules();
	}
	if(Now - m_LastTestRequestCheck >= time_freq() * 120 / 1000)
	{
		m_LastTestRequestCheck = Now;
		PollTestRunRequest();
	}
	if(m_RunnerActive)
		StepRunner();
	// While waiting the remaining time keeps changing, so refresh at ~10 Hz.
	if(m_RunnerActive && m_Runner.m_TestRun && m_Runner.m_WaitUntil != 0 &&
		Now - m_LastTestStateWrite >= time_freq() / 10)
		WriteTestRunState("running");
}

// Quote a console argument so spaces stay part of the value and a stray ";"
// cannot start another command.
static void EscapeParamLocal(char *pDst, const char *pSrc, int Size)
{
	if(Size <= 0)
		return;
	int Out = 0;
	if(Out < Size - 1)
		pDst[Out++] = '"';
	for(const char *p = pSrc ? pSrc : ""; *p && Out < Size - 2; ++p)
	{
		if(*p == '"' || *p == '\\')
		{
			if(Out >= Size - 3)
				break;
			pDst[Out++] = '\\';
		}
		pDst[Out++] = *p;
	}
	if(Out < Size - 1)
		pDst[Out++] = '"';
	pDst[Out] = '\0';
}

void CAutomation::TryReloadRules()
{
	time_t Created = 0;
	time_t Modified = 0;
	if(!Storage()->RetrieveTimes(SHORTCUTS_FILE, IStorage::TYPE_SAVE, &Created, &Modified))
	{
		if(m_FileModifiedTime != 0)
		{
			m_FileModifiedTime = 0;
			m_vShortcuts.clear();
		}
		return;
	}
	if(Modified == m_FileModifiedTime)
		return;
	m_FileModifiedTime = Modified;

	char *pData = nullptr;
	unsigned Size = 0;
	if(!ReadFileToBuffer(Storage(), SHORTCUTS_FILE, &pData, &Size))
	{
		m_vShortcuts.clear();
		return;
	}
	const bool Ok = ParseRulesFile(pData, Size);
	free(pData);
	if(!Ok)
		m_vShortcuts.clear();
}

bool CAutomation::ParseRulesFile(const char *pJson, size_t Length)
{
	json_settings Settings{};
	char aError[256];
	json_value *pRoot = json_parse_ex(&Settings, pJson, Length, aError);
	if(!pRoot)
		return false;

	std::vector<SShortcut> vShortcuts;
	if(pRoot->type == json_object)
	{
		const json_value *pShortcuts = json_object_get(pRoot, "shortcuts");
		if(pShortcuts && pShortcuts->type == json_array)
		{
			const int Count = json_array_length(pShortcuts);
			for(int i = 0; i < Count; ++i)
			{
				const json_value *pEntry = json_array_get(pShortcuts, i);
				if(!pEntry || pEntry->type != json_object)
					continue;
				SShortcut Shortcut;
				const json_value *pId = json_object_get(pEntry, "id");
				if(!pId || pId->type != json_string || !pId->u.string.ptr)
					continue;
				Shortcut.m_Id = pId->u.string.ptr;
				const json_value *pName = json_object_get(pEntry, "name");
				if(pName && pName->type == json_string && pName->u.string.ptr)
					Shortcut.m_Name = pName->u.string.ptr;
				const json_value *pEnabled = json_object_get(pEntry, "enabled");
				Shortcut.m_Enabled = !pEnabled || json_boolean_get(pEnabled);
				const json_value *pKind = json_object_get(pEntry, "kind");
				const char *pKindStr = pKind && pKind->type == json_string ? pKind->u.string.ptr : nullptr;
				Shortcut.m_Manual = pKindStr && str_comp(pKindStr, "manual") == 0;
				const json_value *pTrigger = json_object_get(pEntry, "trigger");
				if(Shortcut.m_Manual)
				{
					if(pTrigger && pTrigger->type == json_object)
						continue;
				}
				else if(!ParseTrigger(pTrigger, Shortcut.m_Trigger))
				{
					continue;
				}
				const json_value *pActions = json_object_get(pEntry, "actions");
				if(!pActions || pActions->type != json_array)
					continue;
				const int ActionCount = json_array_length(pActions);
				for(int a = 0; a < ActionCount; ++a)
				{
					SAction Action;
					if(ParseAction(json_array_get(pActions, a), Action))
						Shortcut.m_vActions.push_back(std::move(Action));
				}
				if(!Shortcut.m_vActions.empty())
					vShortcuts.push_back(std::move(Shortcut));
			}
		}
	}
	json_value_free(pRoot);
	m_vShortcuts = std::move(vShortcuts);
	return true;
}

bool CAutomation::MatchText(ETextMatch Match, const char *pNeedle, const char *pHaystack) const
{
	if(!pNeedle || !pHaystack)
		return false;
	switch(Match)
	{
	case ETextMatch::EQUALS:
		return str_comp_nocase(pHaystack, pNeedle) == 0;
	case ETextMatch::STARTS_WITH:
		return str_startswith_nocase(pHaystack, pNeedle);
	default:
		return str_find_nocase(pHaystack, pNeedle) != nullptr;
	}
}

bool CAutomation::MatchesChatFilter(const SChatFilter &Filter, const SChatEvent &Event, bool IsMe) const
{
	if(Filter.m_Kind == SChatFilter::EKind::SENDER)
	{
		if(!Filter.m_SenderNames.empty())
		{
			for(const std::string &Name : Filter.m_SenderNames)
			{
				if(str_comp_nocase(Event.m_Name.c_str(), Name.c_str()) == 0)
					return true;
			}
			return false;
		}
		switch(Filter.m_Sender)
		{
		case ESenderFilter::ME:
			return IsMe;
		case ESenderFilter::SPECIFIC:
			if(Filter.m_SenderName.empty())
				return false;
			return str_comp_nocase(Event.m_Name.c_str(), Filter.m_SenderName.c_str()) == 0;
		default:
			return true;
		}
	}

	if(Filter.m_Kind == SChatFilter::EKind::CHAT_CHANNEL)
	{
		if(Filter.m_Text.empty())
			return false;
		const char *pEventChannel = Event.m_Channel == EChatChannel::TEAM ? "team" :
									Event.m_Channel == EChatChannel::UCLIENT ? "uclient" : "all";
		return str_comp_nocase(pEventChannel, Filter.m_Text.c_str()) == 0;
	}

	if(Filter.m_Kind == SChatFilter::EKind::UCLIENT_ROOM)
	{
		if(Event.m_Channel != EChatChannel::UCLIENT)
			return false;
		if(Filter.m_Text.empty())
			return true;
		if(!Event.m_UClientRoomId.empty() && str_comp_nocase(Event.m_UClientRoomId.c_str(), Filter.m_Text.c_str()) == 0)
			return true;
		if(!Event.m_UClientRoomName.empty() && str_comp_nocase(Event.m_UClientRoomName.c_str(), Filter.m_Text.c_str()) == 0)
			return true;
		return false;
	}

	if(Filter.m_Text.empty())
		return false;
	const std::string Needle = ExpandFilterText(Filter.m_Text, Event);
	return MatchText(Filter.m_Match, Needle.c_str(), Event.m_Text.c_str());
}

bool CAutomation::ChannelsMatch(EChatChannel TriggerChannel, EChatChannel EventChannel) const
{
	if(TriggerChannel == EventChannel)
		return true;
	// The editor has no control for the trigger channel, it always saves "all".
	// Narrowing down to team or UClient is done through chat filters instead.
	return TriggerChannel == EChatChannel::ALL;
}

static bool TargetHasExplicitPort(const char *pTarget)
{
	if(!pTarget || !pTarget[0])
		return false;
	const char *pColon = str_rchr(pTarget, ':');
	if(!pColon || pColon == pTarget)
		return false;
	for(const char *p = pColon + 1; *p; ++p)
	{
		if(*p < '0' || *p > '9')
			return false;
	}
	return pColon[1] != '\0';
}

bool CAutomation::TargetMatchesServer(const std::string &Target, const NETADDR &ServerAddr) const
{
	if(Target.empty())
		return false;

	NETADDR TargetAddr;
	if(net_addr_from_url(&TargetAddr, Target.c_str(), nullptr, 0) != 0 && net_addr_from_str(&TargetAddr, Target.c_str()) != 0)
	{
		char aServer[NETADDR_MAXSTRSIZE];
		net_addr_str(&ServerAddr, aServer, sizeof(aServer), true);
		return str_comp_nocase(aServer, Target.c_str()) == 0;
	}

	if(TargetHasExplicitPort(Target.c_str()))
		return net_addr_comp(&TargetAddr, &ServerAddr) == 0;
	return net_addr_comp_noport(&TargetAddr, &ServerAddr) == 0;
}

bool CAutomation::MatchesServerConnectTrigger(const SShortcut &Shortcut, const NETADDR &ServerAddr) const
{
	const STrigger &Trigger = Shortcut.m_Trigger;
	if(Trigger.m_Type != ETriggerType::SERVER_CONNECT)
		return false;
	if(Trigger.m_ServerTargets.empty())
		return true;
	for(const std::string &Target : Trigger.m_ServerTargets)
	{
		if(TargetMatchesServer(Target, ServerAddr))
			return true;
	}
	return false;
}

void CAutomation::EvaluateServerConnectTriggers(const NETADDR &ServerAddr)
{
	if(!IsActive() || m_vShortcuts.empty())
		return;
	if(m_RunnerActive)
		return;

	for(size_t i = 0; i < m_vShortcuts.size(); ++i)
	{
		const SShortcut &Shortcut = m_vShortcuts[i];
		if(Shortcut.m_Manual || !Shortcut.m_Enabled)
			continue;
		if(MatchesServerConnectTrigger(Shortcut, ServerAddr))
		{
			StartRunner(i);
			break;
		}
	}
}

void CAutomation::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_OFFLINE)
	{
		m_ServerConnectTriggeredForSession = false;
		return;
	}
	if(NewState != IClient::STATE_ONLINE || OldState != IClient::STATE_LOADING)
		return;
	if(m_ServerConnectTriggeredForSession)
		return;
	const NETADDR &ServerAddr = Client()->ServerAddress();
	m_ServerConnectTriggeredForSession = true;
	EvaluateServerConnectTriggers(ServerAddr);
}

bool CAutomation::MatchesChatTrigger(const SShortcut &Shortcut, const SChatEvent &Event) const
{
	const STrigger &Trigger = Shortcut.m_Trigger;
	if(Trigger.m_Type != ETriggerType::CHAT_RECEIVED)
		return false;
	bool IsMe = false;
	for(int LocalId : GameClient()->m_aLocalIds)
	{
		if(Event.m_ClientId >= 0 && Event.m_ClientId == LocalId)
		{
			IsMe = true;
			break;
		}
	}
	if(IsMe)
		return false;
	if(!ChannelsMatch(Trigger.m_Channel, Event.m_Channel))
		return false;
	if(Trigger.m_Filters.empty())
		return true;

	for(const SChatFilter &Filter : Trigger.m_Filters)
	{
		if(!MatchesChatFilter(Filter, Event, IsMe))
			return false;
	}
	return true;
}

void CAutomation::OnChatReceived(const SChatEvent &Event)
{
	if(!IsActive() || m_vShortcuts.empty())
		return;
	if(m_RunnerActive)
		return;

	for(size_t i = 0; i < m_vShortcuts.size(); ++i)
	{
		const SShortcut &Shortcut = m_vShortcuts[i];
		if(Shortcut.m_Manual || !Shortcut.m_Enabled)
			continue;
		if(MatchesChatTrigger(Shortcut, Event))
		{
			StartRunner(i, &Event);
			break;
		}
	}
}

void CAutomation::StartRunner(size_t ShortcutIndex, const SChatEvent *pChatEvent)
{
	if(ShortcutIndex >= m_vShortcuts.size())
		return;
	m_Runner = SRunner{};
	m_Runner.m_ShortcutIndex = ShortcutIndex;
	m_Runner.m_ShortcutId = m_vShortcuts[ShortcutIndex].m_Id;
	m_Runner.m_ActionIndex = 0;
	if(pChatEvent)
	{
		m_Runner.m_HadChatEvent = true;
		m_Runner.m_ChatEvent = *pChatEvent;
		const char *pChannel = pChatEvent->m_Channel == EChatChannel::TEAM ? "team" :
							   pChatEvent->m_Channel == EChatChannel::UCLIENT ? "uclient" : "all";
		m_Runner.m_Variables["messageSender"] = pChatEvent->m_Name;
		m_Runner.m_Variables["messageText"] = pChatEvent->m_Text;
		m_Runner.m_Variables["messageChannel"] = pChannel;
		m_Runner.m_Variables["messageUClientRoom"] = pChatEvent->m_UClientRoomName;
		m_Runner.m_Variables["messageUClientRoomId"] = pChatEvent->m_UClientRoomId;
		m_Runner.m_Variables["senderName"] = pChatEvent->m_Name;
		m_Runner.m_Variables["sender"] = pChatEvent->m_Name;
		m_Runner.m_Variables["message"] = pChatEvent->m_Text;
	}
	if(g_Config.m_PlayerName[0])
		m_Runner.m_Variables["name"] = g_Config.m_PlayerName;
	char aNearestPlayer[MAX_NAME_LENGTH];
	if(GameClient()->m_Binds.FindNearestPlayerName(aNearestPlayer, sizeof(aNearestPlayer)))
		m_Runner.m_Variables["nearestPlayer"] = aNearestPlayer;
	m_RunnerActive = true;
	StepRunner();
}

size_t CAutomation::FindShortcutIndexById(const std::string &Id) const
{
	if(Id.empty())
		return SIZE_MAX;
	for(size_t i = 0; i < m_vShortcuts.size(); ++i)
	{
		if(m_vShortcuts[i].m_Id == Id)
			return i;
	}
	return SIZE_MAX;
}

bool CAutomation::WouldRecurseRunShortcut(const std::string &TargetId) const
{
	if(TargetId.empty())
		return true;
	if(m_Runner.m_ShortcutId == TargetId)
		return true;
	for(const SRunnerCallFrame &Frame : m_Runner.m_vCallStack)
	{
		if(Frame.m_ShortcutId == TargetId)
			return true;
	}
	return false;
}

void CAutomation::PushRunnerCallFrame(size_t ReturnActionIndex)
{
	SRunnerCallFrame Frame;
	Frame.m_ShortcutIndex = m_Runner.m_ShortcutIndex;
	Frame.m_ShortcutId = m_Runner.m_ShortcutId;
	Frame.m_ActionIndex = ReturnActionIndex;
	Frame.m_WaitUntil = m_Runner.m_WaitUntil;
	Frame.m_WeaponUseStep = m_Runner.m_WeaponUseStep;
	Frame.m_WeaponUseTarget = m_Runner.m_WeaponUseTarget;
	Frame.m_WeaponUseReadyTime = m_Runner.m_WeaponUseReadyTime;
	Frame.m_vIfFrames = std::move(m_Runner.m_vIfFrames);
	Frame.m_vRepeatFrames = std::move(m_Runner.m_vRepeatFrames);
	m_Runner.m_vIfFrames.clear();
	m_Runner.m_vRepeatFrames.clear();
	m_Runner.m_vCallStack.push_back(std::move(Frame));
}

void CAutomation::PopRunnerCallFrame()
{
	if(m_Runner.m_vCallStack.empty())
		return;
	SRunnerCallFrame Frame = std::move(m_Runner.m_vCallStack.back());
	m_Runner.m_vCallStack.pop_back();
	m_Runner.m_ShortcutIndex = Frame.m_ShortcutIndex;
	m_Runner.m_ShortcutId = Frame.m_ShortcutId;
	m_Runner.m_ActionIndex = Frame.m_ActionIndex;
	m_Runner.m_WaitUntil = Frame.m_WaitUntil;
	m_Runner.m_WeaponUseStep = Frame.m_WeaponUseStep;
	m_Runner.m_WeaponUseTarget = Frame.m_WeaponUseTarget;
	m_Runner.m_WeaponUseReadyTime = Frame.m_WeaponUseReadyTime;
	m_Runner.m_vIfFrames = std::move(Frame.m_vIfFrames);
	m_Runner.m_vRepeatFrames = std::move(Frame.m_vRepeatFrames);
}

const CAutomation::SShortcut *CAutomation::RunnerShortcut() const
{
	if(m_Runner.m_TestRun)
		return m_TestShortcut.has_value() ? &m_TestShortcut.value() : nullptr;
	if(m_Runner.m_ShortcutIndex < m_vShortcuts.size() &&
		(m_Runner.m_ShortcutId.empty() || m_vShortcuts[m_Runner.m_ShortcutIndex].m_Id == m_Runner.m_ShortcutId))
	{
		return &m_vShortcuts[m_Runner.m_ShortcutIndex];
	}
	// The file was reloaded while running, so locate the shortcut by id again.
	if(!m_Runner.m_ShortcutId.empty())
	{
		for(const SShortcut &Shortcut : m_vShortcuts)
		{
			if(Shortcut.m_Id == m_Runner.m_ShortcutId)
				return &Shortcut;
		}
	}
	return nullptr;
}

size_t CAutomation::FindMatchingEndIf(size_t IfIndex) const
{
	const SShortcut *pShortcut = RunnerShortcut();
	if(!pShortcut)
		return SIZE_MAX;
	const SShortcut &Shortcut = *pShortcut;
	if(IfIndex >= Shortcut.m_vActions.size() || Shortcut.m_vActions[IfIndex].m_Type != EActionType::IF)
		return SIZE_MAX;
	int Depth = 1;
	for(size_t i = IfIndex + 1; i < Shortcut.m_vActions.size(); ++i)
	{
		switch(Shortcut.m_vActions[i].m_Type)
		{
		case EActionType::IF:
			Depth++;
			break;
		case EActionType::END_IF:
			Depth--;
			if(Depth == 0)
				return i;
			break;
		default:
			break;
		}
	}
	return SIZE_MAX;
}

size_t CAutomation::FindMatchingEndRepeat(size_t RepeatIndex) const
{
	const SShortcut *pShortcut = RunnerShortcut();
	if(!pShortcut)
		return SIZE_MAX;
	const SShortcut &Shortcut = *pShortcut;
	if(RepeatIndex >= Shortcut.m_vActions.size() || Shortcut.m_vActions[RepeatIndex].m_Type != EActionType::REPEAT)
		return SIZE_MAX;
	int Depth = 1;
	for(size_t i = RepeatIndex + 1; i < Shortcut.m_vActions.size(); ++i)
	{
		switch(Shortcut.m_vActions[i].m_Type)
		{
		case EActionType::REPEAT:
			Depth++;
			break;
		case EActionType::END_REPEAT:
			Depth--;
			if(Depth == 0)
				return i;
			break;
		default:
			break;
		}
	}
	return SIZE_MAX;
}

size_t CAutomation::FindOtherwise(size_t IfIndex, size_t EndIfIndex) const
{
	const SShortcut *pShortcut = RunnerShortcut();
	if(!pShortcut || EndIfIndex == SIZE_MAX)
		return SIZE_MAX;
	const SShortcut &Shortcut = *pShortcut;
	// Only the "otherwise" of this very if counts, so nested ifs are skipped.
	int Depth = 0;
	for(size_t i = IfIndex + 1; i < EndIfIndex && i < Shortcut.m_vActions.size(); ++i)
	{
		const EActionType Type = Shortcut.m_vActions[i].m_Type;
		if(Type == EActionType::IF)
			Depth++;
		else if(Type == EActionType::END_IF)
			Depth--;
		else if(Type == EActionType::OTHERWISE && Depth == 0)
			return i;
	}
	return SIZE_MAX;
}

bool CAutomation::ValuesMatch(const std::string &Left, const std::string &Op, const std::string &Right) const
{
	if(Op == "has_any")
		return !Left.empty();
	if(Op == "has_none")
		return Left.empty();
	if(Op == "contains")
		return str_find_nocase(Left.c_str(), Right.c_str()) != nullptr;
	if(Op == "not_contains")
		return str_find_nocase(Left.c_str(), Right.c_str()) == nullptr;
	if(Op == "starts_with")
		return str_startswith_nocase(Left.c_str(), Right.c_str()) != nullptr;
	if(Op == "ends_with")
		return str_endswith_nocase(Left.c_str(), Right.c_str()) != nullptr;
	if(Op == "is" || str_comp_nocase(Op.c_str(), "equals") == 0)
		return Left == Right;
	if(Op == "is_not" || str_comp_nocase(Op.c_str(), "is not") == 0)
		return Left != Right;
	return false;
}

bool CAutomation::EvaluateOneIfCondition(const std::string &IfLeft, const std::string &IfOp, const std::string &IfRight) const
{
	if(IfLeft.empty())
		return false;
	const std::string Left = ResolveVariable(IfLeft.c_str());
	return ValuesMatch(Left, IfOp, IfRight);
}

bool CAutomation::EvaluateIfCondition(const SAction &Action) const
{
	if(!Action.m_IfConditions.empty())
	{
		const bool Any = str_comp_nocase(Action.m_IfMatch.c_str(), "any") == 0;
		bool AnyChecked = false;
		for(const SIfCondition &Cond : Action.m_IfConditions)
		{
			if(Cond.m_Left.empty())
				continue;
			const bool Match = EvaluateOneIfCondition(Cond.m_Left, Cond.m_Op, Cond.m_Right);
			AnyChecked = true;
			if(Any && Match)
				return true;
			if(!Any && !Match)
				return false;
		}
		return Any ? false : AnyChecked;
	}
	if(Action.m_IfLeft.empty())
		return false;
	return EvaluateOneIfCondition(Action.m_IfLeft, Action.m_IfOp, Action.m_IfRight);
}

// Title of the window the user is currently looking at. Windows only; other
// platforms fall back to whether our own window holds input focus.
std::string CAutomation::ActiveWindowValue() const
{
#if defined(CONF_FAMILY_WINDOWS)
	const HWND hForeground = GetForegroundWindow();
	if(hForeground != nullptr)
	{
		wchar_t aTitle[512];
		const int Length = GetWindowTextW(hForeground, aTitle, (int)std::size(aTitle));
		if(Length > 0)
		{
			const std::optional<std::string> Utf8 = windows_wide_to_utf8(aTitle);
			if(Utf8.has_value())
				return Utf8.value();
		}
	}
	return std::string();
#else
	IEngineGraphics *pEngineGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	const bool Active = pEngineGraphics && pEngineGraphics->WindowActive();
	return Active ? "Yes" : "No";
#endif
}

void CAutomation::ExecuteGetAction(const SAction &Action)
{
	if(Action.m_GetProperty == "window_active")
		SetRunnerVariable("window_active", ActiveWindowValue());
}

std::string CAutomation::ClipboardTextValue() const
{
	if(GameClient()->Input())
		return GameClient()->Input()->GetClipboardText();
	return std::string();
}

void CAutomation::ExecuteGetClipboardAction(const SAction &Action)
{
	const std::string Key = Action.m_OutputVariable.empty() ? "clipboard" : Action.m_OutputVariable;
	SetRunnerVariable(Key.c_str(), ClipboardTextValue());
}

void CAutomation::ExecuteTextAction(const SAction &Action)
{
	std::string Result;
	for(const STextPart &Part : Action.m_TextParts)
	{
		if(Part.m_Mode == EValueMode::VARIABLE)
			Result += ResolveVariable(Part.m_Variable.c_str());
		else
			Result += Part.m_Text;
	}
	const std::string Key = Action.m_OutputVariable.empty() ? "text" : Action.m_OutputVariable;
	SetRunnerVariable(Key.c_str(), Result);
}

std::string CAutomation::ResolveMessageValue(const SAction &Action) const
{
	if(Action.m_MessageMode == EValueMode::VARIABLE)
		return ResolveVariable(Action.m_Variable.c_str());
	if(Action.m_Message.find('%') != std::string::npos)
		return ExpandTemplate(Action.m_Message);
	return Action.m_Message;
}

static bool ParseChannelLabel(const std::string &Label, CAutomation::EChatChannel &OutChannel)
{
	if(str_comp_nocase(Label.c_str(), "team") == 0)
	{
		OutChannel = CAutomation::EChatChannel::TEAM;
		return true;
	}
	if(str_comp_nocase(Label.c_str(), "uclient") == 0)
	{
		OutChannel = CAutomation::EChatChannel::UCLIENT;
		return true;
	}
	if(str_comp_nocase(Label.c_str(), "all") == 0)
	{
		OutChannel = CAutomation::EChatChannel::ALL;
		return true;
	}
	return false;
}

bool CAutomation::ResolveSendChannel(const SAction &Action, EChatChannel &OutChannel) const
{
	if(Action.m_ChannelMode == EValueMode::VARIABLE)
	{
		const std::string Label = ResolveVariable(Action.m_ChannelVariable.c_str());
		return ParseChannelLabel(Label, OutChannel);
	}
	OutChannel = Action.m_Channel;
	return true;
}

std::string CAutomation::ResolveUClientRoomId(const SAction &Action) const
{
	if(Action.m_UClientRoomMode == EValueMode::VARIABLE)
		return ResolveVariable(Action.m_UClientRoomVariable.c_str());
	return Action.m_UClientRoomId;
}

std::string CAutomation::ResolveVariable(const char *pKey) const
{
	if(!pKey || !pKey[0])
		return "";
	const auto It = m_Runner.m_Variables.find(pKey);
	if(It != m_Runner.m_Variables.end())
		return It->second;
	if(m_Runner.m_HadChatEvent)
	{
		if(str_comp(pKey, "messageSender") == 0 || str_comp(pKey, "senderName") == 0 || str_comp(pKey, "sender") == 0)
			return m_Runner.m_ChatEvent.m_Name;
		if(str_comp(pKey, "messageText") == 0 || str_comp(pKey, "message") == 0)
			return m_Runner.m_ChatEvent.m_Text;
		if(str_comp(pKey, "messageChannel") == 0)
		{
			if(m_Runner.m_ChatEvent.m_Channel == EChatChannel::TEAM)
				return "team";
			if(m_Runner.m_ChatEvent.m_Channel == EChatChannel::UCLIENT)
				return "uclient";
			return "all";
		}
		if(str_comp(pKey, "messageUClientRoom") == 0)
			return m_Runner.m_ChatEvent.m_UClientRoomName;
		if(str_comp(pKey, "messageUClientRoomId") == 0)
			return m_Runner.m_ChatEvent.m_UClientRoomId;
	}
	if(str_comp(pKey, "name") == 0 && g_Config.m_PlayerName[0])
		return g_Config.m_PlayerName;
	char aNearestPlayer[MAX_NAME_LENGTH];
	if(str_comp(pKey, "nearestPlayer") == 0 &&
		GameClient()->m_Binds.FindNearestPlayerName(aNearestPlayer, sizeof(aNearestPlayer)))
	{
		return aNearestPlayer;
	}
	return "";
}

std::string CAutomation::ResolveFilterKey(const char *pKey, const SChatEvent &Event) const
{
	if(!pKey || !pKey[0])
		return "";
	if(str_comp(pKey, "messageSender") == 0 || str_comp(pKey, "senderName") == 0 || str_comp(pKey, "sender") == 0)
		return Event.m_Name;
	if(str_comp(pKey, "messageText") == 0 || str_comp(pKey, "message") == 0)
		return Event.m_Text;
	if(str_comp(pKey, "messageChannel") == 0)
	{
		if(Event.m_Channel == EChatChannel::TEAM)
			return "team";
		if(Event.m_Channel == EChatChannel::UCLIENT)
			return "uclient";
		return "all";
	}
	if(str_comp(pKey, "messageUClientRoom") == 0)
		return Event.m_UClientRoomName;
	if(str_comp(pKey, "messageUClientRoomId") == 0)
		return Event.m_UClientRoomId;
	if(str_comp(pKey, "name") == 0 && g_Config.m_PlayerName[0])
		return g_Config.m_PlayerName;
	char aNearestPlayer[MAX_NAME_LENGTH];
	if(str_comp(pKey, "nearestPlayer") == 0 &&
		GameClient()->m_Binds.FindNearestPlayerName(aNearestPlayer, sizeof(aNearestPlayer)))
	{
		return aNearestPlayer;
	}
	return "";
}

std::string CAutomation::ExpandFilterText(const std::string &Template, const SChatEvent &Event) const
{
	if(Template.find('%') == std::string::npos)
		return Template;
	std::string Result;
	Result.reserve(Template.size());
	for(size_t i = 0; i < Template.size(); ++i)
	{
		if(Template[i] != '%')
		{
			Result.push_back(Template[i]);
			continue;
		}
		size_t End = Template.find('%', i + 1);
		if(End == std::string::npos)
		{
			Result.push_back('%');
			continue;
		}
		const std::string Key = Template.substr(i + 1, End - i - 1);
		Result += ResolveFilterKey(Key.c_str(), Event);
		i = End;
	}
	return Result;
}

std::string CAutomation::ExpandTemplate(const std::string &Template) const
{
	std::string Result;
	Result.reserve(Template.size());
	for(size_t i = 0; i < Template.size(); ++i)
	{
		if(Template[i] != '%')
		{
			Result.push_back(Template[i]);
			continue;
		}
		size_t End = Template.find('%', i + 1);
		if(End == std::string::npos)
		{
			Result.push_back('%');
			continue;
		}
		const std::string Key = Template.substr(i + 1, End - i - 1);
		Result += ResolveVariable(Key.c_str());
		i = End;
	}
	return Result;
}

void CAutomation::StopRunner()
{
	const bool WasTestRun = m_Runner.m_TestRun;
	m_RunnerActive = false;
	m_Runner = SRunner{};
	if(WasTestRun)
	{
		WriteTestRunState("done");
		ClearTestRun();
	}
}

void CAutomation::ClearTestRun()
{
	m_TestShortcut.reset();
	m_TestRunId.clear();
	m_vTestResults.clear();
	m_TestReportedStep = SIZE_MAX;
}

void CAutomation::SetRunnerVariable(const char *pName, const std::string &Value)
{
	m_Runner.m_Variables[pName] = Value;
	if(!m_Runner.m_TestRun)
		return;
	const size_t Index = m_Runner.m_ActionIndex;
	for(auto &Result : m_vTestResults)
	{
		if(Result.first == Index)
		{
			Result.second = Value;
			return;
		}
	}
	m_vTestResults.emplace_back(Index, Value);
}

void CAutomation::WriteTestRunState(const char *pStatus)
{
	if(m_TestRunId.empty())
		return;
	m_LastTestStateWrite = time_get();

	char aEscaped[1024];
	std::string Json = "{\"version\":1,\"runId\":\"";
	Json += EscapeJson(aEscaped, sizeof(aEscaped), m_TestRunId.c_str());
	Json += "\",\"status\":\"";
	Json += pStatus;
	Json += "\"";

	const size_t Total = m_TestShortcut.has_value() ? m_TestShortcut.value().m_vActions.size() : 0;
	char aNum[64];
	str_format(aNum, sizeof(aNum), ",\"total\":%d", (int)Total);
	Json += aNum;
	if(m_RunnerActive)
	{
		str_format(aNum, sizeof(aNum), ",\"step\":%d", (int)m_Runner.m_ActionIndex);
		Json += aNum;
	}
	if(m_RunnerActive && m_Runner.m_WaitUntil != 0)
	{
		double Remaining = (double)(m_Runner.m_WaitUntil - time_get()) / (double)time_freq();
		if(Remaining < 0.0)
			Remaining = 0.0;
		str_format(aNum, sizeof(aNum), ",\"waitRemaining\":%.2f", Remaining);
		Json += aNum;
	}
	Json += ",\"results\":[";
	for(size_t i = 0; i < m_vTestResults.size(); ++i)
	{
		if(i > 0)
			Json += ",";
		str_format(aNum, sizeof(aNum), "{\"index\":%d,\"value\":\"", (int)m_vTestResults[i].first);
		Json += aNum;
		Json += EscapeJson(aEscaped, sizeof(aEscaped), m_vTestResults[i].second.c_str());
		Json += "\"}";
	}
	Json += "]}";

	IOHANDLE File = Storage()->OpenFile(TEST_RUN_STATE_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;
	io_write(File, Json.c_str(), Json.length());
	io_close(File);
}

void CAutomation::PollTestRunRequest()
{
	// The file timestamp only has second resolution, so compare the raw contents
	// to also catch two presses of play within the same second.
	char *pData = nullptr;
	unsigned Size = 0;
	if(!ReadFileToBuffer(Storage(), TEST_RUN_REQUEST_FILE, &pData, &Size))
	{
		m_TestRequestRaw.clear();
		return;
	}
	const std::string Raw(pData, Size);
	if(Raw == m_TestRequestRaw)
	{
		free(pData);
		return;
	}
	m_TestRequestRaw = Raw;
	ParseTestRunRequest(pData, Size);
	free(pData);
}

bool CAutomation::ParseTestRunRequest(const char *pJson, size_t Length)
{
	json_settings Settings{};
	char aError[256];
	json_value *pRoot = json_parse_ex(&Settings, pJson, Length, aError);
	if(!pRoot)
		return false;
	if(pRoot->type != json_object)
	{
		json_value_free(pRoot);
		return false;
	}

	const char *pRunId = json_string_get(json_object_get(pRoot, "runId"));
	const char *pAction = json_string_get(json_object_get(pRoot, "action"));
	if(!pRunId || !pRunId[0])
	{
		json_value_free(pRoot);
		return false;
	}
	if(pAction && str_comp(pAction, "stop") == 0)
	{
		if(m_RunnerActive && m_Runner.m_TestRun)
			StopRunner();
		json_value_free(pRoot);
		return true;
	}

	SShortcut Shortcut;
	Shortcut.m_Id = pRunId;
	Shortcut.m_Enabled = true;
	const json_value *pActions = json_object_get(pRoot, "actions");
	if(!pActions || pActions->type != json_array)
	{
		json_value_free(pRoot);
		return false;
	}
	const int ActionCount = json_array_length(pActions);
	for(int i = 0; i < ActionCount; ++i)
	{
		SAction Action;
		if(ParseAction(json_array_get(pActions, i), Action))
			Shortcut.m_vActions.push_back(Action);
	}
	json_value_free(pRoot);
	if(Shortcut.m_vActions.empty())
		return false;

	if(m_RunnerActive)
		StopRunner();
	m_TestShortcut = Shortcut;
	m_TestRunId = pRunId;
	m_vTestResults.clear();
	m_TestReportedStep = SIZE_MAX;
	m_Runner = SRunner{};
	m_Runner.m_TestRun = true;
	m_Runner.m_ActionIndex = 0;
	if(g_Config.m_PlayerName[0])
		m_Runner.m_Variables["name"] = g_Config.m_PlayerName;
	char aNearestPlayer[MAX_NAME_LENGTH];
	if(GameClient()->m_Binds.FindNearestPlayerName(aNearestPlayer, sizeof(aNearestPlayer)))
		m_Runner.m_Variables["nearestPlayer"] = aNearestPlayer;
	m_RunnerActive = true;
	WriteTestRunState("running");
	StepRunner();
	return true;
}

void CAutomation::ExecuteAction(const SAction &Action)
{
	char aCmd[512];
	switch(Action.m_Type)
	{
	case EActionType::SEND_CHAT:
	{
		const std::string Message = ResolveMessageValue(Action);
		EChatChannel Channel = EChatChannel::ALL;
		if(!ResolveSendChannel(Action, Channel))
			break;
		if(Channel == EChatChannel::UCLIENT)
		{
			const std::string RoomId = ResolveUClientRoomId(Action);
			GameClient()->m_Chat.SayUClient(Message.c_str(), RoomId.empty() ? nullptr : RoomId.c_str());
		}
		else
			GameClient()->m_Chat.SendChat(Channel == EChatChannel::TEAM ? 1 : 0, Message.c_str());
		break;
	}
	case EActionType::CONNECT_SERVER:
		if(!Action.m_ServerAddress.empty())
			Client()->Connect(Action.m_ServerAddress.c_str(), "");
		break;
	case EActionType::LEAVE_SERVER:
		Client()->Disconnect();
		break;
	case EActionType::SET_SKIN:
	{
		char aEscapedSkin[256];
		EscapeParamLocal(aEscapedSkin, Action.m_Skin.c_str(), sizeof(aEscapedSkin));
		str_format(aCmd, sizeof(aCmd), "%s_skin %s", TargetPrefix(Action.m_Target), aEscapedSkin);
		Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
		break;
	}
	case EActionType::SET_CUSTOM_COLOR:
		str_format(aCmd, sizeof(aCmd), "%s_use_custom_color %d", TargetPrefix(Action.m_Target), Action.m_CustomColorEnabled ? 1 : 0);
		Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
		break;
	case EActionType::SET_BODY_COLOR:
		str_format(aCmd, sizeof(aCmd), "%s_color_body %d", TargetPrefix(Action.m_Target), Action.m_Color);
		Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
		break;
	case EActionType::SET_FEET_COLOR:
		str_format(aCmd, sizeof(aCmd), "%s_color_feet %d", TargetPrefix(Action.m_Target), Action.m_Color);
		Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
		break;
	case EActionType::SET_NAME:
	{
		char aEscaped[256];
		EscapeParamLocal(aEscaped, Action.m_Name.c_str(), sizeof(aEscaped));
		str_format(aCmd, sizeof(aCmd), "%s_name %s", TargetPrefix(Action.m_Target), aEscaped);
		Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
		break;
	}
	default:
		break;
	}
}

void CAutomation::StepRunner()
{
	const SShortcut *pShortcut = RunnerShortcut();
	if(!m_RunnerActive || !pShortcut)
	{
		StopRunner();
		return;
	}

	const SShortcut &Shortcut = *pShortcut;
	if(m_Runner.m_ActionIndex >= Shortcut.m_vActions.size())
	{
		if(!m_Runner.m_vCallStack.empty())
		{
			PopRunnerCallFrame();
			StepRunner();
			return;
		}
		StopRunner();
		return;
	}

	const SAction &Action = Shortcut.m_vActions[m_Runner.m_ActionIndex];
	const int64_t Now = time_get();

	if(m_Runner.m_TestRun && m_TestReportedStep != m_Runner.m_ActionIndex)
	{
		m_TestReportedStep = m_Runner.m_ActionIndex;
		WriteTestRunState("running");
	}

	// A test run can be started while sitting in the menus, so actions that need
	// a live server connection are skipped instead of silently misbehaving.
	if(m_Runner.m_TestRun && !IsActive() &&
		(Action.m_Type == EActionType::SEND_CHAT || Action.m_Type == EActionType::SWITCH_WEAPON_USE))
	{
		++m_Runner.m_ActionIndex;
		StepRunner();
		return;
	}

	if(Action.m_Type == EActionType::GET)
	{
		ExecuteGetAction(Action);
		++m_Runner.m_ActionIndex;
		StepRunner();
		return;
	}

	if(Action.m_Type == EActionType::GET_CLIPBOARD)
	{
		ExecuteGetClipboardAction(Action);
		++m_Runner.m_ActionIndex;
		StepRunner();
		return;
	}

	if(Action.m_Type == EActionType::TEXT)
	{
		ExecuteTextAction(Action);
		++m_Runner.m_ActionIndex;
		StepRunner();
		return;
	}

	if(Action.m_Type == EActionType::REPEAT)
	{
		const size_t EndRepeat = FindMatchingEndRepeat(m_Runner.m_ActionIndex);
		if(EndRepeat == SIZE_MAX)
		{
			++m_Runner.m_ActionIndex;
			StepRunner();
			return;
		}
		SRepeatFrame Frame;
		Frame.m_EndRepeatIndex = EndRepeat;
		Frame.m_BodyStartIndex = m_Runner.m_ActionIndex + 1;
		Frame.m_Remaining = Action.m_RepeatCount;
		if(Frame.m_Remaining < 1)
			Frame.m_Remaining = 1;
		if(Frame.m_Remaining > 99)
			Frame.m_Remaining = 99;
		m_Runner.m_vRepeatFrames.push_back(Frame);
		++m_Runner.m_ActionIndex;
		StepRunner();
		return;
	}

	if(Action.m_Type == EActionType::END_REPEAT)
	{
		if(!m_Runner.m_vRepeatFrames.empty())
		{
			SRepeatFrame &Frame = m_Runner.m_vRepeatFrames.back();
			if(Frame.m_EndRepeatIndex == m_Runner.m_ActionIndex)
			{
				Frame.m_Remaining--;
				if(Frame.m_Remaining > 0)
				{
					m_Runner.m_ActionIndex = Frame.m_BodyStartIndex;
					StepRunner();
					return;
				}
				m_Runner.m_vRepeatFrames.pop_back();
			}
		}
		++m_Runner.m_ActionIndex;
		StepRunner();
		return;
	}

	if(Action.m_Type == EActionType::IF)
	{
		const size_t EndIf = FindMatchingEndIf(m_Runner.m_ActionIndex);
		const bool Condition = EvaluateIfCondition(Action);
		if(EndIf == SIZE_MAX)
		{
			++m_Runner.m_ActionIndex;
			StepRunner();
			return;
		}
		SIfFrame Frame;
		Frame.m_EndIfIndex = EndIf;
		Frame.m_TrueBranch = Condition;
		m_Runner.m_vIfFrames.push_back(Frame);
		if(Condition)
		{
			++m_Runner.m_ActionIndex;
		}
		else
		{
			const size_t Otherwise = FindOtherwise(m_Runner.m_ActionIndex, EndIf);
			m_Runner.m_ActionIndex = Otherwise != SIZE_MAX ? Otherwise + 1 : EndIf + 1;
		}
		StepRunner();
		return;
	}

	if(Action.m_Type == EActionType::OTHERWISE)
	{
		// Reaching "otherwise" from the then branch means the else part is skipped.
		if(!m_Runner.m_vIfFrames.empty() && m_Runner.m_vIfFrames.back().m_TrueBranch)
		{
			const size_t EndIf = m_Runner.m_vIfFrames.back().m_EndIfIndex;
			m_Runner.m_vIfFrames.pop_back();
			m_Runner.m_ActionIndex = EndIf != SIZE_MAX ? EndIf + 1 : m_Runner.m_ActionIndex + 1;
		}
		else
		{
			++m_Runner.m_ActionIndex;
		}
		StepRunner();
		return;
	}

	if(Action.m_Type == EActionType::END_IF)
	{
		if(!m_Runner.m_vIfFrames.empty())
			m_Runner.m_vIfFrames.pop_back();
		++m_Runner.m_ActionIndex;
		StepRunner();
		return;
	}

	if(Action.m_Type == EActionType::RUN_SHORTCUT)
	{
		const size_t TargetIndex = FindShortcutIndexById(Action.m_RunShortcutId);
		if(TargetIndex == SIZE_MAX || m_Runner.m_vCallStack.size() >= 16 ||
			WouldRecurseRunShortcut(Action.m_RunShortcutId))
		{
			++m_Runner.m_ActionIndex;
			StepRunner();
			return;
		}
		const SShortcut &Target = m_vShortcuts[TargetIndex];
		if(!Target.m_Manual || Target.m_vActions.empty())
		{
			++m_Runner.m_ActionIndex;
			StepRunner();
			return;
		}
		PushRunnerCallFrame(m_Runner.m_ActionIndex + 1);
		m_Runner.m_ShortcutIndex = TargetIndex;
		m_Runner.m_ShortcutId = Target.m_Id;
		m_Runner.m_ActionIndex = 0;
		m_Runner.m_WaitUntil = 0;
		m_Runner.m_WeaponUseStep = 0;
		m_Runner.m_WeaponUseTarget = 0;
		m_Runner.m_WeaponUseReadyTime = 0;
		StepRunner();
		return;
	}

	if(Action.m_Type == EActionType::STOP)
	{
		StopRunner();
		return;
	}

	if(Action.m_Type == EActionType::WAIT)
	{
		if(m_Runner.m_WaitUntil == 0)
			m_Runner.m_WaitUntil = Now + (int64_t)(Action.m_Seconds * time_freq());
		if(Now < m_Runner.m_WaitUntil)
			return;
		m_Runner.m_WaitUntil = 0;
		++m_Runner.m_ActionIndex;
		StepRunner();
		return;
	}

	if(Action.m_Type == EActionType::SWITCH_WEAPON_USE)
	{
		CNetObj_PlayerInput &Input = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
		const int WantedWeapon = Action.m_Weapon + 1;
		if(m_Runner.m_WeaponUseStep == 0)
		{
			Input.m_WantedWeapon = WantedWeapon;
			m_Runner.m_WeaponUseTarget = Action.m_Weapon;
			m_Runner.m_WeaponUseReadyTime = Now;
			m_Runner.m_WeaponUseStep = 1;
			return;
		}
		if(m_Runner.m_WeaponUseStep == 1)
		{
			// Switch and fire in the same tick makes the shot use the old weapon; wait until
			// the client shows the new one (or a short timeout) before pressing fire.
			const int64_t MinWait = time_freq() / 25;
			const int64_t MaxWait = time_freq() / 4;
			const int64_t Elapsed = Now - m_Runner.m_WeaponUseReadyTime;
			bool Equipped = false;
			if(GameClient()->m_Snap.m_pLocalCharacter)
				Equipped = GameClient()->m_Snap.m_pLocalCharacter->m_Weapon == Action.m_Weapon;
			if(Elapsed < MinWait || (!Equipped && Elapsed < MaxWait))
				return;
			m_Runner.m_WeaponUseStep = 2;
			return;
		}
		if(m_Runner.m_WeaponUseStep == 2)
		{
			Input.m_WantedWeapon = WantedWeapon;
			Input.m_Fire++;
			m_Runner.m_WeaponUseStep = 3;
			return;
		}
		m_Runner.m_WeaponUseStep = 0;
		m_Runner.m_WeaponUseTarget = 0;
		m_Runner.m_WeaponUseReadyTime = 0;
		++m_Runner.m_ActionIndex;
		StepRunner();
		return;
	}

	ExecuteAction(Action);
	++m_Runner.m_ActionIndex;
	if(m_Runner.m_ActionIndex < Shortcut.m_vActions.size())
	{
		const SAction &Next = Shortcut.m_vActions[m_Runner.m_ActionIndex];
		if(Next.m_Type == EActionType::WAIT)
			return;
	}
	StepRunner();
}
