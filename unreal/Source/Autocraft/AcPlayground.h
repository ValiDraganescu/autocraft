// The playground (chunk F5, GAME-LAYER.md §2.15): `autocraft models` in
// Swift (`Playground.swift`, `GameController+Playground.swift`, core
// `Simulation+Playground`). Flat open ground with nothing on it, and every
// building, unit, resource and doodad in a palette down the left: pick one
// and click the ground to put it down, for any of the eight sides, as many
// as you like (R turns it, Esc or a right click lets go of it). Then play
// with them as in a game: click a unit to drive it, a building for its
// card; Delete (or Backspace) takes away what is under the pointer. No AI
// (it has no bases to plan from), no fog (a switch puts it back: the sim is
// built again), every side's ore and MH topped up to 5000 (a switch stops
// it). What stands on it is kept between launches (`playground.json` in the
// Unreal sessions); Clear (⌘N) starts it over.
//
// Pieces:
//   `UAcPlaygroundSubsystem`  the palette's state and rules (this file):
//       hold/turn/drop, where an item goes and why not (`paletteSpot`),
//       place, remove, top up, the status line, keys; hooks into D4's
//       `UAcPointer` (`ClickHandlers` first: a click puts the held item
//       down; `HoverSuppressed` while it holds one) and F1's flow (⌘N is
//       Clear here, ⌘R does nothing).
//   E8's `AAcHologram` (AcHologram.h) shows the hologram under the
//       pointer, green where it fits, red where not (`showGhost(key:)`).
//   `SAcPalette`, `SAcPaletteStatus`, `SAcPaletteConfirm` (SAcPalette.h): the
//       column (side, fog, unlimited, Clear, then the list), the status
//       line, and Clear's question.
//
// Run: `-AcPlayground` (with `-AcNew` to start it over). Scripted (headless
// shots and checks, through the same paths as the mouse; no desktop
// automation):
//   -AcPlaygroundLayout        put down Swift's `windowshot --playground`
//                              layout (`layPlayground`) by palette row
//                              clicks and view clicks
//   -AcPlaygroundHold=NAME     hold a palette item (catalog name: citadel,
//                              ranger, ore, well, rockSpire...) with the
//                              pointer over -AcPlaygroundAt (default 0,-2;
//                              `windowshot --holding`)
//   -AcPlaygroundAt="X,Y"      that ground point
//   -AcPlaygroundSide=N        the side things go to (0..7)
//   -AcPlaygroundTest          place, remove, sides, fog, unlimited, clear:
//                              logs `playgroundtest: ... ok|FAILED`, quits
//   ac.Palette NAME|none, ac.PaletteSide N, ac.PlaygroundPut NAME X Y [SIDE [TURN]],
//   ac.PlaygroundDelete X Y, ac.PlaygroundFog 0|1, ac.PlaygroundUnlimited 0|1,
//   ac.PlaygroundClear
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Types.h"

#include <optional>
#include <string>

#include "AcPlayground.generated.h"

class IInputProcessor;
class SAcPalette;
class SAcPaletteStatus;
class SAcPaletteConfirm;
class UAcSimSubsystem;
struct FAcFrame;

/// What the palette holds out to put down (`PlaygroundItem`).
struct AUTOCRAFT_API FAcPaletteItem
{
	enum class EType : uint8 { Structure, Unit, Ore, Well, Doodad };
	EType Type = EType::Ore;
	ac::StructureKind Structure = ac::StructureKind::citadel;
	ac::UnitKind Unit = ac::UnitKind::prospector;
	ac::Doodad::Kind Doodad = ac::Doodad::Kind::rock;

	static FAcPaletteItem OfStructure(ac::StructureKind K);
	static FAcPaletteItem OfUnit(ac::UnitKind K);
	static FAcPaletteItem OfDoodad(ac::Doodad::Kind K);
	static FAcPaletteItem OfType(EType T);

	/// Its name in `ModelCatalog` (`citadel`, `habdome`, `ranger`, `ore`, `rockSpire`).
	FString CatalogName() const;
	/// `ModelCatalog.title`: "Hab Dome", "Ore deposit", "Rock spire".
	FString Title() const;
	/// Goes to a side (the rest is neutral).
	bool Owned() const { return Type == EType::Structure || Type == EType::Unit; }
	bool operator==(const FAcPaletteItem& O) const;
	bool operator!=(const FAcPaletteItem& O) const { return !(*this == O); }

	/// The palette's rows by group: every kind of building, unit and doodad
	/// the core has (a new kind shows up by itself).
	static const TArray<TPair<FString, TArray<FAcPaletteItem>>>& Groups();
	/// By catalog name (case-insensitive).
	static TOptional<FAcPaletteItem> Named(const FString& Name);
};

