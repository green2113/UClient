// UClient pre-game launcher — Win32 GUI.
// Checks for updates, applies via bestclient-updater.exe, then starts DDNet.exe
// with a one-time --uclient-from-launcher token. Also registers ddnet:// etc.

#include <windows.h>
#include <wingdi.h>
#include <winuser.h>
#include <shellapi.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "msimg32.lib")

#include <cctype>

#ifndef UCLIENT_LAUNCHER_VERSION
#define UCLIENT_LAUNCHER_VERSION "0.0.0"
#endif
#ifndef UCLIENT_UPDATE_LATEST_URL
#define UCLIENT_UPDATE_LATEST_URL "https://ddnet.under1111.com/api/uclient/update/latest"
#endif

static const wchar_t *kTokenArg = L"--uclient-from-launcher";
static const wchar_t *kTokenFile = L"uclient_launch.token";
static const wchar_t *kGameExe = L"DDNet.exe";
static const wchar_t *kUpdaterExe = L"bestclient-updater.exe";
static const wchar_t *kArchiveRel = L"update\\bestclient-release.zip";

static const int WND_W = 480;
static const int WND_H = 215;

static const COLORREF C_BG = RGB(22, 22, 22);
static const COLORREF C_GREEN = RGB(105, 190, 70);
static const COLORREF C_ORANGE = RGB(230, 80, 45);
static const COLORREF C_TITLE = RGB(235, 245, 232);
static const COLORREF C_DIM = RGB(140, 155, 135);
static const COLORREF C_BAR_BG = RGB(40, 40, 40);
static const COLORREF C_BAR_SHINE = RGB(155, 220, 105);
static const COLORREF C_ERROR = RGB(230, 75, 45);

static HWND g_hWnd = nullptr;
static std::atomic<int> g_Percent = 0;
static bool g_Failed = false;
static CRITICAL_SECTION g_Lock;
static wchar_t g_aStatus[256] = L"Starting...";

#define WM_WORKER_TICK (WM_APP + 0)
#define WM_WORKER_DONE (WM_APP + 1)

struct LauncherArgs
{
	std::wstring InstallDir;
	std::wstring SelfPath;
	std::vector<std::wstring> ForwardArgs;
};

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

static bool LaunchProcess(const std::wstring &Exe, const std::vector<std::wstring> &Args, const std::wstring &WorkDir, bool Wait)
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
	CloseHandle(Pi.hProcess);
	CloseHandle(Pi.hThread);
	return true;
}

static bool LaunchGame(const LauncherArgs *pA)
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

	const std::wstring Game = JoinPath(pA->InstallDir, kGameExe);
	SetStatus(L"Starting UClient...");
	SetPercent(100);
	if(!LaunchProcess(Game, Args, pA->InstallDir, false))
	{
		SetStatus(L"Failed to start DDNet.exe");
		g_Failed = true;
		return false;
	}
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

// ─── HTTP (WinHTTP) ───────────────────────────────────────────────────────────

static bool HttpGetToString(const std::wstring &Url, std::string &OutBody)
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

	OutBody.clear();
	if(Ok)
	{
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
			OutBody += Chunk;
		}
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return Ok == TRUE && !OutBody.empty();
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
			if(ContentLen > 0)
				SetPercent((int)((Total * 100ull) / ContentLen));
			else
				SetPercent((int)(Total / (256 * 1024)) % 100);
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

