#include <base/math.h>
#include <base/system.h>
#include <base/time.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/bc_ui_animations.h>
#include <game/client/components/chat.h>
#include <game/client/components/hud_layout.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <vector>

void CMenus::PopupConfirmPasteImageFromChat()
{
	GameClient()->m_Chat.m_UcChatPaste.ConfirmPasteWarning(&GameClient()->m_Chat, m_PopupConfirmCheckboxValue);
	m_PopupConfirmHasCheckbox = false;
	SetActive(false);
}

void CMenus::PopupCancelPasteImageFromChat()
{
	GameClient()->m_Chat.m_UcChatPaste.CancelPasteWarning(&GameClient()->m_Chat);
	m_PopupConfirmHasCheckbox = false;
	SetActive(false);
}

void CMenus::OfferAutoLoginFromChat(int Kind, const char *pCode)
{
	if(Kind != AUTO_LOGIN_OFFER_JAPAN && Kind != AUTO_LOGIN_OFFER_KOG)
		return;
	if(!pCode || pCode[0] == '\0')
		return;

	m_AutoLoginOfferKind = Kind;
	str_copy(m_aAutoLoginOfferCode, pCode, sizeof(m_aAutoLoginOfferCode));

	PopupConfirm(
		"Auto Login",
		"When you connect to this server, enable auto login with the code you just entered?",
		Localize("Confirm"),
		Localize("Cancel"),
		&CMenus::PopupConfirmAutoLoginOffer,
		POPUP_NONE,
		&CMenus::PopupCancelAutoLoginOffer,
		POPUP_NONE);
	SetActive(true);
}

void CMenus::PopupConfirmAutoLoginOffer()
{
	if(m_AutoLoginOfferKind == AUTO_LOGIN_OFFER_JAPAN)
	{
		g_Config.m_UcAutoLoginJapan = 1;
		str_copy(g_Config.m_UcAutoLoginJapanCode, m_aAutoLoginOfferCode, sizeof(g_Config.m_UcAutoLoginJapanCode));
		g_Config.m_UcAutoLoginJapanPromptShown = 1;
	}
	else if(m_AutoLoginOfferKind == AUTO_LOGIN_OFFER_KOG)
	{
		g_Config.m_UcAutoLoginKog = 1;
		str_copy(g_Config.m_UcAutoLoginKogCode, m_aAutoLoginOfferCode, sizeof(g_Config.m_UcAutoLoginKogCode));
		g_Config.m_UcAutoLoginKogPromptShown = 1;
	}

	m_AutoLoginOfferKind = AUTO_LOGIN_OFFER_NONE;
	m_aAutoLoginOfferCode[0] = '\0';
	SetActive(false);
}

void CMenus::PopupCancelAutoLoginOffer()
{
	if(m_AutoLoginOfferKind == AUTO_LOGIN_OFFER_JAPAN)
		g_Config.m_UcAutoLoginJapanPromptShown = 1;
	else if(m_AutoLoginOfferKind == AUTO_LOGIN_OFFER_KOG)
		g_Config.m_UcAutoLoginKogPromptShown = 1;

	m_AutoLoginOfferKind = AUTO_LOGIN_OFFER_NONE;
	m_aAutoLoginOfferCode[0] = '\0';
	SetActive(false);
}

void CMenus::OfferDisableUcChatSendSameServerForReply()
{
	PopupConfirm(
		"UClient chat",
		"'Only send to users on the same server' is currently enabled. Turn that setting off?",
		Localize("Yes"),
		Localize("No"),
		&CMenus::PopupConfirmDisableUcChatSendSameServerForReply,
		POPUP_NONE,
		&CMenus::PopupCancelDisableUcChatSendSameServerForReply,
		POPUP_NONE);
	SetActive(true);
}

void CMenus::PopupConfirmDisableUcChatSendSameServerForReply()
{
	g_Config.m_UcChatSendSameServerOnly = 0;
	GameClient()->m_Chat.ApplyStashedUcReplyAfterSendScopePrompt();
	SetActive(false);
}

void CMenus::PopupCancelDisableUcChatSendSameServerForReply()
{
	GameClient()->m_Chat.ClearStashedUcReplySendScopePrompt();
	SetActive(false);
}

