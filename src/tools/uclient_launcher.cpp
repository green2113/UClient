// UClient pre-game launcher.
// Checks for updates (Play / auto-launch), applies zip in-process, then starts
// DDNet.exe with a one-time --uclient-from-launcher token. Also registers ddnet:// etc.
//
// The UI is HTML/CSS hosted in WebView2 (uclient_launcher_ui.h). When the
// runtime is missing or the SDK was not available at build time, the Win32/GDI
// renderer further down this file takes over instead.

#include <windows.h>
#include <windowsx.h>
#include <wingdi.h>
#include <winuser.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <wincodec.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

#include <cctype>

#ifdef UCLIENT_LAUNCHER_WEBVIEW
#include "uclient_launcher_ui.h"
#include "uclient_launcher_webview.h"
#endif

#ifndef UCLIENT_LAUNCHER_VERSION
#define UCLIENT_LAUNCHER_VERSION "0.0.0"
#endif
#ifndef UCLIENT_UPDATE_LATEST_URL
#define UCLIENT_UPDATE_LATEST_URL "https://ddnet.under1111.com/api/uclient/update/latest"
#endif
#ifndef UCLIENT_API_BASE_URL
#define UCLIENT_API_BASE_URL "https://uclient.under1111.com"
#endif

static const wchar_t *kTokenArg = L"--uclient-from-launcher";
static const wchar_t *kTokenFile = L"uclient_launch.token";
static const wchar_t *kVersionFile = L"uclient_version.txt";
static const wchar_t *kPendingVersionFile = L"uclient_pending_version.txt";
static const wchar_t *kApplyUpdateArg = L"--uclient-apply-update";
static const wchar_t *kWaitPidArg = L"--uclient-wait-pid";
static const wchar_t *kFromGameArg = L"--uclient-from-game";
static const wchar_t *kGameExe = L"DDNet.exe";
static const wchar_t kPlayLabel[] = L"Play";
static const wchar_t kUpdateLabel[] = L"Update";
static const wchar_t kRunningLabel[] = L"RUNNING";
static const wchar_t *kArchiveRel = L"update\\bestclient-release.zip";

static const wchar_t *k_aUserDirs[] = {
	L"data\\assets\\arrow",
	L"data\\assets\\arrows",
	L"data\\assets\\audio",
	L"data\\audio",
};

static const int WND_W = 1160;
static const int WND_H = 700;
static const int WND_RADIUS = 12;
static const int RAIL_W = 76;
static const int PANEL_W = 360;

static const COLORREF C_TITLE = RGB(245, 245, 247);
static const COLORREF C_DIM = RGB(170, 172, 180);
static const COLORREF C_MUTED = RGB(120, 122, 132);
static const COLORREF C_ACCENT = RGB(124, 108, 240); // soft violet
static const COLORREF C_ACCENT_HOVER = RGB(155, 142, 248);
static const COLORREF C_ACCENT_DIM = RGB(90, 74, 216);
static const COLORREF C_ORANGE = RGB(255, 148, 48);
static const COLORREF C_BLUE = RGB(72, 168, 240);
static const COLORREF C_BTN_DISABLED = RGB(42, 44, 52);
static const COLORREF C_BAR_TRACK = RGB(36, 38, 46);
static const COLORREF C_BAR_FILL = RGB(155, 142, 248);
static const COLORREF C_ERROR = RGB(255, 110, 100);
static const COLORREF C_BORDER = RGB(55, 58, 68);
static const COLORREF C_SIDE = RGB(14, 14, 18);
static const COLORREF C_PANEL = RGB(22, 24, 30);
static const COLORREF C_PANEL2 = RGB(32, 34, 42);

static const wchar_t *kSettingsFile = L"uclient_launcher.cfg";

enum class EUiPhase
{
	Checking,
	Updating,
	Ready,
	Launching,
};

enum class EUpdateStage
{
	None,
	Check,
	Download,
	Apply,
};

enum class EMainTab
{
	Overview,
	Updates,
};

struct FriendView
{
	std::string Name;
	std::string Clan;
	bool Online = false;
	std::string ServerName;
	std::string MapName;
	std::string Address;
	RECT HitRc = {};
};

struct NoticeView
{
	std::string Id;
	std::string Title;
	std::string Body;
	std::string Severity;
	bool BlocksPlay = false;
	bool HasExpiresAt = false;
	bool BanPermanent = false;
	int64_t ExpiresAt = 0;
};

struct LauncherArgs
{
	std::wstring InstallDir;
	std::wstring SelfPath;
	std::vector<std::wstring> ForwardArgs;
	std::wstring ApplyArchive; // non-empty → apply zip then start game (no update check)
	DWORD WaitPid = 0;
};

static HWND g_hWnd = nullptr;
static HANDLE g_hSingleInstanceMutex = nullptr;
static std::atomic<int> g_Percent = 0;
static bool g_Failed = false;
static CRITICAL_SECTION g_Lock;
static wchar_t g_aStatus[256] = L"";
static wchar_t g_aButtonLabel[128] = L"Checking for updates";
static wchar_t g_aVersionText[96] = L"Version —";
static EUiPhase g_Phase = EUiPhase::Checking;
static std::atomic<EUpdateStage> g_UpdateStage = EUpdateStage::Check;
static std::atomic<uint64_t> g_DownloadDone = 0;
static std::atomic<uint64_t> g_DownloadTotal = 0;
static std::atomic<uint64_t> g_DownloadSpeed = 0; // bytes per second
static std::atomic<int> g_EtaSeconds = -1;
static bool g_ShowSettings = false;
static bool g_AutoLaunch = false; // default off
static bool g_AutoUpdate = false; // default off; startup check only
static bool g_TryStartupAutoUpdate = false;
static bool g_DiscordRpc = true; // mirrors tc_discord_rpc (default on)
static bool g_LaunchedFromGame = false; // set when DDNet.exe redirected here
static bool g_PlayHover = false;
static bool g_GearHover = false;
static bool g_MinHover = false;
static bool g_CloseHover = false;
static bool g_BackHover = false;
static bool g_FriendRefreshHover = false;
static EMainTab g_MainTab = EMainTab::Overview;

// Animation state, driven by a ~60fps timer while anything is still moving.
static float g_AnimPlay = 0.0f; // play button hover
static float g_AnimGear = 0.0f;
static float g_AnimFriend = 0.0f; // hovered row highlight
static float g_AnimTab = 0.0f; // underline slide: 0 = Overview, 1 = Updates
static float g_AnimIntro = 0.0f; // window fade/slide in
static float g_AnimSpin = 0.0f; // busy indicator phase
static int g_AnimFriendRow = -1;
static int g_FriendHover = -1;
static int g_FriendScroll = 0;
static bool g_FriendsLoading = false;
static bool g_FriendsLoaded = false;
static bool g_NoticesRefreshing = false;
static bool g_UpdateCheckRefreshing = false;
static std::atomic<bool> g_UpdateDownloadRunning{false};
static bool g_UpdateAvailable = false;
static bool g_GameRunning = false;
static std::string g_PendingRemoteVersion;
static std::string g_PendingArchiveUrl;
static std::wstring g_ButtonHint;
#ifdef CONF_UCLIENT_LAUNCHER_DEV
static struct
{
	bool ForceUpdateAvailable = false;
	bool ForcePlayBlocked = false;
	bool ForceGameRunning = false;
	bool InjectNotice = false;
} g_Dev;
#endif
static std::vector<FriendView> g_Friends;
static std::vector<NoticeView> g_Notices;
static bool g_PlayBlocked = false;
static std::wstring g_ConnectAddress;
static HANDLE g_hLaunchedGame = nullptr;
static DWORD g_LaunchPollStartTick = 0;
static LauncherArgs *g_pArgs = nullptr;
static std::wstring g_InstallDir;

static RECT g_PlayBtnRc = {};
static RECT g_GearRc = {};
static RECT g_MinRc = {};
static RECT g_CloseRc = {};
static RECT g_CheckRc = {};
static RECT g_AutoUpdateCheckRc = {};
static RECT g_DiscordCheckRc = {};
static RECT g_BackRc = {};
static RECT g_TabOverviewRc = {};
static RECT g_TabUpdatesRc = {};
static RECT g_FriendAreaRc = {};
static RECT g_FriendRefreshRc = {};

static HBITMAP g_hLogoBmp = nullptr;
static int g_LogoW = 0;
static int g_LogoH = 0;
static HBITMAP g_hMascotBmp = nullptr;
static int g_MascotW = 0;
static int g_MascotH = 0;

#define WM_WORKER_TICK (WM_APP + 0)
#define WM_WORKER_DONE (WM_APP + 1)
#define WM_UPDATE_READY (WM_APP + 2)
#define WM_FRIENDS_READY (WM_APP + 3)
#define WM_NOTICES_READY (WM_APP + 4)
#define WM_UPDATE_CHECK_READY (WM_APP + 5)
#define WM_SHOW_LAUNCHER (WM_APP + 6)

#define ANIM_TIMER_ID 1
#define LAUNCH_TIMER_ID 2
#define NOTICES_TIMER_ID 3
#define UPDATE_CHECK_TIMER_ID 4
#define GAME_POLL_TIMER_ID 5
#define NOTICE_POLL_MS 20000
#define UPDATE_CHECK_POLL_MS 60000
#define GAME_POLL_MS 2500

static void FinishLaunchKeepOpen();
static void PushWebState(bool Force = false);
static void CloseLaunchedGameHandle();
static void BeginLaunchPoll();
static void PollLaunchProcess();
static void OnGameLaunchFailed(const wchar_t *pStatus);
static void SyncReadyButtonLabel();
static void SyncButtonHint();
static void RefreshGameRunningState();
static void RequestUpdateCheck();
static void RequestUpdateDownload();
static void TryStartupAutoUpdate();
static void ShowLauncherWindow(HWND hWnd);
static void ActivateExistingLauncherWindow(HWND hWnd);
static bool AcquireSingleInstanceOrActivateExisting(const std::wstring &InstallDir);
static bool RunUpdateDownload(LauncherArgs *pA, const std::string &RemoteVersion, const std::string &ArchiveUrl);
#ifdef CONF_UCLIENT_LAUNCHER_DEV
static void RequestFakeDownload();
static void HandleDevCommand(const std::string &Json);
static bool EffectiveUpdateAvailable();
static bool EffectivePlayBlocked();
static bool EffectiveGameRunning();
#else
static bool EffectiveUpdateAvailable() { return g_UpdateAvailable; }
static bool EffectivePlayBlocked() { return g_PlayBlocked; }
static bool EffectiveGameRunning() { return g_GameRunning; }
#endif

static void SetStatus(const wchar_t *pText)
{
	EnterCriticalSection(&g_Lock);
	wcsncpy_s(g_aStatus, pText, _TRUNCATE);
	LeaveCriticalSection(&g_Lock);
	if(g_hWnd)
		PostMessage(g_hWnd, WM_WORKER_TICK, 0, 0);
}

static void SetPercent(int Pct)
{
	if(Pct < 0)
		Pct = 0;
	if(Pct > 100)
		Pct = 100;
	g_Percent.store(Pct);
	if(g_hWnd)
		PostMessage(g_hWnd, WM_WORKER_TICK, 0, 0);
}

static void SetPhase(EUiPhase Phase)
{
	g_Phase = Phase;
	if(g_hWnd)
		PostMessage(g_hWnd, WM_WORKER_TICK, 0, 0);
}

static void SetButtonLabel(const wchar_t *pText)
{
	EnterCriticalSection(&g_Lock);
	wcsncpy_s(g_aButtonLabel, pText, _TRUNCATE);
	LeaveCriticalSection(&g_Lock);
	if(g_hWnd)
		PostMessage(g_hWnd, WM_WORKER_TICK, 0, 0);
}

static void SetVersionLabel(const std::string &Version)
{
	EnterCriticalSection(&g_Lock);
	if(Version.empty())
		wcsncpy_s(g_aVersionText, L"Version —", _TRUNCATE);
	else
		_snwprintf_s(g_aVersionText, _TRUNCATE, L"Version %hs", Version.c_str());
	LeaveCriticalSection(&g_Lock);
	if(g_hWnd)
		PostMessage(g_hWnd, WM_WORKER_TICK, 0, 0);
}

static bool PtInRectI(const RECT &Rc, int X, int Y)
{
	return X >= Rc.left && X < Rc.right && Y >= Rc.top && Y < Rc.bottom;
}

static std::wstring JoinPath(const std::wstring &Dir, const wchar_t *File);
static bool ReadTextFile(const std::wstring &Path, std::string &Out);
static bool HttpGetToString(const std::wstring &Url, std::string &OutBody);

static void FillRoundRect(HDC Dc, const RECT &Rc, int Radius, COLORREF Color)
{
	HBRUSH Brush = CreateSolidBrush(Color);
	HPEN Pen = CreatePen(PS_SOLID, 1, Color);
	HGDIOBJ OldBrush = SelectObject(Dc, Brush);
	HGDIOBJ OldPen = SelectObject(Dc, Pen);
	RoundRect(Dc, Rc.left, Rc.top, Rc.right, Rc.bottom, Radius, Radius);
	SelectObject(Dc, OldBrush);
	SelectObject(Dc, OldPen);
	DeleteObject(Brush);
	DeleteObject(Pen);
}

static void StrokeRoundRect(HDC Dc, const RECT &Rc, int Radius, COLORREF Color)
{
	HPEN Pen = CreatePen(PS_SOLID, 1, Color);
	HGDIOBJ OldPen = SelectObject(Dc, Pen);
	HGDIOBJ OldBrush = SelectObject(Dc, GetStockObject(HOLLOW_BRUSH));
	RoundRect(Dc, Rc.left, Rc.top, Rc.right - 1, Rc.bottom - 1, Radius, Radius);
	SelectObject(Dc, OldBrush);
	SelectObject(Dc, OldPen);
	DeleteObject(Pen);
}

static void FillVerticalGradient(HDC Dc, const RECT &Rc, COLORREF Top, COLORREF Bottom)
{
	TRIVERTEX Vert[2];
	Vert[0].x = Rc.left;
	Vert[0].y = Rc.top;
	Vert[0].Red = GetRValue(Top) << 8;
	Vert[0].Green = GetGValue(Top) << 8;
	Vert[0].Blue = GetBValue(Top) << 8;
	Vert[0].Alpha = 0;
	Vert[1].x = Rc.right;
	Vert[1].y = Rc.bottom;
	Vert[1].Red = GetRValue(Bottom) << 8;
	Vert[1].Green = GetGValue(Bottom) << 8;
	Vert[1].Blue = GetBValue(Bottom) << 8;
	Vert[1].Alpha = 0;
	GRADIENT_RECT Gr = {0, 1};
	GradientFill(Dc, Vert, 2, &Gr, 1, GRADIENT_FILL_RECT_V);
}

