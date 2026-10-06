#include "AcHUD.h"

#include "AcHudStyle.h"
#include "AcLog.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcTerrain.h"
#include "SAcRoot.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Widgets/SWindow.h"

static TAutoConsoleVariable<float> CVarAcHudScale(
	TEXT("ac.HudScale"), 0.0f,
	TEXT("Pixels per HUD point (Swift point). 0: the window's backing scale (2 on Retina)."));

AAcHUD::AAcHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

AAcHUD* AAcHUD::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? Cast<AAcHUD>(PC->GetHUD()) : nullptr;
}

void AAcHUD::BeginPlay()
{
	Super::BeginPlay();
	UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim) return;
	Attach();
	if (!RootWidget) return;
	FrameHandle = Sim->AddFrameListener(EAcFrameStage::Hud, FAcFrameEvent::FDelegate::CreateUObject(this, &AAcHUD::OnFrame));
	StartedHandle = Sim->OnGameStarted.AddUObject(this, &AAcHUD::OnGameStarted);
}

void AAcHUD::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this))
	{
		Sim->RemoveFrameListener(EAcFrameStage::Hud, FrameHandle);
		Sim->OnGameStarted.Remove(StartedHandle);
	}
	if (RootWidget && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(RootWidget.ToSharedRef());
	}
	RootWidget.Reset();
	Super::EndPlay(Reason);
}

void AAcHUD::Attach()
{
	UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (!Viewport || !FSlateApplication::IsInitialized()) return;
	TWeakObjectPtr<AAcHUD> Self(this);
	TWeakObjectPtr<UGameViewportClient> WeakViewport(Viewport);
	RootWidget = SNew(SAcRoot)
		.PixelsPerPoint_Lambda([Self] { return Self.IsValid() ? Self->PixelsPerPoint() : 2.0f; })
		.EngineScale_Lambda([WeakViewport]
		{
			return WeakViewport.IsValid() ? UWidgetLayoutLibrary::GetViewportScale(WeakViewport.Get()) : 1.0f;
		});
	Viewport->AddViewportWidgetContent(RootWidget.ToSharedRef(), 10);
	UE_LOG(LogAutocraft, Log, TEXT("hud: attached"));
}

float AAcHUD::PixelsPerPoint() const
{
	static const float FromCommandLine = []
	{
		float S = 0;
		FParse::Value(FCommandLine::Get(), TEXT("AcHudScale="), S);
		return S;
	}();
	if (FromCommandLine > 0) return FromCommandLine;
	if (const float S = CVarAcHudScale.GetValueOnGameThread(); S > 0) return S;
	const UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (const TSharedPtr<SWindow> Window = Viewport ? Viewport->GetWindow() : nullptr)
	{
		return FMath::Max(1.0f, Window->GetDPIScaleFactor());
	}
	return FMath::Max(1.0f, FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(0, 0));
}

FVector2f AAcHUD::PointsFromPixels(const FVector2D Pixels) const
{
	return FVector2f(Pixels / PixelsPerPoint());
}

bool AAcHUD::ProjectToPoints(const FVector& World, FVector2f& OutPoints) const
{
	FVector2D Pixels;
	if (!PlayerOwner || !PlayerOwner->ProjectWorldLocationToScreen(World, Pixels, false)) return false;
	OutPoints = PointsFromPixels(Pixels);
	return true;
}

void AAcHUD::SetDriving(const bool bDriving)
{
	if (RootWidget) RootWidget->SetDriving(bDriving);
}

void AAcHUD::OnGameStarted(UAcSimSubsystem& Sim)
{
	if (RootWidget) RootWidget->Reset();
}

void AAcHUD::OnFrame(const FAcFrame& Frame)
{
	if (!RootWidget || !Frame.State) return;
	if (!bLoggedScale)
	{
		bLoggedScale = true;
		const UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
		FVector2D Size(0, 0);
		if (Viewport) Viewport->GetViewportSize(Size);
		UE_LOG(LogAutocraft, Log, TEXT("hud: viewport %.0fx%.0f px, %.2f px per point, engine UI scale %.3f"), Size.X, Size.Y,
			PixelsPerPoint(), Viewport ? UWidgetLayoutLibrary::GetViewportScale(Viewport) : 0.0f);
	}
	const double Now = FPlatformTime::Seconds();
	RootWidget->Update(*Frame.State, Frame.LocalPlayer, Frame.RealDelta, Now);

	// Deposits the team sees: "+N" 4 cells over the ground there.
	const AAcTerrain* Terrain = AAcTerrain::Find(GetWorld());
	for (const ac::GameEvent& E : Frame.Events)
	{
		const auto* D = E.as<ac::GameEvent::Deposited>();
		if (!D) continue;
		const double Ground = Terrain ? Terrain->FieldHeight(D->at) : 0.0;
		FVector2f At;
		if (ProjectToPoints(AcSpace::ToWorld(D->at, Ground + 4.0), At))
		{
			RootWidget->Popup(D->amount, At, Now);
		}
	}
}
