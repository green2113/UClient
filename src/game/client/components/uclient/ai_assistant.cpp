#include "ai_assistant.h"

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>

#include <iterator>
#include <utility>

void CAiAssistant::OnConsoleInit()
{
	Console()->Register("toggle_ai_assistant", "", CFGFLAG_CLIENT, ConToggle, this, "Toggle the UClient AI assistant overlay");
	Console()->Register("ai_assistant_selftest", "", CFGFLAG_CLIENT, ConSelfTest, this, "Run local AI assistant fallback self-tests");
}

void CAiAssistant::OnReset()
{
	Close();
	m_vMessages.clear();
	ClearPendingProposal();
	m_vUndoLines.clear();
	m_aStatus[0] = '\0';
	if(m_pRequest)
	{
		m_pRequest->Abort();
		m_pRequest.reset();
	}
	m_RequestState = ERequestState::Idle;
}

void CAiAssistant::ConToggle(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CAiAssistant *>(pUserData)->Toggle();
}

void CAiAssistant::ConSelfTest(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	(void)pUserData;
	struct SCase
	{
		const char *m_pInput;
		const char *m_pExpectCommand;
	};
	const SCase aCases[] = {
		{"다이나믹 카메라 꺼줘", "cl_dyncam"},
		{"게임 언어 영어로 바꿔줘", "cl_languagefile"},
		{"마우스 왼쪽 클릭하면 발사되게 해줘", "mouse1"},
		{"g 키를 누르면 1번 이모티콘", "g"},
		{"컨트롤과 g키를 같이 누르면 안녕하세요", "ctrl+g"},
		{"rm -rf /", ""},
	};

	int Passed = 0;
	for(const auto &Case : aCases)
	{
		char aReply[256];
		std::vector<CUClientAiCatalog::SProposedCommand> vCommands;
		CUClientAiCatalog::TryLocalFallback(Case.m_pInput, vCommands, aReply, sizeof(aReply));
		bool Ok = false;
		if(Case.m_pExpectCommand[0] == '\0')
			Ok = vCommands.empty();
		else
		{
			for(const auto &Cmd : vCommands)
			{
				if(str_comp_nocase(Cmd.m_aCommand, Case.m_pExpectCommand) == 0 && Cmd.m_Valid)
				{
					Ok = true;
					break;
				}
			}
		}
		log_info("ai_assistant", "selftest '%s' -> %s (%s)", Case.m_pInput, Ok ? "PASS" : "FAIL", aReply);
		if(Ok)
			++Passed;
	}
	log_info("ai_assistant", "selftest finished: %d/%d passed", Passed, (int)std::size(aCases));
}

bool CAiAssistant::IsFeatureEnabled() const
{
	return g_Config.m_UcAiAssistant != 0;
}

int CAiAssistant::ResolveHotkey() const
{
	if(g_Config.m_UcAiAssistantHotkey[0] == '\0')
		return KEY_F8;
	const int Key = Input()->FindKeyByName(g_Config.m_UcAiAssistantHotkey);
	return Key > KEY_UNKNOWN ? Key : KEY_F8;
}

void CAiAssistant::Toggle()
{
	if(!IsFeatureEnabled())
		return;
	if(m_Active)
		Close();
	else
		Open();
}

void CAiAssistant::Open()
{
	if(!IsFeatureEnabled())
		return;
	m_Active = true;
	m_Input.Clear();
	m_Input.Activate(EInputPriority::UI);
	if(m_vMessages.empty())
		AddMessage(false, "설정이나 바인드를 말해 주세요. 예: 다이나믹 카메라 꺼줘 / 마우스 왼쪽 클릭하면 발사");
	str_copy(m_aStatus, "준비됨", sizeof(m_aStatus));
}

void CAiAssistant::Close()
{
	m_Active = false;
	if(m_Input.IsActive())
		m_Input.Deactivate();
}

void CAiAssistant::AddMessage(bool IsUser, const char *pText)
{
	SChatMessage Msg;
	Msg.m_IsUser = IsUser;
	str_copy(Msg.m_aText, pText ? pText : "", sizeof(Msg.m_aText));
	m_vMessages.push_back(Msg);
	if(m_vMessages.size() > 40)
		m_vMessages.erase(m_vMessages.begin(), m_vMessages.begin() + (m_vMessages.size() - 40));
}

