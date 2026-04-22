/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_MAGIC_PARTICLES_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_MAGIC_PARTICLES_H

#include <base/vmath.h>

#include <game/client/component.h>

class CMagicParticles : public CComponent
{
	float m_SpawnAccumulator = 0.0f;

	void ResetState();
	bool CanSpawnParticles(int &Count, float &Radius, float &Size, float &AlphaDelay, int &Type, vec2 &BasePos) const;

public:
	void OnRender() override;
	void OnReset() override;

	int Sizeof() const override { return sizeof(*this); }
};

#endif
