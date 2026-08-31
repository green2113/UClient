/* Copyright © 2026 BestProject Team */
#include "updater.h"

#include <base/math.h>
#include <base/process.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/config.h>
#include <engine/external/json-parser/json.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/shared/uclient_launch_gate.h>
#include <engine/storage.h>

#include <game/client/components/bestclient/version.h>
#include <game/version.h>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <vector>

static bool StrEndsWithNoCase(const char *pStr, const char *pSuffix)
{
	if(!pStr || !pSuffix)
		return false;
	int StrLen = str_length(pStr);
	int SuffixLen = str_length(pSuffix);
	if(SuffixLen > StrLen)
		return false;
	return str_comp_nocase(pStr + StrLen - SuffixLen, pSuffix) == 0;
}

static constexpr const char *DEFAULT_UPDATE_LATEST_URL = "https://ddnet.under1111.com/api/uclient/update/latest";
static constexpr const char *GITHUB_LATEST_RELEASE_URL = "https://github.com/BestProjectTeam/BestClient/releases/latest";
static constexpr const char *UPDATE_SCRIPT_PATH = "update/apply_uclient_update.ps1";
#if defined(CONF_PLATFORM_ANDROID)
static constexpr const char *UPDATE_ARCHIVE_PATH = "update/bestclient-release.apk";
#else
static constexpr const char *UPDATE_ARCHIVE_PATH = "update/bestclient-release.zip";
#endif
static constexpr const char *UCLIENT_LAUNCHER_EXEC_WIN = "UClient.exe";
static constexpr const char *UCLIENT_APPLY_UPDATE_ARG = "--uclient-apply-update";
static constexpr const char *UCLIENT_WAIT_PID_ARG = "--uclient-wait-pid";
static constexpr const char *UCLIENT_PENDING_VERSION_FILE = "uclient_pending_version.txt";

static const char *CurrentPlatformKey()
{
#if defined(CONF_FAMILY_WINDOWS)
	return "windows";
#elif defined(CONF_PLATFORM_MACOS)
	return "macos";
#else
	return "linux";
#endif
}

static void BuildUpdateLatestUrl(char *pBuf, int BufSize)
{
	const char *pBase = g_Config.m_UcUpdateLatestUrl[0] != '\0' ? g_Config.m_UcUpdateLatestUrl : DEFAULT_UPDATE_LATEST_URL;
	const char *pSeparator = str_find(pBase, "?") ? "&" : "?";
	str_format(pBuf, BufSize, "%s%st=%lld", pBase, pSeparator, (long long)time_timestamp());
}

static std::string ToLowerAscii(const char *pStr)
{
	std::string Lower;
	if(!pStr)
		return Lower;

	for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pStr); *p != '\0'; ++p)
		Lower.push_back(static_cast<char>(std::tolower(*p)));
	return Lower;
}

static const char *JsonStringField(const json_value *pJson, const char *pField)
{
	if(!pJson || pJson->type != json_object)
		return nullptr;
	return json_string_get(json_object_get(pJson, pField));
}

static void FilenameFromUrl(const char *pUrl, char *pBuf, int BufSize)
{
	if(BufSize <= 0)
		return;
	if(!pUrl || pUrl[0] == '\0')
	{
		pBuf[0] = '\0';
		return;
	}
	const char *pStart = pUrl;
	for(const char *p = pUrl; *p != '\0'; ++p)
		if(*p == '/' || *p == '\\')
			pStart = p + 1;
	str_copy(pBuf, pStart, BufSize);
}

static const char *GetReleaseVersionString(const json_value *pJson)
{
	if(!pJson || pJson->type != json_object)
		return nullptr;

	const char *pVersion = json_string_get(json_object_get(pJson, "tag_name"));
	if(!pVersion)
		pVersion = json_string_get(json_object_get(pJson, "name"));
	return pVersion;
}

static void NormalizeVersionString(const char *pVersion, char *pBuf, int BufSize)
{
	if(BufSize <= 0)
		return;

	if(!pVersion)
	{
		pBuf[0] = '\0';
		return;
	}

	while(*pVersion != '\0' && std::isspace(static_cast<unsigned char>(*pVersion)))
		++pVersion;

	if((pVersion[0] == 'v' || pVersion[0] == 'V') && std::isdigit(static_cast<unsigned char>(pVersion[1])))
		++pVersion;

	str_copy(pBuf, pVersion, BufSize);
}

