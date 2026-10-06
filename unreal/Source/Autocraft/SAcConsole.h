// `SAcConsole`: the console along the bottom of the view (chunk D2; GAME-
// LAYER.md §2.4 "Console"), the Slate counterpart of the Swift `Console`
// (Sources/Autocraft/Console.swift): the dashboard (`Cab`) with three faces,
// the minimap's screen on the left, the selection in the middle (hologram
// portrait, hit points, title, status, work bar, queue), the 5 × 3 command
// card on the right; the note over the card and the view switch.
//
// The faces are drawn flat into a strip and warped into the dashboard as
// seen (chunk D3, AcConsoleWarp.h); the looks `.rts`, `.cockpit` and `.cab`
// (`SetStyle`; the cab adds the frame round the view and its two lamps).
// Everything is laid out in Swift points (the HUD root scales to pixels)
// with `FAcCabLayout`.
//
// Slots for other chunks:
//   - the minimap (D7): `SetMinimap(widget)`, placed over `Layout().Minimap`;
//     its size in points comes from `SetMinimapSize` (the map's aspect);
//   - the music player (D9): `SetDeck(widget)`, placed over the deck's
//     screen (`FAcCabLayout::DeckScreen(Layout().Blocks[0])`);
//   Both are drawn warped, off screen, so Slate never hit-tests them: the
//   console hands them its mouse events itself (down, up, move, wheel,
//   enter, leave, cursor), the position mapped back through the face (their
//   geometry is their own box, at 1 Slate unit a Swift point). A drag that
//   starts on one keeps going to it (the console holds the capture; a
//   widget's own `CaptureMouse` is turned into that). They get no
//   keyboard focus. `MinimapPoint` is Swift's `minimapPoint(_:pad:)`;
//   - the command card (D5): `FAcCardInfo::Buttons` are drawn here as Swift
//     draws them (faces, icons, rims, keys) with their tooltip; presses come
//     out through `OnCardButton` (or ask `ButtonAt`), the view switch through
//     `OnViewSwitch`. Hotkeys and the press logic are D5's.
#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

#include "AcCab.h"
#include "AcConsoleInfo.h"
#include "SAcMusicDeck.h"

class SCanvas;
class SBox;
class UTexture2D;
struct FSlateBrush;

DECLARE_DELEGATE_OneParam(FAcOnCardButton, int32 /* index into Buttons */);
DECLARE_DELEGATE(FAcOnViewSwitch);

class AUTOCRAFT_API SAcConsole : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAcConsole) {}
		SLATE_EVENT(FAcOnCardButton, OnCardButton)
		SLATE_EVENT(FAcOnViewSwitch, OnViewSwitch)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAcConsole() override;

	/// Show a selection (unset: nothing selected, the wells stand empty).
	/// `Now`: real seconds (the note's fade, the tips' turns).
	void Show(const TOptional<FAcCardInfo>& Info, double Now);
	/// Keep the note up instead of fading it (shots).
	void HoldNote(bool bHold) { bHoldNote = bHold; }
	/// Which view the switch shows lit (`setView(third:)`).
	void SetView(bool bThird);
	/// What a click on the view switch does (the pilot's V, E3).
	void SetOnViewSwitch(FAcOnViewSwitch Handler) { OnViewSwitch = MoveTemp(Handler); }
	/// The minimap's own size in points (sets the map well's width).
	void SetMinimapSize(FVector2D Size);
	void SetMinimap(TSharedPtr<SWidget> Widget);
	void SetDeck(TSharedPtr<SWidget> Widget);
	/// The music deck's tooltip (D9, `Console.showMusicTip`): over the
	/// dashboard from the deck's block; a card button's tip goes first.
	void SetDeckTip(TArray<FAcDeckTipLine> Lines);
	/// The look (`Console.setStyle`): top-down, driving, a machine's cab.
	void SetStyle(EAcConsoleStyle InStyle);
	EAcConsoleStyle Style() const { return Look; }
	/// What the cab's lamps say (`Console.setLamps`; only the cab has them).
	void SetLamps(EAcCabLamps InLamps);
	/// Who hears card presses after construction (D5, `UAcCommandsSubsystem`).
	void SetOnCardButton(FAcOnCardButton In) { OnCardButton = MoveTemp(In); }

	/// The layout for the current view size.
	const FAcCabLayout& Layout() const { return Lay; }
	/// The console's height in the middle of the view (`Console.cover`), or
	/// 0 before the first layout.
	double Cover() const { return bLaidOut ? Lay.Cover() : 0.0; }

	// Hit tests, in Slate points of this widget (y down).
	/// The card button under `P`: an index into the shown `Buttons`.
	TOptional<int32> ButtonAt(FVector2D P) const;
	bool ViewSwitchAt(FVector2D P) const;
	/// The console covers `P` (a click there is not on the map).
	bool Covers(FVector2D P) const;
	/// The pointer is here (unset: off the view): the card's tooltip.
	void Hover(TOptional<FVector2D> P);
	/// Where `P` (Slate points of this widget) is on the dashboard's faces
	/// laid out flat (Swift points, y up), and which face (0 left, 1 centre,
	/// 2 right); unset off the faces.
	TOptional<FVector2D> FlatAt(FVector2D P, int32* Face = nullptr) const;
	/// `Console.minimapPoint(_:pad:)`: `P` on the minimap in its own points
	/// (y up from its bottom left, unscaled), seen through the left face;
	/// unset more than `Pad` off it.
	TOptional<FVector2D> MinimapPoint(FVector2D P, double Pad) const;

	// SWidget
	virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void OnMouseLeave(const FPointerEvent& Event) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& Event) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;