void CAiAssistant::ClearPendingProposal()
{
	m_vPendingCommands.clear();
}

void CAiAssistant::SubmitCurrentInput()
{
	if(m_RequestState == ERequestState::Pending)
		return;
	const char *pText = m_Input.GetString();
	if(!pText || pText[0] == '\0')
		return;
	char aMessage[256];
	str_copy(aMessage, pText, sizeof(aMessage));
	m_Input.Clear();
	AddMessage(true, aMessage);
	StartRequest(aMessage);
}

void CAiAssistant::StartRequest(const char *pMessage)
{
	ClearPendingProposal();
	if(m_pRequest)
	{
		m_pRequest->Abort();
		m_pRequest.reset();
	}

	if(g_Config.m_UcAiAssistantEndpoint[0] == '\0')
	{
		ApplyLocalFallback(pMessage);
		return;
	}

	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("message");
	Writer.WriteStrValue(pMessage);
	Writer.WriteAttribute("locale");
	Writer.WriteStrValue("ko");
	Writer.WriteAttribute("catalog");
	CUClientAiCatalog::WriteCatalogJson(Writer, g_Config);
	Writer.WriteAttribute("history");
	Writer.BeginArray();
	const int HistoryStart = maximum(0, (int)m_vMessages.size() - 8);
	for(int i = HistoryStart; i < (int)m_vMessages.size(); ++i)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("role");
		Writer.WriteStrValue(m_vMessages[i].m_IsUser ? "user" : "assistant");
		Writer.WriteAttribute("content");
		Writer.WriteStrValue(m_vMessages[i].m_aText);
		Writer.EndObject();
	}
	Writer.EndArray();
	Writer.EndObject();
	const std::string Json = Writer.GetOutputString();

	auto pPost = HttpPostJson(g_Config.m_UcAiAssistantEndpoint, Json.c_str());
	pPost->Timeout(CTimeout{8000, 20000, 500, 10});
	pPost->IpResolve(IPRESOLVE::V4);
	pPost->LogProgress(HTTPLOG::FAILURE);
	pPost->FailOnErrorStatus(false);
	m_pRequest = std::move(pPost);
	Http()->Run(m_pRequest);
	m_RequestState = ERequestState::Pending;
	str_copy(m_aStatus, "AI에게 요청 중...", sizeof(m_aStatus));
}

void CAiAssistant::ApplyLocalFallback(const char *pMessage)
{
	char aReply[256];
	std::vector<CUClientAiCatalog::SProposedCommand> vCommands;
	const bool Ok = CUClientAiCatalog::TryLocalFallback(pMessage, vCommands, aReply, sizeof(aReply));
	AddMessage(false, aReply[0] ? aReply : "로컬 규칙으로 처리했습니다.");
	m_vPendingCommands = std::move(vCommands);
	str_copy(m_aStatus, Ok ? "미리보기 준비됨 (로컬)" : "로컬 해석 실패", sizeof(m_aStatus));
}

