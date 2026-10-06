// `UAcPointer`: pointing in the top-down view (GAME-LAYER.md §2.3, chunk
// D4), on `AAcPlayerController`. The port of `GameController+Hover.swift`
// (`hovers`, `updateHover`, `hover`, `cursor`, `drive(at:)`) and the world
// half of `GameController+Command.swift` (`click` :124, `select` :145,
// `updateSelection` :160).
//
// Every frame (the sim frame's `Hud` stage, after the renderer placed this
// frame's units): pick what the pointer rests on (`AcPick` against
// `Shown()`, so nothing in the fog is hovered), show its cue in the world
// (`AAcPointerCues`), its tip (`SAcTip`) and set the cursor; ring the
// selected building, and let go of it when it is gone, changed hands, or a
// drive started.
//
// Clicks: Slate gets them first, so a click on a HUD widget (console, card,
// music deck, command map) never reaches the world. A left press released
// within 4 points of where it went down (the RTS pawn's drag threshold) is a
// click: `ClickHandlers` first (in order; the playground palette, F5),
// then the world: a unit the player can drive → `OnTakeOver` (E2), the
// player's own building → select it, anything else → select nothing.
// F over the view drives the unit under the pointer (`drive(at:)`).
//
// Cursors (`GameCursor.swift`): four hardware cursors baked from the Swift
// drawing (`Tools/cursors/bake_cursors.sh` → `Content/UI/Cursors/*.tiff`,
// 1x and 2x): normal = `EMouseCursor::Default`, select = `Hand`, enemy =
// `Crosshairs`, drive = `EyeDropper`. A HUD widget that wants the "select"
// arrow over a button sets its own `Cursor(EMouseCursor::Hand)`.
//
// For other chunks:
//   D2/D5 (console, card) `Select()` passes the selection on to
//                 `UAcConsoleSubsystem::Select`; `Selected()`, `OnSelectionChanged`.
//   D6 (bars)     the selected and hovered ids → `AAcLifeBars::SetFocus` each frame.
//   D8 (map)      `SetBlocked(true)` while the command map is open.
//   E1 (rays)     `Shapes().Refine` (see AcPick.h).
//   E2 (pilot)    bind `OnTakeOver`; `SetThirdPerson` for the tip's text;
//                 hover and selection stop while `sim.pilot` is set.
//   F5 (playground) `ClickHandlers`, `HoverSuppressed`.
//
// Scripted pointing (headless shots and tests; no desktop automation):
//   -AcPointAt="X,Y"     the pointer held at a view point (points, y down)
//   -AcHover=NAME        rest on the first `prospector`, `citadel`,
//                        `enemy:ranger`, `ally:citadel`... (Swift `windowshot --hover`),
//                        following it as it moves
//   -AcSelect=KIND       select the player's first building of that kind
//                        (`windowshot --select`)
//   -AcClick=KIND        click its roof through the click path (`--click`)
//   ac.PointAt X Y | off, ac.Click [X Y], ac.Hover NAME | off, ac.Select KIND | none
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "AcPick.h"
#include "AcPointerCues.h"

#include "AcPointer.generated.h"

class SAcTip;
class UAcSimSubsystem;
struct FAcFrame;

/// The cursor kinds (`GameCursor.Kind`).
enum class EAcCursor : uint8 { Normal, Select, Enemy, Drive };

DECLARE_MULTICAST_DELEGATE(FAcSelectionChanged);
DECLARE_DELEGATE_OneParam(FAcTakeOver, int64 /*UnitId*/);

UCLASS()
class AUTOCRAFT_API UAcPointer : public UActorComponent
{
	GENERATED_BODY()

public:
	UAcPointer();

	/// The local player's pointer (null when none).
	static UAcPointer* Get(const UObject* WorldContext);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/// What the pointer rests on now.
	const TOptional<FAcHover>& Hovered() const { return Hover; }
	EAcCursor Cursor() const { return CursorKind; }

	/// The selected building (one of the player's own).
	TOptional<int64> Selected() const { return Selection; }
	void Select(TOptional<int64> Id);
	FAcSelectionChanged OnSelectionChanged;

	/// A click at a view point (points, y down), through the whole routing.
	void Click(FVector2D ViewPoint);
	/// F: drive the unit under a view point, if it can be driven.
	void DriveAt(FVector2D ViewPoint);
	/// The unit or building under a view point.
	TOptional<FAcHover> PickAt(FVector2D ViewPoint) const;

	/// E2 takes over a unit (unbound: logged only).
	FAcTakeOver OnTakeOver;
	void SetThirdPerson(bool bThird) { bThirdPerson = bThird; }
	/// The command map is open: no hover, no world clicks.
	void SetBlocked(bool bInBlocked) { bBlocked = bInBlocked; }
	/// Tried in order before the world; true = handled.
	TArray<TFunction<bool(FVector2D ViewPoint)>> ClickHandlers;
	/// True while something else owns the pointer (the playground's palette).
	TFunction<bool()> HoverSuppressed;
	/// Extra HUD areas that cover the world (besides hit-testable Slate widgets).
	TFunction<bool(FVector2D ViewPoint)> HudCovers;
	/// The pick's shapes (E1 sets `Refine`).
	FAcPickShapes& Shapes() { return PickShapes; }

	/// Hold the pointer at a view point (headless shots); unset: the mouse.
	void SetSimulatedPoint(TOptional<FVector2D> ViewPoint);
	/// Rest on `Name` (as `-AcHover`), followed every frame; empty: stop.
	bool HoverOn(const FString& Name);
	/// The player's first building of a kind (`citadel`).
	TOptional<int64> FirstOwnBuilding(const FString& Kind) const;
	/// Where a click on a building's roof lands (view point).
	TOptional<FVector2D> RoofPoint(int64 Id) const;

private:
	void OnFrame(const FAcFrame& Frame);
	void OnGameStarted(UAcSimSubsystem& Sim);
	/// The pointer now, in view points (false: off the view).
	bool PointerPoint(FVector2D& Out) const;
	/// The pointer's absolute (desktop) position, for the tip.
	bool AbsolutePoint(FVector2D ViewPoint, FVector2D& Out) const;
	bool Covered(FVector2D ViewPoint) const;
	/// -AcHover: aim the held pointer at what is followed.
	bool Follow();
	void UpdateHover(double Time);
	void ShowHover(const TOptional<FAcHover>& H, FVector2D At, double Time);
	EAcCursor CursorFor(const TOptional<FAcHover>& H) const;
	void ApplyCursor(EAcCursor Kind);
	void UpdateSelection(double Time);
	void PollInput();
	void StageCommandLine();
	void InstallCursors();
	void EnsureTip();
	bool Piloting() const;
	double UnitY(const ac::Unit& U) const;
	double GroundY(ac::Vec2 P) const;

	FAcPickShapes PickShapes;
	TOptional<FAcHover> Hover;
	TOptional<int64> Selection;
	EAcCursor CursorKind = EAcCursor::Normal;
	bool bThirdPerson = false;
	bool bBlocked = false;

	TOptional<FVector2D> Simulated;
	/// -AcHover / ac.Hover: what to follow.
	FString FollowName;
	TOptional<int64> FollowId;

	// The left button (a click is a press that did not become a drag).
	bool bPressed = false;
	bool bDragged = false;
	FVector2D PressAt = FVector2D::ZeroVector;

	int32 Frames = 0;
	bool bStaged = false;
	FString LastLog;

	TWeakObjectPtr<class AAcPointerCues> Cues;
	TSharedPtr<SAcTip> Tip;
	FDelegateHandle FrameHandle, StartedHandle;
};
