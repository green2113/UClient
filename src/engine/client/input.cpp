/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "input.h"

#include "keynames.h"

#include <base/system.h>
#include <base/windows.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <SDL.h>

// support older SDL version (pre 2.0.6)
#ifndef SDL_JOYSTICK_AXIS_MIN
#define SDL_JOYSTICK_AXIS_MIN (-32768)
#endif
#ifndef SDL_JOYSTICK_AXIS_MAX
#define SDL_JOYSTICK_AXIS_MAX 32767
#endif

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
// windows.h must be included before imm.h, but clang-format requires includes to be sorted alphabetically, hence this comment.
#include <imm.h>
#include "clipboard_image.h"

namespace
{
struct SClipboardBitmapInfoHeader
{
	DWORD m_Size;
	LONG m_Width;
	LONG m_Height;
	WORD m_Planes;
	WORD m_BitCount;
	DWORD m_Compression;
	DWORD m_SizeImage;
	LONG m_XPelsPerMeter;
	LONG m_YPelsPerMeter;
	DWORD m_ClrUsed;
	DWORD m_ClrImportant;
};

struct SClipboardBitmapV4Header
{
	SClipboardBitmapInfoHeader m_Base;
	DWORD m_RedMask;
	DWORD m_GreenMask;
	DWORD m_BlueMask;
	DWORD m_AlphaMask;
	DWORD m_CSType;
	LONG m_Endpoints[9];
	DWORD m_GammaRed;
	DWORD m_GammaGreen;
	DWORD m_GammaBlue;
};

constexpr DWORD CLIPBOARD_DIB_COMPRESSION_RGB = 0;
constexpr DWORD CLIPBOARD_DIB_COMPRESSION_BITFIELDS = 3;

uint8_t ExtractClipboardMaskedChannel(uint32_t Pixel, uint32_t Mask)
{
	if(Mask == 0)
		return 0;

	int Shift = 0;
	while(((Mask >> Shift) & 1u) == 0)
		++Shift;

	uint32_t Range = Mask >> Shift;
	int Bits = 0;
	while((Range & 1u) != 0)
	{
		++Bits;
		Range >>= 1;
	}

	if(Bits <= 0)
		return 0;

	const uint32_t Value = (Pixel & Mask) >> Shift;
	if(Bits >= 8)
		return (uint8_t)(Value >> (Bits - 8));

	const uint32_t MaxValue = (1u << Bits) - 1u;
	return MaxValue == 0 ? 0 : (uint8_t)((Value * 255u) / MaxValue);
}

bool DecodeClipboardDibImage(const void *pData, size_t DataSize, IInput::SClipboardImage &Image)
{
	Image = {};
	if(pData == nullptr || DataSize < sizeof(SClipboardBitmapInfoHeader))
		return false;

	const auto *pHeader = static_cast<const SClipboardBitmapInfoHeader *>(pData);
	if(pHeader->m_Size < sizeof(SClipboardBitmapInfoHeader) || DataSize < pHeader->m_Size || pHeader->m_Planes != 1)
		return false;
	if(pHeader->m_BitCount != 24 && pHeader->m_BitCount != 32)
		return false;
	if(pHeader->m_Width <= 0 || pHeader->m_Height == 0)
		return false;
	if(pHeader->m_Compression != CLIPBOARD_DIB_COMPRESSION_RGB && pHeader->m_Compression != CLIPBOARD_DIB_COMPRESSION_BITFIELDS)
		return false;
	if(pHeader->m_BitCount == 24 && pHeader->m_Compression != CLIPBOARD_DIB_COMPRESSION_RGB)
		return false;
	if(pHeader->m_ClrUsed != 0 && pHeader->m_BitCount <= 8)
		return false;

	const int Width = pHeader->m_Width;
	const int Height = pHeader->m_Height < 0 ? -pHeader->m_Height : pHeader->m_Height;
	const bool TopDown = pHeader->m_Height < 0;
	const size_t HeaderSize = pHeader->m_Size;
	const size_t RowStride = ((size_t)Width * pHeader->m_BitCount + 31u) / 32u * 4u;

	uint32_t RedMask = 0x00ff0000u;
	uint32_t GreenMask = 0x0000ff00u;
	uint32_t BlueMask = 0x000000ffu;
	uint32_t AlphaMask = 0u;
	size_t PixelOffset = HeaderSize;

	if(pHeader->m_Compression == CLIPBOARD_DIB_COMPRESSION_BITFIELDS)
	{
		if(HeaderSize >= sizeof(SClipboardBitmapV4Header))
		{
			const auto *pHeaderV4 = static_cast<const SClipboardBitmapV4Header *>(pData);
			RedMask = pHeaderV4->m_RedMask;
			GreenMask = pHeaderV4->m_GreenMask;
			BlueMask = pHeaderV4->m_BlueMask;
			AlphaMask = pHeaderV4->m_AlphaMask;
		}
		else
		{
			if(DataSize < HeaderSize + sizeof(uint32_t) * 3)
				return false;
			const auto *pMasks = reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(pData) + HeaderSize);
			RedMask = pMasks[0];
			GreenMask = pMasks[1];
			BlueMask = pMasks[2];
			PixelOffset += sizeof(uint32_t) * 3;
		}
	}

	if(PixelOffset + RowStride * (size_t)Height > DataSize)
		return false;

	const uint8_t *pPixels = static_cast<const uint8_t *>(pData) + PixelOffset;
	Image.m_Width = Width;
	Image.m_Height = Height;
	Image.m_vRgba.resize((size_t)Width * Height * 4);