static void DrawGlow(HDC Dc, int Cx, int Cy, int Radius, COLORREF Color, BYTE MaxAlpha)
{
	const int Size = Radius * 2;
	if(Size <= 0)
		return;
	BITMAPINFO Bi = {};
	Bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	Bi.bmiHeader.biWidth = Size;
	Bi.bmiHeader.biHeight = -Size;
	Bi.bmiHeader.biPlanes = 1;
	Bi.bmiHeader.biBitCount = 32;
	Bi.bmiHeader.biCompression = BI_RGB;
	void *pBits = nullptr;
	HBITMAP Bmp = CreateDIBSection(Dc, &Bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
	if(!Bmp || !pBits)
		return;
	auto *pPx = static_cast<DWORD *>(pBits);
	const int Cr = GetRValue(Color);
	const int Cg = GetGValue(Color);
	const int Cb = GetBValue(Color);
	for(int Y = 0; Y < Size; ++Y)
	{
		for(int X = 0; X < Size; ++X)
		{
			const float Dx = (float)(X - Radius) + 0.5f;
			const float Dy = (float)(Y - Radius) + 0.5f;
			const float Dist = sqrtf(Dx * Dx + Dy * Dy) / (float)Radius;
			float T = 1.0f - Dist;
			if(T < 0.0f)
				T = 0.0f;
			T = T * T;
			const BYTE A = (BYTE)(T * (float)MaxAlpha);
			pPx[Y * Size + X] = ((DWORD)A << 24) | ((DWORD)((Cr * A) / 255) << 16) | ((DWORD)((Cg * A) / 255) << 8) | (DWORD)((Cb * A) / 255);
		}
	}
	HDC Mem = CreateCompatibleDC(Dc);
	HGDIOBJ Old = SelectObject(Mem, Bmp);
	BLENDFUNCTION Blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
	AlphaBlend(Dc, Cx - Radius, Cy - Radius, Size, Size, Mem, 0, 0, Size, Size, Blend);
	SelectObject(Mem, Old);
	DeleteDC(Mem);
	DeleteObject(Bmp);
}

static HBITMAP LoadPngFile(const wchar_t *pPath, int *pOutW, int *pOutH)
{
	*pOutW = 0;
	*pOutH = 0;
	IWICImagingFactory *pFactory = nullptr;
	if(FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory))))
		return nullptr;

	IWICBitmapDecoder *pDecoder = nullptr;
	HBITMAP Result = nullptr;
	if(SUCCEEDED(pFactory->CreateDecoderFromFilename(pPath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &pDecoder)))
	{
		IWICBitmapFrameDecode *pFrame = nullptr;
		if(SUCCEEDED(pDecoder->GetFrame(0, &pFrame)))
		{
			IWICFormatConverter *pConv = nullptr;
			if(SUCCEEDED(pFactory->CreateFormatConverter(&pConv)))
			{
				if(SUCCEEDED(pConv->Initialize(pFrame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
				{
					UINT W = 0, H = 0;
					pConv->GetSize(&W, &H);
					BITMAPINFO Bi = {};
					Bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
					Bi.bmiHeader.biWidth = (LONG)W;
					Bi.bmiHeader.biHeight = -(LONG)H;
					Bi.bmiHeader.biPlanes = 1;
					Bi.bmiHeader.biBitCount = 32;
					Bi.bmiHeader.biCompression = BI_RGB;
					void *pBits = nullptr;
					HDC Screen = GetDC(nullptr);
					HBITMAP Bmp = CreateDIBSection(Screen, &Bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
					ReleaseDC(nullptr, Screen);
					if(Bmp && pBits)
					{
						const UINT Stride = W * 4;
						if(SUCCEEDED(pConv->CopyPixels(nullptr, Stride, Stride * H, static_cast<BYTE *>(pBits))))
						{
							// Brand assets ship on solid black — punch that out to alpha.
							auto *pPx = static_cast<BYTE *>(pBits);
							for(UINT I = 0; I < W * H; ++I)
							{
								BYTE *p = pPx + I * 4;
								const BYTE B = p[0], G = p[1], R = p[2], A = p[3];
								if(A == 0)
									continue;
								if(R < 18 && G < 18 && B < 18)
								{
									p[0] = p[1] = p[2] = p[3] = 0;
									continue;
								}
								// Ensure premultiplied alpha for AlphaBlend.
								p[0] = (BYTE)((B * A) / 255);
								p[1] = (BYTE)((G * A) / 255);
								p[2] = (BYTE)((R * A) / 255);
							}
							*pOutW = (int)W;
							*pOutH = (int)H;
							Result = Bmp;
						}
						else
						{
							DeleteObject(Bmp);
						}
					}
				}
				pConv->Release();
			}
			pFrame->Release();
		}
		pDecoder->Release();
	}
	pFactory->Release();
	return Result;
}

static void LoadLauncherArt(const std::wstring &InstallDir)
{
	if(g_hLogoBmp)
	{
		DeleteObject(g_hLogoBmp);
		g_hLogoBmp = nullptr;
	}
	if(g_hMascotBmp)
	{
		DeleteObject(g_hMascotBmp);
		g_hMascotBmp = nullptr;
	}
	const std::wstring LogoPath = JoinPath(InstallDir, L"data\\BestClient\\gui_logo.png");
	g_hLogoBmp = LoadPngFile(LogoPath.c_str(), &g_LogoW, &g_LogoH);
	if(!g_hLogoBmp)
	{
		const std::wstring Fallback = JoinPath(InstallDir, L"data\\gui_logo.png");
		g_hLogoBmp = LoadPngFile(Fallback.c_str(), &g_LogoW, &g_LogoH);
	}
	const std::wstring MascotPath = JoinPath(InstallDir, L"data\\uclient\\logo\\uclient.png");
	g_hMascotBmp = LoadPngFile(MascotPath.c_str(), &g_MascotW, &g_MascotH);
}

static void FreeLauncherArt()
{
	if(g_hLogoBmp)
	{
		DeleteObject(g_hLogoBmp);
		g_hLogoBmp = nullptr;
	}
	if(g_hMascotBmp)
	{
		DeleteObject(g_hMascotBmp);
		g_hMascotBmp = nullptr;
	}
}

static void DrawBitmapAlpha(HDC Dc, HBITMAP Bmp, int X, int Y, int DstW, int DstH, int SrcW, int SrcH)
{
	if(!Bmp || DstW <= 0 || DstH <= 0)
		return;
	HDC Mem = CreateCompatibleDC(Dc);
	HGDIOBJ Old = SelectObject(Mem, Bmp);
	BLENDFUNCTION Blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
	AlphaBlend(Dc, X, Y, DstW, DstH, Mem, 0, 0, SrcW, SrcH, Blend);
	SelectObject(Mem, Old);
	DeleteDC(Mem);
}

static void ApplyWindowRoundCorners(HWND hWnd)
{
	// Let DWM own the outer shape on Windows 11. SetWindowRgn clips the client
	// area with aliased edges and fights DWM's native rounded corners.
	const DWORD Pref = 2; // DWMWCP_ROUND
	HMODULE hDwmapi = LoadLibraryW(L"dwmapi.dll");
	if(hDwmapi)
	{
		using PfnSet = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
		auto *pSet = reinterpret_cast<PfnSet>(GetProcAddress(hDwmapi, "DwmSetWindowAttribute"));
		if(pSet)
		{
			pSet(hWnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &Pref, sizeof(Pref));
			const COLORREF NoBorder = 0xFFFFFFFE; // DWMWA_COLOR_NONE
			pSet(hWnd, 34 /* DWMWA_BORDER_COLOR */, &NoBorder, sizeof(NoBorder));
		}
		FreeLibrary(hDwmapi);
	}
}

static void DrawCaptionButton(HDC Dc, const RECT &Rc, bool Hover, bool Close)
{
	if(Hover)
		FillRoundRect(Dc, Rc, 12, Close ? RGB(232, 64, 64) : RGB(48, 50, 58));
	HPEN Pen = CreatePen(PS_SOLID, 2, C_TITLE);
	HGDIOBJ OldPen = SelectObject(Dc, Pen);
	const int Cx = (Rc.left + Rc.right) / 2;
	const int Cy = (Rc.top + Rc.bottom) / 2;
	if(Close)
	{
		MoveToEx(Dc, Cx - 5, Cy - 5, nullptr);
		LineTo(Dc, Cx + 6, Cy + 6);
		MoveToEx(Dc, Cx + 5, Cy - 5, nullptr);
		LineTo(Dc, Cx - 6, Cy + 6);
	}
	else
	{
		MoveToEx(Dc, Cx - 5, Cy + 1, nullptr);
		LineTo(Dc, Cx + 6, Cy + 1);
	}
	SelectObject(Dc, OldPen);
	DeleteObject(Pen);
}

static COLORREF LerpColor(COLORREF A, COLORREF B, float T)
{
	if(T < 0.0f)
		T = 0.0f;
	if(T > 1.0f)
		T = 1.0f;
	const int R = (int)(GetRValue(A) + (GetRValue(B) - GetRValue(A)) * T);
	const int G = (int)(GetGValue(A) + (GetGValue(B) - GetGValue(A)) * T);
	const int Bl = (int)(GetBValue(A) + (GetBValue(B) - GetBValue(A)) * T);
	return RGB(R, G, Bl);
}

// Approach Target with an exponential ease; returns true while still moving.
static bool Approach(float &Value, float Target, float Speed)
{
	const float Diff = Target - Value;
	if(fabsf(Diff) < 0.004f)
	{
		Value = Target;
		return false;
	}
	Value += Diff * Speed;
	return true;
}

// Solid toothed gear so it reads as a settings icon at small sizes.
static void DrawGearIcon(HDC Dc, const RECT &Rc, float Hover, float Spin)
{
	if(Hover > 0.01f)
		FillRoundRect(Dc, Rc, 10, LerpColor(RGB(24, 26, 32), RGB(52, 55, 66), Hover));

	const int Cx = (Rc.left + Rc.right) / 2;
	const int Cy = (Rc.top + Rc.bottom) / 2;
	const double Pi = 3.14159265358979323846;
	const int Teeth = 8;
	const double Outer = 10.0;
	const double Inner = 7.0;
	const double Base = Spin * Pi / (double)Teeth;

	POINT aPts[Teeth * 4];
	int N = 0;
	for(int i = 0; i < Teeth; ++i)
	{
		const double A0 = Base + (2.0 * Pi * i) / Teeth;
		const double Step = (2.0 * Pi / Teeth) / 4.0;
		const double aAng[4] = {A0, A0 + Step, A0 + Step * 2.0, A0 + Step * 3.0};
		const double aRad[4] = {Outer, Outer, Inner, Inner};
		for(int k = 0; k < 4; ++k)
		{
			aPts[N].x = Cx + (LONG)(aRad[k] * cos(aAng[k]));
			aPts[N].y = Cy + (LONG)(aRad[k] * sin(aAng[k]));
			++N;
		}
	}

	const COLORREF IconColor = LerpColor(C_DIM, C_TITLE, Hover);
	HBRUSH Brush = CreateSolidBrush(IconColor);
	HPEN Pen = CreatePen(PS_SOLID, 1, IconColor);
	HGDIOBJ OldBrush = SelectObject(Dc, Brush);
	HGDIOBJ OldPen = SelectObject(Dc, Pen);
	Polygon(Dc, aPts, N);
	SelectObject(Dc, OldBrush);
	SelectObject(Dc, OldPen);
	DeleteObject(Brush);
	DeleteObject(Pen);

	// Hub punched out in the panel color behind the rail.
	HBRUSH Hub = CreateSolidBrush(Hover > 0.5f ? RGB(52, 55, 66) : C_SIDE);
	HPEN HubPen = CreatePen(PS_SOLID, 1, Hover > 0.5f ? RGB(52, 55, 66) : C_SIDE);
	OldBrush = SelectObject(Dc, Hub);
	OldPen = SelectObject(Dc, HubPen);
	Ellipse(Dc, Cx - 4, Cy - 4, Cx + 4, Cy + 4);
	SelectObject(Dc, OldBrush);
	SelectObject(Dc, OldPen);
	DeleteObject(Hub);
	DeleteObject(HubPen);
}

static std::wstring JoinPath(const std::wstring &Dir, const wchar_t *File)
{
	if(Dir.empty())
		return File;
	const wchar_t Last = Dir.back();
	if(Last == L'\\' || Last == L'/')
		return Dir + File;
	return Dir + L"\\" + File;
}

static std::wstring ParentDir(const std::wstring &Path)
{
	const size_t Pos = Path.find_last_of(L"\\/");
	if(Pos == std::wstring::npos)
		return L"";
	return Path.substr(0, Pos);
}

static std::wstring Utf8ToWide(const char *pUtf8)
{
	if(!pUtf8 || !pUtf8[0])
		return L"";
	const int Need = MultiByteToWideChar(CP_UTF8, 0, pUtf8, -1, nullptr, 0);
	if(Need <= 0)
		return L"";
	std::wstring Out(Need - 1, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, pUtf8, -1, Out.data(), Need);
	return Out;
}

static std::string WideToUtf8(const std::wstring &Wide)
{
	if(Wide.empty())
		return {};
	const int Need = WideCharToMultiByte(CP_UTF8, 0, Wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if(Need <= 0)
		return {};
	std::string Out(Need - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, Wide.c_str(), -1, Out.data(), Need, nullptr, nullptr);
	return Out;
}

static bool GenerateToken(std::wstring &Out)
{
	unsigned char aBytes[16];
	HCRYPTPROV hProv = 0;
	if(!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
		return false;
	const BOOL Ok = CryptGenRandom(hProv, sizeof(aBytes), aBytes);
	CryptReleaseContext(hProv, 0);
	if(!Ok)
		return false;
	wchar_t aHex[33];
	for(int i = 0; i < 16; ++i)
		_snwprintf_s(aHex + i * 2, 3, _TRUNCATE, L"%02x", aBytes[i]);
	Out = aHex;
	return true;
}

static bool WriteLaunchToken(const std::wstring &InstallDir, std::wstring &TokenOut)
{
	if(!GenerateToken(TokenOut))
		return false;
	const std::wstring Path = JoinPath(InstallDir, kTokenFile);
	FILE *pFile = nullptr;
	if(_wfopen_s(&pFile, Path.c_str(), L"wb") != 0 || !pFile)
		return false;
	const std::string TokenUtf8 = WideToUtf8(TokenOut);
	fprintf(pFile, "%s\n%lld\n", TokenUtf8.c_str(), (long long)time(nullptr));
	fclose(pFile);
	return true;
}

static void QuoteArg(std::wstring &Cmd, const std::wstring &Arg)
{
	if(!Cmd.empty())
		Cmd.push_back(L' ');
	const bool NeedQuote = Arg.find_first_of(L" \t\"") != std::wstring::npos;
	if(!NeedQuote)
	{
		Cmd += Arg;
		return;
	}
	Cmd.push_back(L'"');
	for(wchar_t Ch : Arg)
	{
		if(Ch == L'"')
			Cmd += L"\\\"";
		else
			Cmd.push_back(Ch);
	}
	Cmd.push_back(L'"');
}

static bool LaunchProcess(const std::wstring &Exe, const std::vector<std::wstring> &Args, const std::wstring &WorkDir, bool Wait, HANDLE *pOutProcess = nullptr)
{
	std::wstring Cmd;
	QuoteArg(Cmd, Exe);
	for(const auto &A : Args)
		QuoteArg(Cmd, A);

	STARTUPINFOW Si = {};
	Si.cb = sizeof(Si);
	PROCESS_INFORMATION Pi = {};
	std::wstring Mutable = Cmd;
	if(!CreateProcessW(nullptr, Mutable.data(), nullptr, nullptr, FALSE, 0, nullptr,
		   WorkDir.empty() ? nullptr : WorkDir.c_str(), &Si, &Pi))
		return false;
	if(Wait)
		WaitForSingleObject(Pi.hProcess, INFINITE);
	if(pOutProcess)
		*pOutProcess = Pi.hProcess;
	else
		CloseHandle(Pi.hProcess);
	CloseHandle(Pi.hThread);
	return true;
}

static bool LaunchGame(const LauncherArgs *pA, HANDLE *pOutProcess = nullptr)
{
	std::wstring Token;
	if(!WriteLaunchToken(pA->InstallDir, Token))
	{
		SetStatus(L"Failed to create launch token");
		g_Failed = true;
		return false;
	}

	std::vector<std::wstring> Args;
	Args.emplace_back(kTokenArg);
	Args.push_back(Token);
	for(const auto &A : pA->ForwardArgs)
		Args.push_back(A);
	if(!g_ConnectAddress.empty())
	{
		std::wstring Link = L"ddnet://";
		Link += g_ConnectAddress;
		Args.push_back(Link);
	}
	// Pass the launcher preference as a runtime console override. The game will
	// persist it through its own atomic config writer.
	Args.emplace_back(g_DiscordRpc ? L"tc_discord_rpc 1" : L"tc_discord_rpc 0");

	const std::wstring Game = JoinPath(pA->InstallDir, kGameExe);
	SetStatus(L"Starting UClient...");
	SetPercent(100);
	HANDLE hProcess = nullptr;
	if(!LaunchProcess(Game, Args, pA->InstallDir, false, &hProcess))
	{
		SetStatus(L"Failed to start DDNet.exe");
		g_Failed = true;
		return false;
	}
	if(pOutProcess)
		*pOutProcess = hProcess;
	else if(hProcess)
		CloseHandle(hProcess);
	return true;
}

// ─── Version / JSON helpers ───────────────────────────────────────────────────

static void NormalizeVersion(const std::string &In, std::string &Out)
{
	size_t i = 0;
	while(i < In.size() && isspace((unsigned char)In[i]))
		++i;
	if(i < In.size() && (In[i] == 'v' || In[i] == 'V') && i + 1 < In.size() && isdigit((unsigned char)In[i + 1]))
		++i;
	Out = In.substr(i);
}

static std::vector<int> VersionParts(const std::string &V)
{
	std::vector<int> Parts;
	int Cur = -1;
	for(unsigned char Ch : V)
	{
		if(isdigit(Ch))
		{
			if(Cur < 0)
				Cur = 0;
			Cur = Cur * 10 + (Ch - '0');
		}
		else if(Cur >= 0)
		{
			Parts.push_back(Cur);
			Cur = -1;
		}
	}
	if(Cur >= 0)
		Parts.push_back(Cur);
	return Parts;
}

static int CompareVersions(const std::string &Left, const std::string &Right)
{
	std::string A, B;
	NormalizeVersion(Left, A);
	NormalizeVersion(Right, B);
	const auto LA = VersionParts(A);
	const auto LB = VersionParts(B);
	const size_t N = LA.size() > LB.size() ? LA.size() : LB.size();
	for(size_t i = 0; i < N; ++i)
	{
		const int X = i < LA.size() ? LA[i] : 0;
		const int Y = i < LB.size() ? LB[i] : 0;
		if(X < Y)
			return -1;
		if(X > Y)
			return 1;
	}
	return 0;
}

static bool ReadTextFile(const std::wstring &Path, std::string &Out)
{
	FILE *pFile = nullptr;
	if(_wfopen_s(&pFile, Path.c_str(), L"rb") != 0 || !pFile)
		return false;
	char aBuf[256];
	size_t N = fread(aBuf, 1, sizeof(aBuf) - 1, pFile);
	fclose(pFile);
	if(N == 0)
		return false;
	aBuf[N] = '\0';
	while(N > 0 && (aBuf[N - 1] == '\n' || aBuf[N - 1] == '\r' || aBuf[N - 1] == ' '))
		aBuf[--N] = '\0';
	Out.assign(aBuf, N);
	return !Out.empty();
}

static bool ReadEntireFile(const std::wstring &Path, std::string &Out)
{
	FILE *pFile = nullptr;
	if(_wfopen_s(&pFile, Path.c_str(), L"rb") != 0 || !pFile)
		return false;
	if(fseek(pFile, 0, SEEK_END) != 0)
	{
		fclose(pFile);
		return false;
	}
	const long Sz = ftell(pFile);
	if(Sz < 0)
	{
		fclose(pFile);
		return false;
	}
	fseek(pFile, 0, SEEK_SET);
	Out.assign((size_t)Sz, '\0');
	if(Sz > 0)
		fread(Out.data(), 1, (size_t)Sz, pFile);
	fclose(pFile);
	return true;
}

static bool WriteTextFile(const std::wstring &Path, const std::string &Text)
{
	FILE *pFile = nullptr;
	if(_wfopen_s(&pFile, Path.c_str(), L"wb") != 0 || !pFile)
		return false;
	fwrite(Text.c_str(), 1, Text.size(), pFile);
	fputc('\n', pFile);
	fclose(pFile);
	return true;
}

static std::wstring GetLauncherSettingsPath()
{
	wchar_t aAppData[MAX_PATH] = {};
	if(FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, aAppData)))
		return {};
	std::wstring Dir = aAppData;
	Dir += L"\\UClient";
	CreateDirectoryW(Dir.c_str(), nullptr);
	return Dir + L"\\" + kSettingsFile;
}

static void LoadDiscordRpcSetting();
static void SaveDiscordRpcSetting(bool Enabled);
static void SaveLauncherSettings(const std::wstring &InstallDir);

static void LoadLauncherSettings(const std::wstring &InstallDir)
{
	g_AutoLaunch = false;
	std::string Text;
	const std::wstring AppPath = GetLauncherSettingsPath();
	const bool HasLauncherCfg = !AppPath.empty() && ReadTextFile(AppPath, Text);
	if(HasLauncherCfg)
	{
		if(Text.find("auto_launch=1") != std::string::npos)
			g_AutoLaunch = true;
		if(Text.find("auto_update=1") != std::string::npos)
			g_AutoUpdate = true;
		else if(Text.find("auto_update=0") != std::string::npos)
			g_AutoUpdate = false;
	}
	else
	{
		const std::wstring LegacyPath = JoinPath(InstallDir, kSettingsFile);
		if(ReadTextFile(LegacyPath, Text) && Text.find("auto_launch=1") != std::string::npos)
			g_AutoLaunch = true;
	}

	LoadDiscordRpcSetting();

	if(!AppPath.empty() && (!HasLauncherCfg || Text.find("discord_rpc=") == std::string::npos || Text.find("auto_update=") == std::string::npos))
		SaveLauncherSettings(InstallDir);
}

static void SaveLauncherSettings(const std::wstring &InstallDir)
{
	(void)InstallDir;
	const std::wstring Path = GetLauncherSettingsPath();
	if(Path.empty())
		return;
	std::string Text;
	Text = g_AutoLaunch ? "auto_launch=1\n" : "auto_launch=0\n";
	Text += g_AutoUpdate ? "auto_update=1\n" : "auto_update=0\n";
	Text += g_DiscordRpc ? "discord_rpc=1\n" : "discord_rpc=0\n";
	WriteTextFile(Path, Text);
}

// Prefer on-disk stamp so updates stop looping even if UClient.exe in the zip
// was missing/outdated. Commit any pending stamp left by a finished apply.
static std::string ResolveLocalVersion(const std::wstring &InstallDir)
{
	const std::wstring VersionPath = JoinPath(InstallDir, kVersionFile);
	const std::wstring PendingPath = JoinPath(InstallDir, kPendingVersionFile);
	const std::wstring ArchivePath = JoinPath(InstallDir, kArchiveRel);

	std::string Pending;
	if(ReadTextFile(PendingPath, Pending))
	{
		// Updater deletes the zip after a successful apply. If the archive is gone,
		// treat pending as committed even when an older updater didn't write the stamp.
		if(GetFileAttributesW(ArchivePath.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			WriteTextFile(VersionPath, Pending);
			DeleteFileW(PendingPath.c_str());
		}
	}

	std::string OnDisk;
	if(ReadTextFile(VersionPath, OnDisk))
	{
		if(CompareVersions(OnDisk, UCLIENT_LAUNCHER_VERSION) >= 0)
			return OnDisk;
	}
	return UCLIENT_LAUNCHER_VERSION;
}

static void CommitPendingVersion(const std::wstring &InstallDir)
{
	const std::wstring PendingPath = JoinPath(InstallDir, kPendingVersionFile);
	const std::wstring VersionPath = JoinPath(InstallDir, kVersionFile);
	if(GetFileAttributesW(PendingPath.c_str()) == INVALID_FILE_ATTRIBUTES)
		return;
	DeleteFileW(VersionPath.c_str());
	MoveFileExW(PendingPath.c_str(), VersionPath.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// ─── In-process update apply (merged from bestclient-updater) ─────────────────

static int RunProcess(const wchar_t *pCmd, std::function<void(const wchar_t *)> LineCb = nullptr)
{
	HANDLE hRead = NULL, hWrite = NULL;
	if(LineCb)
	{
		SECURITY_ATTRIBUTES Sa = {sizeof(Sa), NULL, TRUE};
		if(!CreatePipe(&hRead, &hWrite, &Sa, 0))
			return -1;
		SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
	}

	STARTUPINFOW Si = {};
	Si.cb = sizeof(Si);
	if(LineCb)
	{
		Si.dwFlags = STARTF_USESTDHANDLES;
		Si.hStdOutput = hWrite;
		Si.hStdError = hWrite;
	}

	PROCESS_INFORMATION Pi = {};
	std::wstring Cmd(pCmd);
	BOOL Ok = CreateProcessW(NULL, Cmd.data(), NULL, NULL,
		LineCb ? TRUE : FALSE, CREATE_NO_WINDOW, NULL, NULL, &Si, &Pi);

	if(hWrite)
		CloseHandle(hWrite);
	if(!Ok)
	{
		if(hRead)
			CloseHandle(hRead);
		return -1;
	}

	if(LineCb && hRead)
	{
		std::wstring Line;
		char Buf[512];
		DWORD Read;
		while(ReadFile(hRead, Buf, sizeof(Buf) - 1, &Read, NULL) && Read > 0)
		{
			Buf[Read] = '\0';
			for(DWORD i = 0; i < Read; ++i)
			{
				char Ch = Buf[i];
				if(Ch == '\n')
				{
					LineCb(Line.c_str());
					Line.clear();
				}
				else if(Ch != '\r')
					Line.push_back((wchar_t)(unsigned char)Ch);
			}
		}
		if(!Line.empty())
			LineCb(Line.c_str());
		CloseHandle(hRead);
	}

	WaitForSingleObject(Pi.hProcess, INFINITE);
	DWORD ExitCode = (DWORD)-1;
	GetExitCodeProcess(Pi.hProcess, &ExitCode);
	CloseHandle(Pi.hProcess);
	CloseHandle(Pi.hThread);
	return (int)ExitCode;
}

static int CountArchiveEntries(const wchar_t *pArchive)
{
	wchar_t Cmd[1024];
	_snwprintf_s(Cmd, _TRUNCATE, L"tar.exe -tf \"%ls\"", pArchive);
	int N = 0;
	RunProcess(Cmd, [&](const wchar_t *) { ++N; });
	return N > 0 ? N : 1;
}

static int CountFiles(const wchar_t *pDir)
{
	std::wstring Search(pDir);
	Search += L"\\*";
	WIN32_FIND_DATAW Fd;
	HANDLE h = FindFirstFileW(Search.c_str(), &Fd);
	if(h == INVALID_HANDLE_VALUE)
		return 0;
	int N = 0;
	do
	{
		if(!wcscmp(Fd.cFileName, L".") || !wcscmp(Fd.cFileName, L".."))
			continue;
		if(Fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			std::wstring Sub(pDir);
			Sub += L"\\";
			Sub += Fd.cFileName;
			N += CountFiles(Sub.c_str());
		}
		else
			++N;
	} while(FindNextFileW(h, &Fd));
	FindClose(h);
	return N > 0 ? N : 1;
}

static void DeleteTree(const wchar_t *pPath)
{
	std::wstring Search(pPath);
	Search += L"\\*";
	WIN32_FIND_DATAW Fd;
	HANDLE h = FindFirstFileW(Search.c_str(), &Fd);
	if(h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if(!wcscmp(Fd.cFileName, L".") || !wcscmp(Fd.cFileName, L".."))
				continue;
			std::wstring Full(pPath);
			Full += L"\\";
			Full += Fd.cFileName;
			if(Fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				DeleteTree(Full.c_str());
			else
				DeleteFileW(Full.c_str());
		} while(FindNextFileW(h, &Fd));
		FindClose(h);
	}
	RemoveDirectoryW(pPath);
}

static bool PathsEqualNoCase(const std::wstring &A, const std::wstring &B)
{
	return _wcsicmp(A.c_str(), B.c_str()) == 0;
}

static void CopyTree(const wchar_t *pSrc, const wchar_t *pDst, const std::wstring &SelfPath, std::function<void()> PerFile = nullptr)
{
	CreateDirectoryW(pDst, NULL);
	std::wstring Search(pSrc);
	Search += L"\\*";
	WIN32_FIND_DATAW Fd;
	HANDLE h = FindFirstFileW(Search.c_str(), &Fd);
	if(h == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if(!wcscmp(Fd.cFileName, L".") || !wcscmp(Fd.cFileName, L".."))
			continue;
		std::wstring Src(pSrc);
		Src += L"\\";
		Src += Fd.cFileName;
		std::wstring Dst(pDst);
		Dst += L"\\";
		Dst += Fd.cFileName;
		if(Fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			CopyTree(Src.c_str(), Dst.c_str(), SelfPath, PerFile);
		else
		{
			// Running launcher cannot overwrite itself — rename then replace.
			if(PathsEqualNoCase(Dst, SelfPath))
			{
				const std::wstring Old = SelfPath + L".old";
				DeleteFileW(Old.c_str());
				MoveFileExW(SelfPath.c_str(), Old.c_str(), MOVEFILE_REPLACE_EXISTING);
			}
			CopyFileW(Src.c_str(), Dst.c_str(), FALSE);
			if(PerFile)
				PerFile();
		}
	} while(FindNextFileW(h, &Fd));
	FindClose(h);
}

static bool ApplyUpdateArchive(const std::wstring &ArchivePath, const std::wstring &InstallDir, const std::wstring &SelfPath)
{
	g_UpdateStage = EUpdateStage::Apply;
	g_DownloadSpeed = 0;
	g_EtaSeconds = -1;
	SetButtonLabel(L"Applying update");
	SetStatus(L"Extracting update...");
	SetPercent(10);

	const std::wstring ExtractDir = JoinPath(InstallDir, L"update\\extract");
	DeleteTree(ExtractDir.c_str());
	CreateDirectoryW(JoinPath(InstallDir, L"update").c_str(), nullptr);
	if(!CreateDirectoryW(ExtractDir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
	{
		SetStatus(L"Failed to create extraction directory");
		g_Failed = true;
		return false;
	}

	{
		const int Total = CountArchiveEntries(ArchivePath.c_str());
		int Done = 0;
		wchar_t Cmd[1024];
		_snwprintf_s(Cmd, _TRUNCATE, L"tar.exe -xvf \"%ls\" -C \"%ls\"", ArchivePath.c_str(), ExtractDir.c_str());
		const int ExitCode = RunProcess(Cmd, [&](const wchar_t *) {
			++Done;
			const int Pct = 10 + Done * 40 / Total;
			SetPercent(Pct < 50 ? Pct : 50);
		});
		if(ExitCode != 0)
		{
			SetStatus(L"Extraction failed");
			g_Failed = true;
			return false;
		}
	}
	SetPercent(50);

	std::wstring CopyRoot = ExtractDir;
	{
		std::wstring Search = ExtractDir + L"\\*";
		WIN32_FIND_DATAW Fd;
		HANDLE h = FindFirstFileW(Search.c_str(), &Fd);
		if(h != INVALID_HANDLE_VALUE)
		{
			int N = 0;
			wchar_t aFirst[MAX_PATH] = L"";
			do
			{
				if(!wcscmp(Fd.cFileName, L".") || !wcscmp(Fd.cFileName, L".."))
					continue;
				++N;
				if(N == 1)
					wcscpy_s(aFirst, Fd.cFileName);
			} while(FindNextFileW(h, &Fd));
			FindClose(h);
			if(N == 1)
			{
				std::wstring Sub = ExtractDir + L"\\" + aFirst;
				if(GetFileAttributesW(Sub.c_str()) & FILE_ATTRIBUTE_DIRECTORY)
					CopyRoot = Sub;
			}
		}
	}

	SetStatus(L"Backing up settings...");
	wchar_t aBackup[MAX_PATH];
	_snwprintf_s(aBackup, _TRUNCATE, L"%ls\\update\\backup_%lu", InstallDir.c_str(), GetCurrentProcessId());
	DeleteTree(aBackup);
	const std::wstring LegacyCfg = JoinPath(InstallDir, kSettingsFile);
	const std::wstring BackupCfg = JoinPath(aBackup, kSettingsFile);
	if(GetFileAttributesW(LegacyCfg.c_str()) != INVALID_FILE_ATTRIBUTES)
		CopyFileW(LegacyCfg.c_str(), BackupCfg.c_str(), FALSE);
	for(const wchar_t *pRel : k_aUserDirs)
	{
		wchar_t aSrc[MAX_PATH], aDst[MAX_PATH];
		_snwprintf_s(aSrc, _TRUNCATE, L"%ls\\%ls", InstallDir.c_str(), pRel);
		_snwprintf_s(aDst, _TRUNCATE, L"%ls\\%ls", aBackup, pRel);
		if(GetFileAttributesW(aSrc) != INVALID_FILE_ATTRIBUTES)
			CopyTree(aSrc, aDst, SelfPath);
	}
	SetPercent(55);

	SetStatus(L"Installing files...");
	{
		const int Total = CountFiles(CopyRoot.c_str());
		int Done = 0;
		CopyTree(CopyRoot.c_str(), InstallDir.c_str(), SelfPath, [&]() {
			++Done;
			const int Pct = 55 + Done * 35 / Total;
			SetPercent(Pct < 90 ? Pct : 90);
		});
	}
	SetPercent(90);

	SetStatus(L"Restoring settings...");
	for(const wchar_t *pRel : k_aUserDirs)
	{
		wchar_t aSrc[MAX_PATH], aDst[MAX_PATH];
		_snwprintf_s(aSrc, _TRUNCATE, L"%ls\\%ls", aBackup, pRel);
		_snwprintf_s(aDst, _TRUNCATE, L"%ls\\%ls", InstallDir.c_str(), pRel);
		if(GetFileAttributesW(aSrc) != INVALID_FILE_ATTRIBUTES)
			CopyTree(aSrc, aDst, SelfPath);
	}
	const std::wstring AppCfg = GetLauncherSettingsPath();
	if(!AppCfg.empty() && GetFileAttributesW(AppCfg.c_str()) == INVALID_FILE_ATTRIBUTES &&
		GetFileAttributesW(BackupCfg.c_str()) != INVALID_FILE_ATTRIBUTES)
	{
		std::string CfgText;
		if(ReadTextFile(BackupCfg, CfgText))
			WriteTextFile(AppCfg, CfgText);
	}
	SetPercent(95);

	SetStatus(L"Cleaning up...");
	DeleteFileW(ArchivePath.c_str());
	DeleteTree(ExtractDir.c_str());
	DeleteTree(aBackup);
	CommitPendingVersion(InstallDir);
	SetPercent(100);
	return true;
}

static bool ExtractJsonString(const std::string &Json, const char *Key, std::string &Out)
{
	std::string Needle = "\"";
	Needle += Key;
	Needle += "\"";
	size_t Pos = 0;
	while(true)
	{
		Pos = Json.find(Needle, Pos);
		if(Pos == std::string::npos)
			return false;
		size_t Colon = Json.find(':', Pos + Needle.size());
		if(Colon == std::string::npos)
			return false;
		size_t Q1 = Json.find('"', Colon + 1);
		if(Q1 == std::string::npos)
			return false;
		size_t Q2 = Q1 + 1;
		while(Q2 < Json.size())
		{
			if(Json[Q2] == '"' && Json[Q2 - 1] != '\\')
				break;
			++Q2;
		}
		if(Q2 >= Json.size())
			return false;
		Out = Json.substr(Q1 + 1, Q2 - Q1 - 1);
		return true;
	}
}

static bool ExtractWindowsUrl(const std::string &Json, std::string &Version, std::string &Url)
{
	if(!ExtractJsonString(Json, "version", Version))
	{
		if(!ExtractJsonString(Json, "tag_name", Version))
			ExtractJsonString(Json, "name", Version);
	}
	if(Version.empty())
		return false;

	const size_t Platforms = Json.find("\"platforms\"");
	if(Platforms == std::string::npos)
		return false;
	const size_t Windows = Json.find("\"windows\"", Platforms);
	if(Windows == std::string::npos)
		return false;
	const size_t ObjEnd = Json.find('}', Windows);
	if(ObjEnd == std::string::npos)
		return false;
	const std::string Slice = Json.substr(Windows, ObjEnd - Windows + 1);
	if(!ExtractJsonString(Slice, "url", Url))
	{
		if(!ExtractJsonString(Slice, "download_url", Url))
			ExtractJsonString(Slice, "browser_download_url", Url);
	}
	return !Url.empty();
}

static bool ExtractJsonBool(const std::string &Json, const char *Key, bool &Out)
{
	std::string Needle = "\"";
	Needle += Key;
	Needle += "\"";
	const size_t Pos = Json.find(Needle);
	if(Pos == std::string::npos)
		return false;
	const size_t Colon = Json.find(':', Pos + Needle.size());
	if(Colon == std::string::npos)
		return false;
	const size_t TruePos = Json.find("true", Colon + 1);
	const size_t FalsePos = Json.find("false", Colon + 1);
	if(TruePos != std::string::npos && (FalsePos == std::string::npos || TruePos < FalsePos))
	{
		Out = true;
		return true;
	}
	if(FalsePos != std::string::npos)
	{
		Out = false;
		return true;
	}
	return false;
}

static bool ExtractJsonExpiresAt(const std::string &Json, const char *Key, bool &OutKnown, bool &OutPermanent, int64_t &OutUnix)
{
	std::string Needle = "\"";
	Needle += Key;
	Needle += "\"";
	const size_t Pos = Json.find(Needle);
	if(Pos == std::string::npos)
		return false;
	const size_t Colon = Json.find(':', Pos + Needle.size());
	if(Colon == std::string::npos)
		return false;
	size_t i = Colon + 1;
	while(i < Json.size() && isspace((unsigned char)Json[i]))
		++i;
	if(i + 4 <= Json.size() && Json.compare(i, 4, "null") == 0)
	{
		OutKnown = true;
		OutPermanent = true;
		OutUnix = 0;
		return true;
	}
	char *pEnd = nullptr;
	const long long Val = strtoll(Json.c_str() + i, &pEnd, 10);
	if(pEnd == Json.c_str() + i)
		return false;
	OutKnown = true;
	OutPermanent = false;
	OutUnix = (int64_t)Val;
	return true;
}

static bool ParseNoticeObject(const std::string &Json, NoticeView &Out)
{
	NoticeView Notice;
	if(!ExtractJsonString(Json, "id", Notice.Id))
		return false;
	if(!ExtractJsonString(Json, "title", Notice.Title))
		return false;
	if(!ExtractJsonString(Json, "body", Notice.Body))
		return false;
	if(!ExtractJsonString(Json, "severity", Notice.Severity))
		Notice.Severity = "warning";
	bool BlocksPlay = false;
	ExtractJsonBool(Json, "blocks_play", BlocksPlay);
	if(!BlocksPlay)
		ExtractJsonBool(Json, "blocksPlay", BlocksPlay);
	Notice.BlocksPlay = BlocksPlay;
	Out = std::move(Notice);
	return true;
}

static void ParseLauncherNotices(const std::string &Json, std::vector<NoticeView> &Out)
{
	Out.clear();
	const size_t Notices = Json.find("\"notices\"");
	if(Notices == std::string::npos)
		return;
	const size_t ArrayStart = Json.find('[', Notices);
	if(ArrayStart == std::string::npos)
		return;
	size_t Pos = ArrayStart + 1;
	while(Pos < Json.size())
	{
		while(Pos < Json.size() && isspace((unsigned char)Json[Pos]))
			++Pos;
		if(Pos >= Json.size() || Json[Pos] == ']')
			break;
		if(Json[Pos] != '{')
		{
			++Pos;
			continue;
		}
		int Depth = 0;
		const size_t ObjStart = Pos;
		for(; Pos < Json.size(); ++Pos)
		{
			if(Json[Pos] == '{')
				++Depth;
			else if(Json[Pos] == '}')
			{
				--Depth;
				if(Depth == 0)
				{
					++Pos;
					break;
				}
			}
		}
		NoticeView Notice;
		if(ParseNoticeObject(Json.substr(ObjStart, Pos - ObjStart), Notice))
			Out.push_back(std::move(Notice));
		while(Pos < Json.size() && Json[Pos] != '{' && Json[Pos] != ']')
			++Pos;
	}
}

static std::wstring GetUclientAccountPath()
{
	wchar_t aAppData[MAX_PATH] = {};
	if(FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, aAppData)))
		return {};
	return std::wstring(aAppData) + L"\\DDNet\\uclient_account.json";
}

static bool LoadAccountCredentials(std::string &InstallId, std::string &Secret)
{
	InstallId.clear();
	Secret.clear();
	const std::wstring Path = GetUclientAccountPath();
	if(Path.empty())
		return false;
	std::string Text;
	if(!ReadTextFile(Path, Text))
		return false;
	if(!ExtractJsonString(Text, "install_uuid", InstallId))
		ExtractJsonString(Text, "install_id", InstallId);
	ExtractJsonString(Text, "secret", Secret);
	return !InstallId.empty() && !Secret.empty();
}

static void UpsertBanNotice(std::vector<NoticeView> &Notices, const std::string &Reason, bool HasExpiry, bool Permanent, int64_t ExpiresAt)
{
	for(NoticeView &N : Notices)
	{
		if(N.Id == "account_ban")
		{
			if(!Reason.empty())
				N.Body = Reason;
			N.HasExpiresAt = HasExpiry;
			N.BanPermanent = Permanent;
			N.ExpiresAt = ExpiresAt;
			return;
		}
	}
	NoticeView Ban;
	Ban.Id = "account_ban";
	Ban.Title = "Account Suspended";
	Ban.Body = Reason.empty() ? "Your UClient account has been blocked by an administrator." : Reason;
	Ban.Severity = "critical";
	Ban.BlocksPlay = true;
	Ban.HasExpiresAt = HasExpiry;
	Ban.BanPermanent = Permanent;
	Ban.ExpiresAt = ExpiresAt;
	Notices.insert(Notices.begin(), std::move(Ban));
}

static void RecomputePlayBlocked(const std::vector<NoticeView> &Notices)
{
	g_PlayBlocked = false;
	for(const NoticeView &N : Notices)
	{
		if(N.BlocksPlay)
		{
			g_PlayBlocked = true;
			break;
		}
	}
}

static bool HttpJsonRequest(const wchar_t *pMethod, const std::wstring &Url, const std::string &BodyIn, std::string &BodyOut, int &StatusOut);
static bool HttpGetToString(const std::wstring &Url, std::string &OutBody);

static void RefreshLauncherNotices()
{
	std::vector<NoticeView> Notices;
	const std::wstring NoticesUrl = Utf8ToWide((std::string(UCLIENT_API_BASE_URL) + "/launcher/notices").c_str());
	std::string Body;
	if(HttpGetToString(NoticesUrl, Body))
		ParseLauncherNotices(Body, Notices);

	std::string InstallId;
	std::string Secret;
	if(LoadAccountCredentials(InstallId, Secret))
	{
		char aPayload[1024];
		_snprintf_s(aPayload, _TRUNCATE,
			"{\"install_id\":\"%s\",\"secret\":\"%s\",\"version\":\"%s\"}",
			InstallId.c_str(), Secret.c_str(), UCLIENT_LAUNCHER_VERSION);
		std::string VerifyBody;
		int Status = 0;
		const std::wstring VerifyUrl = Utf8ToWide((std::string(UCLIENT_API_BASE_URL) + "/account/verify").c_str());
		if(HttpJsonRequest(L"POST", VerifyUrl, aPayload, VerifyBody, Status) && Status == 423)
		{
			std::string Reason;
			bool HasExpiry = false;
			bool Permanent = false;
			int64_t ExpiresAt = 0;
			ExtractJsonString(VerifyBody, "reason", Reason);
			if(ExtractJsonExpiresAt(VerifyBody, "expires_at", HasExpiry, Permanent, ExpiresAt))
				UpsertBanNotice(Notices, Reason, HasExpiry, Permanent, ExpiresAt);
			else
				UpsertBanNotice(Notices, Reason, false, false, 0);
		}
	}

	RecomputePlayBlocked(Notices);
	EnterCriticalSection(&g_Lock);
	g_Notices = std::move(Notices);
	LeaveCriticalSection(&g_Lock);
}

static DWORD WINAPI NoticesThread(LPVOID)
{
	RefreshLauncherNotices();
	EnterCriticalSection(&g_Lock);
	g_NoticesRefreshing = false;
	LeaveCriticalSection(&g_Lock);
	if(g_hWnd)
		PostMessage(g_hWnd, WM_NOTICES_READY, 0, 0);
	return 0;
}

static void RequestNoticesRefresh()
{
	EnterCriticalSection(&g_Lock);
	if(g_NoticesRefreshing)
	{
		LeaveCriticalSection(&g_Lock);
		return;
	}
	g_NoticesRefreshing = true;
	LeaveCriticalSection(&g_Lock);
	HANDLE hThread = CreateThread(nullptr, 0, NoticesThread, nullptr, 0, nullptr);
	if(hThread)
		CloseHandle(hThread);
	else
	{
		EnterCriticalSection(&g_Lock);
		g_NoticesRefreshing = false;
		LeaveCriticalSection(&g_Lock);
	}
}

// ─── Friends (settings_ddnet.cfg + master servers.json) ───────────────────────

static std::wstring GetDdnetSettingsPath()
{
	wchar_t aAppData[MAX_PATH] = {};
	if(FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, aAppData)))
		return {};
	return std::wstring(aAppData) + L"\\DDNet\\settings_ddnet.cfg";
}

static std::wstring GetTclientSettingsPath()
{
	wchar_t aAppData[MAX_PATH] = {};
	if(FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, aAppData)))
		return {};
	return std::wstring(aAppData) + L"\\DDNet\\settings_tclient.cfg";
}

static bool ParseConfigIntLine(const std::string &Line, const char *pKey, int &Out)
{
	size_t i = 0;
	while(i < Line.size() && isspace((unsigned char)Line[i]))
		++i;
	if(i >= Line.size() || Line[i] == '#')
		return false;
	const size_t KeyLen = strlen(pKey);
	if(Line.compare(i, KeyLen, pKey) != 0)
		return false;
	i += KeyLen;
	if(i < Line.size() && !isspace((unsigned char)Line[i]))
		return false;
	while(i < Line.size() && isspace((unsigned char)Line[i]))
		++i;
	if(i >= Line.size())
		return false;
	char *pEnd = nullptr;
	const long Val = strtol(Line.c_str() + i, &pEnd, 10);
	if(pEnd == Line.c_str() + i)
		return false;
	Out = (int)Val;
	return true;
}

static int ReadIntConfigValue(const std::wstring &Path, const char *pKey, int Default)
{
	std::string Text;
	if(Path.empty() || !ReadEntireFile(Path, Text))
		return Default;

	size_t LineStart = 0;
	while(LineStart <= Text.size())
	{
		size_t LineEnd = Text.find('\n', LineStart);
		if(LineEnd == std::string::npos)
			LineEnd = Text.size();
		std::string Line = Text.substr(LineStart, LineEnd - LineStart);
		if(!Line.empty() && Line.back() == '\r')
			Line.pop_back();
		LineStart = LineEnd + 1;

		int Value = Default;
		if(ParseConfigIntLine(Line, pKey, Value))
			return Value;
	}
	return Default;
}

static void LoadDiscordRpcSetting()
{
	g_DiscordRpc = true;
	const std::wstring LauncherPath = GetLauncherSettingsPath();
	std::string Text;
	if(!LauncherPath.empty() && ReadTextFile(LauncherPath, Text))
	{
		if(Text.find("discord_rpc=0") != std::string::npos)
			g_DiscordRpc = false;
		else if(Text.find("discord_rpc=1") != std::string::npos)
			g_DiscordRpc = true;
		else
			g_DiscordRpc = ReadIntConfigValue(GetTclientSettingsPath(), "tc_discord_rpc", 1) != 0;
		return;
	}
	g_DiscordRpc = ReadIntConfigValue(GetTclientSettingsPath(), "tc_discord_rpc", 1) != 0;
}

static void SaveDiscordRpcSetting(bool Enabled)
{
	g_DiscordRpc = Enabled;
	SaveLauncherSettings(g_InstallDir);
}

static bool ParseQuotedToken(const std::string &Line, size_t &Pos, std::string &Out)
{
	while(Pos < Line.size() && isspace((unsigned char)Line[Pos]))
		++Pos;
	if(Pos >= Line.size() || Line[Pos] != '"')
		return false;
	++Pos;
	Out.clear();
	while(Pos < Line.size())
	{
		const char Ch = Line[Pos++];
		if(Ch == '\\' && Pos < Line.size())
		{
			Out.push_back(Line[Pos++]);
			continue;
		}
		if(Ch == '"')
			return true;
		Out.push_back(Ch);
	}
	return false;
}

static std::vector<FriendView> LoadFriendsFromSettings()
{
	std::vector<FriendView> Friends;
	const std::wstring Path = GetDdnetSettingsPath();
	if(Path.empty())
		return Friends;
	std::string Text;
	if(!ReadEntireFile(Path, Text))
		return Friends;

	size_t LineStart = 0;
	while(LineStart <= Text.size())
	{
		size_t LineEnd = Text.find('\n', LineStart);
		if(LineEnd == std::string::npos)
			LineEnd = Text.size();
		std::string Line = Text.substr(LineStart, LineEnd - LineStart);
		if(!Line.empty() && Line.back() == '\r')
			Line.pop_back();
		LineStart = LineEnd + 1;

		size_t i = 0;
		while(i < Line.size() && isspace((unsigned char)Line[i]))
			++i;
		if(Line.compare(i, 10, "add_friend") != 0)
			continue;
		i += 10;
		std::string Name, Clan;
		if(!ParseQuotedToken(Line, i, Name) || !ParseQuotedToken(Line, i, Clan))
			continue;
		if(Name.empty())
			continue; // clan-only entries: skip for join list
		FriendView F;
		F.Name = Name;
		F.Clan = Clan;
		Friends.push_back(std::move(F));
	}
	return Friends;
}

static std::string StripTwAddress(const std::string &Url)
{
	static const char *Prefixes[] = {
		"tw-0.6+udp://", "tw-0.7+udp://", "tw-0.6+tcp://", "tw-0.7+tcp://", "ddnet://", "ddnet:"};
	std::string Out = Url;
	for(const char *pPrefix : Prefixes)
	{
		const size_t Len = strlen(pPrefix);
		if(Out.size() >= Len && _strnicmp(Out.c_str(), pPrefix, (unsigned)Len) == 0)
		{
			Out = Out.substr(Len);
			break;
		}
	}
	while(!Out.empty() && (Out.back() == '/' || Out.back() == ' '))
		Out.pop_back();
	return Out;
}

static bool ExtractJsonStringAt(const std::string &Json, size_t KeyPos, std::string &Out)
{
	size_t Colon = Json.find(':', KeyPos);
	if(Colon == std::string::npos || Colon > KeyPos + 64)
		return false;
	size_t Q1 = Json.find('"', Colon + 1);
	if(Q1 == std::string::npos)
		return false;
	size_t Q2 = Q1 + 1;
	while(Q2 < Json.size())
	{
		if(Json[Q2] == '"' && Json[Q2 - 1] != '\\')
			break;
		++Q2;
	}
	if(Q2 >= Json.size())
		return false;
	Out = Json.substr(Q1 + 1, Q2 - Q1 - 1);
	return true;
}

static size_t FindMatchingBracket(const std::string &Json, size_t OpenPos, char Open, char Close)
{
	int Depth = 0;
	bool InStr = false;
	for(size_t i = OpenPos; i < Json.size(); ++i)
	{
		const char Ch = Json[i];
		if(InStr)
		{
			if(Ch == '\\' && i + 1 < Json.size())
			{
				++i;
				continue;
			}
			if(Ch == '"')
				InStr = false;
			continue;
		}
		if(Ch == '"')
		{
			InStr = true;
			continue;
		}
		if(Ch == Open)
			++Depth;
		else if(Ch == Close)
		{
			--Depth;
			if(Depth == 0)
				return i;
		}
	}
	return std::string::npos;
}

struct PlayerLoc
{
	std::string Address;
	std::string ServerName;
	std::string MapName;
};

static std::string FriendLookupKey(std::string Name)
{
	for(char &Ch : Name)
		Ch = (char)tolower((unsigned char)Ch);
	return Name;
}

static bool IsClientNameEntry(const std::string &Json, size_t CNameKey)
{
	const size_t CountryKey = Json.find("\"country\"", CNameKey);
	if(CountryKey != std::string::npos && CountryKey < CNameKey + 220)
		return true;
	const size_t ScoreKey = Json.find("\"score\"", CNameKey);
	if(ScoreKey != std::string::npos && ScoreKey < CNameKey + 220)
		return true;
	const size_t IsPlayerKey = Json.find("\"is_player\"", CNameKey);
	if(IsPlayerKey != std::string::npos && IsPlayerKey < CNameKey + 220)
		return true;
	const size_t ClanKey = Json.find("\"clan\"", CNameKey);
	return ClanKey != std::string::npos && ClanKey < CNameKey + 220;
}

static void MatchFriendsOnline(std::vector<FriendView> &Friends, const std::string &Json)
{
	std::unordered_map<std::string, PlayerLoc> ByName;
	size_t Pos = 0;
	while(true)
	{
		const size_t AddrKey = Json.find("\"addresses\"", Pos);
		if(AddrKey == std::string::npos)
			break;
		const size_t AddrArr = Json.find('[', AddrKey);
		if(AddrArr == std::string::npos)
			break;
		const size_t AddrArrEnd = FindMatchingBracket(Json, AddrArr, '[', ']');
		if(AddrArrEnd == std::string::npos)
			break;

		std::string Address;
		{
			size_t Q1 = Json.find('"', AddrArr);
			if(Q1 != std::string::npos && Q1 < AddrArrEnd)
			{
				size_t Q2 = Q1 + 1;
				while(Q2 < AddrArrEnd && !(Json[Q2] == '"' && Json[Q2 - 1] != '\\'))
					++Q2;
				if(Q2 < AddrArrEnd)
					Address = StripTwAddress(Json.substr(Q1 + 1, Q2 - Q1 - 1));
			}
		}
		if(Address.empty())
		{
			Pos = AddrArrEnd + 1;
			continue;
		}

		const size_t InfoKey = Json.find("\"info\"", AddrArrEnd);
		if(InfoKey == std::string::npos)
			break;
		const size_t InfoObj = Json.find('{', InfoKey);
		if(InfoObj == std::string::npos)
			break;
		const size_t InfoEnd = FindMatchingBracket(Json, InfoObj, '{', '}');
		if(InfoEnd == std::string::npos)
			break;

		std::string ServerName, MapName;
		const size_t MapKey = Json.find("\"map\"", InfoObj);
		if(MapKey != std::string::npos && MapKey < InfoEnd)
		{
			const size_t MapNameKey = Json.find("\"name\"", MapKey);
			if(MapNameKey != std::string::npos && MapNameKey < InfoEnd)
				ExtractJsonStringAt(Json, MapNameKey, MapName);
		}
		// Server title: first "name" in info that is not the map name key.
		size_t NameKey = Json.find("\"name\"", InfoObj);
		while(NameKey != std::string::npos && NameKey < InfoEnd)
		{
			if(MapKey == std::string::npos || NameKey < MapKey || NameKey > MapKey + 40)
			{
				ExtractJsonStringAt(Json, NameKey, ServerName);
				break;
			}
			NameKey = Json.find("\"name\"", NameKey + 5);
		}

		const size_t ClientsKey = Json.find("\"clients\"", InfoObj);
		if(ClientsKey != std::string::npos && ClientsKey < InfoEnd)
		{
			const size_t ClientsArr = Json.find('[', ClientsKey);
			if(ClientsArr != std::string::npos && ClientsArr < InfoEnd)
			{
				const size_t ClientsEnd = FindMatchingBracket(Json, ClientsArr, '[', ']');
				if(ClientsEnd != std::string::npos)
				{
					size_t CPos = ClientsArr;
					while(CPos < ClientsEnd)
					{
						const size_t CNameKey = Json.find("\"name\"", CPos);
						if(CNameKey == std::string::npos || CNameKey >= ClientsEnd)
							break;
						std::string PlayerName;
						if(ExtractJsonStringAt(Json, CNameKey, PlayerName) && !PlayerName.empty() &&
							IsClientNameEntry(Json, CNameKey))
						{
							PlayerLoc Loc;
							Loc.Address = Address;
							Loc.ServerName = ServerName;
							Loc.MapName = MapName;
							ByName.emplace(FriendLookupKey(PlayerName), std::move(Loc));
						}
						CPos = CNameKey + 5;
					}
				}
			}
		}

		Pos = InfoEnd + 1;
	}

	for(FriendView &F : Friends)
	{
		const auto It = ByName.find(FriendLookupKey(F.Name));
		if(It == ByName.end())
			continue;
		F.Online = true;
		F.Address = It->second.Address;
		F.ServerName = It->second.ServerName;
		F.MapName = It->second.MapName;
	}

	std::stable_sort(Friends.begin(), Friends.end(), [](const FriendView &A, const FriendView &B) {
		if(A.Online != B.Online)
			return A.Online > B.Online;
		return _stricmp(A.Name.c_str(), B.Name.c_str()) < 0;
	});
}

static DWORD WINAPI FriendsThread(LPVOID)
{
	std::vector<FriendView> Friends = LoadFriendsFromSettings();
	std::string Body;
	static const wchar_t *Urls[] = {
		L"https://master.bestclient.fun/servers.json",
		L"https://master1.ddnet.org/ddnet/15/servers.json",
	};
	bool Got = false;
	for(const wchar_t *pUrl : Urls)
	{
		if(HttpGetToString(pUrl, Body) && Body.find("\"servers\"") != std::string::npos)
		{
			Got = true;
			break;
		}
	}
	if(Got)
		MatchFriendsOnline(Friends, Body);

	EnterCriticalSection(&g_Lock);
	g_Friends = std::move(Friends);
	g_FriendsLoading = false;
	g_FriendsLoaded = true;
	LeaveCriticalSection(&g_Lock);
	if(g_hWnd)
		PostMessage(g_hWnd, WM_FRIENDS_READY, 0, 0);
	return 0;
}

static void RequestFriendsRefresh()
{
	EnterCriticalSection(&g_Lock);
	if(g_FriendsLoading)
	{
		LeaveCriticalSection(&g_Lock);
		return;
	}
	g_FriendsLoading = true;
	LeaveCriticalSection(&g_Lock);
	HANDLE hThread = CreateThread(nullptr, 0, FriendsThread, nullptr, 0, nullptr);
	if(hThread)
		CloseHandle(hThread);
	else
	{
		EnterCriticalSection(&g_Lock);
		g_FriendsLoading = false;
		LeaveCriticalSection(&g_Lock);
	}
}

// ─── HTTP (WinHTTP) ───────────────────────────────────────────────────────────

static bool HttpJsonRequest(const wchar_t *pMethod, const std::wstring &Url, const std::string &BodyIn, std::string &BodyOut, int &StatusOut)
{
	StatusOut = 0;
	BodyOut.clear();

	URL_COMPONENTS Uc = {};
	Uc.dwStructSize = sizeof(Uc);
	wchar_t aHost[256];
	wchar_t aPath[2048];
	Uc.lpszHostName = aHost;
	Uc.dwHostNameLength = 256;
	Uc.lpszUrlPath = aPath;
	Uc.dwUrlPathLength = 2048;
	if(!WinHttpCrackUrl(Url.c_str(), 0, 0, &Uc))
		return false;

	const std::wstring Ua = Utf8ToWide((std::string("UClientLauncher/") + UCLIENT_LAUNCHER_VERSION).c_str());
	HINTERNET hSession = WinHttpOpen(Ua.c_str(),
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if(!hSession)
		return false;

	HINTERNET hConnect = WinHttpConnect(hSession, aHost, Uc.nPort, 0);
	if(!hConnect)
	{
		WinHttpCloseHandle(hSession);
		return false;
	}

	DWORD Flags = (Uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, pMethod, aPath, nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, Flags);
	if(!hRequest)
	{
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	const wchar_t *pHeaders = L"Content-Type: application/json\r\nAccept: application/json";
	BOOL Ok = WinHttpSendRequest(hRequest, pHeaders, (DWORD)-1L,
		BodyIn.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)BodyIn.data(),
		(DWORD)BodyIn.size(), (DWORD)BodyIn.size(), 0);
	if(Ok)
		Ok = WinHttpReceiveResponse(hRequest, nullptr);

	if(Ok)
	{
		DWORD StatusCode = 0;
		DWORD StatusSize = sizeof(StatusCode);
		if(WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			   WINHTTP_HEADER_NAME_BY_INDEX, &StatusCode, &StatusSize, WINHTTP_NO_HEADER_INDEX))
			StatusOut = (int)StatusCode;

		DWORD Avail = 0;
		while(WinHttpQueryDataAvailable(hRequest, &Avail) && Avail > 0)
		{
			std::string Chunk(Avail, '\0');
			DWORD Read = 0;
			if(!WinHttpReadData(hRequest, Chunk.data(), Avail, &Read))
			{
				Ok = FALSE;
				break;
			}
			Chunk.resize(Read);
			BodyOut += Chunk;
		}
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return Ok == TRUE;
}

static bool HttpGetToString(const std::wstring &Url, std::string &OutBody)
{
	int Status = 0;
	if(!HttpJsonRequest(L"GET", Url, std::string(), OutBody, Status))
		return false;
	return Status >= 200 && Status < 300 && !OutBody.empty();
}

static bool HttpDownloadFile(const std::wstring &Url, const std::wstring &DestPath)
{
	URL_COMPONENTS Uc = {};
	Uc.dwStructSize = sizeof(Uc);
	wchar_t aHost[256];
	wchar_t aPath[2048];
	Uc.lpszHostName = aHost;
	Uc.dwHostNameLength = 256;
	Uc.lpszUrlPath = aPath;
	Uc.dwUrlPathLength = 2048;
	if(!WinHttpCrackUrl(Url.c_str(), 0, 0, &Uc))
		return false;

	const std::wstring Ua = Utf8ToWide((std::string("UClientLauncher/") + UCLIENT_LAUNCHER_VERSION).c_str());
	HINTERNET hSession = WinHttpOpen(Ua.c_str(),
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if(!hSession)
		return false;
	HINTERNET hConnect = WinHttpConnect(hSession, aHost, Uc.nPort, 0);
	if(!hConnect)
	{
		WinHttpCloseHandle(hSession);
		return false;
	}
	DWORD Flags = (Uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", aPath, nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, Flags);
	if(!hRequest)
	{
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	BOOL Ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
	if(Ok)
		Ok = WinHttpReceiveResponse(hRequest, nullptr);

	FILE *pFile = nullptr;
	if(Ok)
	{
		CreateDirectoryW(ParentDir(DestPath).c_str(), nullptr);
		if(_wfopen_s(&pFile, DestPath.c_str(), L"wb") != 0 || !pFile)
			Ok = FALSE;
	}

	DWORD Total = 0;
	DWORD ContentLen = 0;
	DWORD LenSize = sizeof(ContentLen);
	WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &ContentLen, &LenSize, WINHTTP_NO_HEADER_INDEX);

	if(Ok)
	{
		g_UpdateStage = EUpdateStage::Download;
		g_DownloadDone = 0;
		g_DownloadTotal = ContentLen;
		g_DownloadSpeed = 0;
		g_EtaSeconds = -1;

		DWORD LastTick = GetTickCount();
		uint64_t LastTotal = 0;
		DWORD Avail = 0;
		while(WinHttpQueryDataAvailable(hRequest, &Avail) && Avail > 0)
		{
			std::vector<char> Buf(Avail);
			DWORD Read = 0;
			if(!WinHttpReadData(hRequest, Buf.data(), Avail, &Read))
			{
				Ok = FALSE;
				break;
			}
			fwrite(Buf.data(), 1, Read, pFile);
			Total += Read;
			g_DownloadDone = Total;
			if(ContentLen > 0)
				SetPercent((int)((Total * 100ull) / ContentLen));
			else
				SetPercent((int)(Total / (256 * 1024)) % 100);

			const DWORD Now = GetTickCount();
			if(Now - LastTick >= 400)
			{
				const uint64_t Delta = Total - LastTotal;
				const uint64_t ElapsedMs = Now - LastTick;
				if(ElapsedMs > 0)
				{
					const uint64_t Speed = (Delta * 1000ull) / ElapsedMs;
					g_DownloadSpeed = Speed;
					if(ContentLen > Total && Speed > 0)
						g_EtaSeconds = (int)((ContentLen - Total) / Speed);
					else
						g_EtaSeconds = 0;
				}
				LastTick = Now;
				LastTotal = Total;
			}
		}
	}

	if(pFile)
		fclose(pFile);
	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	if(!Ok || Total < 4)
	{
		DeleteFileW(DestPath.c_str());
		return false;
	}

	// ZIP magic
	FILE *pCheck = nullptr;
	if(_wfopen_s(&pCheck, DestPath.c_str(), L"rb") == 0 && pCheck)
	{
		unsigned char Mag[2] = {};
		fread(Mag, 1, 2, pCheck);
		fclose(pCheck);
		if(Mag[0] != 'P' || Mag[1] != 'K')
		{
			DeleteFileW(DestPath.c_str());
			return false;
		}
	}
	return true;
}

// ─── Protocol registration ────────────────────────────────────────────────────

static bool RegSetCommand(const wchar_t *ClassPath, const std::wstring &Exe)
{
	HKEY hKey = nullptr;
	if(RegCreateKeyExW(HKEY_CURRENT_USER, ClassPath, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
		return false;
	const std::wstring Value = L"\"" + Exe + L"\" \"%1\"";
	const LSTATUS St = RegSetValueExW(hKey, L"", 0, REG_SZ, (const BYTE *)Value.c_str(), (DWORD)((Value.size() + 1) * sizeof(wchar_t)));
	RegCloseKey(hKey);
	return St == ERROR_SUCCESS;
}

static void RegisterShellHandlers(const std::wstring &LauncherExe)
{
	// ddnet://
	{
		HKEY hProt = nullptr;
		if(RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\ddnet", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hProt, nullptr) == ERROR_SUCCESS)
		{
			const wchar_t *Desc = L"URL:ddnet Protocol";
			RegSetValueExW(hProt, L"", 0, REG_SZ, (const BYTE *)Desc, (DWORD)((wcslen(Desc) + 1) * sizeof(wchar_t)));
			RegSetValueExW(hProt, L"URL Protocol", 0, REG_SZ, (const BYTE *)L"", sizeof(wchar_t));
			RegCloseKey(hProt);
		}
		RegSetCommand(L"Software\\Classes\\ddnet\\shell\\open\\command", LauncherExe);
	}

	// .map / .demo under DDNet.*
	auto RegisterExt = [&](const wchar_t *Ext, const wchar_t *ProgId, const wchar_t *Desc) {
		HKEY hId = nullptr;
		if(RegCreateKeyExW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + ProgId).c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hId, nullptr) == ERROR_SUCCESS)
		{
			RegSetValueExW(hId, L"", 0, REG_SZ, (const BYTE *)Desc, (DWORD)((wcslen(Desc) + 1) * sizeof(wchar_t)));
			RegCloseKey(hId);
		}
		RegSetCommand((std::wstring(L"Software\\Classes\\") + ProgId + L"\\shell\\open\\command").c_str(), LauncherExe);
		HKEY hExt = nullptr;
		if(RegCreateKeyExW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + Ext).c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hExt, nullptr) == ERROR_SUCCESS)
		{
			RegSetValueExW(hExt, L"", 0, REG_SZ, (const BYTE *)ProgId, (DWORD)((wcslen(ProgId) + 1) * sizeof(wchar_t)));
			RegCloseKey(hExt);
		}
	};
	RegisterExt(L".map", L"DDNet.map", L"Map File");
	RegisterExt(L".demo", L"DDNet.demo", L"Demo File");
}

// ─── Worker ───────────────────────────────────────────────────────────────────

// ─── Worker ───────────────────────────────────────────────────────────────────

static void RestorePendingArgs(LauncherArgs *pA)
{
	const std::wstring Pending = JoinPath(pA->InstallDir, L"uclient_launch_pending.args");
	FILE *pFile = nullptr;
	if(_wfopen_s(&pFile, Pending.c_str(), L"rb") != 0 || !pFile)
		return;
	char aLine[1024];
	while(fgets(aLine, sizeof(aLine), pFile))
	{
		size_t N = strlen(aLine);
		while(N > 0 && (aLine[N - 1] == '\n' || aLine[N - 1] == '\r'))
			aLine[--N] = '\0';
		if(N > 0)
			pA->ForwardArgs.push_back(Utf8ToWide(aLine));
	}
	fclose(pFile);
	DeleteFileW(Pending.c_str());
}

static std::wstring NormalizePathLower(std::wstring Path)
{
	if(Path.empty())
		return Path;
	std::transform(Path.begin(), Path.end(), Path.begin(), [](wchar_t Ch) {
		return (wchar_t)towlower(Ch);
	});
	while(!Path.empty() && (Path.back() == L'\\' || Path.back() == L'/'))
		Path.pop_back();
	return Path;
}

static bool GetProcessImagePath(DWORD Pid, std::wstring &Out)
{
	Out.clear();
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Pid);
	if(!hProcess)
		return false;
	wchar_t aPath[MAX_PATH] = {};
	DWORD Size = MAX_PATH;
	const BOOL Ok = QueryFullProcessImageNameW(hProcess, 0, aPath, &Size);
	CloseHandle(hProcess);
	if(!Ok || Size == 0)
		return false;
	Out.assign(aPath, Size);
	return true;
}

static bool IsInstallGameRunning(const std::wstring &InstallDir)
{
	if(InstallDir.empty())
		return false;
	const std::wstring TargetExe = NormalizePathLower(JoinPath(InstallDir, kGameExe));
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if(hSnap == INVALID_HANDLE_VALUE)
		return false;

	PROCESSENTRY32W Entry = {};
	Entry.dwSize = sizeof(Entry);
	bool Found = false;
	if(Process32FirstW(hSnap, &Entry))
	{
		do
		{
			if(_wcsicmp(Entry.szExeFile, kGameExe) != 0)
				continue;
			std::wstring ImagePath;
			if(GetProcessImagePath(Entry.th32ProcessID, ImagePath) &&
				NormalizePathLower(ImagePath) == TargetExe)
			{
				Found = true;
				break;
			}
		} while(Process32NextW(hSnap, &Entry));
	}
	CloseHandle(hSnap);
	return Found;
}

static void RefreshGameRunningState()
{
	if(!g_pArgs)
		return;
	const bool Running = IsInstallGameRunning(g_pArgs->InstallDir);
	EnterCriticalSection(&g_Lock);
	g_GameRunning = Running;
	LeaveCriticalSection(&g_Lock);
}

static void SyncReadyButtonLabel()
{
	if(g_Phase != EUiPhase::Ready)
		return;
	if(EffectiveUpdateAvailable())
		SetButtonLabel(kUpdateLabel);
	else
		SetButtonLabel(kPlayLabel);
}

#ifdef CONF_UCLIENT_LAUNCHER_DEV
static bool EffectiveUpdateAvailable()
{
	if(g_Dev.ForceUpdateAvailable)
		return true;
	return g_UpdateAvailable;
}

static bool EffectivePlayBlocked()
{
	if(g_Dev.ForcePlayBlocked)
		return true;
	return g_PlayBlocked;
}

static bool EffectiveGameRunning()
{
	if(g_Dev.ForceGameRunning)
		return true;
	return g_GameRunning;
}

static void DevRecomputeInjectedNotice()
{
	if(!g_Dev.InjectNotice)
	{
		for(size_t i = 0; i < g_Notices.size();)
		{
			if(g_Notices[i].Id == "dev_injected")
				g_Notices.erase(g_Notices.begin() + (ptrdiff_t)i);
			else
				++i;
		}
		return;
	}

	for(const NoticeView &N : g_Notices)
	{
		if(N.Id == "dev_injected")
			return;
	}
	NoticeView Notice;
	Notice.Id = "dev_injected";
	Notice.Title = "Dev Test Notice";
	Notice.Body = "This notice was injected from the developer panel.";
	Notice.Severity = "warning";
	Notice.BlocksPlay = true;
	g_Notices.insert(g_Notices.begin(), std::move(Notice));
}

static void DevResetOverrides()
{
	g_Dev = {};
	EnterCriticalSection(&g_Lock);
	for(size_t i = 0; i < g_Notices.size();)
	{
		if(g_Notices[i].Id == "dev_injected")
			g_Notices.erase(g_Notices.begin() + (ptrdiff_t)i);
		else
			++i;
	}
	RecomputePlayBlocked(g_Notices);
	LeaveCriticalSection(&g_Lock);
}

static DWORD WINAPI FakeDownloadThread(LPVOID)
{
	SetPhase(EUiPhase::Updating);
	g_UpdateStage = EUpdateStage::Download;
	SetButtonLabel(L"Downloading");
	SetStatus(L"[Dev] Simulated download");
	SetPercent(0);
	g_DownloadTotal = 520 * 1024 * 1024ULL;

	for(int Pct = 0; Pct <= 100; Pct += 2)
	{
		SetPercent(Pct);
		g_DownloadDone = g_DownloadTotal * (uint64_t)Pct / 100ULL;
		g_DownloadSpeed = 9 * 1024 * 1024ULL;
		g_EtaSeconds = Pct >= 100 ? 0 : (100 - Pct) / 2;
		Sleep(90);
	}

	g_UpdateStage = EUpdateStage::Apply;
	SetButtonLabel(L"Applying update");
	SetStatus(L"[Dev] Simulated apply");
	SetPercent(50);
	Sleep(1200);
	SetPercent(100);
	g_UpdateStage = EUpdateStage::None;
	g_DownloadSpeed = 0;
	g_EtaSeconds = -1;
	SetStatus(L"");
	SetPhase(EUiPhase::Ready);
	SyncReadyButtonLabel();
	RefreshGameRunningState();
	SyncButtonHint();
	g_UpdateDownloadRunning = false;
	if(g_hWnd)
		PostMessage(g_hWnd, WM_UPDATE_READY, 0, 0);
	return 0;
}

static void RequestFakeDownload()
{
	if(g_UpdateDownloadRunning.load() || g_Phase != EUiPhase::Ready)
		return;
	g_UpdateDownloadRunning = true;
	HANDLE hThread = CreateThread(nullptr, 0, FakeDownloadThread, nullptr, 0, nullptr);
	if(hThread)
		CloseHandle(hThread);
	else
		g_UpdateDownloadRunning = false;
}

static void HandleDevCommand(const std::string &Json)
{
	std::string Action;
	if(!ExtractJsonString(Json, "action", Action))
		return;

	const bool Value = Json.find("\"value\":true") != std::string::npos;
	if(Action == "forceUpdate")
		g_Dev.ForceUpdateAvailable = Value;
	else if(Action == "forcePlayBlocked")
		g_Dev.ForcePlayBlocked = Value;
	else if(Action == "forceGameRunning")
		g_Dev.ForceGameRunning = Value;
	else if(Action == "injectNotice")
	{
		g_Dev.InjectNotice = Value;
		EnterCriticalSection(&g_Lock);
		DevRecomputeInjectedNotice();
		RecomputePlayBlocked(g_Notices);
		LeaveCriticalSection(&g_Lock);
	}
	else if(Action == "fakeDownload")
		RequestFakeDownload();
	else if(Action == "reset")
		DevResetOverrides();

	SyncReadyButtonLabel();
	SyncButtonHint();
	PushWebState(true);
}
#endif

static void SyncButtonHint()
{
	std::wstring Hint;
	if(g_Phase == EUiPhase::Ready)
	{
#ifdef CONF_UCLIENT_LAUNCHER_DEV
		if(g_Dev.ForcePlayBlocked)
			Hint = L"[Dev] Play blocked for testing.";
		else
#endif
		if(EffectivePlayBlocked())
		{
			EnterCriticalSection(&g_Lock);
			for(const NoticeView &N : g_Notices)
			{
				if(N.BlocksPlay)
				{
					Hint = Utf8ToWide(N.Title.c_str());
					if(!N.Body.empty())
					{
						Hint += L"\n";
						Hint += Utf8ToWide(N.Body.c_str());
					}
					break;
				}
			}
			LeaveCriticalSection(&g_Lock);
			if(Hint.empty())
				Hint = L"Play is blocked until the notice is resolved.";
		}
		else if(EffectiveUpdateAvailable() && EffectiveGameRunning())
		{
			Hint = L"You cannot update because the game is currently running. Please close the game and then proceed with the update.";
		}
#ifdef CONF_UCLIENT_LAUNCHER_DEV
		else if(g_Dev.ForceGameRunning && EffectiveUpdateAvailable())
		{
			Hint = L"[Dev] Game running override is active.";
		}
#endif
	}
	EnterCriticalSection(&g_Lock);
	g_ButtonHint = std::move(Hint);
	LeaveCriticalSection(&g_Lock);
}

static bool RunUpdateDownload(LauncherArgs *pA, const std::string &RemoteVersion, const std::string &ArchiveUrl)
{
	if(!pA || RemoteVersion.empty() || ArchiveUrl.empty())
		return false;

	SetPhase(EUiPhase::Updating);
	SetButtonLabel(L"Downloading");
	g_UpdateStage = EUpdateStage::Download;
	g_Failed = false;
	wchar_t aInfo[128];
	_snwprintf_s(aInfo, _TRUNCATE, L"Version %hs", RemoteVersion.c_str());
	SetStatus(aInfo);
	SetPercent(0);

	const std::wstring ArchivePath = JoinPath(pA->InstallDir, kArchiveRel);
	CreateDirectoryW(JoinPath(pA->InstallDir, L"update").c_str(), nullptr);

	if(!HttpDownloadFile(Utf8ToWide(ArchiveUrl.c_str()), ArchivePath))
	{
		SetStatus(L"Download failed — you can still play the current version");
		g_Failed = true;
		g_UpdateStage = EUpdateStage::None;
		return false;
	}

	WriteTextFile(JoinPath(pA->InstallDir, kPendingVersionFile), RemoteVersion);
	SetStatus(L"");
	g_UpdateStage = EUpdateStage::Apply;
	SetButtonLabel(L"Applying update");
	if(!ApplyUpdateArchive(ArchivePath, pA->InstallDir, pA->SelfPath))
	{
		SetStatus(L"Update failed — you can still play");
		g_Failed = true;
		g_UpdateStage = EUpdateStage::None;
		return false;
	}

	WriteTextFile(JoinPath(pA->InstallDir, kVersionFile), RemoteVersion);
	SetVersionLabel(RemoteVersion);
	g_UpdateStage = EUpdateStage::None;
	g_DownloadSpeed = 0;
	g_EtaSeconds = -1;
	SetPercent(100);
	if(!g_Failed)
		SetStatus(L"");
	return true;
}

static DWORD WINAPI UpdateDownloadThread(LPVOID)
{
	std::string RemoteVersion;
	std::string ArchiveUrl;
	EnterCriticalSection(&g_Lock);
	RemoteVersion = g_PendingRemoteVersion;
	ArchiveUrl = g_PendingArchiveUrl;
	LeaveCriticalSection(&g_Lock);

	LauncherArgs *pA = g_pArgs;
	const bool Ok = pA && RunUpdateDownload(pA, RemoteVersion, ArchiveUrl);
	if(Ok)
	{
		EnterCriticalSection(&g_Lock);
		g_UpdateAvailable = false;
		g_PendingRemoteVersion.clear();
		g_PendingArchiveUrl.clear();
		LeaveCriticalSection(&g_Lock);
	}

	g_UpdateDownloadRunning = false;
	SetPhase(EUiPhase::Ready);
	SyncReadyButtonLabel();
	RefreshGameRunningState();
	SyncButtonHint();
	RefreshLauncherNotices();
	if(g_hWnd)
		PostMessage(g_hWnd, WM_UPDATE_READY, 0, 0);
	return 0;
}

static void RequestUpdateDownload()
{
	if(g_UpdateDownloadRunning.load() || !g_pArgs || g_Phase != EUiPhase::Ready || EffectivePlayBlocked() || EffectiveGameRunning() || !EffectiveUpdateAvailable())
		return;

	EnterCriticalSection(&g_Lock);
	if(g_PendingRemoteVersion.empty() || g_PendingArchiveUrl.empty())
	{
		LeaveCriticalSection(&g_Lock);
#ifdef CONF_UCLIENT_LAUNCHER_DEV
		if(g_Dev.ForceUpdateAvailable)
			RequestFakeDownload();
#endif
		return;
	}
	LeaveCriticalSection(&g_Lock);

	g_UpdateDownloadRunning = true;
	HANDLE hThread = CreateThread(nullptr, 0, UpdateDownloadThread, nullptr, 0, nullptr);
	if(hThread)
		CloseHandle(hThread);
	else
		g_UpdateDownloadRunning = false;
}

static void TryStartupAutoUpdate()
{
	if(!g_TryStartupAutoUpdate)
		return;
	g_TryStartupAutoUpdate = false;
	if(!g_AutoUpdate || !g_pArgs || g_Phase != EUiPhase::Ready || !EffectiveUpdateAvailable())
		return;
	if(EffectivePlayBlocked() || EffectiveGameRunning())
		return;
	RequestUpdateDownload();
}

static DWORD WINAPI UpdateCheckThread(LPVOID)
{
	if(!g_pArgs)
	{
		EnterCriticalSection(&g_Lock);
		g_UpdateCheckRefreshing = false;
		LeaveCriticalSection(&g_Lock);
		return 0;
	}

	const EUiPhase Phase = g_Phase;
	if(Phase == EUiPhase::Checking || Phase == EUiPhase::Updating)
	{
		EnterCriticalSection(&g_Lock);
		g_UpdateCheckRefreshing = false;
		LeaveCriticalSection(&g_Lock);
		return 0;
	}

	const std::string LocalVersion = ResolveLocalVersion(g_pArgs->InstallDir);
	char aUrlUtf8[512];
	_snprintf_s(aUrlUtf8, _TRUNCATE, "%s?t=%lld", UCLIENT_UPDATE_LATEST_URL, (long long)time(nullptr));
	const std::wstring Url = Utf8ToWide(aUrlUtf8);

	std::string Body;
	std::string RemoteVersion;
	std::string ArchiveUrl;
	bool NeedUpdate = false;
	if(HttpGetToString(Url, Body) && ExtractWindowsUrl(Body, RemoteVersion, ArchiveUrl))
	{
		if(CompareVersions(RemoteVersion, LocalVersion) > 0)
			NeedUpdate = true;
	}

	EnterCriticalSection(&g_Lock);
	g_UpdateAvailable = NeedUpdate;
	if(NeedUpdate)
	{
		g_PendingRemoteVersion = RemoteVersion;
		g_PendingArchiveUrl = ArchiveUrl;
	}
	else
	{
		g_PendingRemoteVersion.clear();
		g_PendingArchiveUrl.clear();
	}
	g_UpdateCheckRefreshing = false;
	LeaveCriticalSection(&g_Lock);

	RefreshGameRunningState();
	if(g_Phase == EUiPhase::Ready)
	{
		SyncReadyButtonLabel();
		if(NeedUpdate)
		{
			wchar_t aInfo[128];
			_snwprintf_s(aInfo, _TRUNCATE, L"Update available: %hs", RemoteVersion.c_str());
			SetStatus(aInfo);
		}
	}
	SyncButtonHint();
	if(g_hWnd)
		PostMessage(g_hWnd, WM_UPDATE_CHECK_READY, 0, 0);
	return 0;
}

static void RequestUpdateCheck()
{
	if(!g_pArgs)
		return;
	const EUiPhase Phase = g_Phase;
	if(Phase == EUiPhase::Checking || Phase == EUiPhase::Updating)
		return;

	EnterCriticalSection(&g_Lock);
	if(g_UpdateCheckRefreshing)
	{
		LeaveCriticalSection(&g_Lock);
		return;
	}
	g_UpdateCheckRefreshing = true;
	LeaveCriticalSection(&g_Lock);

	HANDLE hThread = CreateThread(nullptr, 0, UpdateCheckThread, nullptr, 0, nullptr);
	if(hThread)
		CloseHandle(hThread);
	else
	{
		EnterCriticalSection(&g_Lock);
		g_UpdateCheckRefreshing = false;
		LeaveCriticalSection(&g_Lock);
	}
}

static DWORD WINAPI WorkerThread(LPVOID pParam)
{
	auto *pA = static_cast<LauncherArgs *>(pParam);

	RegisterShellHandlers(pA->SelfPath);
	RefreshLauncherNotices();

	// In-game download path: apply existing zip, then start game (no re-check).
	if(!pA->ApplyArchive.empty())
	{
		SetPhase(EUiPhase::Updating);
		g_UpdateStage = EUpdateStage::Apply;
		SetButtonLabel(L"Applying update");
		if(pA->WaitPid != 0)
		{
			SetStatus(L"Waiting for client to close...");
			SetPercent(2);
			HANDLE hProc = OpenProcess(SYNCHRONIZE, FALSE, pA->WaitPid);
			if(hProc)
			{
				WaitForSingleObject(hProc, INFINITE);
				CloseHandle(hProc);
			}
			else
				Sleep(500);
		}

		SetStatus(L"Applying update...");
		if(!ApplyUpdateArchive(pA->ApplyArchive, pA->InstallDir, pA->SelfPath))
		{
			g_UpdateStage = EUpdateStage::None;
			SetPhase(EUiPhase::Ready);
			SyncReadyButtonLabel();
			SyncButtonHint();
			SetStatus(L"Update failed");
			g_Failed = true;
			PostMessage(g_hWnd, WM_UPDATE_READY, 0, 0);
			return 0;
		}

		SetPhase(EUiPhase::Launching);
		SetButtonLabel(kRunningLabel);
		SetStatus(L"Starting UClient...");
		if(!g_PlayBlocked)
		{
			HANDLE hProcess = nullptr;
			if(LaunchGame(pA, &hProcess))
			{
				g_hLaunchedGame = hProcess;
				BeginLaunchPoll();
			}
		}
		else
		{
			SetPhase(EUiPhase::Ready);
			SyncReadyButtonLabel();
			SyncButtonHint();
			SetStatus(L"Play is blocked until the notice is resolved.");
			PostMessage(g_hWnd, WM_UPDATE_READY, 0, 0);
		}
		return 0;
	}

	SetPhase(EUiPhase::Checking);
	g_UpdateStage = EUpdateStage::Check;
	SetButtonLabel(L"Checking for updates");
	SetStatus(L"");
	SetPercent(0);

	const std::string LocalVersion = ResolveLocalVersion(pA->InstallDir);
	SetVersionLabel(LocalVersion);
	RefreshLauncherNotices();

	char aUrlUtf8[512];
	_snprintf_s(aUrlUtf8, _TRUNCATE, "%s?t=%lld", UCLIENT_UPDATE_LATEST_URL, (long long)time(nullptr));
	const std::wstring aUrl = Utf8ToWide(aUrlUtf8);

	std::string Body;
	std::string RemoteVersion;
	std::string ArchiveUrl;
	bool NeedUpdate = false;

	if(HttpGetToString(aUrl, Body) && ExtractWindowsUrl(Body, RemoteVersion, ArchiveUrl))
	{
		if(CompareVersions(RemoteVersion, LocalVersion) > 0)
			NeedUpdate = true;
	}
	else
	{
		SetStatus(L"Update check failed — you can still play");
		g_Failed = true;
	}

	if(NeedUpdate)
	{
		EnterCriticalSection(&g_Lock);
		g_UpdateAvailable = true;
		g_PendingRemoteVersion = RemoteVersion;
		g_PendingArchiveUrl = ArchiveUrl;
		LeaveCriticalSection(&g_Lock);
		wchar_t aInfo[128];
		_snwprintf_s(aInfo, _TRUNCATE, L"Update available: %hs", RemoteVersion.c_str());
		SetStatus(aInfo);
	}

	RestorePendingArgs(pA);
	const std::string FinalVersion = ResolveLocalVersion(pA->InstallDir);
	WriteTextFile(JoinPath(pA->InstallDir, kVersionFile), FinalVersion);
	SetVersionLabel(FinalVersion);
	SetPercent(100);
	SetPhase(EUiPhase::Ready);
	g_UpdateStage = EUpdateStage::None;
	g_DownloadSpeed = 0;
	g_EtaSeconds = -1;
	SyncReadyButtonLabel();
	RefreshGameRunningState();
	SyncButtonHint();
	if(!g_Failed && !NeedUpdate)
		SetStatus(L"");
	RefreshLauncherNotices();
	g_TryStartupAutoUpdate = NeedUpdate && g_AutoUpdate && !g_PlayBlocked && !EffectiveGameRunning();
	PostMessage(g_hWnd, WM_UPDATE_READY, 0, 0);
	return 0;
}

static void RequestLaunchGame(const wchar_t *pConnectAddress = nullptr)
{
	if(!g_pArgs || g_Phase == EUiPhase::Launching || g_Phase == EUiPhase::Checking || g_Phase == EUiPhase::Updating)
		return;
	if(g_PlayBlocked || EffectiveUpdateAvailable())
		return;
	if(pConnectAddress && pConnectAddress[0])
		g_ConnectAddress = pConnectAddress;
	else
		g_ConnectAddress.clear();
	SetPhase(EUiPhase::Launching);
	g_UpdateStage = EUpdateStage::None;
	SetButtonLabel(kRunningLabel);
	SetStatus(L"Starting UClient...");
	CloseLaunchedGameHandle();
	HANDLE hProcess = nullptr;
	if(!LaunchGame(g_pArgs, &hProcess))
	{
		SetPhase(EUiPhase::Ready);
		SyncReadyButtonLabel();
		g_ConnectAddress.clear();
		PushWebState(true);
		return;
	}
	g_hLaunchedGame = hProcess;
	BeginLaunchPoll();
}

// ─── WebView2 UI bridge ───────────────────────────────────────────────────────

// True once the WebView2 controller is hosting the UI. While false the GDI
// renderer below owns the window, which is also the fallback path when the
// WebView2 runtime is missing.
static bool g_WebUi = false;

#ifdef UCLIENT_LAUNCHER_WEBVIEW

static std::string g_LogoUrl;
static std::string g_MascotUrl;
static std::string g_WebStateSent;

static bool FileExistsW(const std::wstring &Path)
{
	const DWORD Attr = GetFileAttributesW(Path.c_str());
	return Attr != INVALID_FILE_ATTRIBUTES && !(Attr & FILE_ATTRIBUTE_DIRECTORY);
}

// The page reaches launcher art through the virtual host mapped onto data/.
static void InitWebArtUrls(const std::wstring &InstallDir)
{
	if(FileExistsW(JoinPath(InstallDir, L"data\\BestClient\\gui_logo.png")))
		g_LogoUrl = "https://uclient.local/BestClient/gui_logo.png";
	else if(FileExistsW(JoinPath(InstallDir, L"data\\gui_logo.png")))
		g_LogoUrl = "https://uclient.local/gui_logo.png";
	if(FileExistsW(JoinPath(InstallDir, L"data\\uclient\\logo\\uclient.png")))
		g_MascotUrl = "https://uclient.local/uclient/logo/uclient.png";
}

static std::wstring WebViewUserDataDir(const std::wstring &InstallDir)
{
	wchar_t aPath[MAX_PATH] = L"";
	if(FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, aPath)) || !aPath[0])
		return JoinPath(InstallDir, L"webview");
	std::wstring Dir = aPath;
	Dir += L"\\UClient";
	CreateDirectoryW(Dir.c_str(), nullptr);
	Dir += L"\\WebView2";
	CreateDirectoryW(Dir.c_str(), nullptr);
	return Dir;
}

static std::string JsonEscape(const std::string &In)
{
	std::string Out;
	Out.reserve(In.size() + 8);
	for(size_t i = 0; i < In.size(); ++i)
	{
		const unsigned char Ch = (unsigned char)In[i];
		// The result is spliced into a JS expression, where U+2028/U+2029 are
		// line terminators and would truncate the script.
		if(Ch == 0xE2 && i + 2 < In.size() && (unsigned char)In[i + 1] == 0x80 &&
			((unsigned char)In[i + 2] == 0xA8 || (unsigned char)In[i + 2] == 0xA9))
		{
			Out += ((unsigned char)In[i + 2] == 0xA8) ? "\\u2028" : "\\u2029";
			i += 2;
			continue;
		}
		switch(Ch)
		{
		case '"': Out += "\\\""; break;
		case '\\': Out += "\\\\"; break;
		case '\n': Out += "\\n"; break;
		case '\r': Out += "\\r"; break;
		case '\t': Out += "\\t"; break;
		default:
			if(Ch < 0x20)
			{
				char aEsc[8];
				_snprintf_s(aEsc, sizeof(aEsc), _TRUNCATE, "\\u%04x", (unsigned)Ch);
				Out += aEsc;
			}
			else
				Out += (char)Ch;
			break;
		}
	}
	return Out;
}

static void JsonAddString(std::string &Json, const char *pKey, const std::string &Value)
{
	Json += "\"";
	Json += pKey;
	Json += "\":\"";
	Json += JsonEscape(Value);
	Json += "\",";
}

static const char *UpdateStageName(EUpdateStage Stage)
{
	switch(Stage)
	{
	case EUpdateStage::Check: return "check";
	case EUpdateStage::Download: return "download";
	case EUpdateStage::Apply: return "apply";
	default: return "none";
	}
}

static std::string BuildStateJson()
{
	std::wstring Button, Version, Status;
	std::vector<FriendView> Friends;
	std::vector<NoticeView> Notices;
	bool FriendsLoading = false;
	bool FriendsLoaded = false;
	bool PlayBlocked = false;
	bool UpdateAvailable = false;
	bool GameRunning = false;
	std::wstring ButtonHint;
	EnterCriticalSection(&g_Lock);
	Button = g_aButtonLabel;
	Version = g_aVersionText;
	Status = g_aStatus;
	Friends = g_Friends;
	Notices = g_Notices;
	FriendsLoading = g_FriendsLoading;
	FriendsLoaded = g_FriendsLoaded;
	PlayBlocked = g_PlayBlocked;
	UpdateAvailable = g_UpdateAvailable;
	GameRunning = g_GameRunning;
	ButtonHint = g_ButtonHint;
	LeaveCriticalSection(&g_Lock);
	PlayBlocked = EffectivePlayBlocked();
	UpdateAvailable = EffectiveUpdateAvailable();
	GameRunning = EffectiveGameRunning();

	const char *pPhase = "checking";
	if(g_Phase == EUiPhase::Updating)
		pPhase = "updating";
	else if(g_Phase == EUiPhase::Ready)
		pPhase = "ready";
	else if(g_Phase == EUiPhase::Launching)
		pPhase = "launching";

	std::string Json = "{";
	JsonAddString(Json, "phase", pPhase);
	JsonAddString(Json, "buttonLabel", WideToUtf8(Button));
	JsonAddString(Json, "version", WideToUtf8(Version));
	JsonAddString(Json, "status", WideToUtf8(Status));
	JsonAddString(Json, "logoUrl", g_LogoUrl);
	JsonAddString(Json, "mascotUrl", g_MascotUrl);

	char aNum[64];
	_snprintf_s(aNum, sizeof(aNum), _TRUNCATE, "\"percent\":%d,", g_Percent.load());
	Json += aNum;
	JsonAddString(Json, "updateStage", UpdateStageName(g_UpdateStage.load()));
	_snprintf_s(aNum, sizeof(aNum), _TRUNCATE, "\"downloadDone\":%llu,", (unsigned long long)g_DownloadDone.load());
	Json += aNum;
	_snprintf_s(aNum, sizeof(aNum), _TRUNCATE, "\"downloadTotal\":%llu,", (unsigned long long)g_DownloadTotal.load());
	Json += aNum;
	_snprintf_s(aNum, sizeof(aNum), _TRUNCATE, "\"downloadSpeed\":%llu,", (unsigned long long)g_DownloadSpeed.load());
	Json += aNum;
	_snprintf_s(aNum, sizeof(aNum), _TRUNCATE, "\"etaSeconds\":%d,", g_EtaSeconds.load());
	Json += aNum;
	Json += g_Failed ? "\"failed\":true," : "\"failed\":false,";
	Json += g_AutoLaunch ? "\"autoLaunch\":true," : "\"autoLaunch\":false,";
	Json += g_AutoUpdate ? "\"autoUpdate\":true," : "\"autoUpdate\":false,";
	Json += g_DiscordRpc ? "\"discordRpc\":true," : "\"discordRpc\":false,";
	Json += FriendsLoading ? "\"friendsLoading\":true," : "\"friendsLoading\":false,";
	Json += FriendsLoaded ? "\"friendsLoaded\":true," : "\"friendsLoaded\":false,";
	Json += PlayBlocked ? "\"playBlocked\":true," : "\"playBlocked\":false,";
	Json += UpdateAvailable ? "\"updateAvailable\":true," : "\"updateAvailable\":false,";
	Json += GameRunning ? "\"gameRunning\":true," : "\"gameRunning\":false,";
	JsonAddString(Json, "buttonHint", WideToUtf8(ButtonHint));
#ifdef CONF_UCLIENT_LAUNCHER_DEV
	Json += "\"devBuild\":true,";
	Json += g_Dev.ForceUpdateAvailable ? "\"devForceUpdate\":true," : "\"devForceUpdate\":false,";
	Json += g_Dev.ForcePlayBlocked ? "\"devForcePlayBlocked\":true," : "\"devForcePlayBlocked\":false,";
	Json += g_Dev.ForceGameRunning ? "\"devForceGameRunning\":true," : "\"devForceGameRunning\":false,";
	Json += g_Dev.InjectNotice ? "\"devInjectNotice\":true," : "\"devInjectNotice\":false,";
#else
	Json += "\"devBuild\":false,";
#endif

	Json += "\"notices\":[";
	for(size_t i = 0; i < Notices.size(); ++i)
	{
		if(i)
			Json += ",";
		Json += "{";
		JsonAddString(Json, "id", Notices[i].Id);
		JsonAddString(Json, "title", Notices[i].Title);
		JsonAddString(Json, "body", Notices[i].Body);
		JsonAddString(Json, "severity", Notices[i].Severity);
		Json += Notices[i].BlocksPlay ? "\"blocksPlay\":true" : "\"blocksPlay\":false";
		if(Notices[i].HasExpiresAt)
		{
			if(Notices[i].BanPermanent)
				Json += ",\"expiresAt\":null";
			else
			{
				char aExpiry[64];
				_snprintf_s(aExpiry, _TRUNCATE, ",\"expiresAt\":%lld", (long long)Notices[i].ExpiresAt);
				Json += aExpiry;
			}
		}
		Json += "}";
	}
	Json += "],";

	Json += "\"friends\":[";
	for(size_t i = 0; i < Friends.size(); ++i)
	{
		if(i)
			Json += ",";
		Json += "{";
		JsonAddString(Json, "name", Friends[i].Name);
		JsonAddString(Json, "clan", Friends[i].Clan);
		JsonAddString(Json, "server", Friends[i].ServerName);
		JsonAddString(Json, "map", Friends[i].MapName);
		JsonAddString(Json, "address", Friends[i].Address);
		Json += Friends[i].Online ? "\"online\":true" : "\"online\":false";
		Json += "}";
	}
	Json += "]}";
	return Json;
}

static void PushWebState(bool Force)
{
	if(!g_WebUi)
		return;
	std::string Json = BuildStateJson();
	if(!Force && Json == g_WebStateSent)
		return;
	g_WebStateSent = Json;
	WebUi::PostState(Json);
}

static void OnWebMessage(const std::string &Json)
{
	std::string Cmd;
	if(!ExtractJsonString(Json, "cmd", Cmd))
		return;

	if(Cmd == "ready")
	{
		PushWebState(true);
	}
	else if(Cmd == "close")
	{
		DestroyWindow(g_hWnd);
	}
	else if(Cmd == "minimize")
	{
		ShowWindow(g_hWnd, SW_MINIMIZE);
	}
	else if(Cmd == "drag")
	{
		// The WebView2 child window owns the mouse, so the page asks the host
		// to take over the drag as if the caption had been grabbed.
		ReleaseCapture();
		SendMessageW(g_hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
	}
	else if(Cmd == "play")
	{
		if(g_Phase == EUiPhase::Ready && !EffectivePlayBlocked() && !EffectiveUpdateAvailable())
			RequestLaunchGame();
	}
	else if(Cmd == "update")
	{
		RequestUpdateDownload();
	}
#ifdef CONF_UCLIENT_LAUNCHER_DEV
	else if(Cmd == "dev")
	{
		HandleDevCommand(Json);
	}
#endif
	else if(Cmd == "join")
	{
		std::string Address;
		if(ExtractJsonString(Json, "address", Address) && !Address.empty() && g_Phase == EUiPhase::Ready && !EffectivePlayBlocked() && !EffectiveUpdateAvailable())
			RequestLaunchGame(Utf8ToWide(Address.c_str()).c_str());
	}
	else if(Cmd == "autolaunch")
	{
		g_AutoLaunch = Json.find("\"value\":true") != std::string::npos;
		SaveLauncherSettings(g_InstallDir);
		PushWebState(true);
	}
	else if(Cmd == "autoupdate")
	{
		g_AutoUpdate = Json.find("\"value\":true") != std::string::npos;
		SaveLauncherSettings(g_InstallDir);
		PushWebState(true);
	}
	else if(Cmd == "discordRpc")
	{
		SaveDiscordRpcSetting(Json.find("\"value\":true") != std::string::npos);
		PushWebState(true);
	}
	else if(Cmd == "refreshFriends")
	{
		RequestFriendsRefresh();
	}
}

// Asynchronous creation failure: hand the window back to the GDI renderer.
static void OnWebFailed()
{
	g_WebUi = false;
	WebUi::Shutdown();
	if(g_hWnd)
	{
		KillTimer(g_hWnd, ANIM_TIMER_ID);
		SetTimer(g_hWnd, ANIM_TIMER_ID, 16, nullptr);
		InvalidateRect(g_hWnd, nullptr, TRUE);
	}
}

#else

static void PushWebState(bool) {}

#endif // UCLIENT_LAUNCHER_WEBVIEW

static void CloseLaunchedGameHandle()
{
	if(g_hLaunchedGame)
	{
		CloseHandle(g_hLaunchedGame);
		g_hLaunchedGame = nullptr;
	}
}

static void OnGameLaunchFailed(const wchar_t *pStatus)
{
	if(g_hWnd)
		KillTimer(g_hWnd, LAUNCH_TIMER_ID);
	CloseLaunchedGameHandle();
	g_ConnectAddress.clear();
	g_Failed = true;
	SetPhase(EUiPhase::Ready);
	SyncReadyButtonLabel();
	SetStatus(pStatus);
	PushWebState(true);
	if(g_hWnd)
		InvalidateRect(g_hWnd, nullptr, FALSE);
}

static void BeginLaunchPoll()
{
	g_LaunchPollStartTick = GetTickCount();
	if(g_hWnd)
		SetTimer(g_hWnd, LAUNCH_TIMER_ID, 200, nullptr);
	PushWebState(true);
}

static void PollLaunchProcess()
{
	if(!g_hLaunchedGame)
	{
		FinishLaunchKeepOpen();
		return;
	}

	DWORD ExitCode = STILL_ACTIVE;
	if(!GetExitCodeProcess(g_hLaunchedGame, &ExitCode))
	{
		OnGameLaunchFailed(L"Failed to monitor game process");
		return;
	}

	if(ExitCode != STILL_ACTIVE)
	{
		OnGameLaunchFailed(L"Game closed before it finished starting");
		return;
	}

	if(GetTickCount() - g_LaunchPollStartTick >= 1500)
	{
		if(g_hWnd)
			KillTimer(g_hWnd, LAUNCH_TIMER_ID);
		CloseLaunchedGameHandle();
		FinishLaunchKeepOpen();
	}
}

static void FinishLaunchKeepOpen()
{
	if(g_hWnd)
		KillTimer(g_hWnd, LAUNCH_TIMER_ID);
	CloseLaunchedGameHandle();
	g_ConnectAddress.clear();
	g_Failed = false;
	g_UpdateStage = EUpdateStage::None;
	g_DownloadSpeed = 0;
	g_EtaSeconds = -1;
	SetPhase(EUiPhase::Ready);
	SyncReadyButtonLabel();
	SetStatus(L"");
	RefreshGameRunningState();
	SyncButtonHint();
	PushWebState(true);
	if(g_hWnd)
		InvalidateRect(g_hWnd, nullptr, FALSE);
}

// ─── UI ───────────────────────────────────────────────────────────────────────

// The gradient + glow backdrop is costly to rasterize, so it is built once and
// blitted every frame while the animation timer runs.
static HDC g_hBgDc = nullptr;
static HBITMAP g_hBgBmp = nullptr;
static HGDIOBJ g_hBgOld = nullptr;

// Fonts live for the process; recreating them per frame would leak GDI handles
// now that the animation timer repaints continuously.
static HFONT g_hFontHuge = nullptr;
static HFONT g_hFontTitle = nullptr;
static HFONT g_hFontBody = nullptr;
static HFONT g_hFontBtn = nullptr;
static HFONT g_hFontSmall = nullptr;
static HFONT g_hFontTab = nullptr;

static HFONT MakeUiFont(int Height, int Weight)
{
	return CreateFontW(Height, 0, 0, 0, Weight, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

static void EnsureFonts()
{
	if(g_hFontHuge)
		return;
	g_hFontHuge = MakeUiFont(44, FW_BOLD);
	g_hFontTitle = MakeUiFont(25, FW_SEMIBOLD);
	g_hFontBody = MakeUiFont(18, FW_NORMAL);
	g_hFontBtn = MakeUiFont(26, FW_BOLD);
	g_hFontSmall = MakeUiFont(15, FW_NORMAL);
	g_hFontTab = MakeUiFont(17, FW_SEMIBOLD);
}

static void FreeFonts()
{
	HFONT *appFonts[] = {&g_hFontHuge, &g_hFontTitle, &g_hFontBody, &g_hFontBtn, &g_hFontSmall, &g_hFontTab};
	for(HFONT *pFont : appFonts)
	{
		if(*pFont)
		{
			DeleteObject(*pFont);
			*pFont = nullptr;
		}
	}
}

static void EnsureBackground(HDC Ref)
{
	if(g_hBgDc)
		return;
	g_hBgDc = CreateCompatibleDC(Ref);
	if(!g_hBgDc)
		return;
	g_hBgBmp = CreateCompatibleBitmap(Ref, WND_W, WND_H);
	if(!g_hBgBmp)
	{
		DeleteDC(g_hBgDc);
		g_hBgDc = nullptr;
		return;
	}
	g_hBgOld = SelectObject(g_hBgDc, g_hBgBmp);

	RECT Full = {0, 0, WND_W, WND_H};
	FillVerticalGradient(g_hBgDc, Full, RGB(16, 18, 26), RGB(5, 5, 8));
	DrawGlow(g_hBgDc, WND_W - 180, 110, 330, C_BLUE, 48);
	DrawGlow(g_hBgDc, RAIL_W + 240, WND_H - 10, 330, C_ORANGE, 36);
	DrawGlow(g_hBgDc, WND_W / 2 - 40, 250, 240, RGB(80, 36, 64), 28);

	RECT Side = {0, 0, RAIL_W, WND_H};
	HBRUSH SideBrush = CreateSolidBrush(C_SIDE);
	FillRect(g_hBgDc, &Side, SideBrush);
	DeleteObject(SideBrush);
	RECT RailEdge = {RAIL_W - 1, 0, RAIL_W, WND_H};
	HBRUSH EdgeBrush = CreateSolidBrush(RGB(34, 36, 44));
	FillRect(g_hBgDc, &RailEdge, EdgeBrush);
	DeleteObject(EdgeBrush);
}

static void FreeBackground()
{
	if(g_hBgDc)
	{
		if(g_hBgOld)
			SelectObject(g_hBgDc, g_hBgOld);
		DeleteDC(g_hBgDc);
		g_hBgDc = nullptr;
		g_hBgOld = nullptr;
	}
	if(g_hBgBmp)
	{
		DeleteObject(g_hBgBmp);
		g_hBgBmp = nullptr;
	}
}

static void Paint(HWND hWnd)
{
	PAINTSTRUCT Ps;
	HDC Dc = BeginPaint(hWnd, &Ps);
	HDC Mem = CreateCompatibleDC(Dc);
	HBITMAP Bmp = CreateCompatibleBitmap(Dc, WND_W, WND_H);
	HGDIOBJ Old = SelectObject(Mem, Bmp);

	EnsureBackground(Dc);
	if(g_hBgDc)
		BitBlt(Mem, 0, 0, WND_W, WND_H, g_hBgDc, 0, 0, SRCCOPY);

	SetBkMode(Mem, TRANSPARENT);
	EnsureFonts();
	HFONT HugeFont = g_hFontHuge;
	HFONT TitleFont = g_hFontTitle;
	HFONT BodyFont = g_hFontBody;
	HFONT BtnFont = g_hFontBtn;
	HFONT SmallFont = g_hFontSmall;
	HFONT TabFont = g_hFontTab;

	wchar_t aButton[128];
	wchar_t aVersion[96];
	wchar_t aStatus[256];
	std::vector<FriendView> Friends;
	bool FriendsLoading = false;
	bool FriendsLoaded = false;
	EnterCriticalSection(&g_Lock);
	wcsncpy_s(aButton, g_aButtonLabel, _TRUNCATE);
	wcsncpy_s(aVersion, g_aVersionText, _TRUNCATE);
	wcsncpy_s(aStatus, g_aStatus, _TRUNCATE);
	Friends = g_Friends;
	FriendsLoading = g_FriendsLoading;
	FriendsLoaded = g_FriendsLoaded;
	LeaveCriticalSection(&g_Lock);

	g_CloseRc = {WND_W - 52, 12, WND_W - 14, 46};
	g_MinRc = {WND_W - 96, 12, WND_W - 58, 46};
	g_GearRc = {RAIL_W / 2 - 20, WND_H - 64, RAIL_W / 2 + 20, WND_H - 24};

	const int PanelL = WND_W - PANEL_W - 24;
	const int ContentL = RAIL_W + 44;
	const int ContentR = PanelL - 32;

	g_BackRc = {ContentL, WND_H - 84, ContentL + 128, WND_H - 44};
	g_CheckRc = {ContentL + 4, 176, ContentR, 262};
	g_AutoUpdateCheckRc = {ContentL + 4, 280, ContentR, 366};
	g_DiscordCheckRc = {ContentL + 4, 428, ContentR, 514};
	g_FriendAreaRc = {PanelL + 12, 182, WND_W - 36, WND_H - 44};
	g_FriendRefreshRc = {WND_W - 78, 68, WND_W - 44, 102};

	// Tabs: equal cells sized from the widest label, centered over the content area.
	{
		SelectObject(Mem, TabFont);
		SIZE A = {}, B = {};
		GetTextExtentPoint32W(Mem, L"Overview", 8, &A);
		GetTextExtentPoint32W(Mem, L"Updates", 7, &B);
		const int CellW = (A.cx > B.cx ? A.cx : B.cx) + 44;
		const int TabsW = CellW * 2;
		const int TabsL = (ContentL + ContentR) / 2 - TabsW / 2;
		g_TabOverviewRc = {TabsL, 58, TabsL + CellW, 100};
		g_TabUpdatesRc = {TabsL + CellW, 58, TabsL + TabsW, 100};
	}

	// Play button width follows its label so long status text never spills out.
	{
		SelectObject(Mem, BtnFont);
		SIZE Ls = {};
		GetTextExtentPoint32W(Mem, aButton, (int)wcslen(aButton), &Ls);
		int BtnW = 44 + Ls.cx + 18;
		if(BtnW < 150)
			BtnW = 150;
		if(BtnW > ContentR - ContentL)
			BtnW = ContentR - ContentL;
		const int Lift = (int)((1.0f - g_AnimIntro) * 18.0f);
		g_PlayBtnRc = {ContentL, WND_H - 132 + Lift, ContentL + BtnW, WND_H - 80 + Lift};
	}

	if(g_hMascotBmp)
		DrawBitmapAlpha(Mem, g_hMascotBmp, RAIL_W / 2 - 14, 48, 28, 28, g_MascotW, g_MascotH);
	else
	{
		RECT Mark = {RAIL_W / 2 - 16, 48, RAIL_W / 2 + 16, 80};
		FillRoundRect(Mem, Mark, 8, C_PANEL2);
		SelectObject(Mem, TabFont);
		SetTextColor(Mem, C_ACCENT);
		TextOutW(Mem, RAIL_W / 2 - 6, 54, L"U", 1);
	}

	{
		if(g_ShowSettings)
		{
			RECT Acc = {0, g_GearRc.top + 2, 3, g_GearRc.bottom - 2};
			HBRUSH AccBrush = CreateSolidBrush(C_ACCENT);
			FillRect(Mem, &Acc, AccBrush);
			DeleteObject(AccBrush);
		}
		const float GearHi = g_ShowSettings ? 1.0f : g_AnimGear;
		DrawGearIcon(Mem, g_GearRc, GearHi, g_AnimGear * 0.9f);
	}

	DrawCaptionButton(Mem, g_CloseRc, g_CloseHover, true);
	DrawCaptionButton(Mem, g_MinRc, g_MinHover, false);

	RECT Panel = {PanelL, 52, WND_W - 24, WND_H - 28};
	FillRoundRect(Mem, Panel, 18, RGB(18, 20, 26));
	StrokeRoundRect(Mem, Panel, 18, C_BORDER);

	int OnlineCount = 0;
	for(const FriendView &F : Friends)
	{
		if(F.Online)
			++OnlineCount;
	}

	SelectObject(Mem, TitleFont);
	SetTextColor(Mem, C_TITLE);
	TextOutW(Mem, PanelL + 24, 74, L"Friends", 7);
	if(g_FriendRefreshHover && !FriendsLoading)
		FillRoundRect(Mem, g_FriendRefreshRc, 10, RGB(44, 46, 55));
	SelectObject(Mem, BodyFont);
	SetTextColor(Mem, FriendsLoading ? C_ACCENT_HOVER : C_DIM);
	DrawTextW(Mem, L"\x21BB", 1, &g_FriendRefreshRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	{
		RECT Dot = {PanelL + 24, 116, PanelL + 34, 126};
		FillRoundRect(Mem, Dot, 10, RGB(90, 210, 130));
		SelectObject(Mem, BodyFont);
		SetTextColor(Mem, RGB(120, 220, 155));
		wchar_t aOnline[64];
		_snwprintf_s(aOnline, _TRUNCATE, L"%d online", OnlineCount);
		TextOutW(Mem, PanelL + 46, 111, aOnline, (int)wcslen(aOnline));
	}

	SelectObject(Mem, SmallFont);
	SetTextColor(Mem, C_MUTED);
	TextOutW(Mem, PanelL + 24, 143, L"Double-click a friend to join", 29);

	RECT Div = {PanelL + 24, 170, WND_W - 48, 171};
	HBRUSH DivBrush = CreateSolidBrush(C_BORDER);
	FillRect(Mem, &Div, DivBrush);
	DeleteObject(DivBrush);

	const int RowH = 62;
	const int VisibleH = g_FriendAreaRc.bottom - g_FriendAreaRc.top;
	const int MaxScroll = (int)Friends.size() * RowH > VisibleH ? (int)Friends.size() * RowH - VisibleH : 0;
	if(g_FriendScroll > MaxScroll)
		g_FriendScroll = MaxScroll;
	if(g_FriendScroll < 0)
		g_FriendScroll = 0;

	HRGN Clip = CreateRectRgn(g_FriendAreaRc.left, g_FriendAreaRc.top, g_FriendAreaRc.right, g_FriendAreaRc.bottom);
	SelectClipRgn(Mem, Clip);

	if(FriendsLoading && !FriendsLoaded)
	{
		SelectObject(Mem, BodyFont);
		SetTextColor(Mem, C_MUTED);
		TextOutW(Mem, g_FriendAreaRc.left + 8, g_FriendAreaRc.top + 14, L"Finding friends", 15);
		// Three dots pulsing in sequence.
		for(int i = 0; i < 3; ++i)
		{
			const float Ph = g_AnimSpin * 2.2f - i * 0.35f;
			const float Pulse = 0.35f + 0.65f * (0.5f + 0.5f * sinf(Ph));
			RECT D = {g_FriendAreaRc.left + 128 + i * 12, g_FriendAreaRc.top + 26,
				g_FriendAreaRc.left + 134 + i * 12, g_FriendAreaRc.top + 32};
			FillRoundRect(Mem, D, 6, LerpColor(RGB(40, 42, 50), C_ACCENT, Pulse));
		}
	}
	else if(Friends.empty())
	{
		SelectObject(Mem, BodyFont);
		SetTextColor(Mem, C_DIM);
		TextOutW(Mem, g_FriendAreaRc.left + 8, g_FriendAreaRc.top + 14, L"No friends yet", 14);
		SelectObject(Mem, SmallFont);
		SetTextColor(Mem, C_MUTED);
		TextOutW(Mem, g_FriendAreaRc.left + 8, g_FriendAreaRc.top + 44, L"Add friends in the game client.", 31);
	}
	else
	{
		for(int i = 0; i < (int)Friends.size(); ++i)
		{
			const int Y = g_FriendAreaRc.top - g_FriendScroll + i * RowH;
			Friends[i].HitRc = {g_FriendAreaRc.left, Y, g_FriendAreaRc.right, Y + RowH - 6};
			if(Y + RowH < g_FriendAreaRc.top || Y > g_FriendAreaRc.bottom)
				continue;

			const bool Hover = (g_FriendHover == i);
			const float Hi = Hover ? g_AnimFriend : 0.0f;
			int Slide = 0;
			if(Hi > 0.01f && Friends[i].Online)
			{
				FillRoundRect(Mem, Friends[i].HitRc, 12, LerpColor(RGB(20, 22, 28), RGB(40, 44, 58), Hi));
				Slide = (int)(Hi * 4.0f);
				RECT Edge = {Friends[i].HitRc.left, Friends[i].HitRc.top + 10, Friends[i].HitRc.left + 3, Friends[i].HitRc.bottom - 10};
				FillRoundRect(Mem, Edge, 2, C_ACCENT);
			}

			const int TextL = Friends[i].HitRc.left + 34 + Slide;
			RECT Dot = {Friends[i].HitRc.left + 12 + Slide, Y + 22, Friends[i].HitRc.left + 22 + Slide, Y + 32};
			FillRoundRect(Mem, Dot, 10, Friends[i].Online ? RGB(90, 210, 130) : RGB(88, 90, 100));

			const std::wstring NameW = Utf8ToWide(Friends[i].Name.c_str());
			SelectObject(Mem, BodyFont);
			SetTextColor(Mem, Friends[i].Online ? C_TITLE : C_DIM);
			TextOutW(Mem, TextL, Y + 10, NameW.c_str(), (int)NameW.size());

			SelectObject(Mem, SmallFont);
			SetTextColor(Mem, Friends[i].Online ? C_DIM : C_MUTED);
			std::wstring Sub;
			if(Friends[i].Online)
			{
				if(!Friends[i].MapName.empty() && !Friends[i].ServerName.empty())
				{
					Sub = Utf8ToWide(Friends[i].MapName.c_str());
					Sub += L" · ";
					Sub += Utf8ToWide(Friends[i].ServerName.c_str());
				}
				else if(!Friends[i].MapName.empty())
					Sub = Utf8ToWide(Friends[i].MapName.c_str());
				else if(!Friends[i].ServerName.empty())
					Sub = Utf8ToWide(Friends[i].ServerName.c_str());
				else
					Sub = L"In a server";
				if(Sub.size() > 36)
					Sub = Sub.substr(0, 35) + L"…";
			}
			else
			{
				Sub = L"Offline";
			}
			TextOutW(Mem, TextL, Y + 34, Sub.c_str(), (int)Sub.size());
		}
	}
	SelectClipRgn(Mem, nullptr);
	DeleteObject(Clip);

	EnterCriticalSection(&g_Lock);
	if(g_Friends.size() == Friends.size())
	{
		for(size_t i = 0; i < Friends.size(); ++i)
			g_Friends[i].HitRc = Friends[i].HitRc;
	}
	LeaveCriticalSection(&g_Lock);

	if(g_ShowSettings)
	{
		SelectObject(Mem, HugeFont);
		SetTextColor(Mem, C_TITLE);
		TextOutW(Mem, ContentL, 76, L"Settings", 8);
		SelectObject(Mem, BodyFont);
		SetTextColor(Mem, C_MUTED);
		TextOutW(Mem, ContentL, 140, L"Launch options", 14);

		const bool Checked = g_AutoLaunch;
		RECT Box = {g_CheckRc.left, g_CheckRc.top + 4, g_CheckRc.left + 26, g_CheckRc.top + 30};
		FillRoundRect(Mem, Box, 7, Checked ? C_ACCENT : C_BAR_TRACK);
		if(Checked)
		{
			HPEN Pen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
			HGDIOBJ OldPen = SelectObject(Mem, Pen);
			MoveToEx(Mem, Box.left + 6, Box.top + 13, nullptr);
			LineTo(Mem, Box.left + 11, Box.top + 18);
			LineTo(Mem, Box.left + 19, Box.top + 7);
			SelectObject(Mem, OldPen);
			DeleteObject(Pen);
		}
		SelectObject(Mem, BodyFont);
		SetTextColor(Mem, C_TITLE);
		TextOutW(Mem, Box.right + 16, g_CheckRc.top + 4, L"Launch game automatically", 25);
		SelectObject(Mem, SmallFont);
		SetTextColor(Mem, C_MUTED);
		TextOutW(Mem, Box.right + 16, g_CheckRc.top + 34, L"After the update check, start without pressing Play.", 52);

		const bool AutoUpdateChecked = g_AutoUpdate;
		RECT AutoUpdateBox = {g_AutoUpdateCheckRc.left, g_AutoUpdateCheckRc.top + 4, g_AutoUpdateCheckRc.left + 26, g_AutoUpdateCheckRc.top + 30};
		FillRoundRect(Mem, AutoUpdateBox, 7, AutoUpdateChecked ? C_ACCENT : C_BAR_TRACK);
		if(AutoUpdateChecked)
		{
			HPEN Pen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
			HGDIOBJ OldPen = SelectObject(Mem, Pen);
			MoveToEx(Mem, AutoUpdateBox.left + 6, AutoUpdateBox.top + 13, nullptr);
			LineTo(Mem, AutoUpdateBox.left + 11, AutoUpdateBox.top + 18);
			LineTo(Mem, AutoUpdateBox.left + 19, AutoUpdateBox.top + 7);
			SelectObject(Mem, OldPen);
			DeleteObject(Pen);
		}
		SelectObject(Mem, BodyFont);
		SetTextColor(Mem, C_TITLE);
		TextOutW(Mem, AutoUpdateBox.right + 16, g_AutoUpdateCheckRc.top + 4, L"Install updates automatically", 28);
		SelectObject(Mem, SmallFont);
		SetTextColor(Mem, C_MUTED);
		TextOutW(Mem, AutoUpdateBox.right + 16, g_AutoUpdateCheckRc.top + 34, L"On launcher startup only. Mid-session updates stay manual.", 56);

		SetTextColor(Mem, C_MUTED);
		TextOutW(Mem, ContentL, 392, L"Integrations", 12);

		const bool DiscordChecked = g_DiscordRpc;
		RECT DiscordBox = {g_DiscordCheckRc.left, g_DiscordCheckRc.top + 4, g_DiscordCheckRc.left + 26, g_DiscordCheckRc.top + 30};
		FillRoundRect(Mem, DiscordBox, 7, DiscordChecked ? C_ACCENT : C_BAR_TRACK);
		if(DiscordChecked)
		{
			HPEN Pen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
			HGDIOBJ OldPen = SelectObject(Mem, Pen);
			MoveToEx(Mem, DiscordBox.left + 6, DiscordBox.top + 13, nullptr);
			LineTo(Mem, DiscordBox.left + 11, DiscordBox.top + 18);
			LineTo(Mem, DiscordBox.left + 19, DiscordBox.top + 7);
			SelectObject(Mem, OldPen);
			DeleteObject(Pen);
		}
		SelectObject(Mem, BodyFont);
		SetTextColor(Mem, C_TITLE);
		TextOutW(Mem, DiscordBox.right + 16, g_DiscordCheckRc.top + 4, L"Show Discord activity", 21);
		SelectObject(Mem, SmallFont);
		SetTextColor(Mem, C_MUTED);
		TextOutW(Mem, DiscordBox.right + 16, g_DiscordCheckRc.top + 34, L"Display in-game status in Discord. Restart the client to apply.", 63);

		FillRoundRect(Mem, g_BackRc, 20, g_BackHover ? C_PANEL2 : C_BTN_DISABLED);
		StrokeRoundRect(Mem, g_BackRc, 20, C_BORDER);
		SelectObject(Mem, BodyFont);
		SetTextColor(Mem, C_TITLE);
		SIZE Sz = {};
		GetTextExtentPoint32W(Mem, L"Back", 4, &Sz);
		TextOutW(Mem, g_BackRc.left + (g_BackRc.right - g_BackRc.left - Sz.cx) / 2,
			g_BackRc.top + (g_BackRc.bottom - g_BackRc.top - Sz.cy) / 2, L"Back", 4);
	}
	else
	{
		RECT TabPill = {g_TabOverviewRc.left, g_TabOverviewRc.top, g_TabUpdatesRc.right, g_TabUpdatesRc.bottom};
		const int PillR = (TabPill.bottom - TabPill.top);
		FillRoundRect(Mem, TabPill, PillR, RGB(24, 26, 34));
		StrokeRoundRect(Mem, TabPill, PillR, C_BORDER);

		SelectObject(Mem, TabFont);
		SIZE OvSz = {}, UpSz = {};
		GetTextExtentPoint32W(Mem, L"Overview", 8, &OvSz);
		GetTextExtentPoint32W(Mem, L"Updates", 7, &UpSz);
		const int OvX = g_TabOverviewRc.left + (g_TabOverviewRc.right - g_TabOverviewRc.left - OvSz.cx) / 2;
		const int UpX = g_TabUpdatesRc.left + (g_TabUpdatesRc.right - g_TabUpdatesRc.left - UpSz.cx) / 2;
		const int TabTextY = g_TabOverviewRc.top + (g_TabOverviewRc.bottom - g_TabOverviewRc.top - OvSz.cy) / 2 - 3;

		SetTextColor(Mem, LerpColor(C_TITLE, C_MUTED, g_AnimTab));
		TextOutW(Mem, OvX, TabTextY, L"Overview", 8);
		SetTextColor(Mem, LerpColor(C_MUTED, C_TITLE, g_AnimTab));
		TextOutW(Mem, UpX, TabTextY, L"Updates", 7);

		const int IntroDrop = (int)((1.0f - g_AnimIntro) * 14.0f);

		if(g_MainTab == EMainTab::Overview)
		{
			if(g_hLogoBmp)
			{
				const int LogoDrawW = 420;
				const int LogoDrawH = g_LogoH > 0 ? (LogoDrawW * g_LogoH) / g_LogoW : 104;
				DrawBitmapAlpha(Mem, g_hLogoBmp, ContentL, 150 - IntroDrop, LogoDrawW, LogoDrawH, g_LogoW, g_LogoH);
			}
			else
			{
				SelectObject(Mem, HugeFont);
				SetTextColor(Mem, C_TITLE);
				TextOutW(Mem, ContentL, 160 - IntroDrop, L"UClient", 7);
			}

			SelectObject(Mem, BodyFont);
			if(aStatus[0] && g_Phase != EUiPhase::Checking && g_Phase != EUiPhase::Launching && g_Phase != EUiPhase::Updating)
			{
				SelectObject(Mem, SmallFont);
				SetTextColor(Mem, g_Failed ? C_ERROR : C_MUTED);
				TextOutW(Mem, ContentL + 4, 312, aStatus, (int)wcslen(aStatus));
			}
		}
		else
		{
			SelectObject(Mem, HugeFont);
			SetTextColor(Mem, C_TITLE);
			TextOutW(Mem, ContentL, 150 - IntroDrop, L"Updates", 7);
			SelectObject(Mem, TitleFont);
			SetTextColor(Mem, C_DIM);
			TextOutW(Mem, ContentL + 2, 214, aVersion, (int)wcslen(aVersion));
			SelectObject(Mem, BodyFont);
			SetTextColor(Mem, C_MUTED);
			const wchar_t *pState = L"Checking for updates...";
			if(g_Phase == EUiPhase::Ready)
				pState = g_Failed ? L"Update check had a problem — you can still play." : L"You are up to date.";
			else if(g_Phase == EUiPhase::Updating)
				pState = L"Downloading and applying update...";
			else if(g_Phase == EUiPhase::Launching)
				pState = L"Launching game...";
			TextOutW(Mem, ContentL + 2, 262, pState, (int)wcslen(pState));
			if(aStatus[0] && g_Phase != EUiPhase::Checking && g_Phase != EUiPhase::Launching)
			{
				SelectObject(Mem, SmallFont);
				SetTextColor(Mem, g_Failed ? C_ERROR : C_MUTED);
				TextOutW(Mem, ContentL + 2, 300, aStatus, (int)wcslen(aStatus));
			}

			RECT Track = {ContentL + 2, 344, ContentL + 442, 354};
			if(g_Phase == EUiPhase::Updating)
			{
				FillRoundRect(Mem, Track, 5, C_BAR_TRACK);
				const int Pct = g_Percent.load();
				const int FillW = ((Track.right - Track.left) * Pct) / 100;
				if(FillW > 0)
				{
					RECT Fill = Track;
					Fill.right = Fill.left + FillW;
					FillRoundRect(Mem, Fill, 5, C_BAR_FILL);
				}
			}
			else if(g_Phase == EUiPhase::Checking)
			{
				// Indeterminate sweep while the version check runs.
				FillRoundRect(Mem, Track, 5, C_BAR_TRACK);
				const int TrackW = Track.right - Track.left;
				const int SweepW = 120;
				const float T = 0.5f + 0.5f * sinf(g_AnimSpin * 1.6f);
				RECT Sweep = Track;
				Sweep.left = Track.left + (int)((TrackW - SweepW) * T);
				Sweep.right = Sweep.left + SweepW;
				FillRoundRect(Mem, Sweep, 5, C_ACCENT_DIM);
			}
		}

		const bool CanUpdate = g_Phase == EUiPhase::Ready && EffectiveUpdateAvailable() && !EffectivePlayBlocked() && !EffectiveGameRunning();
		const bool CanPlay = g_Phase == EUiPhase::Ready && !EffectivePlayBlocked() && !EffectiveUpdateAvailable();
		const bool BtnActive = CanPlay || CanUpdate;
		const bool IsRunning = g_Phase == EUiPhase::Launching;
		const bool ShowBar = g_Phase == EUiPhase::Updating || g_Phase == EUiPhase::Checking;
		const int BtnH = g_PlayBtnRc.bottom - g_PlayBtnRc.top;
		const int BtnR = BtnActive ? 22 : 20;

		std::wstring AlertTitle;
		EnterCriticalSection(&g_Lock);
		if(!g_Notices.empty())
			AlertTitle = Utf8ToWide(g_Notices[0].Title.c_str());
		LeaveCriticalSection(&g_Lock);
		if(!AlertTitle.empty())
		{
			SelectObject(Mem, SmallFont);
			SetTextColor(Mem, C_ERROR);
			TextOutW(Mem, g_PlayBtnRc.left, g_PlayBtnRc.top - 22, AlertTitle.c_str(), (int)AlertTitle.size());
		}

		COLORREF BtnColor = BtnActive ? LerpColor(C_ACCENT, C_ACCENT_HOVER, g_AnimPlay) : (g_Phase == EUiPhase::Updating ? C_ACCENT_DIM : C_BTN_DISABLED);
		if(BtnActive)
			DrawGlow(Mem, (g_PlayBtnRc.left + g_PlayBtnRc.right) / 2, (g_PlayBtnRc.top + g_PlayBtnRc.bottom) / 2,
				64 + (int)(g_AnimPlay * 28.0f), C_ACCENT, (BYTE)(42 + (int)(g_AnimPlay * 48.0f)));
		FillRoundRect(Mem, g_PlayBtnRc, BtnR, BtnColor);

		SelectObject(Mem, BtnFont);
		SIZE BtnSz = {};
		GetTextExtentPoint32W(Mem, aButton, (int)wcslen(aButton), &BtnSz);
		const int IconW = 34;
		const int GroupW = IconW + BtnSz.cx;
		const int GroupL = g_PlayBtnRc.left + (g_PlayBtnRc.right - g_PlayBtnRc.left - GroupW) / 2;

		if(CanPlay)
		{
			const int Cx = GroupL + 12;
			const int Cy = (g_PlayBtnRc.top + g_PlayBtnRc.bottom) / 2 - (ShowBar ? 6 : 0);
			HBRUSH PlayBrush = CreateSolidBrush(RGB(255, 255, 255));
			HGDIOBJ OldBrush = SelectObject(Mem, PlayBrush);
			HPEN PlayPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
			HGDIOBJ OldPen = SelectObject(Mem, PlayPen);
			POINT Tri[3] = {{Cx - 4, Cy - 9}, {Cx - 4, Cy + 9}, {Cx + 10, Cy}};
			Polygon(Mem, Tri, 3);
			SelectObject(Mem, OldBrush);
			SelectObject(Mem, OldPen);
			DeleteObject(PlayPen);
			DeleteObject(PlayBrush);
		}

		SetTextColor(Mem, CanPlay || IsRunning ? RGB(255, 255, 255) : C_DIM);
		const int BtnTextY = g_PlayBtnRc.top + (BtnH - BtnSz.cy) / 2 - (ShowBar ? 7 : 0);
		const int BtnTextX = CanPlay ? (GroupL + IconW) : (g_PlayBtnRc.left + (g_PlayBtnRc.right - g_PlayBtnRc.left - BtnSz.cx) / 2);
		TextOutW(Mem, BtnTextX, BtnTextY, aButton, (int)wcslen(aButton));

		if(ShowBar)
		{
			RECT Track = {g_PlayBtnRc.left + 24, g_PlayBtnRc.bottom - 17, g_PlayBtnRc.right - 24, g_PlayBtnRc.bottom - 12};
			FillRoundRect(Mem, Track, 3, RGB(45, 38, 90));
			const int Pct = g_Percent.load();
			const int FillW = ((Track.right - Track.left) * Pct) / 100;
			if(FillW > 0)
			{
				RECT Fill = Track;
				Fill.right = Fill.left + FillW;
				FillRoundRect(Mem, Fill, 3, RGB(255, 240, 180));
			}
		}

		SelectObject(Mem, SmallFont);
		SetTextColor(Mem, C_DIM);
		TextOutW(Mem, g_PlayBtnRc.left + 2, g_PlayBtnRc.bottom + 8, aVersion, (int)wcslen(aVersion));
	}

	SelectObject(Mem, GetStockObject(SYSTEM_FONT));
	BitBlt(Dc, 0, 0, WND_W, WND_H, Mem, 0, 0, SRCCOPY);
	SelectObject(Mem, Old);
	DeleteObject(Bmp);
	DeleteDC(Mem);
	EndPaint(hWnd, &Ps);
}

static int FriendHitIndex(int X, int Y)
{
	if(!PtInRectI(g_FriendAreaRc, X, Y) || g_ShowSettings)
		return -1;
	EnterCriticalSection(&g_Lock);
	int Hit = -1;
	for(int i = 0; i < (int)g_Friends.size(); ++i)
	{
		if(PtInRectI(g_Friends[i].HitRc, X, Y))
		{
			Hit = i;
			break;
		}
	}
	LeaveCriticalSection(&g_Lock);
	return Hit;
}

static bool IsInteractiveHit(int X, int Y)
{
	if(PtInRectI(g_CloseRc, X, Y) || PtInRectI(g_MinRc, X, Y) || PtInRectI(g_GearRc, X, Y))
		return true;
	if(g_ShowSettings)
		return PtInRectI(g_CheckRc, X, Y) || PtInRectI(g_AutoUpdateCheckRc, X, Y) || PtInRectI(g_DiscordCheckRc, X, Y) || PtInRectI(g_BackRc, X, Y);
	if(PtInRectI(g_TabOverviewRc, X, Y) || PtInRectI(g_TabUpdatesRc, X, Y))
		return true;
	if(PtInRectI(g_PlayBtnRc, X, Y))
		return true;
	if(FriendHitIndex(X, Y) >= 0)
		return true;
	return false;
}

// Advances every animated value one frame; true means another frame is needed.
static bool StepAnimations()
{
	bool Active = false;
	Active |= Approach(g_AnimIntro, 1.0f, 0.16f);
	Active |= Approach(g_AnimPlay, g_PlayHover ? 1.0f : 0.0f, 0.28f);
	Active |= Approach(g_AnimGear, (g_GearHover || g_ShowSettings) ? 1.0f : 0.0f, 0.24f);
	Active |= Approach(g_AnimTab, g_MainTab == EMainTab::Updates ? 1.0f : 0.0f, 0.22f);

	if(g_FriendHover != g_AnimFriendRow)
	{
		g_AnimFriendRow = g_FriendHover;
		g_AnimFriend = 0.0f;
	}
	Active |= Approach(g_AnimFriend, g_FriendHover >= 0 ? 1.0f : 0.0f, 0.3f);

	const bool Busy = g_Phase == EUiPhase::Checking || g_Phase == EUiPhase::Updating || (g_FriendsLoading && !g_FriendsLoaded);
	if(Busy)
	{
		g_AnimSpin += 0.09f;
		if(g_AnimSpin > 62.831853f)
			g_AnimSpin -= 62.831853f;
		Active = true;
	}
	return Active;
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg)
	{
	case WM_PAINT:
		if(g_WebUi)
		{
			// The WebView2 controller covers the client area; just make sure
			// nothing bright shows through before it is visible.
			PAINTSTRUCT Ps;
			HDC Dc = BeginPaint(hWnd, &Ps);
			HBRUSH Brush = CreateSolidBrush(RGB(6, 7, 10));
			FillRect(Dc, &Ps.rcPaint, Brush);
			DeleteObject(Brush);
			EndPaint(hWnd, &Ps);
			return 0;
		}
		Paint(hWnd);
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_SIZE:
#ifdef UCLIENT_LAUNCHER_WEBVIEW
		if(g_WebUi)
			WebUi::Resize(hWnd);
#endif
		return 0;
	case WM_TIMER:
		if(wParam == ANIM_TIMER_ID)
		{
			if(g_WebUi)
				PushWebState();
			else if(StepAnimations())
				InvalidateRect(hWnd, nullptr, FALSE);
		}
		else if(wParam == LAUNCH_TIMER_ID)
		{
			PollLaunchProcess();
		}
		else if(wParam == NOTICES_TIMER_ID)
		{
			RequestNoticesRefresh();
		}
		else if(wParam == UPDATE_CHECK_TIMER_ID)
		{
			RequestUpdateCheck();
		}
		else if(wParam == GAME_POLL_TIMER_ID)
		{
			const bool WasRunning = g_GameRunning;
			RefreshGameRunningState();
			if(WasRunning != g_GameRunning)
			{
				SyncButtonHint();
				PushWebState(true);
			}
		}
		return 0;
	case WM_SETCURSOR:
	{
		if(g_WebUi)
			return DefWindowProcW(hWnd, Msg, wParam, lParam);
		POINT Pt;
		GetCursorPos(&Pt);
		ScreenToClient(hWnd, &Pt);
		bool Hand = false;
		if(PtInRectI(g_CloseRc, Pt.x, Pt.y) || PtInRectI(g_MinRc, Pt.x, Pt.y) || PtInRectI(g_GearRc, Pt.x, Pt.y))
			Hand = true;
		else if(g_ShowSettings)
			Hand = PtInRectI(g_CheckRc, Pt.x, Pt.y) || PtInRectI(g_AutoUpdateCheckRc, Pt.x, Pt.y) || PtInRectI(g_DiscordCheckRc, Pt.x, Pt.y) || PtInRectI(g_BackRc, Pt.x, Pt.y);
		else if(PtInRectI(g_TabOverviewRc, Pt.x, Pt.y) || PtInRectI(g_TabUpdatesRc, Pt.x, Pt.y))
			Hand = true;
		else if(PtInRectI(g_PlayBtnRc, Pt.x, Pt.y) && g_Phase == EUiPhase::Ready && !EffectivePlayBlocked() &&
			(!EffectiveUpdateAvailable() || !EffectiveGameRunning()))
			Hand = true;
		else
		{
			const int Fi = FriendHitIndex(Pt.x, Pt.y);
			if(Fi >= 0)
			{
				EnterCriticalSection(&g_Lock);
				Hand = Fi < (int)g_Friends.size() && g_Friends[Fi].Online && !g_Friends[Fi].Address.empty();
				LeaveCriticalSection(&g_Lock);
			}
		}
		if(Hand)
		{
			SetCursor(LoadCursor(nullptr, IDC_HAND));
			return TRUE;
		}
		SetCursor(LoadCursor(nullptr, IDC_ARROW));
		return TRUE;
	}
	case WM_SHOW_LAUNCHER:
		ShowLauncherWindow(hWnd);
		return 0;
	case WM_WORKER_TICK:
		InvalidateRect(hWnd, nullptr, FALSE);
		PushWebState();
		return 0;
	case WM_FRIENDS_READY:
		InvalidateRect(hWnd, nullptr, FALSE);
		PushWebState();
		return 0;
	case WM_NOTICES_READY:
		InvalidateRect(hWnd, nullptr, FALSE);
		SyncButtonHint();
		PushWebState(true);
		return 0;
	case WM_UPDATE_CHECK_READY:
		InvalidateRect(hWnd, nullptr, FALSE);
		PushWebState(true);
		return 0;
	case WM_UPDATE_READY:
		InvalidateRect(hWnd, nullptr, FALSE);
		PushWebState();
		RequestFriendsRefresh();
		RequestNoticesRefresh();
		TryStartupAutoUpdate();
		if(g_AutoLaunch && g_LaunchedFromGame && g_Phase == EUiPhase::Ready && !EffectivePlayBlocked() && !EffectiveUpdateAvailable() && g_pArgs && g_pArgs->ApplyArchive.empty())
			RequestLaunchGame();
		return 0;
	case WM_WORKER_DONE:
		DestroyWindow(hWnd);
		return 0;
	case WM_DESTROY:
		KillTimer(hWnd, ANIM_TIMER_ID);
		KillTimer(hWnd, LAUNCH_TIMER_ID);
		KillTimer(hWnd, NOTICES_TIMER_ID);
		KillTimer(hWnd, UPDATE_CHECK_TIMER_ID);
		KillTimer(hWnd, GAME_POLL_TIMER_ID);
		CloseLaunchedGameHandle();
#ifdef UCLIENT_LAUNCHER_WEBVIEW
		WebUi::Shutdown();
#endif
		FreeBackground();
		FreeFonts();
		FreeLauncherArt();
		delete g_pArgs;
		g_pArgs = nullptr;
		PostQuitMessage(0);
		return 0;
	case WM_MOUSEWHEEL:
	{
		const int Delta = GET_WHEEL_DELTA_WPARAM(wParam);
		g_FriendScroll -= (Delta / WHEEL_DELTA) * 40;
		if(g_FriendScroll < 0)
			g_FriendScroll = 0;
		InvalidateRect(hWnd, nullptr, FALSE);
		return 0;
	}
	case WM_MOUSEMOVE:
	{
		TRACKMOUSEEVENT Tme = {};
		Tme.cbSize = sizeof(Tme);
		Tme.dwFlags = TME_LEAVE;
		Tme.hwndTrack = hWnd;
		TrackMouseEvent(&Tme);

		const int X = (short)LOWORD(lParam);
		const int Y = (short)HIWORD(lParam);
		const bool PlayHover = !g_ShowSettings && PtInRectI(g_PlayBtnRc, X, Y) && g_Phase == EUiPhase::Ready && !EffectivePlayBlocked() &&
			(!EffectiveUpdateAvailable() || !EffectiveGameRunning());
		const bool GearHover = PtInRectI(g_GearRc, X, Y);
		const bool BackHover = g_ShowSettings && PtInRectI(g_BackRc, X, Y);
		const bool MinHover = PtInRectI(g_MinRc, X, Y);
		const bool CloseHover = PtInRectI(g_CloseRc, X, Y);
		const bool FriendRefreshHover = !g_ShowSettings && PtInRectI(g_FriendRefreshRc, X, Y) && !g_FriendsLoading;
		const int FriendHover = FriendHitIndex(X, Y);
		if(PlayHover != g_PlayHover || GearHover != g_GearHover || BackHover != g_BackHover ||
			MinHover != g_MinHover || CloseHover != g_CloseHover || FriendRefreshHover != g_FriendRefreshHover || FriendHover != g_FriendHover)
		{
			g_PlayHover = PlayHover;
			g_GearHover = GearHover;
			g_BackHover = BackHover;
			g_MinHover = MinHover;
			g_CloseHover = CloseHover;
			g_FriendRefreshHover = FriendRefreshHover;
			g_FriendHover = FriendHover;
			InvalidateRect(hWnd, nullptr, FALSE);
		}
		return 0;
	}
	case WM_MOUSELEAVE:
		g_PlayHover = g_GearHover = g_BackHover = g_MinHover = g_CloseHover = g_FriendRefreshHover = false;
		g_FriendHover = -1;
		InvalidateRect(hWnd, nullptr, FALSE);
		return 0;
	case WM_LBUTTONUP:
	{
		const int X = (short)LOWORD(lParam);
		const int Y = (short)HIWORD(lParam);
		if(PtInRectI(g_CloseRc, X, Y))
		{
			DestroyWindow(hWnd);
			return 0;
		}
		if(PtInRectI(g_MinRc, X, Y))
		{
			ShowWindow(hWnd, SW_MINIMIZE);
			return 0;
		}
		if(PtInRectI(g_GearRc, X, Y))
		{
			g_ShowSettings = !g_ShowSettings;
			InvalidateRect(hWnd, nullptr, FALSE);
			return 0;
		}
		if(g_ShowSettings)
		{
			if(PtInRectI(g_CheckRc, X, Y))
			{
				g_AutoLaunch = !g_AutoLaunch;
				SaveLauncherSettings(g_InstallDir);
				InvalidateRect(hWnd, nullptr, FALSE);
			}
			else if(PtInRectI(g_AutoUpdateCheckRc, X, Y))
			{
				g_AutoUpdate = !g_AutoUpdate;
				SaveLauncherSettings(g_InstallDir);
				InvalidateRect(hWnd, nullptr, FALSE);
			}
			else if(PtInRectI(g_DiscordCheckRc, X, Y))
			{
				SaveDiscordRpcSetting(!g_DiscordRpc);
				InvalidateRect(hWnd, nullptr, FALSE);
			}
			else if(PtInRectI(g_BackRc, X, Y))
			{
				g_ShowSettings = false;
				g_BackHover = false;
				InvalidateRect(hWnd, nullptr, FALSE);
			}
			return 0;
		}
		if(PtInRectI(g_FriendRefreshRc, X, Y))
		{
			RequestFriendsRefresh();
			InvalidateRect(hWnd, nullptr, FALSE);
			return 0;
		}
		if(PtInRectI(g_TabOverviewRc, X, Y))
		{
			g_MainTab = EMainTab::Overview;
			InvalidateRect(hWnd, nullptr, FALSE);
			return 0;
		}
		if(PtInRectI(g_TabUpdatesRc, X, Y))
		{
			g_MainTab = EMainTab::Updates;
			InvalidateRect(hWnd, nullptr, FALSE);
			return 0;
		}
		if(FriendHitIndex(X, Y) >= 0)
			return 0; // joining a friend needs a double-click
		if(PtInRectI(g_PlayBtnRc, X, Y) && g_Phase == EUiPhase::Ready && !EffectivePlayBlocked())
		{
			if(EffectiveUpdateAvailable() && !EffectiveGameRunning())
				RequestUpdateDownload();
			else if(!EffectiveUpdateAvailable())
				RequestLaunchGame();
		}
		return 0;
	}
	case WM_LBUTTONDBLCLK:
	{
		const int X = (short)LOWORD(lParam);
		const int Y = (short)HIWORD(lParam);
		const int Fi = FriendHitIndex(X, Y);
		if(Fi >= 0)
		{
			std::wstring Addr;
			EnterCriticalSection(&g_Lock);
			if(Fi < (int)g_Friends.size() && g_Friends[Fi].Online && !g_Friends[Fi].Address.empty())
				Addr = Utf8ToWide(g_Friends[Fi].Address.c_str());
			LeaveCriticalSection(&g_Lock);
			if(!Addr.empty() && g_Phase == EUiPhase::Ready && !EffectivePlayBlocked() && !EffectiveUpdateAvailable())
				RequestLaunchGame(Addr.c_str());
			return 0;
		}
		if(PtInRectI(g_PlayBtnRc, X, Y) && g_Phase == EUiPhase::Ready && !EffectivePlayBlocked())
		{
			if(EffectiveUpdateAvailable() && !EffectiveGameRunning())
				RequestUpdateDownload();
			else if(!EffectiveUpdateAvailable())
				RequestLaunchGame();
		}
		return 0;
	}
	case WM_NCLBUTTONDBLCLK:
		return 0; // dragging by the fake caption must not maximize
	case WM_NCHITTEST:
	{
		POINT Pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		ScreenToClient(hWnd, &Pt);
		if(IsInteractiveHit(Pt.x, Pt.y))
			return HTCLIENT;
		if(DefWindowProcW(hWnd, Msg, wParam, lParam) == HTCLIENT)
			return HTCAPTION;
		return DefWindowProcW(hWnd, Msg, wParam, lParam);
	}
	case WM_KEYDOWN:
		if(wParam == VK_ESCAPE)
		{
			if(g_ShowSettings)
			{
				g_ShowSettings = false;
				InvalidateRect(hWnd, nullptr, FALSE);
			}
			else if(g_Phase == EUiPhase::Ready)
				DestroyWindow(hWnd);
		}
		else if(wParam == VK_RETURN && g_Phase == EUiPhase::Ready && !g_ShowSettings && !EffectivePlayBlocked())
		{
			if(EffectiveUpdateAvailable() && !EffectiveGameRunning())
				RequestUpdateDownload();
			else if(!EffectiveUpdateAvailable())
				RequestLaunchGame();
		}
		return 0;
	}
	return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static std::wstring SingleInstanceMutexName(const std::wstring &InstallDir)
{
	std::wstring Name = L"Local\\UClientLauncher.";
	for(wchar_t Ch : InstallDir)
	{
		if(Ch == L'\\' || Ch == L'/')
			Name.push_back(L'_');
		else
			Name.push_back((wchar_t)towupper(Ch));
	}
	return Name;
}

static HWND FindLauncherWindow()
{
	return FindWindowW(L"UClientLauncher", L"UClient Launcher");
}

static void ShowLauncherWindow(HWND hWnd)
{
	if(!hWnd || !IsWindow(hWnd))
		return;
	if(g_ShowSettings)
	{
		g_ShowSettings = false;
		InvalidateRect(hWnd, nullptr, FALSE);
	}

	ShowWindow(hWnd, IsIconic(hWnd) ? SW_RESTORE : SW_SHOW);

	HWND hForeground = GetForegroundWindow();
	if(hForeground != hWnd)
	{
		const DWORD ForegroundThread = GetWindowThreadProcessId(hForeground, nullptr);
		const DWORD CurrentThread = GetCurrentThreadId();
		if(ForegroundThread != CurrentThread)
			AttachThreadInput(CurrentThread, ForegroundThread, TRUE);

		SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
		SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
		SetForegroundWindow(hWnd);
		SetFocus(hWnd);
		BringWindowToTop(hWnd);

		if(ForegroundThread != CurrentThread)
			AttachThreadInput(CurrentThread, ForegroundThread, FALSE);
	}

	FLASHWINFO Flash = {};
	Flash.cbSize = sizeof(Flash);
	Flash.hwnd = hWnd;
	Flash.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
	Flash.uCount = 3;
	FlashWindowEx(&Flash);
}

static void ActivateExistingLauncherWindow(HWND hWnd)
{
	if(!hWnd || !IsWindow(hWnd))
		return;

	DWORD ExistingPid = 0;
	GetWindowThreadProcessId(hWnd, &ExistingPid);
	if(ExistingPid != 0)
		AllowSetForegroundWindow(ExistingPid);

	DWORD_PTR Result = 0;
	SendMessageTimeoutW(hWnd, WM_SHOW_LAUNCHER, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 3000, &Result);
}

// Returns true when this process should continue starting a new launcher window.
static bool AcquireSingleInstanceOrActivateExisting(const std::wstring &InstallDir)
{
	const std::wstring MutexName = SingleInstanceMutexName(InstallDir);
	g_hSingleInstanceMutex = CreateMutexW(nullptr, TRUE, MutexName.c_str());
	if(!g_hSingleInstanceMutex)
		return true;

	if(GetLastError() == ERROR_ALREADY_EXISTS)
	{
		CloseHandle(g_hSingleInstanceMutex);
		g_hSingleInstanceMutex = nullptr;

		for(int Attempt = 0; Attempt < 40; ++Attempt)
		{
			HWND hExisting = FindLauncherWindow();
			if(hExisting)
			{
				ActivateExistingLauncherWindow(hExisting);
				break;
			}
			Sleep(50);
		}
		return false;
	}
	return true;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	InitializeCriticalSection(&g_Lock);

	int Argc = 0;
	LPWSTR *ppArgv = CommandLineToArgvW(GetCommandLineW(), &Argc);
	if(!ppArgv)
		return 1;

	wchar_t aSelf[MAX_PATH];
	GetModuleFileNameW(nullptr, aSelf, MAX_PATH);

	g_pArgs = new LauncherArgs();
	g_pArgs->SelfPath = aSelf;
	g_pArgs->InstallDir = ParentDir(aSelf);
	g_InstallDir = g_pArgs->InstallDir;
	for(int i = 1; i < Argc; ++i)
	{
		if(!wcscmp(ppArgv[i], kFromGameArg))
		{
			g_LaunchedFromGame = true;
			continue;
		}
		if(!wcscmp(ppArgv[i], kApplyUpdateArg) && i + 1 < Argc)
		{
			g_pArgs->ApplyArchive = ppArgv[++i];
			continue;
		}
		if(!wcscmp(ppArgv[i], kWaitPidArg) && i + 1 < Argc)
		{
			g_pArgs->WaitPid = (DWORD)_wtol(ppArgv[++i]);
			continue;
		}
		g_pArgs->ForwardArgs.emplace_back(ppArgv[i]);
	}
	LocalFree(ppArgv);

	LoadLauncherSettings(g_InstallDir);
	LoadLauncherArt(g_InstallDir);
	SetVersionLabel(ResolveLocalVersion(g_InstallDir));

	if(g_pArgs->ApplyArchive.empty() && !AcquireSingleInstanceOrActivateExisting(g_InstallDir))
	{
		delete g_pArgs;
		g_pArgs = nullptr;
		DeleteCriticalSection(&g_Lock);
		CoUninitialize();
		return 0;
	}

	WNDCLASSEXW Wc = {};
	Wc.cbSize = sizeof(Wc);
	Wc.style = CS_DBLCLKS;
	Wc.lpfnWndProc = WndProc;
	Wc.hInstance = hInst;
	Wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	Wc.hbrBackground = nullptr;
	Wc.lpszClassName = L"UClientLauncher";
	Wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
	if(!Wc.hIcon)
		Wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	Wc.hIconSm = Wc.hIcon;
	RegisterClassExW(&Wc);

	const int X = (GetSystemMetrics(SM_CXSCREEN) - WND_W) / 2;
	const int Y = (GetSystemMetrics(SM_CYSCREEN) - WND_H) / 2;
	g_hWnd = CreateWindowExW(WS_EX_APPWINDOW, L"UClientLauncher", L"UClient Launcher",
		WS_POPUP | WS_VISIBLE, X, Y, WND_W, WND_H, nullptr, nullptr, hInst, nullptr);
	if(!g_hWnd)
		return 1;
	ApplyWindowRoundCorners(g_hWnd);

#ifdef UCLIENT_LAUNCHER_WEBVIEW
	InitWebArtUrls(g_InstallDir);
	if(WebUi::Start(g_hWnd, WebViewUserDataDir(g_InstallDir), JoinPath(g_InstallDir, L"data"),
		   kLauncherHtml, OnWebMessage, OnWebFailed))
		g_WebUi = true;
#endif
	// 120 ms is plenty to mirror state into the page; the GDI renderer instead
	// needs a 60 fps tick because it animates by redrawing.
	SetTimer(g_hWnd, ANIM_TIMER_ID, g_WebUi ? 120 : 16, nullptr);
	SetTimer(g_hWnd, NOTICES_TIMER_ID, NOTICE_POLL_MS, nullptr);
	SetTimer(g_hWnd, UPDATE_CHECK_TIMER_ID, UPDATE_CHECK_POLL_MS, nullptr);
	SetTimer(g_hWnd, GAME_POLL_TIMER_ID, GAME_POLL_MS, nullptr);
	RequestFriendsRefresh();
	RequestNoticesRefresh();
	RequestUpdateCheck();

	HANDLE hThread = CreateThread(nullptr, 0, WorkerThread, g_pArgs, 0, nullptr);
	if(hThread)
		CloseHandle(hThread);

	MSG Msg;
	while(GetMessageW(&Msg, nullptr, 0, 0))
	{
		TranslateMessage(&Msg);
		DispatchMessageW(&Msg);
	}

	DeleteCriticalSection(&g_Lock);
	if(g_hSingleInstanceMutex)
	{
		CloseHandle(g_hSingleInstanceMutex);
		g_hSingleInstanceMutex = nullptr;
	}
	CoUninitialize();
	return 0;
}
