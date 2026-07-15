/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_settings_controls.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/console.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/textrender.h>

#include <game/client/components/binds.h>
#include <game/client/components/key_binder.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

inline constexpr float HEADER_FONT_SIZE = 16.0f;
inline constexpr float FONT_SIZE = 13.0f;
inline constexpr float MARGIN = 10.0f;
inline constexpr float BUTTON_HEIGHT = 20.0f;
inline constexpr float BUTTON_SPACING = 2.0f;
inline constexpr float BIND_OPTION_SPACING = 4.0f;
inline constexpr int MAX_SENSITIVITY = 1000000;
inline constexpr int MAX_SENSITIVITY_SLIDER = 500;

namespace
{
bool DoSensitivityInput(CUi *pUi, CLineInputNumber *pInput, int *pOption, const CUIRect *pRect, const char *pLabel)
{
	CUIRect Label, EditBox;
	pRect->VSplitLeft(210.0f, &Label, &EditBox);
	pUi->DoLabel(&Label, pLabel, FONT_SIZE, TEXTALIGN_ML);
	pInput->SetEmptyText("1-1000000");

	if(!pInput->IsActive())
	{
		pInput->SetInteger(*pOption);
	}

	if(pUi->DoEditBox(pInput, &EditBox, 14.0f))
	{
		if(pInput->GetLength() > 0)
		{
			*pOption = maximum(1, minimum(MAX_SENSITIVITY, pInput->GetInteger()));
			if(!pInput->IsActive())
			{
				pInput->SetInteger(*pOption);
			}
		}
		return true;
	}

	return false;
}
} // namespace

bool CBindSlotUiElement::operator<(const CBindSlotUiElement &Other) const
{
	if(m_Bind == EMPTY_BIND_SLOT)
	{
		return false;
	}
	if(Other.m_Bind == EMPTY_BIND_SLOT)
	{
		return true;
	}
	return m_Bind.m_ModifierMask < Other.m_Bind.m_ModifierMask ||
	       m_Bind.m_Key < Other.m_Bind.m_Key;
}

std::vector<CBindSlotUiElement>::iterator CBindOption::GetBindSlotElement(const CBindSlot &BindSlot)
{
	return std::find_if(m_vCurrentBinds.begin(), m_vCurrentBinds.end(), [&](const CBindSlotUiElement &BindSlotUiElement) {
		return BindSlotUiElement.m_Bind == BindSlot;
	});
}

bool CBindOption::MatchesSearch(const char *pSearch) const
{
	return (m_Group != EBindOptionGroup::CUSTOM && str_utf8_find_nocase(Localize(m_pLabel), pSearch) != nullptr) ||
	       str_utf8_find_nocase(m_Command.c_str(), pSearch) != nullptr;
}

void CMenusSettingsControls::OnInterfacesInit(CGameClient *pClient)
{
	CComponentInterfaces::OnInterfacesInit(pClient);

	m_vBindOptions = {
		{EBindOptionGroup::MOVEMENT, Localizable("Move left"), "+left"},
		{EBindOptionGroup::MOVEMENT, Localizable("Move right"), "+right"},
		{EBindOptionGroup::MOVEMENT, Localizable("Jump"), "+jump"},
		{EBindOptionGroup::MOVEMENT, Localizable("Fire"), "+fire"},
		{EBindOptionGroup::MOVEMENT, Localizable("Hook"), "+hook"},
		{EBindOptionGroup::MOVEMENT, Localizable("Hook collisions"), "+showhookcoll"},
		{EBindOptionGroup::MOVEMENT, Localizable("Pause"), "say /pause"},
		{EBindOptionGroup::MOVEMENT, Localizable("Kill"), "kill"},
		{EBindOptionGroup::MOVEMENT, Localizable("Zoom in"), "zoom+"},
		{EBindOptionGroup::MOVEMENT, Localizable("Zoom out"), "zoom-"},
		{EBindOptionGroup::MOVEMENT, Localizable("Default zoom"), "zoom"},
		{EBindOptionGroup::MOVEMENT, Localizable("Show others"), "say /showothers"},
		{EBindOptionGroup::MOVEMENT, Localizable("Show all"), "say /showall"},
		{EBindOptionGroup::MOVEMENT, Localizable("Toggle dyncam"), "toggle cl_dyncam 0 1"},
		{EBindOptionGroup::MOVEMENT, Localizable("Toggle ghost"), "toggle cl_race_show_ghost 0 1"},
		{EBindOptionGroup::WEAPON, Localizable("Hammer"), "+weapon1"},
		{EBindOptionGroup::WEAPON, Localizable("Pistol"), "+weapon2"},
		{EBindOptionGroup::WEAPON, Localizable("Shotgun"), "+weapon3"},
		{EBindOptionGroup::WEAPON, Localizable("Grenade"), "+weapon4"},
		{EBindOptionGroup::WEAPON, Localizable("Laser"), "+weapon5"},
		{EBindOptionGroup::WEAPON, Localizable("Next weapon"), "+nextweapon"},
		{EBindOptionGroup::WEAPON, Localizable("Prev. weapon"), "+prevweapon"},
		{EBindOptionGroup::VOTING, Localizable("Vote yes"), "vote yes"},
		{EBindOptionGroup::VOTING, Localizable("Vote no"), "vote no"},
		{EBindOptionGroup::CHAT, Localizable("Chat"), "+show_chat; chat all"},
		{EBindOptionGroup::CHAT, Localizable("Team chat"), "+show_chat; chat team"},
		{EBindOptionGroup::CHAT, Localizable("Converse"), "+show_chat; chat all /c "},
		{EBindOptionGroup::CHAT, Localizable("Chat command"), "+show_chat; chat all /"},
		{EBindOptionGroup::CHAT, Localizable("Show chat"), "+show_chat"},
		{EBindOptionGroup::DUMMY, Localizable("Toggle dummy"), "toggle cl_dummy 0 1"},
		{EBindOptionGroup::DUMMY, Localizable("Dummy copy"), "toggle cl_dummy_copy_moves 0 1"},
		{EBindOptionGroup::DUMMY, Localizable("Hammerfly dummy"), "toggle cl_dummy_hammer 0 1"},
		{EBindOptionGroup::BEST_CLIENT, Localizable("Dummy pseudo"), "+toggle cl_dummy_hammer 1 0"},
		{EBindOptionGroup::BEST_CLIENT, Localizable("Deepfly toggle"), "BC_deepfly_toggle"},
		{EBindOptionGroup::BEST_CLIENT, Localizable("45 deg bind"), "+BC_45_degrees"},
		{EBindOptionGroup::BEST_CLIENT, Localizable("Small sens bind"), "BC_small_sens"},
			{EBindOptionGroup::BEST_CLIENT, Localizable("Left jump"), "+jump; +left"},
			{EBindOptionGroup::BEST_CLIENT, Localizable("Right jump"), "+jump; +right"},
			{EBindOptionGroup::BEST_CLIENT, Localizable("Admin Panel"), "toggle_admin_panel"},
			{EBindOptionGroup::BEST_CLIENT_VOICE, Localizable("Voice panel"), "toggle_voice_panel"},
			{EBindOptionGroup::BEST_CLIENT_VOICE, Localizable("Push-to-talk"), "+voicechat"},
			{EBindOptionGroup::BEST_CLIENT_VOICE, Localizable("Mute microphone"), "toggle_voice_mic_mute"},
			{EBindOptionGroup::BEST_CLIENT_VOICE, Localizable("Mute headphones"), "toggle_voice_headphones_mute"},
			{EBindOptionGroup::BEST_CLIENT_PRACTICE, Localizable("say /r"), "say /r"},
		{EBindOptionGroup::BEST_CLIENT_PRACTICE, Localizable("say /invincible"), "say /invincible"},
		{EBindOptionGroup::BEST_CLIENT_PRACTICE, Localizable("say /telecursor"), "say /telecursor"},
		{EBindOptionGroup::BEST_CLIENT_PRACTICE, Localizable("say /weapons"), "say /weapons"},
		{EBindOptionGroup::BEST_CLIENT_PRACTICE, Localizable("say /unweapons"), "say /unweapons"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Emoticon"), "+emote"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Spectator mode"), "+spectate"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Spectate next"), "spectate_next"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Spectate previous"), "spectate_previous"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Console"), "toggle_local_console"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Remote console"), "toggle_remote_console"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Screenshot"), "screenshot"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Scoreboard"), "+scoreboard"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Scoreboard cursor"), "toggle_scoreboard_cursor"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Statboard"), "+statboard"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Lock team"), "say /lock"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Show entities"), "toggle cl_overlay_entities 0 100"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Show HUD"), "toggle cl_showhud 0 1"},
		{EBindOptionGroup::MISCELLANEOUS, Localizable("Share cursor (UClient)"), "+live_cursor"},
	};
	m_NumPredefinedBindOptions = m_vBindOptions.size();

	std::fill(std::begin(m_aBindGroupExpanded), std::end(m_aBindGroupExpanded), true);
	m_aBindGroupExpanded[(int)EBindOptionGroup::CUSTOM] = false;

	m_JoystickDropDownState.m_SelectionPopupContext.m_pScrollRegion = &m_JoystickDropDownScrollRegion;

	// One preset button per predefined bind option, for the visual editor's preset list.
	m_vBindEditPresetButtons.resize(m_NumPredefinedBindOptions);
}

