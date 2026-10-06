// Chunk C5 (GAME-LAYER.md §2.7): deaths 2. Machines blow apart, flyers are
// shot down, buildings collapse: the `GameScene.died` cases C4 leaves
// (GameScene.swift:1367: the Prospector, Firefly, Longbow, Hailstorm,
// Dropship, Kestrel), `GameScene.blowApart` (:1546), `shootDown`
// (Models+Dropship.swift:511) and `GameScene.destroyed` (:1561), as a
// `UAcEffects` extension (`shatter`, order 140, AcEffectsShatter.cpp).
//
// How a death plays (C4's pattern):
//   1. `OnRemoved` (renderer stage) keeps the last drawn pose of a model
//      this extension draws; a fog-gated `Died`/`Destroyed` of that id at
//      the effects stage turns it into a wreck. Poses kept without one
//      (left sight) are dropped after 0.25 s of game time.
//   2. The wreck is the model cut into chunks (`FAcShatterLibrary`,
//      AcShatter.h; every model is cut in the background after the game
//      starts, a death that comes first finishes its model at once), drawn
//      in instance pools per sub (`MakePool`, a ring of wrecks per model;
//      the oldest is taken over when it is full; a pool's component is
//      visible only while one of its wrecks lives), each sub at its part's
//      world at death × its chunk's move (`AcShatter::ChunkDelta`).
//   3. Burnt metal: `M_AcEmber` (Tools/Editor/make_ember_material.py) with
//      the model instance's values, custom data 3 = the hot spots' glow
//      (≥ 0 charred, < 0 intact), custom data 1 = 0; lamps on C4's fading
//      masters at 0.12 of their last emission. Fade over the last 1.5 s by
//      the dither (custom data 2).
//   4. Fire and smoke from the burning chunks: `Models.damageFire` as C3's
//      `DamageFire`/`DamageSmoke` streams on the chunk's frame.
//   5. The rest through `UAcEffects`: explosions (C3, lit by C6), debris
//      (C4), scorch and decals (C2).
// Per kind (Swift): Prospector (blast 0.8, dark mark 0.6, 7 chunks of
// size 0.7, no scorch; inside a Derrick or aboard: the blast only), Firefly
// 10 × 1, Longbow 14 × 1.35, Hailstorm 12 × 1.2; Dropship 12 × 1.2 and
// Kestrel 10 × 1 fall for 1.15 s spinning and trailing fire first; every
// building 10 + 7r chunks, 4 blasts (0, 0.25, 0.65, 1.05 s), a mark of
// r + 1.2 for 45 s.
//
// Dev: `-AcShatterSheet=KIND -AcShatterSheetSpec=JSON` (AcEffectsShatterSheet.h)
// draws one kind's death at several moments; `-AcShatterDemo="X,Y"` kills
// one of every C5 kind from its rest pose in two rows at sim (X, Y) (not a
// sim kill: wrecks only), 1 s in and every `-AcShatterEvery=S`;
// `-AcShatterAge=A` pauses A s after and holds `-AcShot`; `ac.ShatterStats N` (or `-AcShatterStats=N`)
// logs wrecks, subs and the update's game-thread time every N s.
#pragma once

#include "CoreMinimal.h"

#include "Rules.h"

namespace AcEffectsShatter
{
	/// What a kind's death cuts its model into and how hard it goes (Swift's
	/// `chunks`, `size`, the stripped nodes). False: not a C5 kind.
	struct FSpec
	{
		const TCHAR* Model = nullptr;
		int32 Chunks = 10;
		double Size = 1;
		TArray<FString> Strip;
		bool bFlyer = false;
		/// The Atlas: it kneels and topples (`AcShatter::AtlasFallPose`) before it bursts.
		bool bTopple = false;
		bool bBuilding = false;
		int32 Ring = 8;
		/// Buildings: `GameScene.makeBuilding`'s height and radius.
		double Height = 0, Radius = 0;
	};
	AUTOCRAFT_API bool SpecOf(ac::UnitKind Kind, FSpec& Out);
	AUTOCRAFT_API bool SpecOf(ac::StructureKind Kind, FSpec& Out);
}
