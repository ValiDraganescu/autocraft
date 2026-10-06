// `UAcMusicDeckSubsystem` (chunk D9, GAME-LAYER.md §2.11 "Faceplate"): the
// music deck's game side, as the Swift `GameController`/`HUD` drive
// `MusicDeck` (GameController.swift `huds.forEach { $0.showMusic(np) }`,
// HUD.tickMusic, GameController+Command `audio?.music.press(m)`).
//
// - Makes the deck (`SAcMusicDeck`) and mounts it on the console with
//   `SAcConsole::SetDeck` (again whenever the console widget is remade).
// - Every frame at the `Hud` stage: shows `UAcMusicPlayer::NowPlaying()`
//   when `Revision()` moved (or after a press, or once a second), and runs
//   the deck's clock on the player's clock; hands the hovered button's
//   tooltip to the console (`SAcConsole::SetDeckTip`).
// - A press on a button → `UAcMusicPlayer::Press`.
//
// Command line (shots and tests; renders have no music, as in Swift):
//   -AcMusicSample[=paused|down|ad|off]  show `MusicPlayer.sample()`: the
//       first own track with its tag's title and cover, 1:23 in, voted up
//       (`AUTOCRAFT_MUSIC` in Swift: paused, voted down, the first ad on
//       the station's card at 0:04, or no music); presses are only logged.
//   -AcDeckHover=down|previous|toggle|next|up  rest the pointer on a button.
//   -AcDeckClick=down|previous|toggle|next|up  click a button once the deck
//       is up, through the widget's own mouse handler.
// Console: `ac.MusicDeck [click|hover NAME]` (no argument: the state).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcMusicPlayer.h"

#include "AcMusicDeck.generated.h"

class SAcConsole;
class SAcMusicDeck;
class UTexture2D;
struct FAcFrame;

UCLASS()
class AUTOCRAFT_API UAcMusicDeckSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UAcMusicDeckSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	TSharedPtr<SAcMusicDeck> Deck() const { return DeckWidget; }

	/// "down", "previous" ("prev"), "toggle" ("pause"), "next", "up".
	static TOptional<EAcMusicCommand> CommandNamed(const FString& Name);
	/// `MusicPlayer.sample()`: "" (playing, voted up), "paused", "down", "ad".
	TOptional<FAcNowPlaying> Sample(const FString& Mode);

private:
	void OnFrame(const FAcFrame& Frame);
	void Mount();
	void OnCommand(EAcMusicCommand C);
	/// Click the deck's button `C` through its mouse handler (as the console
	/// hands it a click).
	bool Click(EAcMusicCommand C);

	TSharedPtr<SAcMusicDeck> DeckWidget;
	TWeakPtr<SAcConsole> MountedOn;
	FDelegateHandle FrameHandle;
	int32 ShownRevision = -1;
	double ShownAt = -1;
	bool bRefresh = true;
	bool bSample = false;
	FString SampleMode;
	TOptional<FAcNowPlaying> SampleNp;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> SampleCover;
	TOptional<EAcMusicCommand> PendingClick;
	TOptional<EAcMusicCommand> HeldHover;
	TOptional<EAcMusicCommand> TipShown;
	bool bTipShown = false;
	class IConsoleObject* Command = nullptr;
};