void CMenusSettingsControls::Render(CUIRect MainView)
{
	UpdateBindOptions();

	// UClient: view mode switch (list of actions vs. on-screen keyboard)
	{
		CUIRect TopBar, ListButton, KeyboardButton;
		MainView.HSplitTop(BUTTON_HEIGHT, &TopBar, &MainView);
		MainView.HSplitTop(MARGIN, nullptr, &MainView);
		TopBar.VSplitLeft(130.0f, &ListButton, &TopBar);
		TopBar.VSplitLeft(BUTTON_SPACING, nullptr, &TopBar);
		TopBar.VSplitLeft(130.0f, &KeyboardButton, &TopBar);
		if(GameClient()->m_Menus.DoButton_Menu(&m_ViewListButton, Localize("Action list"), m_VisualBindMode ? 0 : 1, &ListButton))
		{
			m_VisualBindMode = false;
		}
		if(GameClient()->m_Menus.DoButton_Menu(&m_ViewKeyboardButton, Localize("Keyboard"), m_VisualBindMode ? 1 : 0, &KeyboardButton))
		{
			m_VisualBindMode = true;
		}
	}

	if(m_VisualBindMode)
	{
		RenderVisualBinds(MainView);
		return;
	}

	CUIRect QuickSearch, SearchMatches, ResetToDefault;
	MainView.HSplitBottom(BUTTON_HEIGHT, &MainView, &QuickSearch);
	QuickSearch.VSplitRight(200.0f, &QuickSearch, &ResetToDefault);
	QuickSearch.VSplitRight(MARGIN, &QuickSearch, nullptr);
	QuickSearch.VSplitRight(150.0f, &QuickSearch, &SearchMatches);
	QuickSearch.VSplitRight(MARGIN, &QuickSearch, nullptr);
	MainView.HSplitBottom(MARGIN, &MainView, nullptr);

	// Quick search
	if(Ui()->DoEditBox_Search(&m_FilterInput, &QuickSearch, FONT_SIZE, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive() && !GameClient()->m_KeyBinder.IsActive()))
	{
		m_CurrentSearchMatch = 0;
		UpdateSearchMatches();
		m_SearchMatchReveal = true;
	}
	else if(!m_vSearchMatches.empty() && (Ui()->ConsumeHotkey(CUi::EHotkey::HOTKEY_ENTER) || Ui()->ConsumeHotkey(CUi::EHotkey::HOTKEY_TAB)))
	{
		UpdateSearchMatches();
		m_CurrentSearchMatch += Input()->ShiftIsPressed() ? -1 : 1;
		if(m_CurrentSearchMatch >= (int)m_vSearchMatches.size())
		{
			m_CurrentSearchMatch = 0;
		}
		if(m_CurrentSearchMatch < 0)
		{
			m_CurrentSearchMatch = m_vSearchMatches.size() - 1;
		}
		m_SearchMatchReveal = true;
	}

	if(!m_FilterInput.IsEmpty())
	{
		if(!m_vSearchMatches.empty())
		{
			char aSearchMatchLabel[64];
			str_format(aSearchMatchLabel, sizeof(aSearchMatchLabel), Localize("Match %d of %d"), m_CurrentSearchMatch + 1, (int)m_vSearchMatches.size());
			Ui()->DoLabel(&SearchMatches, aSearchMatchLabel, FONT_SIZE, TEXTALIGN_MC);
		}
		else
		{
			Ui()->DoLabel(&SearchMatches, Localize("No results"), FONT_SIZE, TEXTALIGN_MC);
		}
	}

	// Reset to default button
	if(GameClient()->m_Menus.DoButton_Menu(&m_ResetToDefaultButton, Localize("Reset to defaults"), 0, &ResetToDefault))
	{
		GameClient()->m_Menus.PopupConfirm(Localize("Reset controls"), Localize("Are you sure that you want to reset the controls to their defaults?"),
			Localize("Reset"), Localize("Cancel"), &CMenus::ResetSettingsControls);
	}

	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 6.0f * BUTTON_HEIGHT;
	ScrollParams.m_Flags = CScrollRegionParams::FLAG_CONTENT_STATIC_WIDTH;
	m_SettingsScrollRegion.Begin(&MainView, &ScrollOffset, &ScrollParams);
	MainView.y += ScrollOffset.y;

	CUIRect LeftColumn, RightColumn;
	MainView.VSplitMid(&LeftColumn, &RightColumn, MARGIN);

	// Left column
	RenderSettingsBlock(MeasureSettingsMouseHeight(), &LeftColumn,
		Localize("Mouse"), nullptr, nullptr, std::bind_front(&CMenusSettingsControls::RenderSettingsMouse, this));
	RenderSettingsBlock(MeasureSettingsJoystickHeight(), &LeftColumn,
		Localize("Controller"), nullptr, nullptr, std::bind_front(&CMenusSettingsControls::RenderSettingsJoystick, this));
	RenderSettingsBindsBlock(EBindOptionGroup::MOVEMENT, &LeftColumn, Localize("Movement"));
	RenderSettingsBindsBlock(EBindOptionGroup::WEAPON, &LeftColumn, Localize("Weapon"));

	// Right column
	RenderSettingsBindsBlock(EBindOptionGroup::VOTING, &RightColumn, Localize("Voting"));
	RenderSettingsBindsBlock(EBindOptionGroup::CHAT, &RightColumn, Localize("Chat"));
	RenderSettingsBindsBlock(EBindOptionGroup::DUMMY, &RightColumn, Localize("Dummy"));
	RenderSettingsBindsBlock(EBindOptionGroup::BEST_CLIENT, &RightColumn, Localize("BestClient"));
	RenderSettingsBindsBlock(EBindOptionGroup::BEST_CLIENT_VOICE, &RightColumn, Localize("BestClient Voice"));
	RenderSettingsBindsBlock(EBindOptionGroup::BEST_CLIENT_PRACTICE, &RightColumn, Localize("BestClient Practice"));
	RenderSettingsBindsBlock(EBindOptionGroup::MISCELLANEOUS, &RightColumn, Localize("Miscellaneous"));
	if(std::any_of(m_vBindOptions.begin(), m_vBindOptions.end(), [](const CBindOption &Option) { return Option.m_Group == EBindOptionGroup::CUSTOM; }))
	{
		RenderSettingsBindsBlock(EBindOptionGroup::CUSTOM, &RightColumn, Localize("Custom"));
	}

	m_SettingsScrollRegion.End();
}

void CMenusSettingsControls::UpdateBindOptions()
{
	for(CBindOption &Option : m_vBindOptions)
	{
		for(CBindSlotUiElement &BindSlot : Option.m_vCurrentBinds)
		{
			if(BindSlot.m_Bind != EMPTY_BIND_SLOT)
			{
				BindSlot.m_ToBeDeleted = true;
			}
		}
	}

	for(int Mod = KeyModifier::NONE; Mod < KeyModifier::COMBINATION_COUNT; Mod++)
	{
		for(int KeyId = KEY_FIRST; KeyId < KEY_LAST; KeyId++)
		{
			const CBindSlot BindSlot = CBindSlot(KeyId, Mod);
			const char *pBind = GameClient()->m_Binds.Get(BindSlot);
			if(!pBind[0])
			{
				continue;
			}

			auto ExistingOption = std::find_if(m_vBindOptions.begin(), m_vBindOptions.end(), [pBind](const CBindOption &Option) {
				return str_comp(pBind, Option.m_Command.c_str()) == 0;
			});
			if(ExistingOption == m_vBindOptions.end())
			{
				// Bind option not found for command, add custom bind option.
				CBindOption NewOption = {EBindOptionGroup::CUSTOM, nullptr, pBind};
				ExistingOption = m_vBindOptions.insert(
					std::upper_bound(m_vBindOptions.begin() + m_NumPredefinedBindOptions, m_vBindOptions.end(), NewOption, [&](const CBindOption &Option1, const CBindOption &Option2) {
						return str_utf8_comp_nocase(Option1.m_Command.c_str(), Option2.m_Command.c_str()) < 0;
					}),
					NewOption);

				// Update search matches due to new option being added.
				if(!m_FilterInput.IsEmpty())
				{
					const int OptionIndex = ExistingOption - m_vBindOptions.begin();
					for(int &SearchMatch : m_vSearchMatches)
					{
						if(OptionIndex <= SearchMatch)
						{
							++SearchMatch;
						}
					}
					if(ExistingOption->MatchesSearch(m_FilterInput.GetString()))
					{
						const int MatchIndex = m_vSearchMatches.insert(std::upper_bound(m_vSearchMatches.begin(), m_vSearchMatches.end(), OptionIndex), OptionIndex) - m_vSearchMatches.begin();
						if(MatchIndex <= m_CurrentSearchMatch)
						{
							++m_CurrentSearchMatch;
						}
					}
				}
			}
			auto ExistingBindSlot = ExistingOption->GetBindSlotElement(BindSlot);
			if(ExistingBindSlot == ExistingOption->m_vCurrentBinds.end())
			{
				// Remove empty bind slot if one is present because it will be replaced with a bind slot for the new bind.
				auto ExistingEmptyBindSlot = ExistingOption->GetBindSlotElement(EMPTY_BIND_SLOT);
				if(ExistingEmptyBindSlot != ExistingOption->m_vCurrentBinds.end())
				{
					ExistingOption->m_vCurrentBinds.erase(ExistingEmptyBindSlot);
				}

				CBindSlotUiElement BindSlotUiElement = {BindSlot};
				ExistingOption->m_vCurrentBinds.insert(
					std::upper_bound(ExistingOption->m_vCurrentBinds.begin(), ExistingOption->m_vCurrentBinds.end(), BindSlotUiElement),
					BindSlotUiElement);
			}
			else
			{
				ExistingBindSlot->m_ToBeDeleted = false;
			}
		}
	}

	// Remove bind slots that are not bound anymore,
	// mark unused custom bind options for removal.
	for(CBindOption &Option : m_vBindOptions)
	{
		Option.m_vCurrentBinds.erase(std::remove_if(Option.m_vCurrentBinds.begin(), Option.m_vCurrentBinds.end(),
						     [&](const CBindSlotUiElement &BindSlotUiElement) { return BindSlotUiElement.m_ToBeDeleted; }),
			Option.m_vCurrentBinds.end());

		Option.m_ToBeDeleted = Option.m_vCurrentBinds.empty() && Option.m_Group == EBindOptionGroup::CUSTOM;
		if(Option.m_ToBeDeleted)
		{
			continue;
		}

		if(Option.m_vCurrentBinds.empty() ||
			(Option.m_AddNewBind && Option.GetBindSlotElement(EMPTY_BIND_SLOT) == Option.m_vCurrentBinds.end()))
		{
			Option.m_vCurrentBinds.emplace_back(EMPTY_BIND_SLOT);
		}
	}

	// Update search matches when removing bind options.
	for(const CBindOption &Option : m_vBindOptions)
	{
		if(!Option.m_ToBeDeleted)
		{
			continue;
		}
		const int OptionIndex = &Option - m_vBindOptions.data();
		auto ExactSearchMatch = std::find(m_vSearchMatches.begin(), m_vSearchMatches.end(), OptionIndex);
		if(ExactSearchMatch != m_vSearchMatches.end())
		{
			m_vSearchMatches.erase(ExactSearchMatch);
			if((int)(ExactSearchMatch - m_vSearchMatches.begin()) < m_CurrentSearchMatch)
			{
				--m_CurrentSearchMatch;
			}
		}
		for(int &SearchMatch : m_vSearchMatches)
		{
			if(OptionIndex < SearchMatch)
			{
				--SearchMatch;
			}
		}
	}
	if(m_vSearchMatches.empty())
	{
		m_CurrentSearchMatch = 0;
	}
	else if(m_CurrentSearchMatch >= (int)m_vSearchMatches.size())
	{
		m_CurrentSearchMatch = m_vSearchMatches.size() - 1;
	}

	// Remove unused bind options.
	m_vBindOptions.erase(std::remove_if(m_vBindOptions.begin() + m_NumPredefinedBindOptions, m_vBindOptions.end(),
				     [&](const CBindOption &Option) { return Option.m_ToBeDeleted; }),
		m_vBindOptions.end());
}