	for(int y = 0; y < Height; ++y)
	{
		const int SourceY = TopDown ? y : (Height - 1 - y);
		const uint8_t *pRow = pPixels + RowStride * (size_t)SourceY;
		uint8_t *pDst = Image.m_vRgba.data() + (size_t)y * Width * 4;
		for(int x = 0; x < Width; ++x)
		{
			if(pHeader->m_BitCount == 24)
			{
				const uint8_t *pSrc = pRow + x * 3;
				pDst[x * 4 + 0] = pSrc[2];
				pDst[x * 4 + 1] = pSrc[1];
				pDst[x * 4 + 2] = pSrc[0];
				pDst[x * 4 + 3] = 255;
			}
			else
			{
				const uint8_t *pSrc = pRow + x * 4;
				const uint32_t Pixel = (uint32_t)pSrc[0] | ((uint32_t)pSrc[1] << 8) | ((uint32_t)pSrc[2] << 16) | ((uint32_t)pSrc[3] << 24);
				if(pHeader->m_Compression == CLIPBOARD_DIB_COMPRESSION_RGB)
				{
					pDst[x * 4 + 0] = pSrc[2];
					pDst[x * 4 + 1] = pSrc[1];
					pDst[x * 4 + 2] = pSrc[0];
					pDst[x * 4 + 3] = 255;
				}
				else
				{
					pDst[x * 4 + 0] = ExtractClipboardMaskedChannel(Pixel, RedMask);
					pDst[x * 4 + 1] = ExtractClipboardMaskedChannel(Pixel, GreenMask);
					pDst[x * 4 + 2] = ExtractClipboardMaskedChannel(Pixel, BlueMask);
					pDst[x * 4 + 3] = AlphaMask != 0 ? ExtractClipboardMaskedChannel(Pixel, AlphaMask) : 255;
				}
			}
		}
	}

	return Image.IsValid();
}
}
#endif

// for platform specific features that aren't available or are broken in SDL
#include <SDL_syswm.h>
#ifdef KeyPress
#undef KeyPress // Undo pollution from X11/Xlib.h included by SDL_syswm.h on Linux
#endif

void CInput::AddKeyEvent(int Key, int Flags)
{
	dbg_assert(Key >= KEY_FIRST && Key < KEY_LAST, "Key invalid: %d", Key);
	dbg_assert((Flags & (FLAG_PRESS | FLAG_RELEASE)) != 0 && (Flags & ~(FLAG_PRESS | FLAG_RELEASE | FLAG_REPEAT)) == 0, "Flags invalid (unknown flag): %d", Flags);
	dbg_assert((Flags & FLAG_REPEAT) == 0 || (Flags & FLAG_PRESS) != 0, "Flags invalid (key repeat implies key press): %d", Flags);

	CEvent Event;
	Event.m_Key = Key;
	Event.m_Flags = Flags;
	Event.m_aText[0] = '\0';
	Event.m_InputCount = m_InputCounter;
	m_vInputEvents.emplace_back(Event);

	if(Flags & IInput::FLAG_PRESS)
	{
		m_aCurrentKeyStates[Key] = true;
		m_aFrameKeyStates[Key] = true;
	}
	if(Flags & IInput::FLAG_RELEASE)
	{
		m_aCurrentKeyStates[Key] = false;
	}
}

void CInput::AddTextEvent(const char *pText)
{
	CEvent Event;
	Event.m_Key = KEY_UNKNOWN;
	Event.m_Flags = FLAG_TEXT;
	str_copy(Event.m_aText, pText);
	Event.m_InputCount = m_InputCounter;
	m_vInputEvents.emplace_back(Event);
}

CInput::CInput()
{
	std::fill(std::begin(m_aCurrentKeyStates), std::end(m_aCurrentKeyStates), false);
	std::fill(std::begin(m_aFrameKeyStates), std::end(m_aFrameKeyStates), false);

	m_vInputEvents.reserve(32);
	m_LastUpdate = 0;
	m_UpdateTime = 0.0f;

	m_InputCounter = 1;
	m_InputGrabbed = false;

	m_MouseFocus = true;

	m_CompositionCursor = 0;
	m_CandidateSelectedIndex = -1;

	m_aDropFile[0] = '\0';
}

void CInput::Init()
{
	StopTextInput();

	m_pGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pConfigManager = Kernel()->RequestInterface<IConfigManager>();

	MouseModeRelative();

	InitJoysticks();
}

void CInput::Shutdown()
{
	CloseJoysticks();
}

void CInput::InitJoysticks()
{
	if(!SDL_WasInit(SDL_INIT_JOYSTICK))
	{
		if(SDL_InitSubSystem(SDL_INIT_JOYSTICK) < 0)
		{
			dbg_msg("joystick", "Unable to init SDL joystick system: %s", SDL_GetError());
			return;
		}
	}

	const int NumJoysticks = SDL_NumJoysticks();
	dbg_msg("joystick", "%d joystick(s) found", NumJoysticks);
	for(int i = 0; i < NumJoysticks; i++)
		OpenJoystick(i);
	UpdateActiveJoystick();

	Console()->Chain("inp_controller_guid", ConchainJoystickGuidChanged, this);
}

