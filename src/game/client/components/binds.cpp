/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "binds.h"

#include <base/log.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/config.h>
#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/chat.h>
#include <game/client/components/console.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <string>
#include <vector>

static constexpr LOG_COLOR BIND_PRINT_COLOR{255, 255, 204};
static constexpr const char *NEAREST_PLAYER_PLACEHOLDER = "%nearestPlayer%";

static void ApplyGoresModeSuffix(CGameClient *pGameClient, char *pBind, int Size)
{
	if(g_Config.m_BcGoresMode &&
		!pGameClient->m_Controls.m_WeaponsGot &&
		str_find(pBind, "+fire") &&
		!str_find(pBind, "+prevweapon"))
	{
		str_append(pBind, ";+prevweapon", Size);
	}
}

static bool IsInsideQuotedArgument(const std::string &Command, size_t Position)
{
	bool InQuotes = false;
	for(size_t i = 0; i < Position; ++i)
	{
		if(Command[i] == '\\' && i + 1 < Position && Command[i + 1] == '"')
		{
			++i;
			continue;
		}
		if(Command[i] == '"')
			InQuotes = !InQuotes;
	}
	return InQuotes;
}

static std::string EscapeQuotedConsoleValue(const char *pValue)
{
	std::string Escaped;
	for(const char *p = pValue; *p != '\0'; ++p)
	{
		if(*p == '\\' || *p == '"')
			Escaped.push_back('\\');
		Escaped.push_back(*p);
	}
	return Escaped;
}

static std::string ExpandNearestPlayerPlaceholder(const std::string &Command, const char *pPlayerName)
{
	std::string Expanded;
	size_t Cursor = 0;
	while(true)
	{
		const size_t Placeholder = Command.find(NEAREST_PLAYER_PLACEHOLDER, Cursor);
		if(Placeholder == std::string::npos)
		{
			Expanded.append(Command, Cursor, std::string::npos);
			break;
		}

		Expanded.append(Command, Cursor, Placeholder - Cursor);
		if(IsInsideQuotedArgument(Command, Placeholder))
			Expanded += EscapeQuotedConsoleValue(pPlayerName);
		else
			Expanded += pPlayerName;
		Cursor = Placeholder + str_length(NEAREST_PLAYER_PLACEHOLDER);
	}
	return Expanded;
}

static std::vector<std::string> SplitBindCommands(const char *pBind)
{
	std::vector<std::string> vCommands;
	const char *pCommand = pBind;
	if(const char *pWithoutPrefix = str_startswith(pCommand, "mc;"))
		pCommand = pWithoutPrefix;

	const char *pStart = pCommand;
	bool InQuotes = false;
	for(const char *p = pCommand;; ++p)
	{
		if(*p == '\\' && p[1] == '"')
		{
			++p;
			continue;
		}
		if(*p == '"')
			InQuotes = !InQuotes;
		else if(*p == '\0' || (!InQuotes && (*p == ';' || *p == '#')))
		{
			vCommands.emplace_back(pStart, p - pStart);
			if(*p == '\0' || *p == '#')
				break;
			pStart = p + 1;
		}
	}
	return vCommands;
}

bool CBinds::FindNearestPlayerName(char *pName, size_t NameSize)
{
	if(NameSize == 0)
		return false;
	pName[0] = '\0';

	if(Client()->State() != IClient::STATE_ONLINE)
		return false;

	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS ||
		!GameClient()->m_aClients[LocalClientId].m_Active ||
		!GameClient()->m_Snap.m_aCharacters[LocalClientId].m_Active)
	{
		return false;
	}

	const vec2 LocalPos = GameClient()->m_aClients[LocalClientId].m_RenderPos;
	int NearestClientId = -1;
	float NearestDistance = -1.0f;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		if(ClientId == LocalClientId)
			continue;

		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
		if(!ClientData.m_Active || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active || ClientData.m_aName[0] == '\0')
			continue;
		if(GameClient()->IsOtherTeam(ClientId))
			continue;

		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
		if(pInfo && pInfo->m_Team == TEAM_SPECTATORS)
			continue;

		const float Distance = distance(LocalPos, ClientData.m_RenderPos);
		if(NearestClientId < 0 || Distance < NearestDistance)
		{
			NearestClientId = ClientId;
			NearestDistance = Distance;
		}
	}

	if(NearestClientId < 0)
		return false;
	str_copy(pName, GameClient()->m_aClients[NearestClientId].m_aName, NameSize);
	return true;
}