void CMenusSettingsControls::UpdateSearchMatches()
{
	m_vSearchMatches.clear();

	if(!m_FilterInput.IsEmpty())
	{
		for(CBindOption &Option : m_vBindOptions)
		{
			if(!Option.MatchesSearch(m_FilterInput.GetString()))
			{
				continue;
			}

			m_aBindGroupExpanded[(int)Option.m_Group] = true;
			m_vSearchMatches.emplace_back(&Option - m_vBindOptions.data());
		}
	}

	if(m_vSearchMatches.empty())
	{
		m_CurrentSearchMatch = 0;
	}
	else if(m_CurrentSearchMatch >= (int)m_vSearchMatches.size())
	{
		m_CurrentSearchMatch = m_vSearchMatches.size() - 1;
	}
}

void CMenusSettingsControls::RenderSettingsBlock(float Height, CUIRect *pParentRect, const char *pTitle,
	bool *pExpanded, CButtonContainer *pExpandButton, const std::function<void(CUIRect Rect)> &RenderContentFunction)
{
	const bool WasExpanded = pExpanded == nullptr || *pExpanded;
	float FullHeight = WasExpanded ? Height : 0.0f; // Content
	FullHeight += pTitle == nullptr ? 0.0f : HEADER_FONT_SIZE + (WasExpanded ? MARGIN : 0.0f); // Title and spacing
	FullHeight += 2.0f * MARGIN; // Margin

	CUIRect SettingsBlock;
	pParentRect->HSplitTop(FullHeight, &SettingsBlock, pParentRect);
	pParentRect->HSplitTop(MARGIN, nullptr, pParentRect);
	if(m_SettingsScrollRegion.AddRect(SettingsBlock) || m_SearchMatchReveal)
	{
		SettingsBlock.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, pExpandButton == nullptr || Ui()->HotItem() != pExpandButton ? 0.25f : 0.3f), IGraphics::CORNER_ALL, 10.0f);
		SettingsBlock.Margin(MARGIN, &SettingsBlock);

		if(pTitle != nullptr)
		{
			CUIRect Label;
			SettingsBlock.HSplitTop(HEADER_FONT_SIZE, &Label, &SettingsBlock);
			if(WasExpanded)
			{
				SettingsBlock.HSplitTop(MARGIN, nullptr, &SettingsBlock);
			}

			if(pExpanded != nullptr)
			{
				CUIRect ButtonArea;
				Label.Margin(-MARGIN, &ButtonArea);
				if(Ui()->DoButtonLogic(pExpandButton, 0, &ButtonArea, BUTTONFLAG_LEFT, CUi::EButtonSoundType::BUTTON))
				{
					*pExpanded = !*pExpanded;
				}

				CUIRect ExpandButton;
				Label.VSplitRight(20.0f, &Label, &ExpandButton);
				Label.VSplitRight(BUTTON_SPACING, &Label, nullptr);
				if(m_SettingsScrollRegion.AddRect(ExpandButton))
				{
					SLabelProperties Props;
					Props.SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.65f * Ui()->ButtonColorMul(pExpandButton)));
					Props.m_EnableWidthCheck = false;
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
					Ui()->DoLabel(&ExpandButton, *pExpanded ? FontIcon::CHEVRON_UP : FontIcon::CHEVRON_DOWN, HEADER_FONT_SIZE, TEXTALIGN_MR, Props);
					TextRender()->SetRenderFlags(0);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
				}
			}

			if(m_SettingsScrollRegion.AddRect(Label))
			{
				Ui()->DoLabel(&Label, pTitle, HEADER_FONT_SIZE, TEXTALIGN_ML);
			}
		}

		if(WasExpanded)
		{
			RenderContentFunction(SettingsBlock);
		}
	}
}

void CMenusSettingsControls::RenderSettingsBindsBlock(EBindOptionGroup Group, CUIRect *pParentRect, const char *pTitle)
{
	RenderSettingsBlock(MeasureSettingsBindsHeight(Group), pParentRect, pTitle,
		&m_aBindGroupExpanded[(int)Group], &m_aBindGroupExpandButtons[(int)Group],
		[&](CUIRect Rect) { RenderSettingsBinds(Group, Rect); });
}

float CMenusSettingsControls::MeasureSettingsBindsHeight(EBindOptionGroup Group) const
{
	float Height = 0.0f;
	for(const CBindOption &BindOption : m_vBindOptions)
	{
		if(BindOption.m_Group != Group)
		{
			continue;
		}
		if(Height > 0.0f)
		{
			Height += BIND_OPTION_SPACING;
		}
		Height += BUTTON_HEIGHT * BindOption.m_vCurrentBinds.size() + BUTTON_SPACING * (BindOption.m_vCurrentBinds.size() - 1) + BIND_OPTION_SPACING;
	}
	return Height;
}

void CMenusSettingsControls::RenderSettingsBinds(EBindOptionGroup Group, CUIRect View)
{
	for(CBindOption &BindOption : m_vBindOptions)
	{
		if(BindOption.m_Group != Group)
		{
			continue;
		}

		CUIRect KeyReaders;
		View.HSplitTop(BUTTON_HEIGHT * BindOption.m_vCurrentBinds.size() + BUTTON_SPACING * (BindOption.m_vCurrentBinds.size() - 1) + 4.0f, &KeyReaders, &View);
		View.HSplitTop(BIND_OPTION_SPACING, nullptr, &View);
		if(!m_SettingsScrollRegion.AddRect(KeyReaders) && !m_SearchMatchReveal)
		{
			continue;
		}
		KeyReaders.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f), IGraphics::CORNER_ALL, 5.0f);
		KeyReaders.Margin(2.0f, &KeyReaders);

		CUIRect Label, AddButton;
		KeyReaders.VSplitLeft(KeyReaders.w / 3.0f, &Label, &KeyReaders);
		KeyReaders.VSplitLeft(5.0f, nullptr, &KeyReaders);
		KeyReaders.VSplitLeft(BUTTON_HEIGHT, &AddButton, &KeyReaders);
		AddButton.HSplitTop(BUTTON_HEIGHT, &AddButton, nullptr);
		KeyReaders.VSplitLeft(2.0f, nullptr, &KeyReaders);
		Label.HSplitTop(BUTTON_HEIGHT, &Label, nullptr);

		const auto SearchMatch = std::find(m_vSearchMatches.begin(), m_vSearchMatches.end(), &BindOption - m_vBindOptions.data());
		const bool SearchMatchSelected = SearchMatch != m_vSearchMatches.end() && m_CurrentSearchMatch == (int)(SearchMatch - m_vSearchMatches.begin());
		if(SearchMatchSelected && m_SearchMatchReveal)
		{
			m_SearchMatchReveal = false;
			// Scroll to reveal search match
			CUIRect ScrollTarget;
			Label.HMargin(-MARGIN, &ScrollTarget);
			m_SettingsScrollRegion.AddRect(ScrollTarget, true);
		}
		SLabelProperties LabelProps = {.m_MaxWidth = Label.w, .m_EllipsisAtEnd = BindOption.m_Group == EBindOptionGroup::CUSTOM, .m_MinimumFontSize = 9.0f};
		if(SearchMatchSelected)
		{
			LabelProps.SetColor(ColorRGBA(0.1f, 0.1f, 1.0f, 1.0f));
		}
		else if(SearchMatch != m_vSearchMatches.end())
		{
			LabelProps.SetColor(ColorRGBA(0.4f, 0.4f, 0.9f, 1.0f));
		}
		const CLabelResult LabelResult = Ui()->DoLabel(&Label, BindOption.m_Group == EBindOptionGroup::CUSTOM ? BindOption.m_Command.c_str() : Localize(BindOption.m_pLabel),
			FONT_SIZE, TEXTALIGN_ML, LabelProps);
		if(BindOption.m_Group != EBindOptionGroup::CUSTOM || LabelResult.m_Truncated)
		{
			Ui()->DoButtonLogic(&BindOption.m_TooltipButtonId, 0, &Label, BUTTONFLAG_NONE, CUi::EButtonSoundType::SILENT);
			GameClient()->m_Tooltips.DoToolTip(&BindOption.m_TooltipButtonId, &Label, BindOption.m_Command.c_str());
		}

		for(CBindSlotUiElement &CurrentBind : BindOption.m_vCurrentBinds)
		{
			CUIRect KeyReader;
			KeyReaders.HSplitTop(BUTTON_HEIGHT, &KeyReader, &KeyReaders);
			KeyReaders.HSplitTop(BUTTON_SPACING, nullptr, &KeyReaders);
			const bool ActivateKeyReader = BindOption.m_AddNewBindActivate && CurrentBind.m_Bind == EMPTY_BIND_SLOT;
			const CKeyBinder::CKeyReaderResult KeyReaderResult = GameClient()->m_KeyBinder.DoKeyReader(
				&CurrentBind.m_KeyReaderButton, &CurrentBind.m_KeyResetButton,
				&KeyReader, CurrentBind.m_Bind, ActivateKeyReader);
			if(ActivateKeyReader)
			{
				BindOption.m_AddNewBindActivate = false;
				// Scroll to reveal activated key reader
				CUIRect ScrollTarget;
				KeyReader.HMargin(-MARGIN, &ScrollTarget);
				m_SettingsScrollRegion.AddRect(ScrollTarget, true);
			}
			if(KeyReaderResult.m_Aborted)
			{
				BindOption.m_AddNewBind = false;
				if(CurrentBind.m_Bind == EMPTY_BIND_SLOT && (&CurrentBind - BindOption.m_vCurrentBinds.data()) > 0)
				{
					CurrentBind.m_ToBeDeleted = true;
				}
			}
			else if(KeyReaderResult.m_Bind != CurrentBind.m_Bind)
			{
				BindOption.m_AddNewBind = false;
				if(CurrentBind.m_Bind.m_Key != KEY_UNKNOWN || KeyReaderResult.m_Bind.m_Key == KEY_UNKNOWN)
				{
					GameClient()->m_Binds.Bind(CurrentBind.m_Bind.m_Key, "", false, CurrentBind.m_Bind.m_ModifierMask);
				}
				if(KeyReaderResult.m_Bind.m_Key != KEY_UNKNOWN)
				{
					GameClient()->m_Binds.Bind(KeyReaderResult.m_Bind.m_Key, BindOption.m_Command.c_str(), false, KeyReaderResult.m_Bind.m_ModifierMask);
				}
			}
		}

		if(Ui()->DoButton_FontIcon(&BindOption.m_AddBindButtonContainer, FontIcon::PLUS, BindOption.m_AddNewBind ? 1 : 0, &AddButton, BUTTONFLAG_LEFT))
		{
			BindOption.m_AddNewBind = true;
			BindOption.m_AddNewBindActivate = true;
		}
	}
}

