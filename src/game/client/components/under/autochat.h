#ifndef GAME_CLIENT_COMPONENTS_UNDER_AUTOCHAT_H
#define GAME_CLIENT_COMPONENTS_UNDER_AUTOCHAT_H

#include <game/client/component.h>
#include <engine/console.h>


class CAutochat : public CComponent
{
	public:
		CAutochat();

		virtual void OnStateChange(int NewState, int OldState) override;
		virtual void OnConsoleInit() override;
		virtual void OnInit() override;

		static void ConEnterChat(IConsole::IResult *pResult, void *pUserData);

		virtual int Sizeof() const override { return sizeof(*this); }

	private:
		char m_aEnterMsg[256];
		bool m_SendOnce;

		const char *CfgFilename() const { return "under_autochat.cfg"; }

		void LoadConfig();
		void SaveConfig() const;
};

#endif