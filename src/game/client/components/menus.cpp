/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "menus.h"

#include <base/color.h>
#include <base/log.h>
#include <base/math.h>
#include <base/process.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/client/updater.h>
#include <engine/config.h>
#include <engine/editor.h>
#include <engine/font_icons.h>
#include <engine/friends.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/uclient_launch_gate.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/bc_ui_animations.h>
#include <game/client/components/binds.h>
#include <game/client/components/console.h>
#include <game/client/components/key_binder.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std::chrono_literals;

ColorRGBA CMenus::ms_GuiColor;
ColorRGBA CMenus::ms_ColorTabbarInactiveOutgame;
ColorRGBA CMenus::ms_ColorTabbarActiveOutgame;
ColorRGBA CMenus::ms_ColorTabbarHoverOutgame;
ColorRGBA CMenus::ms_ColorTabbarInactive;
ColorRGBA CMenus::ms_ColorTabbarActive = ColorRGBA(0, 0, 0, 0.5f);
ColorRGBA CMenus::ms_ColorTabbarHover;
ColorRGBA CMenus::ms_ColorTabbarInactiveIngame;
ColorRGBA CMenus::ms_ColorTabbarActiveIngame;
ColorRGBA CMenus::ms_ColorTabbarHoverIngame;

float CMenus::ms_ButtonHeight = 25.0f;
float CMenus::ms_ListheaderHeight = 17.0f;

CMenus::CMenus()
{
	m_Popup = POPUP_NONE;
	m_MenuPage = 0;
	m_GamePage = PAGE_GAME;

	m_NeedRestartGraphics = false;
	m_NeedRestartSound = false;
	m_NeedSendinfo = false;
	m_NeedSendDummyinfo = false;
	m_MenuActive = true;
	m_ShowStart = true;

	str_copy(m_aCurrentDemoFolder, "demos");
	m_DemolistStorageType = IStorage::TYPE_ALL;

	m_DemoPlayerState = DEMOPLAYER_NONE;
	m_Dummy = false;

	for(SUIAnimator &Animator : m_aAnimatorsSettingsTab)
	{
		Animator.m_YOffset = -2.5f;
		Animator.m_HOffset = 5.0f;
		Animator.m_WOffset = 5.0f;
		Animator.m_RepositionLabel = true;
	}

	for(SUIAnimator &Animator : m_aAnimatorsBigPage)
	{
		Animator.m_YOffset = -5.0f;
		Animator.m_HOffset = 5.0f;
	}

	for(SUIAnimator &Animator : m_aAnimatorsSmallPage)
	{
		Animator.m_YOffset = -2.5f;
		Animator.m_HOffset = 2.5f;
	}

	m_PasswordInput.SetBuffer(g_Config.m_Password, sizeof(g_Config.m_Password));
	m_PasswordInput.SetHidden(true);
}

int CMenus::DoButton_Toggle(const void *pId, int Checked, const CUIRect *pRect, bool Active, const unsigned Flags)
{
	Graphics()->TextureSet(g_pData->m_aImages[IMAGE_GUIBUTTONS].m_Id);
	Graphics()->QuadsBegin();
	if(!Active)
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.5f);
	Graphics()->SelectSprite(Checked ? SPRITE_GUIBUTTON_ON : SPRITE_GUIBUTTON_OFF);
	IGraphics::CQuadItem QuadItem(pRect->x, pRect->y, pRect->w, pRect->h);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	if(Ui()->HotItem() == pId && Active)
	{
		Graphics()->SelectSprite(SPRITE_GUIBUTTON_HOVER);
		QuadItem = IGraphics::CQuadItem(pRect->x, pRect->y, pRect->w, pRect->h);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
	}
	Graphics()->QuadsEnd();

	return Active ? Ui()->DoButtonLogic(pId, Checked, pRect, Flags) : 0;
}

int CMenus::DoButton_Menu(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, const unsigned Flags, const char *pImageName, int Corners, float Rounding, float FontFactor, ColorRGBA Color)
{
	CUIRect Text = *pRect;

	if(Checked)
		Color = ColorRGBA(0.6f, 0.6f, 0.6f, 0.5f);
	else // TClient, why was this not here? ig they never use "checked" anywhere important
		Color.a *= Ui()->ButtonColorMul(pButtonContainer);

	pRect->Draw(Color, Corners, Rounding);

	if(pImageName)
	{
		CUIRect Image;
		pRect->VSplitRight(pRect->h * 4.0f, &Text, &Image); // always correct ratio for image

		// render image
		const CMenuImage *pImage = FindMenuImage(pImageName);
		if(pImage)
		{
			Graphics()->TextureSet(Ui()->HotItem() == pButtonContainer ? pImage->m_OrgTexture : pImage->m_GreyTexture);
			Graphics()->WrapClamp();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			IGraphics::CQuadItem QuadItem(Image.x, Image.y, Image.w, Image.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		}
	}

	Text.HMargin(pRect->h >= 20.0f ? 2.0f : 1.0f, &Text);
	Text.HMargin((Text.h * FontFactor) / 2.0f, &Text);
	Ui()->DoLabel(&Text, pText, Text.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, Flags);
}

int CMenus::DoButton_MenuEx(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, const unsigned Flags, const char *pImageName, int Corners, float Rounding, float FontFactor, ColorRGBA Color, bool AlwaysColoredImage)
{
	CUIRect Text = *pRect;

	if(Checked)
		Color = ColorRGBA(0.6f, 0.6f, 0.6f, 0.5f);
	else
		Color.a *= Ui()->ButtonColorMul(pButtonContainer);

	pRect->Draw(Color, Corners, Rounding);

	if(pImageName)
	{
		CUIRect Image;
		pRect->VSplitRight(pRect->h * 4.0f, &Text, &Image);

		const CMenuImage *pImage = FindMenuImage(pImageName);
		if(pImage)
		{
			Graphics()->TextureSet((AlwaysColoredImage || Ui()->HotItem() == pButtonContainer) ? pImage->m_OrgTexture : pImage->m_GreyTexture);
			Graphics()->WrapClamp();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			IGraphics::CQuadItem QuadItem(Image.x, Image.y, Image.w, Image.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		}
	}

	Text.HMargin(pRect->h >= 20.0f ? 2.0f : 1.0f, &Text);
	Text.HMargin((Text.h * FontFactor) / 2.0f, &Text);
	Ui()->DoLabel(&Text, pText, Text.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, Flags);
}

int CMenus::DoButton_MenuTab(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners, SUIAnimator *pAnimator, const ColorRGBA *pDefaultColor, const ColorRGBA *pActiveColor, const ColorRGBA *pHoverColor, float EdgeRounding, const CCommunityIcon *pCommunityIcon)
{
	const bool MouseInside = Ui()->HotItem() == pButtonContainer;
	CUIRect Rect = *pRect;

	if(pAnimator != nullptr)
	{
		auto Time = time_get_nanoseconds();

		if(pAnimator->m_Time + 100ms < Time)
		{
			pAnimator->m_Value = pAnimator->m_Active ? 1 : 0;
			pAnimator->m_Time = Time;
		}

		pAnimator->m_Active = Checked || MouseInside;

		if(pAnimator->m_Active)
			pAnimator->m_Value = std::clamp<float>(pAnimator->m_Value + (Time - pAnimator->m_Time).count() / (double)std::chrono::nanoseconds(100ms).count(), 0, 1);
		else
			pAnimator->m_Value = std::clamp<float>(pAnimator->m_Value - (Time - pAnimator->m_Time).count() / (double)std::chrono::nanoseconds(100ms).count(), 0, 1);

		Rect.w += pAnimator->m_Value * pAnimator->m_WOffset;
		Rect.h += pAnimator->m_Value * pAnimator->m_HOffset;
		Rect.x += pAnimator->m_Value * pAnimator->m_XOffset;
		Rect.y += pAnimator->m_Value * pAnimator->m_YOffset;

		pAnimator->m_Time = Time;
	}

	if(Checked)
	{
		ColorRGBA ColorMenuTab = ms_ColorTabbarActive;
		if(pActiveColor)
			ColorMenuTab = *pActiveColor;

		Rect.Draw(ColorMenuTab, Corners, EdgeRounding);
	}
	else
	{
		if(MouseInside)
		{
			ColorRGBA HoverColorMenuTab = ms_ColorTabbarHover;
			if(pHoverColor)
				HoverColorMenuTab = *pHoverColor;

			Rect.Draw(HoverColorMenuTab, Corners, EdgeRounding);
		}
		else
		{
			ColorRGBA ColorMenuTab = ms_ColorTabbarInactive;
			if(pDefaultColor)
				ColorMenuTab = *pDefaultColor;

			Rect.Draw(ColorMenuTab, Corners, EdgeRounding);
		}
	}

	if(pAnimator != nullptr)
	{
		if(pAnimator->m_RepositionLabel)
		{
			Rect.x += Rect.w - pRect->w + Rect.x - pRect->x;
			Rect.y += Rect.h - pRect->h + Rect.y - pRect->y;
		}

		if(!pAnimator->m_ScaleLabel)
		{
			Rect.w = pRect->w;
			Rect.h = pRect->h;
		}
	}

	if(pCommunityIcon)
	{
		CUIRect CommunityIcon;
		Rect.Margin(2.0f, &CommunityIcon);
		m_CommunityIcons.Render(pCommunityIcon, CommunityIcon, true);
	}
	else
	{
		CUIRect Label;
		Rect.HMargin(2.0f, &Label);
		Ui()->DoLabel(&Label, pText, Label.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
	}

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, BUTTONFLAG_LEFT);
}

int CMenus::DoButton_GridHeader(const void *pId, const char *pText, int Checked, const CUIRect *pRect, int Align)
{
	if(Checked == 2)
		pRect->Draw(ColorRGBA(1, 0.98f, 0.5f, 0.55f), IGraphics::CORNER_T, 5.0f);
	else if(Checked)
		pRect->Draw(ColorRGBA(1, 1, 1, 0.5f), IGraphics::CORNER_T, 5.0f);

	CUIRect Temp;
	pRect->VMargin(5.0f, &Temp);
	Ui()->DoLabel(&Temp, pText, pRect->h * CUi::ms_FontmodHeight, Align);
	return Ui()->DoButtonLogic(pId, Checked, pRect, BUTTONFLAG_LEFT);
}

int CMenus::DoButton_Favorite(const void *pButtonId, const void *pParentId, bool Checked, const CUIRect *pRect)
{
	if(Checked || (pParentId != nullptr && Ui()->HotItem() == pParentId) || Ui()->HotItem() == pButtonId)
	{
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		const float Alpha = Ui()->HotItem() == pButtonId ? 0.2f : 0.0f;
		TextRender()->TextColor(Checked ? ColorRGBA(1.0f, 0.85f, 0.3f, 0.8f + Alpha) : ColorRGBA(0.5f, 0.5f, 0.5f, 0.8f + Alpha));
		SLabelProperties Props;
		Props.m_MaxWidth = pRect->w;
		Ui()->DoLabel(pRect, FontIcon::STAR, 12.0f, TEXTALIGN_MC, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	return Ui()->DoButtonLogic(pButtonId, 0, pRect, BUTTONFLAG_LEFT);
}

int CMenus::DoButton_CheckBox_Common(const void *pId, const char *pText, const char *pBoxText, const CUIRect *pRect, const unsigned Flags)
{
	CUIRect Box, Label;
	pRect->VSplitLeft(pRect->h, &Box, &Label);
	Label.VSplitLeft(5.0f, nullptr, &Label);

	Box.Margin(2.0f, &Box);
	Box.Draw(ColorRGBA(1, 1, 1, 0.25f * Ui()->ButtonColorMul(pId)), IGraphics::CORNER_ALL, 3.0f);

	const bool Checkable = *pBoxText == 'X';
	if(Checkable)
	{
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		Ui()->DoLabel(&Box, FontIcon::XMARK, Box.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	else
		Ui()->DoLabel(&Box, pBoxText, Box.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);

	TextRender()->SetRenderFlags(0);
	Ui()->DoLabel(&Label, pText, Box.h * CUi::ms_FontmodHeight, TEXTALIGN_ML);

	const SConfigVariable *pVar = CUClientSettingsLink::FindVariableByPointer(ConfigManager(), pId);
	if(pVar)
	{
		MaybeRegisterSettingsLinkVar(pId, pText);
		MaybeHighlightSettingsLink(pRect, pVar->m_pScriptName);
	}

	return Ui()->DoButtonLogic(pId, 0, pRect, Flags);
}

void CMenus::DoLaserPreview(const CUIRect *pRect, const ColorHSLA LaserOutlineColor, const ColorHSLA LaserInnerColor, const int LaserType)
{
	CUIRect Section = *pRect;
	vec2 From = vec2(Section.x + 30.0f, Section.y + Section.h / 2.0f);
	vec2 Pos = vec2(Section.x + Section.w - 20.0f, Section.y + Section.h / 2.0f);

	const ColorRGBA OuterColor = color_cast<ColorRGBA>(ColorHSLA(LaserOutlineColor));
	const ColorRGBA InnerColor = color_cast<ColorRGBA>(ColorHSLA(LaserInnerColor));
	const float TicksHead = Client()->GlobalTime() * Client()->GameTickSpeed();

	// TicksBody = 4.0 for less laser width for weapon alignment
	GameClient()->m_Items.RenderLaser(From, Pos, OuterColor, InnerColor, 4.0f, TicksHead, LaserType);

	switch(LaserType)
	{
	case LASERTYPE_RIFLE:
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteWeaponLaser);
		Graphics()->SelectSprite(SPRITE_WEAPON_LASER_BODY);
		Graphics()->QuadsBegin();
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		Graphics()->DrawSprite(Section.x + 30.0f, Section.y + Section.h / 2.0f, 60.0f);
		Graphics()->QuadsEnd();
		break;
	case LASERTYPE_SHOTGUN:
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteWeaponShotgun);
		Graphics()->SelectSprite(SPRITE_WEAPON_SHOTGUN_BODY);
		Graphics()->QuadsBegin();
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		Graphics()->DrawSprite(Section.x + 30.0f, Section.y + Section.h / 2.0f, 60.0f);
		Graphics()->QuadsEnd();
		break;
	case LASERTYPE_DRAGGER:
	{
		CTeeRenderInfo TeeRenderInfo;
		TeeRenderInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin));
		TeeRenderInfo.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
		TeeRenderInfo.m_Size = 64.0f;
		RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_NORMAL, vec2(-1, 0), Pos);
		break;
	}
	case LASERTYPE_FREEZE:
	{
		CTeeRenderInfo TeeRenderInfo;
		if(g_Config.m_ClShowNinja)
			TeeRenderInfo.Apply(GameClient()->m_Skins.Find("x_ninja"));
		else
			TeeRenderInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin));
		TeeRenderInfo.m_TeeRenderFlags = TEE_EFFECT_FROZEN;
		TeeRenderInfo.m_Size = 64.0f;
		TeeRenderInfo.m_ColorBody = ColorRGBA(1, 1, 1);
		TeeRenderInfo.m_ColorFeet = ColorRGBA(1, 1, 1);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_PAIN, vec2(1, 0), From);
		GameClient()->m_Effects.FreezingFlakes(From, vec2(32, 32), 1.0f);
		break;
	}
	default:
		GameClient()->m_Items.RenderLaser(From, From, OuterColor, InnerColor, 4.0f, TicksHead, LaserType);
	}
}

bool CMenus::DoLine_RadioMenu(CUIRect &View, const char *pLabel, std::vector<CButtonContainer> &vButtonContainers, const std::vector<const char *> &vLabels, const std::vector<int> &vValues, int &Value)
{
	dbg_assert(vButtonContainers.size() == vValues.size(), "vButtonContainers and vValues must have the same size");
	dbg_assert(vButtonContainers.size() == vLabels.size(), "vButtonContainers and vLabels must have the same size");
	const int N = vButtonContainers.size();
	const float Spacing = 2.0f;
	const float ButtonHeight = 20.0f;
	CUIRect Label, Buttons;
	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(ButtonHeight, &Buttons, &View);
	Buttons.VSplitMid(&Label, &Buttons, 10.0f);
	Buttons.HMargin(2.0f, &Buttons);
	Ui()->DoLabel(&Label, pLabel, 13.0f, TEXTALIGN_ML);
	const float W = Buttons.w / N;
	bool Pressed = false;
	for(int i = 0; i < N; ++i)
	{
		CUIRect Button;
		Buttons.VSplitLeft(W, &Button, &Buttons);
		int Corner = IGraphics::CORNER_NONE;
		if(i == 0)
			Corner = IGraphics::CORNER_L;
		if(i == N - 1)
			Corner = IGraphics::CORNER_R;
		if(DoButton_Menu(&vButtonContainers[i], vLabels[i], vValues[i] == Value, &Button, BUTTONFLAG_LEFT, nullptr, Corner))
		{
			Pressed = true;
			Value = vValues[i];
		}
	}
	return Pressed;
}

ColorHSLA CMenus::DoLine_ColorPicker(CButtonContainer *pResetId, const float LineSize, const float LabelSize, const float BottomMargin, CUIRect *pMainRect, const char *pText, unsigned int *pColorValue, const ColorRGBA DefaultColor, bool CheckBoxSpacing, int *pCheckBoxValue, bool Alpha, bool ShowReset)
{
	CUIRect Section, ColorPickerButton, ResetButton, Label;

	pMainRect->HSplitTop(LineSize, &Section, pMainRect);
	pMainRect->HSplitTop(BottomMargin, nullptr, pMainRect);

	if(ShowReset)
	{
		Section.VSplitRight(60.0f, &Section, &ResetButton);
		Section.VSplitRight(8.0f, &Section, nullptr);
	}
	Section.VSplitRight(Section.h, &Section, &ColorPickerButton);
	Section.VSplitRight(8.0f, &Label, nullptr);

	if(pCheckBoxValue != nullptr)
	{
		Label.Margin(2.0f, &Label);
		if(DoButton_CheckBox(pCheckBoxValue, pText, *pCheckBoxValue, &Label))
			*pCheckBoxValue ^= 1;
	}
	else if(CheckBoxSpacing)
	{
		Label.VSplitLeft(Label.h + 5.0f, nullptr, &Label);
	}
	if(pCheckBoxValue == nullptr)
	{
		Ui()->DoLabel(&Label, pText, LabelSize, TEXTALIGN_ML);
	}

	const ColorHSLA PickedColor = DoButton_ColorPicker(&ColorPickerButton, pColorValue, Alpha);

	if(ShowReset)
	{
		ResetButton.HMargin(2.0f, &ResetButton);
		if(DoButton_Menu(pResetId, Localize("Reset"), 0, &ResetButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.1f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)))
		{
			*pColorValue = color_cast<ColorHSLA>(DefaultColor).Pack(Alpha);
		}
	}

	return PickedColor;
}

ColorHSLA CMenus::DoButton_ColorPicker(const CUIRect *pRect, unsigned int *pHslaColor, bool Alpha)
{
	ColorHSLA HslaColor = ColorHSLA(*pHslaColor, Alpha);

	ColorRGBA Outline = ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f);
	Outline.a *= Ui()->ButtonColorMul(pHslaColor);

	CUIRect Rect;
	pRect->Margin(3.0f, &Rect);

	pRect->Draw(Outline, IGraphics::CORNER_ALL, 4.0f);
	Rect.Draw(color_cast<ColorRGBA>(HslaColor), IGraphics::CORNER_ALL, 4.0f);

	if(Ui()->DoButtonLogic(pHslaColor, 0, pRect, BUTTONFLAG_LEFT))
	{
		m_ColorPickerPopupContext.m_pHslaColor = pHslaColor;
		m_ColorPickerPopupContext.m_HslaColor = HslaColor;
		m_ColorPickerPopupContext.m_HsvaColor = color_cast<ColorHSVA>(HslaColor);
		m_ColorPickerPopupContext.m_RgbaColor = color_cast<ColorRGBA>(m_ColorPickerPopupContext.m_HsvaColor);
		m_ColorPickerPopupContext.m_Alpha = Alpha;
		Ui()->ShowPopupColorPicker(Ui()->MouseX(), Ui()->MouseY(), &m_ColorPickerPopupContext);
	}
	else if(Ui()->IsPopupOpen(&m_ColorPickerPopupContext) && m_ColorPickerPopupContext.m_pHslaColor == pHslaColor)
	{
		HslaColor = color_cast<ColorHSLA>(m_ColorPickerPopupContext.m_HsvaColor);
	}

	return HslaColor;
}

int CMenus::DoButton_CheckBoxAutoVMarginAndSet(const void *pId, const char *pText, int *pValue, CUIRect *pRect, float VMargin)
{
	CUIRect CheckBoxRect;
	pRect->HSplitTop(VMargin, &CheckBoxRect, pRect);

	int Logic = DoButton_CheckBox_Common(pId, pText, *pValue ? "X" : "", &CheckBoxRect, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
	if(Logic == 2)
	{
		TryOpenSettingsLinkMenuForVar(pId, pText, &CheckBoxRect);
		return 0;
	}
	if(Logic)
		*pValue ^= 1;

	return Logic;
}

int CMenus::DoButton_CheckBox(const void *pId, const char *pText, int Checked, const CUIRect *pRect)
{
	int Logic = DoButton_CheckBox_Common(pId, pText, Checked ? "X" : "", pRect, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
	if(Logic == 2)
	{
		TryOpenSettingsLinkMenuForVar(pId, pText, pRect);
		return 0;
	}
	return Logic == 1 ? 1 : 0;
}

int CMenus::DoButton_CheckBox_Number(const void *pId, const char *pText, int Checked, const CUIRect *pRect)
{
	char aBuf[16];
	str_format(aBuf, sizeof(aBuf), "%d", Checked);
	return DoButton_CheckBox_Common(pId, pText, aBuf, pRect, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
}

void CMenus::RenderMenubar(CUIRect Box, IClient::EClientState ClientState)
{
	CUIRect Button;

	int NewPage = -1;
	int ActivePage = -1;
	if(ClientState == IClient::STATE_OFFLINE)
	{
		ActivePage = m_MenuPage;
	}
	else if(ClientState == IClient::STATE_ONLINE)
	{
		ActivePage = m_GamePage;
	}
	else
	{
		dbg_assert_failed("Client state %d is invalid for RenderMenubar", ClientState);
	}

	// First render buttons aligned from right side so remaining
	// width is known when rendering buttons from left side.
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

	Box.VSplitRight(33.0f, &Box, &Button);
	static CButtonContainer s_QuitButton;
	ColorRGBA QuitColor(1, 0, 0, 0.5f);
	if(DoButton_MenuTab(&s_QuitButton, FontIcon::POWER_OFF, 0, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_QUIT], nullptr, nullptr, &QuitColor, 10.0f))
	{
		if(GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0) || m_MenusIngameTouchControls.UnsavedChanges() || GameClient()->m_TouchControls.HasEditingChanges())
		{
			m_Popup = POPUP_QUIT;
		}
		else
		{
			Client()->Quit();
		}
	}
	GameClient()->m_Tooltips.DoToolTip(&s_QuitButton, &Button, Localize("Quit"));

	Box.VSplitRight(10.0f, &Box, nullptr);
	Box.VSplitRight(33.0f, &Box, &Button);
	static CButtonContainer s_SettingsButton;
	if(DoButton_MenuTab(&s_SettingsButton, FontIcon::GEAR, ActivePage == PAGE_SETTINGS, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_SETTINGS]))
	{
		NewPage = PAGE_SETTINGS;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_SettingsButton, &Button, Localize("Settings"));

	Box.VSplitRight(10.0f, &Box, nullptr);
	Box.VSplitRight(33.0f, &Box, &Button);
	static CButtonContainer s_EditorButton;
	if(DoButton_MenuTab(&s_EditorButton, FontIcon::PEN_TO_SQUARE, 0, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_EDITOR]))
	{
		g_Config.m_ClEditor = 1;
	}
	GameClient()->m_Tooltips.DoToolTip(&s_EditorButton, &Button, Localize("Editor"));

	if(ClientState == IClient::STATE_OFFLINE)
	{
		Box.VSplitRight(10.0f, &Box, nullptr);
		Box.VSplitRight(33.0f, &Box, &Button);
		static CButtonContainer s_DemoButton;
		if(DoButton_MenuTab(&s_DemoButton, FontIcon::CLAPPERBOARD, ActivePage == PAGE_DEMOS, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_DEMOBUTTON]))
		{
			NewPage = PAGE_DEMOS;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_DemoButton, &Button, Localize("Demos"));
		Box.VSplitRight(10.0f, &Box, nullptr);

		if(g_Config.m_BcClansEnabled)
		{
			Box.VSplitRight(33.0f, &Box, &Button);
			ColorRGBA ClansAlert(0, 1, 0, 0.25f);
			ColorRGBA ClansAlertHover(0, 1, 0, 0.5f);
			ColorRGBA *pClansColor = nullptr;
			ColorRGBA *pClansHover = nullptr;
			if(g_Config.m_BcClansUnreadBadge && GameClient()->m_Clans.GetUnreadCount() > 0)
			{
				pClansColor = &ClansAlert;
				pClansHover = &ClansAlertHover;
			}
			static CButtonContainer s_ClansButton;
			if(DoButton_MenuTab(&s_ClansButton, FontIcon::ICON_USERS, ActivePage == PAGE_CLANS, &Button, IGraphics::CORNER_T, nullptr, pClansColor, pClansColor, pClansHover, 10.0f))
			{
				NewPage = PAGE_CLANS;
			}
			GameClient()->m_Tooltips.DoToolTip(&s_ClansButton, &Button, Localize("Clans"));
			Box.VSplitRight(10.0f, &Box, nullptr);
		}

		Box.VSplitLeft(33.0f, &Button, &Box);

		bool GotNewsOrUpdate = false;

