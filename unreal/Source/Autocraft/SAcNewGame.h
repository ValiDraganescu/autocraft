// `SAcNewGame` (chunk F1, GAME-LAYER.md §2.14): the new-game dialog, the
// Unreal side of Swift's `MapPicker` (Sources/Autocraft/MapPicker.swift)
// with its `MapPreview` picture (MapPreview.swift), widened for teams:
//
//   NEW GAME                       "Pick a map and how big it is."
//   MAP      Highlands | Open Field | The Ridge | Badlands
//   SIZE     Small (96 × 64) | Medium | Large | Huge
//   PLAYERS  2 | 4 | 8
//   TEAMS    1 v 1 / 2 v 2, Free for all / 4 v 4, 2 v 2 v 2 v 2, Free for all
//   AI  On | Off      FOG OF WAR  On | Off
//   [the map from above: the minimap's ground (D7 `AcMinimapBake::Terrain`
//    at 2 px a cell), ramps in white, bases in yellow, starts in the
//    players' colours, as `MapPreview.render`]
//   the style's blurb; "W × D cells · N bases · ..."
//   [IMPORT SWIFT GAME]                         [CANCEL] [START]
//
// Drawn by hand in Swift points (the HUD root's units) on a dimmed screen;
// it is modal: every click and Return/Escape stays here. Previews are made
// on a worker thread and cached for the process (a Huge 8-player map takes
// a moment). The panel shrinks to fit a small window.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

#include "Async/Future.h"
#include "UObject/StrongObjectPtr.h"

#include "WindowMaps.h"

class UTexture2D;

/// What the dialog picks.
struct FAcNewGameChoice
{
	ac::MapChoice Map{ac::MapStyle::highlands, ac::MapSize::medium, 2};
	/// `1v1`, `2v2`, `4v4`, `2v2v2v2` or `ffa` (AcSaves::ParseTeams).
	FString Teams = TEXT("1v1");
	bool bAI = true;
	bool bFog = true;
};

DECLARE_DELEGATE_OneParam(FAcOnNewGameStart, const FAcNewGameChoice&);

class AUTOCRAFT_API SAcNewGame : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAcNewGame) {}
		SLATE_ARGUMENT(FAcNewGameChoice, Initial)
		/// Under the title (Swift's `informativeText`).
		SLATE_ARGUMENT(FString, Message)
		/// Shown on the import button's line (empty: no button).
		SLATE_ARGUMENT(FString, ImportNote)
		SLATE_EVENT(FAcOnNewGameStart, OnStart)
		SLATE_EVENT(FSimpleDelegate, OnCancel)
		SLATE_EVENT(FSimpleDelegate, OnImport)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAcNewGame() override;

	const FAcNewGameChoice& Choice() const { return Current; }
	/// Change the choice (scripted shots, `ac.NewGameDialog MAP TEAMS`).
	void SetChoice(const FAcNewGameChoice& Choice);
	/// True once the preview of the current choice is drawn.
	bool PreviewReady() const;
	/// Press Start / Cancel / Import as the buttons do.
	void Start();
	void Cancel();
	void Import();

	/// The team options for a player count (`1v1`; `2v2`, `ffa`; `4v4`, `2v2v2v2`, `ffa`).
	static TArray<FString> TeamOptions(int64 Players);
	/// "2 v 2", "Free for all".
	static FString TeamTitle(const FString& Teams);

	// SWidget
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(100, 100); }
	virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override { return FReply::Handled(); }
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override { return FReply::Handled(); }
	virtual void OnMouseLeave(const FPointerEvent& Event) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;

	/// A preview picture and what goes over it (made off the game thread).
	struct FPreview;

private:
	/// Something clickable: a row's option or a button.
	struct FTarget
	{
		FBox2f Box;
		/// 0 map, 1 size, 2 players, 3 teams, 4 AI, 5 fog, 10 start, 11 cancel, 12 import.
		int32 Row = 0;
		int32 Index = 0;
		FString Label;
		bool bSelected = false;
	};

	void Layout(FVector2f ViewSize) const;
	int32 TargetAt(FVector2f LocalPoint) const;
	void Activate(const FTarget& T);
	void RequestPreview();
	FString FactsLine() const;

	FAcNewGameChoice Current;
	FString Message;
	FString ImportNote;
	FAcOnNewGameStart OnStartDelegate;
	FSimpleDelegate OnCancelDelegate;
	FSimpleDelegate OnImportDelegate;

	/// Laid out at paint (panel points), `PanelScale` and `PanelOrigin` from the view.
	mutable TArray<FTarget> Targets;
	mutable FBox2f PreviewBox;
	mutable float PanelScale = 1.0f;
	mutable FVector2f PanelOrigin = FVector2f::ZeroVector;
	mutable FVector2f PanelSize = FVector2f::ZeroVector;
	mutable FVector2f LaidOutFor = FVector2f(-1, -1);

	int32 Hovered = INDEX_NONE;
	int32 Pressed = INDEX_NONE;

	/// The preview on screen and the one being made.
	TSharedPtr<FPreview> Shown;
	FString ShownKey;
	TSharedPtr<TFuture<TSharedPtr<FPreview>>> Pending;
	FString PendingKey;
	TStrongObjectPtr<UTexture2D> PreviewTexture;
	TStrongObjectPtr<UTexture2D> DiscTexture;
	TSharedPtr<struct FSlateBrush> PreviewBrush;
	TSharedPtr<struct FSlateBrush> DiscBrush;
};