bool CInput::OpenJoystick(int JoystickIndex)
{
	SDL_Joystick *pJoystick = SDL_JoystickOpen(JoystickIndex);
	if(!pJoystick)
	{
		dbg_msg("joystick", "Could not open joystick %d: '%s'", JoystickIndex, SDL_GetError());
		return false;
	}
	if(std::find_if(m_vJoysticks.begin(), m_vJoysticks.end(), [pJoystick](const CJoystick &Joystick) -> bool { return Joystick.m_pDelegate == pJoystick; }) != m_vJoysticks.end())
	{
		// Joystick has already been added
		return false;
	}
	m_vJoysticks.emplace_back(this, m_vJoysticks.size(), pJoystick);
	const CJoystick &Joystick = m_vJoysticks[m_vJoysticks.size() - 1];
	dbg_msg("joystick", "Opened joystick %d '%s' (%d axes, %d buttons, %d balls, %d hats)", JoystickIndex, Joystick.GetName(),
		Joystick.GetNumAxes(), Joystick.GetNumButtons(), Joystick.GetNumBalls(), Joystick.GetNumHats());
	return true;
}

void CInput::UpdateActiveJoystick()
{
	m_pActiveJoystick = nullptr;
	if(m_vJoysticks.empty())
		return;
	for(auto &Joystick : m_vJoysticks)
	{
		if(str_comp(Joystick.GetGUID(), g_Config.m_InpControllerGUID) == 0)
		{
			m_pActiveJoystick = &Joystick;
			return;
		}
	}
	// Fall back to first joystick if no match was found
	if(!m_pActiveJoystick)
		m_pActiveJoystick = &m_vJoysticks.front();
}

void CInput::ConchainJoystickGuidChanged(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() == 1)
	{
		static_cast<CInput *>(pUserData)->UpdateActiveJoystick();
	}
}

float CInput::GetJoystickDeadzone()
{
	return minimum(g_Config.m_InpControllerTolerance / 50.0f, 0.995f);
}

CInput::CJoystick::CJoystick(CInput *pInput, int Index, SDL_Joystick *pDelegate)
{
	m_pInput = pInput;
	m_Index = Index;
	m_pDelegate = pDelegate;
	m_NumAxes = SDL_JoystickNumAxes(pDelegate);
	m_NumButtons = SDL_JoystickNumButtons(pDelegate);
	m_NumBalls = SDL_JoystickNumBalls(pDelegate);
	m_NumHats = SDL_JoystickNumHats(pDelegate);
	str_copy(m_aName, SDL_JoystickName(pDelegate));
	SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(pDelegate), m_aGUID, sizeof(m_aGUID));
	m_InstanceId = SDL_JoystickInstanceID(pDelegate);
}

void CInput::CloseJoysticks()
{
	if(SDL_WasInit(SDL_INIT_JOYSTICK))
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK);

	m_vJoysticks.clear();
	m_pActiveJoystick = nullptr;
}

void CInput::SetActiveJoystick(size_t Index)
{
	m_pActiveJoystick = &m_vJoysticks[Index];
	str_copy(g_Config.m_InpControllerGUID, m_pActiveJoystick->GetGUID());
}

float CInput::CJoystick::GetAxisValue(int Axis)
{
	return (SDL_JoystickGetAxis(m_pDelegate, Axis) - SDL_JOYSTICK_AXIS_MIN) / (float)(SDL_JOYSTICK_AXIS_MAX - SDL_JOYSTICK_AXIS_MIN) * 2.0f - 1.0f;
}

void CInput::CJoystick::GetJoystickHatKeys(int Hat, int HatValue, int (&aHatKeys)[2])
{
	if(HatValue & SDL_HAT_UP)
		aHatKeys[0] = KEY_JOY_HAT0_UP + Hat * NUM_JOYSTICK_BUTTONS_PER_HAT;
	else if(HatValue & SDL_HAT_DOWN)
		aHatKeys[0] = KEY_JOY_HAT0_DOWN + Hat * NUM_JOYSTICK_BUTTONS_PER_HAT;
	else
		aHatKeys[0] = KEY_UNKNOWN;

	if(HatValue & SDL_HAT_LEFT)
		aHatKeys[1] = KEY_JOY_HAT0_LEFT + Hat * NUM_JOYSTICK_BUTTONS_PER_HAT;
	else if(HatValue & SDL_HAT_RIGHT)
		aHatKeys[1] = KEY_JOY_HAT0_RIGHT + Hat * NUM_JOYSTICK_BUTTONS_PER_HAT;
	else
		aHatKeys[1] = KEY_UNKNOWN;
}

void CInput::CJoystick::GetHatValue(int Hat, int (&aHatKeys)[2])
{
	GetJoystickHatKeys(Hat, SDL_JoystickGetHat(m_pDelegate, Hat), aHatKeys);
}

bool CInput::CJoystick::Relative(float *pX, float *pY)
{
	if(!Input()->m_MouseFocus || !Input()->m_InputGrabbed || !g_Config.m_InpControllerEnable)
		return false;

	const vec2 RawJoystickPos = vec2(GetAxisValue(g_Config.m_InpControllerX), GetAxisValue(g_Config.m_InpControllerY));
	const float Len = length(RawJoystickPos);
	const float DeadZone = Input()->GetJoystickDeadzone();
	if(Len > DeadZone)
	{
		const float Factor = 2500.0f * Input()->GetUpdateTime() * maximum((Len - DeadZone) / (1.0f - DeadZone), 0.001f) / Len;
		*pX = RawJoystickPos.x * Factor;
		*pY = RawJoystickPos.y * Factor;
		return true;
	}
	return false;
}

