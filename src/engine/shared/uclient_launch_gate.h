#ifndef ENGINE_SHARED_UCLIENT_LAUNCH_GATE_H
#define ENGINE_SHARED_UCLIENT_LAUNCH_GATE_H

/**
 * Pre-game launcher gate: DDNet.exe may only continue when started by UClient.exe
 * with a one-time token (--uclient-from-launcher <token>).
 *
 * Token file lives next to the client binary as uclient_launch.token.
 */

#if defined(CONF_FAMILY_WINDOWS)
#define UCLIENT_LAUNCHER_EXEC "UClient.exe"
#define UCLIENT_GAME_EXEC "DDNet.exe"
#else
#define UCLIENT_LAUNCHER_EXEC "UClient"
#define UCLIENT_GAME_EXEC "DDNet"
#endif

#define UCLIENT_LAUNCH_TOKEN_ARG "--uclient-from-launcher"
#define UCLIENT_LAUNCH_TOKEN_FILE "uclient_launch.token"
#define UCLIENT_LAUNCH_TOKEN_TTL_SECONDS 60
#define UCLIENT_LAUNCH_ALLOW_ENV "UCLIENT_ALLOW_DIRECT_LAUNCH"

/**
 * Validates or redirects launch on Windows client entry.
 *
 * On success (allowed to continue): strips --uclient-from-launcher <token> from argv
 * and returns true.
 * On failure: attempts to spawn UClient.exe with remaining args and returns false
 * (caller must exit immediately).
 *
 * Non-Windows: always returns true.
 */
bool UClientLaunchGate_EnsureFromLauncher(int *pArgc, const char ***ppArgv);

/**
 * Writes a one-time launch token file next to the given install directory.
 * Returns true and fills pTokenOut on success.
 */
bool UClientLaunchGate_WriteToken(const char *pInstallDir, char *pTokenOut, int TokenOutSize);

/**
 * Absolute path to UClient.exe next to the running binary (or empty on failure).
 */
bool UClientLaunchGate_FindLauncherPath(char *pBuf, int BufSize);

/**
 * Absolute path to DDNet.exe next to the running binary (or empty on failure).
 */
bool UClientLaunchGate_FindGamePath(char *pBuf, int BufSize);

/**
 * Install directory containing the running executable.
 */
bool UClientLaunchGate_FindInstallDir(char *pBuf, int BufSize);

#endif