// UClient: visual (on-screen keyboard) bind editor
namespace
{
struct SKeyCap
{
	int m_Key;
	const char *m_pLabel;
	float m_X, m_Y; // grid position in key-units (top-left)
	float m_W, m_H; // size in key-units
};

// Physical keyboard layout in key-units: main block on the left, nav cluster and
// inverted-T arrow keys on the right, just like a real keyboard. Grid is 18.5 x 6.5 units.
const std::vector<SKeyCap> &KeyboardLayout()
{
	static const std::vector<SKeyCap> s_Layout = {
		// Function row (y = 0)
		{KEY_ESCAPE, "Esc", 0.0f, 0.0f, 1.0f, 1.0f},
		{KEY_F1, "F1", 2.0f, 0.0f, 1.0f, 1.0f}, {KEY_F2, "F2", 3.0f, 0.0f, 1.0f, 1.0f}, {KEY_F3, "F3", 4.0f, 0.0f, 1.0f, 1.0f}, {KEY_F4, "F4", 5.0f, 0.0f, 1.0f, 1.0f},
		{KEY_F5, "F5", 6.5f, 0.0f, 1.0f, 1.0f}, {KEY_F6, "F6", 7.5f, 0.0f, 1.0f, 1.0f}, {KEY_F7, "F7", 8.5f, 0.0f, 1.0f, 1.0f}, {KEY_F8, "F8", 9.5f, 0.0f, 1.0f, 1.0f},
		{KEY_F9, "F9", 11.0f, 0.0f, 1.0f, 1.0f}, {KEY_F10, "F10", 12.0f, 0.0f, 1.0f, 1.0f}, {KEY_F11, "F11", 13.0f, 0.0f, 1.0f, 1.0f}, {KEY_F12, "F12", 14.0f, 0.0f, 1.0f, 1.0f},
		{KEY_PRINTSCREEN, "Prt", 15.5f, 0.0f, 1.0f, 1.0f}, {KEY_SCROLLLOCK, "ScLk", 16.5f, 0.0f, 1.0f, 1.0f}, {KEY_PAUSE, "Paus", 17.5f, 0.0f, 1.0f, 1.0f},

		// Number row (y = 1.5)
		{KEY_GRAVE, "`", 0.0f, 1.5f, 1.0f, 1.0f}, {KEY_1, "1", 1.0f, 1.5f, 1.0f, 1.0f}, {KEY_2, "2", 2.0f, 1.5f, 1.0f, 1.0f}, {KEY_3, "3", 3.0f, 1.5f, 1.0f, 1.0f}, {KEY_4, "4", 4.0f, 1.5f, 1.0f, 1.0f}, {KEY_5, "5", 5.0f, 1.5f, 1.0f, 1.0f}, {KEY_6, "6", 6.0f, 1.5f, 1.0f, 1.0f}, {KEY_7, "7", 7.0f, 1.5f, 1.0f, 1.0f}, {KEY_8, "8", 8.0f, 1.5f, 1.0f, 1.0f}, {KEY_9, "9", 9.0f, 1.5f, 1.0f, 1.0f}, {KEY_0, "0", 10.0f, 1.5f, 1.0f, 1.0f}, {KEY_MINUS, "-", 11.0f, 1.5f, 1.0f, 1.0f}, {KEY_EQUALS, "=", 12.0f, 1.5f, 1.0f, 1.0f}, {KEY_BACKSPACE, "Bksp", 13.0f, 1.5f, 2.0f, 1.0f},
		{KEY_INSERT, "Ins", 15.5f, 1.5f, 1.0f, 1.0f}, {KEY_HOME, "Home", 16.5f, 1.5f, 1.0f, 1.0f}, {KEY_PAGEUP, "PgUp", 17.5f, 1.5f, 1.0f, 1.0f},

		// Tab row (y = 2.5)
		{KEY_TAB, "Tab", 0.0f, 2.5f, 1.5f, 1.0f}, {KEY_Q, "Q", 1.5f, 2.5f, 1.0f, 1.0f}, {KEY_W, "W", 2.5f, 2.5f, 1.0f, 1.0f}, {KEY_E, "E", 3.5f, 2.5f, 1.0f, 1.0f}, {KEY_R, "R", 4.5f, 2.5f, 1.0f, 1.0f}, {KEY_T, "T", 5.5f, 2.5f, 1.0f, 1.0f}, {KEY_Y, "Y", 6.5f, 2.5f, 1.0f, 1.0f}, {KEY_U, "U", 7.5f, 2.5f, 1.0f, 1.0f}, {KEY_I, "I", 8.5f, 2.5f, 1.0f, 1.0f}, {KEY_O, "O", 9.5f, 2.5f, 1.0f, 1.0f}, {KEY_P, "P", 10.5f, 2.5f, 1.0f, 1.0f}, {KEY_LEFTBRACKET, "[", 11.5f, 2.5f, 1.0f, 1.0f}, {KEY_RIGHTBRACKET, "]", 12.5f, 2.5f, 1.0f, 1.0f}, {KEY_BACKSLASH, "\\", 13.5f, 2.5f, 1.5f, 1.0f},
		{KEY_DELETE, "Del", 15.5f, 2.5f, 1.0f, 1.0f}, {KEY_END, "End", 16.5f, 2.5f, 1.0f, 1.0f}, {KEY_PAGEDOWN, "PgDn", 17.5f, 2.5f, 1.0f, 1.0f},

		// Caps row (y = 3.5)
		{KEY_CAPSLOCK, "Caps", 0.0f, 3.5f, 1.75f, 1.0f}, {KEY_A, "A", 1.75f, 3.5f, 1.0f, 1.0f}, {KEY_S, "S", 2.75f, 3.5f, 1.0f, 1.0f}, {KEY_D, "D", 3.75f, 3.5f, 1.0f, 1.0f}, {KEY_F, "F", 4.75f, 3.5f, 1.0f, 1.0f}, {KEY_G, "G", 5.75f, 3.5f, 1.0f, 1.0f}, {KEY_H, "H", 6.75f, 3.5f, 1.0f, 1.0f}, {KEY_J, "J", 7.75f, 3.5f, 1.0f, 1.0f}, {KEY_K, "K", 8.75f, 3.5f, 1.0f, 1.0f}, {KEY_L, "L", 9.75f, 3.5f, 1.0f, 1.0f}, {KEY_SEMICOLON, ";", 10.75f, 3.5f, 1.0f, 1.0f}, {KEY_APOSTROPHE, "'", 11.75f, 3.5f, 1.0f, 1.0f}, {KEY_RETURN, "Enter", 12.75f, 3.5f, 2.25f, 1.0f},

		// Shift row (y = 4.5)
		{KEY_LSHIFT, "Shift", 0.0f, 4.5f, 2.25f, 1.0f}, {KEY_Z, "Z", 2.25f, 4.5f, 1.0f, 1.0f}, {KEY_X, "X", 3.25f, 4.5f, 1.0f, 1.0f}, {KEY_C, "C", 4.25f, 4.5f, 1.0f, 1.0f}, {KEY_V, "V", 5.25f, 4.5f, 1.0f, 1.0f}, {KEY_B, "B", 6.25f, 4.5f, 1.0f, 1.0f}, {KEY_N, "N", 7.25f, 4.5f, 1.0f, 1.0f}, {KEY_M, "M", 8.25f, 4.5f, 1.0f, 1.0f}, {KEY_COMMA, ",", 9.25f, 4.5f, 1.0f, 1.0f}, {KEY_PERIOD, ".", 10.25f, 4.5f, 1.0f, 1.0f}, {KEY_SLASH, "/", 11.25f, 4.5f, 1.0f, 1.0f}, {KEY_RSHIFT, "Shift", 12.25f, 4.5f, 2.75f, 1.0f},
		{KEY_UP, "^", 16.5f, 4.5f, 1.0f, 1.0f},

		// Bottom row (y = 5.5)
		{KEY_LCTRL, "Ctrl", 0.0f, 5.5f, 1.25f, 1.0f}, {KEY_LGUI, "Gui", 1.25f, 5.5f, 1.25f, 1.0f}, {KEY_LALT, "Alt", 2.5f, 5.5f, 1.25f, 1.0f}, {KEY_SPACE, "Space", 3.75f, 5.5f, 6.25f, 1.0f}, {KEY_RALT, "Alt", 10.0f, 5.5f, 1.25f, 1.0f}, {KEY_RGUI, "Gui", 11.25f, 5.5f, 1.25f, 1.0f}, {KEY_MENU, "Menu", 12.5f, 5.5f, 1.25f, 1.0f}, {KEY_RCTRL, "Ctrl", 13.75f, 5.5f, 1.25f, 1.0f},
		{KEY_LEFT, "<", 15.5f, 5.5f, 1.0f, 1.0f}, {KEY_DOWN, "v", 16.5f, 5.5f, 1.0f, 1.0f}, {KEY_RIGHT, ">", 17.5f, 5.5f, 1.0f, 1.0f},
	};
	return s_Layout;
}
} // namespace