static std::vector<int> ExtractVersionNumbers(const char *pVersion)
{
	std::vector<int> vNumbers;
	if(!pVersion)
		return vNumbers;

	int Current = -1;
	for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pVersion); *p != '\0'; ++p)
	{
		if(std::isdigit(*p))
		{
			if(Current < 0)
				Current = 0;
			Current = Current * 10 + (*p - '0');
		}
		else if(Current >= 0)
		{
			vNumbers.push_back(Current);
			Current = -1;
		}
	}

	if(Current >= 0)
		vNumbers.push_back(Current);

	return vNumbers;
}

static int CompareVersionStrings(const char *pLeft, const char *pRight)
{
	char aLeftNormalized[64];
	char aRightNormalized[64];
	NormalizeVersionString(pLeft, aLeftNormalized, sizeof(aLeftNormalized));
	NormalizeVersionString(pRight, aRightNormalized, sizeof(aRightNormalized));

	const std::vector<int> vLeft = ExtractVersionNumbers(aLeftNormalized);
	const std::vector<int> vRight = ExtractVersionNumbers(aRightNormalized);
	const size_t Num = maximum(vLeft.size(), vRight.size());
	for(size_t i = 0; i < Num; ++i)
	{
		const int Left = i < vLeft.size() ? vLeft[i] : 0;
		const int Right = i < vRight.size() ? vRight[i] : 0;
		if(Left < Right)
			return -1;
		if(Left > Right)
			return 1;
	}

	return str_comp_nocase(aLeftNormalized, aRightNormalized);
}

static int ScoreArchiveAsset(const char *pAssetName)
{
	if(!pAssetName)
		return -1;

	const std::string Lower = ToLowerAscii(pAssetName);
	if(Lower.find("bestclient") == std::string::npos)
		return -1;

#if defined(CONF_FAMILY_WINDOWS)
	if(!StrEndsWithNoCase(pAssetName, ".zip"))
		return -1;
	if(Lower.find("windows") == std::string::npos && Lower.find("win") == std::string::npos)
		return -1;
#elif defined(CONF_PLATFORM_ANDROID)
	if(!StrEndsWithNoCase(pAssetName, ".apk"))
		return -1;
	if(Lower.find("android") == std::string::npos)
		return -1;
#elif defined(CONF_PLATFORM_LINUX)
	if(!StrEndsWithNoCase(pAssetName, ".tar.xz"))
		return -1;
	if(Lower.find("linux") == std::string::npos)
		return -1;
#else
	return -1;
#endif

	if(Lower.find("debug") != std::string::npos || Lower.find("symbols") != std::string::npos || Lower.find("source") != std::string::npos)
		return -1;

	int Score = 100;

#if defined(CONF_FAMILY_WINDOWS)
	if(Lower == "bestclient-windows.zip")
		Score += 200;
	if(Lower.find("x64") != std::string::npos || Lower.find("64") != std::string::npos || Lower.find("amd64") != std::string::npos)
		Score += 20;
#elif defined(CONF_PLATFORM_ANDROID)
	if(Lower == "bestclient-android.apk")
		Score += 200;
#elif defined(CONF_PLATFORM_LINUX)
	if(Lower == "bestclient-linux.tar.xz")
		Score += 200;
#endif

#if defined(CONF_ARCH_AMD64)
	if(Lower.find("x64") != std::string::npos || Lower.find("64") != std::string::npos || Lower.find("amd64") != std::string::npos)
		Score += 10;
#elif defined(CONF_ARCH_IA32)
	if(Lower.find("x86") != std::string::npos || Lower.find("32") != std::string::npos)
		Score += 10;
#endif

	return Score;
}

