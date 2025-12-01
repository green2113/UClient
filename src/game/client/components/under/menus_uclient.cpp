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

void CMenus::RenderSettingsUClient(CUIRect MainView)
{
	CUIRect Label, Checkbox, EditBox;
	MainView.HSplitTop(MarginBetweenSections, &Label, &MainView);

	// Auto Reply //
	Ui()->DoLabel(&Label, Localize("Auto Reply"), HeadlineFontSize, TEXTALIGN_ML);

	CUIRect TagReply;
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClTagReply, Localize("Automatic reply when tagged."), &g_Config.m_ClTagReply, &MainView, LineSize);

	MainView.HSplitTop(LineSize + MarginExtraSmall, &TagReply, &MainView);

	MainView.HSplitTop(LineSize, nullptr, &MainView);
	if(g_Config.m_ClTagReply)
	{
		TagReply.HSplitTop(MarginExtraSmall, nullptr, &TagReply);
		static CLineInput l_TagReply(g_Config.m_ClTagReplyMessage, sizeof(g_Config.m_ClTagReplyMessage));
		l_TagReply.SetEmptyText(Localize("Enter the message to send as an auto-reply."));
		Ui()->DoEditBox(&l_TagReply, &TagReply, EditBoxFontSize);
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
