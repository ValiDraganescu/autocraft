// The leveling UI (chunk E9, GAME-LAYER.md §2.13, docs/leveling.md "On
// screen"): the port of `PickCards.swift` (`LevelInfo`, `PickOffer`,
// `PerkReach`, `PickCards.wrap`), `GameController+Leveling.swift`
// (`pickPerk`, `pickKey`, `leveledUp`, `pilotRows`) and the level-up banner
// of `HUD.applyLevelUp`.
//
// `UAcLevelingSubsystem` (world subsystem, `Hud` frame stage):
//   - the banner (`SAcLevelBanner`, layer `BannerLayer`): one for each
//     `LeveledUp` event, "PROSPECTOR  LEVEL 4"; the fanfare is C7's
//     (`FAcAudioRules::LevelUp` on the same event), not played here;
//   - the pick cards (`SAcPickCards`, layer `CardsLayer`) over the
//     dashboard's centre while driving (not in the top-down look): the
//     driven kind's lowest waiting level, its two picks; Z and X (the pilot
//     pawn's `OnKey`, chained), or Option (`Pointable`, chained) and a click;
//   - the PILOTS rows on the command map (D8): `OnInfo` reserves
//     `PilotsRoom`, `PilotsPainter` draws them, `OnPick` takes a pick.
//   A pick is an order (`Command::Pick`); a kept one latches (`Picked`, C7),
//   a refused one notes why on the cockpit (E6 `Note`).
//   The XP bar and the LV badge beside the driven unit's name are E6's
//   (`AcPilotText::Level` → the console card).
//
// Command line (headless stills and checks; `windowshot` has the same):
//   -AcLevel=prospector:4[,ranger:3]  those kinds driven this game at that
//                          level, a third of the way to the next, every pick
//                          from level 2 up waiting (`--level`)
//   -AcPicks=N             the first N of each kind's picks made (`--picks`)
//   -AcLevelUp             the banner held, for the driven kind or the
//                          first -AcLevel one (`--levelup`)
//   -AcEarn=XP[@S]         XP for the driven unit S real seconds (default 1)
//                          after a drive starts (`sim.earn`: the real path,
//                          event, banner and fanfare cue)
//   -AcPickKey=z|x[@S]     that key once cards are up (S seconds later)
//   -AcPickClick=1|2       a click on that card through the widget
//   -AcMapPickPerk=N       a click on the command map's Nth pick button
//   ac.Earn XP, ac.Pick z|x|PERK, ac.LevelUp [KIND LEVEL] (console)
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcCab.h"
#include "AcConsoleInfo.h"
#include "Types.h"

#include "AcLeveling.generated.h"

class SAcLevelBanner;
class SAcPickCards;
struct FAcCommandMapColumn;
struct FAcCommandMapInfo;
struct FAcFrame;

namespace ac { class Simulation; }

/// `PickOffer`: a kind's choice waiting to be picked: the lowest pending
/// level's two perks, and how many levels wait in all.
struct AUTOCRAFT_API FAcPickOffer
{
	ac::UnitKind Kind = ac::UnitKind::prospector;
	int32 Level = 2;
	TArray<ac::Perk> Perks;
	int32 Waiting = 0;

	/// Unset with nothing waiting for `Kind`.
	static TOptional<FAcPickOffer> Of(const ac::PilotRecord& R, ac::UnitKind Kind);
	/// The keys that pick the first and the second card while driving.
	static const TCHAR* Key(int32 Slot) { return Slot == 0 ? TEXT("Z") : TEXT("X"); }
	bool operator==(const FAcPickOffer& B) const
	{
		return Kind == B.Kind && Level == B.Level && Perks == B.Perks && Waiting == B.Waiting;
	}
};

/// `PerkReach`: who a pick helps, for its card's mark.
enum class EAcPerkReach : uint8 { Driven, Nearby, Team };

/// A kind driven this game, for the command map (`CommandMapInfo.Driven`).
struct AUTOCRAFT_API FAcDrivenRow
{
	ac::UnitKind Kind = ac::UnitKind::prospector;
	FAcLevelInfo Level;
	TArray<ac::Perk> Picks;
	TOptional<FAcPickOffer> Offer;
};