static bool ParseReleaseObject(const json_value *pJson, char *pVersion, int VersionSize, char *pArchiveName, int ArchiveNameSize, char *pArchiveUrl, int ArchiveUrlSize)
{
	if(!pJson || pJson->type != json_object)
		return false;

	const char *pReleaseVersion = json_string_get(json_object_get(pJson, "tag_name"));
	if(!pReleaseVersion)
		pReleaseVersion = json_string_get(json_object_get(pJson, "name"));
	if(!pReleaseVersion)
		return false;

	const json_value *pAssets = json_object_get(pJson, "assets");
	if(!pAssets || pAssets->type != json_array)
		return false;

	int BestScore = -1;
	char aBestName[128] = "";
	char aBestUrl[2048] = "";

	for(int i = 0; i < json_array_length(pAssets); ++i)
	{
		const json_value *pAsset = json_array_get(pAssets, i);
		if(!pAsset || pAsset->type != json_object)
			continue;

		const char *pName = json_string_get(json_object_get(pAsset, "name"));
		const char *pUrl = json_string_get(json_object_get(pAsset, "browser_download_url"));
		const int Score = ScoreArchiveAsset(pName);
		if(!pName || !pUrl || Score < BestScore)
			continue;

		BestScore = Score;
		str_copy(aBestName, pName, sizeof(aBestName));
		str_copy(aBestUrl, pUrl, sizeof(aBestUrl));
	}

	if(BestScore < 0)
		return false;

	str_copy(pVersion, pReleaseVersion, VersionSize);
	str_copy(pArchiveName, aBestName, ArchiveNameSize);
	str_copy(pArchiveUrl, aBestUrl, ArchiveUrlSize);
	return true;
}

static bool ParseLatestPlatformObject(const json_value *pJson, char *pVersion, int VersionSize, char *pArchiveName, int ArchiveNameSize, char *pArchiveUrl, int ArchiveUrlSize, int64_t *pExpectedSize)
{
	if(!pJson || pJson->type != json_object)
		return false;

	const char *pVersionValue = JsonStringField(pJson, "version");
	if(!pVersionValue)
		pVersionValue = JsonStringField(pJson, "tag_name");
	if(!pVersionValue)
		pVersionValue = JsonStringField(pJson, "name");
	if(!pVersionValue)
		return false;

	const json_value *pPlatforms = json_object_get(pJson, "platforms");
	if(!pPlatforms || pPlatforms->type != json_object)
		return false;

	const json_value *pPlatform = json_object_get(pPlatforms, CurrentPlatformKey());
	if(!pPlatform || pPlatform->type != json_object)
		return false;

	const char *pUrl = JsonStringField(pPlatform, "url");
	if(!pUrl)
		pUrl = JsonStringField(pPlatform, "download_url");
	if(!pUrl)
		pUrl = JsonStringField(pPlatform, "browser_download_url");
	if(!pUrl)
		return false;

	const char *pName = JsonStringField(pPlatform, "name");
	char aDerivedName[128];
	if(!pName)
	{
		FilenameFromUrl(pUrl, aDerivedName, sizeof(aDerivedName));
		pName = aDerivedName;
	}

	str_copy(pVersion, pVersionValue, VersionSize);
	str_copy(pArchiveName, pName, ArchiveNameSize);
	str_copy(pArchiveUrl, pUrl, ArchiveUrlSize);
	if(pExpectedSize)
	{
		*pExpectedSize = 0;
		const json_value *pSize = json_object_get(pPlatform, "size");
		if(pSize && pSize->type == json_integer)
			*pExpectedSize = pSize->u.integer;
	}
	return true;
}

static bool ParseLatestRelease(json_value *pJson, char *pVersion, int VersionSize, char *pArchiveName, int ArchiveNameSize, char *pArchiveUrl, int ArchiveUrlSize)
{
	if(!pJson)
		return false;

	if(pJson->type == json_object)
		return ParseReleaseObject(pJson, pVersion, VersionSize, pArchiveName, ArchiveNameSize, pArchiveUrl, ArchiveUrlSize);

	if(pJson->type == json_array)
	{
		const json_value *pBestRelease = nullptr;
		char aBestVersion[64] = "";
		for(int i = 0; i < json_array_length(pJson); ++i)
		{
			const json_value *pRelease = json_array_get(pJson, i);
			const char *pReleaseVersion = GetReleaseVersionString(pRelease);
			if(!pReleaseVersion)
				continue;

			if(!pBestRelease || CompareVersionStrings(pReleaseVersion, aBestVersion) > 0)
			{
				pBestRelease = pRelease;
				str_copy(aBestVersion, pReleaseVersion, sizeof(aBestVersion));
			}
		}

		if(pBestRelease)
			return ParseReleaseObject(pBestRelease, pVersion, VersionSize, pArchiveName, ArchiveNameSize, pArchiveUrl, ArchiveUrlSize);
	}

	return false;
}

static void StripFilename(char *pPath)
{
	if(!pPath)
		return;

	for(int i = str_length(pPath) - 1; i >= 0; --i)
	{
		if(pPath[i] == '/' || pPath[i] == '\\')
		{
			pPath[i] = '\0';
			return;
		}
	}
	pPath[0] = '\0';
}

