#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/shared/localization.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include "../menus.h"

#include <vector>

enum
{
	UCLIENT_TAB_SETTINGS = 0,
	UCLIENT_TAB_CONFIG,
	NUMBER_OF_UCLIENT_TABS
};

using namespace FontIcons;

const float FontSize = 14.0f;
const float EditBoxFontSize = 12.0f;
const float LineSize = 20.0f;
const float ColorPickerLineSize = 25.0f;
const float HeadlineFontSize = 20.0f;
const float StandardFontSize = 14.0f;

const float HeadlineHeight = HeadlineFontSize + 0.0f;
const float Margin = 10.0f;
const float MarginSmall = 5.0f;
const float MarginExtraSmall = 2.5f;
const float MarginBetweenSections = 30.0f;
const float MarginBetweenViews = 30.0f;

static CLineInput s_SkinSwitchNameInput(g_Config.m_ClSkinSwitchSkinName, sizeof(g_Config.m_ClSkinSwitchSkinName));
static CLineInput s_SkinSwitchBodyColorInput(g_Config.m_ClSkinSwitchBodyColor, sizeof(g_Config.m_ClSkinSwitchBodyColor));
static CLineInput s_SkinSwitchFeetColorInput(g_Config.m_ClSkinSwitchFeetColor, sizeof(g_Config.m_ClSkinSwitchFeetColor));
static CLineInput s_UcTranslateApiInput(g_Config.m_UcTranslateApi, sizeof(g_Config.m_UcTranslateApi));
static CLineInput s_UcTranslateKeyInput(g_Config.m_UcTranslateKey, sizeof(g_Config.m_UcTranslateKey));
static CLineInput s_UcTranslateTargetInput(g_Config.m_UcTranslateTarget, sizeof(g_Config.m_UcTranslateTarget));
static CLineInput s_TagReplyMessageInput(g_Config.m_ClTagReplyMessage, sizeof(g_Config.m_ClTagReplyMessage));
static CLineInput s_TagReplyWebhookInput(g_Config.m_ClTagDiscordWebHookUrl, sizeof(g_Config.m_ClTagDiscordWebHookUrl));
static CLineInput s_ChatWebhookInput(g_Config.m_ClChatDiscordWebHookUrl, sizeof(g_Config.m_ClChatDiscordWebHookUrl));
static CLineInput s_ChatSkinMessageInput(g_Config.m_ClChatSkinMessage, sizeof(g_Config.m_ClChatSkinMessage));
static CLineInput s_UcTranslateBackendInput(g_Config.m_UcTranslateBackend, sizeof(g_Config.m_UcTranslateBackend));
void CMenus::RenderSettingsUClientConfig(CUIRect MainView)
{
	static CScrollRegion s_ScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams Params;
	Params.m_ScrollUnit = 80.0f;
	Params.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	Params.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollOffset, &Params);

	MainView.y += ScrollOffset.y;
	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);

	static CUi::SDropDownState s_RichPresenceDropDownState;
	static CScrollRegion s_RichPresenceDropDownScrollRegion;
	s_RichPresenceDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_RichPresenceDropDownScrollRegion;

	static CButtonContainer s_SegmentedButtons[32];

	const float kSegmentWidth = 180.0f;

	auto DoSegmentedSelector = [&](CUIRect Rect, const char *const *ppOptions, int Count, int &Value, int BaseId) {
		if(Rect.w > kSegmentWidth)
		{
			CUIRect Left;
			Rect.VSplitRight(kSegmentWidth, &Left, &Rect);
		}

		CUIRect Button;
		float Remaining = Rect.w;
		for(int i = 0; i < Count; ++i)
		{
			float Segment = (i == Count - 1) ? Remaining : Rect.w / Count;
			Rect.VSplitLeft(Segment, &Button, &Rect);
			Remaining -= Segment;
			int Corners = i == 0 ? IGraphics::CORNER_L : i == Count - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE;
			ColorRGBA DefaultColor(0.4f, 0.4f, 0.4f, 0.5f);
			ColorRGBA ActiveColor(0.9f, 0.9f, 0.9f, 0.7f);
			ColorRGBA HoverColor(0.6f, 0.6f, 0.6f, 0.6f);
			if(DoButton_MenuTab(&s_SegmentedButtons[BaseId * 4 + i], ppOptions[i], Value == i, &Button, Corners, nullptr, &DefaultColor, &ActiveColor, &HoverColor, 4.0f))
				Value = i;
		}
	};

	auto RenderCard = [&](float Height, const char *pCommandName, const char *pDesc, auto &&Controls) {
		CUIRect Card;
		MainView.HSplitTop(Height, &Card, &MainView);
		MainView.HSplitTop(Margin, nullptr, &MainView);
		const bool Visible = s_ScrollRegion.AddRect(Card);
		if(!Visible)
			return;
		Card.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);

		CUIRect Inner;
		Card.Margin(10.0f, &Inner);
		CUIRect Left, Right;
		Inner.VSplitLeft(Inner.w * 0.45f, &Left, &Right);
		CUIRect TitleRect, DescRect;
		Left.HSplitTop(HeadlineHeight, &TitleRect, &Left);
		Ui()->DoLabel(&TitleRect, pCommandName, HeadlineFontSize, TEXTALIGN_ML);
		Left.HSplitTop(StandardFontSize + 4.0f, &DescRect, &Left);
		Ui()->DoLabel(&DescRect, pDesc, StandardFontSize, TEXTALIGN_ML);
		Controls(Right);
	};

	RenderCard(100.0f, "uc_tag_discord_webhook_url", Localize("Sends a message to the Discord webhook when you’re tagged."), [&](CUIRect &Controls) {
		CUIRect Row;
		Controls.HSplitTop(LineSize, &Row, &Controls);
		s_TagReplyWebhookInput.SetEmptyText(Localize("Webhook URL"));
		DoEditBoxWithLabel(&s_TagReplyWebhookInput, &Row, Localize("Value"), "", g_Config.m_ClTagDiscordWebHookUrl, sizeof(g_Config.m_ClTagDiscordWebHookUrl));
	});

	RenderCard(100.0f, "uc_chat_skin_message", Localize("It switches to the sender’s skin when a specific language is detected."), [&](CUIRect &Controls) {
		CUIRect Row;
		Controls.HSplitTop(LineSize, &Row, &Controls);
		s_ChatSkinMessageInput.SetEmptyText("uc_chat_skin_message");
		DoEditBoxWithLabel(&s_ChatSkinMessageInput, &Row, Localize("Value"), "uc_chat_skin_message", g_Config.m_ClChatSkinMessage, sizeof(g_Config.m_ClChatSkinMessage));
	});

	RenderCard(100.0f, "uc_chat_discord_webhook_url", Localize("Sends every chat message to the Discord webhook."), [&](CUIRect &Controls) {
		CUIRect Row;
		Controls.HSplitTop(LineSize, &Row, &Controls);
		s_ChatWebhookInput.SetEmptyText(Localize("Webhook URL"));
		DoEditBoxWithLabel(&s_ChatWebhookInput, &Row, Localize("Value"), "", g_Config.m_ClChatDiscordWebHookUrl, sizeof(g_Config.m_ClChatDiscordWebHookUrl));
	});

	RenderCard(100.0f, "uc_skin_switch_skin_name", Localize("Enter the name of the skin you often use."), [&](CUIRect &Controls) {
		CUIRect Row;
		Controls.HSplitTop(LineSize, &Row, &Controls);
		s_SkinSwitchNameInput.SetEmptyText(Localize("Skin name"));
		DoEditBoxWithLabel(&s_SkinSwitchNameInput, &Row, Localize("Value"), "default", g_Config.m_ClSkinSwitchSkinName, sizeof(g_Config.m_ClSkinSwitchSkinName));
	});

	RenderCard(100.0f, "uc_skin_switch_body_color", Localize("Enter the body color of the skin you often use."), [&](CUIRect &Controls) {
		CUIRect Row;
		Controls.HSplitTop(LineSize, &Row, &Controls);
		s_SkinSwitchBodyColorInput.SetEmptyText("0");
		DoEditBoxWithLabel(&s_SkinSwitchBodyColorInput, &Row, Localize("Value"), "", g_Config.m_ClSkinSwitchBodyColor, sizeof(g_Config.m_ClSkinSwitchBodyColor));
	});

	RenderCard(100.0f, "uc_skin_switch_feet_color", Localize("Enter the feet color of the skin you often use."), [&](CUIRect &Controls) {
		CUIRect Row;
		Controls.HSplitTop(LineSize, &Row, &Controls);
		s_SkinSwitchFeetColorInput.SetEmptyText("0");
		DoEditBoxWithLabel(&s_SkinSwitchFeetColorInput, &Row, Localize("Value"), "", g_Config.m_ClSkinSwitchFeetColor, sizeof(g_Config.m_ClSkinSwitchFeetColor));
	});

	RenderCard(100.0f, "uc_skin_switch_use_custom_colors", Localize("Set this to 1 if you plan to use custom colors."), [&](CUIRect &Controls) {
		const char *apUseColor[] = {"0", "1"};
		CUIRect Row;
		Controls.HSplitTop(LineSize, &Row, &Controls);
		int UseColor = g_Config.m_UcSkinSwitchUseCustomColors ? 1 : 0;
		DoSegmentedSelector(Row, apUseColor, 2, UseColor, 1);
		g_Config.m_UcSkinSwitchUseCustomColors = UseColor;
	});

	RenderCard(100.0f, "uc_rich_presence_image", Localize("It sets the Discord Game activity image."), [&](CUIRect &Controls) {
		CUIRect Row;
		Controls.HSplitTop(LineSize, &Row, &Controls);
			Row.VSplitRight(150.0f, &Row, nullptr);
		const char *apPresence[] = {"0", "1", "2", "3"};
		g_Config.m_UcRichPresenceImage = Ui()->DoDropDown(&Row, g_Config.m_UcRichPresenceImage, apPresence, std::size(apPresence), s_RichPresenceDropDownState);
	});

	RenderCard(100.0f, "uc_translate", Localize("It sends your chat messages in their translated form when you send them."), [&](CUIRect &Controls) {
		const char *apAuto[] = {"0", "1"};
		CUIRect Row;
		Controls.HSplitTop(LineSize, &Row, &Controls);
		int AutoIndex = g_Config.m_UcTranslate ? 1 : 0;
		DoSegmentedSelector(Row, apAuto, 2, AutoIndex, 3);
		g_Config.m_UcTranslate = AutoIndex;
	});

	RenderCard(100.0f, "uc_translate_backend", Localize("Specify which translator to use (google, deepl)."), [&](CUIRect &Controls) {
		CUIRect Row;
		Controls.HSplitTop(LineSize, &Row, &Controls);
		s_UcTranslateBackendInput.SetEmptyText("google");
		DoEditBoxWithLabel(&s_UcTranslateBackendInput, &Row, Localize("Value"), "google", g_Config.m_UcTranslateBackend, sizeof(g_Config.m_UcTranslateBackend));
	});

	RenderCard(100.0f, "uc_translate_target", Localize("Choose the language you want to translate into (google: ISO 639, DeepL: ISO 639-1 + ISO 15924)."), [&](CUIRect &Controls) {
		CUIRect Row;
		Controls.HSplitTop(LineSize, &Row, &Controls);
		s_UcTranslateTargetInput.SetEmptyText("en");
		DoEditBoxWithLabel(&s_UcTranslateTargetInput, &Row, Localize("Value"), "en", g_Config.m_UcTranslateTarget, sizeof(g_Config.m_UcTranslateTarget));
	});

	RenderCard(100.0f, "uc_translate_api", Localize("Enter the API address that matches the translator selected for the target."), [&](CUIRect &Controls) {
		CUIRect Row;
		Controls.HSplitTop(LineSize, &Row, &Controls);
		s_UcTranslateApiInput.SetEmptyText(Localize("API address"));
		DoEditBoxWithLabel(&s_UcTranslateApiInput, &Row, Localize("Value"), "", g_Config.m_UcTranslateApi, sizeof(g_Config.m_UcTranslateApi));
	});

	RenderCard(100.0f, "uc_translate_key", Localize("Enter the API key."), [&](CUIRect &Controls) {
		CUIRect Row;
		Controls.HSplitTop(LineSize, &Row, &Controls);
		s_UcTranslateKeyInput.SetEmptyText(Localize("API Key"));
		DoEditBoxWithLabel(&s_UcTranslateKeyInput, &Row, Localize("Value"), "", g_Config.m_UcTranslateKey, sizeof(g_Config.m_UcTranslateKey));
	});

	s_ScrollRegion.End();
}

