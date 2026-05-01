#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_UCLIENT_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_UCLIENT_H

#include <game/client/component.h>

class CUClient : public CComponent
{
public:
	CUClient() = default;
	int Sizeof() const override { return sizeof(*this); }

	bool ChatDoSkin(const char *pInput);
};

#endif
