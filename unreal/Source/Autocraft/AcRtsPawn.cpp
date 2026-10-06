#include "AcRtsPawn.h"

#include "AcEffects.h"
#include "AcHeadless.h"

#include "AcLog.h"
#include "AcShot.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcWorldRenderer.h"

#include "Camera/CameraComponent.h"
#include "ContentStreaming.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/ViewportSplitScreen.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ShaderCompiler.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

#include <cmath>

namespace
{
	/// The Swift game has no edge scroll (GAME-LAYER.md §0); this is off
	/// unless asked for.
	TAutoConsoleVariable<int32> CVarEdgeScroll(
		TEXT("ac.EdgeScroll"), 0,
		TEXT("Pan the RTS camera when the cursor rests within this many points of the view's edge (0: off)."));

	FVector2D ToFVector(ac::Vec2 V) { return FVector2D(V.x, V.y); }

	/// Player P's main base: where the game put it (`players[p].start`; a
	/// 2-player map swaps its two starts half the time, State.swift:810),
	/// else the map's start P. Null if neither exists.
	std::optional<ac::Vec2> MainBase(const UAcSimSubsystem& Sim, const size_t P)
	{
		const ac::MapDefinition& Map = Sim.Map();
		const std::vector<ac::Player>& Players = Sim.State().players;
		if (P < Players.size() && Players[P].start && size_t(*Players[P].start) < Map.bases.size())
		{
			return Map.bases[size_t(*Players[P].start)].center;
		}
		if (P < Map.starts.size()) return Map.bases[size_t(Map.starts[P])].center;
		return std::nullopt;
	}
	ac::Vec2 ToVec(FVector2D V) { return ac::Vec2(V.X, V.Y); }

	const TCHAR* const InputDir = TEXT("/Game/Input/");
	TSoftObjectPtr<UInputAction> ActionPath(const TCHAR* Name)
	{
		const FString Path = FString(InputDir) + Name + TEXT(".") + Name;
		return TSoftObjectPtr<UInputAction>(FSoftObjectPath(Path));
	}
}

/// The trackpad, which Enhanced Input has no keys for: the scroll gesture
/// (two fingers) and the pinch, as `GameView.scrollWheel` and `magnify`
/// read them. A plain mouse wheel is no gesture and goes on to `IA_Zoom`.
class FAcRtsInputProcessor : public IInputProcessor
{
public:
	explicit FAcRtsInputProcessor(AAcRtsPawn* InPawn) : Pawn(InPawn) {}

	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

	virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication&, const FPointerEvent& Wheel, const FPointerEvent* Gesture) override
	{
		AAcRtsPawn* P = Pawn.Get();
		if (!Gesture || !P) return false;
		FVector2D At;
		if (!P->ScreenToView(FVector2D(Gesture->GetScreenSpacePosition()), At)) return false;
		const FModifierKeysState& Keys = Gesture->GetModifierKeys();
		switch (Gesture->GetGestureType())
		{
		case EGestureEvent::Scroll:
			// Option-scroll is the hearing range (a later chunk): let it through.
			if (Keys.IsAltDown()) return false;
			P->OnScrollGesture(At, FVector2D(Gesture->GetGestureDelta()), Keys.IsCommandDown() || Keys.IsControlDown());
			return true;
		case EGestureEvent::Magnify:
			P->OnMagnify(At, Gesture->GetGestureDelta().X);
			return true;
		default:
			return false;
		}
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("AcRtsTrackpad"); }

private:
	TWeakObjectPtr<AAcRtsPawn> Pawn;
};