void CMenus::RenderSettingsUClient(CUIRect MainView)
{
	static int s_CurUClientTab = UCLIENT_TAB_SETTINGS;

	CUIRect TabBar, Button;
	MainView.HSplitTop(LineSize, &TabBar, &MainView);

	const float TabWidth = TabBar.w / NUMBER_OF_UCLIENT_TABS;
	static CButtonContainer s_aTabs[NUMBER_OF_UCLIENT_TABS];
	const char *apTabNames[] = {
		Localize("Settings"),
		Localize("Config")};

	for(int Tab = 0; Tab < NUMBER_OF_UCLIENT_TABS; ++Tab)
	{
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == 0 ? IGraphics::CORNER_L : Tab == NUMBER_OF_UCLIENT_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE;
		if(DoButton_MenuTab(&s_aTabs[Tab], apTabNames[Tab], s_CurUClientTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			s_CurUClientTab = Tab;
	}

	MainView.HSplitTop(Margin, nullptr, &MainView);

	if(s_CurUClientTab == UCLIENT_TAB_CONFIG)
	{
		RenderSettingsUClientConfig(MainView);
		return;
	}

	CUIRect Label, Checkbox, EditBox;
	MainView.HSplitTop(MarginBetweenSections, &Label, &MainView);

	// Auto Reply //
	Ui()->DoLabel(&Label, Localize("Auto Reply"), HeadlineFontSize, TEXTALIGN_ML);

	CUIRect TagReply;
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClTagReply, Localize("Automatic reply when tagged."), &g_Config.m_ClTagReply, &MainView, LineSize);

	const float AutoReplyPreviewHeight = LineSize;
	const float AutoReplyBlockHeight = LineSize + MarginExtraSmall + AutoReplyPreviewHeight;
	MainView.HSplitTop(AutoReplyBlockHeight, &TagReply, &MainView);

	MainView.HSplitTop(LineSize, nullptr, &MainView);
	if(g_Config.m_ClTagReply)
	{
		TagReply.HSplitTop(MarginExtraSmall, nullptr, &TagReply);
		static CLineInput l_TagReply(g_Config.m_ClTagReplyMessage, sizeof(g_Config.m_ClTagReplyMessage));
		l_TagReply.SetEmptyText(Localize("Enter the message to send as an auto-reply."));
		CUIRect EditRect, PreviewBlock;
		TagReply.HSplitTop(LineSize, &EditRect, &PreviewBlock);
		Ui()->DoEditBox(&l_TagReply, &EditRect, EditBoxFontSize);

		char aAutoMessage[128];
		if(str_comp(g_Config.m_ClLanguagefile, "languages/korean.txt") == 0)
			str_copy(aAutoMessage, "이 메세지는 자동 응답이에요!", sizeof(aAutoMessage));
		else if(str_comp(g_Config.m_ClLanguagefile, "languages/simplified_chinese.txt") == 0)
			str_copy(aAutoMessage, "此消息为自动回复", sizeof(aAutoMessage));
		else if(str_comp(g_Config.m_ClLanguagefile, "languages/traditional_chinese.txt") == 0)
			str_copy(aAutoMessage, "此訊息為自動回覆", sizeof(aAutoMessage));
		else
			str_copy(aAutoMessage, "This message is an auto-reply!", sizeof(aAutoMessage));

		const char *pUserAutoReply = g_Config.m_ClTagReplyMessage[0] ? g_Config.m_ClTagReplyMessage : Localize("(None)");

		PreviewBlock.HSplitTop(MarginExtraSmall, nullptr, &PreviewBlock);
		CUIRect PreviewRect;
		PreviewBlock.HSplitTop(LineSize, &PreviewRect, nullptr);
		char aPreview[256];
		str_format(aPreview, sizeof(aPreview), "%s: %s - %s", Localize("Preview"), pUserAutoReply, aAutoMessage);
		Ui()->DoLabel(&PreviewRect, aPreview, EditBoxFontSize, TEXTALIGN_TL);
	}

	MainView.HSplitTop(MarginBetweenSections, &Label, &MainView);

	// Skin Switch //
	CUIRect SkinLabel = Label, FillButton;
	SkinLabel.VSplitRight(180.0f, &SkinLabel, &FillButton);
	SkinLabel.HSplitTop(HeadlineHeight, &SkinLabel, nullptr);
	FillButton.HSplitTop(HeadlineHeight, &FillButton, nullptr);
	FillButton.VSplitLeft(10.0f, nullptr, &FillButton);
	Ui()->DoLabel(&SkinLabel, Localize("Skin Switch"), HeadlineFontSize, TEXTALIGN_ML);
	static CButtonContainer s_FillSkinButton;
	if(DoButton_Menu(&s_FillSkinButton, Localize("Use current skin"), 0, &FillButton))
	{
		const char *pSkinName = g_Config.m_ClPlayerSkin;
		const int BodyColor = g_Config.m_ClPlayerColorBody;
		const int FeetColor = g_Config.m_ClPlayerColorFeet;

		str_copy(g_Config.m_ClSkinSwitchSkinName, pSkinName, sizeof(g_Config.m_ClSkinSwitchSkinName));
		str_format(g_Config.m_ClSkinSwitchBodyColor, sizeof(g_Config.m_ClSkinSwitchBodyColor), "%d", BodyColor);
		str_format(g_Config.m_ClSkinSwitchFeetColor, sizeof(g_Config.m_ClSkinSwitchFeetColor), "%d", FeetColor);
	}

	float infoH = EditBoxFontSize;
	float labelH = EditBoxFontSize;
	float editH = LineSize;
	float vGap = Margin;
	float totalH = infoH + vGap + labelH + editH;
	CUIRect section;
	MainView.HSplitTop(totalH, &section, &MainView);

	CUIRect infoRect;
	section.HSplitTop(infoH, &infoRect, &section);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf),
		Localize("When you enter /skin %s, it reverts to the skin you have set."),
		g_Config.m_PlayerName);
	Ui()->DoLabel(&infoRect, aBuf, EditBoxFontSize, TEXTALIGN_TL);

	section.HSplitTop(MarginSmall, nullptr, &section);
	CUIRect CustomColorRect;
	section.HSplitTop(LineSize, &CustomColorRect, &section);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_UcSkinSwitchUseCustomColors, Localize("Use custom colors"), &g_Config.m_UcSkinSwitchUseCustomColors, &CustomColorRect, LineSize);

	section.HSplitTop(vGap, nullptr, &section);

	float colW = (section.w - MarginSmall * 2) / 3.0f;
	CUIRect cName, cBody, cFeet;
	section.VSplitLeft(colW, &cName, &section);
	section.VSplitLeft(MarginSmall, nullptr, &section);
	section.VSplitLeft(colW, &cBody, &section);
	section.VSplitLeft(MarginSmall, nullptr, &section);
	cFeet = section;

	{
		CUIRect rLabel, rEdit;
		cName.HSplitTop(labelH, &rLabel, &rEdit);
		Ui()->DoLabel(&rLabel, Localize("Skin Name"), EditBoxFontSize, TEXTALIGN_TL);
		rEdit.HSplitTop(editH, &rEdit, nullptr);
		s_SkinSwitchNameInput.SetEmptyText(Localize("Please enter the skin name."));
		Ui()->DoEditBox(&s_SkinSwitchNameInput, &rEdit, EditBoxFontSize);
	}

	{
		CUIRect rLabel, rEdit;
		cBody.HSplitTop(labelH, &rLabel, &rEdit);
		Ui()->DoLabel(&rLabel, Localize("Body color"), EditBoxFontSize, TEXTALIGN_TL);
		rEdit.HSplitTop(editH, &rEdit, nullptr);

		s_SkinSwitchBodyColorInput.SetEmptyText("Please enter the body color code.");
		Ui()->DoEditBox(&s_SkinSwitchBodyColorInput, &rEdit, EditBoxFontSize);
	}

	{
		CUIRect rLabel, rEdit;
		cFeet.HSplitTop(labelH, &rLabel, &rEdit);
		Ui()->DoLabel(&rLabel, Localize("Feet color"), EditBoxFontSize, TEXTALIGN_TL);
		rEdit.HSplitTop(editH, &rEdit, nullptr);

		s_SkinSwitchFeetColorInput.SetEmptyText("Please enter the feet color code.");
		Ui()->DoEditBox(&s_SkinSwitchFeetColorInput, &rEdit, EditBoxFontSize);
	}
}