UCLASS()
class AUTOCRAFT_API UAcPlaygroundSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UAcPlaygroundSubsystem* Get(const UObject* WorldContext);
	/// This run is the playground (`-AcPlayground`).
	static bool IsPlaygroundRun();

	/// Both sides' ore and MH kept at no less than this (`playgroundStock`).
	static constexpr int64 Stock = 5000;
	static constexpr int32 Sides = 8;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// MARK: - The palette (`GameController+Playground`)

	/// Hold `Item` out to put down (unset: let go). A fresh turn and look
	/// for the scenery; a unit faces the camera.
	void Hold(TOptional<FAcPaletteItem> Item);
	/// A palette row clicked (`rowClicked`): it picks its item up (leaving a
	/// driven unit first); the one held, clicked again, lets go.
	void PickRow(const FAcPaletteItem& Item);
	/// Esc, a right click, driving: let go.
	void Drop();
	const TOptional<FAcPaletteItem>& Held() const { return Palette; }
	/// R: a quarter of a half turn more.
	void Turn();
	/// The side what is put down belongs to (0..7).
	int32 Team() const { return PaletteTeam; }
	void SetTeam(int32 Team);
	/// Why the held item can't go where the pointer is (empty: it can).
	const FString& Note() const { return PaletteNote; }

	/// A click in the view while the palette holds something: put it down
	/// there (it stays held for the next one). False: nothing held.
	bool PlaceAt(FVector2D ViewPoint);
	/// Delete: take away what is under a view point. False: nothing there.
	bool RemoveAt(FVector2D ViewPoint);

	/// Where the held item would go for a ground point, and why it can't.
	struct FSpot
	{
		ac::Vec2 At;
		std::optional<std::string> Why;
	};
	FSpot SpotFor(const FAcPaletteItem& Item, ac::Vec2 Ground) const;

	// MARK: - Toggles (the bar)

	bool IsUnlimited() const { return bUnlimited; }
	void SetUnlimited(bool bOn);
	bool IsFogOn() const;
	void SetFog(bool bOn);
	/// Clear… (asks first); `ClearNow` takes everything away.
	void AskClear();
	void ClearNow();
	void CancelClear();
	bool IsAskingClear() const { return Confirm.IsValid(); }
	bool Piloting() const;
	/// ⌘⇧D: drive the unit nearest the view's middle, or leave it.
	void TogglePilot();

	/// The line under the bar (`showStatus`).
	FString StatusText() const;

	/// The ground under a view point (`pickGround`).
	TOptional<ac::Vec2> PickGround(FVector2D ViewPoint) const;
	/// A ground point's place in the view (points, y down).
	TOptional<FVector2D> ViewPointOf(ac::Vec2 Ground) const;

	/// Scripted: hold `Name`, side `Side`, turned `Turn` (unset: as held),
	/// clicked at ground `At` through the palette and the pointer. False:
	/// not put down (logged).
	bool Put(const FString& Name, ac::Vec2 At, TOptional<int32> Side = {}, TOptional<double> Turn = {});
	/// Put down Swift's `layPlayground` layout.
	void LayOut();

private:
	void OnWorldReady();
	void OnGameStarted(UAcSimSubsystem& Sim);
	void OnFrame(const FAcFrame& Frame);
	bool Hook();
	void TopUp();
	void UpdateGhost(double Time);
	bool PointerPoint(FVector2D& Out) const;
	bool OverHud(FVector2D ViewPoint) const;
	/// Points the palette's column stops short of the view's bottom: clear
	/// of the console's dashboard (the music deck) at its left end. (Swift's
	/// list sits beside the view; here it is laid over it, above the console.)
	float ColumnInset() const;
	void Staged();
	void RunTest();
	void LoadSettings();
	void SaveSettings() const;

	TOptional<FAcPaletteItem> Palette;
	int32 PaletteTeam = 0;
	double PaletteTurn = 0.0;
	uint32 PaletteVariant = 1;
	FString PaletteNote;
	bool bUnlimited = true;

	bool bActive = false;
	bool bHooked = false;
	int32 Frames = 0;
	bool bStaged = false;
	/// Scripted pointer: a ground point held under it (-AcPlaygroundAt).
	TOptional<ac::Vec2> HeldAt;

	TWeakObjectPtr<class AAcHologram> Ghost;
	/// The hologram is the palette's (E8's pilot build shares the actor).
	bool bGhostShown = false;
	TSharedPtr<SAcPalette> List;
	TSharedPtr<SAcPaletteStatus> Status;
	TSharedPtr<SAcPaletteConfirm> Confirm;
	TSharedPtr<IInputProcessor> Keys;
	FDelegateHandle FrameHandle, StartedHandle;
};
