#include <engine/shared/config.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

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

void CMenus::OfferAutoLoginFromChat(int Kind, const char *pCode)
{
	if(Kind != AUTO_LOGIN_OFFER_JAPAN && Kind != AUTO_LOGIN_OFFER_KOG)
		return;
	if(!pCode || pCode[0] == '\0')
		return;

	m_AutoLoginOfferKind = Kind;
	str_copy(m_aAutoLoginOfferCode, pCode, sizeof(m_aAutoLoginOfferCode));

	PopupConfirm(
		"Auto Login",
		"When you connect to this server, enable auto login with the code you just entered?",
		Localize("Confirm"),
		Localize("Cancel"),
		&CMenus::PopupConfirmAutoLoginOffer,
		POPUP_NONE,
		&CMenus::PopupCancelAutoLoginOffer,
		POPUP_NONE);
	SetActive(true);
}

void CMenus::PopupConfirmAutoLoginOffer()
{
	if(m_AutoLoginOfferKind == AUTO_LOGIN_OFFER_JAPAN)
	{
		g_Config.m_UcAutoLoginJapan = 1;
		str_copy(g_Config.m_UcAutoLoginJapanCode, m_aAutoLoginOfferCode, sizeof(g_Config.m_UcAutoLoginJapanCode));
		g_Config.m_UcAutoLoginJapanPromptShown = 1;
	}
	else if(m_AutoLoginOfferKind == AUTO_LOGIN_OFFER_KOG)
	{
		g_Config.m_UcAutoLoginKog = 1;
		str_copy(g_Config.m_UcAutoLoginKogCode, m_aAutoLoginOfferCode, sizeof(g_Config.m_UcAutoLoginKogCode));
		g_Config.m_UcAutoLoginKogPromptShown = 1;
	}

	m_AutoLoginOfferKind = AUTO_LOGIN_OFFER_NONE;
	m_aAutoLoginOfferCode[0] = '\0';
	SetActive(false);
}

void CMenus::PopupCancelAutoLoginOffer()
{
	if(m_AutoLoginOfferKind == AUTO_LOGIN_OFFER_JAPAN)
		g_Config.m_UcAutoLoginJapanPromptShown = 1;
	else if(m_AutoLoginOfferKind == AUTO_LOGIN_OFFER_KOG)
		g_Config.m_UcAutoLoginKogPromptShown = 1;

	m_AutoLoginOfferKind = AUTO_LOGIN_OFFER_NONE;
	m_aAutoLoginOfferCode[0] = '\0';
	SetActive(false);
}
