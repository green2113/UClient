#include "uclient_launch_gate.h"

#include <base/fs.h>
#include <base/io.h>
#include <base/log.h>
#include <base/process.h>
#include <base/str.h>
#include <base/system.h>
#include <base/time.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <optional>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>
#include <windows.h>
#endif

static void JoinPath(char *pBuf, int BufSize, const char *pDir, const char *pFile)
{
	const int Len = str_length(pDir);
#if defined(CONF_FAMILY_WINDOWS)
	const char Sep = '\\';
#else
	const char Sep = '/';
#endif
	if(Len > 0 && (pDir[Len - 1] == '/' || pDir[Len - 1] == '\\'))
		str_format(pBuf, BufSize, "%s%s", pDir, pFile);
	else
		str_format(pBuf, BufSize, "%s%c%s", pDir, Sep, pFile);
}

bool UClientLaunchGate_FindInstallDir(char *pBuf, int BufSize)
{
	if(!pBuf || BufSize <= 0)
		return false;
	if(fs_executable_path(pBuf, BufSize) != 0)
		return false;
	return fs_parent_dir(pBuf) == 0;
}

#if defined(CONF_FAMILY_WINDOWS)
// Resolve launcher via GetModuleFileNameW to avoid UTF-8 path round-trip issues.
static bool FindLauncherPathWindows(char *pBuf, int BufSize, wchar_t *pWideOut, int WideOutSize)
{
	wchar_t aWide[IO_MAX_PATH_LENGTH];
	SetLastError(ERROR_SUCCESS);
	const DWORD Len = GetModuleFileNameW(nullptr, aWide, std::size(aWide));
	if(Len == 0 || Len >= std::size(aWide))
		return false;

	wchar_t *pSlash = wcsrchr(aWide, L'\\');
	if(!pSlash)
		pSlash = wcsrchr(aWide, L'/');
	if(!pSlash)
		return false;
	wcscpy_s(pSlash + 1, std::size(aWide) - (size_t)(pSlash + 1 - aWide), L"UClient.exe");

	if(pWideOut && WideOutSize > 0)
		wcsncpy_s(pWideOut, WideOutSize, aWide, _TRUNCATE);

	const DWORD Attr = GetFileAttributesW(aWide);
	if(Attr == INVALID_FILE_ATTRIBUTES || (Attr & FILE_ATTRIBUTE_DIRECTORY))
	{
		if(pBuf && BufSize > 0)
		{
			const std::optional<std::string> Path = windows_wide_to_utf8(aWide);
			if(Path.has_value())
				str_copy(pBuf, Path.value().c_str(), BufSize);
			else
				pBuf[0] = '\0';
		}
		return false;
	}

	const std::optional<std::string> Path = windows_wide_to_utf8(aWide);
	if(!Path.has_value())
		return false;
	str_copy(pBuf, Path.value().c_str(), BufSize);
	return true;
}
#endif

bool UClientLaunchGate_FindLauncherPath(char *pBuf, int BufSize)
{
#if defined(CONF_FAMILY_WINDOWS)
	return FindLauncherPathWindows(pBuf, BufSize, nullptr, 0);
#else
	char aDir[IO_MAX_PATH_LENGTH];
	if(!UClientLaunchGate_FindInstallDir(aDir, sizeof(aDir)))
		return false;
	JoinPath(pBuf, BufSize, aDir, UCLIENT_LAUNCHER_EXEC);
	return fs_is_file(pBuf) != 0;
#endif
}

bool UClientLaunchGate_FindGamePath(char *pBuf, int BufSize)
{
	char aDir[IO_MAX_PATH_LENGTH];
	if(!UClientLaunchGate_FindInstallDir(aDir, sizeof(aDir)))
		return false;
	JoinPath(pBuf, BufSize, aDir, UCLIENT_GAME_EXEC);
	return fs_is_file(pBuf) != 0;
}