struct AUTOCRAFT_API AcLeveling
{
	/// `PerkReach(perk)`: from what it does, else from its effect line.
	static EAcPerkReach Reach(ac::Perk Perk);
	/// "YOU", "NEARBY", "TEAM".
	static const TCHAR* Tag(EAcPerkReach R);
	static FString Title(ac::Perk Perk);
	static FString Effect(ac::Perk Perk);
	static FString Name(ac::Perk Perk);
	static TOptional<ac::Perk> ParsePerk(const FString& Name);
	/// `PickCards.wrap`: `Text` broken at spaces into lines no wider than
	/// `Width` points at `Size`, at most `Max` (the last cut with "…").
	static TArray<FString> Wrap(const FString& Text, double Size, double Width, int32 Max);
	/// `pilotRows`: each kind driven this game.
	static TArray<FAcDrivenRow> Rows(const ac::Simulation& Sim);
	/// `CommandMap.pilotsHeight`.
	static double PilotsHeight(const TArray<FAcDrivenRow>& Rows);
	/// A choice's line and its two buttons, under its kind's row (`pickRoom`).
	static constexpr double PickRoom = 22 + 2 * 38;
	/// The banner's lines (`applyLevelUp`).
	static FString BannerTitle(ac::UnitKind Kind, int32 Level);
	static FString BannerSub(int32 Level);
	/// `-AcLevel` / `windowshot --level`: those kinds at those levels, a
	/// third of the way on, then the first `Picks` of each made. The kinds,
	/// in order.
	static TArray<ac::UnitKind> Stage(ac::Simulation& Sim, const FString& Spec, int32 Picks);
};

UCLASS()
class AUTOCRAFT_API UAcLevelingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/// The cards over the console (20) and the bar (30), under the pilot
	/// overlay (35); the banner over the overlay and the victory banner (40).
	static constexpr int32 CardsLayer = 34;
	static constexpr int32 BannerLayer = 41;

	static UAcLevelingSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/// `pickPerk`: keep `Perk` (an order like any other). False, with the
	/// reason noted while driving, when it is not on offer now.
	bool Pick(ac::Perk Perk);
	/// `pickKey`: Z or X while driving. False with nothing waiting.
	bool PickKey(const FString& Key);
	/// The driven kind's offer (unset: not driving, or nothing waiting).
	TOptional<FAcPickOffer> DrivenOffer() const;
	/// `leveledUp`: the banner (and the log).
	void LevelUp(ac::UnitKind Kind, int32 Level, bool bHold = false);
	/// Dev: XP for the driven unit (`sim.earn`); false when not driving.
	bool Earn(double Xp);
	/// The command map's pick buttons as last drawn (Swift points, y up), for scripted clicks.
	const TArray<TPair<FAcRect, ac::Perk>>& MapButtons() const { return MapPicks; }
	TSharedPtr<SAcPickCards> Cards() const { return CardsWidget; }

private:
	void OnFrame(const FAcFrame& Frame);
	void OnMapInfo(FAcCommandMapInfo& Info);
	void PaintPilots(FAcCommandMapColumn& Col);
	bool Mount();
	void HookPawn();
	void Script(const class UAcSimSubsystem& Sim);

	TSharedPtr<SAcPickCards> CardsWidget;
	TSharedPtr<SAcLevelBanner> BannerWidget;
	FDelegateHandle FrameHandle;
	FDelegateHandle MapInfoHandle;
	FDelegateHandle MapPickHandle;
	TWeakObjectPtr<class AAcPilotPawn> HookedPawn;
	/// The rows the command map shows (set with its info).
	TArray<FAcDrivenRow> MapRows;
	TArray<TPair<FAcRect, ac::Perk>> MapPicks;
	double MapViewHeight = 0;

	// Scripted (dev flags).
	FString LevelSpec;
	int32 LevelPicks = 0;
	bool bStaged = false;
	bool bHoldBanner = false;
	TArray<ac::UnitKind> Leveled;
	TOptional<TPair<ac::UnitKind, int32>> HeldBanner;
	double EarnXp = 0;
	double EarnAfter = 1;
	double DriveStarted = -1;
	FString PickKeyWanted;
	/// Frames the cards are up before -AcPickKey's key (8; "z@S": S seconds).
	int32 PickAfterFrames = 8;
	int32 PickClickWanted = 0;
	int32 MapPickWanted = 0;
	int32 CardFrames = 0;
	int32 MapFrames = 0;
};