AAcRtsPawn::AAcRtsPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	// Before the camera manager reads the view.
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	SetCanBeDamaged(false);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	RootComponent = Camera;
	Camera->SetConstraintAspectRatio(false);
	// The FOV set in `Apply` is horizontal at the view's real aspect. The
	// player's default (MaintainYFOV) would read it at the camera's 16:9
	// aspect instead and zoom in 1.11x at 16:10 (SHOTS.md, A3).
	Camera->bOverrideAspectRatioAxisConstraint = true;
	Camera->AspectRatioAxisConstraint = AspectRatio_MaintainXFOV;
	// No post settings here: exposure and grading are the daylight's
	// (`AAcDaylight`, an unbound post-process volume); the camera's own
	// settings would override it.

	MappingContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Input/IMC_Rts.IMC_Rts")));
	PanAction = ActionPath(TEXT("IA_Pan"));
	DragAction = ActionPath(TEXT("IA_DragPan"));
	ZoomAction = ActionPath(TEXT("IA_Zoom"));
	ZoomStepAction = ActionPath(TEXT("IA_ZoomStep"));
	FitAction = ActionPath(TEXT("IA_Fit"));
	GoToBaseAction = ActionPath(TEXT("IA_GoToBase"));
}

AAcRtsPawn* AAcRtsPawn::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? Cast<AAcRtsPawn>(PC->GetPawn()) : nullptr;
}

void AAcRtsPawn::BeginPlay()
{
	Super::BeginPlay();
	if (FParse::Value(FCommandLine::Get(), TEXT("AcCover="), Cover)) Cover = FMath::Max(0.0, Cover);
	Size = MeasureSize();
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this))
	{
		GameStartedHandle = Sim->OnGameStarted.AddUObject(this, &AAcRtsPawn::OnGameStarted);
		if (Sim->IsRunning()) StartView(*Sim);
	}
	if (FSlateApplication::IsInitialized())
	{
		Trackpad = MakeShared<FAcRtsInputProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(Trackpad);
	}
	if (UAcShotSubsystem::IsShotRun()) StageShot();
}

void AAcRtsPawn::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this); Sim && GameStartedHandle.IsValid())
	{
		Sim->OnGameStarted.Remove(GameStartedHandle);
	}
	GameStartedHandle.Reset();
	if (Trackpad && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(Trackpad);
	Trackpad.Reset();
	Super::EndPlay(Reason);
}

void AAcRtsPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (APlayerController* PC = Cast<APlayerController>(NewController))
	{
		// A visible cursor that never gets captured: the game is played
		// with the pointer (the controller chunk owns clicks).
		PC->SetShowMouseCursor(true);
		if (AcHeadless::Is()) AcHeadless::FreeMouse(GetWorld()); // never touch the user's mouse
		else
		{
			FInputModeGameAndUI Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Mode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Mode);
		}
		AddMappingContext();
	}
}

void AAcRtsPawn::AddMappingContext()
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	const ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Input = Player ? Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	UInputMappingContext* Context = MappingContext.LoadSynchronous();
	if (!Input) return;
	if (!Context)
	{
		UE_LOG(LogAutocraft, Error, TEXT("rts camera: %s is missing (run Tools/Editor/make_input.py)"), *MappingContext.ToString());
		return;
	}
	if (!Input->HasMappingContext(Context)) Input->AddMappingContext(Context, 0);
}

void AAcRtsPawn::SetupPlayerInputComponent(UInputComponent* InputComponent)
{
	Super::SetupPlayerInputComponent(InputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		UE_LOG(LogAutocraft, Error, TEXT("rts camera: the input component is not an EnhancedInputComponent"));
		return;
	}
	auto Bind = [&](TSoftObjectPtr<UInputAction>& Soft, ETriggerEvent Event, void (AAcRtsPawn::*Handler)(const FInputActionValue&)) {
		if (UInputAction* Action = Soft.LoadSynchronous())
		{
			Input->BindAction(Action, Event, this, Handler);
		}
		else
		{
			UE_LOG(LogAutocraft, Error, TEXT("rts camera: %s is missing (run Tools/Editor/make_input.py)"), *Soft.ToString());
		}
	};
	Bind(PanAction, ETriggerEvent::Triggered, &AAcRtsPawn::OnPan);
	Bind(PanAction, ETriggerEvent::Completed, &AAcRtsPawn::OnPanEnd);
	Bind(DragAction, ETriggerEvent::Started, &AAcRtsPawn::OnDragStart);
	Bind(DragAction, ETriggerEvent::Completed, &AAcRtsPawn::OnDragEnd);
	Bind(ZoomAction, ETriggerEvent::Triggered, &AAcRtsPawn::OnWheel);
	Bind(ZoomStepAction, ETriggerEvent::Triggered, &AAcRtsPawn::OnZoomStep);
	Bind(FitAction, ETriggerEvent::Triggered, &AAcRtsPawn::OnFit);
	Bind(GoToBaseAction, ETriggerEvent::Triggered, &AAcRtsPawn::OnGoToBase);
	AddMappingContext();
}

