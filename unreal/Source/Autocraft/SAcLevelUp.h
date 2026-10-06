// The leveling UI's widgets (chunk E9; see AcLeveling.h):
//   - `SAcLevelBanner`: `HUD.applyLevelUp`, "PROSPECTOR  LEVEL 4" on a plate
//     in the upper middle of the view (pops in at ×1.15 over 0.18 s, holds
//     2.4 s, fades 0.6 s; a new one replaces it);
//   - `SAcPickCards`: `PickCards.node` over the dashboard's centre, flat (not
//     on the warped faces), placed as `Console.showPicks` places it;
//   - `AcLevelPaint::Pilots`: `CommandMap.pilots`, the PILOTS rows.
// All laid out in Swift points, y up from the view's bottom, over the whole
// view (the HUD root's layer).
#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

#include "AcCab.h"
#include "AcLeveling.h"

class UTexture2D;
struct FAcCommandMapColumn;
struct FSlateBrush;

/// A plate drawn by `AcCabArt::Plate` into a texture, kept until its size changes.
struct FAcLevelPlate
{
	TStrongObjectPtr<UTexture2D> Texture;
	TSharedPtr<FSlateBrush> Brush;
	FVector2D For = FVector2D::ZeroVector;

	const FSlateBrush* Get(FVector2D Size, double TL, double TR, double BR, double BL, int32 Seed, const TCHAR* Name);
};

class AUTOCRAFT_API SAcLevelBanner : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAcLevelBanner) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	/// `levelUp(kind, level, hold)`: `Now` real seconds; `bHold` keeps it up, unanimated.
	void Show(ac::UnitKind Kind, int32 Level, double Now, bool bHold);
	bool IsShowing(double Now) const;

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	FString Title;
	FString Sub;
	double Since = -1e9;
	bool bHeld = false;
	bool bOn = false;
	mutable FAcLevelPlate Plate;
	TSharedPtr<FActiveTimerHandle> Timer;

	EActiveTimerReturnType Animate(double CurrentTime, float DeltaTime);
};

DECLARE_DELEGATE_OneParam(FAcOnPickCard, ac::Perk);

class AUTOCRAFT_API SAcPickCards : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAcPickCards) {}
		SLATE_EVENT(FAcOnPickCard, OnPick)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	/// `showPicks`: the offer (unset: none) over the dashboard of `Layout`,
	/// framed in `Accent`; `bKeys`: the keys that pick (driving).
	void Show(const TOptional<FAcPickOffer>& Offer, const FAcCabLayout& Layout, const FLinearColor& Accent, bool bKeys);
	const TOptional<FAcPickOffer>& Offer() const { return Shown; }
	/// The panel (Swift points, y up), zero with none.
	FAcRect Panel() const { return PanelRect; }
	/// The card under a point (Swift points, y up), as last laid out.
	TOptional<ac::Perk> CardAt(FVector2D P) const;
	/// Card `Slot`'s rectangle (Swift points, y up).
	TOptional<FAcRect> CardRect(int32 Slot) const;
	/// A press and release at a view point (points, y down) through the mouse path.
	bool Click(FVector2D ViewPoint);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		return OnMouseButtonDown(Geometry, Event);
	}

private:
	void Relayout();

	FAcOnPickCard OnPick;
	TOptional<FAcPickOffer> Shown;
	FAcCabLayout Cab;
	FLinearColor Accent = FLinearColor::White;
	bool bKeys = true;
	FAcRect PanelRect;
	double CardH = 66;
	TArray<TArray<FString>> Lines;
	TArray<TPair<FAcRect, ac::Perk>> Frames;
	TOptional<ac::Perk> Pressed;
	mutable FAcLevelPlate Plate;
};

namespace AcLevelPaint
{
	/// `PerkReach.mark(s, color)` centred on `At` (Swift points, y up).
	void Mark(FSlateWindowElementList& Out, const FGeometry& Geometry, int32& Layer, double ViewHeight, EAcPerkReach Reach,
		FVector2D At, double S, const FLinearColor& Color);
	/// `CommandMap.pilots`: the PILOTS rows from `Col.Y` down; adds a pick
	/// hit per button and records them in `OutButtons`.
	void Pilots(FAcCommandMapColumn& Col, const TArray<FAcDrivenRow>& Rows, TArray<TPair<FAcRect, ac::Perk>>& OutButtons);
}
