#include <game/client/components/menus.h>
#include <game/client/gameclient.h>

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