// MARK: - The view

void AAcRtsPawn::OnGameStarted(UAcSimSubsystem& Sim)
{
	StartView(Sim);
}

void AAcRtsPawn::StartView(const UAcSimSubsystem& Sim)
{
	const ac::MapDefinition& Map = Sim.Map();
	// The same map (a restart, the next game): keep the view
	// (`GameController.keepCamera`). Otherwise over blue's (player 0's)
	// main base (`GameController.swift:167`; the playground's middle: it
	// has none). Swift takes `map.starts.first`, which is the enemy's main
	// in the half of 2-player games whose starts were swapped: here the
	// base player 0 really starts on.
	const bool bFirst = !FreeView.has_value();
	if (!FreeView || !(FreeView->bounds == Map.bounds))
	{
		FreeView.emplace(Map.bounds, MainBase(Sim, 0).value_or(Map.front()));
	}
	// The window's size may not be known yet (the first frame): then the
	// Swift default until Tick measures it.
	if (Size.X <= 1 || Size.Y <= 1) Size = FVector2D(1280, 800);
	FreeView->resize(ToVec(Size), Cover);
	if (bFirst) bCommandLinePending = true;
	bDirty = true;
	Apply();
	UE_LOG(LogAutocraft, Log, TEXT("rts camera: %s, view %.0fx%.0f points, cover %.0f, target %.2f,%.2f, %.3f points per cell (%.3f-%.0f)"),
	       UTF8_TO_TCHAR(Map.name.c_str()), Size.X, Size.Y, Cover, FreeView->target.x, FreeView->target.y,
	       FreeView->pointsPerCell, FreeView->minPointsPerCell(), ac::FreeView::maxPointsPerCell);
}

void AAcRtsPawn::ReadCamPath()
{
	bCamPathRead = true;
	FString Text;
	if (!FParse::Value(FCommandLine::Get(), TEXT("AcCamPath="), Text, /*bShouldStopOnSeparator*/ false)) return;
	TArray<FString> Keys;
	Text.TrimQuotes().ParseIntoArray(Keys, TEXT(";"));
	for (const FString& K : Keys)
	{
		FString T, Rest;
		TArray<FString> V;
		if (!K.Split(TEXT(":"), &T, &Rest) || Rest.ParseIntoArray(V, TEXT(",")) < 2)
		{
			UE_LOG(LogAutocraft, Warning, TEXT("rts camera: -AcCamPath key '%s' is not T:DX,DZ[,PPC]"), *K);
			continue;
		}
		CamPath.Add({FCString::Atod(*T), FCString::Atod(*V[0]), FCString::Atod(*V[1]), V.Num() > 2 ? FCString::Atod(*V[2]) : 0.0});
	}
	CamPath.Sort([](const FCamKey& A, const FCamKey& B) { return A.T < B.T; });
	UE_LOG(LogAutocraft, Log, TEXT("rts camera: -AcCamPath with %d keys over %.2f s"), CamPath.Num(), CamPath.Num() ? CamPath.Last().T : 0.0);
}

