#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_AUTOMATION_AUTOMATION_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_AUTOMATION_AUTOMATION_H

#include <game/client/component.h>

#include <cstdint>
#include <string>
#include <vector>

class CAutomation : public CComponent
{
public:
	enum class ETriggerType
	{
		NONE,
		CHAT_RECEIVED,
	};

	enum class EChatChannel
	{
		ALL,
		TEAM,
		UCLIENT,
	};

	enum class ESenderFilter
	{
		EVERYONE,
		ME,
		SPECIFIC,
	};

	enum class ETextMatch
	{
		CONTAINS,
		EQUALS,
		STARTS_WITH,
	};

	enum class EActionType
	{
		SEND_CHAT,
		WAIT,
		SWITCH_WEAPON_USE,
		SET_SKIN,
		SET_CUSTOM_COLOR,
		SET_BODY_COLOR,
		SET_FEET_COLOR,
		SET_NAME,
	};

	enum class ETarget
	{
		PLAYER,
		DUMMY,
	};

	struct STrigger
	{
		ETriggerType m_Type = ETriggerType::NONE;
		EChatChannel m_Channel = EChatChannel::ALL;
		ESenderFilter m_Sender = ESenderFilter::EVERYONE;
		std::string m_SenderName;
		ETextMatch m_Match = ETextMatch::CONTAINS;
		std::string m_Text;
	};

	struct SAction
	{
		EActionType m_Type = EActionType::WAIT;
		EChatChannel m_Channel = EChatChannel::ALL;
		std::string m_Message;
		double m_Seconds = 0.0;
		int m_Weapon = 0;
		ETarget m_Target = ETarget::PLAYER;
		std::string m_Skin;
		bool m_CustomColorEnabled = false;
		int m_Color = 0;
		std::string m_Name;
	};

	struct SShortcut
	{
		std::string m_Id;
		std::string m_Name;
		bool m_Enabled = true;
		STrigger m_Trigger;
		std::vector<SAction> m_vActions;
	};

	struct SChatEvent
	{
		EChatChannel m_Channel;
		int m_ClientId;
		std::string m_Name;
		std::string m_Text;
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnUpdate() override;

	void OnChatReceived(const SChatEvent &Event);

private:
	struct SRunner
	{
		size_t m_ShortcutIndex = 0;
		size_t m_ActionIndex = 0;
		int64_t m_WaitUntil = 0;
		int m_WeaponUseStep = 0;
	};

	std::vector<SShortcut> m_vShortcuts;
	int64_t m_LastFileCheck = 0;
	int64_t m_FileModifiedTime = -1;
	SRunner m_Runner;
	bool m_RunnerActive = false;

	bool IsActive() const;
	void TryReloadRules();
	bool ParseRulesFile(const char *pJson, size_t Length);
	void EvaluateChatTriggers(const SChatEvent &Event);
	bool MatchesChatTrigger(const SShortcut &Shortcut, const SChatEvent &Event) const;
	bool MatchText(ETextMatch Match, const char *pNeedle, const char *pHaystack) const;
	void StartRunner(size_t ShortcutIndex);
	void StopRunner();
	void StepRunner();
	void ExecuteAction(const SAction &Action);
};

#endif
