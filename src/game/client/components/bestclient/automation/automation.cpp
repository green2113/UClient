#include "automation.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/client/components/chat.h>
#include <game/client/components/controls.h>
#include <game/client/gameclient.h>

static constexpr const char *SHORTCUTS_FILE = "uclient_shortcuts.json";

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
	return false;
}

static bool ParseTrigger(const json_value *pTrigger, CAutomation::STrigger &Out)
{
	if(!pTrigger || pTrigger->type != json_object)
		return false;
	const json_value *pType = json_object_get(pTrigger, "type");
	const char *pTypeStr = pType && pType->type == json_string ? pType->u.string.ptr : nullptr;
	if(!pTypeStr || str_comp(pTypeStr, "chat_received") != 0)
		return false;
	Out = CAutomation::STrigger{};
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
		return !Out.m_Filters.empty();
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
		Out.m_Channel = ParseChannel(pChannel && pChannel->type == json_string ? pChannel->u.string.ptr : nullptr);
		const json_value *pMessage = json_object_get(pAction, "message");
		if(!pMessage || pMessage->type != json_string || !pMessage->u.string.ptr)
			return false;
		Out.m_Message = pMessage->u.string.ptr;
		return true;
	}
	if(str_comp(pTypeStr, "wait") == 0)
	{
		Out.m_Type = CAutomation::EActionType::WAIT;
		const json_value *pSeconds = json_object_get(pAction, "seconds");
		Out.m_Seconds = pSeconds && pSeconds->type == json_double ? pSeconds->u.dbl :
									 pSeconds && pSeconds->type == json_integer ? (double)pSeconds->u.integer :
														 1.0;
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
		const json_value *pColor = json_object_get(pAction, "color");
		Out.m_Color = pColor && pColor->type == json_integer ? pColor->u.integer : 0;
		return true;
	}
	if(str_comp(pTypeStr, "set_feet_color") == 0)
	{
		Out.m_Type = CAutomation::EActionType::SET_FEET_COLOR;
		const json_value *pTarget = json_object_get(pAction, "target");
		Out.m_Target = ParseTarget(pTarget && pTarget->type == json_string ? pTarget->u.string.ptr : nullptr);
		const json_value *pColor = json_object_get(pAction, "color");
		Out.m_Color = pColor && pColor->type == json_integer ? pColor->u.integer : 0;
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
	return false;
}

void CAutomation::OnInit()
{
	TryReloadRules();
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
	if(m_RunnerActive)
		StepRunner();
}

static void EscapeParamLocal(char *pDst, const char *pSrc, int Size)
{
	str_copy(pDst, pSrc, Size);
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
				const json_value *pTrigger = json_object_get(pEntry, "trigger");
				if(!ParseTrigger(pTrigger, Shortcut.m_Trigger))
					continue;
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

	if(Filter.m_Text.empty())
		return false;
	return MatchText(Filter.m_Match, Filter.m_Text.c_str(), Event.m_Text.c_str());
}

bool CAutomation::ChannelsMatch(EChatChannel TriggerChannel, EChatChannel EventChannel) const
{
	if(TriggerChannel == EventChannel)
		return true;
	// When UI channel is "All", listen to both global and team server chat.
	if(TriggerChannel == EChatChannel::ALL && (EventChannel == EChatChannel::ALL || EventChannel == EChatChannel::TEAM))
		return true;
	return false;
}

bool CAutomation::MatchesChatTrigger(const SShortcut &Shortcut, const SChatEvent &Event) const
{
	const STrigger &Trigger = Shortcut.m_Trigger;
	if(Trigger.m_Type != ETriggerType::CHAT_RECEIVED)
		return false;
	if(!ChannelsMatch(Trigger.m_Channel, Event.m_Channel))
		return false;
	if(Trigger.m_Filters.empty())
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
		if(!Shortcut.m_Enabled)
			continue;
		if(MatchesChatTrigger(Shortcut, Event))
		{
			StartRunner(i);
			break;
		}
	}
}

void CAutomation::StartRunner(size_t ShortcutIndex)
{
	if(ShortcutIndex >= m_vShortcuts.size())
		return;
	m_Runner = SRunner{};
	m_Runner.m_ShortcutIndex = ShortcutIndex;
	m_Runner.m_ActionIndex = 0;
	m_RunnerActive = true;
	StepRunner();
}

void CAutomation::StopRunner()
{
	m_RunnerActive = false;
	m_Runner = SRunner{};
}

void CAutomation::ExecuteAction(const SAction &Action)
{
	char aCmd[512];
	switch(Action.m_Type)
	{
	case EActionType::SEND_CHAT:
		if(Action.m_Channel == EChatChannel::UCLIENT)
			GameClient()->m_Chat.SayUClient(Action.m_Message.c_str());
		else
			GameClient()->m_Chat.SendChat(Action.m_Channel == EChatChannel::TEAM ? 1 : 0, Action.m_Message.c_str());
		break;
	case EActionType::SET_SKIN:
		str_format(aCmd, sizeof(aCmd), "%s_skin %s", TargetPrefix(Action.m_Target), Action.m_Skin.c_str());
		Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
		break;
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
	if(!m_RunnerActive || m_Runner.m_ShortcutIndex >= m_vShortcuts.size())
	{
		StopRunner();
		return;
	}

	const SShortcut &Shortcut = m_vShortcuts[m_Runner.m_ShortcutIndex];
	if(m_Runner.m_ActionIndex >= Shortcut.m_vActions.size())
	{
		StopRunner();
		return;
	}

	const SAction &Action = Shortcut.m_vActions[m_Runner.m_ActionIndex];
	const int64_t Now = time_get();

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
		if(m_Runner.m_WeaponUseStep == 0)
		{
			Input.m_WantedWeapon = Action.m_Weapon + 1;
			m_Runner.m_WeaponUseStep = 1;
			return;
		}
		if(m_Runner.m_WeaponUseStep == 1)
		{
			Input.m_Fire++;
			m_Runner.m_WeaponUseStep = 2;
			return;
		}
		m_Runner.m_WeaponUseStep = 0;
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
