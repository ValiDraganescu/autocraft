// `AAcBeacons`: the objective beacons in the world (chunk D8, GAME-LAYER.md
// §2.3 "Objective beacons"), the port of `GameScene+Beacons.swift`: where
// the team's humans sent its army, a column of light 7 cells tall (a thin
// beam pulsing 0.45-1 over 1.8 s and a faint halo), a ring on the ground
// (radius 1.5, breathing to 1.25×) and four ticks turning once in 6 s. Red
// for an attack, blue for a base to defend, teal for an area held, violet for
// a Bastion to man, amber for a site to expand to.
//
// Additive and unlit (SceneKit `.constant`, `blendMode = .add`): D4's
// `/Game/Materials/M_AcPointerAdd` (Color, Intensity, Opacity), meshes made
// here. `UAcCommandMapSubsystem` spawns it and calls `Show` with the team's
// orders (`FAcCommandMapLogic::Beacons`) four times a second; a beacon whose
// kind changed (an attack turned hold) is made again in its new colour.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AcCommandMapLogic.h"

#include "AcBeacons.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class AUTOCRAFT_API AAcBeacons : public AActor
{
	GENERATED_BODY()

public:
	AAcBeacons();

	static AAcBeacons* SpawnFor(UWorld* World);
	static AAcBeacons* Find(const UWorld* World);

	/// Show these beacons; the rest go (`showObjectives`).
	void Show(const TArray<FAcBeaconSpec>& List);
	int32 Num() const { return Beacons.Num(); }

	virtual void Tick(float DeltaSeconds) override;

private:
	struct FBeacon
	{
		TOptional<ac::Objective::Kind> Kind;
		TObjectPtr<USceneComponent> Root;
		TObjectPtr<UStaticMeshComponent> Beam;
		TObjectPtr<UStaticMeshComponent> Halo;
		TObjectPtr<UStaticMeshComponent> Ring;
		TObjectPtr<UStaticMeshComponent> Ticks;
		TObjectPtr<UMaterialInstanceDynamic> BeamMaterial;
		double Born = 0;
	};
	void BuildMeshes();
	FBeacon Make(int64 Id, TOptional<ac::Objective::Kind> Kind);
	UStaticMeshComponent* Part(const FName Name, UStaticMesh* Mesh, USceneComponent* Parent, const FLinearColor& Color, double Opacity,
		UMaterialInstanceDynamic** OutMaterial = nullptr);
	void Remove(FBeacon& B);
	double Now() const;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> Meshes;
	/// Keeps every beacon's components and materials alive.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> Owned;

	TMap<int64, FBeacon> Beacons;
	int32 Made = 0;
};
