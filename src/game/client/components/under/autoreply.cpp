#include "autoreply.h"

#include <game/client/gameclient.h>
#include <generated/protocol.h>

#include <engine/console.h>
#include <engine/storage.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/http.h>

#include <memory>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

std::string CAutoreply::GetCurrentDateTime()
{
    char buf[32];
    std::time_t t = std::time(nullptr);
    std::tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

static bool LineShouldHighlight(const char *pLine, const char *pName)
{
    const char *pHit = str_utf8_find_nocase(pLine, pName);
    
    while(pHit)
    {
        int Length = str_length(pName);
        if(Length > 0 && (pLine == pHit || pHit[-1] == ' ') && (pHit[Length] == 0 || pHit[Length] == ' ' || pHit[Length] == '.' || pHit[Length] == '!' || pHit[Length] == ',' || pHit[Length] == '?' || pHit[Length] == ':'))
					return true;

        pHit = str_find_nocase(pHit + 1, pName);
    }
    return false;
}

CAutoreply::CAutoreply() {}

void CAutoreply::OnInit() {}

void CAutoreply::OnConsoleInit()
{
    Console()->Register(
        "tagreply", "?r[message]", CFGFLAG_CLIENT | CFGFLAG_SAVE,
        ConTagreply, this,
        "사용자가 본인을 태그했을 때 자동으로 응답합니다");
}

void CAutoreply::ConTagreply(IConsole::IResult *pResult, void *pUserData)
{
    CAutoreply *pSelf = static_cast<CAutoreply *>(pUserData);

    if (pResult->NumArguments() == 0)
    {
        if (g_Config.m_ClTagReplyMessage[0] != '\0')
        {
            char aBuf[512];
            std::snprintf(aBuf, sizeof(aBuf), "\"%s\"", g_Config.m_ClTagReplyMessage);
            pSelf->Console()->Print(
                IConsole::OUTPUT_LEVEL_STANDARD, "tagreply", aBuf);
        }
        return;
    }

    const char *pMsgText = pResult->GetString(0);
    if (pMsgText[0] == '\0')
    {
        g_Config.m_ClTagReplyMessage[0] = '\0';
    }
    else
    {
        str_copy(g_Config.m_ClTagReplyMessage, pMsgText, sizeof(g_Config.m_ClTagReplyMessage));
    }
}

void CAutoreply::OnMessage(int Msg, void *pRawMsg)
{
    if (Msg != NETMSGTYPE_SV_CHAT)
        return;

    CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;

    const char *pText = pMsg->m_pMessage;
    const char *pMyName = Client()->PlayerName();
    if (!pMyName || pMyName[0] == '\0')
        return;

    const bool IsWhisperSend = pMsg->m_Team == TEAM_WHISPER_SEND;
    const bool IsWhisperRecv = pMsg->m_Team == TEAM_WHISPER_RECV;

    std::string senderName;
    std::string targetName;

    if(IsWhisperSend)
    {
        senderName = pMyName;
        if(pMsg->m_ClientId >= 0)
            targetName = GameClient()->m_aClients[pMsg->m_ClientId].m_aName;
    }
    else if(pMsg->m_ClientId < 0)
    {
        senderName = "[SERVER]";
    }
    else
    {
        senderName = GameClient()->m_aClients[pMsg->m_ClientId].m_aName;
        if(IsWhisperRecv)
            targetName = pMyName;
    }

    if(senderName.empty())
        return;

    std::string dateTime = GetCurrentDateTime();
    
    // JSON 이스케이프 함수
    auto escapeJson = [](const std::string& str) -> std::string {
        std::string result;
        for(char c : str) {
            switch(c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c; break;
            }
        }
        return result;
    };

    if(g_Config.m_ClChatDiscordWebHookUrl[0] != '\0')
    {
        char aJson[1024];
        std::string escapedText = escapeJson(pText);
        std::string escapedSender = escapeJson(senderName);
        
        if(IsWhisperSend)
        {
            std::string escapedTarget = escapeJson(targetName.empty() ? "?" : targetName);
            str_format(aJson, sizeof(aJson), "{\"content\":\"`%s` (whisper -> %s) `%s`: %s\"}", dateTime.c_str(), escapedTarget.c_str(), escapedSender.c_str(), escapedText.c_str());
        }
        else if(IsWhisperRecv)
        {
            str_format(aJson, sizeof(aJson), "{\"content\":\"||<@776421522188664843>|| (whisper) `%s` `%s`: %s\"}", dateTime.c_str(), escapedSender.c_str(), escapedText.c_str());
        }
        else
        {
            if(LineShouldHighlight(pText, pMyName))
            {
                if(pMsg->m_ClientId < 0)
                    return;

                if(senderName == pMyName)
                {
                    str_format(aJson, sizeof(aJson), "{\"content\":\"`%s` `%s`: %s\"}", dateTime.c_str(), escapedSender.c_str(), escapedText.c_str());
                }
                else
                {
                    str_format(aJson, sizeof(aJson), "{\"content\":\"||<@776421522188664843>|| `%s` `%s`: %s\"}", dateTime.c_str(), escapedSender.c_str(), escapedText.c_str());
                }
            }
            else
            {
                str_format(aJson, sizeof(aJson), "{\"content\":\"`%s` `%s`: %s\"}", dateTime.c_str(), escapedSender.c_str(), escapedText.c_str());
            }
        }

        auto pUniqueReq = HttpPostJson(
            g_Config.m_ClChatDiscordWebHookUrl,
            aJson
        );

        std::shared_ptr<IHttpRequest> pReq(
            pUniqueReq.release(),
            [](IHttpRequest *p){ delete static_cast <CHttpRequest*>(p); }
        );

        if (auto pHttp = Kernel()->RequestInterface<IHttp>())
            pHttp->Run(pReq);
    }

    if (g_Config.m_ClTagReply == 1)
    {
        if (g_Config.m_ClTagReplyMessage[0] == '\0')
            return;
    
    
        char aBuf[256];
        if(LineShouldHighlight(pText, pMyName))
        {   
            if (pMsg->m_ClientId < 0)
                return;

            if(g_Config.m_ClTagDiscordWebHookUrl[0] != '\0')
            {
                char bJson[1024];
                if (pMsg->m_Team == TEAM_WHISPER_RECV)
                {
                    str_format(bJson, sizeof(bJson), "{\"content\":\"||<@776421522188664843>|| (whisper) `%s` `%s`: %s\"}", dateTime, senderName, pText);
                }
                else
                {
                    if(senderName == "언더")
                        return;

                    str_format(bJson, sizeof(bJson), "{\"content\":\"||<@776421522188664843>|| `%s` `%s`: %s\"}", dateTime, senderName, pText);
                }

                auto pUniqueReq = HttpPostJson(
                    g_Config.m_ClTagDiscordWebHookUrl,
                    bJson
                );

                std::shared_ptr<IHttpRequest> pReq(
                    pUniqueReq.release(),
                    [](IHttpRequest *p){ delete static_cast <CHttpRequest*>(p); }
                );

                if (auto pHttp = Kernel()->RequestInterface<IHttp>())
                    pHttp->Run(pReq);
            }

            char autoMessage[128];
            if(str_comp(g_Config.m_ClLanguagefile, "languages/korean.txt") == 0)
                str_copy(autoMessage, "이 메세지는 자동 응답이에요!", sizeof(autoMessage));
            else if(str_comp(g_Config.m_ClLanguagefile, "languages/simplified_chinese.txt") == 0)
                str_copy(autoMessage, "此消息为自动回复", sizeof(autoMessage));
            else if(str_comp(g_Config.m_ClLanguagefile, "languages/traditional_chinese.txt") == 0)
                str_copy(autoMessage, "此訊息為自動回覆", sizeof(autoMessage));
            else
                str_copy(autoMessage, "This message is an auto-reply!", sizeof(autoMessage));
            
            if (pMsg->m_Team == TEAM_WHISPER_RECV)
            {
                str_format(aBuf, sizeof(aBuf), "/w %s %s - %s", senderName, g_Config.m_ClTagReplyMessage, autoMessage);
                GameClient()->m_Chat.SendChat(0, aBuf);
            }
            else
            {
                str_format(aBuf, sizeof(aBuf), "%s: %s - %s", senderName, g_Config.m_ClTagReplyMessage, autoMessage);
                GameClient()->m_Chat.SendChat(0, aBuf);
            }
        }
    }
}