#if defined(CONF_AUTOUPDATE)
		int State = Updater()->GetCurrentState();
		bool NeedUpdate = str_comp(Client()->LatestVersion(), "0");
		if(State == IUpdater::CLEAN && NeedUpdate)
		{
			GotNewsOrUpdate = true;
		}
#endif

		GotNewsOrUpdate |= (bool)g_Config.m_UiUnreadNews;

		ColorRGBA HomeButtonColorAlert(0, 1, 0, 0.25f);
		ColorRGBA HomeButtonColorAlertHover(0, 1, 0, 0.5f);
		ColorRGBA *pHomeButtonColor = nullptr;
		ColorRGBA *pHomeButtonColorHover = nullptr;

		const char *pHomeScreenButtonLabel = FontIcon::HOUSE;
		if(GotNewsOrUpdate)
		{
			pHomeScreenButtonLabel = FontIcon::NEWSPAPER;
			pHomeButtonColor = &HomeButtonColorAlert;
			pHomeButtonColorHover = &HomeButtonColorAlertHover;
		}

		static CButtonContainer s_StartButton;
		if(DoButton_MenuTab(&s_StartButton, pHomeScreenButtonLabel, false, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_HOME], pHomeButtonColor, pHomeButtonColor, pHomeButtonColorHover, 10.0f))
		{
			m_ShowStart = true;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_StartButton, &Button, Localize("Main menu"));

		const float BrowserButtonWidth = 75.0f;
		Box.VSplitLeft(10.0f, nullptr, &Box);
		Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
		static CButtonContainer s_InternetButton;
		if(DoButton_MenuTab(&s_InternetButton, FontIcon::EARTH_AMERICAS, ActivePage == PAGE_INTERNET, &Button, IGraphics::CORNER_T, &m_aAnimatorsBigPage[BIG_TAB_INTERNET]))
		{
			NewPage = PAGE_INTERNET;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_InternetButton, &Button, Localize("Internet"));

		Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
		static CButtonContainer s_LanButton;
		if(DoButton_MenuTab(&s_LanButton, FontIcon::NETWORK_WIRED, ActivePage == PAGE_LAN, &Button, IGraphics::CORNER_T, &m_aAnimatorsBigPage[BIG_TAB_LAN]))
		{
			NewPage = PAGE_LAN;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_LanButton, &Button, Localize("LAN"));

		Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
		static CButtonContainer s_FavoritesButton;
		if(DoButton_MenuTab(&s_FavoritesButton, FontIcon::STAR, ActivePage == PAGE_FAVORITES, &Button, IGraphics::CORNER_T, &m_aAnimatorsBigPage[BIG_TAB_FAVORITES]))
		{
			NewPage = PAGE_FAVORITES;
		}
		GameClient()->m_Tooltips.DoToolTip(&s_FavoritesButton, &Button, Localize("Favorites"));

		int MaxPage = PAGE_FAVORITES + ServerBrowser()->FavoriteCommunities().size();
		if(
			!Ui()->IsPopupOpen() &&
			CLineInput::GetActiveInput() == nullptr &&
			(g_Config.m_UiPage >= PAGE_INTERNET && g_Config.m_UiPage <= MaxPage) &&
			(m_MenuPage >= PAGE_INTERNET && m_MenuPage <= PAGE_FAVORITE_COMMUNITY_5))
		{
			if(Input()->KeyPress(KEY_RIGHT))
			{
				NewPage = g_Config.m_UiPage + 1;
				if(NewPage > MaxPage)
					NewPage = PAGE_INTERNET;
			}
			if(Input()->KeyPress(KEY_LEFT))
			{
				NewPage = g_Config.m_UiPage - 1;
				if(NewPage < PAGE_INTERNET)
					NewPage = MaxPage;
			}
		}

		size_t FavoriteCommunityIndex = 0;
		static CButtonContainer s_aFavoriteCommunityButtons[5];
		static_assert(std::size(s_aFavoriteCommunityButtons) == (size_t)PAGE_FAVORITE_COMMUNITY_5 - PAGE_FAVORITE_COMMUNITY_1 + 1);
		static_assert(std::size(s_aFavoriteCommunityButtons) == (size_t)BIT_TAB_FAVORITE_COMMUNITY_5 - BIT_TAB_FAVORITE_COMMUNITY_1 + 1);
		static_assert(std::size(s_aFavoriteCommunityButtons) == (size_t)IServerBrowser::TYPE_FAVORITE_COMMUNITY_5 - IServerBrowser::TYPE_FAVORITE_COMMUNITY_1 + 1);
		for(const CCommunity *pCommunity : ServerBrowser()->FavoriteCommunities())
		{
			if(Box.w < BrowserButtonWidth)
				break;
			Box.VSplitLeft(BrowserButtonWidth, &Button, &Box);
			const int Page = PAGE_FAVORITE_COMMUNITY_1 + FavoriteCommunityIndex;
			if(DoButton_MenuTab(&s_aFavoriteCommunityButtons[FavoriteCommunityIndex], FontIcon::ELLIPSIS, ActivePage == Page, &Button, IGraphics::CORNER_T, &m_aAnimatorsBigPage[BIT_TAB_FAVORITE_COMMUNITY_1 + FavoriteCommunityIndex], nullptr, nullptr, nullptr, 10.0f, m_CommunityIcons.Find(pCommunity->Id())))
			{
				NewPage = Page;
			}
			GameClient()->m_Tooltips.DoToolTip(&s_aFavoriteCommunityButtons[FavoriteCommunityIndex], &Button, pCommunity->Name());

			++FavoriteCommunityIndex;
			if(FavoriteCommunityIndex >= std::size(s_aFavoriteCommunityButtons))
				break;
		}

		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	else
	{
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

		// online menus
		Box.VSplitLeft(90.0f, &Button, &Box);
		static CButtonContainer s_GameButton;
		if(DoButton_MenuTab(&s_GameButton, Localize("Game"), ActivePage == PAGE_GAME, &Button, IGraphics::CORNER_TL))
			NewPage = PAGE_GAME;

		Box.VSplitLeft(90.0f, &Button, &Box);
		static CButtonContainer s_PlayersButton;
		if(DoButton_MenuTab(&s_PlayersButton, Localize("Players"), ActivePage == PAGE_PLAYERS, &Button, IGraphics::CORNER_NONE))
			NewPage = PAGE_PLAYERS;

		Box.VSplitLeft(130.0f, &Button, &Box);
		static CButtonContainer s_ServerInfoButton;
		if(DoButton_MenuTab(&s_ServerInfoButton, Localize("Server info"), ActivePage == PAGE_SERVER_INFO, &Button, IGraphics::CORNER_NONE))
			NewPage = PAGE_SERVER_INFO;

		Box.VSplitLeft(90.0f, &Button, &Box);
		static CButtonContainer s_NetworkButton;
		if(DoButton_MenuTab(&s_NetworkButton, Localize("Browser"), ActivePage == PAGE_NETWORK, &Button, IGraphics::CORNER_NONE))
			NewPage = PAGE_NETWORK;

		if(GameClient()->m_GameInfo.m_Race)
		{
			Box.VSplitLeft(90.0f, &Button, &Box);
			static CButtonContainer s_GhostButton;
			if(DoButton_MenuTab(&s_GhostButton, Localize("Ghost"), ActivePage == PAGE_GHOST, &Button, IGraphics::CORNER_NONE))
				NewPage = PAGE_GHOST;
		}

		Box.VSplitLeft(100.0f, &Button, &Box);
		Box.VSplitLeft(4.0f, nullptr, &Box);
		static CButtonContainer s_CallVoteButton;
		if(DoButton_MenuTab(&s_CallVoteButton, Localize("Call vote"), ActivePage == PAGE_CALLVOTE, &Button, IGraphics::CORNER_TR))
		{
			NewPage = PAGE_CALLVOTE;
			m_ControlPageOpening = true;
		}

		if(Box.w >= 10.0f + 33.0f + 10.0f)
		{
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

			Box.VSplitRight(10.0f, &Box, nullptr);
			Box.VSplitRight(33.0f, &Box, &Button);
			static CButtonContainer s_DemoButton;
			if(DoButton_MenuTab(&s_DemoButton, FontIcon::CLAPPERBOARD, ActivePage == PAGE_DEMOS, &Button, IGraphics::CORNER_T, &m_aAnimatorsSmallPage[SMALL_TAB_DEMOBUTTON]))
			{
				NewPage = PAGE_DEMOS;
			}
			GameClient()->m_Tooltips.DoToolTip(&s_DemoButton, &Button, Localize("Demos"));
			Box.VSplitRight(10.0f, &Box, nullptr);

			if(g_Config.m_BcClansEnabled)
			{
				Box.VSplitRight(33.0f, &Box, &Button);
				ColorRGBA ClansAlert(0, 1, 0, 0.25f);
				ColorRGBA ClansAlertHover(0, 1, 0, 0.5f);
				ColorRGBA *pClansColor = nullptr;
				ColorRGBA *pClansHover = nullptr;
				if(g_Config.m_BcClansUnreadBadge && GameClient()->m_Clans.GetUnreadCount() > 0)
				{
					pClansColor = &ClansAlert;
					pClansHover = &ClansAlertHover;
				}
				static CButtonContainer s_ClansButtonIngame;
				if(DoButton_MenuTab(&s_ClansButtonIngame, FontIcon::ICON_USERS, ActivePage == PAGE_CLANS, &Button, IGraphics::CORNER_T, nullptr, pClansColor, pClansColor, pClansHover, 10.0f))
				{
					NewPage = PAGE_CLANS;
				}
				GameClient()->m_Tooltips.DoToolTip(&s_ClansButtonIngame, &Button, Localize("Clans"));
				Box.VSplitRight(10.0f, &Box, nullptr);
			}

			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}
	}

	if(NewPage != -1)
	{
		if(ClientState == IClient::STATE_OFFLINE)
			SetMenuPage(NewPage);
		else
			m_GamePage = NewPage;
	}
}

void CMenus::RenderLoading(const char *pCaption, const char *pContent, int IncreaseCounter)
{
	// TODO: not supported right now due to separate render thread

	// m_Total==0 means we're outside the initial loading screen, skip entirely.
	if(m_LoadingState.m_Total == 0)
		return;

	m_LoadingState.m_Current += IncreaseCounter;
	if(m_LoadingState.m_Current > m_LoadingState.m_Total)
	{
		log_error("menus", "RenderLoading overflow: current=%d total=%d increase=%d", m_LoadingState.m_Current, m_LoadingState.m_Total, IncreaseCounter);
		m_LoadingState.m_Current = m_LoadingState.m_Total;
		return;
	}

	const bool IsServerJoinLoading =
		Client()->State() == IClient::STATE_CONNECTING || Client()->State() == IClient::STATE_LOADING;
	// Initial startup loading (black screen, bottom-left status) while m_Total > 0.
	if(!IsServerJoinLoading && Client()->State() != IClient::STATE_OFFLINE)
		return;

	const int CurLoadRenderCount = m_LoadingState.m_Current;

	// make sure that we don't render for each little thing we load
	// because that will slow down loading if we have vsync
	const std::chrono::nanoseconds Now = time_get_nanoseconds();
	if(Now - m_LoadingState.m_LastRender < std::chrono::nanoseconds(1s) / 60l)
		return;

	// need up date this here to get correct
	ms_GuiColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));

	Ui()->MapScreen();

	m_LoadingState.m_LastRender = Now;

	CUIRect FullScreen = *Ui()->Screen();
	if(IsServerJoinLoading)
	{
		const float BackgroundColor = 21.0f / 255.0f;
		FullScreen.Draw(ColorRGBA(BackgroundColor, BackgroundColor, BackgroundColor, 1.0f), IGraphics::CORNER_ALL, 0.0f);

		Graphics()->TextureSet(m_BcLogoTexture.IsValid() && !m_BcLogoTexture.IsNullTexture() ? m_BcLogoTexture : g_pData->m_aImages[IMAGE_BANNER].m_Id);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		const float LogoWidth = minimum(420.0f, maximum(220.0f, FullScreen.w * 0.38f));
		const float LogoHeight = LogoWidth / (360.0f / 103.0f);
		const float LogoY = FullScreen.y + minimum(70.0f, FullScreen.h * 0.12f);
		IGraphics::CQuadItem LogoQuad(FullScreen.x + (FullScreen.w - LogoWidth) / 2.0f, LogoY, LogoWidth, LogoHeight);
		Graphics()->QuadsDrawTL(&LogoQuad, 1);
		Graphics()->QuadsEnd();

		const bool HasContent = pContent != nullptr && pContent[0] != '\0';
		const bool HasProgressBar = m_LoadingState.m_Total > 0;
		const float LabelWidth = minimum(860.0f, FullScreen.w - 80.0f);
		const float TitleHeight = 24.0f;
		const float TextHeight = 24.0f;
		const float TextSpacing = 8.0f;
		const float ProgressSpacing = 16.0f;
		const float ProgressHeight = 24.0f;

		float ContentHeight = TitleHeight;
		if(HasContent)
			ContentHeight += TextSpacing + TextHeight;
		if(HasProgressBar)
			ContentHeight += ProgressSpacing + ProgressHeight;

		CUIRect Label;
		Label.x = FullScreen.x + (FullScreen.w - LabelWidth) / 2.0f;
		Label.y = FullScreen.y + FullScreen.h * 0.5f - ContentHeight / 2.0f;
		Label.w = LabelWidth;
		Label.h = TitleHeight;

		SLabelProperties TitleProps;
		TitleProps.m_MaxWidth = Label.w;
		TitleProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Label, pCaption, 24.0f, TEXTALIGN_MC, TitleProps);

		if(HasContent)
		{
			Label.y += TitleHeight + TextSpacing;
			Label.h = TextHeight;
			Ui()->DoLabel(&Label, pContent, 20.0f, TEXTALIGN_MC);
		}

		if(HasProgressBar)
		{
			CUIRect ProgressBar;
			ProgressBar.x = Label.x;
			ProgressBar.w = Label.w;
			ProgressBar.h = ProgressHeight;
			ProgressBar.y = (HasContent ? Label.y + TextHeight : Label.y + TitleHeight) + ProgressSpacing;
			Ui()->RenderProgressBar(ProgressBar, CurLoadRenderCount / (float)m_LoadingState.m_Total);
		}

		CUIRect Button;
		Button.w = FullScreen.w - 16.0f;
		Button.h = 36.0f;
		Button.x = FullScreen.x + 8.0f;
		Button.y = FullScreen.y + FullScreen.h - Button.h - 8.0f;

		static CButtonContainer s_Button;
		if(DoButton_Menu(&s_Button, Localize("Cancel"), 0, &Button) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		{
			Client()->Disconnect();
			Ui()->SetActiveItem(nullptr);
			RefreshBrowserTab(true);
		}
	}
	else
	{
		FullScreen.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f), IGraphics::CORNER_ALL, 0.0f);

		CUIRect Label;
		Label.x = FullScreen.x + 10.0f;
		Label.w = FullScreen.w - 20.0f;
		Label.h = 14.0f;
		Label.y = FullScreen.y + FullScreen.h - Label.h - 8.0f;

		char aStatus[256];
		if(pContent != nullptr && pContent[0] != '\0')
			str_format(aStatus, sizeof(aStatus), "%s: %s", pCaption, pContent);
		else
			str_copy(aStatus, pCaption);

		SLabelProperties TextProps;
		TextProps.m_MaxWidth = Label.w;
		TextProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Label, aStatus, 12.0f, TEXTALIGN_ML, TextProps);
	}

	Graphics()->SetColor(1.0, 1.0, 1.0, 1.0);
	Client()->UpdateAndSwap();
}

void CMenus::SetupLoadingTotal(int NumComponents)
{
	m_LoadingState.m_Total = g_pData->m_NumImages + NumComponents;
	if(!g_Config.m_ClThreadsoundloading)
		m_LoadingState.m_Total += g_pData->m_NumSounds;
}

void CMenus::FinishLoading()
{
	m_LoadingState.m_Current = 0;
	m_LoadingState.m_Total = 0;
}

void CMenus::RenderNews(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_NEWS);

	g_Config.m_UiUnreadNews = false;

	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);

	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.VSplitLeft(15.0f, nullptr, &MainView);

	CUIRect Label;

	const char *pStr = Client()->News();
	char aLine[256];
	while((pStr = str_next_token(pStr, "\n", aLine, sizeof(aLine))))
	{
		const int Len = str_length(aLine);
		if(Len > 0 && aLine[0] == '|' && aLine[Len - 1] == '|')
		{
			MainView.HSplitTop(30.0f, &Label, &MainView);
			aLine[Len - 1] = '\0';
			Ui()->DoLabel(&Label, aLine + 1, 20.0f, TEXTALIGN_ML);
		}
		else
		{
			MainView.HSplitTop(20.0f, &Label, &MainView);
			Ui()->DoLabel(&Label, aLine, 15.f, TEXTALIGN_ML);
		}
	}
}

void CMenus::OnInterfacesInit(CGameClient *pClient)
{
	CComponentInterfaces::OnInterfacesInit(pClient);
	m_MenusIngameTouchControls.OnInterfacesInit(pClient);
	m_MenusSettingsControls.OnInterfacesInit(pClient);
	m_MenusStart.OnInterfacesInit(pClient);
	m_CommunityIcons.OnInterfacesInit(pClient);
}

void CMenus::OnInit()
{
	if(g_Config.m_ClShowWelcome)
	{
		m_Popup = POPUP_LANGUAGE;
		m_CreateDefaultFavoriteCommunities = true;
	}

	if(g_Config.m_UiPage >= PAGE_FAVORITE_COMMUNITY_1 && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5 &&
		(size_t)(g_Config.m_UiPage - PAGE_FAVORITE_COMMUNITY_1) >= ServerBrowser()->FavoriteCommunities().size())
	{
		// Reset page to internet when there is no favorite community for this page.
		g_Config.m_UiPage = PAGE_INTERNET;
	}

	if(g_Config.m_ClSkipStartMenu)
	{
		m_ShowStart = false;
	}
	m_MenuPage = g_Config.m_UiPage;

	m_RefreshButton.Init(Ui(), -1);
	m_ConnectButton.Init(Ui(), -1);

	Console()->Chain("add_favorite", ConchainFavoritesUpdate, this);
	Console()->Chain("remove_favorite", ConchainFavoritesUpdate, this);
	Console()->Chain("add_friend", ConchainFriendlistUpdate, this);
	Console()->Chain("remove_friend", ConchainFriendlistUpdate, this);

	Console()->Chain("add_excluded_community", ConchainCommunitiesUpdate, this);
	Console()->Chain("remove_excluded_community", ConchainCommunitiesUpdate, this);
	Console()->Chain("add_excluded_country", ConchainCommunitiesUpdate, this);
	Console()->Chain("remove_excluded_country", ConchainCommunitiesUpdate, this);
	Console()->Chain("add_excluded_type", ConchainCommunitiesUpdate, this);
	Console()->Chain("remove_excluded_type", ConchainCommunitiesUpdate, this);

	Console()->Chain("ui_page", ConchainUiPageUpdate, this);

	Console()->Chain("snd_enable", ConchainUpdateMusicState, this);
	Console()->Chain("snd_enable_music", ConchainUpdateMusicState, this);
	Console()->Chain("cl_background_entities", ConchainBackgroundEntities, this);

	Console()->Chain("cl_assets_entities", ConchainAssetsEntities, this);
	Console()->Chain("cl_asset_game", ConchainAssetGame, this);
	Console()->Chain("cl_asset_emoticons", ConchainAssetEmoticons, this);
	Console()->Chain("cl_asset_particles", ConchainAssetParticles, this);
	Console()->Chain("cl_asset_hud", ConchainAssetHud, this);
	Console()->Chain("cl_asset_extras", ConchainAssetExtras, this);
	Console()->Chain("cl_asset_cursor", ConchainAssetCursor, this);
	Console()->Chain("cl_asset_arrow", ConchainAssetArrow, this);
	Console()->Chain("snd_pack", ConchainSndPack, this);
	Console()->Register("add_favorite_asset", "s[tab] s[asset_name]", CFGFLAG_CLIENT, ConAddFavoriteAsset, this, "Add an asset item as a favorite");
	Console()->Register("remove_favorite_asset", "s[tab] s[asset_name]", CFGFLAG_CLIENT, ConRemoveFavoriteAsset, this, "Remove an asset item from the favorites");
	ConfigManager()->RegisterCallback(CMenus::ConfigSaveCallback, this, ConfigDomain::TCLIENT);

	Console()->Chain("demo_play", ConchainDemoPlay, this);
	Console()->Chain("demo_speed", ConchainDemoSpeed, this);

	m_TextureBlob = Graphics()->LoadTexture("blob.png", IStorage::TYPE_ALL);
	m_MenuMediaBackground.Init(Graphics(), Storage());
	m_MainMenuLogoTexture = Graphics()->LoadTexture("bestclient/gui_logo.png", IStorage::TYPE_ALL);
	if(!m_MainMenuLogoTexture.IsValid() || m_MainMenuLogoTexture.IsNullTexture())
		m_MainMenuLogoTexture = Graphics()->LoadTexture("BestClient/gui_logo.png", IStorage::TYPE_ALL);
	m_UcLogoTexture = Graphics()->LoadTexture("uclient/logo/uclient.png", IStorage::TYPE_ALL);
	m_BcLogoTexture = Graphics()->LoadTexture("BestClient/gui_logo.png", IStorage::TYPE_ALL);

	m_LoadingState.m_Current = 0;
	// m_Total is set by gameclient.cpp via SetupLoadingTotal() after this returns,
	// using the pre-computed NumComponents from the init loop.
	m_IsInit = true;

	// load menu images
	m_vMenuImages.clear();
	Storage()->ListDirectory(IStorage::TYPE_ALL, "menuimages", MenuImageScan, this);

	m_CommunityIcons.Load();

	// Quad for the direction arrows above the player
	m_DirectionQuadContainerIndex = Graphics()->CreateQuadContainer(false);
	Graphics()->QuadContainerAddSprite(m_DirectionQuadContainerIndex, 0.f, 0.f, 22.f);
	Graphics()->QuadContainerUpload(m_DirectionQuadContainerIndex);
}

