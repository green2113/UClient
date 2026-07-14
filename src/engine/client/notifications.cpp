#include "notifications.h"

#include <base/detect.h>
#include <base/system.h>

#if defined(CONF_PLATFORM_MACOS)
// Code is in src/macos/notifications.mm.
void NotificationsNotifyMacOsInternal(const char *pTitle, const char *pMessage);
#elif defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_HAIKU) && !defined(CONF_PLATFORM_EMSCRIPTEN)
#include <libnotify/notify.h>
#define NOTIFICATIONS_USE_LIBNOTIFY
#elif defined(CONF_FAMILY_WINDOWS) && __has_include(<windows.ui.notifications.h>)
#define NOTIFICATIONS_USE_WINRT_TOAST
#include <base/windows.h>

#include <roapi.h>
#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Data::Xml::Dom;
using namespace ABI::Windows::UI::Notifications;
#endif

#if defined(NOTIFICATIONS_USE_WINRT_TOAST)
static bool AppendXmlEscaped(char *pBuf, int BufSize, int &Offset, const char *pText)
{
	if(!pText)
		return true;
	for(; *pText; ++pText)
	{
		const char *pRep = nullptr;
		switch(*pText)
		{
		case '&': pRep = "&amp;"; break;
		case '<': pRep = "&lt;"; break;
		case '>': pRep = "&gt;"; break;
		case '"': pRep = "&quot;"; break;
		case '\'': pRep = "&apos;"; break;
		default: break;
		}
		if(pRep)
		{
			const int Len = str_length(pRep);
			if(Offset + Len >= BufSize)
				return false;
			mem_copy(pBuf + Offset, pRep, Len);
			Offset += Len;
		}
		else
		{
			if(Offset + 1 >= BufSize)
				return false;
			pBuf[Offset++] = *pText;
		}
	}
	if(Offset >= BufSize)
		return false;
	pBuf[Offset] = '\0';
	return true;
}

static bool BuildToastXml(char *pOut, int OutSize, const char *pTitle, const char *pMessage)
{
	char aTitle[256];
	char aMessage[512];
	int TitleLen = 0;
	int MessageLen = 0;
	aTitle[0] = '\0';
	aMessage[0] = '\0';
	if(!AppendXmlEscaped(aTitle, sizeof(aTitle), TitleLen, pTitle ? pTitle : "") ||
		!AppendXmlEscaped(aMessage, sizeof(aMessage), MessageLen, pMessage ? pMessage : ""))
	{
		return false;
	}

	str_format(pOut, OutSize,
		"<toast>"
		"<visual>"
		"<binding template='ToastGeneric'>"
		"<text>%s</text>"
		"<text>%s</text>"
		"</binding>"
		"</visual>"
		"</toast>",
		aTitle, aMessage);
	return true;
}
#endif

void CNotifications::Init(const char *pAppname)
{
	m_Initialized = false;
	str_copy(m_aAppName, pAppname ? pAppname : "DDNet", sizeof(m_aAppName));

#if defined(NOTIFICATIONS_USE_LIBNOTIFY)
	notify_init(m_aAppName);
	m_Initialized = true;
#elif defined(NOTIFICATIONS_USE_WINRT_TOAST)
	// Confirm WinRT COM entry points exist (Windows 8+).
	HMODULE LibCombase = LoadLibraryW(L"combase.dll");
	if(!LibCombase)
		return;
	FARPROC RoInitializeProc = GetProcAddress(LibCombase, "RoInitialize");
	FARPROC RoActivateInstanceProc = GetProcAddress(LibCombase, "RoActivateInstance");
	FreeLibrary(LibCombase);
	if(RoInitializeProc == nullptr || RoActivateInstanceProc == nullptr)
		return;

	// Desktop apps need an AppUserModelID for toast notifications to appear.
	HMODULE LibShell32 = LoadLibraryW(L"shell32.dll");
	if(!LibShell32)
		return;
	auto *pSetAumid = reinterpret_cast<HRESULT(WINAPI *)(PCWSTR)>(GetProcAddress(LibShell32, "SetCurrentProcessExplicitAppUserModelID"));
	if(pSetAumid == nullptr)
	{
		FreeLibrary(LibShell32);
		return;
	}
	const HRESULT AumidHr = pSetAumid(windows_utf8_to_wide(m_aAppName).c_str());
	FreeLibrary(LibShell32);
	if(FAILED(AumidHr))
		return;

	// Client already runs CoInitializeEx(APARTMENTTHREADED). RoInitialize is
	// still required for some WinRT factories; S_FALSE / RPC_E_CHANGED_MODE are OK.
	typedef HRESULT(WINAPI *PFnRoInitialize)(RO_INIT_TYPE);
	HMODULE LibCombase2 = LoadLibraryW(L"combase.dll");
	if(LibCombase2)
	{
		auto *pRoInitialize = reinterpret_cast<PFnRoInitialize>(GetProcAddress(LibCombase2, "RoInitialize"));
		if(pRoInitialize)
			pRoInitialize(RO_INIT_SINGLETHREADED);
		FreeLibrary(LibCombase2);
	}

	m_Initialized = true;
#else
	// macOS has no Init-side work; Notify() always works.
	m_Initialized = true;
#endif
}