static DWORD WINAPI WorkerThread(LPVOID pParam)
{
	auto *pA = static_cast<LauncherArgs *>(pParam);

	RegisterShellHandlers(pA->SelfPath);

	SetStatus(L"Checking for updates...");
	SetPercent(5);

	char aUrlUtf8[512];
	_snprintf_s(aUrlUtf8, _TRUNCATE, "%s?t=%lld", UCLIENT_UPDATE_LATEST_URL, (long long)time(nullptr));
	const std::wstring aUrl = Utf8ToWide(aUrlUtf8);

	std::string Body;
	std::string RemoteVersion;
	std::string ArchiveUrl;
	bool NeedUpdate = false;

	if(HttpGetToString(aUrl, Body) && ExtractWindowsUrl(Body, RemoteVersion, ArchiveUrl))
	{
		if(CompareVersions(RemoteVersion, UCLIENT_LAUNCHER_VERSION) > 0)
			NeedUpdate = true;
	}
	else
	{
		SetStatus(L"Update check failed — starting game");
		Sleep(600);
	}

	if(NeedUpdate)
	{
		wchar_t aMsg[128];
		_snwprintf_s(aMsg, _TRUNCATE, L"Downloading UClient %hs...", RemoteVersion.c_str());
		SetStatus(aMsg);
		SetPercent(10);

		const std::wstring ArchivePath = JoinPath(pA->InstallDir, kArchiveRel);
		CreateDirectoryW(JoinPath(pA->InstallDir, L"update").c_str(), nullptr);

		if(!HttpDownloadFile(Utf8ToWide(ArchiveUrl.c_str()), ArchivePath))
		{
			SetStatus(L"Download failed — starting current version");
			g_Failed = true;
			Sleep(1200);
			if(!LaunchGame(pA))
				Sleep(2000);
			PostMessage(g_hWnd, WM_WORKER_DONE, 0, 0);
			delete pA;
			return 0;
		}

		SetStatus(L"Applying update...");
		SetPercent(95);

		const std::wstring Updater = JoinPath(pA->InstallDir, kUpdaterExe);
		wchar_t aPid[32];
		_snwprintf_s(aPid, _TRUNCATE, L"%lu", GetCurrentProcessId());

		std::vector<std::wstring> UpArgs;
		UpArgs.emplace_back(aPid);
		UpArgs.push_back(ArchivePath);
		UpArgs.push_back(pA->InstallDir);
		UpArgs.push_back(pA->SelfPath); // relaunch launcher, not game

		if(!LaunchProcess(Updater, UpArgs, pA->InstallDir, false))
		{
			SetStatus(L"Failed to start updater");
			g_Failed = true;
			Sleep(2000);
			PostMessage(g_hWnd, WM_WORKER_DONE, 0, 0);
			delete pA;
			return 0;
		}

		// Updater waits for us; exit so it can replace files. Forward args are lost
		// unless we persist them — write a small sidecar for the next launcher run.
		if(!pA->ForwardArgs.empty())
		{
			const std::wstring Pending = JoinPath(pA->InstallDir, L"uclient_launch_pending.args");
			FILE *pFile = nullptr;
			if(_wfopen_s(&pFile, Pending.c_str(), L"wb") == 0 && pFile)
			{
				for(const auto &A : pA->ForwardArgs)
				{
					const std::string U = WideToUtf8(A);
					fprintf(pFile, "%s\n", U.c_str());
				}
				fclose(pFile);
			}
		}

		PostMessage(g_hWnd, WM_WORKER_DONE, 0, 0);
		delete pA;
		return 0;
	}

	// Restore pending args from a previous update apply (if any).
	{
		const std::wstring Pending = JoinPath(pA->InstallDir, L"uclient_launch_pending.args");
		FILE *pFile = nullptr;
		if(_wfopen_s(&pFile, Pending.c_str(), L"rb") == 0 && pFile)
		{
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
	}

	SetStatus(L"Up to date — starting...");
	SetPercent(90);
	LaunchGame(pA);
	Sleep(400);
	PostMessage(g_hWnd, WM_WORKER_DONE, 0, 0);
	delete pA;
	return 0;
}

// ─── UI ───────────────────────────────────────────────────────────────────────

static void Paint(HWND hWnd)
{
	PAINTSTRUCT Ps;
	HDC Dc = BeginPaint(hWnd, &Ps);
	HDC Mem = CreateCompatibleDC(Dc);
	HBITMAP Bmp = CreateCompatibleBitmap(Dc, WND_W, WND_H);
	HGDIOBJ Old = SelectObject(Mem, Bmp);

	RECT Rc = {0, 0, WND_W, WND_H};
	HBRUSH Bg = CreateSolidBrush(C_BG);
	FillRect(Mem, &Rc, Bg);
	DeleteObject(Bg);

	SetBkMode(Mem, TRANSPARENT);
	HFONT TitleFont = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
	HFONT BodyFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

	SelectObject(Mem, TitleFont);
	SetTextColor(Mem, C_TITLE);
	TextOutW(Mem, 24, 28, L"UClient", 7);

	SelectObject(Mem, BodyFont);
	SetTextColor(Mem, g_Failed ? C_ERROR : C_DIM);
	wchar_t aStatus[256];
	EnterCriticalSection(&g_Lock);
	wcsncpy_s(aStatus, g_aStatus, _TRUNCATE);
	LeaveCriticalSection(&g_Lock);
	TextOutW(Mem, 24, 70, aStatus, (int)wcslen(aStatus));

	const int BarX = 24, BarY = 120, BarW = WND_W - 48, BarH = 18;
	HBRUSH BarBg = CreateSolidBrush(C_BAR_BG);
	RECT BarRc = {BarX, BarY, BarX + BarW, BarY + BarH};
	FillRect(Mem, &BarRc, BarBg);
	DeleteObject(BarBg);

	const int Pct = g_Percent.load();
	const int FillW = (BarW * Pct) / 100;
	if(FillW > 0)
	{
		TRIVERTEX Vert[2];
		Vert[0].x = BarX;
		Vert[0].y = BarY;
		Vert[0].Red = GetRValue(C_GREEN) << 8;
		Vert[0].Green = GetGValue(C_GREEN) << 8;
		Vert[0].Blue = GetBValue(C_GREEN) << 8;
		Vert[0].Alpha = 0;
		Vert[1].x = BarX + FillW;
		Vert[1].y = BarY + BarH;
		Vert[1].Red = GetRValue(C_ORANGE) << 8;
		Vert[1].Green = GetGValue(C_ORANGE) << 8;
		Vert[1].Blue = GetBValue(C_ORANGE) << 8;
		Vert[1].Alpha = 0;
		GRADIENT_RECT Gr = {0, 1};
		GradientFill(Mem, Vert, 2, &Gr, 1, GRADIENT_FILL_RECT_H);

		HPEN Shine = CreatePen(PS_SOLID, 1, C_BAR_SHINE);
		HGDIOBJ OldPen = SelectObject(Mem, Shine);
		MoveToEx(Mem, BarX, BarY + 1, nullptr);
		LineTo(Mem, BarX + FillW, BarY + 1);
		SelectObject(Mem, OldPen);
		DeleteObject(Shine);
	}

	wchar_t aPct[16];
	_snwprintf_s(aPct, _TRUNCATE, L"%d%%", Pct);
	SetTextColor(Mem, C_TITLE);
	TextOutW(Mem, BarX + BarW - 40, BarY + BarH + 8, aPct, (int)wcslen(aPct));

	DeleteObject(TitleFont);
	DeleteObject(BodyFont);
	BitBlt(Dc, 0, 0, WND_W, WND_H, Mem, 0, 0, SRCCOPY);
	SelectObject(Mem, Old);
	DeleteObject(Bmp);
	DeleteDC(Mem);
	EndPaint(hWnd, &Ps);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg)
	{
	case WM_PAINT:
		Paint(hWnd);
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_WORKER_TICK:
		InvalidateRect(hWnd, nullptr, FALSE);
		return 0;
	case WM_WORKER_DONE:
		DestroyWindow(hWnd);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_NCHITTEST:
		if(DefWindowProcW(hWnd, Msg, wParam, lParam) == HTCLIENT)
			return HTCAPTION;
		return DefWindowProcW(hWnd, Msg, wParam, lParam);
	case WM_KEYDOWN:
		if(wParam == VK_ESCAPE)
			DestroyWindow(hWnd);
		return 0;
	}
	return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
	InitializeCriticalSection(&g_Lock);

	int Argc = 0;
	LPWSTR *ppArgv = CommandLineToArgvW(GetCommandLineW(), &Argc);
	if(!ppArgv)
		return 1;

	wchar_t aSelf[MAX_PATH];
	GetModuleFileNameW(nullptr, aSelf, MAX_PATH);

	auto *pArgs = new LauncherArgs();
	pArgs->SelfPath = aSelf;
	pArgs->InstallDir = ParentDir(aSelf);
	for(int i = 1; i < Argc; ++i)
		pArgs->ForwardArgs.emplace_back(ppArgv[i]);
	LocalFree(ppArgv);

	WNDCLASSEXW Wc = {};
	Wc.cbSize = sizeof(Wc);
	Wc.lpfnWndProc = WndProc;
	Wc.hInstance = hInst;
	Wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	Wc.hbrBackground = nullptr;
	Wc.lpszClassName = L"UClientLauncher";
	Wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	RegisterClassExW(&Wc);

	const int X = (GetSystemMetrics(SM_CXSCREEN) - WND_W) / 2;
	const int Y = (GetSystemMetrics(SM_CYSCREEN) - WND_H) / 2;
	g_hWnd = CreateWindowExW(WS_EX_APPWINDOW, L"UClientLauncher", L"UClient",
		WS_POPUP | WS_VISIBLE, X, Y, WND_W, WND_H, nullptr, nullptr, hInst, nullptr);
	if(!g_hWnd)
		return 1;

	HANDLE hThread = CreateThread(nullptr, 0, WorkerThread, pArgs, 0, nullptr);
	if(hThread)
		CloseHandle(hThread);

	MSG Msg;
	while(GetMessageW(&Msg, nullptr, 0, 0))
	{
		TranslateMessage(&Msg);
		DispatchMessageW(&Msg);
	}

	DeleteCriticalSection(&g_Lock);
	return 0;
}