CUpdater::CUpdater()
{
	m_pClient = nullptr;
	m_pStorage = nullptr;
	m_pHttp = nullptr;

	m_State = CLEAN;
	m_aStatus[0] = '\0';
	m_Percent = 0;
	m_aLatestVersion[0] = '\0';
	m_aArchiveName[0] = '\0';
	m_aArchiveUrl[0] = '\0';
	str_copy(m_aArchivePath, UPDATE_ARCHIVE_PATH, sizeof(m_aArchivePath));
	m_ExpectedArchiveSize = 0;
}

void CUpdater::Init(CHttp *pHttp)
{
	m_pClient = Kernel()->RequestInterface<IClient>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pHttp = pHttp;

#if !defined(CONF_HEADLESS_CLIENT) && (defined(CONF_FAMILY_WINDOWS) || defined(CONF_PLATFORM_LINUX) || defined(CONF_PLATFORM_ANDROID))
#if defined(CONF_FAMILY_WINDOWS)
	// Pre-game launcher owns update checks when present — avoid download loops.
	char aLauncher[IO_MAX_PATH_LENGTH];
	if(!UClientLaunchGate_FindLauncherPath(aLauncher, sizeof(aLauncher)))
#endif
		m_bAutoCheckPending = true;
#endif
}

void CUpdater::SetCurrentState(EUpdaterState NewState)
{
	const CLockScope LockScope(m_Lock);
	m_State = NewState;
}

void CUpdater::SetStatus(const char *pStatus)
{
	const CLockScope LockScope(m_Lock);
	str_copy(m_aStatus, pStatus ? pStatus : "", sizeof(m_aStatus));
}

void CUpdater::SetPercent(int Percent)
{
	const CLockScope LockScope(m_Lock);
	m_Percent = std::clamp(Percent, 0, 100);
}

IUpdater::EUpdaterState CUpdater::GetCurrentState()
{
	const CLockScope LockScope(m_Lock);
	return m_State;
}

void CUpdater::GetCurrentFile(char *pBuf, int BufSize)
{
	const CLockScope LockScope(m_Lock);
	str_copy(pBuf, m_aStatus, BufSize);
}

int CUpdater::GetCurrentPercent()
{
	const CLockScope LockScope(m_Lock);
	return m_Percent;
}

const char *CUpdater::GetLatestVersionString()
{
	return m_aLatestVersion;
}

bool CUpdater::HasCompletedCheck()
{
	return m_CheckCompleted;
}

void CUpdater::ResetTask()
{
	if(m_pCurrentTask)
	{
		m_pCurrentTask->Abort();
		m_pCurrentTask = nullptr;
	}
	m_TaskKind = ETaskKind::NONE;
}

void CUpdater::StartReleaseFetch()
{
	ResetTask();
	m_ExpectedArchiveSize = 0;
	SetStatus("Checking latest release");
	SetPercent(0);
	SetCurrentState(IUpdater::GETTING_MANIFEST);

	char aUrl[2304];
	BuildUpdateLatestUrl(aUrl, sizeof(aUrl));
	m_TaskKind = ETaskKind::FETCH_RELEASE;
	m_pCurrentTask = HttpGet(aUrl);
	m_pCurrentTask->HeaderString("Accept", "application/json");
	m_pCurrentTask->HeaderString("User-Agent", CLIENT_NAME);
	m_pCurrentTask->HeaderString("Cache-Control", "no-cache");
	m_pCurrentTask->HeaderString("Pragma", "no-cache");
	m_pCurrentTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pCurrentTask->IpResolve(IPRESOLVE::V4);
	m_pHttp->Run(m_pCurrentTask);
}