void CMenus::ConchainBackgroundEntities(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CMenus *pSelf = (CMenus *)pUserData;
		if(str_comp(g_Config.m_ClBackgroundEntities, pSelf->GameClient()->m_Background.MapName()) != 0)
			pSelf->GameClient()->m_Background.LoadBackground();
	}
}

void CMenus::ConchainUpdateMusicState(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	auto *pSelf = (CMenus *)pUserData;
	if(pResult->NumArguments())
		pSelf->UpdateMusicState();
}

void CMenus::UpdateMusicState()
{
	const bool ShouldPlay = Client()->State() == IClient::STATE_OFFLINE && g_Config.m_SndEnable && g_Config.m_SndMusic;
	if(ShouldPlay && !GameClient()->m_Sounds.IsPlaying(SOUND_MENU))
		GameClient()->m_Sounds.Enqueue(CSounds::CHN_MUSIC, SOUND_MENU);
	else if(!ShouldPlay && GameClient()->m_Sounds.IsPlaying(SOUND_MENU))
		GameClient()->m_Sounds.Stop(SOUND_MENU);
}

void CMenus::LoadMenuSfx()
{
	if(m_MenuSfxLoaded)
		return;

	static constexpr std::array<const char *, (size_t)EMenuSfxSample::COUNT> s_apSampleNames = {
		"bss-complete",
		"bss-progress",
		"bss-stage-0",
		"bss-stage-1",
		"bss-stage-2",
		"bss-stage-3",
		"button-hover",
		"button-select",
		"button-sidebar-hover",
		"button-sidebar-select",
		"check-off",
		"check-on",
		"cursor-tap",
		"default-hover",
		"default-select-disabled",
		"default-select",
		"dialog-cancel-select",
		"dialog-dangerous-select",
		"dialog-dangerous-tick",
		"dialog-ok-select",
		"dialog-pop-in",
		"dialog-pop-out",
		"dropdown-close",
		"dropdown-open",
		"generic-error",
		"item-swap",
		"menu-close",
		"menu-open-select",
		"menu-open",
		"menu-sub-open",
		"metronome-latch",
		"metronome-tick-downbeat",
		"metronome-tick",
		"noclick-hover",
		"noclick-select",
		"notch-tick",
		"notification-cancel",
		"notification-default",
		"notification-done",
		"notification-error",
		"notification-friend-offline",
		"notification-friend-online",
		"notification-mention",
		"osd-change",
		"osd-off",
		"osd-on",
		"overlay-big-pop-in",
		"overlay-big-pop-out",
		"overlay-pop-in",
		"overlay-pop-out",
		"ruleset-select-fruits",
		"ruleset-select-mania",
		"ruleset-select-osu",
		"ruleset-select-taiko",
		"screen-back",
		"scroll-to-previous",
		"scroll-to-top",
		"settings-pop-in",
		"shutter",
		"submit-select",
		"tabselect-select",
		"toolbar-hover",
		"toolbar-select",
		"wave-pop-in",
		"wave-pop-out",
	};

	for(size_t i = 0; i < s_apSampleNames.size(); ++i)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "audio/osu/ui/%s.wv", s_apSampleNames[i]);
		m_aMenuSfxSamples[i] = Sound()->LoadWV(aPath);
	}
	m_MenuSfxLastHoverTick = 0;
	m_MenuSfxLastClickTick = 0;
	m_MenuSfxLastScrollTick = 0;
	m_MenuSfxLastSliderTick = 0;
	m_MenuSfxLastPopupTick = 0;
	m_MenuSfxLoaded = true;
}

void CMenus::UnloadMenuSfx()
{
	if(!m_MenuSfxLoaded)
		return;

	for(const int SampleId : m_aMenuSfxSamples)
	{
		if(SampleId >= 0)
			Sound()->UnloadSample(SampleId);
	}

	m_aMenuSfxSamples.fill(-1);
	m_MenuSfxLastHoverTick = 0;
	m_MenuSfxLastClickTick = 0;
	m_MenuSfxLastScrollTick = 0;
	m_MenuSfxLastSliderTick = 0;
	m_MenuSfxLastPopupTick = 0;
	m_MenuSfxLoaded = false;
}

void CMenus::PlayMenuSfxSample(int SampleId, float Pitch)
{
	if(SampleId < 0 || !g_Config.m_SndEnable || !g_Config.m_BcMenuSfx)
		return;

	const float Volume = std::clamp(g_Config.m_BcMenuSfxVolume / 100.0f, 0.0f, 1.0f);
	if(Volume <= 0.0f)
		return;

	// NOTE: Voice pitch control is not exposed by ISound on this branch, so Pitch
	// is accepted for API compatibility but currently has no effect.
	(void)Pitch;
	Sound()->Play(CSounds::CHN_GUI, SampleId, 0, Volume);
}

void CMenus::PlayMenuSfxSample(EMenuSfxSample Sample, float Pitch)
{
	if(!m_MenuSfxLoaded)
		LoadMenuSfx();

	PlayMenuSfxSample(m_aMenuSfxSamples[(size_t)Sample], Pitch);
}

void CMenus::PlayIngameMenuOpenSfx()
{
	PlayMenuSfxSample(EMenuSfxSample::MENU_OPEN);
	PlayMenuSfxSample(EMenuSfxSample::MENU_SUB_OPEN);
}

void CMenus::PlayIngameMenuCloseSfx()
{
	PlayMenuSfxSample(EMenuSfxSample::MENU_CLOSE);
	PlayMenuSfxSample(EMenuSfxSample::MENU_SUB_OPEN);
}

void CMenus::PopupMessage(const char *pTitle, const char *pMessage, const char *pButtonLabel, int NextPopup, FPopupButtonCallback pfnButtonCallback)
{
	// reset active item
	Ui()->SetActiveItem(nullptr);

	str_copy(m_aPopupTitle, pTitle);
	str_copy(m_aPopupMessage, pMessage);
	str_copy(m_aPopupButtons[BUTTON_CONFIRM].m_aLabel, pButtonLabel);
	m_aPopupButtons[BUTTON_CONFIRM].m_NextPopup = NextPopup;
	m_aPopupButtons[BUTTON_CONFIRM].m_pfnCallback = pfnButtonCallback;
	m_Popup = POPUP_MESSAGE;
}

void CMenus::PopupConfirm(const char *pTitle, const char *pMessage, const char *pConfirmButtonLabel, const char *pCancelButtonLabel,
	FPopupButtonCallback pfnConfirmButtonCallback, int ConfirmNextPopup, FPopupButtonCallback pfnCancelButtonCallback, int CancelNextPopup)
{
	// reset active item
	Ui()->SetActiveItem(nullptr);

	str_copy(m_aPopupTitle, pTitle);
	str_copy(m_aPopupMessage, pMessage);
	str_copy(m_aPopupButtons[BUTTON_CONFIRM].m_aLabel, pConfirmButtonLabel);
	m_aPopupButtons[BUTTON_CONFIRM].m_NextPopup = ConfirmNextPopup;
	m_aPopupButtons[BUTTON_CONFIRM].m_pfnCallback = pfnConfirmButtonCallback;
	str_copy(m_aPopupButtons[BUTTON_CANCEL].m_aLabel, pCancelButtonLabel);
	m_aPopupButtons[BUTTON_CANCEL].m_NextPopup = CancelNextPopup;
	m_aPopupButtons[BUTTON_CANCEL].m_pfnCallback = pfnCancelButtonCallback;
	m_Popup = POPUP_CONFIRM;
	m_PopupConfirmHasCheckbox = false;
}

bool CMenus::PopupUpdateRequired()
{
	if(!Client()->UpdateRequired())
		return false;

#if defined(CONF_AUTOUPDATE)
	const IUpdater::EUpdaterState State = Updater()->GetCurrentState();
	if(State == IUpdater::NEED_RESTART)
	{
		PopupConfirm(Localize("Update required"),
			Localize("The update has been downloaded. Restart via the launcher to finish updating, then you can join a server."),
			Localize("Restart launcher"), Localize("Not now"), &CMenus::PopupConfirmStartUpdate);
	}
	else if(State == IUpdater::DOWNLOADING || State == IUpdater::GETTING_MANIFEST)
	{
		PopupMessage(Localize("Update required"),
			Localize("The update is downloading. You can join a server as soon as it has been installed."),
			Localize("Ok"));
	}
	else
	{
		char aMessage[256];
		str_format(aMessage, sizeof(aMessage),
			Localize("UClient %s is available. You have to update before you can join a server."),
			Updater()->GetLatestVersionString());
		PopupConfirm(Localize("Update required"), aMessage, Localize("Update now"), Localize("Not now"), &CMenus::PopupConfirmStartUpdate);
	}
#else
	PopupMessage(Localize("Update required"),
		Localize("A new version of UClient is available. You have to update before you can join a server."),
		Localize("Ok"));
#endif

	// Keep the menu open afterwards so the update progress stays visible.
	m_PopupDeactivateAfterButton = false;
	SetActive(true);
	return true;
}

void CMenus::PopupConfirmStartUpdate()
{
#if defined(CONF_AUTOUPDATE)
	if(Updater()->GetCurrentState() == IUpdater::NEED_RESTART)
		Updater()->ApplyUpdateAndRestart();
	else
	{
#if defined(CONF_FAMILY_WINDOWS)
		char aLauncher[IO_MAX_PATH_LENGTH];
		if(UClientLaunchGate_FindLauncherPath(aLauncher, sizeof(aLauncher)))
		{
			process_execute(aLauncher, EShellExecuteWindowState::FOREGROUND);
			Client()->Quit();
			return;
		}
#endif
		Updater()->InitiateUpdate();
	}
#endif
}

void CMenus::PopupConfirmWithCheckbox(const char *pTitle, const char *pMessage, const char *pConfirmButtonLabel, const char *pCancelButtonLabel,
	const char *pCheckboxLabel, bool CheckboxValue, FPopupButtonCallback pfnConfirmButtonCallback, int ConfirmNextPopup,
	FPopupButtonCallback pfnCancelButtonCallback, int CancelNextPopup)
{
	PopupConfirm(pTitle, pMessage, pConfirmButtonLabel, pCancelButtonLabel, pfnConfirmButtonCallback, ConfirmNextPopup, pfnCancelButtonCallback, CancelNextPopup);
	m_PopupConfirmHasCheckbox = true;
	m_PopupConfirmCheckboxValue = CheckboxValue;
	str_copy(m_aPopupCheckboxLabel, pCheckboxLabel, sizeof(m_aPopupCheckboxLabel));
}

void CMenus::PopupConfirmOpenLink(const char *pTitle, const char *pMessage, const char *pConfirmButtonLabel, const char *pCancelButtonLabel, const char *pUrl, bool Dangerous)
{
	const bool WasActive = IsActive();
	PopupConfirm(pTitle, pMessage, pConfirmButtonLabel, pCancelButtonLabel, &CMenus::PopupOpenStoredLink, POPUP_NONE, &CMenus::PopupCancelStoredLink);
	str_copy(m_aPopupLinkUrl, pUrl);
	m_PopupDangerousConfirmButton = Dangerous;
	if(Dangerous)
		str_copy(m_aPopupDangerousHoverLabel, Localize("Do you really want to open it?"));
	else
		m_aPopupDangerousHoverLabel[0] = '\0';
	m_PopupDeactivateAfterButton = !WasActive;
	SetActive(true);
}

void CMenus::PopupOpenStoredLink()
{
	if(m_aPopupLinkUrl[0] == '\0')
		return;
	if(!Client()->ViewLink(m_aPopupLinkUrl))
		log_error("client", "Failed to open link '%s'", m_aPopupLinkUrl);

	if(m_PopupDeactivateAfterButton)
		SetActive(false);
	m_PopupDeactivateAfterButton = false;
}

void CMenus::PopupCancelStoredLink()
{
	if(m_PopupDeactivateAfterButton)
		SetActive(false);
	m_PopupDeactivateAfterButton = false;
}

void CMenus::RequestUClientServerJoin(const char *pAddr, const char *pServerName)
{
	if(!pAddr || pAddr[0] == '\0')
		return;
	if(PopupUpdateRequired())
		return;
	str_copy(m_aUClientJoinServerAddr, pAddr, sizeof(m_aUClientJoinServerAddr));

	const char *pDisplayName = (pServerName && pServerName[0] != '\0') ? pServerName : pAddr;
	char aMessage[256];
	str_format(aMessage, sizeof(aMessage), Localize("Do you really want to move to %s?"), pDisplayName);

	const bool WasActive = IsActive();
	PopupConfirm(Localize("Join server"), aMessage, Localize("Yes"), Localize("No"),
		&CMenus::PopupConfirmJoinUClientServer, POPUP_NONE, &CMenus::PopupCancelStoredLink);
	m_PopupDeactivateAfterButton = !WasActive;
	SetActive(true);
}

void CMenus::PopupConfirmJoinUClientServer()
{
	if(m_aUClientJoinServerAddr[0] != '\0')
	{
		// Connect directly (not via the menus Connect() helper) to avoid a second confirm.
		Client()->Connect(m_aUClientJoinServerAddr);
		m_aUClientJoinServerAddr[0] = '\0';
	}
	if(m_PopupDeactivateAfterButton)
		SetActive(false);
	m_PopupDeactivateAfterButton = false;
}

void CMenus::OpenSoundboardDeletePopup(bool LocalFile)
{
	const char *pBody = LocalFile ?
		Localize("Are you sure you want to delete this recording from your computer?") :
		Localize("Are you sure you want to delete this sound?");
	PopupConfirm(Localize("Delete sound"), pBody,
		Localize("Delete"), Localize("Cancel"),
		&CMenus::PopupConfirmSoundboardDelete, POPUP_NONE, &CMenus::PopupCancelSoundboardDelete);
	SetActive(true);
}

void CMenus::PopupConfirmSoundboardDelete()
{
	GameClient()->m_VoiceChat.ConfirmSoundboardDelete();
}

void CMenus::PopupCancelSoundboardDelete()
{
	GameClient()->m_VoiceChat.CancelSoundboardDelete();
}

void CMenus::PopupWarning(const char *pTopic, const char *pBody, const char *pButton, std::chrono::nanoseconds Duration)
{
	// no multiline support for console
	std::string BodyStr = pBody;
	std::replace(BodyStr.begin(), BodyStr.end(), '\n', ' ');
	log_warn("client", "%s: %s", pTopic, BodyStr.c_str());

	Ui()->SetActiveItem(nullptr);

	str_copy(m_aMessageTopic, pTopic);
	str_copy(m_aMessageBody, pBody);
	str_copy(m_aMessageButton, pButton);
	m_Popup = POPUP_WARNING;
	SetActive(true);

	m_PopupWarningDuration = Duration;
	m_PopupWarningLastTime = time_get_nanoseconds();
}

bool CMenus::CanDisplayWarning() const
{
	return m_Popup == POPUP_NONE;
}

void CMenus::RenderUClientAccountGate(CUIRect Screen)
{
	const CUClientAccount &Account = GameClient()->m_UClientAccount;
	CUIRect Box = {
		Screen.x + maximum(0.0f, (Screen.w - 520.0f) / 2.0f),
		Screen.y + maximum(0.0f, (Screen.h - 340.0f) / 2.0f),
		minimum(Screen.w, 520.0f),
		minimum(Screen.h, 340.0f)};
	Box.Draw(ColorRGBA(0.03f, 0.03f, 0.05f, 0.94f), IGraphics::CORNER_ALL, 10.0f);
	CUIRect Content;
	Box.Margin(24.0f, &Content);

	CUIRect Row;
	Content.HSplitTop(38.0f, &Row, &Content);
	const bool Banned = Account.State() == CUClientAccount::EState::BLOCKED_BANNED;
	Ui()->DoLabel(&Row,
		Account.IsPending() ? Localize("Verifying UClient account...") :
		Banned ? Localize("Your UClient has been blocked") :
		Localize("UClient account verification failed"),
		24.0f, TEXTALIGN_MC);

	Content.HSplitTop(12.0f, nullptr, &Content);
	Content.HSplitTop(46.0f, &Row, &Content);
	Ui()->DoLabel(&Row,
		Account.IsPending() ? Localize("Please wait while your account is verified.") : Account.ErrorMessage(),
		13.0f, TEXTALIGN_MC, {.m_MaxWidth = Row.w});

	if(!Account.IsPending() && Account.ErrorCode()[0])
	{
		Content.HSplitTop(28.0f, &Row, &Content);
		CUIRect ErrorCodeLabel, CopyButton;
		Row.VSplitRight(76.0f, &ErrorCodeLabel, &CopyButton);
		ErrorCodeLabel.VSplitRight(8.0f, &ErrorCodeLabel, nullptr);
		char aErrorCode[192];
		str_format(aErrorCode, sizeof(aErrorCode), Localize("Error code: %s"), Account.ErrorCode());
		Ui()->DoLabel(&ErrorCodeLabel, aErrorCode, 12.0f, TEXTALIGN_MR, {.m_MaxWidth = ErrorCodeLabel.w});
		static CButtonContainer s_CopyAccountErrorCodeButton;
		if(DoButton_Menu(&s_CopyAccountErrorCodeButton, Localize("Copy"), 0, &CopyButton))
			Input()->SetClipboardText(Account.ErrorCode());
	}

	if(Banned)
	{
		char aLine[384];
		str_format(aLine, sizeof(aLine), Localize("Reason: %s"),
			Account.BanReason()[0] ? Account.BanReason() : Localize("No reason was provided."));
		Content.HSplitTop(28.0f, &Row, &Content);
		Ui()->DoLabel(&Row, aLine, 13.0f, TEXTALIGN_MC, {.m_MaxWidth = Row.w});

		if(Account.BanExpiresAt() > 0)
		{
			char aTimestamp[64];
			str_timestamp_ex((time_t)Account.BanExpiresAt(), aTimestamp, sizeof(aTimestamp), TimestampFormat::SPACE);
			str_format(aLine, sizeof(aLine), Localize("Expires: %s"), aTimestamp);
		}
		else
			str_copy(aLine, Localize("Duration: Permanent"), sizeof(aLine));
		Content.HSplitTop(24.0f, &Row, &Content);
		Ui()->DoLabel(&Row, aLine, 13.0f, TEXTALIGN_MC);

		Content.HSplitTop(42.0f, &Row, &Content);
		Ui()->DoLabel(&Row,
			Localize("If you believe this ban is unjustified, please join the Discord below and contact an administrator."),
			12.0f, TEXTALIGN_MC, {.m_MaxWidth = Row.w});
	}

	constexpr float ButtonWidth = 120.0f;
	constexpr float RestartButtonWidth = 34.0f;
	constexpr float ButtonGap = 10.0f;
	constexpr float ButtonsWidth = ButtonWidth * 2.0f + RestartButtonWidth + ButtonGap * 2.0f;
	const float ButtonsX = Box.x + (Box.w - ButtonsWidth) / 2.0f;
	CUIRect DiscordButton = {ButtonsX, Box.y + Box.h - 48.0f, ButtonWidth, 30.0f};
	CUIRect QuitButton = {DiscordButton.x + DiscordButton.w + ButtonGap, DiscordButton.y, ButtonWidth, DiscordButton.h};
	CUIRect RestartButton = {QuitButton.x + QuitButton.w + ButtonGap, QuitButton.y, RestartButtonWidth, QuitButton.h};
	static CButtonContainer s_DiscordButton;
	static CButtonContainer s_QuitButton;
	static CButtonContainer s_RestartButton;
	if(DoButton_Menu(&s_DiscordButton, "Discord", 0, &DiscordButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.2f))
		Client()->ViewLink("https://discord.gg/EN4yYypsPs");
	if(DoButton_Menu(&s_QuitButton, Localize("Quit"), 0, &QuitButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.2f))
		Client()->Quit();
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	const bool Restart = DoButton_Menu(&s_RestartButton, FontIcon::ARROWS_ROTATE, 0, &RestartButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.15f);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	GameClient()->m_Tooltips.DoToolTip(&s_RestartButton, &RestartButton, Localize("Restart client"));
	if(Restart)
		Client()->Restart();
}