std::string CBinds::ExecuteBind(int Stroke, const char *pBind, bool ReportPlaceholderError, const char *pNearestPlayerOverride)
{
	char aBind[IConsole::CMDLINE_LENGTH];
	str_copy(aBind, pBind, sizeof(aBind));
	ApplyGoresModeSuffix(GameClient(), aBind, sizeof(aBind));

	const bool HasPlaceholder = str_find(aBind, NEAREST_PLAYER_PLACEHOLDER) != nullptr;
	char aNearestPlayer[MAX_NAME_LENGTH] = "";
	bool HasNearestPlayer = !HasPlaceholder;
	if(HasPlaceholder)
	{
		if(pNearestPlayerOverride)
		{
			str_copy(aNearestPlayer, pNearestPlayerOverride, sizeof(aNearestPlayer));
			HasNearestPlayer = aNearestPlayer[0] != '\0';
		}
		else
		{
			HasNearestPlayer = FindNearestPlayerName(aNearestPlayer, sizeof(aNearestPlayer));
		}
	}
	bool SkippedPlaceholderCommand = false;

	for(const std::string &Command : SplitBindCommands(aBind))
	{
		if(Command.empty())
			continue;

		const bool CommandHasPlaceholder = Command.find(NEAREST_PLAYER_PLACEHOLDER) != std::string::npos;
		if(CommandHasPlaceholder && !HasNearestPlayer)
		{
			SkippedPlaceholderCommand = true;
			continue;
		}

		const std::string Expanded = CommandHasPlaceholder ? ExpandNearestPlayerPlaceholder(Command, aNearestPlayer) : Command;
		if(Expanded.size() >= IConsole::CMDLINE_LENGTH)
		{
			if(ReportPlaceholderError)
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "binds", "Expanded bind command is too long.");
			continue;
		}
		Console()->ExecuteLineStroked(Stroke, Expanded.c_str(), IConsole::CLIENT_ID_UNSPECIFIED, false);
	}

	if(SkippedPlaceholderCommand && ReportPlaceholderError)
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "binds", "No same-team player is available for %nearestPlayer%.");
	return aNearestPlayer;
}

bool CBinds::CBindsSpecial::OnInput(const IInput::CEvent &Event)
{
	if((Event.m_Flags & (IInput::FLAG_PRESS | IInput::FLAG_RELEASE)) == 0)
		return false;

	// only handle F and composed F binds
	// do not handle F5 bind while menu is active
	if(((Event.m_Key >= KEY_F1 && Event.m_Key <= KEY_F12) || (Event.m_Key >= KEY_F13 && Event.m_Key <= KEY_F24)) &&
		(Event.m_Key != KEY_F5 || !GameClient()->m_Menus.IsActive()))
	{
		return m_pBinds->OnInput(Event);
	}

	return false;
}

CBinds::CBinds()
{
	mem_zero(m_aapKeyBindings, sizeof(m_aapKeyBindings));
	m_SpecialBinds.m_pBinds = this;
}

CBinds::~CBinds()
{
	UnbindAll();
}

void CBinds::Bind(int KeyId, const char *pStr, bool FreeOnly, int ModifierCombination)
{
	dbg_assert(KeyId >= KEY_FIRST && KeyId < KEY_LAST, "KeyId invalid");
	dbg_assert(ModifierCombination >= KeyModifier::NONE && ModifierCombination < KeyModifier::COMBINATION_COUNT, "ModifierCombination invalid");

	if(FreeOnly && Get(KeyId, ModifierCombination)[0])
		return;

	free(m_aapKeyBindings[ModifierCombination][KeyId]);
	m_aapKeyBindings[ModifierCombination][KeyId] = nullptr;

	char aBindName[128];
	GetKeyBindName(KeyId, ModifierCombination, aBindName, sizeof(aBindName));
	if(!pStr[0])
	{
		log_info_color(BIND_PRINT_COLOR, "binds", "unbound %s", aBindName);
	}
	else
	{
		int Size = str_length(pStr) + 1;
		m_aapKeyBindings[ModifierCombination][KeyId] = (char *)malloc(Size);
		str_copy(m_aapKeyBindings[ModifierCombination][KeyId], pStr, Size);
		log_info_color(BIND_PRINT_COLOR, "binds", "bound %s = %s", aBindName, m_aapKeyBindings[ModifierCombination][KeyId]);
	}
}

