#ifndef GAME_CLIENT_COMPONENTS_UNDER_TAGREPLY_H
#define GAME_CLIENT_COMPONENTS_UNDER_TAGREPLY_H

#include <game/client/component.h>

#include <engine/shared/http.h>
#include <engine/console.h>

#include <memory>
#include <string>

class CAutoreply : public CComponent
{
	class IEngineGraphics *m_pGraphics = nullptr;

	public:
		CAutoreply();

		virtual void OnInit() override;
		virtual void OnConsoleInit() override;
		virtual void OnMessage(int Msg, void *pRawMsg) override;

		static void ConTagreply(IConsole::IResult *pResult, void *pUserData);

		virtual int Sizeof() const override { return sizeof(*this); }

	private:
		std::string GetCurrentDateTime();
};

#endif