const char *CMenusSettingsControls::BindDisplayName(const char *pCommand) const
{
	if(!pCommand[0])
	{
		return "";
	}
	for(const CBindOption &Option : m_vBindOptions)
	{
		if(Option.m_Group == EBindOptionGroup::CUSTOM || Option.m_pLabel == nullptr)
		{
			continue;
		}
		if(str_comp(Option.m_Command.c_str(), pCommand) == 0)
		{
			return Localize(Option.m_pLabel);
		}
	}
	return pCommand; // custom / unknown command: show the raw command
}

void CMenusSettingsControls::SetEditedBind(const char *pCommand)
{
	// An empty command unbinds the slot.
	GameClient()->m_Binds.Bind(m_BindEditKey, pCommand, false, m_BindEditModifier);
}

void CMenusSettingsControls::SwitchBindPreset(int Index)
{
	const int Active = g_Config.m_UcBindPresetActive;
	if(Index == Active || Index < 0 || Index >= CBinds::NUM_PRESETS)
		return;

	// Persist the current (possibly customized) binds into the slot we are leaving.
	GameClient()->m_Binds.SaveToPreset(Active);

	// Activate the target slot. If it has never been used, start it from the DDNet
	// defaults (A = left, D = right, Space = jump, ...) so it's a clean base.
	if(GameClient()->m_Binds.PresetExists(Index))
	{
		GameClient()->m_Binds.LoadFromPreset(Index);
	}
	else
	{
		GameClient()->m_Binds.SetDefaults();
		GameClient()->m_Binds.SaveToPreset(Index);
	}

	g_Config.m_UcBindPresetActive = Index;
}

void CMenusSettingsControls::RenderVisualBinds(CUIRect View)
{
	// Preset (loadout) switcher: save the current binds as a preset and switch between them.
	{
		CUIRect PresetRow, PresetLabel;
		View.HSplitTop(BUTTON_HEIGHT, &PresetRow, &View);
		View.HSplitTop(MARGIN, nullptr, &View);
		PresetRow.VSplitLeft(120.0f, &PresetLabel, &PresetRow);
		PresetRow.VSplitLeft(BUTTON_SPACING, nullptr, &PresetRow);
		Ui()->DoLabel(&PresetLabel, Localize("Bind preset"), FONT_SIZE, TEXTALIGN_ML);

		const int ActivePreset = g_Config.m_UcBindPresetActive;
		const float PresetWidth = (PresetRow.w - (CBinds::NUM_PRESETS - 1) * BUTTON_SPACING) / CBinds::NUM_PRESETS;
		for(int i = 0; i < CBinds::NUM_PRESETS; i++)
		{
			CUIRect PresetButton;
			PresetRow.VSplitLeft(PresetWidth, &PresetButton, &PresetRow);
			if(i < CBinds::NUM_PRESETS - 1)
			{
				PresetRow.VSplitLeft(BUTTON_SPACING, nullptr, &PresetRow);
			}
			char aLabel[32];
			str_format(aLabel, sizeof(aLabel), "%s %d", Localize("Preset"), i + 1);
			if(GameClient()->m_Menus.DoButton_Menu(&m_aBindPresetButtons[i], aLabel, i == ActivePreset ? 1 : 0, &PresetButton))
			{
				SwitchBindPreset(i);
			}
		}
	}

	// Top row: modifier toggles on the left (combine freely, e.g. Ctrl+Alt), reset on the right.
	CUIRect TogglesRow, ResetButton;
	View.HSplitTop(BUTTON_HEIGHT, &TogglesRow, &View);
	View.HSplitTop(MARGIN, nullptr, &View);
	TogglesRow.VSplitRight(150.0f, &TogglesRow, &ResetButton);
	TogglesRow.VSplitRight(MARGIN, &TogglesRow, nullptr);

	// A short label describing the currently selected modifier combination.
	CUIRect LabelRect;
	TogglesRow.VSplitLeft(120.0f, &LabelRect, &TogglesRow);
	TogglesRow.VSplitLeft(BUTTON_SPACING, nullptr, &TogglesRow);
	char aLayer[64] = "";
	if(m_VisualModifierMask == KeyModifier::NONE)
	{
		str_copy(aLayer, Localize("No modifier"));
	}
	else
	{
		for(int Mod = KeyModifier::CTRL; Mod < KeyModifier::COUNT; Mod++)
		{
			if(m_VisualModifierMask & (1 << Mod))
			{
				str_append(aLayer, CBinds::GetModifierName(Mod));
				str_append(aLayer, "+", sizeof(aLayer));
			}
		}
	}
	Ui()->DoLabel(&LabelRect, aLayer, FONT_SIZE, TEXTALIGN_ML);

	static const struct
	{
		const char *m_pLabel;
		int m_Modifier;
	} s_aModifiers[4] = {
		{"Ctrl", KeyModifier::CTRL},
		{"Alt", KeyModifier::ALT},
		{"Shift", KeyModifier::SHIFT},
		{"Gui", KeyModifier::GUI},
	};
	const float ToggleWidth = (TogglesRow.w - 3.0f * BUTTON_SPACING) / 4.0f;
	for(int i = 0; i < 4; i++)
	{
		CUIRect Toggle;
		TogglesRow.VSplitLeft(ToggleWidth, &Toggle, &TogglesRow);
		if(i < 3)
		{
			TogglesRow.VSplitLeft(BUTTON_SPACING, nullptr, &TogglesRow);
		}
		const int Bit = 1 << s_aModifiers[i].m_Modifier;
		if(GameClient()->m_Menus.DoButton_Menu(&m_aVisualModifierToggles[i], s_aModifiers[i].m_pLabel, (m_VisualModifierMask & Bit) ? 1 : 0, &Toggle))
		{
			m_VisualModifierMask ^= Bit;
		}
	}

	if(GameClient()->m_Menus.DoButton_Menu(&m_VisualResetButton, Localize("Reset to defaults"), 0, &ResetButton))
	{
		GameClient()->m_Menus.PopupConfirm(Localize("Reset controls"), Localize("Are you sure that you want to reset the controls to their defaults?"),
			Localize("Reset"), Localize("Cancel"), &CMenus::ResetSettingsControls);
	}

	// Help line
	CUIRect HelpRow;
	View.HSplitTop(16.0f, &HelpRow, &View);
	View.HSplitTop(MARGIN, nullptr, &View);
	SLabelProperties HelpProps;
	HelpProps.m_MaxWidth = HelpRow.w;
	TextRender()->TextColor(0.7f, 0.7f, 0.7f, 1.0f);
	Ui()->DoLabel(&HelpRow, Localize("Click a key to change its bind. Switch presets to keep separate bind loadouts."), 10.0f, TEXTALIGN_ML, HelpProps);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	RenderKeyboardLayout(View);
}

void CMenusSettingsControls::DoVisualKey(CUIRect Cell, int Key, const char *pLabel, CButtonContainer *pId, int Corners, float Rounding)
{
	if(Cell.w <= 0.0f || Cell.h <= 0.0f)
	{
		return;
	}

	const char *pCommand = GameClient()->m_Binds.Get(Key, m_VisualModifierMask);
	const bool Bound = pCommand[0] != '\0';
	const bool Hovered = Ui()->HotItem() == pId;

	// Click opens the edit popup (only when no popup is currently open).
	if(!Ui()->IsPopupOpen() && Ui()->DoButtonLogic(pId, 0, &Cell, BUTTONFLAG_LEFT, CUi::EButtonSoundType::BUTTON))
	{
		m_BindEditKey = Key;
		m_BindEditModifier = m_VisualModifierMask;
		m_BindEditCustomInput.Set(pCommand);
		// Reset autocomplete state so suggestions recompute for the newly opened bind.
		m_BindEditSuggestionSel = -1;
		m_aBindEditLastApplied[0] = '\0';
		m_aBindEditCompletionToken[0] = '\0';
		m_BindEditTokenHasArgs = false;
		Ui()->DoPopupMenu(&m_BindEditPopupId, Cell.x, Cell.y + Cell.h, 320.0f, 360.0f, this, PopupBindEdit);
	}

	// Background: accent for bound keys, subtle for empty, brighter on hover.
	ColorRGBA KeyColor;
	if(Hovered)
	{
		KeyColor = Bound ? ColorRGBA(0.45f, 0.65f, 1.0f, 0.9f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.35f);
	}
	else if(Bound)
	{
		KeyColor = ColorRGBA(0.35f, 0.5f, 0.85f, 0.75f);
	}
	else
	{
		KeyColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f);
	}
	Cell.Draw(KeyColor, Corners, Rounding);

	// Key name (top) and, if space allows, the bound command label (bottom).
	CUIRect Inner;
	Cell.Margin(2.0f, &Inner);
	if(Bound && Cell.h >= 26.0f)
	{
		CUIRect NameRect, CmdRect;
		Inner.HSplitTop(Inner.h * 0.5f, &NameRect, &CmdRect);
		Ui()->DoLabel(&NameRect, pLabel, minimum(11.0f, Cell.h * 0.42f), TEXTALIGN_MC);
		SLabelProperties CmdProps;
		CmdProps.m_MaxWidth = CmdRect.w;
		CmdProps.m_EllipsisAtEnd = true;
		TextRender()->TextColor(0.9f, 0.95f, 1.0f, 1.0f);
		Ui()->DoLabel(&CmdRect, BindDisplayName(pCommand), minimum(9.0f, Cell.h * 0.34f), TEXTALIGN_MC, CmdProps);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	else
	{
		SLabelProperties NameProps;
		NameProps.m_MaxWidth = Inner.w;
		NameProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Inner, pLabel, minimum(11.0f, Cell.h * 0.5f), TEXTALIGN_MC, NameProps);
	}
}