int CBinds::GetModifierMask(IInput *pInput)
{
	int Mask = 0;
	static const auto s_aModifierKeys = {
		KEY_LSHIFT,
		KEY_RSHIFT,
		KEY_LCTRL,
		KEY_RCTRL,
		KEY_LALT,
		KEY_RALT,
		KEY_LGUI,
		KEY_RGUI,
	};
	for(const auto Key : s_aModifierKeys)
	{
		if(pInput->KeyIsPressed(Key))
		{
			Mask |= GetModifierMaskOfKey(Key);
		}
	}

	return Mask;
}

int CBinds::GetModifierMaskOfKey(int Key)
{
	switch(Key)
	{
	case KEY_LSHIFT:
	case KEY_RSHIFT:
		return 1 << KeyModifier::SHIFT;
	case KEY_LCTRL:
	case KEY_RCTRL:
		return 1 << KeyModifier::CTRL;
	case KEY_LALT:
	case KEY_RALT:
		return 1 << KeyModifier::ALT;
	case KEY_LGUI:
	case KEY_RGUI:
		return 1 << KeyModifier::GUI;
	default:
		return KeyModifier::NONE;
	}
}

bool CBinds::OnInput(const IInput::CEvent &Event)
{
	if((Event.m_Flags & (IInput::FLAG_PRESS | IInput::FLAG_RELEASE)) == 0)
		return false;

	const int KeyModifierMask = GetModifierMaskOfKey(Event.m_Key);
	const int ModifierMask = GetModifierMask(Input()) & ~KeyModifierMask;

	bool Handled = false;

	if(Event.m_Flags & IInput::FLAG_PRESS)
	{
		auto ActiveBind = std::find_if(m_vActiveBinds.begin(), m_vActiveBinds.end(), [&](const CActiveBind &Bind) {
			return Event.m_Key == Bind.m_Slot.m_Key;
		});
		if(ActiveBind == m_vActiveBinds.end())
		{
			const auto &&OnKeyPress = [&](int Mask) {
				const char *pBind = m_aapKeyBindings[Mask][Event.m_Key];
				if(g_Config.m_ClSubTickAiming)
				{
					if(str_comp("+fire", pBind) == 0 || str_comp("+hook", pBind) == 0)
					{
						m_MouseOnAction = true;
					}
				}
				std::string NearestPlayerName = ExecuteBind(1, pBind, true);
				m_vActiveBinds.emplace_back(Event.m_Key, Mask, std::move(NearestPlayerName));
			};

			if(m_aapKeyBindings[ModifierMask][Event.m_Key])
			{
				OnKeyPress(ModifierMask);
				Handled = true;
			}
			else if(m_aapKeyBindings[KeyModifier::NONE][Event.m_Key] &&
				ModifierMask != ((1 << KeyModifier::CTRL) | (1 << KeyModifier::SHIFT)) &&
				ModifierMask != ((1 << KeyModifier::GUI) | (1 << KeyModifier::SHIFT)))
			{
				OnKeyPress(KeyModifier::NONE);
				Handled = true;
			}
		}
		else
		{
			// Repeat active bind while key is held down
			// Have to check for nullptr again because the previous execute can unbind itself
			if(m_aapKeyBindings[ActiveBind->m_Slot.m_ModifierMask][ActiveBind->m_Slot.m_Key])
			{
				ExecuteBind(1, m_aapKeyBindings[ActiveBind->m_Slot.m_ModifierMask][ActiveBind->m_Slot.m_Key], false, ActiveBind->m_NearestPlayerName.c_str());
			}
			Handled = true;
		}
	}

	if(Event.m_Flags & IInput::FLAG_RELEASE)
	{
		const auto &&OnKeyRelease = [&](const CActiveBind &Bind) {
			// Prevent binds from being deactivated while chat, console and menus are open, as these components will
			// still allow key release events to be forwarded to this component, so the active binds can be cleared.
			if(GameClient()->m_Chat.IsActive() ||
				GameClient()->m_GameConsole.IsActive() ||
				GameClient()->m_Menus.IsActive())
			{
				return;
			}
			// Have to check for nullptr again because the previous execute can unbind itself
			if(!m_aapKeyBindings[Bind.m_Slot.m_ModifierMask][Bind.m_Slot.m_Key])
			{
				return;
			}
			ExecuteBind(0, m_aapKeyBindings[Bind.m_Slot.m_ModifierMask][Bind.m_Slot.m_Key], false, Bind.m_NearestPlayerName.c_str());
		};

		// Release active bind that uses this primary key
		auto ActiveBind = std::find_if(m_vActiveBinds.begin(), m_vActiveBinds.end(), [&](const CActiveBind &Bind) {
			return Event.m_Key == Bind.m_Slot.m_Key;
		});
		if(ActiveBind != m_vActiveBinds.end())
		{
			OnKeyRelease(*ActiveBind);
			m_vActiveBinds.erase(ActiveBind);
			Handled = true;
		}

		// Release all active binds that use this modifier key
		if(KeyModifierMask != KeyModifier::NONE)
		{
			while(true)
			{
				auto ActiveModifierBind = std::find_if(m_vActiveBinds.begin(), m_vActiveBinds.end(), [&](const CActiveBind &Bind) {
					return (Bind.m_Slot.m_ModifierMask & KeyModifierMask) != 0;
				});
				if(ActiveModifierBind == m_vActiveBinds.end())
					break;
				OnKeyRelease(*ActiveModifierBind);
				m_vActiveBinds.erase(ActiveModifierBind);
				Handled = true;
			}
		}
	}

	return Handled;
}

