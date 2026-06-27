import pathlib

root = pathlib.Path(r"E:/uclient-bc/BestClient/src/game/client/components/uclient")
chat_cpp = pathlib.Path(r"E:/uclient-bc/BestClient/src/game/client/components/chat.cpp")

preamble = '''#include "chat_paste_image.h"

#include <base/system.h>

#include <engine/gfx/image_loader.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/textrender.h>

#include <game/client/components/chat.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

'''

# Copy DrawRoundedMediaPreview helpers from chat.cpp lines 74-196 (1-indexed) -> 73:196
helpers = chat_cpp.read_text(encoding="utf-8").splitlines()[73:196]
helpers_text = "\n".join(helpers) + "\n\n"

wrappers = '''
void CUClientChatPasteImage::Reset(CChat *pChat)
{
	ClearPendingUploadImage(pChat);
}

void CUClientChatPasteImage::OnUpdate(CChat *pChat)
{
	UpdatePendingUpload(pChat);
}

bool CUClientChatPasteImage::OnRenderEditor(CChat *pChat)
{
	if(!m_ImageEditor.m_Active || !m_PendingUploadImage.HasImage())
		return false;

	const vec2 WindowSize(maximum(1.0f, (float)pChat->Graphics()->WindowWidth()), maximum(1.0f, (float)pChat->Graphics()->WindowHeight()));
	pChat->Graphics()->MapScreen(0.0f, 0.0f, WindowSize.x, WindowSize.y);
	RenderImageEditor(pChat);

	const float Height = 300.0f;
	const float Width = Height * pChat->Graphics()->ScreenAspect();
	pChat->Graphics()->MapScreen(0.0f, 0.0f, Width, Height);
	return true;
}

bool CUClientChatPasteImage::TrySendOnEnter(CChat *pChat, const char *pMessagePrefix)
{
	if(!m_PendingUploadImage.HasImage())
		return false;
	StartPendingUpload(pChat, pMessagePrefix);
	return true;
}

bool CUClientChatPasteImage::OnInput(CChat *pChat, const IInput::CEvent &Event)
{
	const bool ChatInputActive = pChat->m_Mode != CChat::MODE_NONE;

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE && m_ImageEditor.m_Active)
	{
		CancelImageEditor(pChat);
		return true;
	}

	if(ChatInputActive && Event.m_Key == KEY_MOUSE_1 && m_PendingUploadCloseRectValid)
	{
		const vec2 MousePos = pChat->ChatMousePos();
		const bool InsideCloseButton = MousePos.x >= m_PendingUploadCloseRect.m_X && MousePos.x <= m_PendingUploadCloseRect.m_X + m_PendingUploadCloseRect.m_W &&
			MousePos.y >= m_PendingUploadCloseRect.m_Y && MousePos.y <= m_PendingUploadCloseRect.m_Y + m_PendingUploadCloseRect.m_H;

		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			m_PendingUploadClosePressed = InsideCloseButton;
			if(InsideCloseButton)
				return true;
		}
		else if(Event.m_Flags & IInput::FLAG_RELEASE)
		{
			const bool ActivateButton = m_PendingUploadClosePressed && InsideCloseButton;
			m_PendingUploadClosePressed = false;
			if(ActivateButton)
			{
				ClearPendingUploadImage(pChat);
				return true;
			}
		}
	}

	if(ChatInputActive && (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && m_ImageEditorEditButtonRectValid)
	{
		const vec2 MousePos = pChat->ChatMousePos();
		const bool InsideEditButton = MousePos.x >= m_ImageEditorEditButtonRect.m_X && MousePos.x <= m_ImageEditorEditButtonRect.m_X + m_ImageEditorEditButtonRect.m_W &&
			MousePos.y >= m_ImageEditorEditButtonRect.m_Y && MousePos.y <= m_ImageEditorEditButtonRect.m_Y + m_ImageEditorEditButtonRect.m_H;
		if(InsideEditButton)
		{
			OpenImageEditor(pChat);
			return true;
		}
	}

	if(ChatInputActive && (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_V && pChat->Input()->ModifierIsPressed())
	{
		if(TryPasteClipboardImage(pChat))
			return true;
	}

	return false;
}

'''

block = (root / "_paste_block.cpp").read_text(encoding="utf-8")
out = preamble + helpers_text + wrappers + block
(root / "chat_paste_image.cpp").write_text(out, encoding="utf-8")
(root / "_paste_block.cpp").unlink()
print("Wrote chat_paste_image.cpp", len(out.splitlines()), "lines")
