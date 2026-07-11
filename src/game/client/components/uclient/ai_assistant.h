#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_AI_ASSISTANT_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_AI_ASSISTANT_H

#include "ai_catalog.h"

#include <engine/console.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>

#include <game/client/component.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>

#include <memory>
#include <string>
#include <vector>

class CAiAssistant : public CComponent
{
public:
	enum class ERequestState
	{
		Idle,
		Pending,
	};

	struct SChatMessage
	{
		bool m_IsUser = false;
		char m_aText[512] = "";
	};

	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnReset() override;
	void OnRender() override;
	void OnUpdate() override;
	bool OnInput(const IInput::CEvent &Event) override;

	bool IsActive() const { return m_Active; }
	void Toggle();
	void Open();
	void Close();

private:
	bool m_Active = false;
	CLineInputBuffered<256> m_Input;
	std::vector<SChatMessage> m_vMessages;
	std::vector<CUClientAiCatalog::SProposedCommand> m_vPendingCommands;
	std::vector<std::string> m_vUndoLines;
	char m_aStatus[192] = "";
	ERequestState m_RequestState = ERequestState::Idle;
	std::shared_ptr<CHttpRequest> m_pRequest;
	CButtonContainer m_ApplyButton;
	CButtonContainer m_CancelButton;
	CButtonContainer m_UndoButton;
	CButtonContainer m_SendButton;

	static void ConToggle(IConsole::IResult *pResult, void *pUserData);
	static void ConSelfTest(IConsole::IResult *pResult, void *pUserData);

	bool IsFeatureEnabled() const;
	int ResolveHotkey() const;
	void AddMessage(bool IsUser, const char *pText);
	void ClearPendingProposal();
	void SubmitCurrentInput();
	void StartRequest(const char *pMessage);
	void PollRequest();
	bool ParseResponseJson(const json_value *pRoot, char *pReply, int ReplySize, std::vector<CUClientAiCatalog::SProposedCommand> &vCommands);
	void ApplyLocalFallback(const char *pMessage);
	void ApplyPendingCommands();
	void CancelPendingCommands();
	void UndoLastApply();
	void RenderPanel();
};

#endif