bool CAiAssistant::ParseResponseJson(const json_value *pRoot, char *pReply, int ReplySize, std::vector<CUClientAiCatalog::SProposedCommand> &vCommands)
{
	vCommands.clear();
	if(pReply && ReplySize > 0)
		pReply[0] = '\0';
	if(!pRoot || pRoot->type != json_object)
		return false;

	const json_value *pReplyVal = json_object_get(pRoot, "reply");
	if(pReplyVal && pReplyVal->type == json_string && pReply)
		str_copy(pReply, json_string_get(pReplyVal), ReplySize);

	const json_value *pCommands = json_object_get(pRoot, "commands");
	if(!pCommands || pCommands->type != json_array)
		return pReply && pReply[0] != '\0';

	for(int i = 0; i < json_array_length(pCommands); ++i)
	{
		const json_value *pCmd = json_array_get(pCommands, i);
		if(!pCmd || pCmd->type != json_object)
			continue;

		CUClientAiCatalog::SProposedCommand Cmd;
		const json_value *pOp = json_object_get(pCmd, "op");
		const json_value *pCommand = json_object_get(pCmd, "command");
		const json_value *pValue = json_object_get(pCmd, "value");
		if(pOp && pOp->type == json_string)
			str_copy(Cmd.m_aOp, json_string_get(pOp), sizeof(Cmd.m_aOp));
		if(pCommand && pCommand->type == json_string)
			str_copy(Cmd.m_aCommand, json_string_get(pCommand), sizeof(Cmd.m_aCommand));
		if(pValue && pValue->type == json_string)
			str_copy(Cmd.m_aValue, json_string_get(pValue), sizeof(Cmd.m_aValue));
		else if(pValue && pValue->type == json_integer)
			str_format(Cmd.m_aValue, sizeof(Cmd.m_aValue), "%d", json_int_get(pValue));
		else if(pValue && pValue->type == json_boolean)
			str_copy(Cmd.m_aValue, json_boolean_get(pValue) ? "1" : "0", sizeof(Cmd.m_aValue));

		if(!CUClientAiCatalog::ValidateAndNormalizeCommand(g_Config, Cmd))
		{
			char aErr[192];
			str_format(aErr, sizeof(aErr), "차단됨: %s (%s)", Cmd.m_aCommand[0] ? Cmd.m_aCommand : "?", Cmd.m_aError);
			AddMessage(false, aErr);
			continue;
		}
		vCommands.push_back(Cmd);
	}
	return true;
}

void CAiAssistant::PollRequest()
{
	if(m_RequestState != ERequestState::Pending || !m_pRequest)
		return;

	const EHttpState State = m_pRequest->State();
	if(State == EHttpState::RUNNING || State == EHttpState::QUEUED)
		return;

	m_RequestState = ERequestState::Idle;
	std::shared_ptr<CHttpRequest> pFinished = std::move(m_pRequest);
	m_pRequest.reset();

	auto Fallback = [&](const char *pReason) {
		str_copy(m_aStatus, pReason, sizeof(m_aStatus));
		if(g_Config.m_UcAiAssistantOfflineFallback)
		{
			const char *pLastUser = nullptr;
			for(int i = (int)m_vMessages.size() - 1; i >= 0; --i)
			{
				if(m_vMessages[i].m_IsUser)
				{
					pLastUser = m_vMessages[i].m_aText;
					break;
				}
			}
			if(pLastUser)
			{
				AddMessage(false, "서버 응답을 받지 못해 로컬 규칙으로 처리합니다.");
				ApplyLocalFallback(pLastUser);
				return;
			}
		}
		AddMessage(false, pReason);
	};

	if(State == EHttpState::ABORTED)
	{
		Fallback("요청이 취소되었습니다.");
		return;
	}
	if(State != EHttpState::DONE)
	{
		Fallback("네트워크 오류로 AI 요청에 실패했습니다.");
		return;
	}
	if(pFinished->StatusCode() != 200)
	{
		char aErr[128];
		str_format(aErr, sizeof(aErr), "AI 서버 오류 (HTTP %d)", pFinished->StatusCode());
		Fallback(aErr);
		return;
	}

	json_value *pJson = pFinished->ResultJson();
	if(!pJson)
	{
		Fallback("AI 응답 JSON을 파싱하지 못했습니다.");
		return;
	}

	char aReply[512];
	std::vector<CUClientAiCatalog::SProposedCommand> vCommands;
	if(!ParseResponseJson(pJson, aReply, sizeof(aReply), vCommands))
	{
		Fallback("AI 응답 형식이 올바르지 않습니다.");
		return;
	}

	AddMessage(false, aReply[0] ? aReply : "명령을 준비했습니다.");
	m_vPendingCommands = std::move(vCommands);
	str_copy(m_aStatus, m_vPendingCommands.empty() ? "적용할 명령 없음" : "미리보기 준비됨", sizeof(m_aStatus));
}