static bool GenerateToken(char *pTokenOut, int TokenOutSize)
{
	if(!pTokenOut || TokenOutSize < 33)
		return false;

	unsigned Seed = (unsigned)time(nullptr) ^ ((unsigned)process_id() << 16) ^ 0xA5C3u;
#if defined(CONF_FAMILY_WINDOWS)
	LARGE_INTEGER Perf = {};
	QueryPerformanceCounter(&Perf);
	Seed ^= (unsigned)Perf.LowPart ^ (unsigned)(Perf.HighPart << 8);
#endif
	for(int i = 0; i < 16; ++i)
	{
		Seed = Seed * 1664525u + 1013904223u;
		str_format(pTokenOut + i * 2, TokenOutSize - i * 2, "%02x", (Seed >> 16) & 0xFFu);
	}
	return true;
}

bool UClientLaunchGate_WriteToken(const char *pInstallDir, char *pTokenOut, int TokenOutSize)
{
	if(!pInstallDir || !pTokenOut || TokenOutSize < 33)
		return false;
	if(!GenerateToken(pTokenOut, TokenOutSize))
		return false;

	char aPath[IO_MAX_PATH_LENGTH];
	JoinPath(aPath, sizeof(aPath), pInstallDir, UCLIENT_LAUNCH_TOKEN_FILE);

	IOHANDLE File = io_open(aPath, IOFLAG_WRITE);
	if(!File)
		return false;

	char aLine[128];
	str_format(aLine, sizeof(aLine), "%s\n%lld\n", pTokenOut, (long long)time(nullptr));
	io_write(File, aLine, str_length(aLine));
	io_close(File);
	return true;
}

static bool ReadTokenFile(const char *pInstallDir, char *pTokenOut, int TokenOutSize, int64_t *pCreated)
{
	char aPath[IO_MAX_PATH_LENGTH];
	JoinPath(aPath, sizeof(aPath), pInstallDir, UCLIENT_LAUNCH_TOKEN_FILE);
	IOHANDLE File = io_open(aPath, IOFLAG_READ);
	if(!File)
		return false;

	char aBuf[256];
	const unsigned Read = io_read(File, aBuf, sizeof(aBuf) - 1);
	io_close(File);
	if(Read == 0)
		return false;
	aBuf[Read] = '\0';

	int Nl = -1;
	for(unsigned i = 0; i < Read; ++i)
	{
		if(aBuf[i] == '\n')
		{
			Nl = (int)i;
			break;
		}
	}
	if(Nl < 0)
		return false;
	aBuf[Nl] = '\0';
	if(Nl > 0 && aBuf[Nl - 1] == '\r')
		aBuf[Nl - 1] = '\0';
	str_copy(pTokenOut, aBuf, TokenOutSize);

	const char *pTs = aBuf + Nl + 1;
	while(*pTs == '\r' || *pTs == '\n' || *pTs == ' ')
		++pTs;
	*pCreated = (int64_t)strtoll(pTs, nullptr, 10);
	return pTokenOut[0] != '\0' && *pCreated > 0;
}

static void DeleteTokenFile(const char *pInstallDir)
{
	char aPath[IO_MAX_PATH_LENGTH];
	JoinPath(aPath, sizeof(aPath), pInstallDir, UCLIENT_LAUNCH_TOKEN_FILE);
	fs_remove(aPath);
}

static int FindLaunchTokenArg(int Argc, const char **ppArgv)
{
	for(int i = 1; i < Argc - 1; ++i)
	{
		if(str_comp(ppArgv[i], UCLIENT_LAUNCH_TOKEN_ARG) == 0)
			return i;
	}
	return -1;
}

static void StripArgPair(int *pArgc, const char ***ppArgv, int Index)
{
	const int Argc = *pArgc;
	const char **ppArgvLocal = *ppArgv;
	for(int i = Index; i + 2 < Argc; ++i)
		ppArgvLocal[i] = ppArgvLocal[i + 2];
	*pArgc = Argc - 2;
}

