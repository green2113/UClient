#include <engine/shared/config.h>

#include "gores.h"

#define GAME_TYPE_NAME "Gores"
#define TEST_TYPE_NAME "TestGores"

CGameControllerGores::CGameControllerGores(CGameContext *pGameServer)
    : CGameControllerDDRace(pGameServer)
{
    m_pGameType = g_Config.m_SvTestingCommands ? TEST_TYPE_NAME : GAME_TYPE_NAME;

    // Add gores-specific initialization here if needed.
}

CGameControllerGores::~CGameControllerGores() = default;

void CGameControllerGores::Tick()
{
    // Reuse the DDRace tick logic (handles race timing, teams, etc.).
    CGameControllerDDRace::Tick();
}