void CAiAssistant::ApplyPendingCommands()
{
	if(m_vPendingCommands.empty())
		return;

	m_vUndoLines.clear();
	int Applied = 0;
	for(const auto &Cmd : m_vPendingCommands)
	{
		if(!Cmd.m_Valid)
			continue;
		if(Cmd.m_aUndoLine[0] != '\0')
			m_vUndoLines.emplace_back(Cmd.m_aUndoLine);

		char aLine[320];
		if(str_comp_nocase(Cmd.m_aOp, "set") == 0)
		{
			if(Cmd.m_aValue[0] == '\0')
				str_format(aLine, sizeof(aLine), "%s \"\"", Cmd.m_aCommand);
			else
				str_format(aLine, sizeof(aLine), "%s %s", Cmd.m_aCommand, Cmd.m_aValue);
		}
		else if(str_comp_nocase(Cmd.m_aOp, "bind") == 0)
			str_format(aLine, sizeof(aLine), "bind %s \"%s\"", Cmd.m_aCommand, Cmd.m_aValue);
		else if(str_comp_nocase(Cmd.m_aOp, "unbind") == 0)
			str_format(aLine, sizeof(aLine), "unbind %s", Cmd.m_aCommand);
		else
			continue;

		Console()->ExecuteLine(aLine, IConsole::CLIENT_ID_UNSPECIFIED);
		++Applied;
	}

	char aMsg[128];
	str_format(aMsg, sizeof(aMsg), "%d개 명령을 적용했습니다.", Applied);
	AddMessage(false, aMsg);
	str_copy(m_aStatus, "적용 완료", sizeof(m_aStatus));
	ClearPendingProposal();
}

void CAiAssistant::CancelPendingCommands()
{
	ClearPendingProposal();
	str_copy(m_aStatus, "제안 취소됨", sizeof(m_aStatus));
	AddMessage(false, "제안된 명령을 취소했습니다.");
}

void CAiAssistant::UndoLastApply()
{
	if(m_vUndoLines.empty())
	{
		str_copy(m_aStatus, "되돌릴 변경 없음", sizeof(m_aStatus));
		return;
	}
	for(auto It = m_vUndoLines.rbegin(); It != m_vUndoLines.rend(); ++It)
	{
		if(!It->empty())
			Console()->ExecuteLine(It->c_str(), IConsole::CLIENT_ID_UNSPECIFIED);
	}
	m_vUndoLines.clear();
	AddMessage(false, "마지막 적용을 되돌렸습니다.");
	str_copy(m_aStatus, "되돌리기 완료", sizeof(m_aStatus));
}

void CAiAssistant::OnUpdate()
{
	PollRequest();
}

bool CAiAssistant::OnInput(const IInput::CEvent &Event)
{
	if(!IsFeatureEnabled())
		return false;

	if(!m_Active)
	{
		if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == ResolveHotkey())
		{
			Open();
			return true;
		}
		return false;
	}

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE)
	{
		Close();
		return true;
	}
	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == ResolveHotkey())
	{
		Close();
		return true;
	}
	if((Event.m_Flags & IInput::FLAG_PRESS) && (Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER))
	{
		SubmitCurrentInput();
		return true;
	}

	return m_Input.ProcessInput(Event);
}