void CUpdater::ParseReleaseTask()
{
	m_CheckCompleted = true;

	json_value *pJson = m_pCurrentTask ? m_pCurrentTask->ResultJson() : nullptr;
	if(!pJson)
	{
		SetStatus("Failed to parse release info");
		SetCurrentState(IUpdater::FAIL);
		return;
	}

	char aVersion[64] = "";
	char aArchiveName[128] = "";
	char aArchiveUrl[2048] = "";
	int64_t ExpectedArchiveSize = 0;

	const bool Parsed = ParseLatestPlatformObject(pJson, aVersion, sizeof(aVersion), aArchiveName, sizeof(aArchiveName), aArchiveUrl, sizeof(aArchiveUrl), &ExpectedArchiveSize) ||
		ParseLatestRelease(pJson, aVersion, sizeof(aVersion), aArchiveName, sizeof(aArchiveName), aArchiveUrl, sizeof(aArchiveUrl));
	json_value_free(pJson);

	// Update is available only when the remote version is higher than the current one.
	if(!Parsed || CompareVersionStrings(aVersion, UCLIENT_VERSION) <= 0)
	{
		m_aLatestVersion[0] = '\0';
		m_aArchiveName[0] = '\0';
		m_aArchiveUrl[0] = '\0';
		m_ExpectedArchiveSize = 0;
		SetStatus("No update available");
		SetCurrentState(IUpdater::CLEAN);
		return;
	}

	str_copy(m_aLatestVersion, aVersion, sizeof(m_aLatestVersion));
	str_copy(m_aArchiveName, aArchiveName, sizeof(m_aArchiveName));
	str_copy(m_aArchiveUrl, aArchiveUrl, sizeof(m_aArchiveUrl));
	m_ExpectedArchiveSize = ExpectedArchiveSize;
	SetStatus("Update available");
	SetCurrentState(IUpdater::VERSION_AVAILABLE);
}

void CUpdater::StartArchiveDownload()
{
	ResetTask();
	str_copy(m_aArchivePath, UPDATE_ARCHIVE_PATH, sizeof(m_aArchivePath));
	m_pStorage->RemoveBinaryFile(m_aArchivePath);

	SetStatus(m_aArchiveName);
	SetPercent(0);
	SetCurrentState(IUpdater::DOWNLOADING);

	m_TaskKind = ETaskKind::DOWNLOAD_ARCHIVE;
	m_pCurrentTask = HttpGetFile(m_aArchiveUrl, m_pStorage, m_aArchivePath, IStorage::TYPE_ABSOLUTE);
	m_pCurrentTask->HeaderString("User-Agent", CLIENT_NAME);
	m_pCurrentTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pCurrentTask->IpResolve(IPRESOLVE::V4);
	m_pHttp->Run(m_pCurrentTask);
}

bool CUpdater::ValidateDownloadedArchive()
{
	char aArchivePath[IO_MAX_PATH_LENGTH];
	m_pStorage->GetBinaryPath(m_aArchivePath, aArchivePath, sizeof(aArchivePath));
	if(!m_pStorage->FileExists(aArchivePath, IStorage::TYPE_ABSOLUTE))
	{
		SetStatus("Downloaded archive missing");
		return false;
	}

	IOHANDLE File = io_open(aArchivePath, IOFLAG_READ);
	if(!File)
	{
		SetStatus("Downloaded archive unreadable");
		return false;
	}

	const int64_t FileSize = io_length(File);
	unsigned char aMagic[4] = {0, 0, 0, 0};
	const unsigned MagicLength = io_read(File, aMagic, sizeof(aMagic));
	io_close(File);

	if(FileSize < 1024 * 1024)
	{
		SetStatus("Downloaded file is too small");
		return false;
	}

	if(m_ExpectedArchiveSize > 0)
	{
		const int64_t MinSize = m_ExpectedArchiveSize * 95 / 100;
		if(FileSize < MinSize)
		{
			SetStatus("Downloaded file size mismatch");
			return false;
		}
	}

#if defined(CONF_FAMILY_WINDOWS)
	if(MagicLength < 2 || aMagic[0] != 'P' || aMagic[1] != 'K')
	{
		SetStatus("Downloaded file is not a zip archive");
		return false;
	}
#endif

	return true;
}

