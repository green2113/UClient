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
MACRO_CONFIG_INT(UcChatPlayerSearchEngine, uc_chat_player_search_engine, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Search engine used when clicking player names in chat (0=DDNet, 1=DDStats)")