void CAiAssistant::RenderPanel()
{
	const float Height = 480.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);

	CUIRect Screen = {0.0f, 0.0f, Width, Height};
	CUIRect Panel;
	Screen.VMargin(Width * 0.22f, &Panel);
	Panel.HMargin(Height * 0.12f, &Panel);

	Panel.Draw(ColorRGBA(0.08f, 0.09f, 0.12f, 0.94f), IGraphics::CORNER_ALL, 8.0f);

	CUIRect Header, Body, Footer, InputRow, Buttons;
	Panel.Margin(10.0f, &Panel);
	Panel.HSplitTop(24.0f, &Header, &Panel);
	Panel.HSplitBottom(70.0f, &Body, &Footer);
	Ui()->DoLabel(&Header, "UClient AI Assistant", 16.0f, TEXTALIGN_ML);

	CUIRect Status;
	Header.VSplitRight(180.0f, &Header, &Status);
	TextRender()->TextColor(0.7f, 0.75f, 0.8f, 1.0f);
	Ui()->DoLabel(&Status, m_aStatus, 11.0f, TEXTALIGN_MR);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	Body.Draw(ColorRGBA(0.05f, 0.06f, 0.08f, 0.75f), IGraphics::CORNER_ALL, 6.0f);
	Body.Margin(8.0f, &Body);

	float Y = Body.y;
	const float LineH = 14.0f;
	for(const auto &Msg : m_vMessages)
	{
		if(Y + LineH > Body.y + Body.h - 4.0f)
			break;
		CUIRect Line = {Body.x, Y, Body.w, LineH * 2.2f};
		char aLine[560];
		str_format(aLine, sizeof(aLine), "%s: %s", Msg.m_IsUser ? "You" : "AI", Msg.m_aText);
		TextRender()->TextColor(Msg.m_IsUser ? ColorRGBA(0.85f, 0.9f, 1.0f, 1.0f) : ColorRGBA(0.75f, 0.95f, 0.8f, 1.0f));
		Ui()->DoLabel(&Line, aLine, 11.0f, TEXTALIGN_TL);
		Y += LineH * 2.0f;
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	if(!m_vPendingCommands.empty())
	{
		CUIRect Proposal = {Body.x, minimum(Y + 4.0f, Body.y + Body.h - 48.0f), Body.w, 44.0f};
		Proposal.Draw(ColorRGBA(0.15f, 0.22f, 0.35f, 0.9f), IGraphics::CORNER_ALL, 4.0f);
		Proposal.Margin(4.0f, &Proposal);
		char aProp[384] = "제안: ";
		for(size_t i = 0; i < m_vPendingCommands.size(); ++i)
		{
			if(i > 0)
				str_append(aProp, " | ", sizeof(aProp));
			str_append(aProp, m_vPendingCommands[i].m_aDisplay, sizeof(aProp));
		}
		Ui()->DoLabel(&Proposal, aProp, 11.0f, TEXTALIGN_ML);
	}

	Footer.HSplitTop(28.0f, &InputRow, &Buttons);
	InputRow.Draw(ColorRGBA(0.12f, 0.13f, 0.16f, 1.0f), IGraphics::CORNER_ALL, 4.0f);
	Ui()->DoEditBox(&m_Input, &InputRow, 12.0f);

	Buttons.HSplitTop(6.0f, nullptr, &Buttons);
	CUIRect Send, Apply, Cancel, Undo;
	Buttons.VSplitLeft(Buttons.w * 0.24f, &Send, &Buttons);
	Buttons.VSplitLeft(6.0f, nullptr, &Buttons);
	Buttons.VSplitLeft(Buttons.w * 0.32f, &Apply, &Buttons);
	Buttons.VSplitLeft(6.0f, nullptr, &Buttons);
	Buttons.VSplitLeft(Buttons.w * 0.48f, &Cancel, &Buttons);
	Buttons.VSplitLeft(6.0f, nullptr, &Buttons);
	Undo = Buttons;

	auto DrawBtn = [&](CButtonContainer &Btn, CUIRect Rect, const char *pLabel, bool Enabled) {
		const ColorRGBA Col = Enabled ? ColorRGBA(0.22f, 0.28f, 0.4f, 1.0f) : ColorRGBA(0.15f, 0.15f, 0.18f, 0.8f);
		Rect.Draw(Col, IGraphics::CORNER_ALL, 4.0f);
		Ui()->DoLabel(&Rect, pLabel, 11.0f, TEXTALIGN_MC);
		return Enabled && Ui()->DoButtonLogic(&Btn, 0, &Rect, BUTTONFLAG_LEFT);
	};

	if(DrawBtn(m_SendButton, Send, "전송", m_RequestState != ERequestState::Pending))
		SubmitCurrentInput();
	if(DrawBtn(m_ApplyButton, Apply, "적용", !m_vPendingCommands.empty()))
		ApplyPendingCommands();
	if(DrawBtn(m_CancelButton, Cancel, "취소", !m_vPendingCommands.empty()))
		CancelPendingCommands();
	if(DrawBtn(m_UndoButton, Undo, "되돌리기", !m_vUndoLines.empty()))
		UndoLastApply();
}

void CAiAssistant::OnRender()
{
	if(!m_Active || !IsFeatureEnabled())
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK && Client()->State() != IClient::STATE_OFFLINE)
		return;

	RenderPanel();
}