void CMenus::Render()
{
	Ui()->MapScreen();
	Ui()->SetMouseSlow(false);

	static int s_Frame = 0;
	if(s_Frame == 0)
	{
		RefreshBrowserTab(true);
		s_Frame++;
	}
	else if(s_Frame == 1)
	{
		UpdateMusicState();
		s_Frame++;
	}
	else
	{
		m_CommunityIcons.Update();
	}

	// Initially add DDNet as favorite community and select its tab.
	// This must be delayed until the DDNet info is available.
	if(m_CreateDefaultFavoriteCommunities &&
		ServerBrowser()->DDNetInfoAvailable())
	{
		m_CreateDefaultFavoriteCommunities = false;
		if(ServerBrowser()->Community(IServerBrowser::COMMUNITY_DDNET) != nullptr)
		{
			ServerBrowser()->FavoriteCommunitiesFilter().Clear();
			ServerBrowser()->FavoriteCommunitiesFilter().Add(IServerBrowser::COMMUNITY_DDNET);
			SetMenuPage(PAGE_FAVORITE_COMMUNITY_1);
			ServerBrowser()->Refresh(IServerBrowser::TYPE_FAVORITE_COMMUNITY_1);
		}
	}
	if(m_JoinTutorial.m_Queued && m_Popup == POPUP_NONE)
	{
		const char *pAddr = ServerBrowser()->GetTutorialServer();
		if(pAddr)
		{
			Client()->Connect(pAddr);
		}
		else
		{
			m_Popup = POPUP_JOIN_TUTORIAL;
		}
		m_JoinTutorial.m_Queued = false;
	}

	// BestClient: auto-refresh the server browser list on a timer while a browser
	// tab is open. Uses the plain (non-forced) refresh path so it only re-polls the
	// current list instead of re-requesting DDNet info and force-rebuilding the
	// community cache every tick, which caused noticeable stutter with a short
	// refresh interval.
	const bool BrowserPageActive = m_MenuPage >= PAGE_INTERNET && m_MenuPage <= PAGE_FAVORITE_COMMUNITY_5;
	if(BrowserPageActive && g_Config.m_BcAutoServerListRefresh)
	{
		const bool BrowserBusy = ServerBrowser()->IsRefreshing() || ServerBrowser()->IsGettingServerlist();
		if(!BrowserBusy)
		{
			const int64_t Now = time_get();
			const int64_t RefreshInterval = (int64_t)g_Config.m_BcAutoServerListRefreshSeconds * time_freq();
			if(m_LastServerBrowserRefreshTick == 0)
				m_LastServerBrowserRefreshTick = Now;
			else if(RefreshInterval > 0 && Now - m_LastServerBrowserRefreshTick >= RefreshInterval)
			{
				ServerBrowser()->Refresh(ServerBrowser()->GetCurrentType());
				UpdateCommunityCache(false);
				m_LastServerBrowserRefreshTick = Now;
			}
		}
	}
	else if(!BrowserPageActive)
	{
		m_LastServerBrowserRefreshTick = 0;
	}

	// Determine the client state once before rendering because it can change
	// while rendering which causes frames with broken user interface.
	const IClient::EClientState ClientState = Client()->State();

	if(ClientState == IClient::STATE_ONLINE || ClientState == IClient::STATE_DEMOPLAYBACK)
	{
		m_MenuMediaBackground.Unload();
		ms_ColorTabbarInactive = ms_ColorTabbarInactiveIngame;
		ms_ColorTabbarActive = ms_ColorTabbarActiveIngame;
		ms_ColorTabbarHover = ms_ColorTabbarHoverIngame;
	}
	else
	{
		const float MediaScreenHeight = 300.0f;
		const float MediaScreenWidth = MediaScreenHeight * Graphics()->ScreenAspect();
		m_MenuMediaBackground.SyncFromConfig(g_Config.m_BcMenuMediaBackground, g_Config.m_BcMenuMediaBackgroundPath);
		m_MenuMediaBackground.Update();
		if(!m_MenuMediaBackground.Render(MediaScreenWidth, MediaScreenHeight) && !GameClient()->m_MenuBackground.Render())
		{
			RenderBackground();
		}
		ms_ColorTabbarInactive = ms_ColorTabbarInactiveOutgame;
		ms_ColorTabbarActive = ms_ColorTabbarActiveOutgame;
		ms_ColorTabbarHover = ms_ColorTabbarHoverOutgame;
	}

	CUIRect Screen = *Ui()->Screen();
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK || m_Popup != POPUP_NONE)
	{
		Screen.Margin(10.0f, &Screen);
	}
	if(!GameClient()->m_UClientAccount.IsReady())
	{
		RenderUClientAccountGate(Screen);
		return;
	}

	switch(ClientState)
	{
	case IClient::STATE_QUITTING:
	case IClient::STATE_RESTARTING:
		// Render nothing except menu background. This should not happen for more than one frame.
		return;

	case IClient::STATE_CONNECTING:
		RenderPopupConnecting(Screen);
		break;

	case IClient::STATE_LOADING:
		RenderPopupLoading(Screen);
		break;

	case IClient::STATE_OFFLINE:
		if(m_Popup != POPUP_NONE)
		{
			RenderPopupFullscreen(Screen);
		}
		else if(m_ShowStart)
		{
			m_MenusStart.RenderStartMenu(Screen);
		}
		else
		{
			CUIRect TabBar, MainView;
			Screen.HSplitTop(24.0f, &TabBar, &MainView);

			if(m_MenuPage == PAGE_NEWS)
			{
				RenderNews(MainView);
			}
			else if(m_MenuPage >= PAGE_INTERNET && m_MenuPage <= PAGE_FAVORITE_COMMUNITY_5)
			{
				RenderServerbrowser(MainView);
			}
			else if(m_MenuPage == PAGE_DEMOS)
			{
				RenderDemoBrowser(MainView);
			}
			else if(m_MenuPage == PAGE_SETTINGS)
			{
				RenderSettings(MainView);
			}
			else if(m_MenuPage == PAGE_CLANS)
			{
				RenderClans(MainView);
			}
			else
			{
				dbg_assert_failed("Invalid m_MenuPage: %d", m_MenuPage);
			}

			RenderMenubar(TabBar, ClientState);
		}
		break;

	case IClient::STATE_ONLINE:
		if(m_Popup != POPUP_NONE)
		{
			RenderPopupFullscreen(Screen);
		}
		else
		{
			CUIRect TabBar, MainView;
			Screen.HSplitTop(24.0f, &TabBar, &MainView);

			if(m_GamePage == PAGE_GAME)
			{
				RenderGame(MainView);
				RenderIngameHint();
			}
			else if(m_GamePage == PAGE_PLAYERS)
			{
				RenderPlayers(MainView);
			}
			else if(m_GamePage == PAGE_SERVER_INFO)
			{
				RenderServerInfo(MainView);
			}
			else if(m_GamePage == PAGE_NETWORK)
			{
				RenderInGameNetwork(MainView);
			}
			else if(m_GamePage == PAGE_GHOST)
			{
				RenderGhost(MainView);
			}
			else if(m_GamePage == PAGE_CALLVOTE)
			{
				RenderServerControl(MainView);
			}
			else if(m_GamePage == PAGE_DEMOS)
			{
				RenderDemoBrowser(MainView);
			}
			else if(m_GamePage == PAGE_SETTINGS)
			{
				RenderSettings(MainView);
			}
			else if(m_GamePage == PAGE_CLANS)
			{
				RenderClans(MainView);
			}
			else
			{
				dbg_assert_failed("Invalid m_GamePage: %d", m_GamePage);
			}

			RenderMenubar(TabBar, ClientState);
		}
		break;

	case IClient::STATE_DEMOPLAYBACK:
		if(m_Popup != POPUP_NONE)
		{
			Ui()->ClosePopupMenu(&m_DemoCameraEffectsPopupId);
			RenderPopupFullscreen(Screen);
		}
		else
		{
			RenderDemoPlayer(Screen);
		}
		break;
	}

	Ui()->RenderPopupMenus();

	// Prevent UI elements from being hovered while a key reader is active
	if(GameClient()->m_KeyBinder.IsActive())
	{
		Ui()->SetHotItem(nullptr);
	}

	// Handle this escape hotkey after popup menus
	if(!m_ShowStart && ClientState == IClient::STATE_OFFLINE && Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		m_ShowStart = true;
	}
}

void CMenus::RenderPopupFullscreen(CUIRect Screen)
{
	char aBuf[1536];
	const char *pTitle = "";
	const char *pExtraText = "";
	const char *pButtonText = "";
	bool TopAlign = false;

	ColorRGBA BgColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f);
	if(m_Popup == POPUP_MESSAGE || m_Popup == POPUP_CONFIRM)
	{
		pTitle = m_aPopupTitle;
		pExtraText = m_aPopupMessage;
		TopAlign = true;
	}
	else if(m_Popup == POPUP_DISCONNECTED)
	{
		pTitle = Localize("Disconnected");
		pExtraText = Client()->ErrorString();
		pButtonText = Localize("Ok");
		if(Client()->ReconnectTime() > 0)
		{
			str_format(aBuf, sizeof(aBuf), Localize("Reconnect in %d sec"), (int)((Client()->ReconnectTime() - time_get()) / time_freq()) + 1);
			pTitle = Client()->ErrorString();
			pExtraText = aBuf;
			pButtonText = Localize("Abort");
		}
	}
	else if(m_Popup == POPUP_RENAME_DEMO)
	{
		dbg_assert(m_DemolistSelectedIndex >= 0, "m_DemolistSelectedIndex invalid for POPUP_RENAME_DEMO");
		pTitle = m_vpFilteredDemos[m_DemolistSelectedIndex]->m_IsDir ? Localize("Rename folder") : Localize("Rename demo");
	}
#if defined(CONF_VIDEORECORDER)
	else if(m_Popup == POPUP_RENDER_DEMO)
	{
		pTitle = Localize("Render demo");
	}
	else if(m_Popup == POPUP_RENDER_DONE)
	{
		pTitle = Localize("Render complete");
	}
