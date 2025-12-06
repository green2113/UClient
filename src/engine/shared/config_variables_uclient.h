// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Save, Desc)
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Save, Desc)
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Save, Desc)
#endif

// 리플레이
MACRO_CONFIG_INT(ClTagReply, uc_tag_reply, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable automatic reply when your name is tagged")
MACRO_CONFIG_STR(ClTagReplyMessage, uc_tag_reply_message, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Message to send when automatic tag reply triggers")

// 채팅 로그
MACRO_CONFIG_STR(ClTagDiscordWebHookUrl, uc_tag_discord_webhook_url, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Discord webhook URL used when automatic tag reply triggers")
MACRO_CONFIG_STR(ClChatDiscordWebHookUrl, uc_chat_discord_webhook_url, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Discord webhook URL used to send chat logs")

// 스킨
MACRO_CONFIG_STR(ClChatSkinMessage, uc_chat_skin_message, 128, "uc_chat_skin_message", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keyword that triggers automatic skin change")
MACRO_CONFIG_STR(ClSkinSwitchSkinName, uc_skin_switch_skin_name, 64, "default", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skin name to switch to when using /skin command")
MACRO_CONFIG_INT(UcSkinSwitchUseCustomColors, uc_skin_switch_use_custom_colors, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use custom body and feet colors when switching skin")
MACRO_CONFIG_STR(ClSkinSwitchBodyColor, uc_skin_switch_body_color, 9, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Body color to use when switching skin")
MACRO_CONFIG_STR(ClSkinSwitchFeetColor, uc_skin_switch_feet_color, 9, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Feet color to use when switching skin")

// 번역
MACRO_CONFIG_INT(UcTranslate, uc_translate, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically translate chat messages before sending")
MACRO_CONFIG_STR(UcTranslateTarget, uc_translate_target, 8, "en", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Translation target language (google: ISO 639, deepl: ISO 639-1 + ISO 15924)")
MACRO_CONFIG_STR(UcTranslateApi, uc_translate_api, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom translation API endpoint for chat translator")
MACRO_CONFIG_STR(UcTranslateKey, uc_translate_key, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "API key for the custom translation endpoint")
MACRO_CONFIG_STR(UcTranslateBackend, uc_translate_backend, 16, "google", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Translation backend (google, deepl)")

// 디스코드 게임 활동 이미지
MACRO_CONFIG_INT(UcRichPresenceImage, uc_rich_presence_image, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Discord Rich Presence image index")

// 업데이트 알림
MACRO_CONFIG_INT(TcUpdateNotice, uc_update_notice, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show UClient update notifications")