static bool EnvAllowsDirectLaunch()
{
	const char *pEnv = getenv(UCLIENT_LAUNCH_ALLOW_ENV);
	return pEnv && pEnv[0] == '1' && pEnv[1] == '\0';
}

#if defined(CONF_FAMILY_WINDOWS)
static bool SpawnLauncherWithArgs(const char *pLauncherPath, int Argc, const char **ppArgv, int SkipIndex)
{
	std::vector<const char *> vArgs;
	for(int i = 1; i < Argc; ++i)
	{
		if(i == SkipIndex || i == SkipIndex + 1)
			continue;
		vArgs.push_back(ppArgv[i]);
	}

	const char **ppArgs = vArgs.empty() ? nullptr : vArgs.data();
	return process_execute(pLauncherPath, EShellExecuteWindowState::FOREGROUND, ppArgs, vArgs.size()) != INVALID_PROCESS;
}
#endif

bool UClientLaunchGate_EnsureFromLauncher(int *pArgc, const char ***ppArgv)
{
#if !defined(CONF_FAMILY_WINDOWS)
	(void)pArgc;
	(void)ppArgv;
	return true;
#else
	if(!pArgc || !ppArgv || !*ppArgv)
		return true;

	if(EnvAllowsDirectLaunch())
	{
		const int TokenIdx = FindLaunchTokenArg(*pArgc, *ppArgv);
		if(TokenIdx >= 0)
			StripArgPair(pArgc, ppArgv, TokenIdx);
		return true;
	}

	char aInstallDir[IO_MAX_PATH_LENGTH];
	if(!UClientLaunchGate_FindInstallDir(aInstallDir, sizeof(aInstallDir)))
	{
		log_error("launch-gate", "Could not resolve install directory");
		return false;
	}

	const int TokenIdx = FindLaunchTokenArg(*pArgc, *ppArgv);
	if(TokenIdx >= 0)
	{
		const char *pProvided = (*ppArgv)[TokenIdx + 1];
		char aFileToken[64];
		int64_t Created = 0;
		if(ReadTokenFile(aInstallDir, aFileToken, sizeof(aFileToken), &Created) &&
			str_comp(pProvided, aFileToken) == 0)
		{
			const int64_t Now = (int64_t)time(nullptr);
			if(Now >= Created && (Now - Created) <= UCLIENT_LAUNCH_TOKEN_TTL_SECONDS)
			{
				DeleteTokenFile(aInstallDir);
				StripArgPair(pArgc, ppArgv, TokenIdx);
				return true;
			}
		}
		DeleteTokenFile(aInstallDir);
	}

	char aLauncher[IO_MAX_PATH_LENGTH];
	wchar_t aExpectedWide[IO_MAX_PATH_LENGTH] = L"";
	if(!FindLauncherPathWindows(aLauncher, sizeof(aLauncher), aExpectedWide, std::size(aExpectedWide)))
	{
		wchar_t aMsg[1024];
		if(aExpectedWide[0] != L'\0')
		{
			_snwprintf_s(aMsg, _TRUNCATE,
				L"UClient launcher was not found.\n\n"
				L"Expected:\n%ls\n\n"
				L"Please reinstall the client.",
				aExpectedWide);
		}
		else
		{
			wcsncpy_s(aMsg,
				L"UClient launcher was not found.\n"
				L"Please reinstall the client.",
				_TRUNCATE);
		}
		MessageBoxW(nullptr, aMsg, L"UClient", MB_OK | MB_ICONERROR);
		return false;
	}

	if(!SpawnLauncherWithArgs(aLauncher, *pArgc, *ppArgv, TokenIdx))
	{
		MessageBoxW(nullptr,
			L"Failed to start UClient.exe. Please run the launcher instead of DDNet.exe.",
			L"UClient", MB_OK | MB_ICONERROR);
		return false;
	}
	return false;
#endif
}