bool CInput::CJoystick::Absolute(float *pX, float *pY)
{
	if(!Input()->m_MouseFocus || !Input()->m_InputGrabbed || !g_Config.m_InpControllerEnable)
		return false;

	const vec2 RawJoystickPos = vec2(GetAxisValue(g_Config.m_InpControllerX), GetAxisValue(g_Config.m_InpControllerY));
	const float DeadZone = Input()->GetJoystickDeadzone();
	if(dot(RawJoystickPos, RawJoystickPos) > DeadZone * DeadZone)
	{
		*pX = RawJoystickPos.x;
		*pY = RawJoystickPos.y;
		return true;
	}
	return false;
}

bool CInput::MouseRelative(float *pX, float *pY)
{
	if(!m_MouseFocus || !m_InputGrabbed)
		return false;

	ivec2 Relative;
	SDL_GetRelativeMouseState(&Relative.x, &Relative.y);

	*pX = Relative.x;
	*pY = Relative.y;
	return *pX != 0.0f || *pY != 0.0f;
}

void CInput::MouseModeAbsolute()
{
	m_InputGrabbed = false;
	SDL_SetRelativeMouseMode(SDL_FALSE);
	Graphics()->SetWindowGrab(false);
}

void CInput::MouseModeRelative()
{
	m_InputGrabbed = true;
	SDL_SetRelativeMouseMode(SDL_TRUE);
	Graphics()->SetWindowGrab(true);
	// Clear pending relative mouse motion
	SDL_GetRelativeMouseState(nullptr, nullptr);
}

vec2 CInput::NativeMousePos() const
{
	ivec2 Position;
	SDL_GetMouseState(&Position.x, &Position.y);
	return vec2(Position.x, Position.y);
}

bool CInput::NativeMousePressed(int Index) const
{
	int i = SDL_GetMouseState(nullptr, nullptr);
	return (i & SDL_BUTTON(Index)) != 0;
}

const std::vector<IInput::CTouchFingerState> &CInput::TouchFingerStates() const
{
	return m_vTouchFingerStates;
}

void CInput::ClearTouchDeltas()
{
	for(CTouchFingerState &TouchFingerState : m_vTouchFingerStates)
	{
		TouchFingerState.m_Delta = vec2(0.0f, 0.0f);
	}
}

std::string CInput::GetClipboardText()
{
	char *pClipboardText = SDL_GetClipboardText();
	std::string ClipboardText = pClipboardText;
	SDL_free(pClipboardText);
	return ClipboardText;
}

bool CInput::GetClipboardImage(SClipboardImage &Image)
{
	Image = {};

#if defined(CONF_FAMILY_WINDOWS)
	if(!OpenClipboard(nullptr))
		return false;

	HANDLE hClipboard = GetClipboardData(CF_DIBV5);
	if(hClipboard == nullptr)
		hClipboard = GetClipboardData(CF_DIB);
	if(hClipboard == nullptr)
	{
		CloseClipboard();
		return false;
	}

	void *pClipboardData = GlobalLock(hClipboard);
	const SIZE_T ClipboardSize = GlobalSize(hClipboard);
	const bool Success = pClipboardData != nullptr && ClipboardSize > 0 && DecodeClipboardDibImage(pClipboardData, (size_t)ClipboardSize, Image);
	if(pClipboardData != nullptr)
		GlobalUnlock(hClipboard);
	CloseClipboard();
	return Success;
#else
	return false;
#endif
}

void CInput::SetClipboardText(const char *pText)
{
	SDL_SetClipboardText(pText);
}

void CInput::StartTextInput()
{
	// enable system messages for IME
	SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
	// Avoid restarting an already-active text input — on Android that rapidly
	// opens/closes the soft keyboard (especially with Vulkan).
	if(SDL_IsTextInputActive())
		return;
	SDL_StartTextInput();
}

void CInput::StopTextInput()
{
	if(SDL_IsTextInputActive())
		SDL_StopTextInput();
	// disable system messages for performance
	SDL_EventState(SDL_SYSWMEVENT, SDL_DISABLE);
	m_CompositionString = "";
	m_CompositionCursor = 0;
	m_vCandidates.clear();
	m_TextInputRectValid = false;
}

void CInput::EnsureScreenKeyboardShown()
{
	if(!SDL_HasScreenKeyboardSupport() ||
		Graphics()->IsScreenKeyboardShown())
	{
		return;
	}
	// SDL_IsScreenKeyboardShown can lag behind while the keyboard is animating
	// (common on Android/Vulkan). Throttle restarts so we don't open/close forever.
	const int64_t Now = time_get();
	if(m_LastEnsureKeyboardTime != 0 && Now - m_LastEnsureKeyboardTime < time_freq() / 2)
		return;
	m_LastEnsureKeyboardTime = Now;
	if(SDL_IsTextInputActive())
		SDL_StopTextInput();
	SDL_StartTextInput();
}

void CInput::ConsumeEvents(std::function<void(const CEvent &Event)> Consumer) const
{
	for(const CEvent &Event : m_vInputEvents)
	{
		// Only propagate valid input events
		if(Event.m_InputCount == m_InputCounter)
		{
			Consumer(Event);
		}
	}
}

void CInput::Clear()
{
	std::fill(std::begin(m_aFrameKeyStates), std::end(m_aFrameKeyStates), false);
	m_vInputEvents.clear();
	ClearTouchDeltas();
}

float CInput::GetUpdateTime() const
{
	return m_UpdateTime;
}

