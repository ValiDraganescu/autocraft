// `SAcMusicDeck`: the music player's faceplate (chunk D9, GAME-LAYER.md
// §2.11 "Faceplate"), the Slate port of the Swift `MusicDeck`
// (Sources/Autocraft/MusicDeck.swift). It fills the screen in the console's
// left instrument block (`FAcCabLayout::DeckScreen(Blocks[0])`, mounted with
// `SAcConsole::SetDeck`; the console draws "KSTR 88.7 STARDUST" over it on
// the plate): the track's cover (the station's card gets "KSTR" and
// "88.7  STARDUST" drawn on it), its title (an ad's as "AD · <title>" in
// amber, greyed while paused), elapsed time, a progress bar, the length, and
// five buttons along the foot: thumbs down, previous, pause or play, next,
// thumbs up (a vote lights its thumb green or red).
//
// Coordinates: the widget's box is the deck's screen at 1 Slate unit a
// Swift point (the console warps it with the face and hands it the mouse in
// that box; it must not rely on `HasMouseCapture`). Inside, it lays out as
// Swift does, in points y up from the screen's bottom left.
//
// It holds no player state: `UAcMusicDeckSubsystem` (AcMusicDeck.h) feeds
// it `Show(NowPlaying)` and the clock, and hears presses on `OnCommand`.
#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

#include "AcCab.h"
#include "AcMusicPlayer.h"

DECLARE_DELEGATE_OneParam(FAcOnMusicCommand, EAcMusicCommand);

/// A line of the deck's tooltip (`MusicDeck.tip`): text, colour, size.
struct FAcDeckTipLine
{
	FString Text;
	FLinearColor Color;
	double Size = 13;
};

class AUTOCRAFT_API SAcMusicDeck : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAcMusicDeck) {}
		SLATE_EVENT(FAcOnMusicCommand, OnCommand)
	SLATE_END_ARGS()

	/// `MusicDeck.order`: the buttons left to right.
	static constexpr EAcMusicCommand Order[5] = {EAcMusicCommand::Down, EAcMusicCommand::Previous, EAcMusicCommand::Toggle,
		EAcMusicCommand::Next, EAcMusicCommand::Up};

	void Construct(const FArguments& InArgs);

	/// Show the track (unset: no music). `Now`: the player's clock.
	void Show(const TOptional<FAcNowPlaying>& Np, double Now);
	/// Each frame: the bar and the time run on (the player's clock).
	void SetNow(double Now);
	const TOptional<FAcNowPlaying>& Shown() const { return Np; }

	/// The buttons' frames for a screen of `Size` points (y up from its
	/// bottom left), empty with no music or no room (`MusicDeck.rebuild`).
	static TArray<TPair<FAcRect, EAcMusicCommand>> Buttons(FVector2D Size, bool bMusic);
	/// The button under `P` (points of the screen, y up), if any.
	TOptional<EAcMusicCommand> CommandAt(FVector2D P) const;
	/// The button the pointer rests on, and its tooltip (`MusicDeck.tip`).
	TOptional<EAcMusicCommand> Hovered() const { return HoveredCommand; }
	TArray<FAcDeckTipLine> Tip(EAcMusicCommand C) const;
	/// Rest the pointer on a button (shots; unset: off the deck).
	void SetHovered(TOptional<EAcMusicCommand> C) { HoveredCommand = C; }
	/// Press a button as a click on it would.
	void Press(EAcMusicCommand C);

	/// `MusicDeck.clock`: "m:ss".
	static FString Clock(double Seconds);
	/// The SF Symbol drawn on a button (`MusicDeck.button`).
	static FString SymbolName(EAcMusicCommand C, bool bVoted, bool bPaused);
	/// The title as drawn, shrunk to `Room` points (to 70 %), then cut short
	/// with an ellipsis (`MusicDeck.fit`): the text and its size.
	static TPair<FString, double> Fit(const FString& Text, double Size, double Room);

	// SWidget
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(120, 160); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void OnMouseLeave(const FPointerEvent& Event) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;

private:
	/// A pointer event in screen points, y up.
	FVector2D PointOf(const FGeometry& Geometry, const FPointerEvent& Event) const;
	const FSlateBrush* CoverBrush() const;

	FAcOnMusicCommand OnCommand;
	TOptional<FAcNowPlaying> Np;
	double Now = 0;
	int32 ShownSecond = -1;
	/// The screen's size in points (from the last paint or event).
	mutable FVector2D Size = FVector2D::ZeroVector;
	TOptional<EAcMusicCommand> HoveredCommand;
	mutable TSharedPtr<FSlateBrush> Cover;
};
