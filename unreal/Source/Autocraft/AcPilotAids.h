// `AAcPilotAids`: what the world shows round a driven unit (GAME-LAYER.md
// §2.12, chunk E4):
//
// - The range ring (`RangeRing.swift`): how far the unit's weapon (or a
//   Dropship's heal) reaches, on the ground round it. A disc of rings from
//   just past the unit out to the edge whose added light grows toward the
//   rim (vertex colour 0.05 + 0.12·f³, nothing in the middle), a thin band
//   at the edge, and for an anchored Longbow a red band at its minimum
//   range. Cyan at rest, red with an enemy in reach under the sight, green
//   with a patient in reach; fainter while a tank anchors or packs up. Draped
//   0.08 cells over `TerrainField.height`, rebuilt only when the cue moves
//   (segments `clamp(2πr / 0.35, 48, 160)`). Additive, unlit, no depth
//   write (`M_AcRangeRing`), so units and buildings stand over it.
// - The pilot's fog veil (`FogOfWar.veil`): the fog of war's top-down pass is
//   off while driving (`AAcFog::SetSuppressed`); instead the ground round the
//   unit is clear to a quarter of how far the eye sees (`FAcSkyTones::Reach`,
//   40 cells at night to 110 by day) and lost in the sky from there on, so
//   the map's edge never shows; with the Swift pilot camera's vignette
//   (0.55 in the corners). `M_AcPilotVeil`, a post-process material on an
//   unbound component, the sky being the atmosphere toward the pixel
//   (`SkyAtmosphereViewLuminance`). Only on a map with a fog grid, as Swift
//   (`fogInPass`). `ac.PilotVeil 0` turns it off.
//
// Materials: Tools/Editor/make_pilot_materials.py (`/Game/Pilot/`).
// `AAcPilotPawn` drives it: `Update` each frame while driving, `Hide` on
// leaving. Spawned on first use.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AcPilotAim.h"

#include "AcPilotAids.generated.h"

class UMaterialInstanceDynamic;
class UPostProcessComponent;
class UProceduralMeshComponent;

UCLASS()
class AUTOCRAFT_API AAcPilotAids : public AActor
{
	GENERATED_BODY()

public:
	AAcPilotAids();

	static AAcPilotAids* SpawnFor(UWorld* World);
	static AAcPilotAids* Find(const UWorld* World);

	/// This frame's ring (unset: none) and the driven unit's body (world cm),
	/// the veil's centre.
	void Update(const TOptional<FAcRangeCue>& Cue, const FVector& Body);
	/// Off: not driving.
	void Hide();

	/// The ring as last built (for tests and logs).
	const TOptional<FAcRangeCue>& Shown() const { return Built; }

private:
	void Rebuild(const FAcRangeCue& Cue);
	void Paint(const FAcRangeCue& Cue);
	void Veil(bool bOn, const FVector& Body);
	void EnsureMaterials();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> Ring;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPostProcessComponent> Post;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DiscMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EdgeMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MinMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> VeilMaterial;

	TOptional<FAcRangeCue> Built;
	bool bMaterials = false;
	bool bMinShown = false;
};