void AAcRtsPawn::FollowCamPath()
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (!CamBase)
	{
		CamBase = FreeView->target;
		CamBaseZoom = FreeView->pointsPerCell;
	}
	if (CamStart < 0)
	{
		// A recording: from its first frame; until then the warm-up frames
		// hold the first key, so the first recorded frame shows it.
		if (UAcShotSubsystem::IsRecordRun())
		{
			const UAcShotSubsystem* Shot = UAcShotSubsystem::Get(this);
			if (Shot && Shot->GetRecordStart().IsSet()) CamStart = Shot->GetRecordStart().GetValue();
		}
		else
		{
			CamStart = Now;
		}
	}
	const double T = CamStart < 0 ? CamPath[0].T : Now - CamStart;
	auto Zoom = [this](const FCamKey& K) { return K.Zoom > 0 ? K.Zoom : CamBaseZoom; };
	FCamKey At = CamPath[0];
	if (T >= CamPath.Last().T)
	{
		At = CamPath.Last();
	}
	else if (T > CamPath[0].T)
	{
		int32 I = 1;
		while (CamPath[I].T <= T) ++I;
		const FCamKey& A = CamPath[I - 1];
		const FCamKey& B = CamPath[I];
		const double U = FMath::SmoothStep(0.0, 1.0, (T - A.T) / FMath::Max(B.T - A.T, 1e-6));
		At.Dx = FMath::Lerp(A.Dx, B.Dx, U);
		At.Dz = FMath::Lerp(A.Dz, B.Dz, U);
		At.Zoom = FMath::Exp(FMath::Lerp(FMath::Loge(Zoom(A)), FMath::Loge(Zoom(B)), U));
	}
	FreeView->pointsPerCell = FMath::Clamp(Zoom(At), FreeView->minPointsPerCell(), ac::FreeView::maxPointsPerCell);
	FreeView->center(ac::Vec2(CamBase->x + At.Dx, CamBase->y + At.Dz));
	bDirty = true;
}

void AAcRtsPawn::ApplyCommandLine()
{
	if (!bCamPathRead) ReadCamPath();
	// As `WindowSnapshot.run`: the zoom clamped to the map's range, then
	// centred on the point (the clamp keeps the view on the map).
	const TCHAR* Cmd = FCommandLine::Get();
	double Zoom = 0;
	if (FParse::Value(Cmd, TEXT("AcZoom="), Zoom) && Zoom > 0)
	{
		FreeView->pointsPerCell = FMath::Clamp(Zoom, FreeView->minPointsPerCell(), ac::FreeView::maxPointsPerCell);
		FreeView->clamp();
	}
	// Not stopping on separators: `-AcCamAt=-14,-17` whole, unquoted or
	// quoted (`'-AcCamAt="-14,-17"'`).
	FString At;
	if (FParse::Value(Cmd, TEXT("AcCamAt="), At, /*bShouldStopOnSeparator*/ false))
	{
		FString X, Z;
		if (At.TrimQuotes().Split(TEXT(","), &X, &Z)) FreeView->center(ac::Vec2(FCString::Atod(*X), FCString::Atod(*Z)));
	}
	// -AcCamAtArmy: centre on `-AcStageFight`'s middle once the renderer has
	// staged it (it stages on the game's start; wait up to ~5 s of frames).
	if (FParse::Param(Cmd, TEXT("AcCamAtArmy")))
	{
		static int32 Tries = 0;
		const UAcWorldRenderer* Renderer = UAcWorldRenderer::Get(this);
		if (const TOptional<FVector2D> Fight = Renderer ? Renderer->StagedFightCentre() : TOptional<FVector2D>())
		{
			FreeView->center(ac::Vec2(Fight->X, Fight->Y));
			UE_LOG(LogAutocraft, Log, TEXT("rts camera: -AcCamAtArmy at %.1f,%.1f"), Fight->X, Fight->Y);
		}
		else if (++Tries < 300)
		{
			bCommandLinePending = true;
		}
		else
		{
			UE_LOG(LogAutocraft, Warning, TEXT("rts camera: -AcCamAtArmy without a staged fight (-AcStageFight=N)"));
		}
	}
}

