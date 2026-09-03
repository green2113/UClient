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
#include <map>
#include <string>
#include <vector>

#include <game/client/components/uclient/chat_rooms.h>

void CMenus::PopupConfirmPasteImageWarning()
{
	const bool WasActive = IsActive();
	PopupConfirmWithCheckbox(
		Localize("Warning"),
		Localize("You are about to paste an image from your clipboard into chat. Make sure it does not contain personal information."),
		Localize("Paste"),
		Localize("Cancel"),
		Localize("Don't ask again"),
		false,
		&CMenus::PopupConfirmPasteImageFromChat,
		CMenus::POPUP_NONE,
		&CMenus::PopupCancelPasteImageFromChat,
		CMenus::POPUP_NONE);
	m_PopupDeactivateAfterButton = !WasActive;
	if(!WasActive)
		SetActive(true);
}

void CMenus::PopupConfirmPasteImageFromChat()
{
	GameClient()->m_Chat.m_UcChatPaste.ConfirmPasteWarning(&GameClient()->m_Chat, m_PopupConfirmCheckboxValue);
	m_PopupConfirmHasCheckbox = false;
	if(m_PopupDeactivateAfterButton)
		SetActive(false);
	m_PopupDeactivateAfterButton = false;
}

void CMenus::PopupCancelPasteImageFromChat()
{
	GameClient()->m_Chat.m_UcChatPaste.CancelPasteWarning(&GameClient()->m_Chat);
	m_PopupConfirmHasCheckbox = false;
	if(m_PopupDeactivateAfterButton)
		SetActive(false);
	m_PopupDeactivateAfterButton = false;
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

static void DrawUcMenuBadge(IGraphics *pGraphics, CUi *pUi, ITextRender *pTextRender, CUIRect *pRow, const char *pText, float FontSize, const ColorRGBA &Top, const ColorRGBA &Bottom, float Gap)
{
	const float BadgeWidth = pTextRender->TextWidth(FontSize, pText) + 10.0f;
	CUIRect Badge;
	pRow->VSplitRight(BadgeWidth + Gap, pRow, &Badge);
	Badge.VSplitLeft(Gap, nullptr, &Badge);
	Badge.HMargin(2.0f, &Badge);
	pGraphics->DrawRect4(Badge.x, Badge.y, Badge.w, Badge.h, Top, Bottom, Top, Bottom, IGraphics::CORNER_ALL, 5.0f);
	pUi->DoLabel(&Badge, pText, FontSize, TEXTALIGN_MC);
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

		// Weapon aim trajectory
		{
			const bool WeaponTrajBlocked = GameClient()->IsWeaponTrajBlocked();
			const float BlockedHintHeight = WeaponTrajBlocked ? (MarginSmall + LineSize) : 0.0f;
			const float ContentHeight = LineSize + MarginSmall + LineSize + BlockedHintHeight;
			CUIRect Content, Label;
			BeginBlock(Column, ContentHeight, Content);
			Content.HSplitTop(LineSize, &Label, &Content);
			DrawUcMenuBadge(Graphics(), Ui(), TextRender(), &Label, Localize("NEW"), 12.0f,
				ColorRGBA(0.25f, 0.85f, 0.40f, 1.0f), ColorRGBA(0.10f, 0.60f, 0.25f, 1.0f), MarginSmall);
			Ui()->DoLabel(&Label, Localize("Weapon trajectory"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			PushSettingsLinkParent("uc_weapon_traj");
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcWeaponTraj, Localize("Show weapon trajectory"), &g_Config.m_UcWeaponTraj, &Content, LineSize);
			PopSettingsLinkParent();
			if(WeaponTrajBlocked)
			{
				Content.HSplitTop(MarginSmall, nullptr, &Content);
				Content.HSplitTop(LineSize, &Label, &Content);
				TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
				Ui()->DoLabel(&Label, Localize("Looks like you're on a server where this feature is forbidden"), 12.0f, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Spectator teleport destination preview
		{
			const float ContentHeight = LineSize + MarginSmall + LineSize;
			CUIRect Content, Label;
			BeginBlock(Column, ContentHeight, Content);
			Content.HSplitTop(LineSize, &Label, &Content);
			DrawUcMenuBadge(Graphics(), Ui(), TextRender(), &Label, Localize("NEW"), 12.0f,
				ColorRGBA(0.25f, 0.85f, 0.40f, 1.0f), ColorRGBA(0.10f, 0.60f, 0.25f, 1.0f), MarginSmall);
			Ui()->DoLabel(&Label, Localize("Teleport preview"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			PushSettingsLinkParent("uc_spec_tele_preview");
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcSpecTelePreview, Localize("Show teleport destinations in freeview"), &g_Config.m_UcSpecTelePreview, &Content, LineSize);
			PopSettingsLinkParent();
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
			const float ExpandedTargetHeight = MarginSmall + LineSize * 6.0f + ColorPickerLineSize + ColorPickerLineSpacing;
			const float ExpandedHeight = ExpandedTargetHeight * s_UcChatPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
			CUIRect Content, Label, Visible, Row, DropDown;
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

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Row.VSplitMid(&Label, &DropDown, MarginSmall);
				Ui()->DoLabel(&Label, Localize("Send target"), 14.0f, TEXTALIGN_ML);
				static CUi::SDropDownState s_ChatTargetDropDownState;
				static CScrollRegion s_ChatTargetDropDownScrollRegion;
				GameClient()->m_Chat.RenderUClientChatTargetDropDown(DropDown, s_ChatTargetDropDownState, s_ChatTargetDropDownScrollRegion);

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcChatHideGlobal, Localize("Hide messages sent to all UClient users"), &g_Config.m_UcChatHideGlobal, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcChatDisableAudienceMentions, Localize("Disable @everyone and @here mentions"), &g_Config.m_UcChatDisableAudienceMentions, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcServerJoinSend, Localize("Announce when I join or leave a server"), &g_Config.m_UcServerJoinSend, &Expand, LineSize);

				Expand.HSplitTop(LineSize, &Row, &Expand);
				Row.VSplitMid(&Label, &DropDown, MarginSmall);
				Ui()->DoLabel(&Label, Localize("Join/leave send target"), 14.0f, TEXTALIGN_ML);
				const auto &vRooms = GameClient()->m_UClientChatRooms.Rooms();
				std::vector<std::string> vAnnouncementLabels = {Localize("All UClient users")};
				vAnnouncementLabels.reserve(1 + vRooms.size());
				for(const auto &Room : vRooms)
				{
					char aLabel[96];
					str_format(aLabel, sizeof(aLabel), "%s (%d)", Room.m_aName, (int)Room.m_vMembers.size());
					vAnnouncementLabels.emplace_back(aLabel);
				}
				std::vector<const char *> vpAnnouncementLabels;
				vpAnnouncementLabels.reserve(vAnnouncementLabels.size());
				for(const auto &Entry : vAnnouncementLabels)
					vpAnnouncementLabels.push_back(Entry.c_str());
				int CurrentAnnouncementTarget = 0;
				for(size_t i = 0; i < vRooms.size(); ++i)
				{
					if(!str_comp(g_Config.m_UcServerJoinSendRoom, vRooms[i].m_aId))
						CurrentAnnouncementTarget = 1 + (int)i;
				}
				static CUi::SDropDownState s_AnnouncementTargetDropDownState;
				static CScrollRegion s_AnnouncementTargetDropDownScrollRegion;
				s_AnnouncementTargetDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_AnnouncementTargetDropDownScrollRegion;
				const int NewAnnouncementTarget = Ui()->DoDropDown(&DropDown, CurrentAnnouncementTarget, vpAnnouncementLabels.data(), (int)vpAnnouncementLabels.size(), s_AnnouncementTargetDropDownState);
				if(NewAnnouncementTarget != CurrentAnnouncementTarget)
				{
					str_copy(g_Config.m_UcServerJoinSendRoom, NewAnnouncementTarget > 0 ? vRooms[NewAnnouncementTarget - 1].m_aId : "");
					ConfigManager()->Save();
				}

				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcServerJoinHideGlobal, Localize("Hide global join/leave messages"), &g_Config.m_UcServerJoinHideGlobal, &Expand, LineSize);
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
		// Room membership is pushed over the presence relay; no need to poll while this page is open.
		const auto &vRooms = GameClient()->m_UClientChatRooms.Rooms();
		CUIRect Page = MainView;
		Page.VMargin(10.0f, &Page);
		Page.HSplitTop(10.0f, nullptr, &Page);

		static CLineInputBuffered<64> s_CreateRoomInput;
		static CLineInputBuffered<40> s_JoinRoomInput;
		static CLineInputBuffered<64> s_RenameRoomInput;
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
				if(GameClient()->m_UClientChatRooms.Create(s_CreateRoomInput.GetString(), Client()->PlayerName()))
					s_CreateRoomInput.Set("");
			}

			CUIRect JoinInput, JoinButton;
			JoinAction.VSplitRight(76.0f, &JoinInput, &JoinButton);
			JoinInput.VSplitRight(8.0f, &JoinInput, nullptr);
			Ui()->DoClearableEditBox(&s_JoinRoomInput, &JoinInput, EditBoxFontSize);
			if(DoButton_Menu(&s_JoinButton, Localize("Join"), 0, &JoinButton) && s_JoinRoomInput.GetString()[0])
			{
				if(GameClient()->m_UClientChatRooms.Join(s_JoinRoomInput.GetString(), Client()->PlayerName()))
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

		static std::vector<CButtonContainer> s_vExpandButtons;
		static std::vector<CButtonContainer> s_vSettingsButtons;
		static std::vector<CButtonContainer> s_vLeaveButtons;
		static std::vector<CButtonContainer> s_vCopyButtons;
		static std::vector<CButtonContainer> s_vRegenerateButtons;
		static std::vector<CButtonContainer> s_vInvitePublicButtons;
		static std::vector<CButtonContainer> s_vSaveSettingsButtons;
		static std::vector<CButtonContainer> s_vCancelSettingsButtons;
		static std::vector<CButtonContainer> s_vMemberMenuButtons;
		static std::vector<CButtonContainer> s_vRoomColorResetButtons;
		static std::map<std::string, bool> s_RoomExpanded;
		static char s_aSettingsRoomId[64] = "";
		static unsigned s_SettingsNameColor = 0;
		static bool s_SettingsInvitePublic = false;
		s_vExpandButtons.resize(vRooms.size());
		s_vSettingsButtons.resize(vRooms.size());
		s_vLeaveButtons.resize(vRooms.size());
		s_vCopyButtons.resize(vRooms.size());
		s_vRegenerateButtons.resize(vRooms.size());
		s_vInvitePublicButtons.resize(vRooms.size());
		s_vSaveSettingsButtons.resize(vRooms.size());
		s_vCancelSettingsButtons.resize(vRooms.size());
		s_vRoomColorResetButtons.resize(vRooms.size());
		size_t MemberCount = 0;
		for(const auto &Room : vRooms)
			MemberCount += Room.m_vMembers.size();
		s_vMemberMenuButtons.resize(MemberCount);
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
			auto ExpandedIt = s_RoomExpanded.find(Room.m_aId);
			if(ExpandedIt == s_RoomExpanded.end())
				ExpandedIt = s_RoomExpanded.emplace(Room.m_aId, true).first;
			bool &Expanded = ExpandedIt->second;

			const bool SettingsMode = !str_comp(s_aSettingsRoomId, Room.m_aId);
			const bool CanManageRoom = Room.m_Owner || Room.m_Admin;
			const bool CanSeeInvite = Room.m_aInviteCode[0] != '\0';
			const char *pRoleLabel = Room.m_Owner ? Localize("Owner") : (Room.m_Admin ? Localize("Admin") : Localize("Member"));

			float RoomHeight = 24.0f;
			if(Expanded)
			{
				RoomHeight += 8.0f;
				if(CanSeeInvite)
					RoomHeight += LineSize + (SettingsMode ? 0.0f : 6.0f);
				if(SettingsMode)
				{
					RoomHeight += 6.0f + LineSize; // rename
					if(Room.m_Owner)
						RoomHeight += ColorPickerLineSpacing + ColorPickerLineSize + ColorPickerLineSpacing; // gap + color
					RoomHeight += 8.0f + LineSize; // save/cancel
				}
				else
				{
					const int MemberRows = maximum(1, (int)Room.m_vMembers.size());
					RoomHeight += LineSize + MemberRows * 24.0f;
				}
			}

			CUIRect Content, Header;
			BeginBlock(Page, RoomHeight, Content);

			Content.HSplitTop(24.0f, &Header, &Content);
			CUIRect HeaderText, LeaveButton, SettingsButton, ExpandIcon;
			Header.VSplitRight(76.0f, &HeaderText, &LeaveButton);
			HeaderText.VSplitRight(8.0f, &HeaderText, nullptr);
			if(CanManageRoom)
			{
				HeaderText.VSplitRight(28.0f, &HeaderText, &SettingsButton);
				HeaderText.VSplitRight(8.0f, &HeaderText, nullptr);
			}
			HeaderText.VSplitRight(20.0f, &HeaderText, &ExpandIcon);
			HeaderText.VSplitRight(6.0f, &HeaderText, nullptr);

			CUIRect ExpandHit = HeaderText;
			ExpandHit.w += ExpandIcon.w + 6.0f;
			if(Ui()->DoButtonLogic(&s_vExpandButtons[RoomIndex], 0, &ExpandHit, BUTTONFLAG_LEFT))
				Expanded = !Expanded;

			{
				SLabelProperties Props;
				Props.SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.65f * Ui()->ButtonColorMul(&s_vExpandButtons[RoomIndex])));
				Props.m_EnableWidthCheck = false;
				TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
				TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
				Ui()->DoLabel(&ExpandIcon, Expanded ? FontIcon::CHEVRON_UP : FontIcon::CHEVRON_DOWN, 14.0f, TEXTALIGN_MR, Props);
				TextRender()->SetRenderFlags(0);
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			}

			char aRoomTitle[160];
			str_format(aRoomTitle, sizeof(aRoomTitle), "%s  ·  %s", Room.m_aName, pRoleLabel);
			Ui()->DoLabel(&HeaderText, aRoomTitle, 14.0f, TEXTALIGN_ML, {.m_MaxWidth = HeaderText.w});

			if(CanManageRoom)
			{
				if(Ui()->DoButton_FontIcon(&s_vSettingsButtons[RoomIndex], FontIcon::GEAR, SettingsMode ? 1 : 0, &SettingsButton, BUTTONFLAG_LEFT))
				{
					if(SettingsMode)
						s_aSettingsRoomId[0] = '\0';
					else
					{
						str_copy(s_aSettingsRoomId, Room.m_aId, sizeof(s_aSettingsRoomId));
						s_RenameRoomInput.Set(Room.m_aName);
						s_SettingsNameColor = Room.m_NameColor != 0 ? Room.m_NameColor : color_cast<ColorHSLA>(CUClientChatRooms::DefaultNameColor()).Pack(false);
						s_SettingsInvitePublic = Room.m_InviteCodePublic;
						Expanded = true;
					}
				}
			}

			if(DoButton_Menu(&s_vLeaveButtons[RoomIndex], Room.m_Owner ? Localize("Delete") : Localize("Leave"), 0, &LeaveButton))
			{
				str_copy(m_aPendingUClientRoomId, Room.m_aId, sizeof(m_aPendingUClientRoomId));
				m_aPendingUClientRoomMemberId[0] = '\0';
				m_PendingUClientRoomAction = EUClientRoomPendingAction::LEAVE_OR_DELETE;
				PopupConfirm(Room.m_Owner ? Localize("Delete room") : Localize("Leave room"),
					Room.m_Owner ? Localize("Are you sure you want to delete this room?") : Localize("Are you sure you want to leave this room?"),
					Room.m_Owner ? Localize("Delete") : Localize("Leave"), Localize("Cancel"),
					&CMenus::PopupConfirmUClientRoomAction);
			}

			if(!Expanded)
			{
				Page.HSplitTop(MarginBetweenSections, nullptr, &Page);
				continue;
			}

			Content.HSplitTop(8.0f, nullptr, &Content);

			if(CanSeeInvite)
			{
				CUIRect InviteRow;
				Content.HSplitTop(LineSize, &InviteRow, &Content);
				CUIRect InviteLabel, InviteCode, CopyButton, RegenerateButton, EyeButton;
				InviteRow.VSplitLeft(80.0f, &InviteLabel, &InviteRow);
				Ui()->DoLabel(&InviteLabel, Localize("Invite code"), 11.0f, TEXTALIGN_ML);
				if(SettingsMode && Room.m_Owner)
				{
					InviteRow.VSplitRight(28.0f, &InviteRow, &EyeButton);
					InviteRow.VSplitRight(8.0f, &InviteRow, nullptr);
					InviteRow.VSplitRight(94.0f, &InviteRow, &RegenerateButton);
					InviteRow.VSplitRight(8.0f, &InviteRow, nullptr);
				}
				InviteRow.VSplitRight(64.0f, &InviteRow, &CopyButton);
				InviteRow.VSplitRight(8.0f, &InviteCode, nullptr);
				InviteCode.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f), IGraphics::CORNER_ALL, 5.0f);
				InviteCode.HMargin(6.0f, &InviteCode);
				Ui()->DoLabel(&InviteCode, Room.m_aInviteCode, 12.0f, TEXTALIGN_ML);
				if(DoButton_Menu(&s_vCopyButtons[RoomIndex], Localize("Copy"), 0, &CopyButton))
					Input()->SetClipboardText(Room.m_aInviteCode);
				if(SettingsMode && Room.m_Owner)
				{
					if(DoButton_Menu(&s_vRegenerateButtons[RoomIndex], Localize("New code"), 0, &RegenerateButton))
						GameClient()->m_UClientChatRooms.RegenerateCode(Room.m_aId);
					if(Ui()->DoButton_FontIcon(&s_vInvitePublicButtons[RoomIndex],
						   s_SettingsInvitePublic ? FontIcon::EYE : FontIcon::EYE_SLASH,
						   s_SettingsInvitePublic ? 1 : 0, &EyeButton, BUTTONFLAG_LEFT))
					{
						s_SettingsInvitePublic = !s_SettingsInvitePublic;
					}
				}
			}

			if(SettingsMode)
			{
				CUIRect RenameRow, ActionRow;
				Content.HSplitTop(6.0f, nullptr, &Content);
				Content.HSplitTop(LineSize, &RenameRow, &Content);
				CUIRect RenameLabel, RenameValue;
				RenameRow.VSplitLeft(80.0f, &RenameLabel, &RenameValue);
				Ui()->DoLabel(&RenameLabel, Localize("Room name"), 11.0f, TEXTALIGN_ML);
				Ui()->DoClearableEditBox(&s_RenameRoomInput, &RenameValue, EditBoxFontSize);

				if(Room.m_Owner)
				{
					// Same spacing as consecutive scoreboard color pickers (Display > HUD).
					Content.HSplitTop(ColorPickerLineSpacing, nullptr, &Content);
					DoLine_ColorPicker(&s_vRoomColorResetButtons[RoomIndex], ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Content,
						Localize("Room color"), &s_SettingsNameColor, CUClientChatRooms::DefaultNameColor(), false);
				}

				Content.HSplitTop(8.0f, nullptr, &Content);
				Content.HSplitTop(LineSize, &ActionRow, &Content);
				CUIRect SaveButton, CancelButton;
				ActionRow.VSplitRight(76.0f, &ActionRow, &SaveButton);
				ActionRow.VSplitRight(8.0f, &ActionRow, nullptr);
				ActionRow.VSplitRight(76.0f, &ActionRow, &CancelButton);
				if(DoButton_Menu(&s_vCancelSettingsButtons[RoomIndex], Localize("Cancel"), 0, &CancelButton))
					s_aSettingsRoomId[0] = '\0';
				if(DoButton_Menu(&s_vSaveSettingsButtons[RoomIndex], Localize("Save"), 0, &SaveButton))
				{
					const char *pNewName = s_RenameRoomInput.GetString();
					const bool NameChanged = pNewName[0] && str_comp(pNewName, Room.m_aName) != 0;
					const unsigned DefaultPacked = color_cast<ColorHSLA>(CUClientChatRooms::DefaultNameColor()).Pack(false);
					const unsigned DesiredColor = s_SettingsNameColor == DefaultPacked ? 0 : s_SettingsNameColor;
					const bool ColorChanged = Room.m_Owner && DesiredColor != Room.m_NameColor;
					const bool InvitePublicChanged = Room.m_Owner && s_SettingsInvitePublic != Room.m_InviteCodePublic;
					if(NameChanged || ColorChanged || InvitePublicChanged)
					{
						GameClient()->m_UClientChatRooms.UpdateSettings(
							Room.m_aId, pNewName, NameChanged, DesiredColor, ColorChanged,
							s_SettingsInvitePublic, InvitePublicChanged);
					}
					s_aSettingsRoomId[0] = '\0';
				}
			}
			else
			{
				if(CanSeeInvite)
					Content.HSplitTop(6.0f, nullptr, &Content);
				CUIRect MembersTitle;
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

						const bool CanPassOwner = Room.m_Owner && !Member.m_Owner && !Member.m_Self;
						const bool CanToggleAdmin = Room.m_Owner && !Member.m_Owner && !Member.m_Self;
						const bool CanKick = !Member.m_Self && !Member.m_Owner &&
							(Room.m_Owner || (Room.m_Admin && !Member.m_Admin));
						if(CanPassOwner || CanToggleAdmin || CanKick)
						{
							CUIRect MenuButton;
							MemberName.VSplitRight(28.0f, &MemberName, &MenuButton);
							MemberName.VSplitRight(8.0f, &MemberName, nullptr);
							if(DoButton_Menu(&s_vMemberMenuButtons[MemberButtonIndex], "...", 0, &MenuButton))
							{
								m_UClientRoomMemberMenu = {};
								m_UClientRoomMemberMenu.m_pMenus = this;
								str_copy(m_UClientRoomMemberMenu.m_aRoomId, Room.m_aId, sizeof(m_UClientRoomMemberMenu.m_aRoomId));
								str_copy(m_UClientRoomMemberMenu.m_aMemberId, Member.m_aId, sizeof(m_UClientRoomMemberMenu.m_aMemberId));
								str_copy(m_UClientRoomMemberMenu.m_aDisplayName, Member.m_aDisplayName, sizeof(m_UClientRoomMemberMenu.m_aDisplayName));
								m_UClientRoomMemberMenu.m_CanPassOwner = CanPassOwner;
								m_UClientRoomMemberMenu.m_CanToggleAdmin = CanToggleAdmin;
								m_UClientRoomMemberMenu.m_TargetIsAdmin = Member.m_Admin;
								m_UClientRoomMemberMenu.m_CanKick = CanKick;
								int OptionCount = (CanPassOwner ? 1 : 0) + (CanToggleAdmin ? 1 : 0) + (CanKick ? 1 : 0);
								const float PopupHeight = 8.0f + OptionCount * 22.0f + maximum(0, OptionCount - 1) * 2.0f;
								Ui()->DoPopupMenu(&m_UClientRoomMemberMenuId, MenuButton.x + MenuButton.w - 160.0f, MenuButton.y + MenuButton.h,
									160.0f, PopupHeight, &m_UClientRoomMemberMenu, PopupUClientRoomMemberMenu);
							}
						}

						CUIRect RoleLabel;
						MemberName.VSplitRight(64.0f, &MemberName, &RoleLabel);
						Ui()->DoLabel(&MemberName, Member.m_aDisplayName, 11.0f, TEXTALIGN_ML, {.m_MaxWidth = MemberName.w});
						TextRender()->TextColor(ColorRGBA(0.7f, 0.7f, 0.75f, 1.0f));
						Ui()->DoLabel(&RoleLabel, Member.m_Owner ? Localize("Owner") : (Member.m_Admin ? Localize("Admin") : Localize("Member")), 10.0f, TEXTALIGN_MR);
						TextRender()->TextColor(TextRender()->DefaultTextColor());
						++MemberButtonIndex;
					}
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

CUi::EPopupMenuFunctionResult CMenus::PopupUClientRoomMemberMenu(void *pContext, CUIRect View, bool Active)
{
	auto *pMenu = static_cast<SUClientRoomMemberMenuContext *>(pContext);
	CMenus *pMenus = pMenu->m_pMenus;
	(void)Active;
	const float RowHeight = 20.0f;
	const float Spacing = 2.0f;
	int ButtonIndex = 0;
	CUIRect Row;

	auto ConfirmAction = [&](EUClientRoomPendingAction Action, const char *pTitle, const char *pMessage, const char *pConfirm) {
		str_copy(pMenus->m_aPendingUClientRoomId, pMenu->m_aRoomId, sizeof(pMenus->m_aPendingUClientRoomId));
		str_copy(pMenus->m_aPendingUClientRoomMemberId, pMenu->m_aMemberId, sizeof(pMenus->m_aPendingUClientRoomMemberId));
		pMenus->m_PendingUClientRoomAction = Action;
		pMenus->PopupConfirm(pTitle, pMessage, pConfirm, Localize("Cancel"), &CMenus::PopupConfirmUClientRoomAction);
	};

	if(pMenu->m_CanPassOwner)
	{
		View.HSplitTop(RowHeight, &Row, &View);
		if(pMenus->DoButton_Menu(&pMenu->m_aButtons[ButtonIndex++], Localize("Pass owner"), 0, &Row))
		{
			ConfirmAction(EUClientRoomPendingAction::PASS_OWNER, Localize("Pass ownership"),
				Localize("Are you sure you want to transfer ownership of this room?"), Localize("Pass owner"));
			return CUi::POPUP_CLOSE_CURRENT;
		}
		View.HSplitTop(Spacing, nullptr, &View);
	}
	if(pMenu->m_CanToggleAdmin)
	{
		View.HSplitTop(RowHeight, &Row, &View);
		if(pMenu->m_TargetIsAdmin)
		{
			if(pMenus->DoButton_Menu(&pMenu->m_aButtons[ButtonIndex++], Localize("Remove admin"), 0, &Row))
			{
				ConfirmAction(EUClientRoomPendingAction::REMOVE_ADMIN, Localize("Remove admin"),
					Localize("Remove admin privileges from this member?"), Localize("Remove"));
				return CUi::POPUP_CLOSE_CURRENT;
			}
		}
		else if(pMenus->DoButton_Menu(&pMenu->m_aButtons[ButtonIndex++], Localize("Give admin"), 0, &Row))
		{
			ConfirmAction(EUClientRoomPendingAction::GIVE_ADMIN, Localize("Give admin"),
				Localize("Grant admin privileges to this member?"), Localize("Give admin"));
			return CUi::POPUP_CLOSE_CURRENT;
		}
		View.HSplitTop(Spacing, nullptr, &View);
	}
	if(pMenu->m_CanKick)
	{
		View.HSplitTop(RowHeight, &Row, &View);
		if(pMenus->DoButton_Menu(&pMenu->m_aButtons[ButtonIndex++], Localize("Kick"), 0, &Row))
		{
			ConfirmAction(EUClientRoomPendingAction::KICK, Localize("Remove member"),
				Localize("Are you sure you want to remove this member?"), Localize("Kick"));
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}
	return CUi::POPUP_KEEP_OPEN;
}

void CMenus::PopupConfirmUClientRoomAction()
{
	if(!m_aPendingUClientRoomId[0])
		return;
	auto &Rooms = GameClient()->m_UClientChatRooms;
	switch(m_PendingUClientRoomAction)
	{
	case EUClientRoomPendingAction::KICK:
		if(m_aPendingUClientRoomMemberId[0])
			Rooms.Kick(m_aPendingUClientRoomId, m_aPendingUClientRoomMemberId);
		break;
	case EUClientRoomPendingAction::PASS_OWNER:
		if(m_aPendingUClientRoomMemberId[0])
			Rooms.TransferOwnership(m_aPendingUClientRoomId, m_aPendingUClientRoomMemberId);
		break;
	case EUClientRoomPendingAction::GIVE_ADMIN:
		if(m_aPendingUClientRoomMemberId[0])
			Rooms.SetMemberAdmin(m_aPendingUClientRoomId, m_aPendingUClientRoomMemberId, true);
		break;
	case EUClientRoomPendingAction::REMOVE_ADMIN:
		if(m_aPendingUClientRoomMemberId[0])
			Rooms.SetMemberAdmin(m_aPendingUClientRoomId, m_aPendingUClientRoomMemberId, false);
		break;
	case EUClientRoomPendingAction::LEAVE_OR_DELETE:
	default:
		Rooms.Leave(m_aPendingUClientRoomId);
		break;
	}
	m_aPendingUClientRoomId[0] = '\0';
	m_aPendingUClientRoomMemberId[0] = '\0';
	m_PendingUClientRoomAction = EUClientRoomPendingAction::LEAVE_OR_DELETE;
}