void CNotifications::Shutdown()
{
#if defined(NOTIFICATIONS_USE_LIBNOTIFY)
	if(m_Initialized)
		notify_uninit();
#elif defined(NOTIFICATIONS_USE_WINRT_TOAST)
	if(!m_Initialized)
		return;

	ComPtr<IToastNotificationManagerStatics> pToastStatics;
	HRESULT Hr = Windows::Foundation::GetActivationFactory(
		HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager).Get(),
		&pToastStatics);
	if(FAILED(Hr))
		return;

	ComPtr<IToastNotificationManagerStatics2> pToastStatics2;
	Hr = pToastStatics.As(&pToastStatics2);
	if(FAILED(Hr))
		return;

	ComPtr<IToastNotificationHistory> pHistory;
	Hr = pToastStatics2->get_History(&pHistory);
	if(FAILED(Hr) || !pHistory)
		return;
	pHistory->ClearWithId(HStringReference(windows_utf8_to_wide(m_aAppName).c_str()).Get());
#endif
	m_Initialized = false;
}

void CNotifications::Notify(const char *pTitle, const char *pMessage)
{
	if(!m_Initialized)
		return;

#if defined(CONF_PLATFORM_MACOS)
	NotificationsNotifyMacOsInternal(pTitle, pMessage);
#elif defined(NOTIFICATIONS_USE_LIBNOTIFY)
	NotifyNotification *pNotif = notify_notification_new(pTitle, pMessage, "ddnet");
	if(pNotif)
	{
		notify_notification_show(pNotif, nullptr);
		g_object_unref(G_OBJECT(pNotif));
	}
#elif defined(NOTIFICATIONS_USE_WINRT_TOAST)
	char aXml[1536];
	if(!BuildToastXml(aXml, sizeof(aXml), pTitle, pMessage))
		return;

	ComPtr<IInspectable> pInspectable;
	HRESULT Hr = RoActivateInstance(HStringReference(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument).Get(), &pInspectable);
	if(FAILED(Hr))
		return;

	ComPtr<IXmlDocument> pDoc;
	Hr = pInspectable.As(&pDoc);
	if(FAILED(Hr))
		return;

	ComPtr<IXmlDocumentIO> pDocIo;
	Hr = pDoc.As(&pDocIo);
	if(FAILED(Hr))
		return;

	Hr = pDocIo->LoadXml(HStringReference(windows_utf8_to_wide(aXml).c_str()).Get());
	if(FAILED(Hr))
		return;

	ComPtr<IToastNotificationManagerStatics> pToastStatics;
	Hr = Windows::Foundation::GetActivationFactory(
		HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager).Get(),
		&pToastStatics);
	if(FAILED(Hr))
		return;

	ComPtr<IToastNotifier> pNotifier;
	Hr = pToastStatics->CreateToastNotifierWithId(HStringReference(windows_utf8_to_wide(m_aAppName).c_str()).Get(), &pNotifier);
	if(FAILED(Hr))
		return;

	ComPtr<IToastNotificationFactory> pFactory;
	Hr = Windows::Foundation::GetActivationFactory(
		HStringReference(RuntimeClass_Windows_UI_Notifications_ToastNotification).Get(),
		&pFactory);
	if(FAILED(Hr))
		return;

	ComPtr<IToastNotification> pToast;
	Hr = pFactory->CreateToastNotification(pDoc.Get(), &pToast);
	if(FAILED(Hr))
		return;

	pNotifier->Show(pToast.Get());
#endif
}

INotifications *CreateNotifications()
{
	return new CNotifications();
}