FVector2D AAcRtsPawn::MeasureSize() const
{
	UGameViewportClient* Client = GEngine ? GEngine->GameViewport : nullptr;
	if (!Client) return FVector2D::ZeroVector;
	const FVector2D Inset(ViewInset, 0);
	if (const TSharedPtr<SViewport> Widget = Client->GetGameViewportWidget())
	{
		const FVector2D Local = FVector2D(Widget->GetCachedGeometry().GetLocalSize());
		if (Local.X > 1 && Local.Y > 1) return Local - Inset;
	}
	FVector2D Pixels;
	Client->GetViewportSize(Pixels);
	const TSharedPtr<SWindow> Window = Client->GetWindow();
	const double Scale = Window ? FMath::Max(Window->GetDPIScaleFactor(), 0.1f) : 1.0;
	return Pixels / Scale - Inset;
}

void AAcRtsPawn::SetViewInset(const double Points)
{
	const double P = FMath::Max(0.0, Points);
	if (FMath::IsNearlyEqual(P, ViewInset)) return;
	ViewInset = P;
	ApplyInset();
	// The narrower view at once, keeping the scale (`viewResized`), so view
	// points from now on are the new view's.
	const FVector2D Now = MeasureSize();
	if (FreeView && Now.X > 1 && Now.Y > 1)
	{
		Size = Now;
		FreeView->resize(ToVec(Size));
		bDirty = true;
		Apply();
	}
}

void AAcRtsPawn::ApplyInset() const
{
	UGameViewportClient* Client = GEngine ? GEngine->GameViewport : nullptr;
	if (!Client || Client->SplitscreenInfo.Num() <= ESplitScreenType::None) return;
	FSplitscreenData& Data = Client->SplitscreenInfo[ESplitScreenType::None];
	if (Data.PlayerData.Num() == 0) return;
	double Width = 0;
	if (const TSharedPtr<SViewport> Widget = Client->GetGameViewportWidget()) Width = Widget->GetCachedGeometry().GetLocalSize().X;
	const float F = Width > 1 ? float(FMath::Clamp(ViewInset / Width, 0.0, 0.9)) : 0.f;
	FPerPlayerSplitscreenData& P = Data.PlayerData[0];
	if (FMath::IsNearlyEqual(P.OriginX, F) && FMath::IsNearlyEqual(P.SizeX, 1.f - F)) return;
	P = FPerPlayerSplitscreenData(1.f - F, 1.f, F, 0.f);
	Client->LayoutPlayers();
}

bool AAcRtsPawn::ScreenToView(FVector2D Screen, FVector2D& Out) const
{
	UGameViewportClient* Client = GEngine ? GEngine->GameViewport : nullptr;
	const TSharedPtr<SViewport> Widget = Client ? Client->GetGameViewportWidget() : nullptr;
	if (!Widget) return false;
	const FGeometry& G = Widget->GetCachedGeometry();
	Out = FVector2D(G.AbsoluteToLocal(Screen)) - FVector2D(ViewInset, 0);
	const FVector2D S = FVector2D(G.GetLocalSize()) - FVector2D(ViewInset, 0);
	return Out.X >= 0 && Out.Y >= 0 && Out.X <= S.X && Out.Y <= S.Y;
}

bool AAcRtsPawn::CursorPoint(FVector2D& Out) const
{
	if (!FSlateApplication::IsInitialized()) return false;
	return ScreenToView(FVector2D(FSlateApplication::Get().GetCursorPos()), Out);
}

void AAcRtsPawn::Apply()
{
	if (!FreeView || !bDirty) return;
	bDirty = false;
	// `FreeCamera.apply`: the eye where the view says, looking at the
	// target on the ground plane, pitched down 56°.
	const ac::CanvasProjection P = FreeView->projection();
	const ac::Vec3 Eye = FreeView->cameraPosition();
	SetActorLocationAndRotation(AcSpace::FromSceneKit(Eye.x, Eye.y, Eye.z), FRotator(-P.pitchDegrees, -90.0, 0.0));
	// SceneKit's field of view is vertical (`projectionDirection = .vertical`);
	// Unreal's is horizontal: the same 30° up and down at this aspect.
	const double Aspect = Size.X / FMath::Max(Size.Y, 1.0);
	Camera->SetFieldOfView(float(FMath::RadiansToDegrees(2.0 * std::atan(P.tanHalfY() * Aspect))));
}

void AAcRtsPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!FreeView) return;

	// A resized window keeps the scale (`GameController.viewResized`); an
	// inset view keeps its share of the new width.
	if (ViewInset > 0) ApplyInset();
	const FVector2D Now = MeasureSize();
	if (Now.X > 1 && Now.Y > 1 && !Now.Equals(Size, 0.5))
	{
		Size = Now;
		FreeView->resize(ToVec(Size));
		bDirty = true;
	}
	if (bCommandLinePending && Now.X > 1 && Now.Y > 1)
	{
		bCommandLinePending = false;
		ApplyCommandLine();
		bDirty = true;
		UE_LOG(LogAutocraft, Log, TEXT("rts camera: view %.0fx%.0f points, cover %.0f, bounds %.1f,%.1f-%.1f,%.1f, target %.2f,%.2f, %.3f points per cell (%.3f-%.0f)"),
		       Size.X, Size.Y, Cover, FreeView->bounds.minX, FreeView->bounds.minZ, FreeView->bounds.maxX, FreeView->bounds.maxZ, FreeView->target.x, FreeView->target.y, FreeView->pointsPerCell,
		       FreeView->minPointsPerCell(), ac::FreeView::maxPointsPerCell);
	}
	if (!bCommandLinePending && CamPath.Num() > 0) FollowCamPath();

	if (!bInputBlocked)
	{
		// The left button: a drag once it has moved 4 points (`GameView.mouseDragged`).
		FVector2D C;
		if (bPressed && CursorPoint(C))
		{
			if (bDragged || FVector2D::Distance(C, PressAt) >= DragThreshold)
			{
				bDragged = true;
				if (!C.Equals(LastDrag)) Drag(LastDrag, C);
				LastDrag = C;
			}
		}

		// Keys (`GameController.moveFreeCamera`): a screenful in about
		// 1.3 s at any zoom, diagonals no faster.
		FVector2D D = PanInput;
		if (const int32 Edge = CVarEdgeScroll.GetValueOnGameThread(); Edge > 0 && !bPressed && CursorPoint(C))
		{
			if (C.X <= Edge) D.X -= 1;
			if (C.X >= Size.X - Edge) D.X += 1;
			if (C.Y <= Edge) D.Y -= 1;
			if (C.Y >= Size.Y - Edge) D.Y += 1;
		}
		if (!D.IsNearlyZero())
		{
			const double Speed = FreeView->visibleCells() * 0.8 * DeltaSeconds;
			const FVector2D N = D / FMath::Max(D.Size(), 1.0);
			FreeView->target = FreeView->target + ac::Vec2(N.X, N.Y * 1.2) * Speed;
			FreeView->clamp();
			bDirty = true;
		}
	}
	Apply();

	// A Quake stomp in view shakes the camera a little, when the camera is
	// close (zoomed in to 30 cells of height or less); the view itself
	// (`FreeView`) never moves. Put back once the shake dies.
	double Shake = 0.0;
	if (FreeView->visibleCells() <= 30.0)
	{
		if (const UAcEffects* Fx = UAcEffects::Get(this)) Shake = Fx->StompShake(FreeView->target, FreeView->visibleCells() * 0.6);
	}
	if (Shake != 0.0)
	{
		bShaking = true;
		const ac::Vec3 Eye = FreeView->cameraPosition();
		const FVector Base = AcSpace::FromSceneKit(Eye.x, Eye.y, Eye.z);
		SetActorLocation(Base + FVector(0.0, 0.0, Shake * AcSpace::ToCm(0.15)));
	}
	else if (bShaking)
	{
		bShaking = false;
		bDirty = true;
		Apply();
	}
}

// MARK: - Commands

void AAcRtsPawn::Fit()
{
	if (!FreeView) return;
	FreeView->fit();
	bDirty = true;
}

void AAcRtsPawn::ZoomBy(double Factor)
{
	ZoomAt(Factor, Size / 2);
}