bool CInput::KeyIsPressed(int Key) const
{
	dbg_assert(Key >= KEY_FIRST && Key < KEY_LAST, "Key invalid: %d", Key);
	return m_aCurrentKeyStates[Key];
}

bool CInput::KeyPress(int Key) const
{
	dbg_assert(Key >= KEY_FIRST && Key < KEY_LAST, "Key invalid: %d", Key);
	return m_aFrameKeyStates[Key];
}

const char *CInput::KeyName(int Key) const
{
	dbg_assert(Key >= KEY_FIRST && Key < KEY_LAST, "Key invalid: %d", Key);
	return g_aaKeyStrings[Key];
}

int CInput::FindKeyByName(const char *pKeyName) const
{
	// check for numeric
	if(pKeyName[0] == '&')
	{
		int Key = str_toint(pKeyName + 1);
		if(Key > KEY_FIRST && Key < KEY_LAST)
			return Key; // numeric
	}

	// search for key
	for(int Key = KEY_FIRST; Key < KEY_LAST; Key++)
	{
		if(str_comp_nocase(pKeyName, KeyName(Key)) == 0)
			return Key;
	}

	return KEY_UNKNOWN;
}

void CInput::HandleJoystickAxisMotionEvent(const SDL_JoyAxisEvent &Event)
{
	if(!g_Config.m_InpControllerEnable)
		return;
	CJoystick *pJoystick = GetActiveJoystick();
	if(!pJoystick || pJoystick->GetInstanceId() != Event.which)
		return;
	if(Event.axis >= NUM_JOYSTICK_AXES)
		return;

	const int LeftKey = KEY_JOY_AXIS_0_LEFT + 2 * Event.axis;
	const int RightKey = LeftKey + 1;
	const float DeadZone = GetJoystickDeadzone();

	if(Event.value <= SDL_JOYSTICK_AXIS_MIN * DeadZone && !m_aCurrentKeyStates[LeftKey])
	{
		AddKeyEvent(LeftKey, IInput::FLAG_PRESS);
	}
	else if(Event.value > SDL_JOYSTICK_AXIS_MIN * DeadZone && m_aCurrentKeyStates[LeftKey])
	{
		AddKeyEvent(LeftKey, IInput::FLAG_RELEASE);
	}

	if(Event.value >= SDL_JOYSTICK_AXIS_MAX * DeadZone && !m_aCurrentKeyStates[RightKey])
	{
		AddKeyEvent(RightKey, IInput::FLAG_PRESS);
	}
	else if(Event.value < SDL_JOYSTICK_AXIS_MAX * DeadZone && m_aCurrentKeyStates[RightKey])
	{
		AddKeyEvent(RightKey, IInput::FLAG_RELEASE);
	}
}

void CInput::HandleJoystickButtonEvent(const SDL_JoyButtonEvent &Event)
{
	if(!g_Config.m_InpControllerEnable)
		return;
	CJoystick *pJoystick = GetActiveJoystick();
	if(!pJoystick || pJoystick->GetInstanceId() != Event.which)
		return;
	if(Event.button >= NUM_JOYSTICK_BUTTONS)
		return;

	const int Key = Event.button + KEY_JOYSTICK_BUTTON_0;

	if(Event.type == SDL_JOYBUTTONDOWN)
	{
		AddKeyEvent(Key, IInput::FLAG_PRESS);
	}
	else if(Event.type == SDL_JOYBUTTONUP)
	{
		AddKeyEvent(Key, IInput::FLAG_RELEASE);
	}
}

void CInput::HandleJoystickHatMotionEvent(const SDL_JoyHatEvent &Event)
{
	if(!g_Config.m_InpControllerEnable)
		return;
	CJoystick *pJoystick = GetActiveJoystick();
	if(!pJoystick || pJoystick->GetInstanceId() != Event.which)
		return;
	if(Event.hat >= NUM_JOYSTICK_HATS)
		return;

	int aHatKeys[2];
	CJoystick::GetJoystickHatKeys(Event.hat, Event.value, aHatKeys);

	for(int Key = KEY_JOY_HAT0_UP + Event.hat * NUM_JOYSTICK_BUTTONS_PER_HAT; Key <= KEY_JOY_HAT0_RIGHT + Event.hat * NUM_JOYSTICK_BUTTONS_PER_HAT; Key++)
	{
		if(Key != aHatKeys[0] && Key != aHatKeys[1] && m_aCurrentKeyStates[Key])
		{
			AddKeyEvent(Key, IInput::FLAG_RELEASE);
		}
	}

	for(int CurrentKey : aHatKeys)
	{
		if(CurrentKey != KEY_UNKNOWN && !m_aCurrentKeyStates[CurrentKey])
		{
			AddKeyEvent(CurrentKey, IInput::FLAG_PRESS);
		}
	}
}

void CInput::HandleJoystickAddedEvent(const SDL_JoyDeviceEvent &Event)
{
	if(OpenJoystick(Event.which))
	{
		UpdateActiveJoystick();
	}
}

void CInput::HandleJoystickRemovedEvent(const SDL_JoyDeviceEvent &Event)
{
	auto RemovedJoystick = std::find_if(m_vJoysticks.begin(), m_vJoysticks.end(), [Event](const CJoystick &Joystick) -> bool { return Joystick.GetInstanceId() == Event.which; });
	if(RemovedJoystick != m_vJoysticks.end())
	{
		dbg_msg("joystick", "Closed joystick %d '%s'", (*RemovedJoystick).GetIndex(), (*RemovedJoystick).GetName());
		auto NextJoystick = m_vJoysticks.erase(RemovedJoystick);
		// Adjust indices of following joysticks
		while(NextJoystick != m_vJoysticks.end())
		{
			(*NextJoystick).m_Index--;
			++NextJoystick;
		}
		UpdateActiveJoystick();
	}
}