#endif
	else if(m_Popup == POPUP_PASSWORD)
	{
		pTitle = Localize("Password incorrect");
		pButtonText = Localize("Try again");
	}
	else if(m_Popup == POPUP_RESTART)
	{
		pTitle = Localize("Restart");
		pExtraText = Localize("Are you sure that you want to restart?");
	}
	else if(m_Popup == POPUP_QUIT)
	{
		pTitle = Localize("Quit");
		pExtraText = Localize("Are you sure that you want to quit?");
	}
	else if(m_Popup == POPUP_FIRST_LAUNCH)
	{
		pTitle = Localize("Welcome to DDNet");
		str_format(aBuf, sizeof(aBuf), "%s\n\n%s\n\n%s\n\n%s",
			Localize("DDraceNetwork is a cooperative online game where the goal is for you and your group of tees to reach the finish line of the map. As a newcomer you should start on Novice servers, which host the easiest maps. Consider the ping to choose a server close to you."),
			Localize("Use k key to kill (restart), q to pause and watch other players. See settings for other key binds."),
			Localize("It's recommended that you check the settings to adjust them to your liking before joining a server."),
			Localize("Please enter your nickname below."));
		pExtraText = aBuf;
		pButtonText = Localize("Ok");
		TopAlign = true;
	}
	else if(m_Popup == POPUP_JOIN_TUTORIAL)
	{
		pTitle = Localize("Joining Tutorial server");
	}
	else if(m_Popup == POPUP_POINTS)
	{
		pTitle = Localize("Existing Player");
		if(Client()->InfoState() == IClient::EInfoState::SUCCESS && Client()->Points() > 50)
		{
			str_format(aBuf, sizeof(aBuf), Localize("Your nickname '%s' is already used (%d points). Do you still want to use it?"), Client()->PlayerName(), Client()->Points());
			pExtraText = aBuf;
			TopAlign = true;
		}
		else
		{
			pExtraText = Localize("Checking for existing player with your name");
		}
	}
	else if(m_Popup == POPUP_WARNING)
	{
		BgColor = ColorRGBA(0.5f, 0.0f, 0.0f, 0.7f);
		pTitle = m_aMessageTopic;
		pExtraText = m_aMessageBody;
		pButtonText = m_aMessageButton;
		TopAlign = true;
	}
	else if(m_Popup == POPUP_SAVE_SKIN)
	{
		pTitle = Localize("Save skin");
		pExtraText = Localize("Are you sure you want to save your skin? If a skin with this name already exists, it will be replaced.");
	}

	CUIRect Box, Part;
	Box = Screen;
	if(m_Popup != POPUP_FIRST_LAUNCH)
	{
		Box.Margin(150.0f, &Box);
	}

	// Background
	Box.Draw(BgColor, IGraphics::CORNER_ALL, 15.0f);

	// Title
	{
		CUIRect Title;
		Box.HSplitTop(20.0f, nullptr, &Box);
		Box.HSplitTop(24.0f, &Title, &Box);
		Box.HSplitTop(20.0f, nullptr, &Box);
		Title.VMargin(20.0f, &Title);

		const float TitleFontSize = 24.0f;
		if(TextRender()->TextWidth(TitleFontSize, pTitle) > Title.w)
			Ui()->DoLabel(&Title, pTitle, TitleFontSize, TEXTALIGN_ML, {.m_MaxWidth = Title.w});
		else
			Ui()->DoLabel(&Title, pTitle, TitleFontSize, TEXTALIGN_MC);
	}

	// Extra text (optional)
	if(m_Popup != POPUP_JOIN_TUTORIAL)
	{
		CUIRect ExtraText;
		Box.HSplitTop(24.0f, &ExtraText, &Box);
		ExtraText.VMargin(20.0f, &ExtraText);
		if(pExtraText[0] != '\0')
		{
			const float ExtraTextFontSize = m_Popup == POPUP_FIRST_LAUNCH ? 16.0f : 20.0f;

			if(TopAlign)
				Ui()->DoLabel(&ExtraText, pExtraText, ExtraTextFontSize, TEXTALIGN_TL, {.m_MaxWidth = ExtraText.w});
			else if(TextRender()->TextWidth(ExtraTextFontSize, pExtraText) > ExtraText.w)
				Ui()->DoLabel(&ExtraText, pExtraText, ExtraTextFontSize, TEXTALIGN_ML, {.m_MaxWidth = ExtraText.w});
			else
				Ui()->DoLabel(&ExtraText, pExtraText, ExtraTextFontSize, TEXTALIGN_MC);
		}
	}

	if(m_Popup == POPUP_MESSAGE || m_Popup == POPUP_CONFIRM)
	{
		if(m_Popup == POPUP_CONFIRM && m_PopupConfirmHasCheckbox)
		{
			CUIRect CheckboxRow;
			Box.HSplitBottom(24.0f, &Box, &CheckboxRow);
			CheckboxRow.VMargin(100.0f, &CheckboxRow);
			static CButtonContainer s_PopupCheckbox;
			if(DoButton_CheckBox(&s_PopupCheckbox, m_aPopupCheckboxLabel, m_PopupConfirmCheckboxValue, &CheckboxRow))
				m_PopupConfirmCheckboxValue = !m_PopupConfirmCheckboxValue;
		}

		CUIRect ButtonBar;
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &ButtonBar);
		ButtonBar.VMargin(100.0f, &ButtonBar);

		if(m_Popup == POPUP_MESSAGE)
		{
			static CButtonContainer s_ButtonConfirm;
			if(DoButton_Menu(&s_ButtonConfirm, m_aPopupButtons[BUTTON_CONFIRM].m_aLabel, 0, &ButtonBar) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
			{
				m_Popup = m_aPopupButtons[BUTTON_CONFIRM].m_NextPopup;
				(this->*m_aPopupButtons[BUTTON_CONFIRM].m_pfnCallback)();
			}
		}
		else if(m_Popup == POPUP_CONFIRM)
		{
			CUIRect CancelButton, ConfirmButton;
			ButtonBar.VSplitMid(&CancelButton, &ConfirmButton, 40.0f);

			static CButtonContainer s_ButtonCancel;
			if(DoButton_Menu(&s_ButtonCancel, m_aPopupButtons[BUTTON_CANCEL].m_aLabel, 0, &CancelButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			{
				m_Popup = m_aPopupButtons[BUTTON_CANCEL].m_NextPopup;
				(this->*m_aPopupButtons[BUTTON_CANCEL].m_pfnCallback)();
			}

			static CButtonContainer s_ButtonConfirm;
			if(DoButton_Menu(&s_ButtonConfirm, m_aPopupButtons[BUTTON_CONFIRM].m_aLabel, 0, &ConfirmButton) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
			{
				m_Popup = m_aPopupButtons[BUTTON_CONFIRM].m_NextPopup;
				(this->*m_aPopupButtons[BUTTON_CONFIRM].m_pfnCallback)();
			}
		}
	}
	else if(m_Popup == POPUP_QUIT || m_Popup == POPUP_RESTART)
	{
		CUIRect Yes, No;
		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);

		// additional info
		Box.VMargin(20.f, &Box);
		if(GameClient()->Editor()->HasUnsavedData())
		{
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s", Localize("There's an unsaved map in the editor, you might want to save it."), Localize("Continue anyway?"));
			Ui()->DoLabel(&Box, aBuf, 20.0f, TEXTALIGN_ML, {.m_MaxWidth = Part.w - 20.0f});
		}
		else if(GameClient()->m_TouchControls.HasEditingChanges() || m_MenusIngameTouchControls.UnsavedChanges())
		{
			str_format(aBuf, sizeof(aBuf), "%s\n\n%s", Localize("There's an unsaved change in the touch controls editor, you might want to save it."), Localize("Continue anyway?"));
			Ui()->DoLabel(&Box, aBuf, 20.0f, TEXTALIGN_ML, {.m_MaxWidth = Part.w - 20.0f});
		}

		// buttons
		Part.VMargin(80.0f, &Part);
		Part.VSplitMid(&No, &Yes);
		Yes.VMargin(20.0f, &Yes);
		No.VMargin(20.0f, &No);

		static CButtonContainer s_ButtonAbort;
		if(DoButton_Menu(&s_ButtonAbort, Localize("No"), 0, &No) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			m_Popup = POPUP_NONE;

		static CButtonContainer s_ButtonTryAgain;
		if(DoButton_Menu(&s_ButtonTryAgain, Localize("Yes"), 0, &Yes) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			if(m_Popup == POPUP_RESTART)
			{
				m_Popup = POPUP_NONE;
				Client()->Restart();
			}
			else
			{
				m_Popup = POPUP_NONE;
				Client()->Quit();
			}
		}
	}
	else if(m_Popup == POPUP_PASSWORD)
	{
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Part);
		Part.VMargin(100.0f, &Part);

		CUIRect TryAgain, Abort;
		Part.VSplitMid(&Abort, &TryAgain, 40.0f);

		static CButtonContainer s_ButtonAbort;
		if(DoButton_Menu(&s_ButtonAbort, Localize("Abort"), 0, &Abort) ||
			Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		{
			m_Popup = POPUP_NONE;
		}

		char aAddr[NETADDR_MAXSTRSIZE];
		net_addr_str(&Client()->ServerAddress(), aAddr, sizeof(aAddr), true);

		static CButtonContainer s_ButtonTryAgain;
		if(DoButton_Menu(&s_ButtonTryAgain, Localize("Try again"), 0, &TryAgain) ||
			Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			Client()->Connect(aAddr, g_Config.m_Password);
		}

		Box.VMargin(60.0f, &Box);
		Box.HSplitBottom(32.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Part);

		CUIRect Label, TextBox;
		Part.VSplitLeft(100.0f, &Label, &TextBox);
		TextBox.VSplitLeft(20.0f, nullptr, &TextBox);
		Ui()->DoLabel(&Label, Localize("Password"), 18.0f, TEXTALIGN_ML);
		Ui()->DoClearableEditBox(&m_PasswordInput, &TextBox, 12.0f);

		Box.HSplitBottom(32.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Part);

		CUIRect Address;
		Part.VSplitLeft(100.0f, &Label, &Address);
		Address.VSplitLeft(20.0f, nullptr, &Address);
		Ui()->DoLabel(&Label, Localize("Address"), 18.0f, TEXTALIGN_ML);
		Ui()->DoLabel(&Address, aAddr, 18.0f, TEXTALIGN_ML);

		const CServerBrowser::CServerEntry *pEntry = ServerBrowser()->Find(Client()->ServerAddress());
		if(pEntry != nullptr && pEntry->m_GotInfo)
		{
			const CCommunity *pCommunity = ServerBrowser()->Community(pEntry->m_Info.m_aCommunityId);
			const CCommunityIcon *pIcon = pCommunity == nullptr ? nullptr : m_CommunityIcons.Find(pCommunity->Id());

			Box.HSplitBottom(32.0f, &Box, nullptr);
			Box.HSplitBottom(24.0f, &Box, &Part);

			CUIRect Name;
			Part.VSplitLeft(100.0f, &Label, &Name);
			Name.VSplitLeft(20.0f, nullptr, &Name);
			if(pIcon != nullptr)
			{
				CUIRect Icon;
				static char s_CommunityTooltipButtonId;
				Name.VSplitLeft(2.5f * Name.h, &Icon, &Name);
				m_CommunityIcons.Render(pIcon, Icon, true);
				Ui()->DoButtonLogic(&s_CommunityTooltipButtonId, 0, &Icon, BUTTONFLAG_NONE);
				GameClient()->m_Tooltips.DoToolTip(&s_CommunityTooltipButtonId, &Icon, pCommunity->Name());
			}

			Ui()->DoLabel(&Label, Localize("Name"), 18.0f, TEXTALIGN_ML);
			Ui()->DoLabel(&Name, pEntry->m_Info.m_aName, 18.0f, TEXTALIGN_ML);
		}
	}
	else if(m_Popup == POPUP_LANGUAGE)
	{
		CUIRect Button;
		Screen.Margin(150.0f, &Box);
		Box.HSplitTop(20.0f, nullptr, &Box);
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Button);
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.VMargin(20.0f, &Box);
		const bool Activated = RenderLanguageSelection(Box);
		Button.VMargin(120.0f, &Button);

		static CButtonContainer s_Button;
		if(DoButton_Menu(&s_Button, Localize("Ok"), 0, &Button) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || Activated)
			m_Popup = POPUP_FIRST_LAUNCH;
	}
	else if(m_Popup == POPUP_RENAME_DEMO)
	{
		CUIRect Label, TextBox, Ok, Abort;

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(80.0f, &Part);

		Part.VSplitMid(&Abort, &Ok);

		Ok.VMargin(20.0f, &Ok);
		Abort.VMargin(20.0f, &Abort);

		static CButtonContainer s_ButtonAbort;
		if(DoButton_Menu(&s_ButtonAbort, Localize("Abort"), 0, &Abort) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			m_Popup = POPUP_NONE;

		static CButtonContainer s_ButtonOk;
		if(DoButton_Menu(&s_ButtonOk, Localize("Ok"), 0, &Ok) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			m_Popup = POPUP_NONE;
			// rename demo
			char aBufOld[IO_MAX_PATH_LENGTH];
			str_format(aBufOld, sizeof(aBufOld), "%s/%s", m_aCurrentDemoFolder, m_vpFilteredDemos[m_DemolistSelectedIndex]->m_aFilename);
			char aBufNew[IO_MAX_PATH_LENGTH];
			str_format(aBufNew, sizeof(aBufNew), "%s/%s", m_aCurrentDemoFolder, m_DemoRenameInput.GetString());
			if(!m_vpFilteredDemos[m_DemolistSelectedIndex]->m_IsDir && !str_endswith(aBufNew, ".demo"))
				str_append(aBufNew, ".demo");

			if(str_comp(aBufOld, aBufNew) == 0)
			{
				// Nothing to rename, also same capitalization
			}
			else if(!str_valid_filename(m_DemoRenameInput.GetString()))
			{
				PopupMessage(Localize("Error"), Localize("This name cannot be used for files and folders"), Localize("Ok"), POPUP_RENAME_DEMO);
			}
			else if(str_utf8_comp_nocase(aBufOld, aBufNew) != 0 && // Allow renaming if it only changes capitalization to support case-insensitive filesystems
				Storage()->FileExists(aBufNew, m_vpFilteredDemos[m_DemolistSelectedIndex]->m_StorageType))
			{
				PopupMessage(Localize("Error"), Localize("A demo with this name already exists"), Localize("Ok"), POPUP_RENAME_DEMO);
			}
			else if(Storage()->FolderExists(aBufNew, m_vpFilteredDemos[m_DemolistSelectedIndex]->m_StorageType))
			{
				PopupMessage(Localize("Error"), Localize("A folder with this name already exists"), Localize("Ok"), POPUP_RENAME_DEMO);
			}
			else if(Storage()->RenameFile(aBufOld, aBufNew, m_vpFilteredDemos[m_DemolistSelectedIndex]->m_StorageType))
			{
				str_copy(m_aCurrentDemoSelectionName, m_DemoRenameInput.GetString());
				if(!m_vpFilteredDemos[m_DemolistSelectedIndex]->m_IsDir)
					fs_split_file_extension(m_DemoRenameInput.GetString(), m_aCurrentDemoSelectionName, sizeof(m_aCurrentDemoSelectionName));
				DemolistPopulate();
				DemolistOnUpdate(false);
			}
			else
			{
				PopupMessage(Localize("Error"), m_vpFilteredDemos[m_DemolistSelectedIndex]->m_IsDir ? Localize("Unable to rename the folder") : Localize("Unable to rename the demo"), Localize("Ok"), POPUP_RENAME_DEMO);
			}
		}

		Box.HSplitBottom(60.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);

		Part.VSplitLeft(60.0f, nullptr, &Label);
		Label.VSplitLeft(120.0f, nullptr, &TextBox);
		TextBox.VSplitLeft(20.0f, nullptr, &TextBox);
		TextBox.VSplitRight(60.0f, &TextBox, nullptr);
		Ui()->DoLabel(&Label, Localize("New name:"), 18.0f, TEXTALIGN_ML);
		Ui()->DoEditBox(&m_DemoRenameInput, &TextBox, 12.0f);
	}
#if defined(CONF_VIDEORECORDER)
	else if(m_Popup == POPUP_RENDER_DEMO)
	{
		CUIRect Row, Ok, Abort;
		Box.VMargin(60.0f, &Box);
		Box.HMargin(20.0f, &Box);
		Box.HSplitBottom(24.0f, &Box, &Row);
		Box.HSplitBottom(40.0f, &Box, nullptr);
		Row.VMargin(40.0f, &Row);
		Row.VSplitMid(&Abort, &Ok, 40.0f);

		static CButtonContainer s_ButtonAbort;
		if(DoButton_Menu(&s_ButtonAbort, Localize("Abort"), 0, &Abort) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		{
			m_DemoRenderInput.Clear();
			m_Popup = POPUP_NONE;
		}

		static CButtonContainer s_ButtonOk;
		if(DoButton_Menu(&s_ButtonOk, Localize("Ok"), 0, &Ok) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			m_Popup = POPUP_NONE;
			// render video
			char aVideoPath[IO_MAX_PATH_LENGTH];
			str_format(aVideoPath, sizeof(aVideoPath), "videos/%s", m_DemoRenderInput.GetString());
			if(!str_endswith(aVideoPath, ".mp4"))
				str_append(aVideoPath, ".mp4");

			if(!str_valid_filename(m_DemoRenderInput.GetString()))
			{
				PopupMessage(Localize("Error"), Localize("This name cannot be used for files and folders"), Localize("Ok"), POPUP_RENDER_DEMO);
			}
			else if(Storage()->FolderExists(aVideoPath, IStorage::TYPE_SAVE))
			{
				PopupMessage(Localize("Error"), Localize("A folder with this name already exists"), Localize("Ok"), POPUP_RENDER_DEMO);
			}
			else if(Storage()->FileExists(aVideoPath, IStorage::TYPE_SAVE))
			{
				char aMessage[128 + IO_MAX_PATH_LENGTH];
				str_format(aMessage, sizeof(aMessage), Localize("File '%s' already exists, do you want to overwrite it?"), m_DemoRenderInput.GetString());
				PopupConfirm(Localize("Replace video"), aMessage, Localize("Yes"), Localize("No"), &CMenus::PopupConfirmDemoReplaceVideo, POPUP_NONE, &CMenus::DefaultButtonCallback, POPUP_RENDER_DEMO);
			}
			else
			{
				PopupConfirmDemoReplaceVideo();
			}
		}

		CUIRect ShowChatCheckbox, UseSoundsCheckbox;
		Box.HSplitBottom(20.0f, &Box, &Row);
		Box.HSplitBottom(10.0f, &Box, nullptr);
		Row.VSplitMid(&ShowChatCheckbox, &UseSoundsCheckbox, 20.0f);

		if(DoButton_CheckBox(&g_Config.m_ClVideoShowChat, Localize("Show chat"), g_Config.m_ClVideoShowChat, &ShowChatCheckbox))
			g_Config.m_ClVideoShowChat ^= 1;

		if(DoButton_CheckBox(&g_Config.m_ClVideoSndEnable, Localize("Use sounds"), g_Config.m_ClVideoSndEnable, &UseSoundsCheckbox))
			g_Config.m_ClVideoSndEnable ^= 1;

		CUIRect ShowHudButton;
		Box.HSplitBottom(20.0f, &Box, &Row);
		Row.VSplitMid(&Row, &ShowHudButton, 20.0f);

		if(DoButton_CheckBox(&g_Config.m_ClVideoShowhud, Localize("Show ingame HUD"), g_Config.m_ClVideoShowhud, &ShowHudButton))
			g_Config.m_ClVideoShowhud ^= 1;

		// slowdown
		CUIRect SlowDownButton;
		Row.VSplitLeft(20.0f, &SlowDownButton, &Row);
		Row.VSplitLeft(5.0f, nullptr, &Row);
		static CButtonContainer s_SlowDownButton;
		if(Ui()->DoButton_FontIcon(&s_SlowDownButton, FontIcon::BACKWARD, 0, &SlowDownButton, BUTTONFLAG_LEFT))
			m_Speed = std::clamp(m_Speed - 1, 0, (int)(std::size(DEMO_SPEEDS) - 1));

		// paused
		CUIRect PausedButton;
		Row.VSplitLeft(20.0f, &PausedButton, &Row);
		Row.VSplitLeft(5.0f, nullptr, &Row);
		static CButtonContainer s_PausedButton;
		if(Ui()->DoButton_FontIcon(&s_PausedButton, FontIcon::PAUSE, 0, &PausedButton, BUTTONFLAG_LEFT))
			m_StartPaused ^= 1;

		// fastforward
		CUIRect FastForwardButton;
		Row.VSplitLeft(20.0f, &FastForwardButton, &Row);
		Row.VSplitLeft(8.0f, nullptr, &Row);
		static CButtonContainer s_FastForwardButton;
		if(Ui()->DoButton_FontIcon(&s_FastForwardButton, FontIcon::FORWARD, 0, &FastForwardButton, BUTTONFLAG_LEFT))
			m_Speed = std::clamp(m_Speed + 1, 0, (int)(std::size(DEMO_SPEEDS) - 1));

		// speed meter
		char aBuffer[128];
		const char *pPaused = m_StartPaused ? Localize("(paused)") : "";
		str_format(aBuffer, sizeof(aBuffer), "%s: ×%g %s", Localize("Speed"), DEMO_SPEEDS[m_Speed], pPaused);
		Ui()->DoLabel(&Row, aBuffer, 12.8f, TEXTALIGN_ML);
		Box.HSplitBottom(16.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Row);

		CUIRect Label, TextBox;
		Row.VSplitLeft(110.0f, &Label, &TextBox);
		TextBox.VSplitLeft(10.0f, nullptr, &TextBox);
		Ui()->DoLabel(&Label, Localize("Video name:"), 12.8f, TEXTALIGN_ML);
		Ui()->DoEditBox(&m_DemoRenderInput, &TextBox, 12.8f);

		// Warn about disconnect if online
		if(Client()->State() == IClient::STATE_ONLINE)
		{
			Box.HSplitBottom(10.0f, &Box, nullptr);
			Box.HSplitBottom(20.0f, &Box, &Row);
			SLabelProperties LabelProperties;
			LabelProperties.SetColor(ColorRGBA(1.0f, 0.0f, 0.0f));
			Ui()->DoLabel(&Row, Localize("You will be disconnected from the server."), 12.8f, TEXTALIGN_MC, LabelProperties);
		}
	}
	else if(m_Popup == POPUP_RENDER_DONE)
	{
		CUIRect Ok, OpenFolder;

		char aFilePath[IO_MAX_PATH_LENGTH];
		char aSaveFolder[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "videos", aSaveFolder, sizeof(aSaveFolder));
		str_format(aFilePath, sizeof(aFilePath), "%s/%s.mp4", aSaveFolder, m_DemoRenderInput.GetString());

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(80.0f, &Part);

		Part.VSplitMid(&OpenFolder, &Ok);

		Ok.VMargin(20.0f, &Ok);
		OpenFolder.VMargin(20.0f, &OpenFolder);

		static CButtonContainer s_ButtonOpenFolder;
		if(DoButton_Menu(&s_ButtonOpenFolder, Localize("Videos directory"), 0, &OpenFolder))
		{
			Client()->ViewFile(aSaveFolder);
		}

		static CButtonContainer s_ButtonOk;
		if(DoButton_Menu(&s_ButtonOk, Localize("Ok"), 0, &Ok) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			m_Popup = POPUP_NONE;
			m_DemoRenderInput.Clear();
		}

		Box.HSplitBottom(160.f, &Box, &Part);
		Part.VMargin(20.0f, &Part);

		str_format(aBuf, sizeof(aBuf), Localize("Video was saved to '%s'"), aFilePath);

		SLabelProperties MessageProps;
		MessageProps.m_MaxWidth = (int)Part.w;
		Ui()->DoLabel(&Part, aBuf, 18.0f, TEXTALIGN_TL, MessageProps);
	}
#endif
	else if(m_Popup == POPUP_FIRST_LAUNCH)
	{
		CUIRect Label, TextBox, Skip, Join;

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(80.0f, &Part);
		Part.VSplitMid(&Skip, &Join);
		Skip.VMargin(20.0f, &Skip);
		Join.VMargin(20.0f, &Join);

		static CButtonContainer s_JoinTutorialButton;
		if(DoButton_Menu(&s_JoinTutorialButton, Localize("Join Tutorial Server"), 0, &Join) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			Client()->RequestDDNetInfo();
			m_Popup = g_Config.m_BrIndicateFinished ? POPUP_POINTS : POPUP_NONE;
			JoinTutorial();
		}

		static CButtonContainer s_SkipTutorialButton;
		if(DoButton_Menu(&s_SkipTutorialButton, Localize("Skip Tutorial"), 0, &Skip) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		{
			Client()->RequestDDNetInfo();
			m_Popup = g_Config.m_BrIndicateFinished ? POPUP_POINTS : POPUP_NONE;
		}

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);

		Part.VSplitLeft(30.0f, nullptr, &Part);
		str_format(aBuf, sizeof(aBuf), "%s\n(%s)",
			Localize("Show DDNet map finishes in server browser"),
			Localize("transmits your player name to info.ddnet.org"));

		if(DoButton_CheckBox(&g_Config.m_BrIndicateFinished, aBuf, g_Config.m_BrIndicateFinished, &Part))
			g_Config.m_BrIndicateFinished ^= 1;

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);

		Part.VSplitLeft(60.0f, nullptr, &Label);
		Label.VSplitLeft(100.0f, nullptr, &TextBox);
		TextBox.VSplitLeft(20.0f, nullptr, &TextBox);
		TextBox.VSplitRight(60.0f, &TextBox, nullptr);
		Ui()->DoLabel(&Label, Localize("Nickname"), 16.0f, TEXTALIGN_ML);
		static CLineInput s_PlayerNameInput(g_Config.m_PlayerName, sizeof(g_Config.m_PlayerName));
		s_PlayerNameInput.SetEmptyText(Client()->PlayerName());
		Ui()->DoEditBox(&s_PlayerNameInput, &TextBox, 12.0f);
	}
	else if(m_Popup == POPUP_JOIN_TUTORIAL)
	{
		CUIRect ButtonBar, StatusLabel, ProgressLabel, ProgressIndicator;
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &ButtonBar);
		ButtonBar.VMargin(120.0f, &ButtonBar);
		Box.HSplitBottom(20.0f, &StatusLabel, nullptr);
		StatusLabel.VMargin(20.0f, &StatusLabel);
		StatusLabel.HSplitMid(&StatusLabel, &ProgressLabel);
		ProgressLabel.VSplitLeft(50.0f, &ProgressIndicator, &ProgressLabel);

		if(m_JoinTutorial.m_Status == CJoinTutorial::EStatus::REFRESHING)
		{
			if(ServerBrowser()->IsGettingServerlist() ||
				Client()->InfoState() == IClient::EInfoState::LOADING)
			{
				// Still refreshing
			}
			else if(ServerBrowser()->IsServerlistError() ||
				Client()->InfoState() == IClient::EInfoState::ERROR)
			{
				m_JoinTutorial.m_Status = CJoinTutorial::EStatus::SERVER_LIST_ERROR;
			}
			else
			{
				const char *pAddr = ServerBrowser()->GetTutorialServer();
				if(pAddr)
				{
					Client()->Connect(pAddr);
				}
				else
				{
					m_JoinTutorial.m_Status = CJoinTutorial::EStatus::NO_TUTORIAL_AVAILABLE;
				}
			}
		}

		const char *pStatusLabel = nullptr;
		switch(m_JoinTutorial.m_Status)
		{
		case CJoinTutorial::EStatus::REFRESHING:
			pStatusLabel = Localize("Getting server list from master server");
			break;
		case CJoinTutorial::EStatus::SERVER_LIST_ERROR:
			pStatusLabel = Localize("Could not get server list from master server");
			break;
		case CJoinTutorial::EStatus::NO_TUTORIAL_AVAILABLE:
			pStatusLabel = Localize("There are no Tutorial servers available");
			break;
		}
		if(pStatusLabel != nullptr)
		{
			Ui()->DoLabel(&StatusLabel, pStatusLabel, 20.0f, TEXTALIGN_ML);
		}

		const char *pProgressLabel = nullptr;
		bool ProgressDeterminate = true;
		const float LastStateChangeSeconds = std::chrono::duration_cast<std::chrono::duration<float>>(time_get_nanoseconds() - m_JoinTutorial.m_StateChange).count();
		constexpr float RefreshDelay = 5.0f;

		if(m_JoinTutorial.m_Status == CJoinTutorial::EStatus::REFRESHING)
		{
			pProgressLabel = Localize("Please wait…");
			ProgressDeterminate = false;
		}
		else if(!m_JoinTutorial.m_TryRefresh)
		{
			if(!m_JoinTutorial.m_TriedRefresh)
			{
				m_JoinTutorial.m_TryRefresh = true;
				m_JoinTutorial.m_StateChange = time_get_nanoseconds();
			}
			else if(m_JoinTutorial.m_LocalServerState == CJoinTutorial::ELocalServerState::NOT_TRIED)
			{
				m_JoinTutorial.m_LocalServerState = CJoinTutorial::ELocalServerState::TRY;
				m_JoinTutorial.m_StateChange = time_get_nanoseconds();
			}
		}

		if(m_JoinTutorial.m_TryRefresh)
		{
			if(LastStateChangeSeconds >= RefreshDelay)
			{
				// Activate internet tab before joining tutorial to make sure the server info
				// for the tutorial servers is available.
				GameClient()->m_Menus.SetMenuPage(CMenus::PAGE_INTERNET);
				GameClient()->m_Menus.RefreshBrowserTab(true);
				m_JoinTutorial.m_Status = CJoinTutorial::EStatus::REFRESHING;
				m_JoinTutorial.m_TryRefresh = false;
				m_JoinTutorial.m_TriedRefresh = true;
				m_JoinTutorial.m_StateChange = time_get_nanoseconds();
			}
			else
			{
				pProgressLabel = Localize("Retrying…");
			}
		}

		const auto &&ShowFinalErrorMessage = [&]() {
			PopupMessage(Localize("Error joining Tutorial server"), Localize("Could not find a Tutorial server. Check your internet connection."), Localize("Ok"));
		};
		const auto &&RunServer = [&]() {
			char aMotd[256];
			str_copy(aMotd, "sv_motd \"");
			char *pDst = aMotd + str_length(aMotd);
			str_escape(&pDst, Localize("You're playing on a local server because no online Tutorial server could be found.\n\nYour record will only be saved locally."), aMotd + sizeof(aMotd) - 1);
			str_append(aMotd, "\"");
			if(GameClient()->m_LocalServer.RunServer({"sv_register 0", "sv_map Tutorial", aMotd}))
			{
				m_JoinTutorial.m_LocalServerState = CJoinTutorial::ELocalServerState::WAITING_START;
				m_JoinTutorial.m_StateChange = time_get_nanoseconds();
			}
			else
			{
				ShowFinalErrorMessage();
			}
		};
		if(m_JoinTutorial.m_LocalServerState == CJoinTutorial::ELocalServerState::TRY)
		{
			if(LastStateChangeSeconds >= RefreshDelay)
			{
				if(GameClient()->m_LocalServer.IsServerRunning())
				{
					GameClient()->m_LocalServer.KillServer();
					m_JoinTutorial.m_LocalServerState = CJoinTutorial::ELocalServerState::WAITING_STOP;
					m_JoinTutorial.m_StateChange = time_get_nanoseconds();
				}
				else
				{
					RunServer();
				}
			}
			else
			{
				pProgressLabel = Localize("Could not find online Tutorial server.\nStarting and connecting to local server…");
			}
		}
		else if(m_JoinTutorial.m_LocalServerState == CJoinTutorial::ELocalServerState::WAITING_STOP)
		{
			if(LastStateChangeSeconds >= 5.0f)
			{
				ShowFinalErrorMessage();
			}
			else
			{
				if(!GameClient()->m_LocalServer.IsServerRunning())
				{
					RunServer();
				}

				pProgressLabel = Localize("Waiting for local server to stop…");
				ProgressDeterminate = false;
			}
		}
		else if(m_JoinTutorial.m_LocalServerState == CJoinTutorial::ELocalServerState::WAITING_START)
		{
			if(LastStateChangeSeconds >= 5.0f)
			{
				ShowFinalErrorMessage();
			}
			else
			{
				if(LastStateChangeSeconds >= 2.0f &&
					GameClient()->m_LocalServer.IsServerRunning())
				{
					Client()->Connect("localhost");
				}

				pProgressLabel = Localize("Waiting for local server to start…");
				ProgressDeterminate = false;
			}
		}

		if(pProgressLabel != nullptr)
		{
			Ui()->RenderProgressSpinner(ProgressIndicator.Center(), 12.0f, {.m_Progress = ProgressDeterminate ? (LastStateChangeSeconds / RefreshDelay) : -1.0f});
			Ui()->DoLabel(&ProgressLabel, pProgressLabel, 20.0f, TEXTALIGN_ML);
		}

		static CButtonContainer s_Button;
		if(DoButton_Menu(&s_Button, Localize("Cancel"), 0, &ButtonBar) ||
			Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) ||
			Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			m_Popup = POPUP_NONE;
		}
	}
	else if(m_Popup == POPUP_POINTS)
	{
		Box.HSplitBottom(20.0f, &Box, nullptr);
		Box.HSplitBottom(24.0f, &Box, &Part);
		Part.VMargin(120.0f, &Part);

		if(Client()->InfoState() == IClient::EInfoState::SUCCESS && Client()->Points() > 50)
		{
			CUIRect Yes, No;
			Part.VSplitMid(&No, &Yes, 40.0f);
			static CButtonContainer s_ButtonNo;
			if(DoButton_Menu(&s_ButtonNo, Localize("No"), 0, &No) ||
				Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			{
				m_Popup = POPUP_FIRST_LAUNCH;
			}

			static CButtonContainer s_ButtonYes;
			if(DoButton_Menu(&s_ButtonYes, Localize("Yes"), 0, &Yes) ||
				Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
			{
				m_Popup = POPUP_NONE;
			}
		}
		else
		{
			static CButtonContainer s_Button;
			if(DoButton_Menu(&s_Button, Localize("Cancel"), 0, &Part) ||
				Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) ||
				Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) ||
				Client()->InfoState() == IClient::EInfoState::SUCCESS)
			{
				m_Popup = POPUP_NONE;
			}
			if(Client()->InfoState() == IClient::EInfoState::ERROR)
			{
				PopupMessage(Localize("Error checking player name"), Localize("Could not check for existing player with your name. Check your internet connection."), Localize("Ok"));
			}
		}
	}
	else if(m_Popup == POPUP_WARNING)
	{
		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(120.0f, &Part);

		static CButtonContainer s_Button;
		if(DoButton_Menu(&s_Button, pButtonText, 0, &Part) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || (m_PopupWarningDuration > 0s && time_get_nanoseconds() - m_PopupWarningLastTime >= m_PopupWarningDuration))
		{
			m_Popup = POPUP_NONE;
			SetActive(false);
		}
	}
	else if(m_Popup == POPUP_SAVE_SKIN)
	{
		CUIRect Label, TextBox, Yes, No;

		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(80.0f, &Part);

		Part.VSplitMid(&No, &Yes);

		Yes.VMargin(20.0f, &Yes);
		No.VMargin(20.0f, &No);

		static CButtonContainer s_ButtonNo;
		if(DoButton_Menu(&s_ButtonNo, Localize("No"), 0, &No) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
			m_Popup = POPUP_NONE;

		static CButtonContainer s_ButtonYes;
		if(DoButton_Menu(&s_ButtonYes, Localize("Yes"), m_SkinNameInput.IsEmpty() ? 1 : 0, &Yes) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			if(!str_valid_filename(m_SkinNameInput.GetString()))
			{
				PopupMessage(Localize("Error"), Localize("This name cannot be used for files and folders"), Localize("Ok"), POPUP_SAVE_SKIN);
			}
			else if(CSkins7::IsSpecialSkin(m_SkinNameInput.GetString()))
			{
				PopupMessage(Localize("Error"), Localize("Unable to save the skin with a reserved name"), Localize("Ok"), POPUP_SAVE_SKIN);
			}
			else if(!GameClient()->m_Skins7.SaveSkinfile(m_SkinNameInput.GetString(), m_Dummy))
			{
				PopupMessage(Localize("Error"), Localize("Unable to save the skin"), Localize("Ok"), POPUP_SAVE_SKIN);
			}
			else
			{
				m_Popup = POPUP_NONE;
				m_SkinList7LastRefreshTime = std::nullopt;
			}
		}

		Box.HSplitBottom(60.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);

		Part.VMargin(60.0f, &Label);
		Label.VSplitLeft(100.0f, &Label, &TextBox);
		TextBox.VSplitLeft(20.0f, nullptr, &TextBox);
		Ui()->DoLabel(&Label, Localize("Name"), 18.0f, TEXTALIGN_ML);
		Ui()->DoClearableEditBox(&m_SkinNameInput, &TextBox, 12.0f);
	}
	else
	{
		Box.HSplitBottom(20.f, &Box, &Part);
		Box.HSplitBottom(24.f, &Box, &Part);
		Part.VMargin(120.0f, &Part);

		static CButtonContainer s_Button;
		if(DoButton_Menu(&s_Button, pButtonText, 0, &Part) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
		{
			if(m_Popup == POPUP_DISCONNECTED && Client()->ReconnectTime() > 0)
				Client()->SetReconnectTime(0);
			m_Popup = POPUP_NONE;
		}
	}

	if(m_Popup == POPUP_NONE)
		Ui()->SetActiveItem(nullptr);
}

void CMenus::RenderPopupConnecting(CUIRect Screen)
{
	(void)Screen;
	const float FontSize = 20.0f;
	const float BackgroundColor = 21.0f / 255.0f;

	CUIRect FullScreen = *Ui()->Screen();
	FullScreen.Draw(ColorRGBA(BackgroundColor, BackgroundColor, BackgroundColor, 1.0f), IGraphics::CORNER_ALL, 0.0f);

	Graphics()->TextureSet(m_BcLogoTexture.IsValid() && !m_BcLogoTexture.IsNullTexture() ? m_BcLogoTexture : g_pData->m_aImages[IMAGE_BANNER].m_Id);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	const float LogoWidth = minimum(420.0f, maximum(220.0f, FullScreen.w * 0.38f));
	const float LogoHeight = LogoWidth / (360.0f / 103.0f);
	const float LogoY = FullScreen.y + minimum(70.0f, FullScreen.h * 0.12f);
	IGraphics::CQuadItem LogoQuad(FullScreen.x + (FullScreen.w - LogoWidth) / 2.0f, LogoY, LogoWidth, LogoHeight);
	Graphics()->QuadsDrawTL(&LogoQuad, 1);
	Graphics()->QuadsEnd();

	const float LabelWidth = minimum(860.0f, FullScreen.w - 80.0f);
	CUIRect Label;
	Label.x = FullScreen.x + (FullScreen.w - LabelWidth) / 2.0f;
	Label.y = FullScreen.y + FullScreen.h * 0.5f - 28.0f;
	Label.w = LabelWidth;
	Label.h = 24.0f;

	Ui()->DoLabel(&Label, Localize("Connecting to"), 24.0f, TEXTALIGN_MC);

	Label.y += 32.0f;
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	Props.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&Label, Client()->ConnectAddressString(), FontSize, TEXTALIGN_MC, Props);

	if(time_get() - Client()->StateStartTime() > time_freq())
	{
		const char *pConnectivityLabel = "";
		switch(Client()->UdpConnectivity(Client()->ConnectNetTypes()))
		{
		case IClient::CONNECTIVITY_UNKNOWN:
			break;
		case IClient::CONNECTIVITY_CHECKING:
			pConnectivityLabel = Localize("Trying to determine UDP connectivity…");
			break;
		case IClient::CONNECTIVITY_UNREACHABLE:
			pConnectivityLabel = Localize("UDP seems to be filtered.");
			break;
		case IClient::CONNECTIVITY_DIFFERING_UDP_TCP_IP_ADDRESSES:
			pConnectivityLabel = Localize("UDP and TCP IP addresses seem to be different. Try disabling VPN, proxy or network accelerators.");
			break;
		case IClient::CONNECTIVITY_REACHABLE:
			pConnectivityLabel = Localize("No answer from server yet.");
			break;
		}
		if(pConnectivityLabel[0] != '\0')
		{
			Label.y += 32.0f;
			SLabelProperties ConnectivityLabelProps;
			ConnectivityLabelProps.m_MaxWidth = Label.w;
			if(TextRender()->TextWidth(FontSize, pConnectivityLabel) > Label.w)
				Ui()->DoLabel(&Label, pConnectivityLabel, FontSize, TEXTALIGN_ML, ConnectivityLabelProps);
			else
				Ui()->DoLabel(&Label, pConnectivityLabel, FontSize, TEXTALIGN_MC);
		}
	}

	CUIRect Button;
	Button.w = FullScreen.w - 16.0f;
	Button.h = 36.0f;
	Button.x = FullScreen.x + 8.0f;
	Button.y = FullScreen.y + FullScreen.h - Button.h - 8.0f;

	static CButtonContainer s_Button;
	if(DoButton_Menu(&s_Button, Localize("Cancel"), 0, &Button) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		Client()->Disconnect();
		Ui()->SetActiveItem(nullptr);
		RefreshBrowserTab(true);
	}
}

void CMenus::RenderPopupLoading(CUIRect Screen)
{
	(void)Screen;
	char aTitle[256];
	char aLabel1[128];
	char aLabel2[128];
	if(Client()->MapDownloadTotalsize() > 0)
	{
		const int64_t Now = time_get();
		if(Now - m_DownloadLastCheckTime >= time_freq())
		{
			if(m_DownloadLastCheckSize > Client()->MapDownloadAmount())
			{
				// map downloaded restarted
				m_DownloadLastCheckSize = 0;
			}

			// update download speed
			const float Diff = (Client()->MapDownloadAmount() - m_DownloadLastCheckSize) / ((int)((Now - m_DownloadLastCheckTime) / time_freq()));
			const float StartDiff = m_DownloadLastCheckSize - 0.0f;
			if(StartDiff + Diff > 0.0f)
				m_DownloadSpeed = (Diff / (StartDiff + Diff)) * (Diff / 1.0f) + (StartDiff / (Diff + StartDiff)) * m_DownloadSpeed;
			else
				m_DownloadSpeed = 0.0f;
			m_DownloadLastCheckTime = Now;
			m_DownloadLastCheckSize = Client()->MapDownloadAmount();
		}

		str_format(aTitle, sizeof(aTitle), "%s: %s", Localize("Downloading map"), Client()->MapDownloadName());

		str_format(aLabel1, sizeof(aLabel1), Localize("%d/%d KiB (%.1f KiB/s)"), Client()->MapDownloadAmount() / 1024, Client()->MapDownloadTotalsize() / 1024, m_DownloadSpeed / 1024.0f);

		const int SecondsLeft = maximum(1, m_DownloadSpeed > 0.0f ? static_cast<int>((Client()->MapDownloadTotalsize() - Client()->MapDownloadAmount()) / m_DownloadSpeed) : 1);
		const int MinutesLeft = SecondsLeft / 60;
		if(MinutesLeft > 0)
		{
			str_format(aLabel2, sizeof(aLabel2), MinutesLeft == 1 ? Localize("%i minute left") : Localize("%i minutes left"), MinutesLeft);
		}
		else
		{
			str_format(aLabel2, sizeof(aLabel2), SecondsLeft == 1 ? Localize("%i second left") : Localize("%i seconds left"), SecondsLeft);
		}
	}
	else
	{
		str_copy(aTitle, Localize("Connected"));
		switch(Client()->LoadingStateDetail())
		{
		case IClient::LOADING_STATE_DETAIL_INITIAL:
			str_copy(aLabel1, Localize("Getting game info"));
			break;
		case IClient::LOADING_STATE_DETAIL_LOADING_MAP:
			str_copy(aLabel1, Localize("Loading map file from storage"));
			break;
		case IClient::LOADING_STATE_DETAIL_LOADING_DEMO:
			str_copy(aLabel1, Localize("Loading demo file from storage"));
			break;
		case IClient::LOADING_STATE_DETAIL_SENDING_READY:
			str_copy(aLabel1, Localize("Requesting to join the game"));
			break;
		case IClient::LOADING_STATE_DETAIL_GETTING_READY:
			str_copy(aLabel1, Localize("Sending initial client info"));
			break;
		default:
			dbg_assert_failed("Invalid loading state %d for RenderPopupLoading", static_cast<int>(Client()->LoadingStateDetail()));
		}
		aLabel2[0] = '\0';
	}

	const float FontSize = 20.0f;
	const float BackgroundColor = 21.0f / 255.0f;

	CUIRect FullScreen = *Ui()->Screen();
	FullScreen.Draw(ColorRGBA(BackgroundColor, BackgroundColor, BackgroundColor, 1.0f), IGraphics::CORNER_ALL, 0.0f);

	Graphics()->TextureSet(m_BcLogoTexture.IsValid() && !m_BcLogoTexture.IsNullTexture() ? m_BcLogoTexture : g_pData->m_aImages[IMAGE_BANNER].m_Id);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	const float LogoWidth = minimum(420.0f, maximum(220.0f, FullScreen.w * 0.38f));
	const float LogoHeight = LogoWidth / (360.0f / 103.0f);
	const float LogoY = FullScreen.y + minimum(70.0f, FullScreen.h * 0.12f);
	IGraphics::CQuadItem LogoQuad(FullScreen.x + (FullScreen.w - LogoWidth) / 2.0f, LogoY, LogoWidth, LogoHeight);
	Graphics()->QuadsDrawTL(&LogoQuad, 1);
	Graphics()->QuadsEnd();

	const bool HasExtraText = aLabel2[0] != '\0';
	const bool HasProgressBar = Client()->MapDownloadTotalsize() > 0;
	const float LabelWidth = minimum(860.0f, FullScreen.w - 80.0f);
	const float TitleHeight = 24.0f;
	const float TextHeight = 24.0f;
	const float TextSpacing = 8.0f;
	const float ProgressSpacing = 16.0f;
	const float ProgressHeight = 24.0f;

	float ContentHeight = TitleHeight + TextSpacing + TextHeight;
	if(HasExtraText)
		ContentHeight += TextSpacing + TextHeight;
	if(HasProgressBar)
		ContentHeight += ProgressSpacing + ProgressHeight;

	CUIRect Label;
	Label.x = FullScreen.x + (FullScreen.w - LabelWidth) / 2.0f;
	Label.y = FullScreen.y + FullScreen.h * 0.5f - ContentHeight / 2.0f;
	Label.w = LabelWidth;
	Label.h = TitleHeight;

	SLabelProperties TitleProps;
	TitleProps.m_MaxWidth = Label.w;
	TitleProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&Label, aTitle, 24.0f, TEXTALIGN_MC, TitleProps);

	Label.y += TitleHeight + TextSpacing;
	Label.h = TextHeight;
	Ui()->DoLabel(&Label, aLabel1, FontSize, TEXTALIGN_MC);

	if(HasExtraText)
	{
		Label.y += TextHeight + TextSpacing;
		SLabelProperties ExtraTextProps;
		ExtraTextProps.m_MaxWidth = Label.w;
		if(TextRender()->TextWidth(FontSize, aLabel2) > Label.w)
			Ui()->DoLabel(&Label, aLabel2, FontSize, TEXTALIGN_ML, ExtraTextProps);
		else
			Ui()->DoLabel(&Label, aLabel2, FontSize, TEXTALIGN_MC);
	}

	if(HasProgressBar)
	{
		CUIRect ProgressBar;
		ProgressBar.x = Label.x;
		ProgressBar.w = Label.w;
		ProgressBar.h = ProgressHeight;
		ProgressBar.y = Label.y + TextHeight + ProgressSpacing;
		Ui()->RenderProgressBar(ProgressBar, Client()->MapDownloadAmount() / (float)Client()->MapDownloadTotalsize());
	}

	CUIRect Button;
	Button.w = FullScreen.w - 16.0f;
	Button.h = 36.0f;
	Button.x = FullScreen.x + 8.0f;
	Button.y = FullScreen.y + FullScreen.h - Button.h - 8.0f;

	static CButtonContainer s_Button;
	if(DoButton_Menu(&s_Button, Localize("Cancel"), 0, &Button) || Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		Client()->Disconnect();
		Ui()->SetActiveItem(nullptr);
		RefreshBrowserTab(true);
	}
}

