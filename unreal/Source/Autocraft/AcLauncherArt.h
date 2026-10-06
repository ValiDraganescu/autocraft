// `FAcLauncherArt`: the key art behind the home screen. Every *.jpg / *.png in
// `unreal/Content-src/launcher/` (sorted by name) becomes a texture, loaded
// lazily the first time it is asked for and kept alive here (FGCObject).
// `-AcLauncherArt=N` (1-based) pins image N for stills.
#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Styling/SlateBrush.h"

class UTexture2D;

class AUTOCRAFT_API FAcLauncherArt : public FGCObject
{
public:
	FAcLauncherArt();

	static FString Folder();
	int32 Num() const { return Files.Num(); }
	/// The pinned image's index (`-AcLauncherArt=N`), or INDEX_NONE.
	int32 Pinned() const { return PinnedIndex; }
	/// The first image of a run (random each launch unless pinned).
	int32 Start() const { return StartIndex; }

	/// The brush for image `I` (wrapped), loading it on first use; null if it will not load.
	const FSlateBrush* Brush(int32 I);
	/// Pixel size of image `I` (0 when it failed).
	FVector2f Size(int32 I);

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FAcLauncherArt"); }

private:
	struct FSlot
	{
		bool bTried = false;
		TObjectPtr<UTexture2D> Texture = nullptr;
		FSlateBrush Brush;
	};
	FSlot* Load(int32 I);

	TArray<FString> Files;
	TArray<FSlot> Slots;
	int32 PinnedIndex = INDEX_NONE;
	int32 StartIndex = 0;
};