void CBinds::UnbindAll()
{
	for(auto &apKeyBinding : m_aapKeyBindings)
	{
		for(auto &pKeyBinding : apKeyBinding)
		{
			free(pKeyBinding);
			pKeyBinding = nullptr;
		}
	}
}

const char *CBinds::Get(int KeyId, int ModifierCombination) const
{
	dbg_assert(KeyId >= KEY_FIRST && KeyId < KEY_LAST, "KeyId invalid");
	dbg_assert(ModifierCombination >= KeyModifier::NONE && ModifierCombination < KeyModifier::COMBINATION_COUNT, "ModifierCombination invalid");
	return m_aapKeyBindings[ModifierCombination][KeyId] ? m_aapKeyBindings[ModifierCombination][KeyId] : "";
}

const char *CBinds::Get(const CBindSlot &BindSlot) const
{
	return Get(BindSlot.m_Key, BindSlot.m_ModifierMask);
}

void CBinds::GetKey(const char *pBindStr, char *pBuf, size_t BufSize) const
{
	pBuf[0] = '\0';
	for(int Modifier = KeyModifier::NONE; Modifier < KeyModifier::COMBINATION_COUNT; Modifier++)
	{
		for(int KeyId = KEY_FIRST; KeyId < KEY_LAST; KeyId++)
		{
			const char *pBind = Get(KeyId, Modifier);
			if(!pBind[0])
				continue;

			if(str_comp(pBind, pBindStr) == 0)
			{
				GetKeyBindName(KeyId, Modifier, pBuf, BufSize);
				return;
			}
		}
	}
}