#if defined(CONF_VIDEORECORDER)
void CMenus::PopupConfirmDemoReplaceVideo()
{
	char aBuf[IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "%s/%s.demo", m_aCurrentDemoFolder, m_aCurrentDemoSelectionName);
	char aVideoName[IO_MAX_PATH_LENGTH];
	str_copy(aVideoName, m_DemoRenderInput.GetString());
	const char *pError = Client()->DemoPlayer_Render(aBuf, m_DemolistStorageType, aVideoName, m_Speed, m_StartPaused);
	m_Speed = DEMO_SPEED_INDEX_DEFAULT;
	m_StartPaused = false;
	m_LastPauseChange = -1.0f;
	m_LastSpeedChange = -1.0f;
	if(pError)
	{
		m_DemoRenderInput.Clear();
		PopupMessage(Localize("Error loading demo"), pError, Localize("Ok"));
	}
}
#endif

void CMenus::RenderThemeSelection(CUIRect MainView)
{
	const std::vector<CTheme> &vThemes = GameClient()->m_MenuBackground.GetThemes();

	int SelectedTheme = -1;
	for(int i = 0; i < (int)vThemes.size(); i++)
	{
		if(str_comp(vThemes[i].m_Name.c_str(), g_Config.m_ClMenuMap) == 0)
		{
			SelectedTheme = i;
			break;
		}
	}
	const int OldSelected = SelectedTheme;

	static CListBox s_ListBox;
	s_ListBox.DoHeader(&MainView, Localize("Theme"), 20.0f);
	s_ListBox.DoStart(20.0f, vThemes.size(), 1, 3, SelectedTheme);

	for(int i = 0; i < (int)vThemes.size(); i++)
	{
		const CTheme &Theme = vThemes[i];
		const CListboxItem Item = s_ListBox.DoNextItem(&Theme.m_Name, i == SelectedTheme);

		if(!Item.m_Visible)
			continue;

		CUIRect Icon, Label;
		Item.m_Rect.VSplitLeft(Item.m_Rect.h * 2.0f, &Icon, &Label);

		// draw icon if it exists
		if(Theme.m_IconTexture.IsValid())
		{
			Icon.VMargin(6.0f, &Icon);
			Icon.HMargin(3.0f, &Icon);
			Graphics()->TextureSet(Theme.m_IconTexture);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			IGraphics::CQuadItem QuadItem(Icon.x, Icon.y, Icon.w, Icon.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}

		char aName[128];
		if(Theme.m_Name.empty())
			str_copy(aName, "(none)");
		else if(str_comp(Theme.m_Name.c_str(), "auto") == 0)
			str_copy(aName, "(seasons)");
		else if(str_comp(Theme.m_Name.c_str(), "rand") == 0)
			str_copy(aName, "(random)");
		else if(Theme.m_HasDay && Theme.m_HasNight)
			str_copy(aName, Theme.m_Name.c_str());
		else if(Theme.m_HasDay && !Theme.m_HasNight)
			str_format(aName, sizeof(aName), "%s (day)", Theme.m_Name.c_str());
		else if(!Theme.m_HasDay && Theme.m_HasNight)
			str_format(aName, sizeof(aName), "%s (night)", Theme.m_Name.c_str());
		else // generic
			str_copy(aName, Theme.m_Name.c_str());

		Ui()->DoLabel(&Label, aName, 16.0f * CUi::ms_FontmodHeight, TEXTALIGN_ML);
	}

	SelectedTheme = s_ListBox.DoEnd();

	if(OldSelected != SelectedTheme)
	{
		const CTheme &Theme = vThemes[SelectedTheme];
		str_copy(g_Config.m_ClMenuMap, Theme.m_Name.c_str());
		GameClient()->m_MenuBackground.LoadMenuBackground(Theme.m_HasDay, Theme.m_HasNight);
	}
}

void CMenus::SetActive(bool Active)
{
	if(Active != m_MenuActive)
	{
		Ui()->SetHotItem(nullptr);
		Ui()->SetActiveItem(nullptr);
	}
	m_MenuActive = Active;
	if(!m_MenuActive)
	{
		if(m_NeedSendinfo)
		{
			GameClient()->SendInfo(false);
			m_NeedSendinfo = false;
		}

		if(m_NeedSendDummyinfo)
		{
			GameClient()->SendDummyInfo(false);
			m_NeedSendDummyinfo = false;
		}

		if(Client()->State() == IClient::STATE_ONLINE)
		{
			GameClient()->OnRelease();
		}
	}
	else
	{
		if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
			GameClient()->OnRelease();
	}
}

void CMenus::OnReset()
{
	m_MenuMediaBackground.Unload();
}

void CMenus::OnShutdown()
{
	m_MenuMediaBackground.Shutdown();
	m_CommunityIcons.Shutdown();
}

bool CMenus::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_MenuActive)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);

	return true;
}

bool CMenus::OnInput(const IInput::CEvent &Event)
{
	// Escape key is always handled to activate/deactivate menu
	if((Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE) || IsActive())
	{
		Ui()->OnInput(Event);
		return true;
	}
	return false;
}

void CMenus::OnStateChange(int NewState, int OldState)
{
	// reset active item
	Ui()->SetActiveItem(nullptr);
	if(NewState != IClient::STATE_DEMOPLAYBACK)
		Ui()->ClosePopupMenu(&m_DemoCameraEffectsPopupId);

	if(OldState == IClient::STATE_ONLINE || OldState == IClient::STATE_OFFLINE)
		TextRender()->DeleteTextContainer(m_MotdTextContainerIndex);

	if(NewState == IClient::STATE_OFFLINE)
	{
		if(OldState >= IClient::STATE_ONLINE && NewState < IClient::STATE_QUITTING)
			UpdateMusicState();
		m_Popup = POPUP_NONE;
		if(Client()->ErrorString() && Client()->ErrorString()[0] != 0)
		{
			if(str_find(Client()->ErrorString(), "password"))
			{
				m_Popup = POPUP_PASSWORD;
				m_PasswordInput.SelectAll();
				Ui()->SetActiveItem(&m_PasswordInput);
			}
			else
				m_Popup = POPUP_DISCONNECTED;
		}
	}
	else if(NewState == IClient::STATE_LOADING)
	{
		m_DownloadLastCheckTime = time_get();
		m_DownloadLastCheckSize = 0;
		m_DownloadSpeed = 0.0f;
	}
	else if(NewState == IClient::STATE_ONLINE || NewState == IClient::STATE_DEMOPLAYBACK)
	{
		if(m_Popup != POPUP_WARNING)
		{
			m_Popup = POPUP_NONE;
			SetActive(false);
		}
	}
}

void CMenus::OnWindowResize()
{
	TextRender()->DeleteTextContainer(m_MotdTextContainerIndex);
}

void CMenus::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		SetActive(true);

	if(Client()->State() == IClient::STATE_ONLINE && GameClient()->m_ServerMode == CGameClient::SERVERMODE_PUREMOD)
	{
		Client()->Disconnect();
		SetActive(true);
		PopupMessage(Localize("Disconnected"), Localize("The server is running a non-standard tuning on a pure game type."), Localize("Ok"));
	}

	if(!IsActive())
	{
		if(Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
		{
			SetActive(true);
		}
		else if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
		{
			Ui()->ClearHotkeys();
			return;
		}
	}

	Ui()->StartCheck();
	UpdateColors();

	const bool IngameMenu = IsActive() && (Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK);
	const bool UseWindowAspectForUi = IngameMenu && g_Config.m_BcCustomAspectRatioApplyMode != 1;
	Ui()->SetUseGraphicsScreenAspect(!UseWindowAspectForUi);
	// Console disables UI while open; don't re-enable every frame or Android soft keyboard thrash.
	if(!GameClient()->m_GameConsole.IsActive())
		Ui()->SetEnabled(true);

	Ui()->Update();

	// Safety overlay for Full aspect stretch. Seed from saved config on first frame so
	// re-entering the client does not reset stretch or re-prompt; only mid-session changes do.
	static bool s_StuckOverlaySeeded = false;
	static bool s_StuckOverlayDismissed = true;
	static int s_StuckLastMode = -99;
	static int s_StuckLastRatio = -99;
	static int s_StuckLastApplyMode = -99;
	const int CurAspectMode = g_Config.m_BcCustomAspectRatioMode;
	const int CurAspectRatio = g_Config.m_BcCustomAspectRatio;
	const int CurAspectApplyMode = g_Config.m_BcCustomAspectRatioApplyMode;
	const bool AspectOverlayActive = CurAspectApplyMode == 1 &&
		(CurAspectMode > 0 || (CurAspectMode < 0 && CurAspectRatio > 0));
	if(!s_StuckOverlaySeeded)
	{
		s_StuckOverlaySeeded = true;
		s_StuckOverlayDismissed = true;
		s_StuckLastMode = CurAspectMode;
		s_StuckLastRatio = CurAspectRatio;
		s_StuckLastApplyMode = CurAspectApplyMode;
	}
	else if(s_StuckLastMode != CurAspectMode || s_StuckLastRatio != CurAspectRatio || s_StuckLastApplyMode != CurAspectApplyMode)
	{
		s_StuckLastMode = CurAspectMode;
		s_StuckLastRatio = CurAspectRatio;
		s_StuckLastApplyMode = CurAspectApplyMode;
		s_StuckOverlayDismissed = !AspectOverlayActive;
	}
	const bool ShowStuckOverlay = IsActive() && AspectOverlayActive && !s_StuckOverlayDismissed;
	// Block menu button fires: clear active item so no DoButtonLogic fires this frame
	if(ShowStuckOverlay)
		Ui()->SetActiveItem(nullptr);

	Render();

	// After Render: discard buttons that became active, and suppress hot item next frame
	if(ShowStuckOverlay)
	{
		Ui()->SetActiveItem(nullptr);
		Ui()->SetHotItem(nullptr);
	}

	// render debug information
	if(g_Config.m_Debug)
		Ui()->DebugRender(2.0f, Ui()->Screen()->h - 12.0f);

	if(Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE))
	{
		Ui()->SetHotItem(nullptr);
		Ui()->SetActiveItem(nullptr);
		SetActive(false);
	}

	// Safety overlay: shown in menu when aspect ratio "Full" mode is active.
	// Renders in real window coordinates so it stays readable regardless of distortion.
	// Uses UpdatedMousePos (raw pixels) → real virtual coords to avoid the distorted m_MousePos.
	if(ShowStuckOverlay)
	{
		Ui()->SetUseGraphicsScreenAspect(false);
		Ui()->MapScreen();
		const CUIRect *pReal = Ui()->Screen();

		// Convert raw pixel mouse to real virtual coords
		const float WinW = (float)Graphics()->ScreenWidth();
		const float WinH = (float)Graphics()->ScreenHeight();
		const vec2 RawMouse = Ui()->UpdatedMousePos();
		const vec2 RealMouse = vec2(RawMouse.x * pReal->w / WinW, RawMouse.y * pReal->h / WinH);

		const float PanelW = minimum(360.0f, pReal->w - 12.0f);
		CUIRect Panel;
		Panel.x = pReal->x + (pReal->w - PanelW) * 0.5f;
		Panel.y = pReal->y + 6.0f;
		Panel.w = PanelW;
		Panel.h = 30.0f;
		Graphics()->DrawRect(Panel.x, Panel.y, Panel.w, Panel.h, ColorRGBA(0.0f, 0.0f, 0.0f, 0.82f), IGraphics::CORNER_ALL, 5.0f);
		Panel.Margin(2.0f, &Panel);

		CUIRect StuckBtn, CloseBtn;
		Panel.VSplitRight(80.0f, &Panel, &CloseBtn);
		CloseBtn.VMargin(2.0f, &CloseBtn);
		Panel.VSplitRight(4.0f, &Panel, nullptr);
		Panel.VSplitRight(88.0f, &Panel, &StuckBtn);
		StuckBtn.VMargin(2.0f, &StuckBtn);
		Panel.VSplitRight(4.0f, &Panel, nullptr);
		Ui()->DoLabel(&Panel, "Aspect ratio: Full", 10.0f, TEXTALIGN_ML);

		auto RenderOverlayBtn = [&](const CUIRect &Rect, const char *pText, ColorRGBA Normal, ColorRGBA Hovered) -> bool {
			const bool Hover = RealMouse.x >= Rect.x && RealMouse.x < Rect.x + Rect.w &&
				RealMouse.y >= Rect.y && RealMouse.y < Rect.y + Rect.h;
			Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, Hover ? Hovered : Normal, IGraphics::CORNER_ALL, 3.0f);
			Ui()->DoLabel(&Rect, pText, 10.5f, TEXTALIGN_MC);
			return Hover && Ui()->MouseButtonClicked(0);
		};

		if(RenderOverlayBtn(StuckBtn, "I'm stuck", ColorRGBA(0.45f, 0.18f, 0.18f, 1.0f), ColorRGBA(0.7f, 0.28f, 0.28f, 1.0f)))
		{
			g_Config.m_BcCustomAspectRatioMode = 0;
			g_Config.m_BcCustomAspectRatio = 0;
			GameClient()->m_TClient.SetForcedAspect();
		}
		if(RenderOverlayBtn(CloseBtn, "Looks fine", ColorRGBA(0.18f, 0.36f, 0.18f, 1.0f), ColorRGBA(0.28f, 0.55f, 0.28f, 1.0f)))
			s_StuckOverlayDismissed = true;

		// Restore original coordinate system before cursor render
		Ui()->SetUseGraphicsScreenAspect(!UseWindowAspectForUi);
		Ui()->MapScreen();
	}

	if(IsActive())
	{
		RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);
	}

	Ui()->FinishCheck();
	Ui()->ClearHotkeys();
	Ui()->SetUseGraphicsScreenAspect(true);
}

void CMenus::UpdateColors()
{
	ms_GuiColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UiColor, true));

	ms_ColorTabbarInactiveOutgame = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);
	ms_ColorTabbarActiveOutgame = ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f);
	ms_ColorTabbarHoverOutgame = ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f);

	const float ColorIngameScaleI = 0.5f;
	const float ColorIngameScaleA = 0.2f;

	ms_ColorTabbarInactiveIngame = ColorRGBA(
		ms_GuiColor.r * ColorIngameScaleI,
		ms_GuiColor.g * ColorIngameScaleI,
		ms_GuiColor.b * ColorIngameScaleI,
		ms_GuiColor.a * 0.8f);

	ms_ColorTabbarActiveIngame = ColorRGBA(
		ms_GuiColor.r * ColorIngameScaleA,
		ms_GuiColor.g * ColorIngameScaleA,
		ms_GuiColor.b * ColorIngameScaleA,
		ms_GuiColor.a);

	ms_ColorTabbarHoverIngame = ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f);
}

void CMenus::RenderBackground()
{
	Graphics()->BlendNormal();

	const float ScreenHeight = 300.0f;
	const float ScreenWidth = ScreenHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, ScreenWidth, ScreenHeight);

	// render background color
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(ms_GuiColor.WithAlpha(1.0f));
	const IGraphics::CQuadItem BackgroundQuadItem = IGraphics::CQuadItem(0, 0, ScreenWidth, ScreenHeight);
	Graphics()->QuadsDrawTL(&BackgroundQuadItem, 1);
	Graphics()->QuadsEnd();

	// render the tiles
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.045f);
	const float Size = 15.0f;
	const float OffsetTime = std::fmod(Client()->GlobalTime() * 0.15f, 2.0f);
	IGraphics::CQuadItem aCheckerItems[64];
	size_t NumCheckerItems = 0;
	const int NumItemsWidth = std::ceil(ScreenWidth / Size);
	const int NumItemsHeight = std::ceil(ScreenHeight / Size);
	for(int y = -2; y < NumItemsHeight; y++)
	{
		for(int x = 0; x < NumItemsWidth + 4; x += 2)
		{
			aCheckerItems[NumCheckerItems] = IGraphics::CQuadItem((x - 2 * OffsetTime + (y & 1)) * Size, (y + OffsetTime) * Size, Size, Size);
			NumCheckerItems++;
			if(NumCheckerItems == std::size(aCheckerItems))
			{
				Graphics()->QuadsDrawTL(aCheckerItems, NumCheckerItems);
				NumCheckerItems = 0;
			}
		}
	}
	if(NumCheckerItems != 0)
		Graphics()->QuadsDrawTL(aCheckerItems, NumCheckerItems);
	Graphics()->QuadsEnd();

	// render border fade
	Graphics()->TextureSet(m_TextureBlob);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	const IGraphics::CQuadItem BlobQuadItem = IGraphics::CQuadItem(-100, -100, ScreenWidth + 200, ScreenHeight + 200);
	Graphics()->QuadsDrawTL(&BlobQuadItem, 1);
	Graphics()->QuadsEnd();

	// restore screen
	Ui()->MapScreen();
}