void CInput::HandleTouchDownEvent(const SDL_TouchFingerEvent &Event)
{
	CTouchFingerState TouchFingerState;
	TouchFingerState.m_Finger.m_DeviceId = Event.touchId;
	TouchFingerState.m_Finger.m_FingerId = Event.fingerId;
	TouchFingerState.m_Position = vec2(Event.x, Event.y);
	TouchFingerState.m_Delta = vec2(Event.dx, Event.dy);
	TouchFingerState.m_PressTime = time_get_nanoseconds();
	m_vTouchFingerStates.emplace_back(TouchFingerState);
}

void CInput::HandleTouchUpEvent(const SDL_TouchFingerEvent &Event)
{
	auto FoundState = std::find_if(m_vTouchFingerStates.begin(), m_vTouchFingerStates.end(), [Event](const CTouchFingerState &State) {
		return State.m_Finger.m_DeviceId == Event.touchId && State.m_Finger.m_FingerId == Event.fingerId;
	});
	if(FoundState != m_vTouchFingerStates.end())
	{
		m_vTouchFingerStates.erase(FoundState);
	}
}

void CInput::HandleTouchMotionEvent(const SDL_TouchFingerEvent &Event)
{
	auto FoundState = std::find_if(m_vTouchFingerStates.begin(), m_vTouchFingerStates.end(), [Event](const CTouchFingerState &State) {
		return State.m_Finger.m_DeviceId == Event.touchId && State.m_Finger.m_FingerId == Event.fingerId;
	});
	if(FoundState != m_vTouchFingerStates.end())
	{
		FoundState->m_Position = vec2(Event.x, Event.y);
		FoundState->m_Delta += vec2(Event.dx, Event.dy);
	}
}

void CInput::HandleTextEditingEvent(const char *pText, int Start, int Length)
{
	if(pText[0] != '\0')
	{
		m_CompositionString = pText;
		m_CompositionCursor = 0;
		for(int i = 0; i < Start; i++)
		{
			m_CompositionCursor = str_utf8_forward(m_CompositionString.c_str(), m_CompositionCursor);
		}
		// Length is currently unused on Windows and will always be 0, so we don't support selecting composition text
		AddTextEvent("");
	}
	else
	{
		m_CompositionString = "";
		m_CompositionCursor = 0;
	}
}

void CInput::SetCompositionWindowPosition(float X, float Y, float H)
{
	SDL_Rect Rect;
	Rect.x = X / m_pGraphics->ScreenHiDPIScale();
	Rect.y = Y / m_pGraphics->ScreenHiDPIScale();
	Rect.h = H / m_pGraphics->ScreenHiDPIScale();
	Rect.w = 0;
	// Redundant SetTextInputRect calls restart the Android soft keyboard.
	if(m_TextInputRectValid &&
		m_TextInputRect.x == Rect.x &&
		m_TextInputRect.y == Rect.y &&
		m_TextInputRect.w == Rect.w &&
		m_TextInputRect.h == Rect.h)
	{
		return;
	}
	m_TextInputRect = Rect;
	m_TextInputRectValid = true;
	SDL_SetTextInputRect(&Rect);
}

static int TranslateKeyEventKey(const SDL_KeyboardEvent &KeyEvent)
{
	// See SDL_Keymod for possible modifiers:
	// NONE   =     0
	// LSHIFT =     1
	// RSHIFT =     2
	// LCTRL  =    64
	// RCTRL  =   128
	// LALT   =   256
	// RALT   =   512
	// LGUI   =  1024
	// RGUI   =  2048
	// NUM    =  4096
	// CAPS   =  8192
	// MODE   = 16384
	// Sum if you want to ignore multiple modifiers.
	if(KeyEvent.keysym.mod & g_Config.m_InpIgnoredModifiers)
	{
		return KEY_UNKNOWN;
	}

	int Key = g_Config.m_InpTranslatedKeys ? SDL_GetScancodeFromKey(KeyEvent.keysym.sym) : KeyEvent.keysym.scancode;

#if defined(CONF_PLATFORM_ANDROID)
	// Translate the Android back-button to the escape-key so it can be used to open/close the menu, close popups etc.
	if(Key == KEY_AC_BACK)
	{
		Key = KEY_ESCAPE;
	}
#endif

	return Key;
}

static int TranslateMouseButtonEventKey(const SDL_MouseButtonEvent &MouseButtonEvent)
{
	switch(MouseButtonEvent.button)
	{
	case SDL_BUTTON_LEFT:
		return KEY_MOUSE_1;
	case SDL_BUTTON_RIGHT:
		return KEY_MOUSE_2;
	case SDL_BUTTON_MIDDLE:
		return KEY_MOUSE_3;
	case SDL_BUTTON_X1:
		return KEY_MOUSE_4;
	case SDL_BUTTON_X2:
		return KEY_MOUSE_5;
	case 6:
		return KEY_MOUSE_6;
	case 7:
		return KEY_MOUSE_7;
	case 8:
		return KEY_MOUSE_8;
	case 9:
		return KEY_MOUSE_9;
	default:
		return KEY_UNKNOWN;
	}
}