bool CUpdater::WriteApplyScript(char *pScriptPath, int ScriptPathSize, char *pInstallDir, int InstallDirSize, char *pExePath, int ExePathSize)
{
	m_pStorage->GetBinaryPath(UPDATE_SCRIPT_PATH, pScriptPath, ScriptPathSize);
	m_pStorage->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, pExePath, ExePathSize);
	str_copy(pInstallDir, pExePath, InstallDirSize);
	StripFilename(pInstallDir);

	if(fs_makedir_rec_for(pScriptPath) < 0)
		return false;

	static constexpr const char *pScript =
		"param(\n"
		"    [int]$PidToWait,\n"
		"    [string]$ArchivePath,\n"
		"    [string]$InstallDir,\n"
		"    [string]$ExePath\n"
		")\n"
		"$ErrorActionPreference = 'Stop'\n"
		"try {\n"
		"    $updateDir = Join-Path $InstallDir 'update'\n"
		"    New-Item -ItemType Directory -Path $updateDir -Force | Out-Null\n"
		"    $logPath = Join-Path $updateDir 'apply_update.log'\n"
		"    Add-Content -LiteralPath $logPath -Value ('[' + (Get-Date).ToString('s') + '] updater started')\n"
		"    while(Get-Process -Id $PidToWait -ErrorAction SilentlyContinue) {\n"
		"        Start-Sleep -Milliseconds 200\n"
		"    }\n"
		"    Add-Content -LiteralPath $logPath -Value ('[' + (Get-Date).ToString('s') + '] client process stopped')\n"
		"    $extractDir = Join-Path $InstallDir 'update\\extract'\n"
		"    if(Test-Path -LiteralPath $extractDir) {\n"
		"        Remove-Item -LiteralPath $extractDir -Recurse -Force\n"
		"    }\n"
		"    New-Item -ItemType Directory -Path $extractDir -Force | Out-Null\n"
		"    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $extractDir -Force\n"
		"    Add-Content -LiteralPath $logPath -Value ('[' + (Get-Date).ToString('s') + '] archive extracted')\n"
		"    $copyRoot = $extractDir\n"
		"    $items = @(Get-ChildItem -LiteralPath $extractDir -Force)\n"
		"    if($items.Count -eq 1 -and $items[0].PSIsContainer) {\n"
		"        $copyRoot = $items[0].FullName\n"
		"    }\n"
		"    foreach($item in Get-ChildItem -LiteralPath $copyRoot -Force) {\n"
		"        Copy-Item -Path $item.FullName -Destination $InstallDir -Recurse -Force\n"
		"    }\n"
		"    Add-Content -LiteralPath $logPath -Value ('[' + (Get-Date).ToString('s') + '] files copied')\n"
		"    Remove-Item -LiteralPath $ArchivePath -Force -ErrorAction SilentlyContinue\n"
		"    Remove-Item -LiteralPath $extractDir -Recurse -Force -ErrorAction SilentlyContinue\n"
		"    Start-Process -FilePath $ExePath -WorkingDirectory $InstallDir\n"
		"    Add-Content -LiteralPath $logPath -Value ('[' + (Get-Date).ToString('s') + '] client relaunched')\n"
		"} catch {\n"
		"    $logPath = Join-Path $InstallDir 'update\\apply_update_error.txt'\n"
		"    $_ | Out-String | Set-Content -LiteralPath $logPath -Encoding UTF8\n"
		"}\n";

	IOHANDLE File = io_open(pScriptPath, IOFLAG_WRITE);
	if(!File)
		return false;

	io_write(File, pScript, str_length(pScript));
	io_close(File);
	return true;
}

#if defined(CONF_FAMILY_WINDOWS)
static bool ResolveUpdatePaths(IStorage *pStorage, const char *pArchiveStoragePath, char *pArchivePath, int ArchivePathSize, char *pInstallDir, int InstallDirSize, char *pExePath, int ExePathSize)
{
	pStorage->GetBinaryPath(pArchiveStoragePath, pArchivePath, ArchivePathSize);
	if(!pStorage->FileExists(pArchivePath, IStorage::TYPE_ABSOLUTE))
		return false;

	// Prefer launcher so post-update entry always goes through the pre-game gate.
	pStorage->GetBinaryPathAbsolute("UClient.exe", pExePath, ExePathSize);
	if(!pStorage->FileExists(pExePath, IStorage::TYPE_ABSOLUTE))
		pStorage->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, pExePath, ExePathSize);
	str_copy(pInstallDir, pExePath, InstallDirSize);
	StripFilename(pInstallDir);
	return true;
}

static bool LaunchLauncherApply(IStorage *pStorage, const char *pPid, const char *pArchivePath)
{
	char aLauncherPath[IO_MAX_PATH_LENGTH];
	pStorage->GetBinaryPathAbsolute(UCLIENT_LAUNCHER_EXEC_WIN, aLauncherPath, sizeof(aLauncherPath));
	if(!pStorage->FileExists(aLauncherPath, IStorage::TYPE_ABSOLUTE))
		return false;

	const char *apArguments[] = {
		UCLIENT_APPLY_UPDATE_ARG,
		pArchivePath,
		UCLIENT_WAIT_PID_ARG,
		pPid,
	};

	return process_execute(aLauncherPath, EShellExecuteWindowState::FOREGROUND, apArguments, std::size(apArguments)) != INVALID_PROCESS;
}

