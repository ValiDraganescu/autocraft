// The C3 effects extension (AcEffectsBursts.cpp): explosions, the particle
// bursts, the vehicles' jets and plumes, drill sparks and hurt buildings'
// smoke and fire, drawn by `FAcParticles`.
//
// Other chunks emit their own particles through the same set: C2 a grenade's
// or shell's trail (`EAcParticle::GrenadeTrail`, `ShellFire`, `ShellSmoke`
// streamed along the flight), C5 a burning wreck or a Dropship going down
// (`DamageSmoke`/`DamageFire` with `FAcEmit::ShapeScale` = Swift's
// `damageFire(radius:)`).
#pragma once

#include "CoreMinimal.h"

class FAcParticles;
class UAcEffects;

namespace AcEffectsBursts
{
	/// The particle set of `Effects`' world (null before its `BeginPlay` or
	/// if the particle assets are missing). Game thread.
	AUTOCRAFT_API FAcParticles* ParticlesOf(const UAcEffects* Effects);
}