static int TranslateMouseWheelEventKey(const SDL_MouseWheelEvent &MouseWheelEvent)
{
	if(MouseWheelEvent.y > 0)
	{
		return KEY_MOUSE_WHEEL_UP;
	}
	else if(MouseWheelEvent.y < 0)
	{
		return KEY_MOUSE_WHEEL_DOWN;
	}
	else if(MouseWheelEvent.x > 0)
	{
		return KEY_MOUSE_WHEEL_RIGHT;
	}
	else if(MouseWheelEvent.x < 0)
	{
		return KEY_MOUSE_WHEEL_LEFT;
	}
	else
	{
		return KEY_UNKNOWN;
	}
}

#if defined(CONF_PLATFORM_EMSCRIPTEN)
extern "C" {

// This will be called from Emscripten JS code
bool EmscriptenQuit = false;
void EmscriptenCallbackQuit()
{
	EmscriptenQuit = true;
}
}
#endif

int CInput::Update()
{
	const int64_t Now = time_get();
	if(m_LastUpdate != 0)
	{
		const float Diff = (Now - m_LastUpdate) / (float)time_freq();
		m_UpdateTime = m_UpdateTime == 0.0f ? Diff : (m_UpdateTime * 0.8f + Diff * 0.2f);
	}
	m_LastUpdate = Now;

	// keep the counter between 1..0xFFFFFFFF, 0 means not pressed
	m_InputCounter = (m_InputCounter % std::numeric_limits<decltype(m_InputCounter)>::max()) + 1;

	SDL_Event Event;
	bool IgnoreKeys = false;

	const auto &&AddKeyEventChecked = [&](int Key, int Flags) {
		const bool PasteShortcut = Key == KEY_V && (SDL_GetModState() & (KMOD_CTRL | KMOD_GUI)) != 0;
		if(Key != KEY_UNKNOWN && !IgnoreKeys && (!(Flags & IInput::FLAG_PRESS) || !HasComposition() || PasteShortcut))
		{
			AddKeyEvent(Key, Flags);
		}
	};

#if defined(CONF_PLATFORM_EMSCRIPTEN)
	if(EmscriptenQuit)
	{
		return 1;
	}
#endif

	while(SDL_PollEvent(&Event))
	{
		switch(Event.type)
		{
		case SDL_SYSWMEVENT:
			ProcessSystemMessage(Event.syswm.msg);
			break;

		case SDL_TEXTEDITING:
			HandleTextEditingEvent(Event.edit.text, Event.edit.start, Event.edit.length);
			break;

#if SDL_VERSION_ATLEAST(2, 0, 22)
		case SDL_TEXTEDITING_EXT:
			HandleTextEditingEvent(Event.editExt.text, Event.editExt.start, Event.editExt.length);
			SDL_free(Event.editExt.text);
			break;
#endif

		case SDL_TEXTINPUT:
			m_CompositionString = "";
			m_CompositionCursor = 0;
#if defined(CONF_FAMILY_WINDOWS)
			// With SDL text input active, Ctrl+V often arrives as TEXTINPUT only (no KEY_V).
			if((SDL_GetModState() & KMOD_CTRL) != 0 && !KeyPress(KEY_V))
			{
				AddKeyEvent(KEY_V, IInput::FLAG_PRESS);
				break;
			}
#endif
			AddTextEvent(Event.text.text);
			break;

		// handle keys
		case SDL_KEYDOWN:
			AddKeyEventChecked(TranslateKeyEventKey(Event.key), IInput::FLAG_PRESS | (Event.key.repeat != 0 ? FLAG_REPEAT : 0));
			break;

		case SDL_KEYUP:
			AddKeyEventChecked(TranslateKeyEventKey(Event.key), IInput::FLAG_RELEASE);
			break;

		// handle the joystick events
		case SDL_JOYAXISMOTION:
			HandleJoystickAxisMotionEvent(Event.jaxis);
			break;

		case SDL_JOYBUTTONUP:
		case SDL_JOYBUTTONDOWN:
			HandleJoystickButtonEvent(Event.jbutton);
			break;

		case SDL_JOYHATMOTION:
			HandleJoystickHatMotionEvent(Event.jhat);
			break;

		case SDL_JOYDEVICEADDED:
			HandleJoystickAddedEvent(Event.jdevice);
			break;

		case SDL_JOYDEVICEREMOVED:
			HandleJoystickRemovedEvent(Event.jdevice);
			break;

		// handle mouse buttons
		case SDL_MOUSEBUTTONDOWN:
			AddKeyEventChecked(TranslateMouseButtonEventKey(Event.button), IInput::FLAG_PRESS);
			break;

		case SDL_MOUSEBUTTONUP:
			AddKeyEventChecked(TranslateMouseButtonEventKey(Event.button), IInput::FLAG_RELEASE);
			break;

		case SDL_MOUSEWHEEL:
			AddKeyEventChecked(TranslateMouseWheelEventKey(Event.wheel), IInput::FLAG_PRESS | IInput::FLAG_RELEASE);
			break;

		case SDL_FINGERDOWN:
			HandleTouchDownEvent(Event.tfinger);
			break;

		case SDL_FINGERUP:
			HandleTouchUpEvent(Event.tfinger);
			break;

		case SDL_FINGERMOTION:
			HandleTouchMotionEvent(Event.tfinger);
			break;

		case SDL_WINDOWEVENT:
			// Ignore keys following a focus gain as they may be part of global
			// shortcuts
			switch(Event.window.event)
			{
#if SDL_VERSION_ATLEAST(2, 0, 18)
			case SDL_WINDOWEVENT_DISPLAY_CHANGED:
				Graphics()->SwitchWindowScreen(Event.display.data1, false);
				break;
#endif
			case SDL_WINDOWEVENT_MOVED:
				Graphics()->Move(Event.window.data1, Event.window.data2);
				break;
			// listen to size changes, this includes our manual changes and the ones by the window manager
			case SDL_WINDOWEVENT_SIZE_CHANGED:
				Graphics()->GotResized(Event.window.data1, Event.window.data2, -1);
				break;
			case SDL_WINDOWEVENT_FOCUS_GAINED:
				if(m_InputGrabbed)
				{
					MouseModeRelative();
					// Clear pending relative mouse motion
					SDL_GetRelativeMouseState(nullptr, nullptr);
				}
				m_MouseFocus = true;
				IgnoreKeys = true;
				break;
			case SDL_WINDOWEVENT_FOCUS_LOST:
				std::fill(std::begin(m_aCurrentKeyStates), std::end(m_aCurrentKeyStates), false);
				m_MouseFocus = false;
				IgnoreKeys = true;
				if(m_InputGrabbed)
				{
					MouseModeAbsolute();
					// Remember that we had relative mouse
					m_InputGrabbed = true;
				}
				break;
			case SDL_WINDOWEVENT_MINIMIZED:
#if defined(CONF_PLATFORM_ANDROID) // Save the config when minimized on Android.
				m_pConfigManager->Save();
#endif
				Graphics()->WindowDestroyNtf(Event.window.windowID);
				break;

			case SDL_WINDOWEVENT_MAXIMIZED:
#if defined(CONF_PLATFORM_MACOS) // Todo: remove this when fixed in SDL
				MouseModeAbsolute();
				MouseModeRelative();
#endif
				[[fallthrough]];
			case SDL_WINDOWEVENT_RESTORED:
				Graphics()->WindowCreateNtf(Event.window.windowID);
				break;
			}
			break;

		// other messages
		case SDL_QUIT:
			return 1;

		case SDL_DROPFILE:
			str_copy(m_aDropFile, Event.drop.file);
			SDL_free(Event.drop.file);
			break;
		}
	}

	return 0;
}

