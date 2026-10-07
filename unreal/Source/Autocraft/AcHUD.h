// `AAcHUD`: the HUD (GAME-LAYER.md §2.4, §3.2; chunk D1). It puts `SAcRoot`
// (Slate, in C++) over the game viewport and feeds it each frame from
// `UAcSimSubsystem` at the `Hud` stage: the local player's stock, every
// player's army and games won, the victory banner, and a "+N" pop-up for
// each deposit the team sees (`GameController.swift:335`: 4 cells over the
// ground where it was dropped).
//
// Scale: the HUD lays out in Swift points. Pixels per point is the game
// window's backing scale (2 on a Retina screen), or `-AcHudScale=N` /
// `ac.HudScale N` (0: auto). Headless shots (`-RenderOffscreen`) have no
// screen to ask, so pass `-AcHudScale=2` to match a Retina window.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "AcHUD.generated.h"

class SAcRoot;
class UAcSimSubsystem;
struct FAcFrame;

UCLASS()
class AUTOCRAFT_API AAcHUD : public AHUD
{
	GENERATED_BODY()

public:
	AAcHUD();

	/// The HUD of `WorldContext`'s first local player (null when none).
	static AAcHUD* Get(const UObject* WorldContext);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/// The root widget (null in a run with no viewport, e.g. -nullrhi).
	TSharedPtr<SAcRoot> Root() const { return RootWidget; }

	/// Pixels per Swift point (see the header comment).
	float PixelsPerPoint() const;
	/// Viewport pixels → HUD points.
	FVector2f PointsFromPixels(FVector2D Pixels) const;
	/// A world position (Unreal cm) → HUD points; false when behind the camera.
	bool ProjectToPoints(const FVector& World, FVector2f& OutPoints) const;

	/// While driving only the resources stay up (E2 calls this).
	void SetDriving(bool bDriving);

	/// The whole HUD at this opacity (a recording's eject fade). The widgets
	/// paint their own colours, so the first call moves the HUD into a
	/// retainer that draws it as one picture; it stays there.
	void SetFade(float Opacity);

private:
	void Attach();
	void OnFrame(const FAcFrame& Frame);
	void OnGameStarted(UAcSimSubsystem& Sim);

	TSharedPtr<SAcRoot> RootWidget;
	TSharedPtr<class SRetainerWidget> Fader;
	FDelegateHandle FrameHandle;
	FDelegateHandle StartedHandle;
	bool bLoggedScale = false;
};
