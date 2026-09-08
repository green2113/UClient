#include <game/version.h>

#include <cstdio>

#if defined(_WIN32)
#define UCLIENT_BUILD_PLATFORM "windows"
#elif defined(__APPLE__)
#define UCLIENT_BUILD_PLATFORM "macos"
#elif defined(__linux__)
#define UCLIENT_BUILD_PLATFORM "linux"
#else
#define UCLIENT_BUILD_PLATFORM "unknown"
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
#define UCLIENT_BUILD_ARCH "arm64"
#elif defined(_M_X64) || defined(__x86_64__)
#define UCLIENT_BUILD_ARCH "x86_64"
#elif defined(_M_IX86) || defined(__i386__)
#define UCLIENT_BUILD_ARCH "x86"
#elif defined(__arm__)
#define UCLIENT_BUILD_ARCH "arm"
#else
#define UCLIENT_BUILD_ARCH "unknown"
#endif

int main()
{
	std::printf(
		"{\"clientVersion\":\"%s\",\"launcherVersion\":\"%s\","
		"\"platform\":\"%s\",\"architecture\":\"%s\"}\n",
		UCLIENT_VERSION,
		UCLIENT_LAUNCHER_VERSION,
		UCLIENT_BUILD_PLATFORM,
		UCLIENT_BUILD_ARCH);
	return 0;
}