void AAcRtsPawn::ZoomAt(double Factor, FVector2D At)
{
	if (!FreeView || bInputBlocked) return;
	FreeView->zoom(Factor, ToVec(At));
	bDirty = true;
}

void AAcRtsPawn::Drag(FVector2D From, FVector2D To)
{
	if (!FreeView || bInputBlocked) return;
	FreeView->drag(ToVec(From), ToVec(To));
	bDirty = true;
}

void AAcRtsPawn::CenterOn(ac::Vec2 Ground)
{
	if (!FreeView) return;
	FreeView->center(Ground);
	bDirty = true;
}

void AAcRtsPawn::GoToBase(int32 Player)
{
	const UAcSimSubsystem* Sim = UAcSimSubsystem::Get(this);
	if (!Sim || !Sim->IsRunning()) return;
	const ac::MapDefinition& Map = Sim->Map();
	if (Player < 0 || size_t(Player) >= FMath::Max(Map.starts.size(), Sim->State().players.size())) return;
	if (const std::optional<ac::Vec2> Main = MainBase(*Sim, size_t(Player))) CenterOn(*Main);
}

void AAcRtsPawn::SetCover(double Points)
{
	Cover = FMath::Max(0.0, Points);
	if (!FreeView) return;
	FreeView->resize(ToVec(Size), Cover);
	bDirty = true;
}

// MARK: - Input

void AAcRtsPawn::OnPan(const FInputActionValue& Value)
{
	// x right, y down the view, each -1…1.
	PanInput = Value.Get<FVector2D>();
}

void AAcRtsPawn::OnPanEnd(const FInputActionValue&)
{
	PanInput = FVector2D::ZeroVector;
}

void AAcRtsPawn::OnWheel(const FInputActionValue& Value)
{
	// Option-scroll is the hearing range, not a zoom.
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetModifierKeys().IsAltDown()) return;
	const double Dy = Value.Get<float>();
	FVector2D At;
	if (!CursorPoint(At)) At = Size / 2;
	ZoomAt(std::exp(Dy * 0.06), At);
}

void AAcRtsPawn::OnZoomStep(const FInputActionValue& Value)
{
	const float Steps = Value.Get<float>();
	if (Steps != 0) ZoomBy(std::pow(1.25, double(FMath::Sign(Steps))));
}

void AAcRtsPawn::OnFit(const FInputActionValue&)
{
	if (!bInputBlocked) Fit();
}

void AAcRtsPawn::OnDragStart(const FInputActionValue&)
{
	FVector2D C;
	if (!CursorPoint(C)) return;
	bPressed = true;
	bDragged = false;
	PressAt = LastDrag = C;
}

void AAcRtsPawn::OnDragEnd(const FInputActionValue&)
{
	bPressed = false;
	bDragged = false;
}

void AAcRtsPawn::OnGoToBase(const FInputActionValue& Value)
{
	GoToBase(FMath::RoundToInt(Value.Get<float>()) - 1);
}

void AAcRtsPawn::OnScrollGesture(FVector2D At, FVector2D ScrollDelta, bool bZoomModifier)
{
	if (bZoomModifier)
	{
		ZoomAt(std::exp(ScrollDelta.Y * 0.01), At);
	}
	else
	{
		Drag(At, At + ScrollDelta);
	}
}

void AAcRtsPawn::OnMagnify(FVector2D At, double Magnification)
{
	ZoomAt(1.0 + Magnification, At);
}

// MARK: - Headless shots

void AAcRtsPawn::StageShot()
{
	// Hold the picture until the shaders are compiled and the textures in.
	UAcShotSubsystem* Shot = UAcShotSubsystem::Get(this);
	if (!Shot) return;
	Shot->Hold();
	const TWeakObjectPtr<UAcShotSubsystem> WeakShot(Shot);
	int32 Frames = 0;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakShot, Frames](float) mutable {
		if (!WeakShot.IsValid()) return false;
		const bool bCompiling = GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0;
		if (bCompiling || ++Frames < 20) return true;
		IStreamingManager::Get().StreamAllResources(1.0f);
		WeakShot->Release();
		return false;
	}));
}