void CMenus::RenderSettingsUClient(CUIRect MainView)
{
	enum
	{
		UCLIENT_TAB_GAMEPLAY = 0,
		UCLIENT_TAB_OTHERS,
		UCLIENT_TAB_CHAT_ROOMS,
		NUM_UCLIENT_TABS,
	};

	static int s_CurTab = UCLIENT_TAB_GAMEPLAY;
	{
		int NavTab = -1;
		if(ConsumeSettingsLinkNavUClientTab(NavTab) && NavTab >= 0 && NavTab < NUM_UCLIENT_TABS)
			s_CurTab = NavTab;
	}
	static CButtonContainer s_aPageTabs[NUM_UCLIENT_TABS] = {};
	if(s_CurTab < 0 || s_CurTab >= NUM_UCLIENT_TABS)
		s_CurTab = UCLIENT_TAB_GAMEPLAY;

	const float LineSize = 20.0f;
	const float EditBoxFontSize = 12.0f;
	const float ColorPickerLineSize = 25.0f;
	const float ColorPickerLabelSize = 13.0f;
	const float ColorPickerLineSpacing = 5.0f;
	const float HeadlineFontSize = 20.0f;
	const float MarginSmall = 5.0f;
	const float MarginBetweenSections = 30.0f;
	const float MarginBetweenViews = 30.0f;
	const ColorRGBA BlockColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);

	const auto UpdateRevealPhase = [&](float &Phase, bool Expanded) {
		if(BCUiAnimations::Enabled() && g_Config.m_BcModuleUiRevealAnimation != 0)
			BCUiAnimations::UpdatePhase(Phase, Expanded ? 1.0f : 0.0f, Client()->RenderFrameTime(), BCUiAnimations::MsToSeconds(g_Config.m_BcModuleUiRevealAnimationMs));
		else
			Phase = Expanded ? 1.0f : 0.0f;
	};

	MainView.HSplitTop(8.0f, nullptr, &MainView);

	CUIRect TabBar, TabButton;
	MainView.HSplitTop(24.0f, &TabBar, &MainView);
	const char *apTabNames[NUM_UCLIENT_TABS] = {
		Localize("Gameplay"),
		Localize("Others"),
		Localize("Chat rooms"),
	};
	const float TabWidth = TabBar.w / (float)NUM_UCLIENT_TABS;
	for(int Tab = 0; Tab < NUM_UCLIENT_TABS; ++Tab)
	{
		TabBar.VSplitLeft(TabWidth, &TabButton, &TabBar);
		const int Corners = Tab == 0 ? IGraphics::CORNER_L : (Tab == NUM_UCLIENT_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurTab == Tab, &TabButton, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			s_CurTab = Tab;
		if(Ui()->MouseHovered(&TabButton) && Input()->KeyPress(KEY_MOUSE_2))
			TryOpenSettingsLinkMenuForPage("UClient", CUClientSettingsLink::UClientTabToken(Tab), &TabButton);
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	SetSettingsLinkContext(SETTINGS_UCLIENT, CUClientSettingsLink::UClientTabToken(s_CurTab));

	static CScrollRegion s_UClientScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_UClientScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);
	SetSettingsLinkScrollRegion(&s_UClientScrollRegion);

	MainView.y += ScrollOffset.y;
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);

	CUIRect LeftView, RightView;
	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	static std::vector<CUIRect> s_SectionBoxes;
	static vec2 s_PrevScrollOffset(0.0f, 0.0f);
	for(CUIRect &Section : s_SectionBoxes)
	{
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.y -= s_PrevScrollOffset.y - ScrollOffset.y;
		Section.Draw(BlockColor, IGraphics::CORNER_ALL, 10.0f);
	}
	s_PrevScrollOffset = ScrollOffset;
	s_SectionBoxes.clear();

	auto BeginBlock = [&](CUIRect &ColumnRef, float ContentHeight, CUIRect &Content) {
		CUIRect Block;
		ColumnRef.HSplitTop(ContentHeight, &Block, &ColumnRef);
		s_SectionBoxes.push_back(Block);
		Content = Block;
	};

	float LeftColumnEndY = LeftView.y;
	float RightColumnEndY = RightView.y;

	if(s_CurTab == UCLIENT_TAB_GAMEPLAY)
	{
		CUIRect Column = LeftView;
		Column.HSplitTop(10.0f, nullptr, &Column);

		// Back notify
		{
			static float s_BackPhase = 0.0f;
			const bool BackExpanded = g_Config.m_UcNotifyWhenBack != 0;
			UpdateRevealPhase(s_BackPhase, BackExpanded);
			const float ExpandedTargetHeight = MarginSmall + LineSize + MarginSmall + ColorPickerLineSize + ColorPickerLineSpacing + LineSize * 6.0f;
			const float ExpandedHeight = ExpandedTargetHeight * s_BackPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
			CUIRect Content, Label, Button, Visible;
			BeginBlock(Column, ContentHeight, Content);
			Content.HSplitTop(LineSize, &Label, &Content);
			Label.VSplitRight(MarginSmall, &Label, nullptr);
			Ui()->DoLabel(&Label, Localize("Back notify"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcNotifyWhenBack, Localize("Show when a teammate behind you freezes"), &g_Config.m_UcNotifyWhenBack, &Content, LineSize);
			if(g_Config.m_UcNotifyWhenBack && !HudLayout::IsEnabled(HudLayout::MODULE_NOTIFY_BACK))
				HudLayout::SetEnabled(HudLayout::MODULE_NOTIFY_BACK, true);
			if(ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				PushSettingsLinkParent("uc_back_notify");
				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				CUIRect TextRow, TextLabel, BackHudEditorButton;
				Expand.HSplitTop(LineSize, &TextRow, &Expand);
				TextRow.VSplitRight(LineSize + 8.0f, &TextRow, &BackHudEditorButton);
				static CButtonContainer s_BackHudEditorButton;
				const bool BackCanOpenHudEditor = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
				const bool BackHudEditorClicked = Ui()->DoButton_FontIcon(&s_BackHudEditorButton, FontIcon::UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, BackCanOpenHudEditor ? 0 : -1, &BackHudEditorButton, BUTTONFLAG_LEFT);
				GameClient()->m_Tooltips.DoToolTip(&s_BackHudEditorButton, &BackHudEditorButton, BackCanOpenHudEditor ? Localize("Open in HUD editor") : Localize("Join a game first"));
				GameClient()->m_Tooltips.SetFadeTime(&s_BackHudEditorButton, 0.0f);
				if(BackHudEditorClicked && BackCanOpenHudEditor)
				{
					SetActive(false);
					GameClient()->m_HudEditor.Activate();
				}
				TextRow.VSplitLeft(40.0f, &TextLabel, &TextRow);
				Ui()->DoLabel(&TextLabel, Localize("Text"), 12.0f, TEXTALIGN_ML);
				static CLineInput s_BackInput(g_Config.m_UcNotifyWhenBackText, sizeof(g_Config.m_UcNotifyWhenBackText));
				s_BackInput.SetEmptyText("Back");
				Ui()->DoEditBox(&s_BackInput, &TextRow, EditBoxFontSize);

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				static CButtonContainer s_NotifyWhenBackColor;
				DoLine_ColorPicker(&s_NotifyWhenBackColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Expand, Localize("Color"), &g_Config.m_UcNotifyWhenBackColor, ColorRGBA(1.0f, 0.55f, 0.15f), false);

				Expand.HSplitTop(LineSize, &Button, &Expand);
				DoScrollbarOptionSettingsLink(&g_Config.m_UcNotifyWhenBackMaxDistance, &g_Config.m_UcNotifyWhenBackMaxDistance, &Button, Localize("Max distance"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcNotifyWhenBackShowCount, Localize("Show frozen user count"), &g_Config.m_UcNotifyWhenBackShowCount, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcNotifyWhenBackShowNames, Localize("Show frozen user names"), &g_Config.m_UcNotifyWhenBackShowNames, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Button, &Expand);
				DoScrollbarOptionSettingsLink(&g_Config.m_UcNotifyWhenBackX, &g_Config.m_UcNotifyWhenBackX, &Button, Localize("Horizontal Position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				Expand.HSplitTop(LineSize, &Button, &Expand);
				DoScrollbarOptionSettingsLink(&g_Config.m_UcNotifyWhenBackY, &g_Config.m_UcNotifyWhenBackY, &Button, Localize("Vertical Position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				Expand.HSplitTop(LineSize, &Button, &Expand);
				DoScrollbarOptionSettingsLink(&g_Config.m_UcNotifyWhenBackSize, &g_Config.m_UcNotifyWhenBackSize, &Button, Localize("Font Size"), 1, 50);
				PopSettingsLinkParent();
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Timeout reconnect
		{
			static float s_TimeoutPhase = 0.0f;
			static float s_AutoTimeoutPhase = 0.0f;
			const bool TimeoutExpanded = g_Config.m_UcShowTimeoutReconnect != 0;
			const bool AutoExpanded = TimeoutExpanded && g_Config.m_UcAutoTimeoutReconnect != 0;
			UpdateRevealPhase(s_TimeoutPhase, TimeoutExpanded);
			UpdateRevealPhase(s_AutoTimeoutPhase, AutoExpanded);

			const float WarningGap = 1.0f;
			const float WarningHeight = WarningGap + LineSize;
			const float AutoExpandedHeight = WarningHeight * s_AutoTimeoutPhase;
			const float ExpandedTargetHeight = MarginSmall + LineSize + AutoExpandedHeight;
			const float ExpandedHeight = ExpandedTargetHeight * s_TimeoutPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;

			CUIRect Content, Label, Visible;
			BeginBlock(Column, ContentHeight, Content);
			Content.HSplitTop(LineSize, &Label, &Content);
			Label.VSplitRight(MarginSmall, &Label, nullptr);
			Ui()->DoLabel(&Label, Localize("Reconnect timeout"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcShowTimeoutReconnect, Localize("Show remaining reconnect timeout"), &g_Config.m_UcShowTimeoutReconnect, &Content, LineSize);
			if(ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				PushSettingsLinkParent("uc_show_timeout_reconnect");
				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcAutoTimeoutReconnect, "Auto reconnect to server when timeout expires", &g_Config.m_UcAutoTimeoutReconnect, &Expand, LineSize);
				if(AutoExpandedHeight > 0.0f)
				{
					CUIRect Warning;
					Expand.HSplitTop(WarningGap, nullptr, &Expand);
					Expand.HSplitTop(LineSize, &Warning, &Expand);
					// Slow pulse (~0.4 Hz); keep alpha high so the warning stays readable.
					const float Pulse = 0.5f + 0.5f * std::sin((float)time_get() / (float)time_freq() * (0.8f * pi));
					const float Blink = 0.55f + 0.45f * Pulse;
					TextRender()->TextColor(1.0f, 0.15f, 0.1f, Blink);
					Ui()->DoLabel(&Warning, "You may be reconnected while playing the map.", 11.0f, TEXTALIGN_ML);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
				PopSettingsLinkParent();
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		LeftColumnEndY = Column.y;
		Column = RightView;
		Column.HSplitTop(10.0f, nullptr, &Column);

		// Auto login
		{
			static float s_AutoLoginJapanPhase = 0.0f;
			static float s_AutoLoginKogPhase = 0.0f;
			const bool JapanExpanded = g_Config.m_UcAutoLoginJapan != 0;
			const bool KogExpanded = g_Config.m_UcAutoLoginKog != 0;
			UpdateRevealPhase(s_AutoLoginJapanPhase, JapanExpanded);
			UpdateRevealPhase(s_AutoLoginKogPhase, KogExpanded);

			const float CodeBoxHeight = LineSize;
			const float ExpandedTargetHeight = MarginSmall + CodeBoxHeight;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedTargetHeight * s_AutoLoginJapanPhase + MarginSmall + LineSize + ExpandedTargetHeight * s_AutoLoginKogPhase;
			CUIRect Content, Label, Visible, Row;
			BeginBlock(Column, ContentHeight, Content);

			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, Localize("Auto Login"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcAutoLoginJapan, Localize("Auto login to Japan server"), &g_Config.m_UcAutoLoginJapan, &Content, LineSize);
			const float JapanCurHeight = ExpandedTargetHeight * s_AutoLoginJapanPhase;
			if(JapanCurHeight > 0.0f)
			{
				Content.HSplitTop(JapanCurHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				PushSettingsLinkParent("uc_auto_login_japan");
				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(CodeBoxHeight, &Row, &Expand);
				static CLineInput s_AutoLoginJapanCode(g_Config.m_UcAutoLoginJapanCode, sizeof(g_Config.m_UcAutoLoginJapanCode));
				s_AutoLoginJapanCode.SetEmptyText("Enter Japan server login code");
				Ui()->DoClearableEditBox(&s_AutoLoginJapanCode, &Row, 14.0f);
				MaybeRegisterSettingsLinkVar(&g_Config.m_UcAutoLoginJapanCode, Localize("Japan server login code"));
				PopSettingsLinkParent();
			}

			Content.HSplitTop(MarginSmall, nullptr, &Content);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcAutoLoginKog, Localize("Auto login to KoG server"), &g_Config.m_UcAutoLoginKog, &Content, LineSize);
			const float KogCurHeight = ExpandedTargetHeight * s_AutoLoginKogPhase;
			if(KogCurHeight > 0.0f)
			{
				Content.HSplitTop(KogCurHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				PushSettingsLinkParent("uc_auto_login_kog");
				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(CodeBoxHeight, &Row, &Expand);
				static CLineInput s_AutoLoginKogCode(g_Config.m_UcAutoLoginKogCode, sizeof(g_Config.m_UcAutoLoginKogCode));
				s_AutoLoginKogCode.SetEmptyText("Enter KoG server login code");
				Ui()->DoClearableEditBox(&s_AutoLoginKogCode, &Row, 14.0f);
				MaybeRegisterSettingsLinkVar(&g_Config.m_UcAutoLoginKogCode, Localize("KoG server login code"));
				PopSettingsLinkParent();
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		RightColumnEndY = Column.y;
	}
	else if(s_CurTab == UCLIENT_TAB_OTHERS)
	{
		CUIRect Column = LeftView;
		Column.HSplitTop(10.0f, nullptr, &Column);

		// UClient chat
		{
			static float s_UcChatPhase = 0.0f;
			const bool ChatExpanded = g_Config.m_UcChat != 0;
			UpdateRevealPhase(s_UcChatPhase, ChatExpanded);
			// Friends-only overrides the same-server scope, so those two options reveal/collapse
			// with their own phase just like the whole section does for "Enable UClient chat".
			static float s_UcChatSameServerPhase = 0.0f;
			UpdateRevealPhase(s_UcChatSameServerPhase, g_Config.m_UcChatFriendsOnly == 0);
			const float SameServerTargetHeight = LineSize * 2.0f;
			const float SameServerHeight = SameServerTargetHeight * s_UcChatSameServerPhase;
			// Same idea for the join/leave announce options once they are friends-only.
			static float s_UcJoinOptionsPhase = 0.0f;
			UpdateRevealPhase(s_UcJoinOptionsPhase, g_Config.m_UcServerJoinFriendsOnly == 0);
			const float JoinOptionsTargetHeight = LineSize * 2.0f;
			const float JoinOptionsHeight = JoinOptionsTargetHeight * s_UcJoinOptionsPhase;
			const float ExpandedTargetHeight = MarginSmall + LineSize * 2.0f + SameServerHeight + JoinOptionsHeight + ColorPickerLineSize + ColorPickerLineSpacing;
			const float ExpandedHeight = ExpandedTargetHeight * s_UcChatPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
			CUIRect Content, Label, Visible;
			BeginBlock(Column, ContentHeight, Content);
			Content.HSplitTop(LineSize, &Label, &Content);
			Label.VSplitRight(MarginSmall, &Label, nullptr);
			Ui()->DoLabel(&Label, "UClient chat", HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcChat, "Enable UClient chat", &g_Config.m_UcChat, &Content, LineSize);
			if(ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				PushSettingsLinkParent("uc_chat");
				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcChatFriendsOnly, "Only send to and show chats from friends", &g_Config.m_UcChatFriendsOnly, &Expand, LineSize);

				if(SameServerHeight > 0.0f)
				{
					CUIRect SameServerVisible;
					Expand.HSplitTop(SameServerHeight, &SameServerVisible, &Expand);
					Ui()->ClipEnable(&SameServerVisible);
					SScopedClip SameServerClipGuard{Ui()};
					CUIRect SameServer = {SameServerVisible.x, SameServerVisible.y, SameServerVisible.w, SameServerTargetHeight};

					// Inverted parent: these rows only exist while friends-only is off, so a copied
					// link has to carry it and clear it instead of enabling it.
					PushSettingsLinkParent("!uc_chat_friends_only");

					// "채팅 보기" (show) couples with "유저 전송" (send):
					// show 0→1 with send 0 => send 1
					// send 1→0 while show 1 => show 0
					// show 1→0 while send was auto-coupled => send 0
					static bool s_UcChatSendCoupledWithShow = false;
					const int OldShow = g_Config.m_UcChatShowSameServerOnly;
					const int OldSend = g_Config.m_UcChatSendSameServerOnly;

					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcChatSendSameServerOnly, "Only send to users on the same server", &g_Config.m_UcChatSendSameServerOnly, &SameServer, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcChatShowSameServerOnly, "Only show chats from users on the same server", &g_Config.m_UcChatShowSameServerOnly, &SameServer, LineSize);

					if(OldSend && !g_Config.m_UcChatSendSameServerOnly && g_Config.m_UcChatShowSameServerOnly)
					{
						g_Config.m_UcChatShowSameServerOnly = 0;
						s_UcChatSendCoupledWithShow = false;
					}
					else if(g_Config.m_UcChatSendSameServerOnly != OldSend)
					{
						s_UcChatSendCoupledWithShow = false;
					}

					if(g_Config.m_UcChatShowSameServerOnly != OldShow)
					{
						if(g_Config.m_UcChatShowSameServerOnly)
						{
							if(OldSend == 0)
							{
								g_Config.m_UcChatSendSameServerOnly = 1;
								s_UcChatSendCoupledWithShow = true;
							}
							else
							{
								s_UcChatSendCoupledWithShow = false;
							}
						}
						else if(s_UcChatSendCoupledWithShow)
						{
							g_Config.m_UcChatSendSameServerOnly = 0;
							s_UcChatSendCoupledWithShow = false;
						}
					}

					PopSettingsLinkParent();
				}

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcServerJoinFriendsOnly, "Only announce to and show join/leave from friends", &g_Config.m_UcServerJoinFriendsOnly, &Expand, LineSize);

				if(JoinOptionsHeight > 0.0f)
				{
					CUIRect JoinOptionsVisible;
					Expand.HSplitTop(JoinOptionsHeight, &JoinOptionsVisible, &Expand);
					Ui()->ClipEnable(&JoinOptionsVisible);
					SScopedClip JoinOptionsClipGuard{Ui()};
					CUIRect JoinOptions = {JoinOptionsVisible.x, JoinOptionsVisible.y, JoinOptionsVisible.w, JoinOptionsTargetHeight};

					PushSettingsLinkParent("!uc_server_join_friends_only");

					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcServerJoinSend, "Announce when I join or leave a server", &g_Config.m_UcServerJoinSend, &JoinOptions, LineSize);
					DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcServerJoinShow, "Show server join/leave messages", &g_Config.m_UcServerJoinShow, &JoinOptions, LineSize);

					PopSettingsLinkParent();
				}

				static CButtonContainer s_UcMessageColor;
				DoLine_ColorPicker(&s_UcMessageColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Expand, "UClient message", &g_Config.m_UcMessageColor, ColorRGBA(0.63f, 0.92f, 1.0f));
				PopSettingsLinkParent();
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Misc
		{
			const float ContentHeight = LineSize + MarginSmall + LineSize * 2.0f;
			CUIRect Content, Label, Button;
			BeginBlock(Column, ContentHeight, Content);
			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, Localize("Misc"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);

			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcShowSharedCursors, Localize("Show shared cursors"), &g_Config.m_UcShowSharedCursors, &Content, LineSize);

			Content.HSplitTop(LineSize, &Button, &Content);
			CUIRect PlayerSearchLabel, PlayerSearchDropDown;
			Button.VSplitMid(&PlayerSearchLabel, &PlayerSearchDropDown, MarginSmall);
			Ui()->DoLabel(&PlayerSearchLabel, Localize("Player name search"), 14.0f, TEXTALIGN_ML);
			static const char *s_apPlayerSearchEngines[] = {"DDNet", "DDStats"};
			static CUi::SDropDownState s_PlayerSearchEngineDropDownState;
			static CScrollRegion s_PlayerSearchEngineDropDownScrollRegion;
			s_PlayerSearchEngineDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_PlayerSearchEngineDropDownScrollRegion;
			const int OldPlayerSearchEngine = g_Config.m_UcChatPlayerSearchEngine == 1 ? 1 : 0;
			const int NewPlayerSearchEngine = Ui()->DoDropDown(&PlayerSearchDropDown, OldPlayerSearchEngine, s_apPlayerSearchEngines, (int)std::size(s_apPlayerSearchEngines), s_PlayerSearchEngineDropDownState);
			if(NewPlayerSearchEngine != OldPlayerSearchEngine)
				g_Config.m_UcChatPlayerSearchEngine = NewPlayerSearchEngine;

			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		LeftColumnEndY = Column.y;
		RightColumnEndY = RightView.y;
	}
	else
	{
		// D1 does not push membership changes, so poll only while this page is visible.
		GameClient()->m_UClientChatRooms.RefreshIfStale(5);
		const auto &vRooms = GameClient()->m_UClientChatRooms.Rooms();
		CUIRect Page = MainView;
		Page.VMargin(10.0f, &Page);
		Page.HSplitTop(10.0f, nullptr, &Page);

		static CLineInputBuffered<64> s_CreateRoomInput;
		static CLineInputBuffered<40> s_JoinRoomInput;
		static CLineInputBuffered<64> s_RenameRoomInput;
		static char s_aRenameRoomId[64] = "";
		static CButtonContainer s_CreateButton, s_JoinButton, s_RefreshButton;
		int OwnedRoomCount = 0;
		for(const auto &Room : vRooms)
			OwnedRoomCount += Room.m_Owner ? 1 : 0;
		const bool CanCreateRoom = OwnedRoomCount < 5;
		s_CreateRoomInput.SetEmptyText(CanCreateRoom ? Localize("Room name") : Localize("Room creation limit reached"));
		s_JoinRoomInput.SetEmptyText(Localize("Invite code"));

		// Page heading and primary actions.
		{
			CUIRect Content, Heading, RoomLimitLabel, RefreshButton, Actions, CreateAction, JoinAction;
			BeginBlock(Page, 48.0f, Content);
			Content.HSplitTop(LineSize, &Heading, &Content);
			Heading.VSplitRight(76.0f, &Heading, &RefreshButton);
			Heading.VSplitRight(8.0f, &Heading, nullptr);
			Heading.VSplitRight(82.0f, &Heading, &RoomLimitLabel);
			Ui()->DoLabel(&Heading, Localize("Chat rooms"), HeadlineFontSize, TEXTALIGN_ML);
			char aRoomLimit[32];
			str_format(aRoomLimit, sizeof(aRoomLimit), "%d / 5", OwnedRoomCount);
			Ui()->DoLabel(&RoomLimitLabel, aRoomLimit, 11.0f, TEXTALIGN_MR);
			if(DoButton_Menu(&s_RefreshButton, Localize("Refresh"), 0, &RefreshButton))
				GameClient()->m_UClientChatRooms.Refresh();

			Content.HSplitTop(8.0f, nullptr, &Content);
			Content.HSplitTop(LineSize, &Actions, &Content);
			Actions.VSplitMid(&CreateAction, &JoinAction, 20.0f);

			CUIRect CreateInput, CreateButton;
			CreateAction.VSplitRight(76.0f, &CreateInput, &CreateButton);
			CreateInput.VSplitRight(8.0f, &CreateInput, nullptr);
			Ui()->DoClearableEditBox(&s_CreateRoomInput, &CreateInput, EditBoxFontSize);
			if(DoButton_Menu(&s_CreateButton, CanCreateRoom ? Localize("Create") : Localize("Limit"), !CanCreateRoom, &CreateButton) &&
				CanCreateRoom && s_CreateRoomInput.GetString()[0])
			{
				GameClient()->m_UClientChatRooms.Create(s_CreateRoomInput.GetString(), g_Config.m_PlayerName);
				s_CreateRoomInput.Set("");
			}

			CUIRect JoinInput, JoinButton;
			JoinAction.VSplitRight(76.0f, &JoinInput, &JoinButton);
			JoinInput.VSplitRight(8.0f, &JoinInput, nullptr);
			Ui()->DoClearableEditBox(&s_JoinRoomInput, &JoinInput, EditBoxFontSize);
			if(DoButton_Menu(&s_JoinButton, Localize("Join"), 0, &JoinButton) && s_JoinRoomInput.GetString()[0])
			{
				GameClient()->m_UClientChatRooms.Join(s_JoinRoomInput.GetString(), g_Config.m_PlayerName);
				s_JoinRoomInput.Set("");
			}
			Page.HSplitTop(MarginBetweenSections, nullptr, &Page);
		}

		if(GameClient()->m_UClientChatRooms.ErrorMessage()[0])
		{
			CUIRect Content, ErrorRow;
			BeginBlock(Page, LineSize, Content);
			Content.HSplitTop(LineSize, &ErrorRow, &Content);
			TextRender()->TextColor(1.0f, 0.35f, 0.35f, 1.0f);
			Ui()->DoLabel(&ErrorRow, GameClient()->m_UClientChatRooms.ErrorMessage(), 11.0f, TEXTALIGN_ML, {.m_MaxWidth = ErrorRow.w});
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			Page.HSplitTop(MarginBetweenSections, nullptr, &Page);
		}

		static std::vector<CButtonContainer> s_vSelectButtons;
		static std::vector<CButtonContainer> s_vLeaveButtons;
		static std::vector<CButtonContainer> s_vCopyButtons;
		static std::vector<CButtonContainer> s_vRegenerateButtons;
		static std::vector<CButtonContainer> s_vRenameButtons;
		static std::vector<CButtonContainer> s_vKickButtons;
		s_vSelectButtons.resize(vRooms.size());
		s_vLeaveButtons.resize(vRooms.size());
		s_vCopyButtons.resize(vRooms.size());
		s_vRegenerateButtons.resize(vRooms.size());
		s_vRenameButtons.resize(vRooms.size());
		size_t MemberCount = 0;
		for(const auto &Room : vRooms)
			MemberCount += Room.m_vMembers.size();
		s_vKickButtons.resize(MemberCount);
		size_t MemberButtonIndex = 0;

		if(vRooms.empty())
		{
			CUIRect Content, EmptyRow;
			BeginBlock(Page, 60.0f, Content);
			Content.HSplitTop(60.0f, &EmptyRow, &Content);
			Ui()->DoLabel(&EmptyRow, Localize("You have not joined any chat rooms yet."), 12.0f, TEXTALIGN_MC);
			Page.HSplitTop(MarginBetweenSections, nullptr, &Page);
		}

		for(size_t RoomIndex = 0; RoomIndex < vRooms.size(); ++RoomIndex)
		{
			const auto &Room = vRooms[RoomIndex];
			const int MemberRows = maximum(1, (int)Room.m_vMembers.size());
			const float OwnerRowsHeight = Room.m_Owner ? 46.0f : 0.0f;
			const float RoomHeight = 24.0f + 8.0f + OwnerRowsHeight + 20.0f + MemberRows * 24.0f;
			CUIRect Content, Header, MembersTitle;
			BeginBlock(Page, RoomHeight, Content);

			Content.HSplitTop(24.0f, &Header, &Content);
			CUIRect HeaderText, SelectButton, LeaveButton;
			Header.VSplitRight(76.0f, &HeaderText, &LeaveButton);
			HeaderText.VSplitRight(8.0f, &HeaderText, nullptr);
			HeaderText.VSplitRight(88.0f, &HeaderText, &SelectButton);
			HeaderText.VSplitRight(8.0f, &HeaderText, nullptr);
			char aRoomTitle[160];
			str_format(aRoomTitle, sizeof(aRoomTitle), "%s  ·  %s", Room.m_aName, Room.m_Owner ? Localize("Owner") : Localize("Member"));
			Ui()->DoLabel(&HeaderText, aRoomTitle, 14.0f, TEXTALIGN_ML, {.m_MaxWidth = HeaderText.w});
			const bool Selected = !str_comp(g_Config.m_UcChatSendRoom, Room.m_aId);
			if(DoButton_Menu(&s_vSelectButtons[RoomIndex], Selected ? Localize("Selected") : Localize("Select"), Selected, &SelectButton))
				GameClient()->m_UClientChatRooms.SelectSendRoom(Room.m_aId);
			if(DoButton_Menu(&s_vLeaveButtons[RoomIndex], Room.m_Owner ? Localize("Delete") : Localize("Leave"), 0, &LeaveButton))
			{
				str_copy(m_aPendingUClientRoomId, Room.m_aId, sizeof(m_aPendingUClientRoomId));
				m_aPendingUClientRoomMemberId[0] = '\0';
				PopupConfirm(Room.m_Owner ? Localize("Delete room") : Localize("Leave room"),
					Room.m_Owner ? Localize("Are you sure you want to delete this room?") : Localize("Are you sure you want to leave this room?"),
					Room.m_Owner ? Localize("Delete") : Localize("Leave"), Localize("Cancel"),
					&CMenus::PopupConfirmUClientRoomAction);
			}
			Content.HSplitTop(8.0f, nullptr, &Content);

			if(Room.m_Owner)
			{
				CUIRect InviteRow, RenameRow;
				Content.HSplitTop(LineSize, &InviteRow, &Content);
				CUIRect InviteLabel, InviteCode, CopyButton, RegenerateButton;
				InviteRow.VSplitLeft(80.0f, &InviteLabel, &InviteRow);
				Ui()->DoLabel(&InviteLabel, Localize("Invite code"), 11.0f, TEXTALIGN_ML);
				InviteRow.VSplitRight(94.0f, &InviteRow, &RegenerateButton);
				InviteRow.VSplitRight(8.0f, &InviteRow, nullptr);
				InviteRow.VSplitRight(64.0f, &InviteRow, &CopyButton);
				InviteRow.VSplitRight(8.0f, &InviteCode, nullptr);
				InviteCode.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f), IGraphics::CORNER_ALL, 5.0f);
				InviteCode.HMargin(6.0f, &InviteCode);
				Ui()->DoLabel(&InviteCode, Room.m_aInviteCode, 12.0f, TEXTALIGN_ML);
				if(DoButton_Menu(&s_vCopyButtons[RoomIndex], Localize("Copy"), 0, &CopyButton))
					Input()->SetClipboardText(Room.m_aInviteCode);
				if(DoButton_Menu(&s_vRegenerateButtons[RoomIndex], Localize("New code"), 0, &RegenerateButton))
					GameClient()->m_UClientChatRooms.RegenerateCode(Room.m_aId);

				Content.HSplitTop(6.0f, nullptr, &Content);
				Content.HSplitTop(LineSize, &RenameRow, &Content);
				CUIRect RenameLabel, RenameValue, RenameButton;
				RenameRow.VSplitLeft(80.0f, &RenameLabel, &RenameRow);
				Ui()->DoLabel(&RenameLabel, Localize("Room name"), 11.0f, TEXTALIGN_ML);
				RenameRow.VSplitRight(76.0f, &RenameValue, &RenameButton);
				RenameValue.VSplitRight(8.0f, &RenameValue, nullptr);
				if(!str_comp(s_aRenameRoomId, Room.m_aId))
				{
					Ui()->DoClearableEditBox(&s_RenameRoomInput, &RenameValue, EditBoxFontSize);
					if(DoButton_Menu(&s_vRenameButtons[RoomIndex], Localize("Save"), 0, &RenameButton) && s_RenameRoomInput.GetString()[0])
					{
						GameClient()->m_UClientChatRooms.Rename(Room.m_aId, s_RenameRoomInput.GetString());
						s_aRenameRoomId[0] = '\0';
						s_RenameRoomInput.Set("");
					}
				}
				else
				{
					Ui()->DoLabel(&RenameValue, Room.m_aName, 12.0f, TEXTALIGN_ML, {.m_MaxWidth = RenameValue.w});
					if(DoButton_Menu(&s_vRenameButtons[RoomIndex], Localize("Rename"), 0, &RenameButton))
					{
						str_copy(s_aRenameRoomId, Room.m_aId, sizeof(s_aRenameRoomId));
						s_RenameRoomInput.Set(Room.m_aName);
					}
				}
			}

			Content.HSplitTop(LineSize, &MembersTitle, &Content);
			char aMembersTitle[64];
			str_format(aMembersTitle, sizeof(aMembersTitle), Localize("Members (%d)"), (int)Room.m_vMembers.size());
			Ui()->DoLabel(&MembersTitle, aMembersTitle, 12.0f, TEXTALIGN_ML);

			if(Room.m_vMembers.empty())
			{
				CUIRect EmptyMemberRow;
				Content.HSplitTop(24.0f, &EmptyMemberRow, &Content);
				Ui()->DoLabel(&EmptyMemberRow, Localize("No members"), 11.0f, TEXTALIGN_ML);
			}
			else
			{
				for(size_t MemberIndex = 0; MemberIndex < Room.m_vMembers.size(); ++MemberIndex)
				{
					const auto &Member = Room.m_vMembers[MemberIndex];
					CUIRect MemberRow;
					Content.HSplitTop(24.0f, &MemberRow, &Content);
					MemberRow.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, MemberIndex % 2 == 0 ? 0.055f : 0.025f), IGraphics::CORNER_ALL, 5.0f);
					MemberRow.Margin(3.0f, &MemberRow);
					CUIRect MemberName = MemberRow;
					if(Room.m_Owner && !Member.m_Owner)
					{
						CUIRect KickButton;
						MemberName.VSplitRight(62.0f, &MemberName, &KickButton);
						MemberName.VSplitRight(8.0f, &MemberName, nullptr);
						if(DoButton_Menu(&s_vKickButtons[MemberButtonIndex], Localize("Kick"), 0, &KickButton))
						{
							str_copy(m_aPendingUClientRoomId, Room.m_aId, sizeof(m_aPendingUClientRoomId));
							str_copy(m_aPendingUClientRoomMemberId, Member.m_aId, sizeof(m_aPendingUClientRoomMemberId));
							PopupConfirm(Localize("Remove member"), Localize("Are you sure you want to remove this member?"),
								Localize("Kick"), Localize("Cancel"), &CMenus::PopupConfirmUClientRoomAction);
						}
					}
					CUIRect RoleLabel;
					MemberName.VSplitRight(64.0f, &MemberName, &RoleLabel);
					Ui()->DoLabel(&MemberName, Member.m_aDisplayName, 11.0f, TEXTALIGN_ML, {.m_MaxWidth = MemberName.w});
					TextRender()->TextColor(ColorRGBA(0.7f, 0.7f, 0.75f, 1.0f));
					Ui()->DoLabel(&RoleLabel, Member.m_Owner ? Localize("Owner") : Localize("Member"), 10.0f, TEXTALIGN_MR);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					++MemberButtonIndex;
				}
			}
			Page.HSplitTop(MarginBetweenSections, nullptr, &Page);
		}

		LeftColumnEndY = Page.y;
		RightColumnEndY = Page.y;
	}

	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = maximum(LeftColumnEndY, RightColumnEndY) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_UClientScrollRegion.AddRect(ScrollRegion);
	s_UClientScrollRegion.End();
	SetSettingsLinkScrollRegion(nullptr);
}

void CMenus::PopupConfirmUClientRoomAction()
{
	if(!m_aPendingUClientRoomId[0])
		return;
	if(m_aPendingUClientRoomMemberId[0])
		GameClient()->m_UClientChatRooms.Kick(m_aPendingUClientRoomId, m_aPendingUClientRoomMemberId);
	else
		GameClient()->m_UClientChatRooms.Leave(m_aPendingUClientRoomId);
	m_aPendingUClientRoomId[0] = '\0';
	m_aPendingUClientRoomMemberId[0] = '\0';
}
