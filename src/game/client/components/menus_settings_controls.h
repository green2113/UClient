/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MENUS_SETTINGS_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_MENUS_SETTINGS_CONTROLS_H

#include <game/client/component.h>
#include <game/client/components/binds.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

#include <vector>

enum class EBindOptionGroup
{
	MOVEMENT,
	WEAPON,
	VOTING,
	CHAT,
	DUMMY,
	BEST_CLIENT,
	BEST_CLIENT_VOICE,
	BEST_CLIENT_PRACTICE,
	MISCELLANEOUS,
	CUSTOM,
	NUM,
};

class CBindSlotUiElement
{
public:
	CBindSlot m_Bind;
	CButtonContainer m_KeyReaderButton;
	CButtonContainer m_KeyResetButton;
	bool m_ToBeDeleted = false;

	bool operator<(const CBindSlotUiElement &Other) const;
};

class CBindOption
{
public:
	EBindOptionGroup m_Group;
	const char *m_pLabel;
	std::string m_Command;
	std::vector<CBindSlotUiElement> m_vCurrentBinds;
	CButtonContainer m_AddBindButtonContainer;
	char m_TooltipButtonId;
	bool m_AddNewBind = false;
	bool m_AddNewBindActivate = false;
	bool m_ToBeDeleted = false;

	std::vector<CBindSlotUiElement>::iterator GetBindSlotElement(const CBindSlot &BindSlot);
	bool MatchesSearch(const char *pSearch) const;
};

class CMenusSettingsControls : public CComponentInterfaces
{
public:
	void OnInterfacesInit(CGameClient *pClient) override;
	void Render(CUIRect MainView);

private:
	bool m_aBindGroupExpanded[(int)EBindOptionGroup::NUM];
	CButtonContainer m_aBindGroupExpandButtons[(int)EBindOptionGroup::NUM];
	std::vector<CBindOption> m_vBindOptions;
	size_t m_NumPredefinedBindOptions;
	void UpdateBindOptions();

	CScrollRegion m_SettingsScrollRegion;
	CButtonContainer m_ResetToDefaultButton;
	CLineInputBuffered<128> m_FilterInput;
	int m_CurrentSearchMatch = 0;
	std::vector<int> m_vSearchMatches;
	bool m_SearchMatchReveal = false;
	void UpdateSearchMatches();

	void RenderSettingsBlock(float Height, CUIRect *pParentRect, const char *pTitle,
		bool *pExpanded, CButtonContainer *pExpandButton, const std::function<void(CUIRect Rect)> &RenderContentFunction);

	void RenderSettingsBindsBlock(EBindOptionGroup Group, CUIRect *pParentRect, const char *pTitle);
	float MeasureSettingsBindsHeight(EBindOptionGroup Group) const;
	void RenderSettingsBinds(EBindOptionGroup Group, CUIRect View);

	// UClient: visual (on-screen keyboard) bind editor
	bool m_VisualBindMode = false;
	int m_VisualModifierMask = 0; // KeyModifier combination bitmask, built from the modifier toggles
	CButtonContainer m_ViewListButton;
	CButtonContainer m_ViewKeyboardButton;
	CButtonContainer m_VisualResetButton;
	CButtonContainer m_aVisualModifierToggles[4]; // Ctrl / Alt / Shift / Gui
	CButtonContainer m_aBindPresetButtons[3]; // preset (loadout) switcher

	void SwitchBindPreset(int Index);
	CButtonContainer m_aKeyCapButtons[256];
	CButtonContainer m_aMouseButtons[8];

	SPopupMenuId m_BindEditPopupId;
	int m_BindEditKey = 0;
	int m_BindEditModifier = 0;
	CScrollRegion m_BindEditScrollRegion;
	std::vector<CButtonContainer> m_vBindEditPresetButtons;
	CLineInputBuffered<128> m_BindEditCustomInput;
	CButtonContainer m_BindEditClearButton;
	CButtonContainer m_BindEditSetButton;
	CButtonContainer m_BindEditCloseButton;

	// UClient: console-style command autocomplete for the custom command input
	CUIRect m_BindEditInputRect;
	std::vector<const char *> m_vBindEditSuggestions;
	std::vector<CButtonContainer> m_vBindEditSuggestionButtons;
	int m_BindEditSuggestionSel = -1;
	int m_BindEditTokenStart = 0;
	bool m_BindEditTokenHasArgs = false;
	char m_aBindEditCompletionToken[128] = "";
	char m_aBindEditLastApplied[128] = "";
	void UpdateBindEditSuggestions();
	void ApplyBindEditSuggestion(int Index, bool Accept);
	void RenderBindEditSuggestions();

	void RenderVisualBinds(CUIRect View);
	void RenderKeyboardLayout(CUIRect View);
	void RenderMouse(CUIRect Area);
	void DoVisualKey(CUIRect Cell, int Key, const char *pLabel, CButtonContainer *pId, int Corners, float Rounding);
	const char *BindDisplayName(const char *pCommand) const;
	void SetEditedBind(const char *pCommand);
	static CUi::EPopupMenuFunctionResult PopupBindEdit(void *pContext, CUIRect View, bool Active);

	float MeasureSettingsMouseHeight() const;
	CLineInputNumber m_IngameMouseSensInput;
	CLineInputNumber m_UiMouseSensInput;
	void RenderSettingsMouse(CUIRect View);

	std::vector<CButtonContainer> m_vJoystickIngameModeButtonContainers = {{}, {}};
	char m_aaJoystickAxisCheckboxIds[NUM_JOYSTICK_AXES][2]; // 2 for X and Y buttons
	CScrollRegion m_JoystickDropDownScrollRegion;
	CUi::SDropDownState m_JoystickDropDownState;
	float MeasureSettingsJoystickHeight() const;
	CLineInputNumber m_IngameControllerSensInput;
	CLineInputNumber m_UiControllerSensInput;
	void RenderSettingsJoystick(CUIRect View);
	void RenderJoystickAxisPicker(CUIRect View);
	void RenderJoystickBar(const CUIRect *pRect, float Current, float Tolerance, bool Active);
};

#endif
