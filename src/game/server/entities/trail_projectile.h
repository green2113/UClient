/* (c) DDNet developers. See licence.txt in the root of the distribution for more information. */
#ifndef GAME_SERVER_ENTITIES_TRAIL_PROJECTILE_H
#define GAME_SERVER_ENTITIES_TRAIL_PROJECTILE_H

#include <game/server/entity.h>

class CTrailProjectile : public CEntity
{
public:
	CTrailProjectile(CGameWorld *pGameWorld, int ClientId, int WeaponType, bool Freeze, bool Explosive, bool StarEffect, bool OnlyWhileMoving);
	~CTrailProjectile() override;

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
	void SwapClients(int Client1, int Client2) override;

private:
	int m_ClientId;
	int m_WeaponType;
	int m_StartTick;
	bool m_Freeze;
	bool m_Explosive;
	bool m_StarEffect;
	int m_LastStarTick;
	bool m_OnlyWhileMoving;
};

#endif
