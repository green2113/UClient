#ifndef GAME_CLIENT_COMPONENTS_UNDER_CHATSKIN_H
#define GAME_CLIENT_COMPONENTS_UNDER_CHATSKIN_H

#include <game/client/component.h>
#include <engine/console.h>

class CChatskin : public CComponent
{
	public:
		CChatskin();

		virtual void OnConsoleInit() override;
		virtual void OnMessage(int Msg, void *pRawMsg) override;

		virtual int Sizeof() const override { return sizeof(*this); }

	private:
		static void ConMessage(IConsole::IResult *pResult, void *pUserData);

		char m_aTriggerMessage[256];
};

#endif