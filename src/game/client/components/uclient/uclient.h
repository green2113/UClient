#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_UCLIENT_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_UCLIENT_H

#include <engine/console.h>
#include <game/client/component.h>

class CUClient : public CComponent
{
	static void ConNtc(IConsole::IResult *pResult, void *pUserData);

public:
	CUClient() = default;
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	bool ChatDoSkin(const char *pInput);
};

#endif