int CMenus::DoButton_CheckBox_Tristate(const void *pId, const char *pText, TRISTATE Checked, const CUIRect *pRect)
{
	switch(Checked)
	{
	case TRISTATE::NONE:
		return DoButton_CheckBox_Common(pId, pText, "", pRect, BUTTONFLAG_LEFT);
	case TRISTATE::SOME:
		return DoButton_CheckBox_Common(pId, pText, "O", pRect, BUTTONFLAG_LEFT);
	case TRISTATE::ALL:
		return DoButton_CheckBox_Common(pId, pText, "X", pRect, BUTTONFLAG_LEFT);
	default:
		dbg_assert_failed("Invalid tristate. Checked: %d", static_cast<int>(Checked));
	}
}

int CMenus::MenuImageScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	const char *pExtension = ".png";
	CMenuImage MenuImage;
	CMenus *pSelf = static_cast<CMenus *>(pUser);
	if(IsDir || !str_endswith(pName, pExtension) || str_length(pName) - str_length(pExtension) >= (int)sizeof(MenuImage.m_aName))
		return 0;

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "menuimages/%s", pName);

	CImageInfo Info;
	if(!pSelf->Graphics()->LoadPng(Info, aPath, DirType))
	{
		char aError[IO_MAX_PATH_LENGTH + 64];
		str_format(aError, sizeof(aError), "Failed to load menu image from '%s'", aPath);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "menus", aError);
		return 0;
	}
	if(Info.m_Format != CImageInfo::FORMAT_RGBA)
	{
		Info.Free();
		char aError[IO_MAX_PATH_LENGTH + 64];
		str_format(aError, sizeof(aError), "Failed to load menu image from '%s': must be an RGBA image", aPath);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "menus", aError);
		return 0;
	}

	MenuImage.m_OrgTexture = pSelf->Graphics()->LoadTextureRaw(Info, 0, aPath);

	ConvertToGrayscale(Info);
	MenuImage.m_GreyTexture = pSelf->Graphics()->LoadTextureRawMove(Info, 0, aPath);

	str_truncate(MenuImage.m_aName, sizeof(MenuImage.m_aName), pName, str_length(pName) - str_length(pExtension));
	pSelf->m_vMenuImages.push_back(MenuImage);

	pSelf->RenderLoading(Localize("Loading DDNet Client"), Localize("Loading menu images"), 0);

	return 0;
}

const CMenus::CMenuImage *CMenus::FindMenuImage(const char *pName)
{
	for(auto &Image : m_vMenuImages)
		if(str_comp(Image.m_aName, pName) == 0)
			return &Image;
	return nullptr;
}

void CMenus::SetMenuPage(int NewPage)
{
	const int OldPage = m_MenuPage;
	m_MenuPage = NewPage;
	if(NewPage >= PAGE_INTERNET && NewPage <= PAGE_FAVORITE_COMMUNITY_5)
	{
		g_Config.m_UiPage = NewPage;
		bool ForceRefresh = false;
		if(m_ForceRefreshLanPage && NewPage == PAGE_LAN)
		{
			ForceRefresh = true;
			m_ForceRefreshLanPage = false;
		}
		if(OldPage != NewPage || ForceRefresh)
		{
			RefreshBrowserTab(ForceRefresh);
		}
	}
}

void CMenus::RefreshBrowserTab(bool Force)
{
	bool BrowserRefreshed = false;
	if(g_Config.m_UiPage == PAGE_INTERNET)
	{
		if(Force || ServerBrowser()->GetCurrentType() != IServerBrowser::TYPE_INTERNET)
		{
			if(Force || ServerBrowser()->GetCurrentType() == IServerBrowser::TYPE_LAN)
			{
				Client()->RequestDDNetInfo();
			}
			ServerBrowser()->Refresh(IServerBrowser::TYPE_INTERNET);
			UpdateCommunityCache(true);
			m_LastServerBrowserRefreshTick = time_get();
			BrowserRefreshed = true;
		}
	}
	else if(g_Config.m_UiPage == PAGE_LAN)
	{
		if(Force || ServerBrowser()->GetCurrentType() != IServerBrowser::TYPE_LAN)
		{
			ServerBrowser()->Refresh(IServerBrowser::TYPE_LAN);
			UpdateCommunityCache(true);
			m_LastServerBrowserRefreshTick = time_get();
			BrowserRefreshed = true;
		}
	}
	else if(g_Config.m_UiPage == PAGE_FAVORITES)
	{
		if(Force || ServerBrowser()->GetCurrentType() != IServerBrowser::TYPE_FAVORITES)
		{
			if(Force || ServerBrowser()->GetCurrentType() == IServerBrowser::TYPE_LAN)
			{
				Client()->RequestDDNetInfo();
			}
			ServerBrowser()->Refresh(IServerBrowser::TYPE_FAVORITES);
			UpdateCommunityCache(true);
			m_LastServerBrowserRefreshTick = time_get();
			BrowserRefreshed = true;
		}
	}
	else if(g_Config.m_UiPage >= PAGE_FAVORITE_COMMUNITY_1 && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5)
	{
		const int BrowserType = g_Config.m_UiPage - PAGE_FAVORITE_COMMUNITY_1 + IServerBrowser::TYPE_FAVORITE_COMMUNITY_1;
		if(Force || ServerBrowser()->GetCurrentType() != BrowserType)
		{
			if(Force || ServerBrowser()->GetCurrentType() == IServerBrowser::TYPE_LAN)
			{
				Client()->RequestDDNetInfo();
			}
			ServerBrowser()->Refresh(BrowserType);
			UpdateCommunityCache(true);
			m_LastServerBrowserRefreshTick = time_get();
			BrowserRefreshed = true;
		}
	}

	if(BrowserRefreshed)
	{
		GameClient()->m_ClientIndicator.RefreshBrowserCache(false);
		GameClient()->m_ClientIndicator.RefreshUcPresenceList(true);
	}
}

void CMenus::ForceRefreshLanPage()
{
	m_ForceRefreshLanPage = true;
}

void CMenus::SetShowStart(bool ShowStart)
{
	m_ShowStart = ShowStart;
}

void CMenus::ShowQuitPopup()
{
	m_Popup = POPUP_QUIT;
}

void CMenus::QuitWithMenuSfx()
{
	if(Client()->State() == IClient::STATE_QUITTING || Client()->State() == IClient::STATE_RESTARTING)
		return;

	GameClient()->m_TimeoutReconnect.MarkIntentionalLeave();

	if(!m_MenuSfxLoaded)
		LoadMenuSfx();

	const int ExitSample = m_aMenuSfxSamples[(size_t)EMenuSfxSample::SCREEN_BACK];
	const bool CanDelayQuitForSfx = g_Config.m_SndEnable && g_Config.m_BcMenuSfx && ExitSample >= 0;
	if(CanDelayQuitForSfx)
	{
		if(!m_MenuSfxExitPlayed)
		{
			PlayMenuSfxSample(ExitSample);
			m_MenuSfxExitPlayed = true;
		}

		if(!m_MenuSfxQuitPending)
		{
			const float DelaySeconds = std::clamp(Sound()->GetSampleTotalTime(ExitSample), 0.08f, 0.35f);
			m_MenuSfxQuitAt = time_get() + (int64_t)(DelaySeconds * time_freq());
			m_MenuSfxQuitPending = true;
		}
		return;
	}

	Client()->Quit();
}

void CMenus::JoinTutorial()
{
	m_JoinTutorial.m_Queued = true;
	m_JoinTutorial.m_Status = CJoinTutorial::EStatus::REFRESHING;
	m_JoinTutorial.m_TryRefresh = false;
	m_JoinTutorial.m_TriedRefresh = false;
	m_JoinTutorial.m_LocalServerState = CJoinTutorial::ELocalServerState::NOT_TRIED;
	m_JoinTutorial.m_StateChange = time_get_nanoseconds();
}

void CMenus::SetSettingsLinkContext(int SettingsPage, const char *pTab)
{
	m_SettingsLinkContextPage = SettingsPage;
	const char *pPage = CUClientSettingsLink::PageTokenFromSettingsPage(SettingsPage);
	str_copy(m_aSettingsLinkContextPageToken, pPage ? pPage : "", sizeof(m_aSettingsLinkContextPageToken));
	if(pTab)
		str_copy(m_aSettingsLinkContextTab, pTab, sizeof(m_aSettingsLinkContextTab));
	else
		m_aSettingsLinkContextTab[0] = '\0';
	m_SettingsLinkParentCount = 0;
	m_SettingsLinkVarEnabled = CUClientSettingsLink::PageAllowsVarLinks(SettingsPage);
	if(SettingsPage == SETTINGS_LANGUAGE || SettingsPage == SETTINGS_PLAYER || SettingsPage == SETTINGS_CONFIGS)
		m_SettingsLinkVarEnabled = false;
	else if(SettingsPage == SETTINGS_GRAPHICS)
		m_SettingsLinkVarEnabled = true; // allowlist enforced per-var
}

void CMenus::SetSettingsLinkContextToken(const char *pPage, const char *pTab)
{
	m_SettingsLinkContextPage = -2; // non-settings page (browser, etc.)
	str_copy(m_aSettingsLinkContextPageToken, pPage ? pPage : "", sizeof(m_aSettingsLinkContextPageToken));
	if(pTab)
		str_copy(m_aSettingsLinkContextTab, pTab, sizeof(m_aSettingsLinkContextTab));
	else
		m_aSettingsLinkContextTab[0] = '\0';
	m_SettingsLinkParentCount = 0;
	m_SettingsLinkVarEnabled = m_aSettingsLinkContextPageToken[0] != '\0';
}

void CMenus::PushSettingsLinkParent(const char *pScriptName)
{
	if(!pScriptName || pScriptName[0] == '\0' || m_SettingsLinkParentCount >= CUClientSettingsLink::MAX_PARENTS)
		return;
	str_copy(m_aaSettingsLinkParents[m_SettingsLinkParentCount], pScriptName, sizeof(m_aaSettingsLinkParents[0]));
	m_SettingsLinkParentCount++;
}

void CMenus::PopSettingsLinkParent()
{
	if(m_SettingsLinkParentCount > 0)
		m_SettingsLinkParentCount--;
}

void CMenus::SetSettingsLinkVarEnabled(bool Enabled)
{
	m_SettingsLinkVarEnabled = Enabled;
}

void CMenus::MaybeRegisterSettingsLinkVar(const void *pId, const char *pLabel)
{
	if(!m_SettingsLinkVarEnabled || m_aSettingsLinkContextPageToken[0] == '\0' || !pId)
		return;
	const SConfigVariable *pVar = CUClientSettingsLink::FindVariableByPointer(ConfigManager(), pId);
	if(!pVar)
		return;
	if(m_SettingsLinkContextPage == SETTINGS_GRAPHICS && !CUClientSettingsLink::IsGraphicsAllowlisted(pVar->m_pScriptName))
		return;
	const char *apParents[CUClientSettingsLink::MAX_PARENTS] = {};
	for(int i = 0; i < m_SettingsLinkParentCount; ++i)
		apParents[i] = m_aaSettingsLinkParents[i];
	CUClientSettingsLink::RegisterVarLocation(pVar->m_pScriptName, pLabel, m_aSettingsLinkContextPageToken, m_aSettingsLinkContextTab, apParents, m_SettingsLinkParentCount);
}

bool CMenus::TryOpenSettingsLinkMenuForVar(const void *pId, const char *pLabel, const CUIRect *pRect)
{
	if(!m_SettingsLinkVarEnabled)
		return false;
	const SConfigVariable *pVar = CUClientSettingsLink::FindVariableByPointer(ConfigManager(), pId);
	if(!pVar)
		return false;
	if(m_SettingsLinkContextPage == SETTINGS_GRAPHICS && !CUClientSettingsLink::IsGraphicsAllowlisted(pVar->m_pScriptName))
		return false;

	MaybeRegisterSettingsLinkVar(pId, pLabel);

	if(m_aSettingsLinkContextPageToken[0] == '\0')
		return false;
	const char *apParents[CUClientSettingsLink::MAX_PARENTS] = {};
	for(int i = 0; i < m_SettingsLinkParentCount; ++i)
		apParents[i] = m_aaSettingsLinkParents[i];

	// Prefer English help text in the URI so percent-encoding stays short/ASCII and
	// receivers can Localize() it. Localized UI labels are kept only in the local registry.
	const char *pUriLabel = (pVar->m_pHelp && pVar->m_pHelp[0] != '\0') ? pVar->m_pHelp : pLabel;
	char aUri[CUClientSettingsLink::MAX_URI_LENGTH];
	if(!CUClientSettingsLink::BuildVarUri(aUri, sizeof(aUri), pVar->m_pScriptName, pUriLabel, m_aSettingsLinkContextPageToken, m_aSettingsLinkContextTab, apParents, m_SettingsLinkParentCount))
		return false;

	str_copy(m_aSettingsLinkPendingUri, aUri, sizeof(m_aSettingsLinkPendingUri));
	m_SettingsLinkCopyIsTab = false;
	const float X = pRect ? pRect->x : Ui()->MouseX();
	const float Y = pRect ? (pRect->y + pRect->h) : Ui()->MouseY();
	if(Ui()->IsPopupOpen(&m_SettingsLinkPopupId))
		Ui()->ClosePopupMenu(&m_SettingsLinkPopupId, true);
	Ui()->DoPopupMenu(&m_SettingsLinkPopupId, X, Y, 170.0f, 28.0f, this, PopupSettingsLinkCopy, {}, CUi::EButtonSoundType::DEFAULT);
	return true;
}

bool CMenus::TryOpenSettingsLinkMenuForPage(const char *pPageToken, const char *pTabToken, const CUIRect *pRect)
{
	char aUri[CUClientSettingsLink::MAX_URI_LENGTH];
	if(!CUClientSettingsLink::BuildPageUri(aUri, sizeof(aUri), pPageToken, pTabToken))
		return false;
	str_copy(m_aSettingsLinkPendingUri, aUri, sizeof(m_aSettingsLinkPendingUri));
	m_SettingsLinkCopyIsTab = true;
	const float X = pRect ? pRect->x : Ui()->MouseX();
	const float Y = pRect ? (pRect->y + pRect->h) : Ui()->MouseY();
	if(Ui()->IsPopupOpen(&m_SettingsLinkPopupId))
		Ui()->ClosePopupMenu(&m_SettingsLinkPopupId, true);
	Ui()->DoPopupMenu(&m_SettingsLinkPopupId, X, Y, 170.0f, 28.0f, this, PopupSettingsLinkCopy, {}, CUi::EButtonSoundType::DEFAULT);
	return true;
}

bool CMenus::DoScrollbarOptionSettingsLink(const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix)
{
	const bool Changed = Ui()->DoScrollbarOption(pId, pOption, pRect, pStr, Min, Max, pScale, Flags, pSuffix);
	const SConfigVariable *pVar = CUClientSettingsLink::FindVariableByPointer(ConfigManager(), pId);
	if(pVar)
	{
		MaybeRegisterSettingsLinkVar(pId, pStr);
		MaybeHighlightSettingsLink(pRect, pVar->m_pScriptName);
	}
	if(Ui()->MouseHovered(pRect) && Input()->KeyPress(KEY_MOUSE_2))
		TryOpenSettingsLinkMenuForVar(pId, pStr, pRect);
	return Changed;
}

void CMenus::SetSettingsLinkScrollRegion(CScrollRegion *pRegion)
{
	m_pSettingsLinkScrollRegion = pRegion;
}

void CMenus::MaybeHighlightSettingsLink(const CUIRect *pRect, const char *pScriptName)
{
	if(!pRect || !pScriptName || m_aSettingsLinkHighlightScript[0] == '\0')
		return;
	if(str_comp(pScriptName, m_aSettingsLinkHighlightScript) != 0)
		return;
	if(m_SettingsLinkHighlightUntil > 0 && time_get() > m_SettingsLinkHighlightUntil)
	{
		m_aSettingsLinkHighlightScript[0] = '\0';
		m_SettingsLinkHighlightUntil = 0;
		m_SettingsLinkNeedScroll = false;
		return;
	}
	if(m_SettingsLinkNeedScroll && m_pSettingsLinkScrollRegion)
	{
		// Scroll once so the target row sits near the viewport center (clamped at ends).
		m_pSettingsLinkScrollRegion->AddRect(*pRect, false);
		m_pSettingsLinkScrollRegion->ScrollHere(CScrollRegion::SCROLLHERE_CENTER);
		m_SettingsLinkNeedScroll = false;
	}
	constexpr float HighlightDuration = 1.6f;
	constexpr float HighlightBlinks = 2.0f;
	const float Remain = m_SettingsLinkHighlightUntil > 0 ? (float)(m_SettingsLinkHighlightUntil - time_get()) / (float)time_freq() : 0.0f;
	const float Progress = std::clamp(1.0f - Remain / HighlightDuration, 0.0f, 1.0f);
	// Start bright, then complete at least two on/off pulses.
	const float Pulse = std::abs(std::sin((0.5f + Progress * HighlightBlinks) * pi));
	const float Alpha = Pulse * 0.55f;
	pRect->Draw(ColorRGBA(0.35f, 0.75f, 1.0f, Alpha), IGraphics::CORNER_ALL, 4.0f);
}

void CMenus::NavigateToSettingsLink(const CUClientSettingsLink::SNavigateRequest &Request)
{
	if(!Request.m_Valid)
		return;
	SetActive(true);

	if(Request.m_BrowserServerFilter)
	{
		// Server browser toolbox lives on offline menu pages.
		const int BrowserPage = (g_Config.m_UiPage >= PAGE_INTERNET && g_Config.m_UiPage <= PAGE_FAVORITE_COMMUNITY_5) ? g_Config.m_UiPage : PAGE_INTERNET;
		SetMenuPage(BrowserPage);
		g_Config.m_UiToolboxPage = 0; // UI_TOOLBOX_PAGE_FILTERS
	}
	else
	{
		if(Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK)
			m_GamePage = PAGE_SETTINGS;
		else
			SetMenuPage(PAGE_SETTINGS);
		if(Request.m_SettingsPage >= 0)
			g_Config.m_UiSettingsPage = Request.m_SettingsPage;
	}

	m_SettingsLinkNavBestClientTab = Request.m_BestClientTab;
	m_SettingsLinkNavTClientTab = Request.m_TClientTab;
	m_SettingsLinkNavUClientTab = Request.m_UClientTab;
	m_SettingsLinkNavAssetsTab = Request.m_AssetsTab;
	m_SettingsLinkNavControlsMode = Request.m_ControlsMode;
	if(Request.m_Highlight && Request.m_aHighlightScript[0] != '\0')
	{
		str_copy(m_aSettingsLinkHighlightScript, Request.m_aHighlightScript, sizeof(m_aSettingsLinkHighlightScript));
		m_SettingsLinkHighlightUntil = time_get() + (int64_t)(time_freq() * 1.6f); // 2 blinks
		m_SettingsLinkNeedScroll = !Request.m_BrowserServerFilter;

		// Expand parent toggles so nested rows (e.g. AntiPing children) are actually rendered.
		for(int i = 0; i < Request.m_NumParents; ++i)
		{
			const char *pParentName = CUClientSettingsLink::ParentScriptName(Request.m_aaParents[i]);
			const SConfigVariable *pParent = CUClientSettingsLink::FindVariable(ConfigManager(), pParentName);
			if(!pParent || pParent->m_Type != SConfigVariable::VAR_INT)
				continue;
			auto *pInt = static_cast<SIntConfigVariable *>(const_cast<SConfigVariable *>(pParent));
			if(pInt->m_Min != 0 || pInt->m_Max != 1)
				continue;
			// An inverted parent hides the target while it is on, so it has to be cleared instead.
			const int Wanted = CUClientSettingsLink::IsInvertedParent(Request.m_aaParents[i]) ? 0 : 1;
			if(*pInt->m_pVariable == Wanted)
				continue;
			// Through the console so registered conchains fire (coupled toggles).
			char aCmd[192];
			str_format(aCmd, sizeof(aCmd), "%s %d", pParent->m_pScriptName, Wanted);
			Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
		}
	}
	else
	{
		m_aSettingsLinkHighlightScript[0] = '\0';
		m_SettingsLinkHighlightUntil = 0;
		m_SettingsLinkNeedScroll = false;
	}
}

