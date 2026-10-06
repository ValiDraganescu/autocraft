// `AAcPointerCues`: what the pointer shows in the world (GAME-LAYER.md
// §2.3, chunk D4), the port of `GameScene.showHover` and `showSelection`
// (Sources/Autocraft/GameScene+Pointers.swift:55, :69) and their nodes
// (`Pointers`, :171).
//
// - Hover ring: a thin torus (pipe 0.022) under what the pointer rests on,
//   green for the player's (and allies'), red for an enemy's, 75 % opaque.
// - Uplink (a unit the player can drive): a cyan torus (pipe 0.045) that
//   breathes (`0.5 + 0.5·sin 5t`), four L brackets closing in
//   (`1 + 0.18·(0.5 + 0.5·sin 2.6t)`) and turning (1.1 rad/s) drawn over
//   everything (no depth test), a beam 2.2 cells tall fading upwards, and a
//   soft disc on the ground.
// - Selection ring: a torus (pipe 0.035) round the selected building,
//   radius `1.42·r + 0.25`, spinning at 0.6 rad/s, emission 1.4.
//
// Meshes are made here at runtime (torus, bars, open cylinder, quad);
// materials are `/Game/Materials/M_AcPointer` (translucent), `M_AcPointerAdd`
// (additive) and `M_AcPointerTop` (translucent, no depth test), made by
// `Tools/Editor/make_pointer_materials.py`. Each component gets a dynamic
// instance (Color, Intensity, Opacity, Shape).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SimdMath.h"

#include "AcPointerCues.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;

/// Whose thing the pointer rests on (`HoverTone`).
enum class EAcHoverTone : uint8 { Own, Enemy, Drive };

/// `HoverCue`: where and how big the cue under something is (cells).
struct FAcHoverCue
{
	ac::Vec2 Position;
	/// The height its body stands at.
	double Y = 0.0;
	double Radius = 1.0;
	EAcHoverTone Tone = EAcHoverTone::Own;
	bool bUnit = true;
};

UCLASS()
class AUTOCRAFT_API AAcPointerCues : public AActor
{
	GENERATED_BODY()

public:
	AAcPointerCues();

	static AAcPointerCues* SpawnFor(UWorld* World);
	static AAcPointerCues* Find(const UWorld* World);

	/// The cue under what the pointer rests on (unset: none). `Time`: game seconds.
	void ShowHover(const TOptional<FAcHoverCue>& Cue, double Time);
	/// Ring a building of radius `Radius` at `Position`, ground height `GroundY` (unset: none).
	void ShowSelection(const TOptional<ac::Vec2>& Position, double GroundY, double Radius, double Time);

	/// sRGB colours of the Swift game (`Pointers`, `HUD.makeTip`).
	static FLinearColor ToneColor(EAcHoverTone Tone);
	static FLinearColor UplinkColor();

private:
	UStaticMeshComponent* Part(const TCHAR* Name, UStaticMesh* Mesh, const TCHAR* Material, int32 SortPriority,
		USceneComponent* Parent);
	void Build();

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> HoverRing;
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Uplink;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> UplinkRing;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Brackets;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Beam;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Disc;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SelectRing;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> Meshes;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Mids;

	bool bBuilt = false;
};
