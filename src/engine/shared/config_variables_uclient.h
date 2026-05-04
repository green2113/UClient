// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) ;
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Flags, Desc) ;
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc) ;
#endif

MACRO_CONFIG_STR(UcChatGiphyApiKey, uc_chat_giphy_api_key, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Giphy API key used for chat GIF search")
MACRO_CONFIG_INT(UcChatPasteUpload, uc_chat_paste_upload, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Allow uploading clipboard images from chat input")
MACRO_CONFIG_STR(UcChatPasteUploadUrl, uc_chat_paste_upload_url, 256, "https://ddnet.under1111.com/api/media/upload", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Upload endpoint used for pasted chat images")
MACRO_CONFIG_INT(UcChatPasteUploadMaxBytes, uc_chat_paste_upload_max_bytes, 10485760, 0, 67108864, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum PNG size in bytes for pasted chat image uploads")
MACRO_CONFIG_INT(UcChatPasteImageWarningSkip, uc_chat_paste_image_warning_skip, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skip the clipboard image privacy warning before attaching an image to chat")
MACRO_CONFIG_STR(UcInstallUuid, uc_install_uuid, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "BestClient install UUID generated once on first start")
MACRO_CONFIG_STR(UcPresenceApiBaseUrl, uc_presence_api_base_url, 256, "https://ddnet.under1111.com/api/presence", CFGFLAG_CLIENT | CFGFLAG_SAVE, "HTTP presence API base URL used for join/heartbeat/leave/switch updates")
MACRO_CONFIG_INT(UcPresenceHttpHeartbeatSeconds, uc_presence_http_heartbeat_seconds, 60, 60, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Presence HTTP heartbeat interval in seconds (used for /heartbeat API writes)")
MACRO_CONFIG_INT(UcChatPlayerSearchEngine, uc_chat_player_search_engine, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Search engine used when clicking player names in chat (0=DDNet, 1=DDStats)")
#if defined(CONF_DEBUG) || defined(CONF_UCLIENT_DEV_BUILD)
MACRO_CONFIG_INT(UcVoiceTeam, uc_voice_team, -1, -1, 63, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice target team override (-1=off, 0..63=force that team)")
MACRO_CONFIG_INT(UcVoiceId, uc_voice_id, -1, -1, 63, CFGFLAG_CLIENT | CFGFLAG_SAVE, ".")
#else
MACRO_CONFIG_INT(UcVoiceTeam, uc_voice_team, -1, -1, 63, 0, "Voice target team override (-1=off, 0..63=force that team)")
MACRO_CONFIG_INT(UcVoiceId, uc_voice_id, -1, -1, 63, 0, ".")
#endif
MACRO_CONFIG_STR(UcSoundboardApiBaseUrl, uc_soundboard_api_base_url, 256, "https://ddnet.under1111.com/api/soundboard", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Base URL for soundboard API")
MACRO_CONFIG_STR(UcSoundboardUploadUrl, uc_soundboard_upload_url, 256, "https://ddnet.under1111.com/api/soundboard/upload", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Upload endpoint used for personal soundboard sounds")
MACRO_CONFIG_INT(UcSoundboardMaxUploadBytes, uc_soundboard_max_upload_bytes, 5242880, 0, 33554432, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum upload size for personal soundboard files")
MACRO_CONFIG_INT(UcSoundboardMaxUploadSeconds, uc_soundboard_max_upload_seconds, 10, 1, 120, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum upload length in seconds for personal soundboard files")
MACRO_CONFIG_INT(UcVoiceTeamIncludeOwn, uc_voice_team_include_own, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "When uc_voice_team is active, also include your current team audio (0/1)")