static bool WritePendingVersionFile(IStorage * /*pStorage*/, const char *pInstallDir, const char *pVersion)
{
	if(!pVersion || !pVersion[0] || !pInstallDir || !pInstallDir[0])
		return false;
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s", pInstallDir, UCLIENT_PENDING_VERSION_FILE);
	IOHANDLE File = io_open(aPath, IOFLAG_WRITE);
	if(!File)
		return false;
	io_write(File, pVersion, str_length(pVersion));
	io_write(File, "\n", 1);
	io_close(File);
	return true;
}
#endif

bool CUpdater::LaunchApplyScriptAndQuit()
{
#if defined(CONF_FAMILY_WINDOWS)
	char aArchivePath[IO_MAX_PATH_LENGTH];
	char aScriptPath[IO_MAX_PATH_LENGTH];
	char aInstallDir[IO_MAX_PATH_LENGTH];
	char aExePath[IO_MAX_PATH_LENGTH];
	char aPid[32];

	if(!ResolveUpdatePaths(m_pStorage, m_aArchivePath, aArchivePath, sizeof(aArchivePath), aInstallDir, sizeof(aInstallDir), aExePath, sizeof(aExePath)))
	{
		SetStatus("Downloaded archive missing");
		return false;
	}

	str_format(aPid, sizeof(aPid), "%d", process_id());
	WritePendingVersionFile(m_pStorage, aInstallDir, m_aLatestVersion);
	if(LaunchLauncherApply(m_pStorage, aPid, aArchivePath))
	{
		m_pClient->Quit();
		return true;
	}

	if(!WriteApplyScript(aScriptPath, sizeof(aScriptPath), aInstallDir, sizeof(aInstallDir), aExePath, sizeof(aExePath)))
	{
		SetStatus("Failed to prepare updater script");
		return false;
	}

	const char *apArguments[] = {
		"-NoProfile",
		"-NonInteractive",
		"-ExecutionPolicy",
		"Bypass",
		"-File",
		aScriptPath,
		"-PidToWait",
		aPid,
		"-ArchivePath",
		aArchivePath,
		"-InstallDir",
		aInstallDir,
		"-ExePath",
		aExePath,
	};

	if(process_execute("powershell.exe", EShellExecuteWindowState::BACKGROUND, apArguments, std::size(apArguments)) == INVALID_PROCESS)
	{
		SetStatus("Failed to launch updater");
		return false;
	}

	m_pClient->Quit();
	return true;
#elif defined(CONF_PLATFORM_ANDROID)
	char aArchivePath[IO_MAX_PATH_LENGTH];

	m_pStorage->GetBinaryPath(m_aArchivePath, aArchivePath, sizeof(aArchivePath));
	if(!m_pStorage->FileExists(aArchivePath, IStorage::TYPE_ABSOLUTE))
	{
		SetStatus("Downloaded archive missing");
		return false;
	}

	char aAbsoluteArchivePath[IO_MAX_PATH_LENGTH];
	m_pStorage->GetBinaryPathAbsolute(m_aArchivePath, aAbsoluteArchivePath, sizeof(aAbsoluteArchivePath));

	if(!InstallAndroidApk(aAbsoluteArchivePath))
	{
		SetStatus("Failed to launch installer");
		return false;
	}

	// The OS replaces the running process once the user confirms the install,
	// so the client must keep running instead of quitting like on Windows/Linux.
	return true;
#elif defined(CONF_PLATFORM_LINUX)
	char aArchivePath[IO_MAX_PATH_LENGTH];
	char aUpdaterPath[IO_MAX_PATH_LENGTH];
	char aInstallDir[IO_MAX_PATH_LENGTH];
	char aExePath[IO_MAX_PATH_LENGTH];
	char aPid[32];

	m_pStorage->GetBinaryPath(m_aArchivePath, aArchivePath, sizeof(aArchivePath));
	if(!m_pStorage->FileExists(aArchivePath, IStorage::TYPE_ABSOLUTE))
	{
		SetStatus("Downloaded archive missing");
		return false;
	}

	m_pStorage->GetBinaryPathAbsolute("bestclient-updater", aUpdaterPath, sizeof(aUpdaterPath));
	m_pStorage->GetBinaryPathAbsolute("UClient", aExePath, sizeof(aExePath));
	if(!m_pStorage->FileExists(aExePath, IStorage::TYPE_ABSOLUTE))
		m_pStorage->GetBinaryPathAbsolute(PLAT_CLIENT_EXEC, aExePath, sizeof(aExePath));
	str_copy(aInstallDir, aExePath, sizeof(aInstallDir));
	StripFilename(aInstallDir);

	str_format(aPid, sizeof(aPid), "%d", process_id());
	const char *apArguments[] = {aPid, aArchivePath, aInstallDir, aExePath};

	if(process_execute(aUpdaterPath, EShellExecuteWindowState::FOREGROUND, apArguments, std::size(apArguments)) == INVALID_PROCESS)
	{
		SetStatus("Failed to launch updater");
		return false;
	}

	m_pClient->Quit();
	return true;
#else
	SetStatus("Archive updater is only available on Windows and Linux");
	return false;
#endif
}