void CMenusSettingsControls::RenderKeyboardLayout(CUIRect View)
{
	const std::vector<SKeyCap> &Caps = KeyboardLayout();
	const float GridWUnits = 18.5f;
	const float GridHUnits = 6.5f;
	const float Spacing = 3.0f;

	// Reserve a section at the bottom for the mouse widget.
	const float SectionGap = 14.0f;
	float MouseSectionH = maximum(90.0f, minimum(150.0f, View.h * 0.30f));
	float GridAvailH = View.h - MouseSectionH - SectionGap;
	if(GridAvailH < 120.0f)
	{
		GridAvailH = View.h * 0.65f;
		MouseSectionH = View.h - GridAvailH - SectionGap;
	}

	float UnitW = minimum(View.w / GridWUnits, GridAvailH / GridHUnits);
	UnitW = maximum(UnitW, 12.0f);

	const float GridPixW = GridWUnits * UnitW;
	const float GridPixH = GridHUnits * UnitW;
	const float OriginX = View.x + maximum(0.0f, (View.w - GridPixW) / 2.0f);
	const float OriginY = View.y;

	int Index = 0;
	for(const SKeyCap &Cap : Caps)
	{
		CUIRect Cell;
		Cell.x = OriginX + Cap.m_X * UnitW;
		Cell.y = OriginY + Cap.m_Y * UnitW;
		Cell.w = Cap.m_W * UnitW - Spacing;
		Cell.h = Cap.m_H * UnitW - Spacing;
		DoVisualKey(Cell, Cap.m_Key, Cap.m_pLabel, &m_aKeyCapButtons[Index % 256], IGraphics::CORNER_ALL, 3.0f);
		Index++;
	}

	// Mouse section (label + mouse-shaped widget), centered below the keyboard.
	CUIRect MouseArea;
	MouseArea.x = View.x;
	MouseArea.y = OriginY + GridPixH + SectionGap;
	MouseArea.w = View.w;
	MouseArea.h = MouseSectionH;

	CUIRect MouseLabel;
	MouseArea.HSplitTop(14.0f, &MouseLabel, &MouseArea);
	MouseArea.HSplitTop(4.0f, nullptr, &MouseArea);
	TextRender()->TextColor(0.7f, 0.7f, 0.7f, 1.0f);
	Ui()->DoLabel(&MouseLabel, Localize("Mouse"), 11.0f, TEXTALIGN_MC);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	const float MouseW = minimum(MouseArea.h * 0.72f, 120.0f);
	CUIRect MouseWidget;
	MouseWidget.x = MouseArea.x + (MouseArea.w - MouseW) / 2.0f;
	MouseWidget.y = MouseArea.y;
	MouseWidget.w = MouseW;
	MouseWidget.h = MouseArea.h;
	RenderMouse(MouseWidget);
}

void CMenusSettingsControls::RenderMouse(CUIRect Area)
{
	// Left strip holds the two thumb buttons; the rest is the mouse body.
	// No gap between them so the thumb buttons sit flush against the body.
	const float SideW = Area.w * 0.18f;
	CUIRect Side, Body;
	Area.VSplitLeft(SideW, &Side, &Body);

	// Decorative body outline (rounded like a real mouse).
	const float BodyRounding = Body.w * 0.42f;
	Body.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f), IGraphics::CORNER_ALL, BodyRounding);

	// Upper portion (left button | scroll wheel | right button), aligned to the body
	// edges so the L/R clickable blocks' rounded top corners match the mouse outline.
	CUIRect Top;
	Body.HSplitTop(Body.h * 0.48f, &Top, nullptr);

	const float WheelW = Body.w * 0.22f;
	CUIRect LeftBtn, WheelCol, RightBtn;
	Top.VSplitLeft((Top.w - WheelW) / 2.0f, &LeftBtn, &WheelCol);
	WheelCol.VSplitLeft(WheelW, &WheelCol, &RightBtn);
	LeftBtn.VSplitRight(2.0f, &LeftBtn, nullptr);
	RightBtn.VSplitLeft(2.0f, nullptr, &RightBtn);

	// L rounds its top-left, R rounds its top-right, matching the mouse shape.
	DoVisualKey(LeftBtn, KEY_MOUSE_1, "L", &m_aMouseButtons[0], IGraphics::CORNER_TL, BodyRounding);
	DoVisualKey(RightBtn, KEY_MOUSE_2, "R", &m_aMouseButtons[1], IGraphics::CORNER_TR, BodyRounding);

	// Scroll wheel column: wheel up / middle click / wheel down.
	CUIRect WhUp, WhMid, WhDn;
	WheelCol.HSplitTop(WheelCol.h / 3.0f, &WhUp, &WheelCol);
	WheelCol.HSplitTop(WheelCol.h / 2.0f, &WhMid, &WhDn);
	DoVisualKey(WhUp, KEY_MOUSE_WHEEL_UP, "^", &m_aMouseButtons[2], IGraphics::CORNER_NONE, 0.0f);
	DoVisualKey(WhMid, KEY_MOUSE_3, "M3", &m_aMouseButtons[3], IGraphics::CORNER_NONE, 0.0f);
	DoVisualKey(WhDn, KEY_MOUSE_WHEEL_DOWN, "v", &m_aMouseButtons[4], IGraphics::CORNER_NONE, 0.0f);

	// Thumb buttons on the side, flush against the left edge of the body.
	const float H0 = Side.h;
	CUIRect ThumbCol = Side;
	CUIRect S4, S5;
	ThumbCol.HSplitTop(H0 * 0.26f, nullptr, &ThumbCol);
	ThumbCol.HSplitTop(H0 * 0.22f, &S4, &ThumbCol);
	ThumbCol.HSplitTop(H0 * 0.10f, nullptr, &ThumbCol);
	ThumbCol.HSplitTop(H0 * 0.22f, &S5, &ThumbCol);
	DoVisualKey(S4, KEY_MOUSE_4, "4", &m_aMouseButtons[5], IGraphics::CORNER_L, 4.0f);
	DoVisualKey(S5, KEY_MOUSE_5, "5", &m_aMouseButtons[6], IGraphics::CORNER_L, 4.0f);
}

static void CollectBindSuggestionCallback(int Index, const char *pStr, void *pUser)
{
	static_cast<std::vector<const char *> *>(pUser)->push_back(pStr);
}

void CMenusSettingsControls::UpdateBindEditSuggestions()
{
	const char *pStr = m_BindEditCustomInput.GetString();

	// Only recompute the completion base when the user actually edited the text
	// (typing), not when we ourselves rewrote it while cycling with Tab.
	if(str_comp(pStr, m_aBindEditLastApplied) != 0)
	{
		m_BindEditSuggestionSel = -1;
		str_copy(m_aBindEditLastApplied, pStr);

		const int Len = str_length(pStr);
		int SegStart = 0;
		for(int i = 0; i < Len; i++)
		{
			if(pStr[i] == ';')
				SegStart = i + 1;
		}
		while(pStr[SegStart] == ' ')
			SegStart++;

		// A space inside the current segment means the user is typing arguments,
		// so we no longer suggest command names.
		m_BindEditTokenHasArgs = false;
		for(int i = SegStart; i < Len; i++)
		{
			if(pStr[i] == ' ')
			{
				m_BindEditTokenHasArgs = true;
				break;
			}
		}
		m_BindEditTokenStart = SegStart;
		str_copy(m_aBindEditCompletionToken, pStr + SegStart);
	}

	m_vBindEditSuggestions.clear();
	if(m_BindEditTokenHasArgs || m_aBindEditCompletionToken[0] == '\0')
		return;

	Console()->PossibleCommands(m_aBindEditCompletionToken, CFGFLAG_CLIENT, false, CollectBindSuggestionCallback, &m_vBindEditSuggestions);

	// Prefer commands that start with the typed text, then shorter/alphabetical.
	const char *pSearch = m_aBindEditCompletionToken;
	std::sort(m_vBindEditSuggestions.begin(), m_vBindEditSuggestions.end(), [pSearch](const char *pA, const char *pB) {
		const bool StartA = str_startswith_nocase(pA, pSearch) != nullptr;
		const bool StartB = str_startswith_nocase(pB, pSearch) != nullptr;
		if(StartA != StartB)
			return StartA;
		const int LenA = str_length(pA);
		const int LenB = str_length(pB);
		if(LenA != LenB)
			return LenA < LenB;
		return str_comp_nocase(pA, pB) < 0;
	});

	// Hide the dropdown if the only match is exactly what's already typed.
	if(m_vBindEditSuggestions.size() == 1 && str_comp_nocase(m_vBindEditSuggestions[0], m_aBindEditCompletionToken) == 0)
		m_vBindEditSuggestions.clear();

	// Limit the number of suggestions shown/cycled.
	constexpr int MaxSuggestions = 8;
	if((int)m_vBindEditSuggestions.size() > MaxSuggestions)
		m_vBindEditSuggestions.resize(MaxSuggestions);

	if(m_BindEditSuggestionSel >= (int)m_vBindEditSuggestions.size())
		m_BindEditSuggestionSel = -1;
}

void CMenusSettingsControls::ApplyBindEditSuggestion(int Index, bool Accept)
{
	if(Index < 0 || Index >= (int)m_vBindEditSuggestions.size())
		return;

	// Keep whatever comes before the current command token (e.g. "+jump; ") and
	// replace only the token with the chosen command.
	char aNew[128];
	str_truncate(aNew, sizeof(aNew), m_BindEditCustomInput.GetString(), m_BindEditTokenStart);
	str_append(aNew, m_vBindEditSuggestions[Index], sizeof(aNew));
	if(Accept)
	{
		// Accepting (click) adds a trailing space and dismisses the dropdown.
		str_append(aNew, " ", sizeof(aNew));
		m_aBindEditCompletionToken[0] = '\0';
	}
	m_BindEditCustomInput.Set(aNew);
	// Remember that we made this change so UpdateBindEditSuggestions() doesn't treat
	// it as user typing and reset the completion base.
	str_copy(m_aBindEditLastApplied, aNew);
}

