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
#include <engine/updater.h>

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

void CMenus::RenderSettingsUClient(CUIRect MainView)
{
	CUIRect Label, Checkbox, EditBox;
	MainView.HSplitTop(MarginBetweenSections, &Label, &MainView);

	// Auto Reply //
	Ui()->DoLabel(&Label, Localize("Auto Reply"), HeadlineFontSize, TEXTALIGN_ML);

	CUIRect TagReply;
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClTagReply, Localize("태그 되었을 때 자동 응답"), &g_Config.m_ClTagReply, &MainView, LineSize);

	MainView.HSplitTop(LineSize + MarginExtraSmall, &TagReply, &MainView);
	if(g_Config.m_ClTagReply)
	{
		TagReply.HSplitTop(MarginExtraSmall, nullptr, &TagReply);
		static CLineInput l_TagReply(g_Config.m_ClTagReplyMessage, sizeof(g_Config.m_ClTagReplyMessage));
		l_TagReply.SetEmptyText("자동 응답을 보낼 메세지를 입력하세요.");
		Ui()->DoEditBox(&l_TagReply, &TagReply, EditBoxFontSize);
	}

	MainView.HSplitTop(MarginBetweenSections, &Label, &MainView);

	// Skin Switch //
	Ui()->DoLabel(&Label, Localize("Skin Switch"), HeadlineFontSize, TEXTALIGN_ML);

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
		"/skin %s 를(을) 입력 시 본인이 설정한 스킨으로 되돌아옵니다.",
		g_Config.m_PlayerName);
	Ui()->DoLabel(&infoRect, aBuf, EditBoxFontSize, TEXTALIGN_TL);

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
		Ui()->DoLabel(&rLabel, Localize("스킨 이름"), EditBoxFontSize, TEXTALIGN_TL);
		rEdit.HSplitTop(editH, &rEdit, nullptr);
		static CLineInput l_SkinName(g_Config.m_ClSkinSwitchSkinName,
			sizeof(g_Config.m_ClSkinSwitchSkinName));
		l_SkinName.SetEmptyText("스킨 이름을 입력해 주세요.");
		Ui()->DoEditBox(&l_SkinName, &rEdit, EditBoxFontSize);
	}

	{
		CUIRect rLabel, rEdit;
		cBody.HSplitTop(labelH, &rLabel, &rEdit);
		Ui()->DoLabel(&rLabel, Localize("몸 색"), EditBoxFontSize, TEXTALIGN_TL);
		rEdit.HSplitTop(editH, &rEdit, nullptr);

		static CLineInput l_BodyCol(g_Config.m_ClSkinSwitchBodyColor, sizeof(g_Config.m_ClSkinSwitchBodyColor));
		l_BodyCol.SetEmptyText("몸 색 코드를 입력해 주세요.");
		Ui()->DoEditBox(&l_BodyCol, &rEdit, EditBoxFontSize);
	}

	{
		CUIRect rLabel, rEdit;
		cFeet.HSplitTop(labelH, &rLabel, &rEdit);
		Ui()->DoLabel(&rLabel, Localize("발 색"), EditBoxFontSize, TEXTALIGN_TL);
		rEdit.HSplitTop(editH, &rEdit, nullptr);

		static CLineInput l_FeetCol(g_Config.m_ClSkinSwitchFeetColor, sizeof(g_Config.m_ClSkinSwitchFeetColor));
		l_FeetCol.SetEmptyText("발 색 코드를 입력해 주세요.");
		Ui()->DoEditBox(&l_FeetCol, &rEdit, EditBoxFontSize);
	}
}