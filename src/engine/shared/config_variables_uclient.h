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
MACRO_CONFIG_COL(UcChatImagePenColor, uc_chat_image_pen_color, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Pen color used in the chat image editor")
MACRO_CONFIG_STR(UcInstallUuid, uc_install_uuid, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "UClient install UUID generated once on first start")
MACRO_CONFIG_STR(UcUpdateLatestUrl, uc_update_latest_url, 256, "https://ddnet.under1111.com/api/uclient/update/latest", CFGFLAG_CLIENT | CFGFLAG_SAVE, "HTTP endpoint used to fetch latest UClient update metadata")
MACRO_CONFIG_STR(UcPresenceApiBaseUrl, uc_presence_api_base_url, 256, "https://ddnet.under1111.com/api/presence", CFGFLAG_CLIENT | CFGFLAG_SAVE, "UClient presence API base URL (GET list, POST /join /heartbeat /leave /switch)")
MACRO_CONFIG_STR(UcPresenceUdpServerAddress, uc_presence_udp_server_address, 256, "presence-udp.ddnet.under1111.com:8778", CFGFLAG_CLIENT | CFGFLAG_SAVE, "UClient presence UDP relay address (empty disables UDP presence)")
MACRO_CONFIG_STR(UcPresenceUdpSharedToken, uc_presence_udp_shared_token, 256, "d6409a4e897e834040a74cfc9bd63ccf7e5682c3aed36d15135c9b95151bbaaf", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shared token used to authenticate UClient presence UDP packets")
MACRO_CONFIG_INT(UcPresenceHttpHeartbeat, uc_presence_http_heartbeat, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Send periodic HTTP /heartbeat posts (UDP mode uses HTTP only for GET list; 0=UDP liveness only, 1=refresh HTTP presence TTL)")
MACRO_CONFIG_INT(UcPresenceHttpHeartbeatSeconds, uc_presence_http_heartbeat_seconds, 180, 60, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HTTP /heartbeat interval in seconds when uc_presence_http_heartbeat is enabled")
MACRO_CONFIG_INT(UcClientIndicatorInNamePlate, uc_client_indicator_in_name_plate, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show UClient logo in name plates for UC players")
MACRO_CONFIG_INT(UcClientIndicatorInNamePlateAboveSelf, uc_client_indicator_in_name_plate_above_self, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show UClient logo above yourself in name plates")
MACRO_CONFIG_INT(UcClientIndicatorInNamePlateSize, uc_client_indicator_in_name_plate_size, 30, -50, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "UClient logo size in name plates")
MACRO_CONFIG_INT(UcClientIndicatorInScoreboard, uc_client_indicator_in_scoreboard, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show UClient logo in scoreboard for UC players")
MACRO_CONFIG_INT(UcClientIndicatorInScoreboardSize, uc_client_indicator_in_scoreboard_size, 100, -50, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "UClient logo size in scoreboard")
MACRO_CONFIG_INT(UcChatPlayerSearchEngine, uc_chat_player_search_engine, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Search engine used when clicking player names in chat (0=DDNet, 1=DDStats)")
MACRO_CONFIG_COL(UcChatReplyQuoteColor, uc_chat_reply_quote_color, 65471, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reply quote name color")
MACRO_CONFIG_INT(UcShareAgreed, uc_share_agreed, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Remember that the user agreed to upload shared assets/skins to media.under1111.com")
#if defined(CONF_DEBUG) || defined(CONF_UCLIENT_DEV_BUILD)
MACRO_CONFIG_INT(UcVoiceTeam, uc_voice_team, -1, -1, 63, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice target team override (-1=off, 0..63=force that team, dev only)")
MACRO_CONFIG_INT(UcVoiceId, uc_voice_id, -1, -1, 63, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice client id override (-1=off, dev only)")
#endif
MACRO_CONFIG_STR(UcSoundboardApiBaseUrl, uc_soundboard_api_base_url, 256, "https://ddnet.under1111.com/api/soundboard", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Base URL for soundboard API")
MACRO_CONFIG_STR(UcSoundboardUploadUrl, uc_soundboard_upload_url, 256, "https://ddnet.under1111.com/api/soundboard/upload", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Upload endpoint used for personal soundboard sounds")
MACRO_CONFIG_INT(UcSoundboardMaxUploadBytes, uc_soundboard_max_upload_bytes, 5242880, 0, 33554432, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum upload size for personal soundboard files")
MACRO_CONFIG_INT(UcSoundboardMaxUploadSeconds, uc_soundboard_max_upload_seconds, 10, 1, 120, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum upload length in seconds for personal soundboard files")
MACRO_CONFIG_INT(UcVoiceTeamIncludeOwn, uc_voice_team_include_own, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "When uc_voice_team is active, also include your current team audio (0/1)")

// Auto login (Japan server)
MACRO_CONFIG_INT(UcAutoLoginJapan, uc_auto_login_japan, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically send /login on the Japan server")
MACRO_CONFIG_STR(UcAutoLoginJapanCode, uc_auto_login_japan_code, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Login code sent as /login on the Japan server")
MACRO_CONFIG_INT(UcAutoLoginJapanPromptShown, uc_auto_login_japan_prompt_shown, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Whether the Japan auto-login offer popup has already been shown once")
// Auto login (KoG servers)
MACRO_CONFIG_INT(UcAutoLoginKog, uc_auto_login_kog, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically send /login on KoG servers")
MACRO_CONFIG_STR(UcAutoLoginKogCode, uc_auto_login_kog_code, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Login code sent as /login on KoG servers")
MACRO_CONFIG_INT(UcAutoLoginKogPromptShown, uc_auto_login_kog_prompt_shown, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Whether the KoG auto-login offer popup has already been shown once")