namespace
{
struct SSettingsLinkChatLayout
{
	float Margin;
	float CrumbH;
	float CrumbGap;
	float CrumbFontSize;
	float LineSize;
	float LabelSize;
	float ColorH;
	float ColorGap;
	float Rounding;
	float LabelGap;
	float BoxMargin;
};

SSettingsLinkChatLayout SettingsLinkChatLayout(float FontSize)
{
	const float Fs = maximum(5.0f, FontSize);
	SSettingsLinkChatLayout Layout;
	// Inner padding so text doesn't sit flush against the card edges.
	Layout.Margin = maximum(Fs * 0.42f, 4.0f);
	// Breadcrumb stays clearly smaller than the setting row.
	Layout.CrumbFontSize = maximum(Fs * 0.62f, 4.0f);
	Layout.CrumbH = Layout.CrumbFontSize * 1.15f;
	Layout.CrumbGap = maximum(Fs * 0.18f, 2.0f);
	// Compact chat-scale controls (still readable, smaller than settings menus).
	Layout.LineSize = maximum(Fs * 1.15f, 8.0f);
	Layout.BoxMargin = maximum(1.0f, Layout.LineSize * 0.10f);
	Layout.LabelSize = maximum(1.0f, (Layout.LineSize - Layout.BoxMargin * 2.0f) * CUi::ms_FontmodHeight);
	Layout.ColorH = Layout.LineSize;
	Layout.ColorGap = Fs * 0.10f;
	Layout.Rounding = maximum(2.0f, Fs * 0.28f);
	Layout.LabelGap = maximum(1.0f, Layout.LineSize * 0.12f);
	return Layout;
}

constexpr const char *SETTINGS_LINK_PAGE_HINT = "Press the shortcut button to go to settings.";

struct SSettingsLinkPageLayout
{
	float Margin;
	float TitleFont;
	float TitleH;
	float HintFont;
	float HintGap;
};

SSettingsLinkPageLayout SettingsLinkPageLayout(float FontSize)
{
	const float Fs = maximum(5.0f, FontSize);
	SSettingsLinkPageLayout Layout;
	Layout.Margin = maximum(Fs * 0.22f, 2.0f);
	Layout.TitleFont = maximum(Fs * 0.82f, 5.0f);
	Layout.TitleH = Layout.TitleFont * 1.12f;
	Layout.HintFont = maximum(Fs * 0.55f, 3.5f);
	// Small breathing room so the shortcut hint sits just under the tab name /
	// setting rows above it (kept tight per user request).
	Layout.HintGap = maximum(Fs * 0.24f, 2.5f);
	return Layout;
}

// Height reserved for the shortcut hint. When the card is wide enough the hint fits
// on one line; on the narrow scoreboard column it wraps instead of being ellipsized,
// so we measure the wrapped height (word-wrapped at InnerWidth) to reserve room for it.
// InnerWidth <= 0 means "width unknown" -> assume a single line (legacy behavior).
float SettingsLinkHintHeight(ITextRender *pTextRender, const SSettingsLinkPageLayout &Page, float InnerWidth)
{
	if(InnerWidth <= 1.0f)
		return Page.HintFont;
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
	pTextRender->SetRenderFlags(0);
	const STextBoundingBox Box = pTextRender->TextBoundingBox(Page.HintFont, SETTINGS_LINK_PAGE_HINT, -1, InnerWidth);
	return maximum(Page.HintFont, Box.m_H);
}

float SettingsLinkControlHeight(const SConfigVariable *pVar, const SSettingsLinkChatLayout &Layout)
{
	if(!pVar)
		return Layout.LineSize;
	if(pVar->m_Type == SConfigVariable::VAR_COLOR)
		return Layout.ColorH + Layout.ColorGap;
	return Layout.LineSize;
}

float SettingsLinkControlWidth(ITextRender *pTextRender, const SConfigVariable *pVar, const char *pLabel, const SSettingsLinkChatLayout &Layout)
{
	const char *pText = (pLabel && pLabel[0] != '\0') ? pLabel : (pVar ? pVar->m_pScriptName : "");
	const float TextW = pTextRender->TextWidth(Layout.LabelSize, pText);
	if(!pVar)
		return Layout.LineSize + Layout.LabelGap + TextW;
	if(pVar->m_Type == SConfigVariable::VAR_COLOR)
		return Layout.LineSize + Layout.LabelGap + TextW + Layout.ColorH + 50.0f;
	if(pVar->m_Type == SConfigVariable::VAR_INT)
	{
		auto *pInt = static_cast<const SIntConfigVariable *>(pVar);
		if(pInt->m_Min == 0 && pInt->m_Max == 1)
			return Layout.LineSize + Layout.LabelGap + TextW;
		return Layout.LabelGap + TextW + maximum(48.0f, Layout.LineSize * 6.0f);
	}
	return Layout.LabelGap + TextW + maximum(40.0f, Layout.LineSize * 5.0f);
}

// Mirror CMenus::DoButton_CheckBox_Common visuals (box + XMARK + label size), scaled for chat.
void DrawSettingsLinkCheckboxRow(CUi *pUi, ITextRender *pTextRender, CUIRect Row, const char *pText, bool Checked, bool Hovered, bool Pressed, float Blend, float LabelGap, float BoxMargin)
{
	CUIRect Box, Label;
	Row.VSplitLeft(Row.h, &Box, &Label);
	Label.VSplitLeft(LabelGap, nullptr, &Label);

	Box.Margin(BoxMargin, &Box);
	const float ColorMul = Pressed ? pUi->ButtonColorMulActive() : (Hovered ? pUi->ButtonColorMulHot() : pUi->ButtonColorMulDefault());
	Box.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * ColorMul * Blend), IGraphics::CORNER_ALL, maximum(2.0f, Box.h * 0.22f));

	const float GlyphSize = Box.h * CUi::ms_FontmodHeight;
	if(Checked)
	{
		pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
		pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
		pTextRender->TextColor(pTextRender->DefaultTextColor().WithMultipliedAlpha(Blend));
		pTextRender->TextOutlineColor(pTextRender->DefaultTextOutlineColor().WithMultipliedAlpha(Blend));
		pUi->DoLabel(&Box, FontIcon::XMARK, GlyphSize, TEXTALIGN_MC);
	}

	// Always restore default face before CJK labels — icon face must not stay selected.
	pTextRender->SetRenderFlags(0);
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
	pTextRender->TextColor(pTextRender->DefaultTextColor().WithMultipliedAlpha(Blend));
	pTextRender->TextOutlineColor(pTextRender->DefaultTextOutlineColor().WithMultipliedAlpha(Blend));
	SLabelProperties LabelProps;
	LabelProps.m_MaxWidth = Label.w;
	LabelProps.m_EllipsisAtEnd = true;
	LabelProps.m_EnableWidthCheck = false;
	pUi->DoLabel(&Label, pText, GlyphSize, TEXTALIGN_ML, LabelProps);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	pTextRender->TextOutlineColor(pTextRender->DefaultTextOutlineColor());
}

void DrawSettingsLinkClippedLabel(CUi *pUi, ITextRender *pTextRender, const CUIRect &Label, const char *pText, float FontSize, float Blend)
{
	pTextRender->SetRenderFlags(0);
	pTextRender->SetFontPreset(EFontPreset::DEFAULT_FONT);
	pTextRender->TextColor(1.0f, 1.0f, 1.0f, Blend);
	SLabelProperties Props;
	Props.m_MaxWidth = Label.w;
	Props.m_EllipsisAtEnd = true;
	Props.m_EnableWidthCheck = false;
	pUi->DoLabel(&Label, pText, FontSize, TEXTALIGN_ML, Props);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
}
}

float CMenus::MeasureSettingsLinkInlineHeight(const CUClientSettingsLink::SParsed &Parsed, bool Missing, bool PageOnly, float FontSize, float CardWidth) const
{
	const SSettingsLinkChatLayout Layout = SettingsLinkChatLayout(FontSize);
	const SSettingsLinkPageLayout Page = SettingsLinkPageLayout(FontSize);
	float Height = Layout.Margin * 2.0f;
	if(Missing)
	{
		// Tight single-line box (small top/bottom padding) for the red "missing" notice.
		const float MissingMargin = PageOnly ? Page.Margin : Layout.Margin;
		return MissingMargin * 2.0f + Layout.LabelSize * 1.25f;
	}

	// Inner width available to the shortcut hint (card minus side margins). Lets the hint
	// wrap onto multiple lines instead of being cut off when the card is clamped narrow.
	const float Margin = PageOnly ? Page.Margin : Layout.Margin;
	const float InnerWidth = CardWidth > 0.0f ? CardWidth - Margin * 2.0f : -1.0f;
	const float HintH = SettingsLinkHintHeight(TextRender(), Page, InnerWidth);

	if(PageOnly)
		return Page.Margin * 2.0f + Page.TitleH + Page.HintGap + HintH;

	Height += Layout.CrumbH + Layout.CrumbGap;
	for(int i = 0; i < Parsed.m_NumParents; ++i)
		Height += SettingsLinkControlHeight(CUClientSettingsLink::FindVariable(ConfigManager(), CUClientSettingsLink::ParentScriptName(Parsed.m_aaParents[i])), Layout);
	Height += SettingsLinkControlHeight(CUClientSettingsLink::FindVariable(ConfigManager(), Parsed.m_aScriptName), Layout);
	Height += Page.HintGap + HintH;
	return Height;
}

float CMenus::MeasureSettingsLinkInlineWidth(const CUClientSettingsLink::SParsed &Parsed, bool Missing, bool PageOnly, float FontSize) const
{
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->SetRenderFlags(0);
	const SSettingsLinkChatLayout Layout = SettingsLinkChatLayout(FontSize);
	const float Fs = maximum(5.0f, FontSize);
	const float TrailingPad = maximum(Fs * 1.35f, 10.0f); // breathing room after the last glyph
	float Width = Layout.Margin * 2.0f + TrailingPad;
	if(Missing)
	{
		const char *pMissingText = PageOnly ? "This client does not have this tab." : "This client does not have this setting.";
		Width += TextRender()->TextWidth(Layout.LabelSize, pMissingText) * 1.08f;
		return Width;
	}

	char aBreadcrumb[128];
	CUClientSettingsLink::FormatBreadcrumb(Parsed, aBreadcrumb, sizeof(aBreadcrumb));
	const char *pCrumb = aBreadcrumb[0] != '\0' ? aBreadcrumb : Parsed.m_aPage;
	if(PageOnly)
	{
		// Same chip width for short page/tab names: size to the shared hint line
		// (or the title if longer), so "Tee" / "일반" / "BestClient" don't look random.
		// Use the same 1.12f / TrailingPad as var cards — HintW alone under-measures
		// DoLabel and truncates to "Press the shortcut button to ..." even with scoreboard off.
		const SSettingsLinkPageLayout Page = SettingsLinkPageLayout(FontSize);
		const float TitleW = TextRender()->TextWidth(Page.TitleFont, pCrumb) * 1.12f;
		const float HintW = TextRender()->TextWidth(Page.HintFont, SETTINGS_LINK_PAGE_HINT) * 1.12f;
		return Page.Margin * 2.0f + maximum(TitleW, HintW) + TrailingPad;
	}

	const SSettingsLinkPageLayout Page = SettingsLinkPageLayout(FontSize);
	if(pCrumb[0] != '\0')
		Width = maximum(Width, Layout.Margin * 2.0f + TrailingPad + TextRender()->TextWidth(Layout.CrumbFontSize, pCrumb) * 1.12f);

	auto ResolveLabel = [&](const char *pScriptName, const char *pFallback) -> const char * {
		// Must use LookupVarLabel (map storage), never a stack SVarLocation::m_aLabel pointer.
		if(const char *pRegistered = CUClientSettingsLink::LookupVarLabel(pScriptName))
			return pRegistered;
		// Prefer short UI/fallback strings (Localize English keys) over long help text.
		if(pFallback && pFallback[0] != '\0' && str_utf8_check(pFallback))
			return Localize(pFallback);
		const SConfigVariable *pVar = CUClientSettingsLink::FindVariable(ConfigManager(), pScriptName);
		if(pVar && pVar->m_pHelp && pVar->m_pHelp[0] != '\0')
			return Localize(pVar->m_pHelp);
		return pScriptName;
	};
	auto Widen = [&](const char *pScriptName, const char *pLabel) {
		const SConfigVariable *pVar = CUClientSettingsLink::FindVariable(ConfigManager(), pScriptName);
		Width = maximum(Width, Layout.Margin * 2.0f + TrailingPad + SettingsLinkControlWidth(TextRender(), pVar, pLabel, Layout) * 1.08f);
	};
	for(int i = 0; i < Parsed.m_NumParents; ++i)
	{
		const char *pParentLabel = Parsed.m_aaParentLabels[i][0] != '\0' ? Parsed.m_aaParentLabels[i] : nullptr;
		const char *pParentName = CUClientSettingsLink::ParentScriptName(Parsed.m_aaParents[i]);
		Widen(pParentName, ResolveLabel(pParentName, pParentLabel));
	}
	Widen(Parsed.m_aScriptName, ResolveLabel(Parsed.m_aScriptName, Parsed.m_aLabel[0] != '\0' ? Parsed.m_aLabel : nullptr));
	const float HintW = TextRender()->TextWidth(Page.HintFont, SETTINGS_LINK_PAGE_HINT) * 1.12f;
	Width = maximum(Width, Layout.Margin * 2.0f + TrailingPad + HintW);
	return Width;
}

void CMenus::RenderSettingsLinkInline(const CUClientSettingsLink::SParsed &Parsed, bool Missing, bool PageOnly, CUIRect Rect, float FontSize, float Blend, vec2 MousePos)
{
	const SSettingsLinkChatLayout Layout = SettingsLinkChatLayout(FontSize);
	const float Fs = maximum(5.0f, FontSize);
	const ColorRGBA BlockColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.28f * Blend);
	const bool MouseDown = Ui()->MouseButton(0) != 0;

	CUIRect Card = Rect;
	const SSettingsLinkPageLayout Page = SettingsLinkPageLayout(FontSize);

	Card.Draw(BlockColor, IGraphics::CORNER_ALL, Layout.Rounding);
	Card.VMargin(PageOnly ? Page.Margin : Layout.Margin, &Card);
	Card.HMargin(PageOnly ? Page.Margin : Layout.Margin, &Card);

	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->SetRenderFlags(0);

	if(Missing)
	{
		const char *pMissingText = PageOnly ? "This client does not have this tab." : "This client does not have this setting.";
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->SetRenderFlags(0);
		TextRender()->TextColor(1.0f, 0.35f, 0.35f, Blend);
		SLabelProperties MissingProps;
		MissingProps.m_MaxWidth = Card.w;
		MissingProps.m_EllipsisAtEnd = true;
		MissingProps.m_EnableWidthCheck = false;
		Ui()->DoLabel(&Card, pMissingText, Layout.LabelSize, TEXTALIGN_ML, MissingProps);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		return;
	}

	char aBreadcrumb[128];
	CUClientSettingsLink::FormatBreadcrumb(Parsed, aBreadcrumb, sizeof(aBreadcrumb));
	const char *pCrumb = aBreadcrumb[0] != '\0' ? aBreadcrumb : Parsed.m_aPage;

	// Hint may wrap when the card is clamped narrow (scoreboard column); reserve the
	// wrapped height so it is never ellipsized.
	const float HintH = SettingsLinkHintHeight(TextRender(), Page, Card.w);

	auto DrawShortcutHint = [&](CUIRect HintRow) {
		HintRow.HSplitTop(Page.HintGap, nullptr, &HintRow);
		TextRender()->TextColor(0.72f, 0.72f, 0.76f, 0.9f * Blend);
		SLabelProperties HintProps;
		HintProps.m_MaxWidth = HintRow.w;
		// Wrap instead of ellipsize; the reserved HintH matches the wrapped bounding box
		// so the extra lines stay inside the card.
		HintProps.m_EllipsisAtEnd = false;
		HintProps.m_EnableWidthCheck = false;
		Ui()->DoLabel(&HintRow, SETTINGS_LINK_PAGE_HINT, Page.HintFont, TEXTALIGN_ML, HintProps);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	};

	if(PageOnly)
	{
		CUIRect TitleRow, HintRow;
		Card.HSplitBottom(Page.HintGap + HintH, &TitleRow, &HintRow);
		// Title may still ellipsis if MaxWidth was clamped (scoreboard).
		DrawSettingsLinkClippedLabel(Ui(), TextRender(), TitleRow, pCrumb, Page.TitleFont, 0.9f * Blend);
		DrawShortcutHint(HintRow);
		return;
	}

	CUIRect HintRow;
	Card.HSplitBottom(Page.HintGap + HintH, &Card, &HintRow);

	if(pCrumb[0] != '\0')
	{
		CUIRect Crumb;
		Card.HSplitTop(Layout.CrumbH, &Crumb, &Card);
		Card.HSplitTop(Layout.CrumbGap, nullptr, &Card);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.65f * Blend);
		SLabelProperties CrumbProps;
		CrumbProps.m_MaxWidth = Crumb.w;
		CrumbProps.m_EllipsisAtEnd = true;
		CrumbProps.m_EnableWidthCheck = false;
		Ui()->DoLabel(&Crumb, pCrumb, Layout.CrumbFontSize, TEXTALIGN_ML, CrumbProps);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	auto MouseIn = [&](const CUIRect &R) {
		return MousePos.x >= R.x && MousePos.x <= R.x + R.w && MousePos.y >= R.y && MousePos.y <= R.y + R.h;
	};

	auto DrawControl = [&](const char *pScriptName, const char *pLabel) {
		const SConfigVariable *pVar = CUClientSettingsLink::FindVariable(ConfigManager(), pScriptName);
		if(!pVar)
			return;
		const char *pText = (pLabel && pLabel[0] != '\0') ? pLabel : pScriptName;

		if(pVar->m_Type == SConfigVariable::VAR_INT)
		{
			auto *pInt = static_cast<const SIntConfigVariable *>(pVar);
			CUIRect Row;
			Card.HSplitTop(Layout.LineSize, &Row, &Card);
			if(pInt->m_Min == 0 && pInt->m_Max == 1)
			{
				const bool Hovered = MouseIn(Row);
				DrawSettingsLinkCheckboxRow(Ui(), TextRender(), Row, pText, *pInt->m_pVariable != 0, Hovered, Hovered && MouseDown, Blend, Layout.LabelGap, Layout.BoxMargin);
			}
			else
			{
				char aValue[64];
				str_format(aValue, sizeof(aValue), "%s: %d", pText, *pInt->m_pVariable);
				DrawSettingsLinkClippedLabel(Ui(), TextRender(), Row, aValue, Layout.LabelSize, Blend);
			}
			return;
		}

		if(pVar->m_Type == SConfigVariable::VAR_COLOR)
		{
			auto *pColor = static_cast<const SColorConfigVariable *>(pVar);
			CUIRect Row, Swatch, Label;
			Card.HSplitTop(Layout.ColorH, &Row, &Card);
			Card.HSplitTop(Layout.ColorGap, nullptr, &Card);
			Row.VSplitLeft(Layout.ColorH, &Swatch, &Label);
			Label.VSplitLeft(Layout.LabelGap, nullptr, &Label);
			const bool Hovered = MouseIn(Row);
			const float ColorMul = (Hovered && MouseDown) ? Ui()->ButtonColorMulActive() : (Hovered ? Ui()->ButtonColorMulHot() : Ui()->ButtonColorMulDefault());
			Swatch.Margin(Layout.BoxMargin, &Swatch);
			Swatch.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * ColorMul * Blend), IGraphics::CORNER_ALL, maximum(2.0f, Swatch.h * 0.22f));
			CUIRect Inner = Swatch;
			Inner.Margin(1.0f, &Inner);
			Inner.Draw(color_cast<ColorRGBA>(ColorHSLA(*pColor->m_pVariable, pColor->m_Alpha)), IGraphics::CORNER_ALL, 2.0f);
			DrawSettingsLinkClippedLabel(Ui(), TextRender(), Label, pText, Layout.LabelSize, Blend);
			return;
		}

		if(pVar->m_Type == SConfigVariable::VAR_STRING)
		{
			auto *pStr = static_cast<const SStringConfigVariable *>(pVar);
			CUIRect Row;
			Card.HSplitTop(Layout.LineSize, &Row, &Card);
			char aLine[256];
			str_format(aLine, sizeof(aLine), "%s: %s", pText, pStr->m_pStr);
			DrawSettingsLinkClippedLabel(Ui(), TextRender(), Row, aLine, Layout.LabelSize, Blend);
		}
	};

	auto ResolveLabel = [&](const char *pScriptName, const char *pFallback) -> const char * {
		if(const char *pRegistered = CUClientSettingsLink::LookupVarLabel(pScriptName))
			return pRegistered;
		if(pFallback && pFallback[0] != '\0' && str_utf8_check(pFallback))
			return Localize(pFallback);
		const SConfigVariable *pVar = CUClientSettingsLink::FindVariable(ConfigManager(), pScriptName);
		if(pVar && pVar->m_pHelp && pVar->m_pHelp[0] != '\0')
			return Localize(pVar->m_pHelp);
		return pScriptName;
	};
	for(int i = 0; i < Parsed.m_NumParents; ++i)
	{
		const char *pParentLabel = Parsed.m_aaParentLabels[i][0] != '\0' ? Parsed.m_aaParentLabels[i] : nullptr;
		const char *pParentName = CUClientSettingsLink::ParentScriptName(Parsed.m_aaParents[i]);
		DrawControl(pParentName, ResolveLabel(pParentName, pParentLabel));
	}
	DrawControl(Parsed.m_aScriptName, ResolveLabel(Parsed.m_aScriptName, Parsed.m_aLabel[0] != '\0' ? Parsed.m_aLabel : nullptr));
	DrawShortcutHint(HintRow);
}

bool CMenus::TryClickSettingsLinkInline(const CUClientSettingsLink::SParsed &Parsed, bool Missing, bool PageOnly, const CUIRect &Rect, float FontSize, vec2 MousePos)
{
	if(Missing || PageOnly)
		return false;
	if(MousePos.x < Rect.x || MousePos.x > Rect.x + Rect.w || MousePos.y < Rect.y || MousePos.y > Rect.y + Rect.h)
		return false;

	const SSettingsLinkChatLayout Layout = SettingsLinkChatLayout(FontSize);
	const SSettingsLinkPageLayout Page = SettingsLinkPageLayout(FontSize);
	CUIRect Content = Rect;
	Content.VMargin(Layout.Margin, &Content);
	Content.HMargin(Layout.Margin, &Content);
	// Hint row is not interactive — keep hit-testing aligned with controls only.
	// Mirror the (possibly wrapped) hint height used when rendering.
	const float HintH = SettingsLinkHintHeight(TextRender(), Page, Content.w);
	Content.HSplitBottom(Page.HintGap + HintH, &Content, nullptr);

	char aBreadcrumb[128];
	CUClientSettingsLink::FormatBreadcrumb(Parsed, aBreadcrumb, sizeof(aBreadcrumb));
	if(aBreadcrumb[0] != '\0' || Parsed.m_aPage[0] != '\0')
	{
		Content.HSplitTop(Layout.CrumbH, nullptr, &Content);
		Content.HSplitTop(Layout.CrumbGap, nullptr, &Content);
	}

	auto TryToggle = [&](const char *pScriptName) -> bool {
		const SConfigVariable *pVar = CUClientSettingsLink::FindVariable(ConfigManager(), pScriptName);
		if(!pVar)
			return false;
		CUIRect Row;
		if(pVar->m_Type == SConfigVariable::VAR_COLOR)
		{
			Content.HSplitTop(Layout.ColorH + Layout.ColorGap, &Row, &Content);
			return false;
		}
		Content.HSplitTop(Layout.LineSize, &Row, &Content);
		if(pVar->m_Type != SConfigVariable::VAR_INT)
			return false;
		auto *pInt = static_cast<SIntConfigVariable *>(const_cast<SConfigVariable *>(pVar));
		if(pInt->m_Min != 0 || pInt->m_Max != 1)
			return false;
		if(MousePos.x < Row.x || MousePos.x > Row.x + Row.w || MousePos.y < Row.y || MousePos.y > Row.y + Row.h)
			return false;
		// Run through the console so registered conchains fire (e.g. coupled toggles like
		// uc_chat_show/send_same_server_only). Writing *m_pVariable directly bypasses them.
		const int NewValue = *pInt->m_pVariable ? 0 : 1;
		char aCmd[128];
		str_format(aCmd, sizeof(aCmd), "%s %d", pVar->m_pScriptName, NewValue);
		Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
		return true;
	};

	for(int i = 0; i < Parsed.m_NumParents; ++i)
	{
		if(TryToggle(CUClientSettingsLink::ParentScriptName(Parsed.m_aaParents[i])))
			return true;
	}
	return TryToggle(Parsed.m_aScriptName);
}

bool CMenus::ConsumeSettingsLinkNavBestClientTab(int &Tab)
{
	if(m_SettingsLinkNavBestClientTab < 0)
		return false;
	Tab = m_SettingsLinkNavBestClientTab;
	m_SettingsLinkNavBestClientTab = -1;
	return true;
}

bool CMenus::ConsumeSettingsLinkNavTClientTab(int &Tab)
{
	if(m_SettingsLinkNavTClientTab < 0)
		return false;
	Tab = m_SettingsLinkNavTClientTab;
	m_SettingsLinkNavTClientTab = -1;
	return true;
}

bool CMenus::ConsumeSettingsLinkNavUClientTab(int &Tab)
{
	if(m_SettingsLinkNavUClientTab < 0)
		return false;
	Tab = m_SettingsLinkNavUClientTab;
	m_SettingsLinkNavUClientTab = -1;
	return true;
}

bool CMenus::ConsumeSettingsLinkNavAssetsTab(int &Tab)
{
	if(m_SettingsLinkNavAssetsTab < 0)
		return false;
	Tab = m_SettingsLinkNavAssetsTab;
	m_SettingsLinkNavAssetsTab = -1;
	return true;
}

bool CMenus::ConsumeSettingsLinkNavControlsMode(int &Mode)
{
	if(m_SettingsLinkNavControlsMode < 0)
		return false;
	Mode = m_SettingsLinkNavControlsMode;
	m_SettingsLinkNavControlsMode = -1;
	return true;
}

CUi::EPopupMenuFunctionResult CMenus::PopupSettingsLinkCopy(void *pContext, CUIRect View, bool Active)
{
	CMenus *pMenus = static_cast<CMenus *>(pContext);
	(void)Active;
	CUIRect Row;
	View.HSplitTop(18.0f, &Row, &View);
	const char *pButtonText = pMenus->m_SettingsLinkCopyIsTab ? "Copy Tab Link" : "Copy Setting Link";
	if(pMenus->DoButton_Menu(&pMenus->m_SettingsLinkCopyButton, pButtonText, 0, &Row))
	{
		if(pMenus->m_aSettingsLinkPendingUri[0] != '\0')
			pMenus->Input()->SetClipboardText(pMenus->m_aSettingsLinkPendingUri);
		return CUi::POPUP_CLOSE_CURRENT;
	}
	return CUi::POPUP_KEEP_OPEN;
}
