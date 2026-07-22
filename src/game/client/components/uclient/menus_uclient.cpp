#include <base/math.h>
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/bc_ui_animations.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
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

void CMenus::RenderSettingsUClient(CUIRect MainView)
{
	enum
	{
		UCLIENT_TAB_GAMEPLAY = 0,
		UCLIENT_TAB_OTHERS,
		NUM_UCLIENT_TABS,
	};

	static int s_CurTab = UCLIENT_TAB_GAMEPLAY;
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
	};
	const float TabWidth = TabBar.w / (float)NUM_UCLIENT_TABS;
	for(int Tab = 0; Tab < NUM_UCLIENT_TABS; ++Tab)
	{
		TabBar.VSplitLeft(TabWidth, &TabButton, &TabBar);
		const int Corners = Tab == 0 ? IGraphics::CORNER_L : (Tab == NUM_UCLIENT_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurTab == Tab, &TabButton, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			s_CurTab = Tab;
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);

	static CScrollRegion s_UClientScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_UClientScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);

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
			const float ExpandedTargetHeight = MarginSmall + LineSize + MarginSmall + ColorPickerLineSize + ColorPickerLineSpacing + LineSize * 7.0f;
			const float ExpandedHeight = ExpandedTargetHeight * s_BackPhase;
			const float ContentHeight = LineSize + MarginSmall + LineSize + ExpandedHeight;
			CUIRect Content, Label, Button, Visible;
			BeginBlock(Column, ContentHeight, Content);
			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, Localize("Back notify"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcNotifyWhenBack, Localize("Show when a teammate behind you freezes"), &g_Config.m_UcNotifyWhenBack, &Content, LineSize);
			if(ExpandedHeight > 0.0f)
			{
				Content.HSplitTop(ExpandedHeight, &Visible, &Content);
				Ui()->ClipEnable(&Visible);
				struct SScopedClip
				{
					CUi *m_pUi;
					~SScopedClip() { m_pUi->ClipDisable(); }
				} ClipGuard{Ui()};

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);

				CUIRect TextRow, TextLabel;
				Expand.HSplitTop(LineSize, &TextRow, &Expand);
				TextRow.VSplitLeft(40.0f, &TextLabel, &TextRow);
				Ui()->DoLabel(&TextLabel, Localize("Text"), 12.0f, TEXTALIGN_ML);
				static CLineInput s_BackInput(g_Config.m_UcNotifyWhenBackText, sizeof(g_Config.m_UcNotifyWhenBackText));
				s_BackInput.SetEmptyText("Back");
				Ui()->DoEditBox(&s_BackInput, &TextRow, EditBoxFontSize);

				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				static CButtonContainer s_NotifyWhenBackColor;
				DoLine_ColorPicker(&s_NotifyWhenBackColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Expand, Localize("Color"), &g_Config.m_UcNotifyWhenBackColor, ColorRGBA(1.0f, 0.55f, 0.15f), false);

				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_UcNotifyWhenBackMaxDistance, &g_Config.m_UcNotifyWhenBackMaxDistance, &Button, Localize("Max distance"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcNotifyWhenBackIncludeSpec, Localize("Also show players in /spec"), &g_Config.m_UcNotifyWhenBackIncludeSpec, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcNotifyWhenBackShowCount, Localize("Show frozen user count"), &g_Config.m_UcNotifyWhenBackShowCount, &Expand, LineSize);
				DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcNotifyWhenBackShowNames, Localize("Show frozen user names"), &g_Config.m_UcNotifyWhenBackShowNames, &Expand, LineSize);
				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_UcNotifyWhenBackX, &g_Config.m_UcNotifyWhenBackX, &Button, Localize("Horizontal Position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_UcNotifyWhenBackY, &g_Config.m_UcNotifyWhenBackY, &Button, Localize("Vertical Position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
				Expand.HSplitTop(LineSize, &Button, &Expand);
				Ui()->DoScrollbarOption(&g_Config.m_UcNotifyWhenBackSize, &g_Config.m_UcNotifyWhenBackSize, &Button, Localize("Font Size"), 1, 50);
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		// Timeout reconnect
		{
			const float ContentHeight = LineSize + MarginSmall + LineSize;
			CUIRect Content, Label;
			BeginBlock(Column, ContentHeight, Content);
			Content.HSplitTop(LineSize, &Label, &Content);
			Ui()->DoLabel(&Label, Localize("Reconnect timeout"), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcShowTimeoutReconnect, Localize("Show remaining reconnect timeout"), &g_Config.m_UcShowTimeoutReconnect, &Content, LineSize);
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

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(CodeBoxHeight, &Row, &Expand);
				static CLineInput s_AutoLoginJapanCode(g_Config.m_UcAutoLoginJapanCode, sizeof(g_Config.m_UcAutoLoginJapanCode));
				s_AutoLoginJapanCode.SetEmptyText("Enter Japan server login code");
				Ui()->DoClearableEditBox(&s_AutoLoginJapanCode, &Row, 14.0f);
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

				CUIRect Expand = {Visible.x, Visible.y, Visible.w, ExpandedTargetHeight};
				Expand.HSplitTop(MarginSmall, nullptr, &Expand);
				Expand.HSplitTop(CodeBoxHeight, &Row, &Expand);
				static CLineInput s_AutoLoginKogCode(g_Config.m_UcAutoLoginKogCode, sizeof(g_Config.m_UcAutoLoginKogCode));
				s_AutoLoginKogCode.SetEmptyText("Enter KoG server login code");
				Ui()->DoClearableEditBox(&s_AutoLoginKogCode, &Row, 14.0f);
			}
			Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
		}

		RightColumnEndY = Column.y;
	}
	else // UCLIENT_TAB_OTHERS
	{
		CUIRect Column = LeftView;
		Column.HSplitTop(10.0f, nullptr, &Column);

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

	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = maximum(LeftColumnEndY, RightColumnEndY) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_UClientScrollRegion.AddRect(ScrollRegion);
	s_UClientScrollRegion.End();
}
