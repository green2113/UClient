/* (c) DDNet developers. See licence.txt in the root of the distribution for more information. */
#include "trail_projectile.h"

#include "character.h"

#include <generated/protocol.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <base/math.h>
#include <cstdint>

CTrailProjectile::CTrailProjectile(CGameWorld *pGameWorld, int ClientId, int WeaponType, bool Freeze, bool Explosive, bool StarEffect, bool OnlyWhileMoving) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE)
{
	m_ClientId = ClientId;
	m_WeaponType = WeaponType;
	m_StartTick = Server()->Tick();
	m_Freeze = Freeze;
	m_Explosive = Explosive;
	m_StarEffect = StarEffect;
	m_LastStarTick = 0;
	m_OnlyWhileMoving = OnlyWhileMoving;

	if(CCharacter *pChar = GameServer()->GetPlayerChar(m_ClientId))
	{
	m_Pos = pChar->GetPos();
	}

	GameWorld()->InsertEntity(this);
}

CTrailProjectile::~CTrailProjectile()
{
	if(GameServer())
		GameServer()->OnTrailDestroyed(m_ClientId, this);
}

void CTrailProjectile::Reset()
{
	m_MarkedForDestroy = true;
	if(GameServer())
		GameServer()->OnTrailDestroyed(m_ClientId, this);
}

void CTrailProjectile::Tick()
{
	CCharacter *pChar = GameServer()->GetPlayerChar(m_ClientId);
	if(!pChar || !pChar->IsAlive() || !pChar->GetPlayer() || pChar->GetPlayer()->m_TrailMode == 0)
	{
		Reset();
		return;
	}
	if(m_OnlyWhileMoving && pChar->GetPlayer()->GetTeam() == TEAM_SPECTATORS)
	{
		Reset();
		return;
	}
	if(m_OnlyWhileMoving && length(pChar->Core()->m_Vel) <= 0.1f)
		return;

	m_Pos = pChar->GetPos();
	m_StartTick = Server()->Tick();
	if(m_StarEffect)
	{
		const vec2 Vel = pChar->Core()->m_Vel;
		const float Speed = length(Vel);
		const int Tick = Server()->Tick();
		const int Interval = maximum(1, Server()->TickSpeed() / 2);
		if(Speed > 0.1f && Tick - m_LastStarTick >= Interval)
		{
			m_LastStarTick = Tick;
			const vec2 Dir = normalize(Vel);
			uint32_t Seed = (uint32_t)(Tick * 1103515245u + m_ClientId * 12345u);
			Seed ^= Seed << 13;
			Seed ^= Seed >> 17;
			Seed ^= Seed << 5;
			const float Side = ((int)(Seed % 17) - 8) * 0.75f;
			const float Forward = -(12.0f + (float)((Seed >> 8) % 6));
			const vec2 Perp(-Dir.y, Dir.x);
			const vec2 Offset = Dir * Forward + Perp * Side;
			const vec2 ShootDir = -Dir;
			const float Angle = -std::atan2(ShootDir.x, ShootDir.y);
			GameServer()->CreateDamageInd(m_Pos + Offset, Angle, 1, pChar->TeamMask());
		}
	}
}

void CTrailProjectile::Snap(int SnappingClient)
{
	CCharacter *pChar = GameServer()->GetPlayerChar(m_ClientId);
	if(!pChar || !pChar->CanSnapCharacter(SnappingClient))
		return;

	if(NetworkClipped(SnappingClient, m_Pos))
		return;

	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	if(m_OnlyWhileMoving)
	{
		if(!pChar || length(pChar->Core()->m_Vel) <= 0.1f)
			return;
	}
	if(m_StarEffect)
	{
		return;
	}

	if(SnappingClientVersion >= VERSION_DDNET_ENTITY_NETOBJS)
	{
		CNetObj_DDNetProjectile *pProj = static_cast<CNetObj_DDNetProjectile *>(Server()->SnapNewItem(NETOBJTYPE_DDNETPROJECTILE, GetId(), sizeof(CNetObj_DDNetProjectile)));
		if(!pProj)
			return;

		int Flags = 0;
		if(m_Explosive)
			Flags |= PROJECTILEFLAG_EXPLOSIVE;
		if(m_Freeze)
			Flags |= PROJECTILEFLAG_FREEZE;

		pProj->m_X = round_to_int(m_Pos.x * 100.0f);
		pProj->m_Y = round_to_int(m_Pos.y * 100.0f);
		pProj->m_VelX = 0;
		pProj->m_VelY = 0;
		pProj->m_StartTick = m_StartTick;
		pProj->m_Type = m_WeaponType;
		pProj->m_Owner = -1;
		pProj->m_Flags = Flags;
		pProj->m_SwitchNumber = m_Number;
		pProj->m_TuneZone = 0;
	}
	else
	{
		CNetObj_Projectile *pProj = Server()->SnapNewItem<CNetObj_Projectile>(GetId());
		if(!pProj)
			return;

		pProj->m_X = static_cast<int>(m_Pos.x);
		pProj->m_Y = static_cast<int>(m_Pos.y);
		pProj->m_VelX = 0;
		pProj->m_VelY = 0;
		pProj->m_StartTick = m_StartTick;
		pProj->m_Type = m_WeaponType;
	}
}

void CTrailProjectile::SwapClients(int Client1, int Client2)
{
	if(m_ClientId == Client1)
		m_ClientId = Client2;
	else if(m_ClientId == Client2)
		m_ClientId = Client1;
}
