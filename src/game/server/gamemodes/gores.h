#ifndef GAME_SERVER_GAMEMODES_GORES_H
#define GAME_SERVER_GAMEMODES_GORES_H

#include "DDRace.h"

class CGameControllerGores : public CGameControllerDDRace
{
public:
	CGameControllerGores(class CGameContext *pGameServer);
	~CGameControllerGores() override;

	void Tick() override;
};
#endif // GAME_SERVER_GAMEMODES_GORES_H
