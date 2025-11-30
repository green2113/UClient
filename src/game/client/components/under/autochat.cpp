#include "autochat.h"

#include <game/client/components/chat.h>
#include <game/client/gameclient.h>
#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

CAutochat::CAutochat()
{
	m_aEnterMsg[0] = '\0';
};

void CAutochat::OnInit()
{
	LoadConfig();
	m_SendOnce = false;
}

void CAutochat::OnConsoleInit()
{
	Console()->Register("enterchat", "?r[message]", CFGFLAG_CLIENT, ConEnterChat, this, "서버에 접속할 때 자동으로 채팅을 보냅니다.");
};

void CAutochat::ConEnterChat(IConsole::IResult *pResult, void *pUserData)
{
	CAutochat *pSelf = static_cast<CAutochat *>(pUserData);

	if(pResult->NumArguments() == 0)
	{
		if(pSelf->m_aEnterMsg[0] == '\0')
		{
		}
		else
		{
			char aBuf[512];
			std::snprintf(aBuf, sizeof(aBuf), "\"%s\"", pSelf->m_aEnterMsg);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "autochat", aBuf);
		}
		return;
	}
	const char *pMsg = pResult->GetString(0);
	if(pMsg[0] == '\0')
	{
		pSelf->m_aEnterMsg[0] = '\0';
	}
	else
	{
		str_copy(pSelf->m_aEnterMsg, pMsg, sizeof(pSelf->m_aEnterMsg));
	}
	pSelf->SaveConfig();
}

void CAutochat::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_ONLINE && m_aEnterMsg[0] != '\0' && !m_SendOnce == true)
	{
		GameClient()->m_Chat.SendChat(0, m_aEnterMsg);
		m_SendOnce = true;
	}
}

void CAutochat::LoadConfig()
{
  IOHANDLE io = Storage()->OpenFile(CfgFilename(), IOFLAG_READ, IStorage::TYPE_SAVE);
  if (!io) return;
  char buffer[sizeof(m_aEnterMsg)] = {0};
  int read = io_read(io, buffer, sizeof(buffer)-1);
  io_close(io);
  if (read > 0)
{

    int len = read;
    if (buffer[len-1]=='\n' || buffer[len-1]=='\r') buffer[--len] = '\0';
    str_copy(m_aEnterMsg, buffer, sizeof(m_aEnterMsg));
  }
}

void CAutochat::SaveConfig() const
{
  IOHANDLE io = Storage()->OpenFile(CfgFilename(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
  if (!io) return;
  io_write(io, m_aEnterMsg, std::strlen(m_aEnterMsg));
  io_close(io);
}