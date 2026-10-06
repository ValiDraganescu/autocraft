// `AAcModelRow`: a debug actor that puts every model of the catalog in rows
// at its rest pose, assembled from its parts (FAcModelCatalog), for the
// level /Game/Maps/ModelRow (Tools/Editor/make_model_row.py). It builds in
// the editor too (OnConstruction), so the level shows the models.
//
// Rows, front to back: units, buildings, construction scaffolds, cockpits,
// resources, doodads, effects, and a palette row (the Ranger and the
// Citadel in each of the 8 player colours). Each model is a scene
// component per part (the part's rest transform under its parent part)
// holding one static mesh component per material: what UAcWorldRenderer
// will do with instances.
//
// Command line, in a -game run of the level:
//   -AcModelFocus=NAME  show only that model, posed and lit the way
//                       `Autocraft export-models --compare` renders it
//                       (same camera, 30° FOV, the units turned
//                       -(π/2 + 0.6) about up), for a side-by-side check.
//                       Use with -AcShot=OUT.png and -ResX=N -ResY=N (square).
//                       `-AcModelFocus=row` frames the whole row instead.
//   -AcModelTeam=N      draw the focused model in player N's colour.
//   -AcModelDistance=F  move the camera F times as far.
//   -AcModelFloor       keep the floor.
//   -AcModelView=V      front|right|back|left|top|3q: a camera on the focused model for the stencil sheet
//                       (pipeline skill step 2). front|right|back|left|top are orthographic, all at one scale
//                       and one centre (the bounds' centre; 1.2 x the longest side fits a 512 px panel); 3q is a
//                       perspective view from the sheet's 3/4 direction. The model is drawn in grey clay
//                       (-AcModelPaint=clay, lit by a fixed light) or in one flat colour per part
//                       (-AcModelPaint=parts); hidden parts and translucent or additive meshes stay out.
//                       Meant for -ResX=512 -ResY=512. Writes the camera numbers next to the shot, as
//                       <shot>.json (the format of one panel of stencil.json). The sheet is composed by
//                       .claude/skills/autocraft-model-pipeline/stencil.py.
// Shots: use a 16:9 -ResX/-ResY (AcShot crops other aspects, 2026-10-05);
// the camera keeps the 30° vertical FOV, so crop the centre square to compare.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AcModelRow.generated.h"

class ACameraActor;
class USceneComponent;
class UStaticMeshComponent;
struct FAcModelInfo;

UCLASS()
class AUTOCRAFT_API AAcModelRow : public AActor
{
	GENERATED_BODY()

public:
	AAcModelRow();

	/// Comma-separated model names; empty for all.
	UPROPERTY(EditAnywhere, Category = "Autocraft")
	FString Models;

	/// The red exports too (the same meshes as blue, team 1).
	UPROPERTY(EditAnywhere, Category = "Autocraft")
	bool bIncludeRed = true;

	/// Show the parts that start hidden (flashes, spare frames).
	UPROPERTY(EditAnywhere, Category = "Autocraft")
	bool bShowHiddenParts = false;

	/// A row with the Ranger and the Citadel in all 8 player colours.
	UPROPERTY(EditAnywhere, Category = "Autocraft")
	bool bPaletteRow = true;

	/// Space between models, cm.
	UPROPERTY(EditAnywhere, Category = "Autocraft")
	float Gap = 150.f;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/// Show only this model at the origin, posed and framed like the Swift
	/// `--compare` render.
	void Focus(FName Model, int32 Team);
	/// Frame the whole row (-AcModelFocus=row).
	void Overview();

private:
	void Build();
	/// -AcModelView: the stencil camera, the clay or part-colour materials, the camera numbers.
	void ApplyView(const FAcModelInfo& Model, const FString& View, const FString& Paint);
	USceneComponent* AddModel(const FAcModelInfo& Model, const FVector& Location, int32 Team);

	struct FPlaced
	{
		FName Model;
		USceneComponent* Holder = nullptr;
	};
	TArray<FPlaced> Placed;

	/// Every mesh component AddModel made, with the part it belongs to (for -AcModelView).
	struct FPlacedMesh
	{
		TWeakObjectPtr<UStaticMeshComponent> Component;
		int32 Part = INDEX_NONE;
		FName Blend;
	};
	TArray<FPlacedMesh> PlacedMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Built;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> FocusCamera;

	/// Vertical FOV to keep in focus mode (0: leave the camera's as is).
	double FocusVerticalFov = 0.0;
	bool bHolding = false;
	int32 SettleFrames = 0;
};