void CUpdater::CheckForUpdate()
{
	const EUpdaterState State = GetCurrentState();
	if(State == IUpdater::GETTING_MANIFEST || State == IUpdater::DOWNLOADING)
		return;

#if !defined(CONF_FAMILY_WINDOWS) && !defined(CONF_PLATFORM_LINUX) && !defined(CONF_PLATFORM_ANDROID)
	if(m_pClient)
		m_pClient->ViewLink(GITHUB_LATEST_RELEASE_URL);
	return;
#endif

	m_aLatestVersion[0] = '\0';
	m_aArchiveName[0] = '\0';
	m_aArchiveUrl[0] = '\0';
	StartReleaseFetch();
}

void CUpdater::InitiateUpdate()
{
	const EUpdaterState State = GetCurrentState();
	if(State == IUpdater::GETTING_MANIFEST || State == IUpdater::DOWNLOADING)
		return;

	if((State == IUpdater::VERSION_AVAILABLE || State == IUpdater::FAIL) && m_aArchiveUrl[0] != '\0')
	{
		StartArchiveDownload();
		return;
	}

	CheckForUpdate();
}

void CUpdater::ApplyUpdateAndRestart()
{
	if(GetCurrentState() != IUpdater::NEED_RESTART)
		return;

	if(!LaunchApplyScriptAndQuit())
		SetCurrentState(IUpdater::FAIL);
}

void CUpdater::Update()
{
	if(g_Config.m_BcAutoUpdate != 0)
	{
		const EUpdaterState State = GetCurrentState();
		if(State == IUpdater::VERSION_AVAILABLE)
		{
#if defined(CONF_FAMILY_WINDOWS)
			char aLauncher[IO_MAX_PATH_LENGTH];
			if(UClientLaunchGate_FindLauncherPath(aLauncher, sizeof(aLauncher)))
			{
				process_execute(aLauncher, EShellExecuteWindowState::FOREGROUND);
				m_pClient->Quit();
				return;
			}
#endif
			InitiateUpdate();
		}
		else if(State == IUpdater::NEED_RESTART)
			ApplyUpdateAndRestart();
	}

	if(m_bAutoCheckPending && m_pHttp && GetCurrentState() == CLEAN)
	{
		m_bAutoCheckPending = false;
		CheckForUpdate();
	}

	if(!m_pCurrentTask)
		return;

	if(!m_pCurrentTask->Done())
	{
		if(GetCurrentState() == IUpdater::DOWNLOADING)
			SetPercent(m_pCurrentTask->Progress());
		return;
	}

	if(m_pCurrentTask->State() != EHttpState::DONE || m_pCurrentTask->StatusCode() >= 400)
	{
		// A failed check still counts as completed: waiting forever would keep callers that gate on
		// the check result (the mandatory update) hanging whenever the update host is unreachable.
		if(m_TaskKind == ETaskKind::FETCH_RELEASE)
			m_CheckCompleted = true;
		ResetTask();
		SetStatus("Update check failed");
		SetCurrentState(IUpdater::FAIL);
		return;
	}

	if(m_TaskKind == ETaskKind::FETCH_RELEASE)
	{
		ParseReleaseTask();
		ResetTask();
		return;
	}

	if(m_TaskKind == ETaskKind::DOWNLOAD_ARCHIVE)
	{
		ResetTask();
		if(!ValidateDownloadedArchive())
		{
			m_pStorage->RemoveBinaryFile(m_aArchivePath);
			SetCurrentState(IUpdater::FAIL);
			return;
		}
		SetPercent(100);
		SetStatus(m_aArchiveName[0] != '\0' ? m_aArchiveName : "update");
		SetCurrentState(IUpdater::NEED_RESTART);
	}
}