void CInput::ProcessSystemMessage(SDL_SysWMmsg *pMsg)
{
#if defined(CONF_FAMILY_WINDOWS)
	// Todo SDL: remove this after SDL2 supports IME candidates
	if(pMsg->subsystem == SDL_SYSWM_WINDOWS && pMsg->msg.win.msg == WM_IME_NOTIFY)
	{
		switch(pMsg->msg.win.wParam)
		{
		case IMN_OPENCANDIDATE:
		case IMN_CHANGECANDIDATE:
		{
			HWND WindowHandle = pMsg->msg.win.hwnd;
			HIMC ImeContext = ImmGetContext(WindowHandle);
			DWORD Size = ImmGetCandidateListW(ImeContext, 0, nullptr, 0);
			LPCANDIDATELIST pCandidateList = nullptr;
			if(Size > 0)
			{
				pCandidateList = (LPCANDIDATELIST)malloc(Size);
				Size = ImmGetCandidateListW(ImeContext, 0, pCandidateList, Size);
			}
			m_vCandidates.clear();
			if(pCandidateList && Size > 0)
			{
				m_vCandidates.reserve(std::min(pCandidateList->dwCount - pCandidateList->dwPageStart, pCandidateList->dwPageSize));
				for(DWORD i = pCandidateList->dwPageStart; i < pCandidateList->dwCount && (int)m_vCandidates.size() < (int)pCandidateList->dwPageSize; i++)
				{
					LPCWSTR pCandidate = (LPCWSTR)((DWORD_PTR)pCandidateList + pCandidateList->dwOffset[i]);
					m_vCandidates.push_back(windows_wide_to_utf8(pCandidate).value_or("<invalid candidate>"));
				}
				m_CandidateSelectedIndex = pCandidateList->dwSelection - pCandidateList->dwPageStart;
			}
			else
			{
				m_CandidateSelectedIndex = -1;
			}
			free(pCandidateList);
			ImmReleaseContext(WindowHandle, ImeContext);
			break;
		}
		case IMN_CLOSECANDIDATE:
			m_vCandidates.clear();
			m_CandidateSelectedIndex = -1;
			break;
		}
	}
#endif
}

#if defined(CONF_PLATFORM_EMSCRIPTEN)
extern "C" {

// This will be called from Emscripten JS code
char aEmscriptenDropFile[IO_MAX_PATH_LENGTH] = "";
void EmscriptenCallbackDropFile(const char *pFile)
{
	str_copy(aEmscriptenDropFile, pFile);
}
}
#endif

bool CInput::GetDropFile(char *aBuf, int Len)
{
#if defined(CONF_PLATFORM_EMSCRIPTEN)
	if(aEmscriptenDropFile[0] != '\0')
	{
		str_copy(aBuf, aEmscriptenDropFile, Len);
		aEmscriptenDropFile[0] = '\0';
		return true;
	}
#endif
	if(m_aDropFile[0] != '\0')
	{
		str_copy(aBuf, m_aDropFile, Len);
		m_aDropFile[0] = '\0';
		return true;
	}
	return false;
}

IEngineInput *CreateEngineInput()
{
	return new CInput;
}
