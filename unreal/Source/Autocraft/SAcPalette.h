// The playground's panels (chunk F5, see AcPlayground.h), the Slate side of
// Swift's `Playground` window (Playground.swift `buildWindow`): an AppKit
// source list down the left (180 points), a bar over the view (Side
// Blue|Red, Fog of war, Unlimited ore & MH, Clear…) and a status line under
// it. Here they are HUD chrome over the view, in Swift points:
//
//   SAcPalette        the column: Swift's bar on top of it (SIDE with the
//                     eight sides' swatches: Swift had Blue|Red; FOG OF
//                     WAR, UNLIMITED ORE & MH, CLEAR…), then the list: group
//                     headers (BUILDINGS, UNITS, RESOURCES, DOODADS) and a
//                     row per item; a row picks its item up, the one held
//                     clicked again lets go; the wheel scrolls the list.
//                     (The top of the view is the scoreboard's and the
//                     resource bar's, so Swift's bar moved into the column.)
//   SAcPaletteStatus  the status line, over the view's top left under the
//                     scoreboard row; it takes no clicks.
//   SAcPaletteConfirm Clear's question ("Clear the playground?"), modal.
//
// They take clicks for themselves only where they draw, so the view keeps
// the rest; they never take the keyboard (the keys stay the game's).
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

#include "AcPlayground.h"

class SAcPalette : public SLeafWidget
{
public:
	/// The list's width, points (Swift's scroll view).
	static constexpr float Width = 180;
	static constexpr float RowH = 22;
	static constexpr float TitleH = 34;
	/// The title and the controls over the list.
	static constexpr float HeaderH = 170;

	SLATE_BEGIN_ARGS(SAcPalette) {}
		SLATE_ARGUMENT(UAcPlaygroundSubsystem*, Playground)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/// Click `Item`'s row as the mouse would (scripted runs).
	void ClickItem(const FAcPaletteItem& Item);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, 100); }
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void OnMouseLeave(const FPointerEvent& Event) override;
	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override { return FReply::Handled(); }
	virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;

private:
	struct FRow
	{
		FString Title;
		TOptional<FAcPaletteItem> Item;
	};
	/// The controls: 0..7 a side, 10 fog, 11 unlimited, 12 clear.
	struct FControl
	{
		FBox2f Box;
		int32 Id = 0;
	};
	int32 RowAt(FVector2f Local) const;
	/// A control under `Local` (its id), else INDEX_NONE.
	int32 ControlAt(FVector2f Local) const;
	void Activate(int32 Row);
	void Press(int32 Control);
	float ContentHeight() const { return Rows.Num() * RowH + 8; }
	/// Rows' `Pressed`/`Hovered` are row indices; controls are offset by this.
	static constexpr int32 ControlBase = 1000;

	TWeakObjectPtr<UAcPlaygroundSubsystem> Playground;
	TArray<FRow> Rows;
	TArray<FControl> Controls;
	float Scroll = 0;
	mutable float ViewH = 0;
	int32 Hovered = INDEX_NONE;
	int32 Pressed = INDEX_NONE;
};

class SAcPaletteStatus : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAcPaletteStatus) {}
		SLATE_ARGUMENT(UAcPlaygroundSubsystem*, Playground)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(600, 24); }

private:
	TWeakObjectPtr<UAcPlaygroundSubsystem> Playground;
};

class SAcPaletteConfirm : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAcPaletteConfirm) {}
		SLATE_ARGUMENT(UAcPlaygroundSubsystem*, Playground)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(100, 100); }
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override { return FReply::Handled(); }
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override { return FReply::Handled(); }

private:
	/// 0 Clear, 1 Cancel.
	int32 ButtonAt(FVector2f Local) const;
	mutable FBox2f Buttons[2];
	TWeakObjectPtr<UAcPlaygroundSubsystem> Playground;
	int32 Hovered = INDEX_NONE;
	int32 Pressed = INDEX_NONE;
};
