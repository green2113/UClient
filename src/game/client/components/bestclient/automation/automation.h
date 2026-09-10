#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_AUTOMATION_AUTOMATION_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_AUTOMATION_AUTOMATION_H

#include <base/net.h>

#include <game/client/component.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class CAutomation : public CComponent
{
public:
	enum class ETriggerType
	{
		NONE,
		CHAT_RECEIVED,
		SERVER_CONNECT,
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

	enum class EValueMode
	{
		TEXT,
		VARIABLE,
	};

	enum class EActionType
	{
		SEND_CHAT,
		TEXT,
		WAIT,
		SWITCH_WEAPON_USE,
		SET_SKIN,
		SET_CUSTOM_COLOR,
		SET_BODY_COLOR,
		SET_FEET_COLOR,
		SET_NAME,
		GET,
		IF,
		OTHERWISE,
		END_IF,
		STOP,
		CONNECT_SERVER,
		LEAVE_SERVER,
	};

	struct STextPart
	{
		EValueMode m_Mode = EValueMode::TEXT;
		std::string m_Text;
		std::string m_Variable;
	};

	enum class ETarget
	{
		PLAYER,
		DUMMY,
	};

	struct SChatFilter
	{
		enum class EKind
		{
			SENDER,
			MESSAGE,
			CHAT_CHANNEL,
			UCLIENT_ROOM,
		};

		EKind m_Kind = EKind::MESSAGE;
		ESenderFilter m_Sender = ESenderFilter::EVERYONE;
		std::vector<std::string> m_SenderNames;
		std::string m_SenderName;
		ETextMatch m_Match = ETextMatch::CONTAINS;
		std::string m_Text;
	};

	struct STrigger
	{
		ETriggerType m_Type = ETriggerType::NONE;
		EChatChannel m_Channel = EChatChannel::ALL;
		std::vector<SChatFilter> m_Filters;
		std::vector<std::string> m_ServerTargets;
	};

	struct SAction
	{
		EActionType m_Type = EActionType::WAIT;
		EChatChannel m_Channel = EChatChannel::ALL;
		EValueMode m_ChannelMode = EValueMode::TEXT;
		std::string m_ChannelVariable;
		EValueMode m_UClientRoomMode = EValueMode::TEXT;
		std::string m_UClientRoomId;
		std::string m_UClientRoomVariable;
		EValueMode m_MessageMode = EValueMode::TEXT;
		std::string m_Message;
		std::string m_Variable;
		std::vector<STextPart> m_TextParts;
		std::string m_OutputVariable;
		std::string m_ServerAddress;
		double m_Seconds = 0.0;
		int m_Weapon = 0;
		ETarget m_Target = ETarget::PLAYER;
		std::string m_Skin;
		bool m_CustomColorEnabled = false;
		int m_Color = 0;
		std::string m_Name;
		std::string m_GetProperty;
		std::string m_IfLeft;
		std::string m_IfOp;
		std::string m_IfRight;
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
		std::string m_UClientRoomId;
		std::string m_UClientRoomName;
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnUpdate() override;
	void OnStateChange(int NewState, int OldState) override;

	void OnChatReceived(const SChatEvent &Event);

private:
	// One entry per if block the runner is currently inside, so nested ifs keep
	// their own "did the condition match" state.
	struct SIfFrame
	{
		size_t m_EndIfIndex = SIZE_MAX;
		bool m_TrueBranch = false;
	};

	struct SRunner
	{
		size_t m_ShortcutIndex = 0;
		// Kept so a rules reload mid-run cannot make the index point at another
		// shortcut's actions.
		std::string m_ShortcutId;
		size_t m_ActionIndex = 0;
		int64_t m_WaitUntil = 0;
		int m_WeaponUseStep = 0;
		int m_WeaponUseTarget = 0;
		int64_t m_WeaponUseReadyTime = 0;
		std::vector<SIfFrame> m_vIfFrames;
		bool m_HadChatEvent = false;
		bool m_TestRun = false;
		SChatEvent m_ChatEvent;
		std::unordered_map<std::string, std::string> m_Variables;
	};

	std::vector<SShortcut> m_vShortcuts;
	int64_t m_LastFileCheck = 0;
	int64_t m_FileModifiedTime = -1;
	SRunner m_Runner;
	bool m_RunnerActive = false;
	bool m_ServerConnectTriggeredForSession = false;

	// Test run driven by the launcher through two files in the user directory.
	std::optional<SShortcut> m_TestShortcut;
	std::string m_TestRunId;
	std::vector<std::pair<size_t, std::string>> m_vTestResults;
	int64_t m_LastTestRequestCheck = 0;
	std::string m_TestRequestRaw;
	size_t m_TestReportedStep = SIZE_MAX;
	int64_t m_LastTestStateWrite = 0;

	bool IsActive() const;
	void TryReloadRules();
	bool ParseRulesFile(const char *pJson, size_t Length);
	void EvaluateServerConnectTriggers(const NETADDR &ServerAddr);
	bool MatchesServerConnectTrigger(const SShortcut &Shortcut, const NETADDR &ServerAddr) const;
	bool TargetMatchesServer(const std::string &Target, const NETADDR &ServerAddr) const;
	bool MatchesChatTrigger(const SShortcut &Shortcut, const SChatEvent &Event) const;
	bool ChannelsMatch(EChatChannel TriggerChannel, EChatChannel EventChannel) const;
	bool MatchesChatFilter(const SChatFilter &Filter, const SChatEvent &Event, bool IsMe) const;
	bool MatchText(ETextMatch Match, const char *pNeedle, const char *pHaystack) const;
	void StartRunner(size_t ShortcutIndex, const SChatEvent *pChatEvent = nullptr);
	size_t FindMatchingEndIf(size_t IfIndex) const;
	size_t FindOtherwise(size_t IfIndex, size_t EndIfIndex) const;
	std::string ActiveWindowValue() const;
	void SetRunnerVariable(const char *pName, const std::string &Value);
	const SShortcut *RunnerShortcut() const;
	void PollTestRunRequest();
	bool ParseTestRunRequest(const char *pJson, size_t Length);
	void WriteTestRunState(const char *pStatus);
	void ClearTestRun();
	bool EvaluateIfCondition(const SAction &Action) const;
	bool ValuesMatch(const std::string &Left, const std::string &Op, const std::string &Right) const;
	void ExecuteGetAction(const SAction &Action);
	void ExecuteTextAction(const SAction &Action);
	std::string ResolveMessageValue(const SAction &Action) const;
	bool ResolveSendChannel(const SAction &Action, EChatChannel &OutChannel) const;
	std::string ResolveUClientRoomId(const SAction &Action) const;
	std::string ResolveVariable(const char *pKey) const;
	std::string ResolveFilterKey(const char *pKey, const SChatEvent &Event) const;
	std::string ExpandFilterText(const std::string &Template, const SChatEvent &Event) const;
	std::string ExpandTemplate(const std::string &Template) const;
	void StopRunner();
	void StepRunner();
	void ExecuteAction(const SAction &Action);
};

#endif
