#ifndef ENGINE_SHARED_UCLIENT_AI_SETTINGS_H
#define ENGINE_SHARED_UCLIENT_AI_SETTINGS_H

class IConfigManager;
class IStorage;

#define UCLIENT_AI_SETTINGS_REQUEST_FILE "uclient_ai_settings_request.json"
#define UCLIENT_AI_SETTINGS_LIVE_FILE "uclient_ai_settings_live.json"

void UClientAi_PollLiveSettingsDump(IConfigManager *pConfig, IStorage *pStorage, const char *pBindsJson = nullptr);

#endif
