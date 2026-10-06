// `AAcHologram` (chunk E8): the see-through model of what is about to be put
// down, the port of `GameScene.showGhost`/`hideGhost`/`hologram(of:)`
// (GameScene+Pointers.swift). A driven Prospector's building held out (E8,
// AcPilotBuild.h); the playground's palette can use `Show` with any model.
//
// The model is the export's (`<base>_blue`), every part shown (hidden parts
// too, as Swift's clone unhides them) with `M_Hologram`
// (Tools/Editor/make_hologram_material.py): lit, additive, only the nearest
// surface (the components write custom depth; the material drops what lies
// behind it: Swift's depth-only copy drawn first). Green where it fits, red
// where not, pulsing at sin(5t). Under it a flat square plate `radius` out
// on each side (`M_AcPointerAdd`, opacity 0.3: Swift's additive plate).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AcHologram.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;

UCLASS()
class AUTOCRAFT_API AAcHologram : public AActor
{
	GENERATED_BODY()

public:
	AAcHologram();

	static AAcHologram* Find(const UWorld* World);
	static AAcHologram* SpawnFor(UWorld* World);

	/// The hologram of the catalog model `Model` standing at `At` (world cm,
	/// the ground point: lifted 0.02 cell as Swift), turned `Yaw` degrees
	/// about +Z, on a plate `Radius` cells out; `bOk` green, else red;
	/// `Time` (seconds) drives the pulse. False if the model is unknown.
	bool Show(FName Model, const FVector& At, double Yaw, double Radius, bool bOk, double Time);
	void Hide();
	bool IsShown() const { return bShown; }
	FName Model() const { return Key; }

private:
	void Build(FName Model);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Body;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Plate;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Glass;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PlateMaterial;

	FName Key;
	bool bShown = false;
};
