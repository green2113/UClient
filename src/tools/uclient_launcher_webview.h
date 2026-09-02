// WebView2 host for the UClient launcher UI.
//
// Everything here is best-effort: if the WebView2 runtime is missing or the
// controller fails to come up, Start() reports failure (or calls OnFailed
// later, since creation is asynchronous) and the caller falls back to the
// GDI renderer.
#pragma once

#ifdef UCLIENT_LAUNCHER_WEBVIEW

#include <windows.h>

#include <objbase.h>

#include <wrl.h>

#include <WebView2.h>

#include <string>

namespace WebUi
{

using MessageFn = void (*)(const std::string &JsonUtf8);
using FailedFn = void (*)();

namespace detail
{
inline ICoreWebView2Controller *g_pController = nullptr;
inline ICoreWebView2 *g_pWebView = nullptr;
inline MessageFn g_OnMessage = nullptr;
inline FailedFn g_OnFailed = nullptr;
inline bool g_Ready = false;

inline std::wstring Widen(const std::string &Utf8)
{
	if(Utf8.empty())
		return std::wstring();
	const int Need = MultiByteToWideChar(CP_UTF8, 0, Utf8.c_str(), (int)Utf8.size(), nullptr, 0);
	if(Need <= 0)
		return std::wstring();
	std::wstring Out((size_t)Need, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, Utf8.c_str(), (int)Utf8.size(), &Out[0], Need);
	return Out;
}

inline std::string Narrow(const wchar_t *pWide)
{
	if(!pWide || !*pWide)
		return std::string();
	const int Need = WideCharToMultiByte(CP_UTF8, 0, pWide, -1, nullptr, 0, nullptr, nullptr);
	if(Need <= 1)
		return std::string();
	std::string Out((size_t)Need - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, pWide, -1, &Out[0], Need, nullptr, nullptr);
	return Out;
}

inline void Fail()
{
	FailedFn pFn = g_OnFailed;
	g_OnFailed = nullptr;
	if(pFn)
		pFn();
}
} // namespace detail

// True when a WebView2 runtime (Edge or the evergreen redistributable) exists.
inline bool IsRuntimeAvailable()
{
	LPWSTR pVersion = nullptr;
	if(FAILED(GetAvailableCoreWebView2BrowserVersionString(nullptr, &pVersion)) || !pVersion)
		return false;
	const bool Ok = pVersion[0] != L'\0';
	CoTaskMemFree(pVersion);
	return Ok;
}

inline bool Ready()
{
	return detail::g_Ready;
}

inline void Resize(HWND hWnd)
{
	if(!detail::g_pController)
		return;
	RECT Rc = {};
	GetClientRect(hWnd, &Rc);
	detail::g_pController->put_Bounds(Rc);
}

// Pushes a JSON state object into the page as window.__setState(<json>).
// Drops the push until the page reports back with {"cmd":"ready"}; the host
// answers that message with a fresh state, so nothing needs queueing.
inline void PostState(const std::string &JsonUtf8)
{
	if(!detail::g_Ready || !detail::g_pWebView)
		return;
	std::wstring Script = L"window.__setState(";
	Script += detail::Widen(JsonUtf8);
	Script += L");";
	detail::g_pWebView->ExecuteScript(Script.c_str(), nullptr);
}

inline void Shutdown()
{
	detail::g_Ready = false;
	if(detail::g_pController)
	{
		detail::g_pController->Close();
		detail::g_pController->Release();
		detail::g_pController = nullptr;
	}
	if(detail::g_pWebView)
	{
		detail::g_pWebView->Release();
		detail::g_pWebView = nullptr;
	}
}

// Kicks off asynchronous creation. Returns false only for failures we can see
// immediately; later failures arrive through OnFailed.
inline bool Start(HWND hWnd, const std::wstring &UserDataFolder, const std::wstring &AssetFolder,
	const wchar_t *pHtml, MessageFn OnMessage, FailedFn OnFailed)
{
	using namespace Microsoft::WRL;
	detail::g_OnMessage = OnMessage;
	detail::g_OnFailed = OnFailed;

	if(!IsRuntimeAvailable())
		return false;

	const std::wstring Assets = AssetFolder;
	const std::wstring Html = pHtml ? pHtml : L"";

	const HRESULT Hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, UserDataFolder.c_str(), nullptr,
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[hWnd, Assets, Html](HRESULT Result, ICoreWebView2Environment *pEnv) -> HRESULT {
				if(FAILED(Result) || !pEnv)
				{
					detail::Fail();
					return S_OK;
				}
				pEnv->CreateCoreWebView2Controller(hWnd,
					Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
						[hWnd, Assets, Html](HRESULT Result2, ICoreWebView2Controller *pController) -> HRESULT {
							if(FAILED(Result2) || !pController)
							{
								detail::Fail();
								return S_OK;
							}
							pController->AddRef();
							detail::g_pController = pController;

							ICoreWebView2 *pWebView = nullptr;
							if(FAILED(pController->get_CoreWebView2(&pWebView)) || !pWebView)
							{
								detail::Fail();
								return S_OK;
							}
							detail::g_pWebView = pWebView; // get_ returns an owning reference

							ICoreWebView2Settings *pSettings = nullptr;
							if(SUCCEEDED(pWebView->get_Settings(&pSettings)) && pSettings)
							{
								pSettings->put_AreDefaultContextMenusEnabled(FALSE);
								pSettings->put_IsZoomControlEnabled(FALSE);
								pSettings->put_IsStatusBarEnabled(FALSE);
								pSettings->put_AreDevToolsEnabled(FALSE);
								pSettings->Release();
							}

							// Dark default background avoids a white flash on first paint.
							ICoreWebView2Controller2 *pController2 = nullptr;
							if(SUCCEEDED(pController->QueryInterface(IID_PPV_ARGS(&pController2))) && pController2)
							{
								COREWEBVIEW2_COLOR Bg = {255, 6, 7, 10};
								pController2->put_DefaultBackgroundColor(Bg);
								pController2->Release();
							}

							// Lets the embedded page reference launcher art as
							// https://uclient.local/<relative path>.
							if(!Assets.empty())
							{
								ICoreWebView2_3 *pWebView3 = nullptr;
								if(SUCCEEDED(pWebView->QueryInterface(IID_PPV_ARGS(&pWebView3))) && pWebView3)
								{
									pWebView3->SetVirtualHostNameToFolderMapping(L"uclient.local", Assets.c_str(),
										COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
									pWebView3->Release();
								}
							}

							EventRegistrationToken Token = {};
							pWebView->add_WebMessageReceived(
								Callback<ICoreWebView2WebMessageReceivedEventHandler>(
									[](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *pArgs) -> HRESULT {
										LPWSTR pMsg = nullptr;
										if(SUCCEEDED(pArgs->TryGetWebMessageAsString(&pMsg)) && pMsg)
										{
											if(detail::g_OnMessage)
												detail::g_OnMessage(detail::Narrow(pMsg));
											CoTaskMemFree(pMsg);
										}
										return S_OK;
									})
									.Get(),
								&Token);

							Resize(hWnd);
							pController->put_IsVisible(TRUE);

							detail::g_Ready = true;
							pWebView->NavigateToString(Html.c_str());
							return S_OK;
						})
						.Get());
				return S_OK;
			})
			.Get());

	return SUCCEEDED(Hr);
}

} // namespace WebUi

#endif // UCLIENT_LAUNCHER_WEBVIEW