void CBinds::SetDefaults()
{
	UnbindAll();

	Bind(KEY_F1, "toggle_local_console");
	Bind(KEY_F2, "toggle_remote_console");
	Bind(KEY_TAB, "+scoreboard");
	Bind(KEY_EQUALS, "+statboard");
	Bind(KEY_F10, "screenshot");

	Bind(KEY_A, "+left");
	Bind(KEY_D, "+right");

	Bind(KEY_SPACE, "+jump");
	Bind(KEY_MOUSE_1, "+fire");
	Bind(KEY_MOUSE_2, "+hook");
	Bind(KEY_LSHIFT, "+emote");
	Bind(KEY_Q, "+bindwheel");
	Bind(KEY_RETURN, "+show_chat; chat all");
	Bind(KEY_RIGHT, "spectate_next");
	Bind(KEY_LEFT, "spectate_previous");
	Bind(KEY_RSHIFT, "+spectate");

	Bind(KEY_1, "+weapon1");
	Bind(KEY_2, "+weapon2");
	Bind(KEY_3, "+weapon3");
	Bind(KEY_4, "+weapon4");
	Bind(KEY_5, "+weapon5");

	Bind(KEY_MOUSE_WHEEL_UP, "+prevweapon");
	Bind(KEY_MOUSE_WHEEL_DOWN, "+nextweapon");

	Bind(KEY_T, "+show_chat; chat all");
	Bind(KEY_Y, "+show_chat; chat team");
	Bind(KEY_U, "+show_chat");
	Bind(KEY_I, "+show_chat; chat all /c ");

	Bind(KEY_F3, "vote yes");
	Bind(KEY_F4, "vote no");

	Bind(KEY_K, "kill");
	Bind(KEY_J, "toggle_admin_panel");
	Bind(KEY_Q, "say /spec");
	Bind(KEY_P, "say /pause");

	g_Config.m_ClDDRaceBindsSet = 0;
	SetDDRaceBinds(false);
}

void CBinds::OnConsoleInit()
{
	ConfigManager()->RegisterCallback(ConfigSaveCallback, this);

	Console()->Register("bind", "s[key] ?r[command]", CFGFLAG_CLIENT, ConBind, this, "Bind key to execute a command or view keybindings");
	Console()->Register("binds", "?s[key]", CFGFLAG_CLIENT, ConBinds, this, "Print command executed by this keybinding or all binds");
	Console()->Register("unbind", "s[key]", CFGFLAG_CLIENT, ConUnbind, this, "Unbind key");
	Console()->Register("unbindall", "", CFGFLAG_CLIENT, ConUnbindAll, this, "Unbind all keys");

	SetDefaults();
}

void CBinds::ConBind(IConsole::IResult *pResult, void *pUserData)
{
	CBinds *pBinds = (CBinds *)pUserData;
	const char *pBindStr = pResult->GetString(0);
	const CBindSlot BindSlot = pBinds->GetBindSlot(pBindStr);

	if(!BindSlot.m_Key)
	{
		log_info_color(BIND_PRINT_COLOR, "binds", "key %s not found", pBindStr);
		return;
	}

	if(pResult->NumArguments() == 1)
	{
		ConBinds(pResult, pUserData);
		return;
	}

	pBinds->Bind(BindSlot.m_Key, pResult->GetString(1), false, BindSlot.m_ModifierMask);
}

void CBinds::ConBinds(IConsole::IResult *pResult, void *pUserData)
{
	CBinds *pBinds = (CBinds *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pKeyName = pResult->GetString(0);
		const CBindSlot BindSlot = pBinds->GetBindSlot(pKeyName);
		if(!BindSlot.m_Key)
		{
			log_info_color(BIND_PRINT_COLOR, "binds", "key '%s' not found", pKeyName);
		}
		else
		{
			if(!pBinds->m_aapKeyBindings[BindSlot.m_ModifierMask][BindSlot.m_Key])
				log_info_color(BIND_PRINT_COLOR, "binds", "%s is not bound", pKeyName);
			else
			{
				char *pBuf = pBinds->GetKeyBindCommand(BindSlot.m_ModifierMask, BindSlot.m_Key);
				log_info_color(BIND_PRINT_COLOR, "binds", "%s", pBuf);
				free(pBuf);
			}
		}
	}
	else
	{
		for(int Modifier = KeyModifier::NONE; Modifier < KeyModifier::COMBINATION_COUNT; Modifier++)
		{
			for(int Key = KEY_FIRST; Key < KEY_LAST; Key++)
			{
				if(!pBinds->m_aapKeyBindings[Modifier][Key])
					continue;
				char *pBuf = pBinds->GetKeyBindCommand(Modifier, Key);
				log_info_color(BIND_PRINT_COLOR, "binds", "%s", pBuf);
				free(pBuf);
			}
		}
	}
}

void CBinds::ConUnbind(IConsole::IResult *pResult, void *pUserData)
{
	CBinds *pBinds = (CBinds *)pUserData;
	const char *pKeyName = pResult->GetString(0);
	const CBindSlot BindSlot = pBinds->GetBindSlot(pKeyName);

	if(!BindSlot.m_Key)
	{
		log_info_color(BIND_PRINT_COLOR, "binds", "key %s not found", pKeyName);
		return;
	}

	pBinds->Bind(BindSlot.m_Key, "", false, BindSlot.m_ModifierMask);
}

