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
#include <memory>
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
#ifndef UCLIENT_CLIENT_VERSION
#define UCLIENT_CLIENT_VERSION "0.0.0"
#endif
#ifndef UCLIENT_UPDATE_LATEST_URL
#define UCLIENT_UPDATE_LATEST_URL "https://ddnet.under1111.com/api/uclient/update/latest"
#endif
#ifndef UCLIENT_CLIENT_UPDATE_LATEST_URL
#define UCLIENT_CLIENT_UPDATE_LATEST_URL "https://ddnet.under1111.com/uclient/client/latest.json"
#endif
#ifndef UCLIENT_LAUNCHER_UPDATE_LATEST_URL
#define UCLIENT_LAUNCHER_UPDATE_LATEST_URL "https://ddnet.under1111.com/uclient/launcher/latest.json"
#endif
#ifndef UCLIENT_API_BASE_URL
#define UCLIENT_API_BASE_URL "https://uclient.under1111.com"
#endif

static const wchar_t *kTokenArg = L"--uclient-from-launcher";
static const wchar_t *kTokenFile = L"uclient_launch.token";
static const wchar_t *kVersionFile = L"uclient_version.txt";
static const wchar_t *kClientPendingVersionFile = L"uclient_client_pending_version.txt";
static const wchar_t *kLegacyPendingVersionFile = L"uclient_pending_version.txt";
static const wchar_t *kApplyUpdateArg = L"--uclient-apply-update";
static const wchar_t *kWaitPidArg = L"--uclient-wait-pid";
static const wchar_t *kWriteVersionInfoArg = L"--write-version-info";
static const wchar_t *kLauncherUpdateEventArg = L"--uclient-launcher-update-event";
static const wchar_t *kFromGameArg = L"--uclient-from-game";
static const wchar_t *kGameExe = L"DDNet.exe";
static const wchar_t kPlayLabel[] = L"Play";
static const wchar_t kRunningLabel[] = L"RUNNING";
static const wchar_t *kClientArchiveRel = L"update\\uclient-client.zip";
static const wchar_t *kLegacyArchiveRel = L"update\\bestclient-release.zip";
static const wchar_t *kLauncherArchiveRel = L"update\\uclient-launcher.zip";
static const wchar_t *kBuildManifestFile = L"uclient_build_manifest.json";

static const wchar_t *k_aUserDirs[] = {
	L"data\\assets\\arrow",
	L"data\\assets\\arrows",
	L"data\\assets\\audio",
	L"data\\audio",
};

static const int REFERENCE_SCREEN_W = 2560;
static const int REFERENCE_SCREEN_H = 1440;
static const int REFERENCE_WND_W = 1280;
static const int REFERENCE_WND_H = 800;
static int g_WindowW = REFERENCE_WND_W;
static int g_WindowH = REFERENCE_WND_H;
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

enum class EAccountState
{
	Checking,
	NeedsOnboarding,
	ReadyAnonymous,
	ReadyEmail,
	Busy,
	Error,
	Banned,
};

struct BackupFileView
{
	std::string Path;
	uint64_t Size = 0;
};

struct BackupVersionView
{
	std::string Id;
	std::string Path;
	std::string CreatedAt;
	std::string Sha256;
	uint64_t Size = 0;
};

struct FriendView
{
	std::string Name;
	std::string Clan;
	bool Online = false;
	bool Afk = false;
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
	std::wstring VersionInfoPath;
	std::wstring LauncherUpdateEvent;
	DWORD WaitPid = 0;
};

struct UpdateMetadata
{
	std::string Component;
	std::string Version;
	std::string Url;
	std::string Sha256;
	std::string MinLauncherVersion;
	uint64_t Size = 0;
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
static UpdateMetadata g_PendingClientUpdate;
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
static EAccountState g_AccountState = EAccountState::Checking;
static std::string g_AccountEmail;
static std::string g_AccountError;
static std::string g_AccountInstallId;
static std::string g_AccountSecret;
static bool g_AccountWorkerRunning = false;
static bool g_AccountSignedOut = false;
static bool g_HasSavedAccount = false;
static std::string g_SavedAccountInstallId;
static bool g_BackupBusy = false;
static std::string g_BackupError;
static std::vector<BackupFileView> g_BackupFiles;
static std::vector<BackupVersionView> g_BackupVersions;
static uint64_t g_BackupUsed = 0;
static uint64_t g_BackupLimit = 0;
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
#define WM_ACCOUNT_READY (WM_APP + 7)
#define WM_BACKUP_READY (WM_APP + 8)
static constexpr ULONG_PTR COPYDATA_FORWARD_LAUNCH_ARG = 0x55434C46;

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
static void RequestAccountCheck();
static void ShowLauncherWindow(HWND hWnd);
static void ActivateExistingLauncherWindow(HWND hWnd);
static bool IsForwardableShellArg(const std::wstring &Arg);
static std::wstring SingleInstanceMutexName(const std::wstring &InstallDir);
static bool AcquireSingleInstanceOrActivateExisting(const std::wstring &InstallDir, const std::vector<std::wstring> &ForwardArgs);
static bool RunUpdateDownload(LauncherArgs *pA, const UpdateMetadata &Metadata);
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
	g_AccountSignedOut = false;
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
		g_AccountSignedOut = Text.find("account_signed_out=1") != std::string::npos;
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
	Text += g_AccountSignedOut ? "account_signed_out=1\n" : "account_signed_out=0\n";
	WriteTextFile(Path, Text);
}

