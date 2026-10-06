// `UAcCommandMapSubsystem`: the command map (M) wired into the game (chunk
// D8, GAME-LAYER.md §2.3), the port of `GameController+Map.swift` (`toggleMap`,
// `openMap`, `closeMap`, `updateMap`, `mapClick`, `mapKey`, `give`) and of
// `updateCommander`/`cancel` (`GameController+Command.swift` :422, :440).
//
// - M opens it, only with the team's AI commander on (`Commanded`), in the
//   top-down view (not while driving); the selection lets go. While it is
//   open: D4's pointer and D5's hotkeys are blocked (`SetBlocked`), the RTS
//   camera takes no drags or zooms (`SetInputBlocked`; the keys still pan,
//   as in Swift), and clicks are the map's (`SAcCommandMap`, layer
//   `AcHudLayer::CommandMap`). M closes it; Esc shuts the menu, then the map;
//   1-4 set the stance.
// - Four times a second (and at once after an order, or D5's
//   `OnCommanderChanged`): the objectives' status is asked of the AI, the
//   beacons in the world are updated (`AAcBeacons`, also while the map is
//   shut), and the open map is refreshed; an order menu whose orders no
//   longer hold is made again, and goes once it has nothing to offer.
// - The big map is D7's `MakeMinimap(Size, 1.8, false)`, sized to
//   `SAcCommandMap::MapArea` of the view (again when the view's size changes).
// - Eight players: the team is the local player's commander; allies' sites
//   and buildings are "ours" (see AcCommandMapLogic.h).
//
// Not here: the PILOTS rows and their pick buttons (E9, AcLeveling.h: it sets
// `FAcCommandMapInfo::PilotsRoom` through `OnInfo`, draws them through
// `PilotsPainter` and takes `OnPick`).
//
// Command line and console (headless shots and tests; no desktop automation):
//   -AcCommandMap            open it once the game runs (`windowshot --command-map`)
//   -AcOrders[=site]         Swift `windowshot --orders`: an attack on the enemy's
//                            main, a guard on the home base, Aggressive, a Citadel
//                            asked at the nearest free site, a Bastion out front and
//                            a few buildings behind each main; the map open with the
//                            menu on the Bastion (`site`: on the next free site;
//                            `world`: the map shut again, for the beacons)
//   -AcQueue                 Swift `--queue`: Longbow ×3, Aegis shield, Rangers (keep
//                            making), a Bastion; keep 300 ore; MH split
//   -AcMapGround="X,Y"       a click on the map at that ground point (opens the menu)
//   -AcMapPick=N             then press the menu's item N (1-based)
//   -AcMapShut               shut the map again once staged (to see the beacons)
//   ac.CommandMap [open|close]   toggle, open or close
//   ac.MapClick X Y          a click at a view point (points, y down), as the mouse
//   ac.MapGround X Y         a click on the map at a ground point
//   ac.MapPick N             press item N of the open menu
//   ac.MapKey m|esc|1-4      a key while the map is open
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "AcCommandMapLogic.h"

#include <map>

#include "AcCommandMap.generated.h"

class SAcCommandMap;
class UAcSimSubsystem;
struct FAcCommandMapHit;
struct FAcFrame;

DECLARE_MULTICAST_DELEGATE_OneParam(FAcOnCommandMapInfo, FAcCommandMapInfo& /*Info*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FAcOnCommandMapPick, ac::Perk /*Perk*/);
struct FAcCommandMapColumn;

UCLASS()
class AUTOCRAFT_API UAcCommandMapSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UAcCommandMapSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	bool IsOpen() const { return bOpen; }
	/// M: open or shut. True when used.
	bool Toggle();
	/// Open it (false: no commander for the team, or driving).
	bool Open();
	void Close();
	/// A key while it is open (`mapKey`: "m", "esc", "1"-"4"). True when used.
	bool Key(const FString& K);
	/// A click at a view point (points, y down), through the widget's hit test.
	bool ClickAt(FVector2D ViewPoint);
	/// A click on the map at a ground point (opens the order menu there).
	void ClickGround(ac::Vec2 Ground);
	/// Press item `Index` (0-based) of the open menu.
	bool PickMenu(int32 Index);
	/// The order menu, if open.
	const TOptional<FAcMapMenu>& Menu() const { return MapMenu; }

	/// Refresh the team's orders (`updateCommander`): every 0.25 s, or `bNow`.
	void Refresh(bool bNow);

	/// Fired with each map refresh before it is shown (E9 adds the pilots).
	FAcOnCommandMapInfo OnInfo;
	/// E9: a PILOTS pick button was clicked.
	FAcOnCommandMapPick OnPick;
	/// E9: draws the PILOTS rows (`SAcCommandMap::SetPilotsPainter`).
	TFunction<void(FAcCommandMapColumn&)> PilotsPainter;

private:
	void OnFrame(const FAcFrame& Frame);
	void OnHit(const FAcCommandMapHit& Hit);
	void UpdateMap();
	void Give(const ac::Command& C);
	void CancelId(int64 Id);
	bool Mount();
	void FitMinimap();
	void SetBlocked(bool bBlocked);
	void PollKeys();
	void StageCommandLine(UAcSimSubsystem& Sim);
	static double Clock();

	TSharedPtr<SAcCommandMap> Widget;
	bool bOpen = false;
	TOptional<FAcMapMenu> MapMenu;
	std::map<int64_t, ac::ObjectiveStatus> ObjectiveStatus;
	double Shown = -1e9;
	FVector2D MinimapFor = FVector2D::ZeroVector;

	FDelegateHandle FrameHandle;
	FDelegateHandle CommanderHandle;
	TWeakObjectPtr<class UAcCommandsSubsystem> BoundCommands;

	// Scripted staging (-AcOrders, -AcQueue, -AcCommandMap, -AcMapGround, -AcMapPick).
	int32 Frames = 0;
	bool bStaged = false;
	bool bWantOpen = false;
	bool bWantQueue = false;
	TOptional<FString> WantOrders;
	TOptional<ac::Vec2> WantGround;
	int32 WantPick = 0;
};