void CBinds::ConUnbindAll(IConsole::IResult *pResult, void *pUserData)
{
	CBinds *pBinds = (CBinds *)pUserData;
	pBinds->UnbindAll();
}

CBindSlot CBinds::GetBindSlot(const char *pBindString) const
{
	int ModifierMask = KeyModifier::NONE;
	char aMod[32];
	aMod[0] = '\0';
	const char *pKey = str_next_token(pBindString, "+", aMod, sizeof(aMod));
	while(aMod[0] && *(pKey))
	{
		if(!str_comp_nocase(aMod, "shift"))
			ModifierMask |= (1 << KeyModifier::SHIFT);
		else if(!str_comp_nocase(aMod, "ctrl"))
			ModifierMask |= (1 << KeyModifier::CTRL);
		else if(!str_comp_nocase(aMod, "alt"))
			ModifierMask |= (1 << KeyModifier::ALT);
		else if(!str_comp_nocase(aMod, "gui"))
			ModifierMask |= (1 << KeyModifier::GUI);
		else
			return EMPTY_BIND_SLOT;

		if(str_find(pKey + 1, "+"))
			pKey = str_next_token(pKey + 1, "+", aMod, sizeof(aMod));
		else
			break;
	}
	return {Input()->FindKeyByName(ModifierMask == KeyModifier::NONE ? aMod : pKey + 1), ModifierMask};
}

const char *CBinds::GetModifierName(int Modifier)
{
	switch(Modifier)
	{
	case KeyModifier::SHIFT:
		return "shift";
	case KeyModifier::CTRL:
		return "ctrl";
	case KeyModifier::ALT:
		return "alt";
	case KeyModifier::GUI:
		return "gui";
	default:
		dbg_assert_failed("Modifier invalid: %d", Modifier);
	}
}

void CBinds::GetKeyBindName(int Key, int ModifierMask, char *pBuf, size_t BufSize) const
{
	pBuf[0] = '\0';
	for(int Modifier = KeyModifier::CTRL; Modifier < KeyModifier::COUNT; Modifier++)
	{
		if(ModifierMask & (1 << Modifier))
		{
			str_append(pBuf, GetModifierName(Modifier), BufSize);
			str_append(pBuf, "+", BufSize);
		}
	}
	str_append(pBuf, Input()->KeyName(Key), BufSize);
}

char *CBinds::GetKeyBindCommand(int ModifierCombination, int Key) const
{
	char aBindName[128];
	GetKeyBindName(Key, ModifierCombination, aBindName, sizeof(aBindName));
	// worst case the str_escape can double the string length
	int Size = str_length(m_aapKeyBindings[ModifierCombination][Key]) * 2 + str_length(aBindName) + 16;
	auto *pBuf = static_cast<char *>(malloc(Size));
	str_format(pBuf, Size, "bind %s \"", aBindName);
	char *pDst = pBuf + str_length(pBuf);
	// process the string. we need to escape some characters
	str_escape(&pDst, m_aapKeyBindings[ModifierCombination][Key], pBuf + Size);
	str_append(pBuf, "\"", Size);
	return pBuf;
}

void CBinds::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	CBinds *pSelf = (CBinds *)pUserData;

	pConfigManager->WriteLine("unbindall");
	for(int Modifier = KeyModifier::NONE; Modifier < KeyModifier::COMBINATION_COUNT; Modifier++)
	{
		for(int Key = KEY_FIRST; Key < KEY_LAST; Key++)
		{
			if(!pSelf->m_aapKeyBindings[Modifier][Key])
				continue;
			char *pBuf = pSelf->GetKeyBindCommand(Modifier, Key);
			pConfigManager->WriteLine(pBuf);
			free(pBuf);
		}
	}
}

// UClient: bind presets (loadouts)

static void PresetFilename(int Index, char *pBuf, size_t Size)
{
	str_format(pBuf, Size, "bindpreset%d.cfg", Index);
}