void CMenusSettingsControls::RenderBindEditSuggestions()
{
	const int Count = (int)m_vBindEditSuggestions.size();
	if(Count == 0)
		return;

	// Keyboard navigation: Tab cycles + applies, Up/Down move the highlight.
	if(Ui()->ConsumeHotkey(CUi::HOTKEY_DOWN))
		m_BindEditSuggestionSel = (m_BindEditSuggestionSel + 1) % Count;
	if(Ui()->ConsumeHotkey(CUi::HOTKEY_UP))
		m_BindEditSuggestionSel = (m_BindEditSuggestionSel - 1 + Count) % Count;
	if(Ui()->ConsumeHotkey(CUi::HOTKEY_TAB))
	{
		const int Next = m_BindEditSuggestionSel < 0 ? 0 : (m_BindEditSuggestionSel + 1) % Count;
		m_BindEditSuggestionSel = Next;
		ApplyBindEditSuggestion(Next, false);
	}

	const float RowHeight = 15.0f;
	CUIRect Box;
	Box.x = m_BindEditInputRect.x;
	Box.w = m_BindEditInputRect.w;
	Box.y = m_BindEditInputRect.y + m_BindEditInputRect.h + 1.0f;
	Box.h = Count * RowHeight + 2.0f;
	Box.Draw(ColorRGBA(0.02f, 0.02f, 0.05f, 0.95f), IGraphics::CORNER_B, 3.0f);

	CUIRect Rows;
	Box.Margin(1.0f, &Rows);
	m_vBindEditSuggestionButtons.resize(Count);
	for(int i = 0; i < Count; i++)
	{
		CUIRect RowRect;
		Rows.HSplitTop(RowHeight, &RowRect, &Rows);
		const bool Selected = i == m_BindEditSuggestionSel;
		const bool Hovered = Ui()->HotItem() == &m_vBindEditSuggestionButtons[i];
		if(Selected || Hovered)
			RowRect.Draw(ColorRGBA(0.35f, 0.5f, 0.85f, Hovered ? 0.85f : 0.6f), IGraphics::CORNER_NONE, 0.0f);

		if(Ui()->DoButtonLogic(&m_vBindEditSuggestionButtons[i], 0, &RowRect, BUTTONFLAG_LEFT))
			ApplyBindEditSuggestion(i, true);

		CUIRect TextRect;
		RowRect.VMargin(4.0f, &TextRect);
		Ui()->DoLabel(&TextRect, m_vBindEditSuggestions[i], 10.0f, TEXTALIGN_ML);
	}
}

CUi::EPopupMenuFunctionResult CMenusSettingsControls::PopupBindEdit(void *pContext, CUIRect View, bool Active)
{
	CMenusSettingsControls *pThis = static_cast<CMenusSettingsControls *>(pContext);
	CUi *pUi = pThis->Ui();

	// Header: which key/modifier is being edited.
	char aKeyName[128];
	pThis->GameClient()->m_Binds.GetKeyBindName(pThis->m_BindEditKey, pThis->m_BindEditModifier, aKeyName, sizeof(aKeyName));
	char aHeader[160];
	str_format(aHeader, sizeof(aHeader), "%s: %s", Localize("Bind"), aKeyName);
	CUIRect Row;
	View.HSplitTop(18.0f, &Row, &View);
	pUi->DoLabel(&Row, aHeader, 14.0f, TEXTALIGN_ML);
	View.HSplitTop(4.0f, nullptr, &View);

	// Current binding.
	const char *pCurrent = pThis->GameClient()->m_Binds.Get(pThis->m_BindEditKey, pThis->m_BindEditModifier);
	char aCurrent[300];
	str_format(aCurrent, sizeof(aCurrent), "%s: %s", Localize("Current"), pCurrent[0] != '\0' ? pCurrent : Localize("(unbound)"));
	View.HSplitTop(15.0f, &Row, &View);
	SLabelProperties CurProps;
	CurProps.m_MaxWidth = Row.w;
	CurProps.m_EllipsisAtEnd = true;
	pThis->TextRender()->TextColor(0.75f, 0.85f, 1.0f, 1.0f);
	pUi->DoLabel(&Row, aCurrent, 10.0f, TEXTALIGN_ML, CurProps);
	pThis->TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	View.HSplitTop(6.0f, nullptr, &View);

	// Custom command input + Set button.
	CUIRect InputRow, CustomInput, SetButton;
	View.HSplitTop(20.0f, &InputRow, &View);
	InputRow.VSplitRight(48.0f, &CustomInput, &SetButton);
	CustomInput.VSplitRight(4.0f, &CustomInput, nullptr);
	pThis->m_BindEditCustomInput.SetEmptyText(Localize("Custom command"));
	pUi->DoEditBox(&pThis->m_BindEditCustomInput, &CustomInput, 12.0f);
	pThis->m_BindEditInputRect = CustomInput;
	pThis->UpdateBindEditSuggestions();
	bool Close = false;
	if(pThis->GameClient()->m_Menus.DoButton_Menu(&pThis->m_BindEditSetButton, Localize("Set"), 0, &SetButton))
	{
		pThis->SetEditedBind(pThis->m_BindEditCustomInput.GetString());
		Close = true;
	}
	View.HSplitTop(6.0f, nullptr, &View);

	// Presets header.
	View.HSplitTop(14.0f, &Row, &View);
	pThis->TextRender()->TextColor(0.7f, 0.7f, 0.7f, 1.0f);
	pUi->DoLabel(&Row, Localize("Presets"), 11.0f, TEXTALIGN_ML);
	pThis->TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	View.HSplitTop(2.0f, nullptr, &View);

	// Bottom row: Clear / Close.
	CUIRect BottomRow, ClearButton, CloseButton;
	View.HSplitBottom(20.0f, &View, &BottomRow);
	View.HSplitBottom(6.0f, &View, nullptr);
	BottomRow.VSplitMid(&ClearButton, &CloseButton, 8.0f);
	if(pThis->GameClient()->m_Menus.DoButton_Menu(&pThis->m_BindEditClearButton, Localize("Clear"), 0, &ClearButton))
	{
		pThis->SetEditedBind("");
		Close = true;
	}
	if(pThis->GameClient()->m_Menus.DoButton_Menu(&pThis->m_BindEditCloseButton, Localize("Close"), 0, &CloseButton))
	{
		Close = true;
	}

	// Scrollable list of predefined actions.
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 3.0f * 20.0f;
	pThis->m_BindEditScrollRegion.Begin(&View, &ScrollOffset, &ScrollParams);
	CUIRect ListView = View;
	ListView.y += ScrollOffset.y;

	pThis->m_vBindEditPresetButtons.resize(pThis->m_vBindOptions.size());
	for(size_t i = 0; i < pThis->m_vBindOptions.size(); i++)
	{
		const CBindOption &Option = pThis->m_vBindOptions[i];
		if(Option.m_Group == EBindOptionGroup::CUSTOM || Option.m_pLabel == nullptr)
		{
			continue;
		}
		CUIRect Item;
		ListView.HSplitTop(18.0f, &Item, &ListView);
		ListView.HSplitTop(2.0f, nullptr, &ListView);
		if(!pThis->m_BindEditScrollRegion.AddRect(Item))
		{
			continue;
		}
		const bool IsCurrent = str_comp(Option.m_Command.c_str(), pCurrent) == 0;
		if(pThis->GameClient()->m_Menus.DoButton_Menu(&pThis->m_vBindEditPresetButtons[i], Localize(Option.m_pLabel), IsCurrent ? 1 : 0, &Item))
		{
			pThis->SetEditedBind(Option.m_Command.c_str());
			Close = true;
		}
	}
	pThis->m_BindEditScrollRegion.End();

	// Draw the command autocomplete dropdown last so it overlays the presets list.
	pThis->RenderBindEditSuggestions();

	return Close ? CUi::POPUP_CLOSE_CURRENT : CUi::POPUP_KEEP_OPEN;
}

float CMenusSettingsControls::MeasureSettingsMouseHeight() const
{
	return 4.0f * BUTTON_HEIGHT + 3.0f * BUTTON_SPACING;
}

void CMenusSettingsControls::RenderSettingsMouse(CUIRect View)
{
	CUIRect Button;
	View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
	DoSensitivityInput(Ui(), &m_IngameMouseSensInput, &g_Config.m_InpMousesens, &Button, Localize("Ingame mouse sens"));

	View.HSplitTop(BUTTON_SPACING, nullptr, &View);

	View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
	Ui()->DoScrollbarOption(&g_Config.m_InpMousesens, &g_Config.m_InpMousesens, &Button, Localize("Ingame mouse sens."), 1, MAX_SENSITIVITY_SLIDER,
		&CUi::ms_LogarithmicScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);

	View.HSplitTop(BUTTON_SPACING, nullptr, &View);

	View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
	DoSensitivityInput(Ui(), &m_UiMouseSensInput, &g_Config.m_UiMousesens, &Button, Localize("UI mouse sens"));

	View.HSplitTop(BUTTON_SPACING, nullptr, &View);

	View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
	Ui()->DoScrollbarOption(&g_Config.m_UiMousesens, &g_Config.m_UiMousesens, &Button, Localize("UI mouse sens."), 1, MAX_SENSITIVITY_SLIDER,
		&CUi::ms_LogarithmicScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE | CUi::SCROLLBAR_OPTION_DELAYUPDATE);
}

float CMenusSettingsControls::MeasureSettingsJoystickHeight() const
{
	int NumOptions = 1; // expandable header
	if(g_Config.m_InpControllerEnable)
	{
		NumOptions++; // message or joystick name/selection
		if(Input()->NumJoysticks() > 0)
		{
			NumOptions += 4; // mode, ui sens input, ui sens slider, tolerance
			if(!g_Config.m_InpControllerAbsolute)
				NumOptions += 2; // ingame sens input + slider
			NumOptions += Input()->GetActiveJoystick()->GetNumAxes() + 1; // axis selection + header
		}
	}
	return NumOptions * (BUTTON_HEIGHT + BUTTON_SPACING) + (NumOptions == 1 ? 0.0f : BUTTON_SPACING);
}