private:
	struct FArt
	{
		TStrongObjectPtr<UTexture2D> Texture;
		TSharedPtr<FSlateBrush> Brush;
		const FSlateBrush* Get() const { return Brush.Get(); }
	};

	void Relayout(FVector2D Size);
	/// The faces laid out flat (the strip the warp draws): face art, the
	/// selection, the card. In the strip's geometry.
	int32 PaintFaces(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer) const;
	/// The cab's frame and lamps, or the dashboard's top.
	void PaintDash(class FAcConsolePaint& P) const;
	void MakeFrame();

	/// A widget the console hands mouse events to (minimap, music player).
	struct FForward
	{
		TSharedPtr<SWidget> Widget;
		FAcRect Rect;
		int32 Face = -1;
	};
	/// The forwarded widget under `P` (Slate points), and `P` in its box.
	bool ForwardAt(FVector2D P, FForward& Out, FVector2D& Local) const;
	/// `P` in `F`'s box (even off it: a drag runs on).
	FVector2D ForwardLocal(const FForward& F, FVector2D P) const;
	FReply Forward(const FForward& F, const FGeometry& Geometry, const FPointerEvent& Event,
		FReply (SWidget::*Handler)(const FGeometry&, const FPointerEvent&));
	void ForwardHover(const TOptional<FForward>& Over, const FGeometry& Geometry, const FPointerEvent& Event);
	void MakeArt(FArt& Art, const struct FAcArtImage& Image, const TCHAR* Name);
	void PaintBase(class FAcConsolePaint& P) const;
	void PaintCenter(class FAcConsolePaint& P, const FAcCardInfo& Info) const;
	void PaintCard(class FAcConsolePaint& P) const;
	void PaintOver(class FAcConsolePaint& P) const;
	void PaintKeyed(class FAcConsolePaint& P, const FString& Text, double Size, const FLinearColor& Key,
		const FLinearColor& Rest, FVector2D At) const;
	FLinearColor Accent() const;
	/// An icon drawn added (`blendMode = .add`) through M_AcUiAdditive.
	const FSlateBrush* GlowBrush(const FString& Icon) const;
	bool IsNoteShown() const;

	FAcOnCardButton OnCardButton;
	FAcOnViewSwitch OnViewSwitch;

	FAcCabLayout Lay;
	bool bLaidOut = false;
	FVector2D LaidOutSize = FVector2D::ZeroVector;
	FVector2D PendingSize = FVector2D::ZeroVector;
	double PendingSince = 0;
	FVector2D MinimapSize = FVector2D(220, 170);

	FArt Dash, Faces[3], ButtonOn, ButtonOff, Scan, SwitchPlate;
	/// The cab's frame in its bands round the window (made for the `.cab` look).
	TArray<TPair<FAcRect, FArt>> Frame;
	bool bFrameMade = false;
	/// The layout was made warped (`ac.ConsoleWarp`).
	bool bLaidWarped = true;
	EAcConsoleStyle Look = EAcConsoleStyle::Rts;
	EAcCabLamps Lamps = EAcCabLamps::Idle;
	double LampsSince = 0;

	TSharedPtr<class SAcConsoleFaces> Strip;
	TSharedPtr<class FAcConsoleWarp> Warp;
	/// The widget a drag runs to, and the one under the pointer.
	TOptional<FForward> Captured;
	TOptional<FForward> Hovering;

	TOptional<FAcCardInfo> Info;
	bool bThird = false;
	TOptional<int32> Hovered;
	TArray<FAcDeckTipLine> DeckTip;
	double Now = 0;
	TOptional<FString> ShownNote;
	bool bHoldNote = false;
	double NoteSince = 0;
	TOptional<double> TipsSince;

	mutable TMap<FString, TStrongObjectPtr<class UMaterialInstanceDynamic>> GlowMaterials;
	mutable TMap<FString, TSharedPtr<FSlateBrush>> GlowBrushes;

	TSharedPtr<SCanvas> Canvas;
	TSharedPtr<SBox> MinimapBox;
	TSharedPtr<SBox> DeckBox;
};

/// The armoured frame round the whole view in the top-down view and third
/// person (`Cab.viewFrame`, HUD.attachViewFrame): under the rest of the HUD,
/// at `AcHudLayer::ViewFrame`. Redrawn when the view's size changes.
class AUTOCRAFT_API SAcViewFrame : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAcViewFrame) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	TStrongObjectPtr<UTexture2D> Texture;
	TSharedPtr<FSlateBrush> Brush;
	FVector2D DrawnSize = FVector2D::ZeroVector;
	FVector2D PendingSize = FVector2D::ZeroVector;
	double PendingSince = 0;
};