// Prefer the on-disk client stamp and migrate the legacy pending filename once.
static std::string ResolveLocalClientVersion(const std::wstring &InstallDir)
{
	const std::wstring VersionPath = JoinPath(InstallDir, kVersionFile);
	const std::wstring PendingPath = JoinPath(InstallDir, kClientPendingVersionFile);
	const std::wstring LegacyPendingPath = JoinPath(InstallDir, kLegacyPendingVersionFile);
	const std::wstring ArchivePath = JoinPath(InstallDir, kClientArchiveRel);
	const std::wstring LegacyArchivePath = JoinPath(InstallDir, kLegacyArchiveRel);

	if(GetFileAttributesW(PendingPath.c_str()) == INVALID_FILE_ATTRIBUTES &&
		GetFileAttributesW(LegacyPendingPath.c_str()) != INVALID_FILE_ATTRIBUTES)
		MoveFileExW(LegacyPendingPath.c_str(), PendingPath.c_str(), MOVEFILE_REPLACE_EXISTING);

	std::string Pending;
	if(ReadTextFile(PendingPath, Pending))
	{
		// Updater deletes the zip after a successful apply. If the archive is gone,
		// treat pending as committed even when an older updater didn't write the stamp.
		if(GetFileAttributesW(ArchivePath.c_str()) == INVALID_FILE_ATTRIBUTES &&
			GetFileAttributesW(LegacyArchivePath.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			WriteTextFile(VersionPath, Pending);
			DeleteFileW(PendingPath.c_str());
		}
	}

	std::string OnDisk;
	if(ReadTextFile(VersionPath, OnDisk))
	{
		if(CompareVersions(OnDisk, UCLIENT_CLIENT_VERSION) >= 0)
			return OnDisk;
	}
	return UCLIENT_CLIENT_VERSION;
}

static void CommitPendingVersion(const std::wstring &InstallDir)
{
	const std::wstring PendingPath = JoinPath(InstallDir, kClientPendingVersionFile);
	const std::wstring VersionPath = JoinPath(InstallDir, kVersionFile);
	if(GetFileAttributesW(PendingPath.c_str()) == INVALID_FILE_ATTRIBUTES)
		return;
	DeleteFileW(VersionPath.c_str());
	MoveFileExW(PendingPath.c_str(), VersionPath.c_str(), MOVEFILE_REPLACE_EXISTING);
}

static void FinalizePendingLauncherUpdate(const std::wstring &InstallDir, const std::wstring &SelfPath)
{
	const std::wstring PendingPath = JoinPath(InstallDir, L"update\\launcher.pending");
	std::string PendingVersion;
	if(!ReadTextFile(PendingPath, PendingVersion) ||
		CompareVersions(UCLIENT_LAUNCHER_VERSION, PendingVersion) < 0)
		return;
	const std::wstring OldPath = SelfPath + L".old";
	if(GetFileAttributesW(OldPath.c_str()) != INVALID_FILE_ATTRIBUTES && !DeleteFileW(OldPath.c_str()))
		MoveFileExW(OldPath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
	DeleteFileW(PendingPath.c_str());
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
		if(Fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
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

static bool SafeArchiveEntry(const std::wstring &Entry)
{
	if(Entry.empty() || Entry[0] == L'/' || Entry[0] == L'\\' || Entry.find(L':') != std::wstring::npos)
		return false;
	std::wstring Normal = Entry;
	std::replace(Normal.begin(), Normal.end(), L'\\', L'/');
	size_t Start = 0;
	while(Start <= Normal.size())
	{
		const size_t End = Normal.find(L'/', Start);
		const std::wstring Part = Normal.substr(Start, End == std::wstring::npos ? std::wstring::npos : End - Start);
		if(Part == L"..")
			return false;
		if(End == std::wstring::npos)
			break;
		Start = End + 1;
	}
	return true;
}

static bool ValidateArchiveEntries(const std::wstring &ArchivePath, bool AllowLauncherExecutable)
{
	wchar_t aCommand[1024];
	_snwprintf_s(aCommand, _TRUNCATE, L"tar.exe -tf \"%ls\"", ArchivePath.c_str());
	bool Valid = true;
	int Entries = 0;
	const int ExitCode = RunProcess(aCommand, [&](const wchar_t *pLine) {
		const std::wstring Entry = pLine;
		if(Entry.empty())
			return;
		++Entries;
		if(!SafeArchiveEntry(Entry))
		{
			Valid = false;
			return;
		}
		std::wstring Normal = Entry;
		std::replace(Normal.begin(), Normal.end(), L'\\', L'/');
		const size_t Slash = Normal.find_last_of(L'/');
		std::wstring Name = Normal.substr(Slash == std::wstring::npos ? 0 : Slash + 1);
		std::transform(Name.begin(), Name.end(), Name.begin(), [](wchar_t Ch) { return (wchar_t)towlower(Ch); });
		if(!AllowLauncherExecutable && Name == L"uclient.exe")
			Valid = false;
	});
	return ExitCode == 0 && Entries > 0 && Valid;
}

static bool ExtractJsonString(const std::string &Json, const char *Key, std::string &Out);

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
			if((Fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				!(Fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
				DeleteTree(Full.c_str());
			else if(Fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				RemoveDirectoryW(Full.c_str());
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
		if(Fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
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

static bool ApplyUpdateArchive(const std::wstring &ArchivePath, const std::wstring &InstallDir, const std::wstring &SelfPath,
	const std::string &ExpectedClientVersion = {}, bool RequireBuildManifest = false, bool AllowLauncherExecutable = true)
{
	g_UpdateStage = EUpdateStage::Apply;
	g_DownloadSpeed = 0;
	g_EtaSeconds = -1;
	SetButtonLabel(L"Applying update");
	SetStatus(L"Extracting update...");
	SetPercent(10);
	if(!ValidateArchiveEntries(ArchivePath, AllowLauncherExecutable))
	{
		SetStatus(L"Update archive contains an unsafe or unexpected path");
		g_Failed = true;
		return false;
	}

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

	if(RequireBuildManifest)
	{
		std::string BuildManifest;
		std::string BuiltClientVersion;
		if(!ReadTextFile(JoinPath(CopyRoot, kBuildManifestFile), BuildManifest) ||
			!ExtractJsonString(BuildManifest, "clientVersion", BuiltClientVersion) ||
			BuiltClientVersion != ExpectedClientVersion)
		{
			SetStatus(L"Built client version does not match update metadata");
			g_Failed = true;
			DeleteTree(ExtractDir.c_str());
			return false;
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
			if(Json[Q2] == '"')
			{
				size_t Slashes = 0;
				for(size_t Back = Q2; Back > Q1 + 1 && Json[Back - 1] == '\\'; --Back)
					++Slashes;
				if((Slashes & 1) == 0)
					break;
			}
			++Q2;
		}
		if(Q2 >= Json.size())
			return false;
		Out = Json.substr(Q1 + 1, Q2 - Q1 - 1);
		return true;
	}
}

static uint64_t ExtractJsonUint64(const std::string &Json, const char *pKey);

static bool AllowedUpdateUrl(const std::string &Url)
{
	const std::wstring Wide = Utf8ToWide(Url.c_str());
	URL_COMPONENTS Components = {};
	Components.dwStructSize = sizeof(Components);
	wchar_t aHost[256] = {};
	Components.lpszHostName = aHost;
	Components.dwHostNameLength = 256;
	if(!WinHttpCrackUrl(Wide.c_str(), 0, 0, &Components) || Components.nScheme != INTERNET_SCHEME_HTTPS)
		return false;
	std::wstring Host(aHost, Components.dwHostNameLength);
	std::transform(Host.begin(), Host.end(), Host.begin(), [](wchar_t Ch) { return (wchar_t)towlower(Ch); });
	return Host == L"ddnet.under1111.com";
}

static bool ValidSha256(const std::string &Sha256)
{
	if(Sha256.size() != 64)
		return false;
	for(unsigned char Ch : Sha256)
		if(!std::isxdigit(Ch))
			return false;
	return true;
}

static bool ValidComponentVersion(const std::string &Version)
{
	int Major = 0;
	int Minor = 0;
	int Patch = 0;
	char Extra = '\0';
	return sscanf_s(Version.c_str(), "%d.%d.%d%c", &Major, &Minor, &Patch, &Extra, 1) == 3 &&
		Major >= 0 && Minor >= 0 && Patch >= 0;
}

static bool ExtractUpdateMetadata(const std::string &Json, const char *pExpectedComponent, bool AllowLegacy, UpdateMetadata &Out)
{
	UpdateMetadata Metadata;
	ExtractJsonString(Json, "component", Metadata.Component);
	if(!ExtractJsonString(Json, "version", Metadata.Version))
	{
		if(!AllowLegacy || (!ExtractJsonString(Json, "tag_name", Metadata.Version) &&
			!ExtractJsonString(Json, "name", Metadata.Version)))
			return false;
	}
	if(!ValidComponentVersion(Metadata.Version))
		return false;
	ExtractJsonString(Json, "minLauncherVersion", Metadata.MinLauncherVersion);
	if(Metadata.MinLauncherVersion.empty())
		ExtractJsonString(Json, "min_launcher_version", Metadata.MinLauncherVersion);
	if(!Metadata.MinLauncherVersion.empty() && !ValidComponentVersion(Metadata.MinLauncherVersion))
		return false;
	if(!Metadata.Component.empty() && _stricmp(Metadata.Component.c_str(), pExpectedComponent) != 0)
		return false;
	if(Metadata.Component.empty() && !AllowLegacy)
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
	if(!ExtractJsonString(Slice, "url", Metadata.Url))
	{
		if(!ExtractJsonString(Slice, "download_url", Metadata.Url))
			ExtractJsonString(Slice, "browser_download_url", Metadata.Url);
	}
	ExtractJsonString(Slice, "sha256", Metadata.Sha256);
	Metadata.Size = ExtractJsonUint64(Slice, "size");
	if(Metadata.Url.empty() || !AllowedUpdateUrl(Metadata.Url) ||
		!ValidSha256(Metadata.Sha256) || Metadata.Size < 4)
		return false;
	Out = std::move(Metadata);
	return true;
}

static bool FetchUpdateMetadata(const char *pUrl, const char *pComponent, bool AllowLegacy, UpdateMetadata &Out)
{
	char aUrl[768];
	_snprintf_s(aUrl, _TRUNCATE, "%s?t=%lld", pUrl, (long long)time(nullptr));
	std::string Body;
	return HttpGetToString(Utf8ToWide(aUrl), Body) &&
		ExtractUpdateMetadata(Body, pComponent, AllowLegacy, Out);
}

static bool FetchClientUpdateMetadata(UpdateMetadata &Out)
{
	if(FetchUpdateMetadata(UCLIENT_CLIENT_UPDATE_LATEST_URL, "client", false, Out))
		return true;
	UpdateMetadata Legacy;
	if(FetchUpdateMetadata(UCLIENT_UPDATE_LATEST_URL, "client", true, Legacy) &&
		CompareVersions(Legacy.Version, UCLIENT_CLIENT_VERSION) <= 0)
	{
		Out = std::move(Legacy);
		return true;
	}
	return false;
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

static std::wstring GetShortcutsJsonPath()
{
	wchar_t aAppData[MAX_PATH] = {};
	if(FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, aAppData)))
		return {};
	return std::wstring(aAppData) + L"\\DDNet\\uclient_shortcuts.json";
}

static std::string g_ShortcutsFileJson = "{\"version\":1,\"shortcuts\":[]}";
static bool g_ShortcutsLoaded = false;

static bool ReadUtf8File(const std::wstring &Path, std::string &Out)
{
	Out.clear();
	HANDLE hFile = CreateFileW(Path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(hFile == INVALID_HANDLE_VALUE)
		return false;
	DWORD Size = GetFileSize(hFile, nullptr);
	if(Size == INVALID_FILE_SIZE || Size == 0 || Size > 4 * 1024 * 1024)
	{
		CloseHandle(hFile);
		return false;
	}
	Out.resize(Size);
	DWORD ReadBytes = 0;
	const BOOL Ok = ReadFile(hFile, Out.data(), Size, &ReadBytes, nullptr);
	CloseHandle(hFile);
	if(!Ok || ReadBytes != Size)
	{
		Out.clear();
		return false;
	}
	return true;
}

static bool WriteUtf8FileAtomic(const std::wstring &Path, const std::string &Content)
{
	const std::wstring Temp = Path + L".tmp";
	HANDLE hFile = CreateFileW(Temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(hFile == INVALID_HANDLE_VALUE)
		return false;
	DWORD Written = 0;
	const BOOL Ok = WriteFile(hFile, Content.data(), (DWORD)Content.size(), &Written, nullptr);
	CloseHandle(hFile);
	if(!Ok || Written != Content.size())
	{
		DeleteFileW(Temp.c_str());
		return false;
	}
	if(!MoveFileExW(Temp.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING))
	{
		DeleteFileW(Temp.c_str());
		return false;
	}
	return true;
}

static void EnsureShortcutsLoaded()
{
	if(g_ShortcutsLoaded)
		return;
	g_ShortcutsLoaded = true;
	const std::wstring Path = GetShortcutsJsonPath();
	std::string Content;
	if(ReadUtf8File(Path, Content) && Content.find("\"shortcuts\"") != std::string::npos)
		g_ShortcutsFileJson = std::move(Content);
}

static bool ExtractJsonRawValue(const std::string &Json, const char *pKey, std::string &Out)
{
	std::string Needle = "\"";
	Needle += pKey;
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
	if(i >= Json.size())
		return false;
	const char Open = Json[i];
	if(Open != '[' && Open != '{')
		return false;
	const char Close = Open == '[' ? ']' : '}';
	int Depth = 0;
	const size_t Start = i;
	for(; i < Json.size(); ++i)
	{
		if(Json[i] == Open)
			++Depth;
		else if(Json[i] == Close)
		{
			--Depth;
			if(Depth == 0)
			{
				Out = Json.substr(Start, i - Start + 1);
				return true;
			}
		}
	}
	return false;
}

static void SaveShortcutsDocument(const std::string &ShortcutsArrayJson)
{
	std::string Document = "{\"version\":1,\"shortcuts\":";
	Document += ShortcutsArrayJson;
	Document += "}";
	const std::wstring Path = GetShortcutsJsonPath();
	const size_t Slash = Path.find_last_of(L"\\/");
	if(Slash != std::wstring::npos)
		CreateDirectoryW(Path.substr(0, Slash).c_str(), nullptr);
	if(WriteUtf8FileAtomic(Path, Document))
		g_ShortcutsFileJson = Document;
}

static void ToggleShortcutEnabled(const std::string &Id, bool Enabled)
{
	EnsureShortcutsLoaded();
	std::string ArrayJson;
	if(!ExtractJsonRawValue(g_ShortcutsFileJson, "shortcuts", ArrayJson) || ArrayJson.size() < 2)
		return;
	std::string Out;
	Out.reserve(ArrayJson.size());
	size_t Pos = 1;
	while(Pos < ArrayJson.size())
	{
		while(Pos < ArrayJson.size() && isspace((unsigned char)ArrayJson[Pos]))
			++Pos;
		if(Pos >= ArrayJson.size() || ArrayJson[Pos] == ']')
			break;
		if(ArrayJson[Pos] != '{')
		{
			++Pos;
			continue;
		}
		int Depth = 0;
		const size_t ObjStart = Pos;
		for(; Pos < ArrayJson.size(); ++Pos)
		{
			if(ArrayJson[Pos] == '{')
				++Depth;
			else if(ArrayJson[Pos] == '}')
			{
				--Depth;
				if(Depth == 0)
				{
					++Pos;
					break;
				}
			}
		}
		std::string Obj = ArrayJson.substr(ObjStart, Pos - ObjStart);
		std::string ObjId;
		if(!ExtractJsonString(Obj, "id", ObjId) || ObjId != Id)
			continue;
		std::string NewObj = Obj;
		const std::string EnabledNeedle = "\"enabled\":";
		const size_t EnabledPos = NewObj.find(EnabledNeedle);
		if(EnabledPos != std::string::npos)
		{
			size_t j = EnabledPos + EnabledNeedle.size();
			while(j < NewObj.size() && isspace((unsigned char)NewObj[j]))
				++j;
			const size_t ValueStart = j;
			while(j < NewObj.size() && (NewObj[j] == 't' || NewObj[j] == 'r' || NewObj[j] == 'u' || NewObj[j] == 'e' || NewObj[j] == 'f' || NewObj[j] == 'a' || NewObj[j] == 'l' || NewObj[j] == 's'))
				++j;
			NewObj.replace(ValueStart, j - ValueStart, Enabled ? "true" : "false");
		}
		else
		{
			if(NewObj.size() >= 2 && NewObj.back() == '}')
				NewObj.insert(NewObj.size() - 1, Enabled ? ",\"enabled\":true" : ",\"enabled\":false");
		}
		std::string NewArray = "[";
		bool First = true;
		size_t Scan = 1;
		while(Scan < ArrayJson.size())
		{
			while(Scan < ArrayJson.size() && isspace((unsigned char)ArrayJson[Scan]))
				++Scan;
			if(Scan >= ArrayJson.size() || ArrayJson[Scan] == ']')
				break;
			if(ArrayJson[Scan] != '{')
			{
				++Scan;
				continue;
			}
			int Depth = 0;
			const size_t Start = Scan;
			for(; Scan < ArrayJson.size(); ++Scan)
			{
				if(ArrayJson[Scan] == '{')
					++Depth;
				else if(ArrayJson[Scan] == '}')
				{
					--Depth;
					if(Depth == 0)
					{
						++Scan;
						break;
					}
				}
			}
			if(!First)
				NewArray += ",";
			First = false;
			if(Start == ObjStart)
				NewArray += NewObj;
			else
				NewArray += ArrayJson.substr(Start, Scan - Start);
		}
		NewArray += "]";
		SaveShortcutsDocument(NewArray);
		return;
	}
}

static void DeleteShortcutById(const std::string &Id)
{
	EnsureShortcutsLoaded();
	std::string ArrayJson;
	if(!ExtractJsonRawValue(g_ShortcutsFileJson, "shortcuts", ArrayJson) || ArrayJson.size() < 2)
		return;
	std::string NewArray = "[";
	bool First = true;
	size_t Pos = 1;
	while(Pos < ArrayJson.size())
	{
		while(Pos < ArrayJson.size() && isspace((unsigned char)ArrayJson[Pos]))
			++Pos;
		if(Pos >= ArrayJson.size() || ArrayJson[Pos] == ']')
			break;
		if(ArrayJson[Pos] != '{')
		{
			++Pos;
			continue;
		}
		int Depth = 0;
		const size_t ObjStart = Pos;
		for(; Pos < ArrayJson.size(); ++Pos)
		{
			if(ArrayJson[Pos] == '{')
				++Depth;
			else if(ArrayJson[Pos] == '}')
			{
				--Depth;
				if(Depth == 0)
				{
					++Pos;
					break;
				}
			}
		}
		std::string Obj = ArrayJson.substr(ObjStart, Pos - ObjStart);
		std::string ObjId;
		if(ExtractJsonString(Obj, "id", ObjId) && ObjId == Id)
			continue;
		if(!First)
			NewArray += ",";
		First = false;
		NewArray += Obj;
	}
	NewArray += "]";
	SaveShortcutsDocument(NewArray);
}

static std::string JsonEscapeValue(const std::string &In)
{
	std::string Out;
	Out.reserve(In.size() + 8);
	for(unsigned char Ch : In)
	{
		switch(Ch)
		{
		case '"': Out += "\\\""; break;
		case '\\': Out += "\\\\"; break;
		case '\b': Out += "\\b"; break;
		case '\f': Out += "\\f"; break;
		case '\n': Out += "\\n"; break;
		case '\r': Out += "\\r"; break;
		case '\t': Out += "\\t"; break;
		default:
			if(Ch < 0x20)
			{
				char aBuf[8];
				_snprintf_s(aBuf, _TRUNCATE, "\\u%04x", (unsigned)Ch);
				Out += aBuf;
			}
			else
				Out.push_back((char)Ch);
		}
	}
	return Out;
}

static std::string JsonUnescapeValue(const std::string &In)
{
	std::string Out;
	for(size_t i = 0; i < In.size(); ++i)
	{
		if(In[i] != '\\' || i + 1 >= In.size())
		{
			Out.push_back(In[i]);
			continue;
		}
		const char Ch = In[++i];
		switch(Ch)
		{
		case '"': Out.push_back('"'); break;
		case '\\': Out.push_back('\\'); break;
		case '/': Out.push_back('/'); break;
		case 'b': Out.push_back('\b'); break;
		case 'f': Out.push_back('\f'); break;
		case 'n': Out.push_back('\n'); break;
		case 'r': Out.push_back('\r'); break;
		case 't': Out.push_back('\t'); break;
		default: Out.push_back(Ch); break;
		}
	}
	return Out;
}

static bool EnsureDirectoryTree(const std::wstring &Dir)
{
	if(Dir.empty())
		return false;
	const DWORD Attr = GetFileAttributesW(Dir.c_str());
	if(Attr != INVALID_FILE_ATTRIBUTES)
		return (Attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
	const std::wstring Parent = ParentDir(Dir);
	if(!Parent.empty() && Parent != Dir && !EnsureDirectoryTree(Parent))
		return false;
	return CreateDirectoryW(Dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool RandomBytes(unsigned char *pBytes, DWORD Size)
{
	HCRYPTPROV hProv = 0;
	if(!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
		return false;
	const BOOL Ok = CryptGenRandom(hProv, Size, pBytes);
	CryptReleaseContext(hProv, 0);
	return Ok == TRUE;
}

static bool GenerateAccountCredentials(std::string &InstallId, std::string &Secret)
{
	unsigned char aUuid[16], aSecret[32];
	if(!RandomBytes(aUuid, sizeof(aUuid)) || !RandomBytes(aSecret, sizeof(aSecret)))
		return false;
	aUuid[6] = (aUuid[6] & 0x0f) | 0x40;
	aUuid[8] = (aUuid[8] & 0x3f) | 0x80;
	char aId[37];
	_snprintf_s(aId, _TRUNCATE,
		"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		aUuid[0], aUuid[1], aUuid[2], aUuid[3], aUuid[4], aUuid[5], aUuid[6], aUuid[7],
		aUuid[8], aUuid[9], aUuid[10], aUuid[11], aUuid[12], aUuid[13], aUuid[14], aUuid[15]);
	char aHex[65] = {};
	for(int i = 0; i < 32; ++i)
		_snprintf_s(aHex + i * 2, 3, _TRUNCATE, "%02x", aSecret[i]);
	InstallId = aId;
	Secret = aHex;
	return true;
}

static bool ValidInstallUuid(const std::string &Value)
{
	if(Value.size() != 36)
		return false;
	for(size_t i = 0; i < Value.size(); ++i)
	{
		const bool Hyphen = i == 8 || i == 13 || i == 18 || i == 23;
		if(Hyphen ? Value[i] != '-' : !isxdigit((unsigned char)Value[i]))
			return false;
	}
	return true;
}

static bool ValidAccountSecret(const std::string &Value)
{
	if(Value.size() < 32 || Value.size() > 128)
		return false;
	return std::all_of(Value.begin(), Value.end(), [](unsigned char Ch) {
		return Ch >= 0x21 && Ch <= 0x7e;
	});
}

static bool LoadAccountCredentials(std::string &InstallId, std::string &Secret, std::string *pGraceToken = nullptr, int64_t *pGraceExpiresAt = nullptr)
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
	if(pGraceToken)
		ExtractJsonString(Text, "grace_token", *pGraceToken);
	if(pGraceExpiresAt)
	{
		bool Known = false, Permanent = false;
		ExtractJsonExpiresAt(Text, "grace_expires_at", Known, Permanent, *pGraceExpiresAt);
	}
	return ValidInstallUuid(InstallId) && ValidAccountSecret(Secret);
}

static bool SaveAccountCredentials(const std::string &InstallId, const std::string &Secret, const std::string &GraceToken, int64_t GraceExpiresAt)
{
	const std::wstring Path = GetUclientAccountPath();
	if(Path.empty() || !ValidInstallUuid(InstallId) || !ValidAccountSecret(Secret) || !EnsureDirectoryTree(ParentDir(Path)))
		return false;
	std::string Json = "{\"install_id\":\"" + JsonEscapeValue(InstallId) +
		"\",\"install_uuid\":\"" + JsonEscapeValue(InstallId) +
		"\",\"secret\":\"" + JsonEscapeValue(Secret) +
		"\",\"grace_token\":\"" + JsonEscapeValue(GraceToken) + "\"";
	char aNum[96];
	_snprintf_s(aNum, _TRUNCATE, ",\"grace_expires_at\":%lld,\"registered\":true}\n", (long long)GraceExpiresAt);
	Json += aNum;
	const std::wstring Temp = Path + L".tmp";
	if(!WriteTextFile(Temp, Json))
		return false;
	if(!MoveFileExW(Temp.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DeleteFileW(Temp.c_str());
		return false;
	}
	return true;
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

static bool HttpJsonRequest(const wchar_t *pMethod, const std::wstring &Url, const std::string &BodyIn, std::string &BodyOut, int &StatusOut, const std::wstring &ExtraHeaders = L"");
static bool HttpBinaryRequest(const wchar_t *pMethod, const std::wstring &Url, const void *pData, size_t DataSize, std::vector<unsigned char> &BodyOut, int &StatusOut, const std::wstring &ExtraHeaders = L"", const wchar_t *pContentType = L"application/octet-stream");
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
			"{\"install_id\":\"%s\",\"secret\":\"%s\"}",
			InstallId.c_str(), Secret.c_str());
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
	bool Afk = false;
};

static std::string FriendLookupKey(std::string Name)
{
	for(char &Ch : Name)
		Ch = (char)tolower((unsigned char)Ch);
	return Name;
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
						const size_t ClientObj = Json.find('{', CPos);
						if(ClientObj == std::string::npos || ClientObj >= ClientsEnd)
							break;
						const size_t ClientEnd = FindMatchingBracket(Json, ClientObj, '{', '}');
						if(ClientEnd == std::string::npos || ClientEnd > ClientsEnd)
							break;
						const size_t CNameKey = Json.find("\"name\"", ClientObj);
						std::string PlayerName;
						if(CNameKey != std::string::npos && CNameKey < ClientEnd &&
							ExtractJsonStringAt(Json, CNameKey, PlayerName) && !PlayerName.empty())
						{
							PlayerLoc Loc;
							Loc.Address = Address;
							Loc.ServerName = ServerName;
							Loc.MapName = MapName;
							const std::string ClientJson = Json.substr(ClientObj, ClientEnd - ClientObj + 1);
							ExtractJsonBool(ClientJson, "afk", Loc.Afk);
							ByName.emplace(FriendLookupKey(PlayerName), std::move(Loc));
						}
						CPos = ClientEnd + 1;
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
		F.Afk = It->second.Afk;
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

static bool HttpBinaryRequest(const wchar_t *pMethod, const std::wstring &Url, const void *pData, size_t DataSize, std::vector<unsigned char> &BodyOut, int &StatusOut, const std::wstring &ExtraHeaders, const wchar_t *pContentType)
{
	StatusOut = 0;
	BodyOut.clear();
	if(DataSize > MAXDWORD)
		return false;

	URL_COMPONENTS Uc = {};
	Uc.dwStructSize = sizeof(Uc);
	wchar_t aHost[256];
	wchar_t aPath[2048];
	wchar_t aExtra[2048];
	Uc.lpszHostName = aHost;
	Uc.dwHostNameLength = 256;
	Uc.lpszUrlPath = aPath;
	Uc.dwUrlPathLength = 2048;
	Uc.lpszExtraInfo = aExtra;
	Uc.dwExtraInfoLength = 2048;
	if(!WinHttpCrackUrl(Url.c_str(), 0, 0, &Uc))
		return false;
	std::wstring RequestPath(aPath, Uc.dwUrlPathLength);
	if(Uc.dwExtraInfoLength)
		RequestPath.append(aExtra, Uc.dwExtraInfoLength);

	const std::wstring Ua = Utf8ToWide((std::string("UClientLauncher/") + UCLIENT_LAUNCHER_VERSION).c_str());
	HINTERNET hSession = WinHttpOpen(Ua.c_str(),
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if(!hSession)
		return false;
	WinHttpSetTimeouts(hSession, 10000, 10000, 30000, 30000);

	HINTERNET hConnect = WinHttpConnect(hSession, aHost, Uc.nPort, 0);
	if(!hConnect)
	{
		WinHttpCloseHandle(hSession);
		return false;
	}

	DWORD Flags = (Uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, pMethod, RequestPath.c_str(), nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, Flags);
	if(!hRequest)
	{
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	std::wstring Headers = L"Accept: application/json\r\n";
	if(pContentType && pContentType[0])
		Headers += std::wstring(L"Content-Type: ") + pContentType + L"\r\n";
	Headers += ExtraHeaders;
	BOOL Ok = WinHttpSendRequest(hRequest, Headers.c_str(), (DWORD)-1L,
		DataSize == 0 ? WINHTTP_NO_REQUEST_DATA : const_cast<void *>(pData),
		(DWORD)DataSize, (DWORD)DataSize, 0);
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
			const size_t OldSize = BodyOut.size();
			if(OldSize + Avail > 12 * 1024 * 1024)
			{
				Ok = FALSE;
				break;
			}
			BodyOut.resize(OldSize + Avail);
			DWORD Read = 0;
			if(!WinHttpReadData(hRequest, BodyOut.data() + OldSize, Avail, &Read))
			{
				Ok = FALSE;
				break;
			}
			BodyOut.resize(OldSize + Read);
		}
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return Ok == TRUE;
}

static bool HttpJsonRequest(const wchar_t *pMethod, const std::wstring &Url, const std::string &BodyIn, std::string &BodyOut, int &StatusOut, const std::wstring &ExtraHeaders)
{
	std::vector<unsigned char> Bytes;
	if(!HttpBinaryRequest(pMethod, Url, BodyIn.data(), BodyIn.size(), Bytes, StatusOut, ExtraHeaders, L"application/json"))
		return false;
	BodyOut.assign((const char *)Bytes.data(), Bytes.size());
	return true;
}

static bool HttpGetToString(const std::wstring &Url, std::string &OutBody)
{
	int Status = 0;
	if(!HttpJsonRequest(L"GET", Url, std::string(), OutBody, Status))
		return false;
	return Status >= 200 && Status < 300 && !OutBody.empty();
}

static std::wstring AccountAuthHeaders(const std::string &InstallId, const std::string &Secret)
{
	return L"Authorization: Bearer " + Utf8ToWide(Secret.c_str()) + L"\r\nx-uclient-install-id: " + Utf8ToWide(InstallId.c_str()) + L"\r\n";
}

static uint64_t ExtractJsonUint64(const std::string &Json, const char *pKey)
{
	const std::string Needle = std::string("\"") + pKey + "\"";
	size_t Pos = Json.find(Needle);
	if(Pos == std::string::npos || (Pos = Json.find(':', Pos + Needle.size())) == std::string::npos)
		return 0;
	return _strtoui64(Json.c_str() + Pos + 1, nullptr, 10);
}

static void PublishAccount(EAccountState State, const std::string &Email = {}, const std::string &Error = {})
{
	EnterCriticalSection(&g_Lock);
	g_AccountState = State;
	g_AccountEmail = Email;
	g_AccountError = Error;
	g_AccountWorkerRunning = false;
	if(State == EAccountState::Banned)
		g_PlayBlocked = true;
	LeaveCriticalSection(&g_Lock);
	if(g_hWnd)
		PostMessage(g_hWnd, WM_ACCOUNT_READY, 0, 0);
}

static bool IsAccountReady()
{
	EnterCriticalSection(&g_Lock);
	const bool Ready = g_AccountState == EAccountState::ReadyAnonymous || g_AccountState == EAccountState::ReadyEmail;
	LeaveCriticalSection(&g_Lock);
	return Ready;
}

static bool ParseAndSaveAccountResponse(const std::string &Body, const std::string &FallbackInstallId, const std::string &Secret, std::string &Email, bool &HasEmail)
{
	std::string InstallId = FallbackInstallId, GraceToken;
	int64_t GraceExpiresAt = 0;
	ExtractJsonString(Body, "install_id", InstallId);
	ExtractJsonString(Body, "grace_token", GraceToken);
	bool Known = false, Permanent = false;
	ExtractJsonExpiresAt(Body, "grace_expires_at", Known, Permanent, GraceExpiresAt);
	ExtractJsonBool(Body, "has_email", HasEmail);
	ExtractJsonString(Body, "email", Email);
	InstallId = JsonUnescapeValue(InstallId);
	GraceToken = JsonUnescapeValue(GraceToken);
	Email = JsonUnescapeValue(Email);
	if(InstallId.empty() || Secret.empty() || !SaveAccountCredentials(InstallId, Secret, GraceToken, GraceExpiresAt))
		return false;
	EnterCriticalSection(&g_Lock);
	g_AccountInstallId = InstallId;
	g_AccountSecret = Secret;
	LeaveCriticalSection(&g_Lock);
	return true;
}

enum class EAccountOp
{
	Check,
	RegisterEmail,
	LoginEmail,
	LoginKey,
	LoginSaved,
	RegisterAnonymous,
	LinkEmail,
	Logout,
};

struct AccountWork
{
	EAccountOp Op = EAccountOp::Check;
	std::string Email;
	std::string Password;
	std::string InstallId;
	std::string Secret;
};

static void SecureClear(std::string &Value)
{
	if(!Value.empty())
		SecureZeroMemory(Value.data(), Value.size());
	Value.clear();
}

static void MarkAccountSignedIn()
{
	g_AccountSignedOut = false;
	SaveLauncherSettings(g_InstallDir);
}

static DWORD WINAPI AccountThread(LPVOID pData)
{
	std::unique_ptr<AccountWork> Work((AccountWork *)pData);
	std::string InstallId = Work->InstallId;
	std::string Secret = Work->Secret;
	std::string Body, Response, Email;
	bool HasEmail = false;
	int Status = 0;
	const std::wstring Base = Utf8ToWide(UCLIENT_API_BASE_URL);

	if(Work->Op == EAccountOp::Logout)
	{
		RefreshGameRunningState();
		if(EffectiveGameRunning())
		{
			EnterCriticalSection(&g_Lock);
			g_AccountWorkerRunning = false;
			LeaveCriticalSection(&g_Lock);
			if(g_hWnd)
				PostMessage(g_hWnd, WM_ACCOUNT_READY, 0, 0);
			return 0;
		}
		g_AccountSignedOut = true;
		SaveLauncherSettings(g_InstallDir);
		EnterCriticalSection(&g_Lock);
		g_AccountEmail.clear();
		g_AccountError.clear();
		g_SavedAccountInstallId = g_AccountInstallId;
		SecureClear(g_AccountSecret);
		g_AccountInstallId.clear();
		g_BackupFiles.clear();
		g_BackupVersions.clear();
		LeaveCriticalSection(&g_Lock);
		PublishAccount(EAccountState::NeedsOnboarding);
		return 0;
	}

	if(Work->Op == EAccountOp::Check)
	{
		if(!LoadAccountCredentials(InstallId, Secret))
		{
			EnterCriticalSection(&g_Lock);
			g_HasSavedAccount = false;
			g_SavedAccountInstallId.clear();
			LeaveCriticalSection(&g_Lock);
			PublishAccount(EAccountState::NeedsOnboarding);
			return 0;
		}
		EnterCriticalSection(&g_Lock);
		g_HasSavedAccount = true;
		g_SavedAccountInstallId = InstallId;
		LeaveCriticalSection(&g_Lock);
		if(g_AccountSignedOut)
		{
			PublishAccount(EAccountState::NeedsOnboarding, {}, "signin:");
			return 0;
		}
		Body = "{\"install_id\":\"" + JsonEscapeValue(InstallId) + "\",\"secret\":\"" + JsonEscapeValue(Secret) + "\"}";
		if(!HttpJsonRequest(L"POST", Base + L"/account/verify", Body, Response, Status))
		{
			PublishAccount(EAccountState::Error, {}, "Could not verify this account. Check your connection.");
			return 0;
		}
		if(Status == 423)
		{
			PublishAccount(EAccountState::Banned, {}, "This account is suspended.");
			return 0;
		}
		if(Status == 403 || Status == 404)
		{
			PublishAccount(EAccountState::NeedsOnboarding, {}, "signin:");
			return 0;
		}
		if(Status < 200 || Status >= 300)
		{
			PublishAccount(EAccountState::Error, {}, "Account verification failed.");
			return 0;
		}
		if(!ParseAndSaveAccountResponse(Response, InstallId, Secret, Email, HasEmail))
		{
			PublishAccount(EAccountState::Error, {}, "Could not save the refreshed account credentials.");
			return 0;
		}
		Response.clear();
		if(!HttpJsonRequest(L"GET", Base + L"/account/profile", {}, Response, Status, AccountAuthHeaders(InstallId, Secret)))
		{
			PublishAccount(EAccountState::Error, {}, "Could not load the account profile.");
			return 0;
		}
		if(Status == 423)
		{
			PublishAccount(EAccountState::Banned, {}, "This account is suspended.");
			return 0;
		}
		if(Status == 403 || Status == 404)
		{
			PublishAccount(EAccountState::NeedsOnboarding, {}, "signin:");
			return 0;
		}
		if(Status < 200 || Status >= 300)
		{
			PublishAccount(EAccountState::Error, {}, "Could not load the account profile.");
			return 0;
		}
		ExtractJsonBool(Response, "has_email", HasEmail);
		ExtractJsonString(Response, "email", Email);
		Email = JsonUnescapeValue(Email);
		EnterCriticalSection(&g_Lock);
		g_AccountInstallId = InstallId;
		g_AccountSecret = Secret;
		LeaveCriticalSection(&g_Lock);
		PublishAccount(HasEmail ? EAccountState::ReadyEmail : EAccountState::ReadyAnonymous, Email);
		return 0;
	}

	if(Work->Op == EAccountOp::LoginKey || Work->Op == EAccountOp::LoginSaved)
	{
		if(Work->Op == EAccountOp::LoginSaved && !LoadAccountCredentials(InstallId, Secret))
		{
			EnterCriticalSection(&g_Lock);
			g_HasSavedAccount = false;
			g_SavedAccountInstallId.clear();
			LeaveCriticalSection(&g_Lock);
			PublishAccount(EAccountState::NeedsOnboarding, {}, "signin:Saved account credentials are unavailable.");
			return 0;
		}
		Body = "{\"install_id\":\"" + JsonEscapeValue(InstallId) + "\",\"secret\":\"" + JsonEscapeValue(Secret) + "\"}";
		HttpJsonRequest(L"POST", Base + L"/account/verify", Body, Response, Status);
		if(Status >= 200 && Status < 300 && ParseAndSaveAccountResponse(Response, InstallId, Secret, Email, HasEmail))
		{
			Response.clear();
			HttpJsonRequest(L"GET", Base + L"/account/profile", {}, Response, Status, AccountAuthHeaders(InstallId, Secret));
			ExtractJsonBool(Response, "has_email", HasEmail);
			ExtractJsonString(Response, "email", Email);
			Email = JsonUnescapeValue(Email);
			EnterCriticalSection(&g_Lock);
			g_AccountInstallId = InstallId;
			g_AccountSecret = Secret;
			g_HasSavedAccount = true;
			g_SavedAccountInstallId = InstallId;
			LeaveCriticalSection(&g_Lock);
			MarkAccountSignedIn();
			PublishAccount(HasEmail ? EAccountState::ReadyEmail : EAccountState::ReadyAnonymous, Email);
		}
		else if(Status == 423)
			PublishAccount(EAccountState::Banned, {}, "This account is suspended.");
		else
			PublishAccount(EAccountState::NeedsOnboarding, {}, "signin:Invalid UUID or account key.");
		return 0;
	}

	if(Work->Op == EAccountOp::LinkEmail)
	{
		Body = "{\"email\":\"" + JsonEscapeValue(Work->Email) + "\",\"password\":\"" + JsonEscapeValue(Work->Password) + "\"}";
		HttpJsonRequest(L"POST", Base + L"/account/link-email", Body, Response, Status, AccountAuthHeaders(InstallId, Secret));
		SecureClear(Body);
		SecureClear(Work->Password);
		if(Status >= 200 && Status < 300)
			PublishAccount(EAccountState::ReadyEmail, Work->Email);
		else
			PublishAccount(EAccountState::ReadyAnonymous, {}, Status == 409 ? "That email is already in use." : "Could not link the email.");
		return 0;
	}

	if(!GenerateAccountCredentials(InstallId, Secret))
	{
		SecureClear(Work->Password);
		PublishAccount(EAccountState::NeedsOnboarding, {}, "Secure credential generation failed.");
		return 0;
	}
	std::wstring Endpoint;
	if(Work->Op == EAccountOp::RegisterEmail)
	{
		Endpoint = L"/account/register-email";
		Body = "{\"email\":\"" + JsonEscapeValue(Work->Email) + "\",\"password\":\"" + JsonEscapeValue(Work->Password) +
			"\",\"install_id\":\"" + JsonEscapeValue(InstallId) + "\",\"secret\":\"" + JsonEscapeValue(Secret) + "\"}";
	}
	else if(Work->Op == EAccountOp::LoginEmail)
	{
		Endpoint = L"/account/login-email";
		Body = "{\"email\":\"" + JsonEscapeValue(Work->Email) + "\",\"password\":\"" + JsonEscapeValue(Work->Password) +
			"\",\"secret\":\"" + JsonEscapeValue(Secret) + "\"}";
	}
	else
	{
		Endpoint = L"/account/register";
		Body = "{\"install_id\":\"" + JsonEscapeValue(InstallId) + "\",\"secret\":\"" + JsonEscapeValue(Secret) + "\"}";
	}
	HttpJsonRequest(L"POST", Base + Endpoint, Body, Response, Status);
	SecureClear(Body);
	SecureClear(Work->Password);
	if(Status == 423)
		PublishAccount(EAccountState::Banned, {}, "This account is suspended.");
	else if(Status >= 200 && Status < 300 && ParseAndSaveAccountResponse(Response, InstallId, Secret, Email, HasEmail))
	{
		EnterCriticalSection(&g_Lock);
		g_HasSavedAccount = true;
		g_SavedAccountInstallId = g_AccountInstallId;
		LeaveCriticalSection(&g_Lock);
		MarkAccountSignedIn();
		PublishAccount(HasEmail ? EAccountState::ReadyEmail : EAccountState::ReadyAnonymous, Email.empty() ? Work->Email : Email);
	}
	else
		PublishAccount(EAccountState::NeedsOnboarding, {}, Status == 409 ? "This email is already registered." : "Account request failed. Check your details and try again.");
	return 0;
}

static bool StartAccountWork(AccountWork *pWork)
{
	EnterCriticalSection(&g_Lock);
	if(g_AccountWorkerRunning)
	{
		LeaveCriticalSection(&g_Lock);
		delete pWork;
		return false;
	}
	g_AccountWorkerRunning = true;
	g_AccountState = pWork->Op == EAccountOp::Check ? EAccountState::Checking : EAccountState::Busy;
	g_AccountError.clear();
	LeaveCriticalSection(&g_Lock);
	HANDLE hThread = CreateThread(nullptr, 0, AccountThread, pWork, 0, nullptr);
	if(hThread)
	{
		CloseHandle(hThread);
		return true;
	}
	delete pWork;
	PublishAccount(EAccountState::Error, {}, "Could not start the account operation.");
	return false;
}

static void RequestAccountCheck()
{
	auto *pWork = new AccountWork();
	pWork->Op = EAccountOp::Check;
	StartAccountWork(pWork);
}

static std::wstring GetBackupRoot()
{
	wchar_t aAppData[MAX_PATH] = {};
	if(FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, aAppData)))
		return {};
	std::wstring Ddnet = std::wstring(aAppData) + L"\\DDNet";
	if(GetFileAttributesW(Ddnet.c_str()) != INVALID_FILE_ATTRIBUTES)
		return Ddnet;
	std::wstring Legacy = std::wstring(aAppData) + L"\\Teeworlds";
	return GetFileAttributesW(Legacy.c_str()) != INVALID_FILE_ATTRIBUTES ? Legacy : Ddnet;
}

static bool SafeRelativePath(const std::string &Path);

static void ScanBackupTree(const std::wstring &Root, const std::wstring &Relative, std::vector<BackupFileView> &Out)
{
	const std::wstring Dir = Relative.empty() ? Root : JoinPath(Root, Relative.c_str());
	WIN32_FIND_DATAW Fd = {};
	HANDLE hFind = FindFirstFileW((Dir + L"\\*").c_str(), &Fd);
	if(hFind == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if(!wcscmp(Fd.cFileName, L".") || !wcscmp(Fd.cFileName, L".."))
			continue;
		const std::wstring Rel = Relative.empty() ? Fd.cFileName : Relative + L"\\" + Fd.cFileName;
		if(Fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if(!(Fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
				ScanBackupTree(Root, Rel, Out);
			continue;
		}
		const std::string Utf8Rel = WideToUtf8(Rel);
		if((Fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !SafeRelativePath(Utf8Rel))
			continue;
		BackupFileView File;
		File.Path = Utf8Rel;
		File.Size = ((uint64_t)Fd.nFileSizeHigh << 32) | Fd.nFileSizeLow;
		Out.push_back(std::move(File));
	} while(FindNextFileW(hFind, &Fd));
	FindClose(hFind);
}

static std::wstring UrlEncode(const std::string &Value)
{
	static const char *pHex = "0123456789ABCDEF";
	std::string Out;
	for(unsigned char Ch : Value)
	{
		if((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') || (Ch >= '0' && Ch <= '9') || Ch == '-' || Ch == '_' || Ch == '.' || Ch == '~')
			Out.push_back((char)Ch);
		else
		{
			Out.push_back('%');
			Out.push_back(pHex[Ch >> 4]);
			Out.push_back(pHex[Ch & 15]);
		}
	}
	return Utf8ToWide(Out.c_str());
}

static bool ReadBinaryFile(const std::wstring &Path, std::vector<unsigned char> &Out)
{
	FILE *pFile = nullptr;
	if(_wfopen_s(&pFile, Path.c_str(), L"rb") != 0 || !pFile)
		return false;
	_fseeki64(pFile, 0, SEEK_END);
	const __int64 Size = _ftelli64(pFile);
	_fseeki64(pFile, 0, SEEK_SET);
	if(Size < 0 || Size > MAXDWORD)
	{
		fclose(pFile);
		return false;
	}
	Out.resize((size_t)Size);
	const bool Ok = Out.empty() || fread(Out.data(), 1, Out.size(), pFile) == Out.size();
	fclose(pFile);
	return Ok;
}

static bool Sha256Hex(const std::vector<unsigned char> &Data, std::string &Out)
{
	HCRYPTPROV hProv = 0;
	HCRYPTHASH hHash = 0;
	if(!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) ||
		!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
	{
		if(hProv)
			CryptReleaseContext(hProv, 0);
		return false;
	}
	BOOL Ok = Data.empty() || CryptHashData(hHash, Data.data(), (DWORD)Data.size(), 0);
	unsigned char aHash[32];
	DWORD Size = sizeof(aHash);
	Ok = Ok && CryptGetHashParam(hHash, HP_HASHVAL, aHash, &Size, 0);
	CryptDestroyHash(hHash);
	CryptReleaseContext(hProv, 0);
	if(!Ok || Size != 32)
		return false;
	char aHex[65] = {};
	for(int i = 0; i < 32; ++i)
		_snprintf_s(aHex + i * 2, 3, _TRUNCATE, "%02x", aHash[i]);
	Out = aHex;
	return true;
}

static bool SafeRelativePath(const std::string &Path)
{
	if(Path.empty() || Path[0] == '/' || Path[0] == '\\' || Path.find(':') != std::string::npos)
		return false;
	std::string Normal = Path;
	std::replace(Normal.begin(), Normal.end(), '\\', '/');
	if(Normal != ".." && Normal.find("../") != 0 && Normal.find("/../") == std::string::npos)
	{
		for(unsigned char Ch : Normal)
			if(Ch < 0x20 || Ch == '<' || Ch == '>' || Ch == '"' || Ch == '|' || Ch == '?' || Ch == '*')
				return false;
		std::string Lower = Normal;
		std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](unsigned char Ch) { return (char)std::tolower(Ch); });
		size_t SegmentStart = 0;
		while(SegmentStart <= Lower.size())
		{
			const size_t SegmentEnd = Lower.find('/', SegmentStart);
			const std::string Segment = Lower.substr(SegmentStart, SegmentEnd == std::string::npos ? std::string::npos : SegmentEnd - SegmentStart);
			if(Segment == "dumps" || Segment == "downloadedskins" ||
				Segment == "communityicons" || Segment == "communityicsons")
				return false;
			if(SegmentEnd == std::string::npos)
				break;
			SegmentStart = SegmentEnd + 1;
		}
		const size_t Slash = Lower.find_last_of('/');
		const std::string Name = Lower.substr(Slash == std::string::npos ? 0 : Slash + 1);
		if(Name == "steam_uclient_account.json" || Name == "uclient_account.json")
			return false;
		const size_t Dot = Lower.find_last_of('.');
		if(Dot == std::string::npos || (Slash != std::string::npos && Dot < Slash))
			return false;
		const std::string Extension = Lower.substr(Dot);
		return Extension == ".cfg" || Extension == ".txt" || Extension == ".png" ||
			Extension == ".jpg" || Extension == ".jpeg" || Extension == ".log";
	}
	return false;
}

static bool ValidBackupContents(const std::string &Path, const std::vector<unsigned char> &Data)
{
	std::string Lower = Path;
	std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](unsigned char Ch) { return (char)std::tolower(Ch); });
	const size_t Dot = Lower.find_last_of('.');
	const std::string Extension = Dot == std::string::npos ? "" : Lower.substr(Dot);
	if(Extension == ".png")
	{
		static const unsigned char s_aPng[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
		return Data.size() >= sizeof(s_aPng) && memcmp(Data.data(), s_aPng, sizeof(s_aPng)) == 0;
	}
	if(Extension == ".jpg" || Extension == ".jpeg")
		return Data.size() >= 4 && Data[0] == 0xff && Data[1] == 0xd8 && Data[2] == 0xff &&
			Data[Data.size() - 2] == 0xff && Data[Data.size() - 1] == 0xd9;
	if(Extension != ".cfg" && Extension != ".txt" && Extension != ".log")
		return false;
	for(unsigned char Ch : Data)
		if(Ch < 0x20 && Ch != '\t' && Ch != '\r' && Ch != '\n')
			return false;
	std::string Text(Data.begin(), Data.end());
	std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char Ch) { return (char)std::tolower(Ch); });
	const size_t First = Text.find_first_not_of(" \t\r\n");
	const size_t Last = Text.find_last_not_of(" \t\r\n");
	if(First != std::string::npos && Text[First] == '{' && Text[Last] == '}' &&
		Text.find("\"install_id\"") != std::string::npos &&
		(Text.find("\"secret\"") != std::string::npos || Text.find("\"grace_token\"") != std::string::npos))
		return false;
	return true;
}

static bool PathUsesReparsePoint(const std::wstring &Root, const std::wstring &Relative)
{
	std::wstring Current = Root;
	const DWORD RootAttributes = GetFileAttributesW(Current.c_str());
	if(RootAttributes != INVALID_FILE_ATTRIBUTES && (RootAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
		return true;
	size_t Start = 0;
	while(Start < Relative.size())
	{
		const size_t Separator = Relative.find_first_of(L"\\/", Start);
		const std::wstring Part = Relative.substr(Start, Separator == std::wstring::npos ? std::wstring::npos : Separator - Start);
		if(!Part.empty())
		{
			Current = JoinPath(Current, Part.c_str());
			const DWORD Attributes = GetFileAttributesW(Current.c_str());
			if(Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_REPARSE_POINT))
				return true;
		}
		if(Separator == std::wstring::npos)
			break;
		Start = Separator + 1;
	}
	return false;
}

static std::string FormatBackupTimestamp(uint64_t Timestamp)
{
	const time_t Time = (time_t)Timestamp;
	tm Local = {};
	if(localtime_s(&Local, &Time) != 0)
		return std::to_string(Timestamp);
	char aBuffer[64];
	if(strftime(aBuffer, sizeof(aBuffer), "%Y-%m-%d %H:%M:%S", &Local) == 0)
		return std::to_string(Timestamp);
	return aBuffer;
}

static void ParseBackupVersions(const std::string &Json, std::vector<BackupVersionView> &Out)
{
	Out.clear();
	size_t Pos = 0;
	while((Pos = Json.find('{', Pos)) != std::string::npos)
	{
		const size_t End = Json.find('}', Pos);
		if(End == std::string::npos)
			break;
		const std::string Obj = Json.substr(Pos, End - Pos + 1);
		BackupVersionView V;
		ExtractJsonString(Obj, "id", V.Id);
		if(!ExtractJsonString(Obj, "relative_path", V.Path))
			ExtractJsonString(Obj, "path", V.Path);
		const uint64_t Timestamp = ExtractJsonUint64(Obj, "created_at");
		if(Timestamp)
			V.CreatedAt = FormatBackupTimestamp(Timestamp);
		else
			ExtractJsonString(Obj, "server_date", V.CreatedAt);
		ExtractJsonString(Obj, "sha256", V.Sha256);
		V.Id = JsonUnescapeValue(V.Id);
		V.Path = JsonUnescapeValue(V.Path);
		V.CreatedAt = JsonUnescapeValue(V.CreatedAt);
		V.Sha256 = JsonUnescapeValue(V.Sha256);
		V.Size = ExtractJsonUint64(Obj, "size_bytes");
		if(!V.Size)
			V.Size = ExtractJsonUint64(Obj, "size");
		if(!V.Id.empty() && SafeRelativePath(V.Path))
			Out.push_back(std::move(V));
		Pos = End + 1;
	}
}

enum class EBackupOp { Refresh, Upload, Restore, Delete };
struct BackupWork
{
	EBackupOp Op = EBackupOp::Refresh;
	std::vector<std::string> Paths;
	std::string Id;
	std::vector<std::string> Ids;
};

static bool BackupCredentials(std::string &InstallId, std::string &Secret)
{
	EnterCriticalSection(&g_Lock);
	InstallId = g_AccountInstallId;
	Secret = g_AccountSecret;
	LeaveCriticalSection(&g_Lock);
	return !InstallId.empty() && !Secret.empty();
}

static void RefreshBackupData(std::string &Error)
{
	std::string InstallId, Secret, Body;
	int Status = 0;
	std::vector<BackupFileView> Files;
	ScanBackupTree(GetBackupRoot(), L"", Files);
	std::sort(Files.begin(), Files.end(), [](const BackupFileView &A, const BackupFileView &B) {
		return _stricmp(A.Path.c_str(), B.Path.c_str()) < 0;
	});
	if(!BackupCredentials(InstallId, Secret) ||
		!HttpJsonRequest(L"GET", Utf8ToWide((std::string(UCLIENT_API_BASE_URL) + "/backups/cfg").c_str()), {}, Body, Status, AccountAuthHeaders(InstallId, Secret)) ||
		Status < 200 || Status >= 300)
		Error = "Could not load server backups.";
	std::vector<BackupVersionView> Versions;
	if(Error.empty())
		ParseBackupVersions(Body, Versions);
	EnterCriticalSection(&g_Lock);
	g_BackupFiles = std::move(Files);
	g_BackupVersions = std::move(Versions);
	g_BackupUsed = ExtractJsonUint64(Body, "used_bytes");
	if(!g_BackupUsed)
		g_BackupUsed = ExtractJsonUint64(Body, "used");
	if(!g_BackupUsed)
		g_BackupUsed = ExtractJsonUint64(Body, "usage");
	g_BackupLimit = ExtractJsonUint64(Body, "quota_bytes");
	if(!g_BackupLimit)
		g_BackupLimit = ExtractJsonUint64(Body, "limit");
	LeaveCriticalSection(&g_Lock);
}

static DWORD WINAPI BackupThread(LPVOID pData)
{
	std::unique_ptr<BackupWork> Work((BackupWork *)pData);
	std::string InstallId, Secret, Error, Response;
	int Status = 0;
	const std::wstring Root = GetBackupRoot();
	const std::wstring Base = Utf8ToWide((std::string(UCLIENT_API_BASE_URL) + "/backups/cfg").c_str());
	if(!BackupCredentials(InstallId, Secret))
		Error = "Account credentials are unavailable.";
	const std::wstring Auth = AccountAuthHeaders(InstallId, Secret);

	if(Error.empty() && Work->Op == EBackupOp::Upload)
	{
		for(const std::string &Rel : Work->Paths)
		{
			if(!SafeRelativePath(Rel))
			{
				Error = "A selected file path is not allowed.";
				break;
			}
			std::vector<unsigned char> Data, Reply;
			const std::wstring WideRel = Utf8ToWide(Rel.c_str());
			if(PathUsesReparsePoint(Root, WideRel))
			{
				Error = "A selected file uses an unsupported directory link.";
				break;
			}
			if(!ReadBinaryFile(JoinPath(Root, WideRel.c_str()), Data))
			{
				Error = "One or more selected files could not be read.";
				break;
			}
			if(!ValidBackupContents(Rel, Data))
			{
				Error = "The selected file \"" + Rel + "\" has invalid contents or contains account credentials.";
				break;
			}
			if(!HttpBinaryRequest(L"PUT", Base, Data.data(), Data.size(), Reply, Status,
					Auth + L"x-uclient-path: " + UrlEncode(Rel) + L"\r\n") ||
				Status < 200 || Status >= 300)
			{
				Error = Status == 413 ? "The 10 MB account backup limit would be exceeded." :
					"One or more files could not be uploaded.";
				break;
			}
		}
	}
	else if(Error.empty() && Work->Op == EBackupOp::Delete)
	{
		HttpJsonRequest(L"DELETE", Base + L"/" + UrlEncode(Work->Id), {}, Response, Status, Auth);
		if(Status < 200 || Status >= 300)
			Error = "Could not delete this backup.";
	}
	else if(Error.empty() && Work->Op == EBackupOp::Restore)
	{
		struct RestoreItem
		{
			BackupVersionView Meta;
			std::vector<unsigned char> Data;
			std::wstring Rel;
			std::wstring Dest;
			std::wstring ExistingCopy;
			bool HadExisting = false;
		};
		std::vector<BackupVersionView> Available;
		EnterCriticalSection(&g_Lock);
		Available = g_BackupVersions;
		LeaveCriticalSection(&g_Lock);
		std::vector<RestoreItem> Items;
		std::vector<std::string> NormalPaths;
		if(Work->Ids.empty())
			Error = "No backup versions were selected.";
		for(const std::string &Id : Work->Ids)
		{
			if(!Error.empty())
				break;
			RestoreItem Item;
			for(const auto &V : Available)
				if(V.Id == Id)
				{
					Item.Meta = V;
					break;
				}
			if(Item.Meta.Id.empty() || !SafeRelativePath(Item.Meta.Path))
			{
				Error = "Backup metadata no longer matches a selected item.";
				break;
			}
			std::string Normal = Item.Meta.Path;
			std::replace(Normal.begin(), Normal.end(), '\\', '/');
			std::transform(Normal.begin(), Normal.end(), Normal.begin(), [](unsigned char Ch) { return (char)std::tolower(Ch); });
			if(std::find(NormalPaths.begin(), NormalPaths.end(), Normal) != NormalPaths.end())
			{
				Error = "Different versions of the same file cannot be restored together.";
				break;
			}
			NormalPaths.push_back(Normal);
			Items.push_back(std::move(Item));
		}
		for(RestoreItem &Item : Items)
		{
			if(!Error.empty())
				break;
			if(!HttpBinaryRequest(L"GET", Base + L"/" + UrlEncode(Item.Meta.Id), nullptr, 0, Item.Data, Status, Auth, nullptr) ||
				Status < 200 || Status >= 300 || Item.Data.size() != Item.Meta.Size)
			{
				Error = "A downloaded backup size did not match.";
				break;
			}
			std::string Hash;
			if(!Sha256Hex(Item.Data, Hash) || _stricmp(Hash.c_str(), Item.Meta.Sha256.c_str()) != 0)
			{
				Error = "A downloaded backup checksum did not match.";
				break;
			}
			if(!ValidBackupContents(Item.Meta.Path, Item.Data))
			{
				Error = "A downloaded backup no longer matches its allowed file type.";
				break;
			}
			Item.Rel = Utf8ToWide(Item.Meta.Path.c_str());
			Item.Dest = JoinPath(Root, Item.Rel.c_str());
			if(PathUsesReparsePoint(Root, Item.Rel))
				Error = "A restore path contains an unsupported directory link.";
		}
		const std::wstring BackupBase = ParentDir(ParentDir(GetUclientAccountPath())) +
			L"\\UClient\\restore-backups\\" + std::to_wstring((long long)time(nullptr)) + L"-" + std::to_wstring(GetCurrentProcessId());
		for(RestoreItem &Item : Items)
		{
			if(!Error.empty())
				break;
			if(!EnsureDirectoryTree(ParentDir(Item.Dest)))
			{
				Error = "Could not create a restore directory.";
				break;
			}
			const DWORD Attributes = GetFileAttributesW(Item.Dest.c_str());
			Item.HadExisting = Attributes != INVALID_FILE_ATTRIBUTES;
			if(Item.HadExisting)
			{
				if(Attributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					Error = "A restore destination is a directory.";
					break;
				}
				Item.ExistingCopy = JoinPath(BackupBase, Item.Rel.c_str());
				if(!EnsureDirectoryTree(ParentDir(Item.ExistingCopy)) ||
					!CopyFileW(Item.Dest.c_str(), Item.ExistingCopy.c_str(), FALSE))
				{
					Error = "Could not preserve an existing file.";
					break;
				}
			}
		}
		std::vector<size_t> Applied;
		for(size_t i = 0; i < Items.size() && Error.empty(); ++i)
		{
			const std::wstring Temp = Items[i].Dest + L".uclient-restore.tmp";
			DeleteFileW(Temp.c_str());
			FILE *pFile = nullptr;
			if(_wfopen_s(&pFile, Temp.c_str(), L"wb") != 0 || !pFile ||
				(!Items[i].Data.empty() && fwrite(Items[i].Data.data(), 1, Items[i].Data.size(), pFile) != Items[i].Data.size()))
				Error = "Could not write a restored file.";
			if(pFile)
				fclose(pFile);
			if(Error.empty() && !MoveFileExW(Temp.c_str(), Items[i].Dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				Error = "Could not replace a restored file.";
			if(Error.empty())
				Applied.push_back(i);
			else
				DeleteFileW(Temp.c_str());
		}
		if(!Error.empty() && !Applied.empty())
		{
			bool RollbackOk = true;
			for(auto It = Applied.rbegin(); It != Applied.rend(); ++It)
			{
				RestoreItem &Item = Items[*It];
				if(Item.HadExisting)
				{
					const std::wstring Temp = Item.Dest + L".uclient-rollback.tmp";
					DeleteFileW(Temp.c_str());
					if(!CopyFileW(Item.ExistingCopy.c_str(), Temp.c_str(), FALSE) ||
						!MoveFileExW(Temp.c_str(), Item.Dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
					{
						DeleteFileW(Temp.c_str());
						RollbackOk = false;
					}
				}
				else if(!DeleteFileW(Item.Dest.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
					RollbackOk = false;
			}
			if(!RollbackOk)
				Error += " Some files could not be rolled back.";
		}
	}
	if(Error.empty() || Work->Op == EBackupOp::Refresh)
		RefreshBackupData(Error);
	EnterCriticalSection(&g_Lock);
	g_BackupBusy = false;
	g_BackupError = Error;
	LeaveCriticalSection(&g_Lock);
	if(g_hWnd)
		PostMessage(g_hWnd, WM_BACKUP_READY, 0, 0);
	return 0;
}

static void StartBackupWork(BackupWork *pWork)
{
	EnterCriticalSection(&g_Lock);
	if(g_BackupBusy || g_GameRunning)
	{
		LeaveCriticalSection(&g_Lock);
		delete pWork;
		return;
	}
	g_BackupBusy = true;
	g_BackupError.clear();
	LeaveCriticalSection(&g_Lock);
	HANDLE hThread = CreateThread(nullptr, 0, BackupThread, pWork, 0, nullptr);
	if(hThread)
		CloseHandle(hThread);
	else
	{
		delete pWork;
		EnterCriticalSection(&g_Lock);
		g_BackupBusy = false;
		g_BackupError = "Could not start the backup operation.";
		LeaveCriticalSection(&g_Lock);
	}
}

static bool HttpDownloadFile(const std::wstring &Url, const std::wstring &DestPath, uint64_t ExpectedSize, const std::string &ExpectedSha256)
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
	if(Ok)
	{
		wchar_t aFinalUrl[2048] = {};
		DWORD FinalUrlBytes = sizeof(aFinalUrl);
		if(!WinHttpQueryOption(hRequest, WINHTTP_OPTION_URL, aFinalUrl, &FinalUrlBytes) ||
			!AllowedUpdateUrl(WideToUtf8(aFinalUrl)))
			Ok = FALSE;
	}

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
	if(Ok && ContentLen > 0 && ExpectedSize > 0 && ContentLen != ExpectedSize)
		Ok = FALSE;

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
			if(fwrite(Buf.data(), 1, Read, pFile) != Read)
			{
				Ok = FALSE;
				break;
			}
			Total += Read;
			if(ExpectedSize > 0 && Total > ExpectedSize)
			{
				Ok = FALSE;
				break;
			}
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

	if(!Ok || Total < 4 || (ExpectedSize > 0 && Total != ExpectedSize))
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
	std::vector<unsigned char> Downloaded;
	std::string ActualSha256;
	if(!ReadBinaryFile(DestPath, Downloaded) || !Sha256Hex(Downloaded, ActualSha256) ||
		_stricmp(ActualSha256.c_str(), ExpectedSha256.c_str()) != 0)
	{
		DeleteFileW(DestPath.c_str());
		return false;
	}
	return true;
}

static bool InspectLauncherUpdateTree(const std::wstring &Directory, std::wstring &ExecutablePath, std::wstring &ManifestPath)
{
	WIN32_FIND_DATAW Data = {};
	HANDLE hFind = FindFirstFileW((Directory + L"\\*").c_str(), &Data);
	if(hFind == INVALID_HANDLE_VALUE)
		return false;
	bool Valid = true;
	do
	{
		if(!wcscmp(Data.cFileName, L".") || !wcscmp(Data.cFileName, L".."))
			continue;
		if(Data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
		{
			Valid = false;
			break;
		}
		const std::wstring Path = JoinPath(Directory, Data.cFileName);
		if(Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if(!InspectLauncherUpdateTree(Path, ExecutablePath, ManifestPath))
			{
				Valid = false;
				break;
			}
			continue;
		}
		if(_wcsicmp(Data.cFileName, L"UClient.exe") == 0 && ExecutablePath.empty())
			ExecutablePath = Path;
		else if(_wcsicmp(Data.cFileName, kBuildManifestFile) == 0 && ManifestPath.empty())
			ManifestPath = Path;
		else
		{
			Valid = false;
			break;
		}
	} while(FindNextFileW(hFind, &Data));
	FindClose(hFind);
	return Valid;
}

static bool ApplyLauncherUpdate(LauncherArgs *pA, const UpdateMetadata &Metadata)
{
	if(!pA || Metadata.Version.empty())
		return false;
	SetPhase(EUiPhase::Updating);
	g_UpdateStage = EUpdateStage::Download;
	SetButtonLabel(L"Updating launcher");
	SetStatus(L"Downloading launcher update...");
	SetPercent(0);

	const std::wstring ArchivePath = JoinPath(pA->InstallDir, kLauncherArchiveRel);
	if(!HttpDownloadFile(Utf8ToWide(Metadata.Url.c_str()), ArchivePath, Metadata.Size, Metadata.Sha256))
	{
		SetStatus(L"Launcher update download or verification failed");
		g_Failed = true;
		return false;
	}
	if(!ValidateArchiveEntries(ArchivePath, true))
	{
		SetStatus(L"Launcher update archive is unsafe");
		g_Failed = true;
		DeleteFileW(ArchivePath.c_str());
		return false;
	}

	const std::wstring ExtractDir = JoinPath(pA->InstallDir, L"update\\launcher-extract");
	DeleteTree(ExtractDir.c_str());
	EnsureDirectoryTree(ExtractDir);
	wchar_t aCommand[1024];
	_snwprintf_s(aCommand, _TRUNCATE, L"tar.exe -xf \"%ls\" -C \"%ls\"", ArchivePath.c_str(), ExtractDir.c_str());
	if(RunProcess(aCommand) != 0)
	{
		SetStatus(L"Could not extract launcher update");
		g_Failed = true;
		DeleteTree(ExtractDir.c_str());
		DeleteFileW(ArchivePath.c_str());
		return false;
	}

	std::wstring NewExecutable;
	std::wstring BuildManifestPath;
	if(!InspectLauncherUpdateTree(ExtractDir, NewExecutable, BuildManifestPath) ||
		NewExecutable.empty() || BuildManifestPath.empty())
	{
		SetStatus(L"Launcher update contains unexpected files");
		g_Failed = true;
		DeleteTree(ExtractDir.c_str());
		DeleteFileW(ArchivePath.c_str());
		return false;
	}
	std::string BuildManifest;
	std::string BuiltVersion;
	if(!ReadTextFile(BuildManifestPath, BuildManifest) ||
		!ExtractJsonString(BuildManifest, "launcherVersion", BuiltVersion) ||
		BuiltVersion != Metadata.Version)
	{
		SetStatus(L"Built launcher version does not match update metadata");
		g_Failed = true;
		DeleteTree(ExtractDir.c_str());
		DeleteFileW(ArchivePath.c_str());
		return false;
	}

	g_UpdateStage = EUpdateStage::Apply;
	SetStatus(L"Installing launcher update...");
	SetPercent(80);
	const std::wstring OldPath = pA->SelfPath + L".old";
	DeleteFileW(OldPath.c_str());
	const bool MovedOld = MoveFileExW(pA->SelfPath.c_str(), OldPath.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
	if(!MovedOld || !CopyFileW(NewExecutable.c_str(), pA->SelfPath.c_str(), FALSE))
	{
		if(MovedOld)
		{
			DeleteFileW(pA->SelfPath.c_str());
			MoveFileExW(OldPath.c_str(), pA->SelfPath.c_str(), MOVEFILE_REPLACE_EXISTING);
		}
		SetStatus(L"Could not replace launcher");
		g_Failed = true;
		return false;
	}

	const std::wstring PendingPath = JoinPath(pA->InstallDir, L"update\\launcher.pending");
	WriteTextFile(PendingPath, Metadata.Version);
	DeleteTree(ExtractDir.c_str());
	DeleteFileW(ArchivePath.c_str());

	wchar_t aEventName[128];
	_snwprintf_s(aEventName, _TRUNCATE, L"Local\\UClientLauncherUpdate_%lu_%lu", GetCurrentProcessId(), GetTickCount());
	HANDLE hReadyEvent = CreateEventW(nullptr, TRUE, FALSE, aEventName);
	if(!hReadyEvent)
	{
		DeleteFileW(pA->SelfPath.c_str());
		MoveFileExW(OldPath.c_str(), pA->SelfPath.c_str(), MOVEFILE_REPLACE_EXISTING);
		DeleteFileW(PendingPath.c_str());
		SetStatus(L"Could not prepare launcher restart");
		g_Failed = true;
		return false;
	}

	if(g_hSingleInstanceMutex)
	{
		CloseHandle(g_hSingleInstanceMutex);
		g_hSingleInstanceMutex = nullptr;
	}
	std::vector<std::wstring> RestartArgs = pA->ForwardArgs;
	RestartArgs.emplace_back(kLauncherUpdateEventArg);
	RestartArgs.emplace_back(aEventName);
	HANDLE hNewProcess = nullptr;
	if(!LaunchProcess(pA->SelfPath, RestartArgs, pA->InstallDir, false, &hNewProcess))
	{
		CloseHandle(hReadyEvent);
		DeleteFileW(pA->SelfPath.c_str());
		MoveFileExW(OldPath.c_str(), pA->SelfPath.c_str(), MOVEFILE_REPLACE_EXISTING);
		DeleteFileW(PendingPath.c_str());
		g_hSingleInstanceMutex = CreateMutexW(nullptr, TRUE, SingleInstanceMutexName(pA->InstallDir).c_str());
		SetStatus(L"Could not restart updated launcher");
		g_Failed = true;
		return false;
	}
	const bool Restarted = WaitForSingleObject(hReadyEvent, 15000) == WAIT_OBJECT_0;
	CloseHandle(hReadyEvent);
	if(!Restarted)
	{
		TerminateProcess(hNewProcess, 1);
		WaitForSingleObject(hNewProcess, 5000);
		CloseHandle(hNewProcess);
		DeleteFileW(pA->SelfPath.c_str());
		MoveFileExW(OldPath.c_str(), pA->SelfPath.c_str(), MOVEFILE_REPLACE_EXISTING);
		DeleteFileW(PendingPath.c_str());
		g_hSingleInstanceMutex = CreateMutexW(nullptr, TRUE, SingleInstanceMutexName(pA->InstallDir).c_str());
		SetStatus(L"Updated launcher did not start; the previous version was restored");
		g_Failed = true;
		return false;
	}
	CloseHandle(hNewProcess);
	SetPercent(100);
	if(g_hWnd)
		PostMessage(g_hWnd, WM_CLOSE, 0, 0);
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
	std::vector<std::wstring> RestoredArgs;
	char aLine[1024];
	while(fgets(aLine, sizeof(aLine), pFile))
	{
		size_t N = strlen(aLine);
		while(N > 0 && (aLine[N - 1] == '\n' || aLine[N - 1] == '\r'))
			aLine[--N] = '\0';
		if(N > 0)
			RestoredArgs.push_back(Utf8ToWide(aLine));
	}
	fclose(pFile);
	DeleteFileW(Pending.c_str());
	if(!RestoredArgs.empty())
	{
		EnterCriticalSection(&g_Lock);
		pA->ForwardArgs.insert(pA->ForwardArgs.end(), RestoredArgs.begin(), RestoredArgs.end());
		LeaveCriticalSection(&g_Lock);
	}
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

static bool RunUpdateDownload(LauncherArgs *pA, const UpdateMetadata &Metadata)
{
	if(!pA || Metadata.Version.empty() || Metadata.Url.empty())
		return false;

	SetPhase(EUiPhase::Updating);
	SetButtonLabel(L"Downloading");
	g_UpdateStage = EUpdateStage::Download;
	g_Failed = false;
	wchar_t aInfo[128];
	_snwprintf_s(aInfo, _TRUNCATE, L"Version %hs", Metadata.Version.c_str());
	SetStatus(aInfo);
	SetPercent(0);

	const std::wstring ArchivePath = JoinPath(pA->InstallDir, kClientArchiveRel);
	CreateDirectoryW(JoinPath(pA->InstallDir, L"update").c_str(), nullptr);

	if(!HttpDownloadFile(Utf8ToWide(Metadata.Url.c_str()), ArchivePath, Metadata.Size, Metadata.Sha256))
	{
		SetStatus(L"Download failed — you can still play the current version");
		g_Failed = true;
		g_UpdateStage = EUpdateStage::None;
		return false;
	}

	WriteTextFile(JoinPath(pA->InstallDir, kClientPendingVersionFile), Metadata.Version);
	SetStatus(L"");
	g_UpdateStage = EUpdateStage::Apply;
	SetButtonLabel(L"Applying update");
	if(!ApplyUpdateArchive(ArchivePath, pA->InstallDir, pA->SelfPath, Metadata.Version, true, false))
	{
		SetStatus(L"Update failed — you can still play");
		g_Failed = true;
		g_UpdateStage = EUpdateStage::None;
		return false;
	}

	WriteTextFile(JoinPath(pA->InstallDir, kVersionFile), Metadata.Version);
	SetVersionLabel(Metadata.Version);
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
	UpdateMetadata Metadata;
	EnterCriticalSection(&g_Lock);
	Metadata = g_PendingClientUpdate;
	LeaveCriticalSection(&g_Lock);

	LauncherArgs *pA = g_pArgs;
	const bool Ok = pA && RunUpdateDownload(pA, Metadata);
	if(Ok)
	{
		EnterCriticalSection(&g_Lock);
		g_UpdateAvailable = false;
		g_PendingClientUpdate = {};
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
	if(g_PendingClientUpdate.Version.empty() || g_PendingClientUpdate.Url.empty())
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

	const std::string LocalVersion = ResolveLocalClientVersion(g_pArgs->InstallDir);
	UpdateMetadata Metadata;
	bool NeedUpdate = false;
	if(FetchClientUpdateMetadata(Metadata))
	{
		if((Metadata.MinLauncherVersion.empty() ||
			   CompareVersions(UCLIENT_LAUNCHER_VERSION, Metadata.MinLauncherVersion) >= 0) &&
			CompareVersions(Metadata.Version, LocalVersion) > 0)
			NeedUpdate = true;
	}

	EnterCriticalSection(&g_Lock);
	g_UpdateAvailable = NeedUpdate;
	if(NeedUpdate)
	{
		g_PendingClientUpdate = Metadata;
	}
	else
		g_PendingClientUpdate = {};
	g_UpdateCheckRefreshing = false;
	LeaveCriticalSection(&g_Lock);

	RefreshGameRunningState();
	if(g_Phase == EUiPhase::Ready)
	{
		SyncReadyButtonLabel();
		if(NeedUpdate)
		{
			wchar_t aInfo[128];
			_snwprintf_s(aInfo, _TRUNCATE, L"Update available: %hs", Metadata.Version.c_str());
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
		const size_t ArchiveSlash = pA->ApplyArchive.find_last_of(L"\\/");
		const std::wstring ArchiveName = pA->ApplyArchive.substr(ArchiveSlash == std::wstring::npos ? 0 : ArchiveSlash + 1);
		const bool IsSeparatedClientArchive = _wcsicmp(ArchiveName.c_str(), L"uclient-client.zip") == 0;
		std::string ExpectedVersion;
		if(IsSeparatedClientArchive)
			ReadTextFile(JoinPath(pA->InstallDir, kClientPendingVersionFile), ExpectedVersion);
		if(!ApplyUpdateArchive(
			   pA->ApplyArchive,
			   pA->InstallDir,
			   pA->SelfPath,
			   ExpectedVersion,
			   IsSeparatedClientArchive,
			   !IsSeparatedClientArchive))
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
		if(IsAccountReady() && !g_PlayBlocked)
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

	UpdateMetadata LauncherMetadata;
	if(FetchUpdateMetadata(UCLIENT_LAUNCHER_UPDATE_LATEST_URL, "launcher", false, LauncherMetadata) &&
		CompareVersions(LauncherMetadata.Version, UCLIENT_LAUNCHER_VERSION) > 0)
	{
		if(ApplyLauncherUpdate(pA, LauncherMetadata))
			return 0;
		SetPhase(EUiPhase::Ready);
		g_UpdateStage = EUpdateStage::None;
		SetButtonLabel(kPlayLabel);
		SyncButtonHint();
		PostMessage(g_hWnd, WM_UPDATE_READY, 0, 0);
		return 0;
	}

	const std::string LocalVersion = ResolveLocalClientVersion(pA->InstallDir);
	SetVersionLabel(LocalVersion);
	RefreshLauncherNotices();

	UpdateMetadata ClientMetadata;
	bool NeedUpdate = false;

	if(FetchClientUpdateMetadata(ClientMetadata))
	{
		if(!ClientMetadata.MinLauncherVersion.empty() &&
			CompareVersions(UCLIENT_LAUNCHER_VERSION, ClientMetadata.MinLauncherVersion) < 0)
		{
			SetStatus(L"This client update requires a newer launcher");
			g_Failed = true;
		}
		else if(CompareVersions(ClientMetadata.Version, LocalVersion) > 0)
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
		g_PendingClientUpdate = ClientMetadata;
		LeaveCriticalSection(&g_Lock);
		wchar_t aInfo[128];
		_snwprintf_s(aInfo, _TRUNCATE, L"Update available: %hs", ClientMetadata.Version.c_str());
		SetStatus(aInfo);
	}

	RestorePendingArgs(pA);
	const std::string FinalVersion = ResolveLocalClientVersion(pA->InstallDir);
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
	if(!IsAccountReady() || g_PlayBlocked)
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
	LauncherArgs LaunchArgs;
	EnterCriticalSection(&g_Lock);
	LaunchArgs = *g_pArgs;
	g_pArgs->ForwardArgs.clear();
	LeaveCriticalSection(&g_Lock);
	HANDLE hProcess = nullptr;
	if(!LaunchGame(&LaunchArgs, &hProcess))
	{
		EnterCriticalSection(&g_Lock);
		g_pArgs->ForwardArgs.insert(
			g_pArgs->ForwardArgs.begin(),
			LaunchArgs.ForwardArgs.begin(),
			LaunchArgs.ForwardArgs.end());
		LeaveCriticalSection(&g_Lock);
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

static const char *AccountStateName(EAccountState State)
{
	switch(State)
	{
	case EAccountState::NeedsOnboarding: return "needs_onboarding";
	case EAccountState::ReadyAnonymous: return "ready_anonymous";
	case EAccountState::ReadyEmail: return "ready_email";
	case EAccountState::Busy: return "busy";
	case EAccountState::Error: return "error";
	case EAccountState::Banned: return "banned";
	default: return "checking";
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
	EAccountState AccountState;
	std::string AccountEmail, AccountError, SavedAccountInstallId, BackupError;
	bool HasSavedAccount = false;
	bool BackupBusy = false;
	uint64_t BackupUsed = 0, BackupLimit = 0;
	std::vector<BackupFileView> BackupFiles;
	std::vector<BackupVersionView> BackupVersions;
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
	AccountState = g_AccountState;
	AccountEmail = g_AccountEmail;
	AccountError = g_AccountError;
	HasSavedAccount = g_HasSavedAccount;
	SavedAccountInstallId = g_SavedAccountInstallId;
	BackupBusy = g_BackupBusy;
	BackupError = g_BackupError;
	BackupUsed = g_BackupUsed;
	BackupLimit = g_BackupLimit;
	BackupFiles = g_BackupFiles;
	BackupVersions = g_BackupVersions;
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
	JsonAddString(Json, "launcherVersion", UCLIENT_LAUNCHER_VERSION);
	JsonAddString(Json, "status", WideToUtf8(Status));
	JsonAddString(Json, "logoUrl", g_LogoUrl);
	JsonAddString(Json, "mascotUrl", g_MascotUrl);
	JsonAddString(Json, "accountState", AccountStateName(AccountState));
	JsonAddString(Json, "accountEmail", AccountEmail);
	JsonAddString(Json, "accountError", AccountError);
	JsonAddString(Json, "savedAccountInstallId", SavedAccountInstallId);
	JsonAddString(Json, "backupError", BackupError);

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
	Json += HasSavedAccount ? "\"hasSavedAccount\":true," : "\"hasSavedAccount\":false,";
	Json += BackupBusy ? "\"backupBusy\":true," : "\"backupBusy\":false,";
	_snprintf_s(aNum, sizeof(aNum), _TRUNCATE, "\"backupUsed\":%llu,", (unsigned long long)BackupUsed);
	Json += aNum;
	_snprintf_s(aNum, sizeof(aNum), _TRUNCATE, "\"backupLimit\":%llu,", (unsigned long long)BackupLimit);
	Json += aNum;
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

	Json += "\"backupFiles\":[";
	for(size_t i = 0; i < BackupFiles.size(); ++i)
	{
		if(i)
			Json += ",";
		Json += "{";
		JsonAddString(Json, "path", BackupFiles[i].Path);
		char aSize[64];
		_snprintf_s(aSize, _TRUNCATE, "\"size\":%llu}", (unsigned long long)BackupFiles[i].Size);
		Json += aSize;
	}
	Json += "],\"backupVersions\":[";
	for(size_t i = 0; i < BackupVersions.size(); ++i)
	{
		if(i)
			Json += ",";
		Json += "{";
		JsonAddString(Json, "id", BackupVersions[i].Id);
		JsonAddString(Json, "path", BackupVersions[i].Path);
		JsonAddString(Json, "createdAt", BackupVersions[i].CreatedAt);
		JsonAddString(Json, "sha256", BackupVersions[i].Sha256);
		char aSize[64];
		_snprintf_s(aSize, _TRUNCATE, "\"size\":%llu}", (unsigned long long)BackupVersions[i].Size);
		Json += aSize;
	}
	Json += "],";

	EnsureShortcutsLoaded();
	std::string ShortcutsArray;
	if(!ExtractJsonRawValue(g_ShortcutsFileJson, "shortcuts", ShortcutsArray))
		ShortcutsArray = "[]";
	Json += "\"shortcuts\":";
	Json += ShortcutsArray;
	Json += ",\"friends\":[";
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
		Json += Friends[i].Afk ? "\"afk\":true," : "\"afk\":false,";
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

static bool ExtractWebString(const std::string &Json, const char *pKey, std::string &Out)
{
	std::string Raw;
	if(!ExtractJsonString(Json, pKey, Raw))
		return false;
	Out = JsonUnescapeValue(Raw);
	return true;
}

static std::vector<std::string> ExtractWebStringArray(const std::string &Json, const char *pKey)
{
	std::vector<std::string> Out;
	const std::string Needle = std::string("\"") + pKey + "\"";
	size_t Pos = Json.find(Needle);
	if(Pos == std::string::npos || (Pos = Json.find('[', Pos + Needle.size())) == std::string::npos)
		return Out;
	for(++Pos; Pos < Json.size() && Json[Pos] != ']';)
	{
		while(Pos < Json.size() && Json[Pos] != '"' && Json[Pos] != ']')
			++Pos;
		if(Pos >= Json.size() || Json[Pos] == ']')
			break;
		const size_t Start = ++Pos;
		bool Escape = false;
		for(; Pos < Json.size(); ++Pos)
		{
			if(Json[Pos] == '"' && !Escape)
				break;
			Escape = Json[Pos] == '\\' && !Escape;
			if(Json[Pos] != '\\')
				Escape = false;
		}
		Out.push_back(JsonUnescapeValue(Json.substr(Start, Pos - Start)));
		if(Pos < Json.size())
			++Pos;
	}
	return Out;
}

static bool AccountAllowsPlay()
{
	return IsAccountReady();
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
		if(AccountAllowsPlay() && g_Phase == EUiPhase::Ready && !EffectivePlayBlocked())
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
		if(AccountAllowsPlay() && ExtractJsonString(Json, "address", Address) && !Address.empty() && g_Phase == EUiPhase::Ready && !EffectivePlayBlocked())
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
	else if(Cmd == "accountRetry")
	{
		RequestAccountCheck();
	}
	else if(Cmd == "accountRegisterEmail" || Cmd == "accountLoginEmail" || Cmd == "accountLinkEmail")
	{
		auto *pWork = new AccountWork();
		pWork->Op = Cmd == "accountRegisterEmail" ? EAccountOp::RegisterEmail :
			(Cmd == "accountLoginEmail" ? EAccountOp::LoginEmail : EAccountOp::LinkEmail);
		ExtractWebString(Json, "email", pWork->Email);
		ExtractWebString(Json, "password", pWork->Password);
		if(pWork->Op == EAccountOp::LinkEmail)
			BackupCredentials(pWork->InstallId, pWork->Secret);
		StartAccountWork(pWork);
	}
	else if(Cmd == "accountLoginKey")
	{
		auto *pWork = new AccountWork();
		pWork->Op = EAccountOp::LoginKey;
		ExtractWebString(Json, "installId", pWork->InstallId);
		ExtractWebString(Json, "accountKey", pWork->Secret);
		StartAccountWork(pWork);
	}
	else if(Cmd == "accountLoginSaved")
	{
		auto *pWork = new AccountWork();
		pWork->Op = EAccountOp::LoginSaved;
		StartAccountWork(pWork);
	}
	else if(Cmd == "accountRegisterAnonymous")
	{
		auto *pWork = new AccountWork();
		pWork->Op = EAccountOp::RegisterAnonymous;
		StartAccountWork(pWork);
	}
	else if(Cmd == "accountLogout")
	{
		RefreshGameRunningState();
		if(EffectiveGameRunning())
		{
			PushWebState(true);
			return;
		}
		auto *pWork = new AccountWork();
		pWork->Op = EAccountOp::Logout;
		StartAccountWork(pWork);
	}
	else if(Cmd == "backupRefresh")
	{
		auto *pWork = new BackupWork();
		pWork->Op = EBackupOp::Refresh;
		StartBackupWork(pWork);
	}
	else if(Cmd == "backupUpload")
	{
		auto *pWork = new BackupWork();
		pWork->Op = EBackupOp::Upload;
		pWork->Paths = ExtractWebStringArray(Json, "paths");
		StartBackupWork(pWork);
	}
	else if(Cmd == "backupRestore" || Cmd == "backupDelete")
	{
		auto *pWork = new BackupWork();
		pWork->Op = Cmd == "backupRestore" ? EBackupOp::Restore : EBackupOp::Delete;
		if(pWork->Op == EBackupOp::Restore)
		{
			pWork->Ids = ExtractWebStringArray(Json, "ids");
			if(pWork->Ids.empty() && ExtractWebString(Json, "id", pWork->Id))
				pWork->Ids.push_back(pWork->Id);
		}
		else
			ExtractWebString(Json, "id", pWork->Id);
		StartBackupWork(pWork);
	}
	else if(Cmd == "shortcutsSave")
	{
		std::string ShortcutsArray;
		if(ExtractJsonRawValue(Json, "shortcuts", ShortcutsArray))
		{
			SaveShortcutsDocument(ShortcutsArray);
			PushWebState(true);
		}
	}
	else if(Cmd == "shortcutsToggle")
	{
		std::string Id;
		const bool Enabled = Json.find("\"enabled\":true") != std::string::npos;
		if(ExtractJsonString(Json, "id", Id) && !Id.empty())
		{
			ToggleShortcutEnabled(Id, Enabled);
			PushWebState(true);
		}
	}
	else if(Cmd == "shortcutsDelete")
	{
		std::string Id;
		if(ExtractJsonString(Json, "id", Id) && !Id.empty())
		{
			DeleteShortcutById(Id);
			PushWebState(true);
		}
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
	g_hBgBmp = CreateCompatibleBitmap(Ref, g_WindowW, g_WindowH);
	if(!g_hBgBmp)
	{
		DeleteDC(g_hBgDc);
		g_hBgDc = nullptr;
		return;
	}
	g_hBgOld = SelectObject(g_hBgDc, g_hBgBmp);

	RECT Full = {0, 0, g_WindowW, g_WindowH};
	FillVerticalGradient(g_hBgDc, Full, RGB(16, 18, 26), RGB(5, 5, 8));
	DrawGlow(g_hBgDc, g_WindowW - 180, 110, 330, C_BLUE, 48);
	DrawGlow(g_hBgDc, RAIL_W + 240, g_WindowH - 10, 330, C_ORANGE, 36);
	DrawGlow(g_hBgDc, g_WindowW / 2 - 40, 250, 240, RGB(80, 36, 64), 28);

	RECT Side = {0, 0, RAIL_W, g_WindowH};
	HBRUSH SideBrush = CreateSolidBrush(C_SIDE);
	FillRect(g_hBgDc, &Side, SideBrush);
	DeleteObject(SideBrush);
	RECT RailEdge = {RAIL_W - 1, 0, RAIL_W, g_WindowH};
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
	HBITMAP Bmp = CreateCompatibleBitmap(Dc, g_WindowW, g_WindowH);
	HGDIOBJ Old = SelectObject(Mem, Bmp);

	EnsureBackground(Dc);
	if(g_hBgDc)
		BitBlt(Mem, 0, 0, g_WindowW, g_WindowH, g_hBgDc, 0, 0, SRCCOPY);

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

	g_CloseRc = {g_WindowW - 52, 12, g_WindowW - 14, 46};
	g_MinRc = {g_WindowW - 96, 12, g_WindowW - 58, 46};
	g_GearRc = {RAIL_W / 2 - 20, g_WindowH - 64, RAIL_W / 2 + 20, g_WindowH - 24};

	const int PanelL = g_WindowW - PANEL_W - 24;
	const int ContentL = RAIL_W + 44;
	const int ContentR = PanelL - 32;

	g_BackRc = {ContentL, g_WindowH - 84, ContentL + 128, g_WindowH - 44};
	g_CheckRc = {ContentL + 4, 176, ContentR, 262};
	g_AutoUpdateCheckRc = {ContentL + 4, 280, ContentR, 366};
	g_DiscordCheckRc = {ContentL + 4, 428, ContentR, 514};
	g_FriendAreaRc = {PanelL + 12, 182, g_WindowW - 36, g_WindowH - 44};
	g_FriendRefreshRc = {g_WindowW - 78, 68, g_WindowW - 44, 102};

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
		g_PlayBtnRc = {ContentL, g_WindowH - 132 + Lift, ContentL + BtnW, g_WindowH - 80 + Lift};
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

	RECT Panel = {PanelL, 52, g_WindowW - 24, g_WindowH - 28};
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

	RECT Div = {PanelL + 24, 170, g_WindowW - 48, 171};
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
			FillRoundRect(Mem, Dot, 10, Friends[i].Online ? (Friends[i].Afk ? RGB(232, 185, 75) : RGB(90, 210, 130)) : RGB(88, 90, 100));

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

		const bool CanPlay = IsAccountReady() && g_Phase == EUiPhase::Ready && !EffectivePlayBlocked();
		const bool BtnActive = CanPlay;
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

	if(!IsAccountReady())
	{
		RECT Overlay = {0, 0, g_WindowW, g_WindowH};
		HBRUSH OverlayBrush = CreateSolidBrush(RGB(8, 9, 13));
		FillRect(Mem, &Overlay, OverlayBrush);
		DeleteObject(OverlayBrush);
		SelectObject(Mem, TitleFont);
		SetTextColor(Mem, C_TITLE);
		RECT Title = {160, 235, g_WindowW - 160, 290};
		DrawTextW(Mem, L"Account setup required", -1, &Title, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		SelectObject(Mem, BodyFont);
		SetTextColor(Mem, C_DIM);
		RECT Help = {180, 300, g_WindowW - 180, 390};
		DrawTextW(Mem, L"Account onboarding requires Microsoft Edge WebView2.\nInstall or repair the WebView2 Runtime, then restart UClient Launcher.", -1, &Help, DT_CENTER | DT_WORDBREAK);
		DrawCaptionButton(Mem, g_CloseRc, g_CloseHover, true);
		DrawCaptionButton(Mem, g_MinRc, g_MinHover, false);
		g_PlayBtnRc = {};
	}

	SelectObject(Mem, GetStockObject(SYSTEM_FONT));
	BitBlt(Dc, 0, 0, g_WindowW, g_WindowH, Mem, 0, 0, SRCCOPY);
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
		else if(PtInRectI(g_PlayBtnRc, Pt.x, Pt.y) && g_Phase == EUiPhase::Ready && !EffectivePlayBlocked())
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
	case WM_COPYDATA:
	{
		const COPYDATASTRUCT *pCopy = reinterpret_cast<const COPYDATASTRUCT *>(lParam);
		if(!pCopy || pCopy->dwData != COPYDATA_FORWARD_LAUNCH_ARG ||
			!pCopy->lpData || pCopy->cbData < sizeof(wchar_t) ||
			pCopy->cbData > 32768 * sizeof(wchar_t) ||
			pCopy->cbData % sizeof(wchar_t) != 0)
			return FALSE;
		const size_t Chars = pCopy->cbData / sizeof(wchar_t);
		const wchar_t *pArg = static_cast<const wchar_t *>(pCopy->lpData);
		if(pArg[Chars - 1] != L'\0' || wcsnlen_s(pArg, Chars) != Chars - 1)
			return FALSE;
		const std::wstring Arg(pArg, Chars - 1);
		if(!IsForwardableShellArg(Arg) || !g_pArgs)
			return FALSE;
		EnterCriticalSection(&g_Lock);
		g_pArgs->ForwardArgs.push_back(Arg);
		LeaveCriticalSection(&g_Lock);
		ShowLauncherWindow(hWnd);
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
	case WM_ACCOUNT_READY:
		InvalidateRect(hWnd, nullptr, FALSE);
		PushWebState(true);
		if(IsAccountReady())
		{
			EnterCriticalSection(&g_Lock);
			const bool HasEmail = g_AccountState == EAccountState::ReadyEmail;
			LeaveCriticalSection(&g_Lock);
			if(HasEmail)
			{
				auto *pWork = new BackupWork();
				pWork->Op = EBackupOp::Refresh;
				StartBackupWork(pWork);
			}
			if(g_AutoLaunch && g_LaunchedFromGame && g_Phase == EUiPhase::Ready &&
				!EffectivePlayBlocked() && g_pArgs && g_pArgs->ApplyArchive.empty())
				RequestLaunchGame();
		}
		return 0;
	case WM_BACKUP_READY:
		InvalidateRect(hWnd, nullptr, FALSE);
		PushWebState(true);
		return 0;
	case WM_UPDATE_READY:
		InvalidateRect(hWnd, nullptr, FALSE);
		PushWebState();
		RequestFriendsRefresh();
		RequestNoticesRefresh();
		TryStartupAutoUpdate();
		if(IsAccountReady() && g_AutoLaunch && g_LaunchedFromGame && g_Phase == EUiPhase::Ready && !EffectivePlayBlocked() && g_pArgs && g_pArgs->ApplyArchive.empty())
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
		const bool PlayHover = !g_ShowSettings && PtInRectI(g_PlayBtnRc, X, Y) && g_Phase == EUiPhase::Ready && !EffectivePlayBlocked();
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
			RequestLaunchGame();
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
			if(!Addr.empty() && g_Phase == EUiPhase::Ready && !EffectivePlayBlocked())
				RequestLaunchGame(Addr.c_str());
			return 0;
		}
		if(PtInRectI(g_PlayBtnRc, X, Y) && g_Phase == EUiPhase::Ready && !EffectivePlayBlocked())
			RequestLaunchGame();
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
		}
		else if(wParam == VK_RETURN && g_Phase == EUiPhase::Ready && !g_ShowSettings && !EffectivePlayBlocked())
			RequestLaunchGame();
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

static bool IsForwardableShellArg(const std::wstring &Arg)
{
	if(Arg.empty())
		return false;
	std::wstring Lower = Arg;
	std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](wchar_t Ch) {
		return (wchar_t)towlower(Ch);
	});
	const auto EndsWith = [&](const wchar_t *pSuffix) {
		const size_t SuffixLength = wcslen(pSuffix);
		return Lower.size() >= SuffixLength &&
			Lower.compare(Lower.size() - SuffixLength, SuffixLength, pSuffix) == 0;
	};
	return Lower.rfind(L"ddnet://", 0) == 0 || EndsWith(L".demo") || EndsWith(L".map");
}

static void ForwardShellArgsToExistingLauncher(HWND hWnd, const std::vector<std::wstring> &ForwardArgs)
{
	for(const std::wstring &Arg : ForwardArgs)
	{
		if(!IsForwardableShellArg(Arg))
			continue;
		COPYDATASTRUCT Copy = {};
		Copy.dwData = COPYDATA_FORWARD_LAUNCH_ARG;
		Copy.cbData = (DWORD)((Arg.size() + 1) * sizeof(wchar_t));
		Copy.lpData = const_cast<wchar_t *>(Arg.c_str());
		DWORD_PTR Result = 0;
		SendMessageTimeoutW(
			hWnd,
			WM_COPYDATA,
			0,
			reinterpret_cast<LPARAM>(&Copy),
			SMTO_ABORTIFHUNG | SMTO_BLOCK,
			3000,
			&Result);
	}
}

// Returns true when this process should continue starting a new launcher window.
static bool AcquireSingleInstanceOrActivateExisting(const std::wstring &InstallDir, const std::vector<std::wstring> &ForwardArgs)
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
				ForwardShellArgsToExistingLauncher(hExisting, ForwardArgs);
				ActivateExistingLauncherWindow(hExisting);
				break;
			}
			Sleep(50);
		}
		return false;
	}
	return true;
}

static void ConfigureWindowBounds(int &X, int &Y)
{
	POINT Cursor = {};
	GetCursorPos(&Cursor);
	const HMONITOR Monitor = MonitorFromPoint(Cursor, MONITOR_DEFAULTTOPRIMARY);
	MONITORINFO Info = {};
	Info.cbSize = sizeof(Info);
	if(!GetMonitorInfoW(Monitor, &Info))
	{
		Info.rcMonitor = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
		Info.rcWork = Info.rcMonitor;
	}

	const int MonitorW = Info.rcMonitor.right - Info.rcMonitor.left;
	const int MonitorH = Info.rcMonitor.bottom - Info.rcMonitor.top;
	const double ResolutionScale = std::min(
		(double)MonitorW / REFERENCE_SCREEN_W,
		(double)MonitorH / REFERENCE_SCREEN_H);
	g_WindowW = std::max(960, (int)std::lround(REFERENCE_WND_W * ResolutionScale));
	g_WindowH = std::max(600, (int)std::lround(REFERENCE_WND_H * ResolutionScale));

	const int WorkW = Info.rcWork.right - Info.rcWork.left;
	const int WorkH = Info.rcWork.bottom - Info.rcWork.top;
	const double FitScale = std::min(
		(double)std::max(1, WorkW - 24) / g_WindowW,
		(double)std::max(1, WorkH - 24) / g_WindowH);
	if(FitScale < 1.0)
	{
		g_WindowW = std::max(640, (int)std::lround(g_WindowW * FitScale));
		g_WindowH = std::max(400, (int)std::lround(g_WindowH * FitScale));
	}
	X = Info.rcWork.left + (WorkW - g_WindowW) / 2;
	Y = Info.rcWork.top + (WorkH - g_WindowH) / 2;
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
		if(!wcscmp(ppArgv[i], kWriteVersionInfoArg) && i + 1 < Argc)
		{
			g_pArgs->VersionInfoPath = ppArgv[++i];
			continue;
		}
		if(!wcscmp(ppArgv[i], kLauncherUpdateEventArg) && i + 1 < Argc)
		{
			g_pArgs->LauncherUpdateEvent = ppArgv[++i];
			continue;
		}
		g_pArgs->ForwardArgs.emplace_back(ppArgv[i]);
	}
	LocalFree(ppArgv);
	if(!g_pArgs->VersionInfoPath.empty())
	{
		const std::string Json = std::string("{\"component\":\"launcher\",\"clientVersion\":\"") +
			UCLIENT_CLIENT_VERSION + "\",\"launcherVersion\":\"" + UCLIENT_LAUNCHER_VERSION +
			"\",\"platform\":\"windows\",\"architecture\":\"x86_64\"}";
		const bool Ok = WriteTextFile(g_pArgs->VersionInfoPath, Json);
		delete g_pArgs;
		g_pArgs = nullptr;
		DeleteCriticalSection(&g_Lock);
		CoUninitialize();
		return Ok ? 0 : 1;
	}

	LoadLauncherSettings(g_InstallDir);
	LoadLauncherArt(g_InstallDir);
	SetVersionLabel(ResolveLocalClientVersion(g_InstallDir));

	if(g_pArgs->ApplyArchive.empty() && !AcquireSingleInstanceOrActivateExisting(g_InstallDir, g_pArgs->ForwardArgs))
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

	int X = 0;
	int Y = 0;
	ConfigureWindowBounds(X, Y);
	g_hWnd = CreateWindowExW(WS_EX_APPWINDOW, L"UClientLauncher", L"UClient Launcher",
		WS_POPUP | WS_VISIBLE, X, Y, g_WindowW, g_WindowH, nullptr, nullptr, hInst, nullptr);
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
	RequestAccountCheck();

	HANDLE hThread = CreateThread(nullptr, 0, WorkerThread, g_pArgs, 0, nullptr);
	if(hThread)
		CloseHandle(hThread);
	if(!g_pArgs->LauncherUpdateEvent.empty())
	{
		HANDLE hReadyEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, g_pArgs->LauncherUpdateEvent.c_str());
		if(hReadyEvent)
		{
			SetEvent(hReadyEvent);
			CloseHandle(hReadyEvent);
		}
	}
	FinalizePendingLauncherUpdate(g_InstallDir, g_pArgs->SelfPath);

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