void CMenusSettingsControls::RenderSettingsJoystick(CUIRect View)
{
	CUIRect Button;
	View.HSplitTop(BUTTON_SPACING, nullptr, &View);
	View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
	const bool WasJoystickEnabled = g_Config.m_InpControllerEnable;
	if(GameClient()->m_Menus.DoButton_CheckBox(&g_Config.m_InpControllerEnable, Localize("Enable controller"), g_Config.m_InpControllerEnable, &Button))
	{
		g_Config.m_InpControllerEnable ^= 1;
	}
	if(!WasJoystickEnabled) // Use old value because this was used to allocate the available height
	{
		return;
	}

	const int NumJoysticks = Input()->NumJoysticks();
	if(NumJoysticks > 0)
	{
		// show joystick device selection if more than one available or just the joystick name if there is only one
		{
			CUIRect JoystickDropDown;
			View.HSplitTop(BUTTON_SPACING, nullptr, &View);
			View.HSplitTop(BUTTON_HEIGHT, &JoystickDropDown, &View);
			if(NumJoysticks > 1)
			{
				std::vector<std::string> vJoystickNames;
				std::vector<const char *> vpJoystickNames;
				vJoystickNames.resize(NumJoysticks);
				vpJoystickNames.resize(NumJoysticks);

				for(int i = 0; i < NumJoysticks; ++i)
				{
					char aJoystickName[256];
					str_format(aJoystickName, sizeof(aJoystickName), "%s %d: %s", Localize("Controller"), i, Input()->GetJoystick(i)->GetName());
					vJoystickNames[i] = aJoystickName;
					vpJoystickNames[i] = vJoystickNames[i].c_str();
				}

				const int CurrentJoystick = Input()->GetActiveJoystick()->GetIndex();
				const int NewJoystick = Ui()->DoDropDown(&JoystickDropDown, CurrentJoystick, vpJoystickNames.data(), vpJoystickNames.size(), m_JoystickDropDownState);
				if(NewJoystick != CurrentJoystick)
				{
					Input()->SetActiveJoystick(NewJoystick);
				}
			}
			else
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "%s 0: %s", Localize("Controller"), Input()->GetJoystick(0)->GetName());
				Ui()->DoLabel(&JoystickDropDown, aBuf, FONT_SIZE, TEXTALIGN_ML);
			}
		}

		const bool WasAbsolute = g_Config.m_InpControllerAbsolute;
		GameClient()->m_Menus.DoLine_RadioMenu(View, Localize("Ingame controller mode"),
			m_vJoystickIngameModeButtonContainers,
			{Localize("Relative", "Ingame controller mode"), Localize("Absolute", "Ingame controller mode")},
			{0, 1},
			g_Config.m_InpControllerAbsolute);

		if(!WasAbsolute) // Use old value because this was used to allocate the available height
		{
			View.HSplitTop(BUTTON_SPACING, nullptr, &View);
			View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
			DoSensitivityInput(Ui(), &m_IngameControllerSensInput, &g_Config.m_InpControllerSens, &Button, Localize("Ingame controller sens"));

			View.HSplitTop(BUTTON_SPACING, nullptr, &View);
			View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
			Ui()->DoScrollbarOption(&g_Config.m_InpControllerSens, &g_Config.m_InpControllerSens, &Button, Localize("Ingame controller sens."), 1, MAX_SENSITIVITY_SLIDER,
				&CUi::ms_LogarithmicScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
		}

		View.HSplitTop(BUTTON_SPACING, nullptr, &View);
		View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
		DoSensitivityInput(Ui(), &m_UiControllerSensInput, &g_Config.m_UiControllerSens, &Button, Localize("UI controller sens"));

		View.HSplitTop(BUTTON_SPACING, nullptr, &View);
		View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
		Ui()->DoScrollbarOption(&g_Config.m_UiControllerSens, &g_Config.m_UiControllerSens, &Button, Localize("UI controller sens."), 1, MAX_SENSITIVITY_SLIDER,
			&CUi::ms_LogarithmicScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);

		View.HSplitTop(BUTTON_SPACING, nullptr, &View);
		View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
		Ui()->DoScrollbarOption(&g_Config.m_InpControllerTolerance, &g_Config.m_InpControllerTolerance, &Button, Localize("Controller jitter tolerance"), 0, 50);

		View.HSplitTop(BUTTON_SPACING, nullptr, &View);
		if(m_SettingsScrollRegion.AddRect(View))
		{
			View.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f), IGraphics::CORNER_ALL, 5.0f);
			RenderJoystickAxisPicker(View);
		}
	}
	else
	{
		View.HSplitTop(View.h - BUTTON_HEIGHT, nullptr, &View);
		View.HSplitTop(BUTTON_HEIGHT, &Button, &View);
		Ui()->DoLabel(&Button, Localize("No controller found. Plug in a controller."), FONT_SIZE, TEXTALIGN_ML);
	}
}

void CMenusSettingsControls::RenderJoystickAxisPicker(CUIRect View)
{
	const float AxisWidth = 0.2f * View.w;
	const float StatusWidth = 0.4f * View.w;
	const float AimBindWidth = 90.0f;
	const float SpacingV = (View.w - AxisWidth - StatusWidth - AimBindWidth) / 2.0f;

	CUIRect Row, Axis, Status, AimBind;
	View.HSplitTop(BUTTON_SPACING, nullptr, &View);
	View.HSplitTop(BUTTON_HEIGHT, &Row, &View);
	Row.VSplitLeft(AxisWidth, &Axis, &Row);
	Row.VSplitLeft(SpacingV, nullptr, &Row);
	Row.VSplitLeft(StatusWidth, &Status, &Row);
	Row.VSplitLeft(SpacingV, nullptr, &Row);
	Row.VSplitLeft(AimBindWidth, &AimBind, &Row);

	Ui()->DoLabel(&Axis, Localize("Axis"), FONT_SIZE, TEXTALIGN_MC);
	Ui()->DoLabel(&Status, Localize("Status"), FONT_SIZE, TEXTALIGN_MC);
	Ui()->DoLabel(&AimBind, Localize("Aim bind"), FONT_SIZE, TEXTALIGN_MC);

	IInput::IJoystick *pJoystick = Input()->GetActiveJoystick();
	for(int i = 0; i < std::min<int>(pJoystick->GetNumAxes(), NUM_JOYSTICK_AXES); i++)
	{
		View.HSplitTop(BUTTON_SPACING, nullptr, &View);
		View.HSplitTop(BUTTON_HEIGHT, &Row, &View);
		if(!m_SettingsScrollRegion.AddRect(Row))
		{
			continue;
		}
		Row.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f), IGraphics::CORNER_ALL, 5.0f);
		Row.VSplitLeft(AxisWidth, &Axis, &Row);
		Row.VSplitLeft(SpacingV, nullptr, &Row);
		Row.VSplitLeft(StatusWidth, &Status, &Row);
		Row.VSplitLeft(SpacingV, nullptr, &Row);
		Row.VSplitLeft(AimBindWidth, &AimBind, &Row);

		const bool Active = g_Config.m_InpControllerX == i || g_Config.m_InpControllerY == i;

		// Axis label
		char aLabel[16];
		str_format(aLabel, sizeof(aLabel), "%d", i + 1);
		SLabelProperties LabelProps;
		if(!Active)
		{
			LabelProps.SetColor(ColorRGBA(0.7f, 0.7f, 0.7f, 1.0f));
		}
		Ui()->DoLabel(&Axis, aLabel, FONT_SIZE, TEXTALIGN_MC, LabelProps);

		// Axis status
		Status.HMargin(7.0f, &Status);
		RenderJoystickBar(&Status, (pJoystick->GetAxisValue(i) + 1.0f) / 2.0f, g_Config.m_InpControllerTolerance / 50.0f, Active);

		// Bind to X/Y
		CUIRect AimBindX, AimBindY;
		AimBind.VSplitMid(&AimBindX, &AimBindY);
		if(GameClient()->m_Menus.DoButton_CheckBox(&m_aaJoystickAxisCheckboxIds[i][0], "X", g_Config.m_InpControllerX == i, &AimBindX))
		{
			if(g_Config.m_InpControllerY == i)
				g_Config.m_InpControllerY = g_Config.m_InpControllerX;
			g_Config.m_InpControllerX = i;
		}
		if(GameClient()->m_Menus.DoButton_CheckBox(&m_aaJoystickAxisCheckboxIds[i][1], "Y", g_Config.m_InpControllerY == i, &AimBindY))
		{
			if(g_Config.m_InpControllerX == i)
				g_Config.m_InpControllerX = g_Config.m_InpControllerY;
			g_Config.m_InpControllerY = i;
		}
	}
}

void CMenusSettingsControls::RenderJoystickBar(const CUIRect *pRect, float Current, float Tolerance, bool Active)
{
	CUIRect Handle;
	pRect->VSplitLeft(pRect->h, &Handle, nullptr); // Slider size
	Handle.x += (pRect->w - Handle.w) * Current;

	pRect->Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Active ? 0.25f : 0.125f), IGraphics::CORNER_ALL, pRect->h / 2.0f);

	CUIRect ToleranceArea = *pRect;
	ToleranceArea.w *= Tolerance;
	ToleranceArea.x += (pRect->w - ToleranceArea.w) / 2.0f;
	const ColorRGBA ToleranceColor = Active ? ColorRGBA(0.8f, 0.35f, 0.35f, 1.0f) : ColorRGBA(0.7f, 0.5f, 0.5f, 1.0f);
	ToleranceArea.Draw(ToleranceColor, IGraphics::CORNER_ALL, ToleranceArea.h / 2.0f);

	const ColorRGBA SliderColor = Active ? ColorRGBA(0.95f, 0.95f, 0.95f, 1.0f) : ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f);
	Handle.Draw(SliderColor, IGraphics::CORNER_ALL, Handle.h / 2.0f);
}

void CMenus::ResetSettingsControls()
{
	GameClient()->m_Binds.SetDefaults();

	g_Config.m_InpMousesens = 200;
	g_Config.m_UiMousesens = 200;

	g_Config.m_InpControllerEnable = 0;
	g_Config.m_InpControllerGUID[0] = '\0';
	g_Config.m_InpControllerAbsolute = 0;
	g_Config.m_InpControllerSens = 100;
	g_Config.m_InpControllerX = 0;
	g_Config.m_InpControllerY = 1;
	g_Config.m_InpControllerTolerance = 5;
	g_Config.m_UiControllerSens = 100;
}