bool CBinds::PresetExists(int Index) const
{
	if(Index < 0 || Index >= NUM_PRESETS)
		return false;
	char aFilename[IO_MAX_PATH_LENGTH];
	PresetFilename(Index, aFilename, sizeof(aFilename));
	IOHANDLE File = Storage()->OpenFile(aFilename, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return false;
	io_close(File);
	return true;
}

void CBinds::SaveToPreset(int Index)
{
	if(Index < 0 || Index >= NUM_PRESETS)
		return;

	char aFilename[IO_MAX_PATH_LENGTH];
	PresetFilename(Index, aFilename, sizeof(aFilename));
	IOHANDLE File = Storage()->OpenFile(aFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		log_error("binds", "failed to open '%s' for writing", aFilename);
		return;
	}

	static const char s_aClear[] = "unbindall\n";
	io_write(File, s_aClear, sizeof(s_aClear) - 1);
	for(int Modifier = KeyModifier::NONE; Modifier < KeyModifier::COMBINATION_COUNT; Modifier++)
	{
		for(int Key = KEY_FIRST; Key < KEY_LAST; Key++)
		{
			if(!m_aapKeyBindings[Modifier][Key])
				continue;
			char *pBuf = GetKeyBindCommand(Modifier, Key);
			io_write(File, pBuf, str_length(pBuf));
			io_write_newline(File);
			free(pBuf);
		}
	}
	io_close(File);
}

bool CBinds::LoadFromPreset(int Index)
{
	if(!PresetExists(Index))
		return false;
	char aFilename[IO_MAX_PATH_LENGTH];
	PresetFilename(Index, aFilename, sizeof(aFilename));
	// The preset file starts with "unbindall" and then a "bind ..." line per key.
	return Console()->ExecuteFile(aFilename, IConsole::CLIENT_ID_NO_GAME, false, IStorage::TYPE_SAVE);
}

// DDRace

void CBinds::SetDDRaceBinds(bool FreeOnly)
{
	if(g_Config.m_ClDDRaceBindsSet < 1)
	{
		Bind(KEY_KP_PLUS, "zoom+", FreeOnly);
		Bind(KEY_KP_MINUS, "zoom-", FreeOnly);
		Bind(KEY_KP_MULTIPLY, "zoom", FreeOnly);
		Bind(KEY_PAUSE, "say /pause", FreeOnly);
		Bind(KEY_UP, "+jump", FreeOnly);
		Bind(KEY_LEFT, "+left", FreeOnly);
		Bind(KEY_RIGHT, "+right", FreeOnly);
		Bind(KEY_LEFTBRACKET, "+prevweapon", FreeOnly);
		Bind(KEY_RIGHTBRACKET, "+nextweapon", FreeOnly);
		Bind(KEY_C, "say /rank", FreeOnly);
		Bind(KEY_V, "say /info", FreeOnly);
		Bind(KEY_B, "say /top5", FreeOnly);
		Bind(KEY_S, "+showhookcoll", FreeOnly);
		Bind(KEY_X, "toggle cl_dummy 0 1", FreeOnly);
		Bind(KEY_H, "toggle cl_dummy_hammer 0 1", FreeOnly);
		Bind(KEY_SLASH, "+show_chat; chat all /", FreeOnly);
		Bind(KEY_KP_0, "say /emote normal 999999", FreeOnly);
		Bind(KEY_KP_1, "say /emote happy 999999", FreeOnly);
		Bind(KEY_KP_2, "say /emote angry 999999", FreeOnly);
		Bind(KEY_KP_3, "say /emote pain 999999", FreeOnly);
		Bind(KEY_KP_4, "say /emote surprise 999999", FreeOnly);
		Bind(KEY_KP_5, "say /emote blink 999999", FreeOnly);
		Bind(KEY_MINUS, "spectate_previous", FreeOnly);
		Bind(KEY_EQUALS, "spectate_next", FreeOnly);
	}

	if(g_Config.m_ClDDRaceBindsSet < 2)
	{
		const bool DontModifySpectate = FreeOnly && str_comp(Get(KEY_MOUSE_3, KeyModifier::NONE), "+spectate") != 0;
		Bind(KEY_MOUSE_3, "toggle_scoreboard_cursor; +spectate", DontModifySpectate);
		Bind(KEY_LALT, "toggle_scoreboard_cursor", FreeOnly);
	}

	g_Config.m_ClDDRaceBindsSet = 2;
}
