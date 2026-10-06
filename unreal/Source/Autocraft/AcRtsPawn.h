// `AAcRtsPawn`: the top-down camera of the window game (GAME-LAYER.md §2.2,
// chunk A3). The view itself is the core's `ac::FreeView` (a port of
// `Sources/GameCore/FreeView.swift`), so the framing, the zoom range and the
// clamping are the Swift game's to the point; this pawn only feeds it input
// and puts a camera where it says (`FreeCamera.apply` in `FreeCamera.swift`).
//
// Units: the view works in view *points* (Slate units: pixels divided by
// the window's DPI scale), as the Swift view does, so `pointsPerCell` means
// the same on a Retina screen in both games.
//
// Input (Enhanced Input, `IMC_Rts`, made by `Tools/Editor/make_input.py`):
//   IA_Pan       WASD and the arrows: `visibleCells·0.8·dt`, y ×1.2
//   IA_DragPan   left button: drags the ground once the press moved 4 points
//   IA_Zoom      mouse wheel: `exp(dy·0.06)` about the cursor
//   IA_ZoomStep  + and -: ×1.25 about the centre
//   IA_Fit       0: the whole map
//   IA_GoToBase  ⌘1…⌘8: centre on that player's start base (value = player+1)
// The trackpad bypasses Enhanced Input (it has no keys for these): a Slate
// input pre-processor takes the scroll gesture (two fingers pan; with ⌘ or
// ctrl they zoom, `exp(dy·0.01)`) and the pinch (`1 + magnification`), as
// `GameView.scrollWheel` and `magnify` do. Option-scroll is left alone (the
// hearing range, a later chunk).
//
// Command line (for headless shots, like `autocraft windowshot`):
//   -AcZoom=PPC      points per cell (clamped to the map's range; 1 = fit)
//   -AcCamAt=X,Z     centre on that ground point (cells; quoted or not)
//   -AcCamAtArmy     centre on -AcStageFight's fight (AcWorldRenderer.h)
//   -AcCamPath=T:DX,DZ,PPC;...  a camera move for recordings: keys at T
//                    seconds (from the first recorded frame of -AcShotRecord,
//                    else from the first frame), DX,DZ cells from the view's
//                    start (after -AcZoom/-AcCamAt/-AcCamAtArmy), PPC the zoom
//                    (0: the start zoom). Eased between keys (smoothstep; the
//                    zoom in log space), held before the first and after the
//                    last. E.g. `0:0,0,30;4:6,-3,70` pans and zooms in.
//   -AcCover=POINTS  the console's height (default `DefaultCover`, the Swift
//                    console's `cover`, until the console chunk sets it)
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"

#include "FreeView.h"

#include <optional>

#include "AcRtsPawn.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UAcSimSubsystem;
class FAcRtsInputProcessor;

UCLASS()
class AUTOCRAFT_API AAcRtsPawn : public APawn
{
	GENERATED_BODY()

public:
	AAcRtsPawn();

	/// The Swift console's `cover` (`Console.cover`: the dashboard's top at
	/// the middle, `cardH + 62 + 18 + 8` with 3 rows of 46-point buttons):
	/// the same at every window size.
	static constexpr double DefaultCover = 234.0;
	/// A press that moves less than this (points) is a click, not a drag.
	static constexpr double DragThreshold = 4.0;

	/// The pawn the local player is driving with, if it is this kind.
	static AAcRtsPawn* Get(const UObject* WorldContext);

	/// The view (valid once a game is running).
	const ac::FreeView* View() const { return FreeView ? &*FreeView : nullptr; }
	/// The view's size in points, as the last frame measured it.
	FVector2D ViewSize() const { return Size; }

	// Commands (menu and keys; `GameController.fitMap`, `zoom(by:)`, `centerOnBase`).
	void Fit();
	/// Zoom about the view's centre.
	void ZoomBy(double Factor);
	/// Zoom about a view point (points, y down).
	void ZoomAt(double Factor, FVector2D At);
	/// Drag the ground under `From` to `To` (view points).
	void Drag(FVector2D From, FVector2D To);
	void CenterOn(ac::Vec2 Ground);
	void GoToBase(int32 Player);
	/// The HUD's height along the bottom (the console chunk calls this).
	void SetCover(double Points);
	/// Points down the window's left that are not the view (the
	/// playground's palette column: Swift's list sits beside the view, not
	/// over it). The 3-D view and view points start right of it; 0: none.
	void SetViewInset(double Points);
	double GetViewInset() const { return ViewInset; }
	/// Pan and zoom are ignored while the command map is open.
	void SetInputBlocked(bool bBlocked) { bInputBlocked = bBlocked; }
	/// True while the left button is down and has moved far enough to be a
	/// drag (a click handler should then not take the release as a click).
	bool IsDragging() const { return bDragged; }

	/// The cursor in view points (y down), or false when it is off the view.
	bool CursorPoint(FVector2D& Out) const;
	/// A Slate screen position in view points.
	bool ScreenToView(FVector2D Screen, FVector2D& Out) const;

	// Called by the trackpad pre-processor.
	void OnScrollGesture(FVector2D At, FVector2D ScrollDelta, bool bZoomModifier);
	void OnMagnify(FVector2D At, double Magnification);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
	virtual void PossessedBy(AController* NewController) override;

private:
	void OnGameStarted(UAcSimSubsystem& Sim);
	void StartView(const UAcSimSubsystem& Sim);
	void ApplyCommandLine();
	void Apply();
	/// The view's size in points now.
	FVector2D MeasureSize() const;
	void AddMappingContext();
	void StageShot();

	void OnPan(const FInputActionValue& Value);
	void OnPanEnd(const FInputActionValue& Value);
	void OnWheel(const FInputActionValue& Value);
	void OnZoomStep(const FInputActionValue& Value);
	void OnFit(const FInputActionValue& Value);
	void OnDragStart(const FInputActionValue& Value);
	void OnDragEnd(const FInputActionValue& Value);
	void OnGoToBase(const FInputActionValue& Value);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> MappingContext;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> PanAction;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> DragAction;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> ZoomAction;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> ZoomStepAction;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> FitAction;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> GoToBaseAction;

	std::optional<ac::FreeView> FreeView;
	FVector2D Size = FVector2D::ZeroVector;
	double Cover = DefaultCover;
	double ViewInset = 0;
	/// Put the local player's viewport right of `ViewInset`.
	void ApplyInset() const;
	bool bDirty = true;
	/// A Quake stomp is shaking the view now (see `Tick`).
	bool bShaking = false;
	bool bInputBlocked = false;
	/// -AcZoom and -AcCamAt wait for the first measured view size.
	bool bCommandLinePending = false;

	/// -AcCamPath: the keys, and the view it starts from (taken on the first
	/// frame it runs).
	struct FCamKey { double T = 0, Dx = 0, Dz = 0, Zoom = 0; };
	TArray<FCamKey> CamPath;
	TOptional<ac::Vec2> CamBase;
	double CamBaseZoom = 0;
	double CamStart = -1;
	bool bCamPathRead = false;
	void ReadCamPath();
	void FollowCamPath();

	FVector2D PanInput = FVector2D::ZeroVector;
	bool bPressed = false;
	bool bDragged = false;
	FVector2D PressAt = FVector2D::ZeroVector;
	FVector2D LastDrag = FVector2D::ZeroVector;

	FDelegateHandle GameStartedHandle;
	TSharedPtr<FAcRtsInputProcessor> Trackpad;
};
