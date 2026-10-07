#include "AcPilotPawn.h"
#include "AcGasGiant.h"

#include "AcHeadless.h"

#include "AcAudioDirector.h"
#include "AcConsole.h"
#include "AcPilotAudio.h"
#include "AcEffects.h"
#include "AcFog.h"
#include "AcLamps.h"
#include "AcLifeBars.h"
#include "AcLog.h"
#include "AcMinimap.h"
#include "AcModelCatalog.h"
#include "AcPick.h"
#include "AcPilotAids.h"
#include "AcPilotAim.h"
#include "AcPointer.h"
#include "AcPoseAir.h"
#include "AcRaysWorld.h"
#include "AcRtsPawn.h"
#include "AcSettings.h"
#include "AcShot.h"
#include "AcSimSubsystem.h"
#include "AcSpace.h"
#include "AcTerrain.h"
#include "AcWorldRenderer.h"
#include "SAcConsole.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "Pilot.h"
#include "Rules.h"
#include "Simulation.h"

#include <cmath>

namespace
{
	constexpr double Pi = UE_DOUBLE_PI;

	TAutoConsoleVariable<float> CVarLookScale(TEXT("ac.PilotLookScale"), 1.0f,
		TEXT("Scale on the mouse look while driving (1: Swift's 0.0032/0.0026 rad a point)."));
	TAutoConsoleVariable<float> CVarLookBase(TEXT("ac.PilotLookBase"), 2.0f,
		TEXT("The mouse turn's base gain over Swift's points (raw counts are slower than macOS's accelerated points)."));
	TAutoConsoleVariable<float> CVarDive(TEXT("ac.PilotDive"), 1.2f,
		TEXT("Seconds the take-over flies the camera from the top-down view into the unit (0: a cut)."));
	TAutoConsoleVariable<int32> CVarPickParts(TEXT("ac.PickParts"), 1,
		TEXT("Top-down picking against the models' parts (E1 rays): 0 boxes only (Swift), 1 a part hit refines "
		     "the box's distance (a miss keeps the box), 2 a miss drops the candidate."));

	const TCHAR* const ActionNames[] = {TEXT("IA_PilotMove"), TEXT("IA_PilotLook"), TEXT("IA_PilotAct"),
		TEXT("IA_PilotAbility"), TEXT("IA_PilotView"), TEXT("IA_PilotNext"), TEXT("IA_PilotLeave"),
		TEXT("IA_PilotKey"), TEXT("IA_PilotFree")};

	UAcSimSubsystem* SimOf(const UObject* O) { return UAcSimSubsystem::Get(O); }

	FString KindName(const ac::UnitKind K) { return UTF8_TO_TCHAR(std::string(ac::rawValue(K)).c_str()); }
	FString TaskName(const ac::Unit::Task T) { return UTF8_TO_TCHAR(std::string(ac::rawValue(T)).c_str()); }

	/// How high a unit's chest is over its feet (`GameScene.chestHeight`).
	double ChestHeight(const ac::UnitKind K) { return K == ac::UnitKind::juggernaut ? 0.85 : 0.62; }

	double GroundAt(const UWorld* World, const ac::Vec2 P)
	{
		const AAcTerrain* T = AAcTerrain::Find(World);
		return T ? T->FieldHeight(P) : 0.0;
	}
}

// MARK: - The camera

void UAcPilotCameraComponent::GetCameraView(const float DeltaTime, FMinimalViewInfo& DesiredView)
{
	Super::GetCameraView(DeltaTime, DesiredView);
	// The eye sits inside a helmet or a cab: Swift's near plane, this view only.
	DesiredView.PerspectiveNearClipPlane = float(AcSpace::ToCm(FAcPilotCamera::NearCells));
}

// MARK: - The pawn

AAcPilotPawn::AAcPilotPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	SetCanBeDamaged(false);
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	AutoPossessAI = EAutoPossessAI::Disabled;

	CameraComponent = CreateDefaultSubobject<UAcPilotCameraComponent>(TEXT("Camera"));
	RootComponent = CameraComponent;
	CameraComponent->SetFieldOfView(float(FAcPilotCamera::HorizontalFov(16.0 / 10.0)));
	CameraComponent->SetConstraintAspectRatio(false);
	CameraComponent->bOverrideAspectRatioAxisConstraint = true;
	CameraComponent->AspectRatioAxisConstraint = AspectRatio_MaintainXFOV;
	// No post settings: exposure and grading are the daylight's.

	Mount = CreateDefaultSubobject<USceneComponent>(TEXT("CockpitMount"));
	Mount->SetupAttachment(CameraComponent);

	Skirt = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Skirt"));
	Skirt->SetupAttachment(CameraComponent);
	Skirt->SetUsingAbsoluteLocation(true);
	Skirt->SetUsingAbsoluteRotation(true);
	Skirt->SetUsingAbsoluteScale(true);
	Skirt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Skirt->SetCastShadow(false);
	Skirt->SetGenerateOverlapEvents(false);
	Skirt->bNeverDistanceCull = true;
	Skirt->SetVisibility(false);
	if (UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		Skirt->SetStaticMesh(Plane);
	}
}

AAcPilotPawn* AAcPilotPawn::SpawnFor(UWorld* World)
{
	if (!World) return nullptr;
	if (AAcPilotPawn* Existing = Find(World)) return Existing;
	FActorSpawnParameters Params;
	Params.Name = TEXT("AcPilotPawn");
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AAcPilotPawn>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
}

AAcPilotPawn* AAcPilotPawn::Find(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World) return nullptr;
	TActorIterator<AAcPilotPawn> It(const_cast<UWorld*>(World));
	return It ? *It : nullptr;
}

void AAcPilotPawn::BeginPlay()
{
	Super::BeginPlay();
	for (const TCHAR* Name : ActionNames)
	{
		const FString Asset = FString::Printf(TEXT("/Game/Input/%s.%s"), Name, Name);
		if (UInputAction* A = LoadObject<UInputAction>(nullptr, *Asset)) Actions.Add(Name, A);
		else UE_LOG(LogAutocraft, Warning, TEXT("pilot: %s is missing (run Tools/Editor/make_pilot_input.py)"), *Asset);
	}
	PilotContext = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Pilot.IMC_Pilot"));
	RtsContext = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Rts.IMC_Rts"));

	if (UAcSimSubsystem* Sim = SimOf(this))
	{
		FrameHandle = Sim->AddFrameListener(EAcFrameStage::Hud, FAcFrameEvent::FDelegate::CreateUObject(this, &AAcPilotPawn::OnFrame));
		StartedHandle = Sim->OnGameStarted.AddUObject(this, &AAcPilotPawn::OnGameStarted);
	}
	BindPointer();

	// The scripted drive (-AcPilot=KIND).
	const TCHAR* Cmd = FCommandLine::Get();
	// The view chosen last (V), kept across launches (`UAcSettings`; not in
	// agents' headless runs); -AcPilotThird: third person
	// (`windowshot --pilot ... --third`).
	if (UAcSettings::Remembers()) bThird = UAcSettings::Get().bThirdPerson;
	if (FParse::Param(Cmd, TEXT("AcPilotThird"))) bThird = true;
	if (UAcPointer* Pointer = UAcPointer::Get(this)) Pointer->SetThirdPerson(bThird);
	if (float DiveSeconds = 0; FParse::Value(Cmd, TEXT("AcPilotDive="), DiveSeconds)) CVarDive->Set(DiveSeconds, ECVF_SetByCommandline);
	if (FParse::Value(Cmd, TEXT("AcPilot="), ScriptKind))
	{
		if (UAcShotSubsystem::IsRecordRun()) FParse::Value(Cmd, TEXT("AcPilotDiveAt="), ScriptDiveAt);
		if (FString Out; UAcShotSubsystem::IsRecordRun() && FParse::Value(Cmd, TEXT("AcPilotPullOut="), Out, false))
		{
			FString At, For;
			if (!Out.Split(TEXT(","), &At, &For)) At = Out;
			ScriptPullOutAt = FCString::Atod(*At);
			if (!For.IsEmpty()) ScriptPullOutFor = FMath::Max(0.5, FCString::Atod(*For));
		}
		bScriptStage = FParse::Param(Cmd, TEXT("AcPilotStage"));
		FParse::Value(Cmd, TEXT("AcPilotVariant="), StageVariant);
		FString Steps = TEXT("w:1.5,w+right:1.5,d:1,act:1");
		FParse::Value(Cmd, TEXT("AcPilotPath="), Steps, false);
		TArray<FString> Parts;
		Steps.ParseIntoArray(Parts, TEXT(","));
		for (const FString& P : Parts)
		{
			FString Key, Seconds;
			if (!P.Split(TEXT(":"), &Key, &Seconds)) { Key = P; Seconds = TEXT("0"); }
			Path.Add({Key.ToLower(), FCString::Atod(*Seconds)});
		}
		double Total = 0;
		for (const FStep& S : Path) Total += S.Seconds;
		ScriptFor = bScriptStage ? 0.5 : Total + 0.2;
		FParse::Value(Cmd, TEXT("AcPilotFor="), ScriptFor);
		// A recording (-AcShotRecord) shows the drive instead of waiting it out.
		if (UAcShotSubsystem::IsShotRun() && !UAcShotSubsystem::IsRecordRun())
		{
			if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(this))
			{
				Shot->Hold();
				bShotHeld = true;
			}
		}
	}
}

void AAcPilotPawn::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UAcSimSubsystem* Sim = SimOf(this))
	{
		if (FrameHandle.IsValid()) Sim->RemoveFrameListener(EAcFrameStage::Hud, FrameHandle);
		if (StartedHandle.IsValid()) Sim->OnGameStarted.Remove(StartedHandle);
	}
	if (UAcAudioDirector* Audio = UAcAudioDirector::Get(this))
	{
		Audio->SetEars({});
		Audio->Blocked = nullptr;
	}
	Super::EndPlay(Reason);
}

void AAcPilotPawn::OnGameStarted(UAcSimSubsystem& Sim)
{
	// A new game (or the next one): whoever was driven is no more.
	if (Driving()) Leave();
	SkirtMap = nullptr;
}

void AAcPilotPawn::BindPointer()
{
	if (bPointerBound) return;
	UAcPointer* Pointer = UAcPointer::Get(this);
	if (!Pointer) return;
	bPointerBound = true;
	Pointer->OnTakeOver.BindWeakLambda(this, [this](const int64 Id) { TakeOver(Id); });
	Pointer->SetThirdPerson(bThird);

	// D4's precise picks on E1's part shapes (`FAcPickShapes::Refine`).
	TWeakObjectPtr<AAcPilotPawn> Self(this);
	Pointer->Shapes().Refine = [Self](const FAcHover& What, const ac::FreeView::Ray& Ray, const double T) -> std::optional<double>
	{
		const int32 Mode = CVarPickParts.GetValueOnGameThread();
		const UAcWorldRenderer* R = Self.IsValid() ? UAcWorldRenderer::Get(Self.Get()) : nullptr;
		if (Mode == 0 || !R) return T;
		std::optional<double> Out = T;
		R->ForEachObject([&](const int64 Id, const bool bUnit, const FAcModelInfo& Model, const FAcPose& Pose,
			TConstArrayView<FTransform> PartWorld)
		{
			if (Id != What.Id || bUnit != What.IsUnit() || Pose.bHidden) return;
			const FAcRayShape* Shape = FAcRayShapes::Get().Find(&Model);
			if (!Shape || Shape->Empty() || PartWorld.Num() != Model.Parts.Num()) return;
			std::vector<FAcMat4> M(PartWorld.Num());
			std::vector<uint8_t> Visible(PartWorld.Num(), 1);
			for (int32 P = 0; P < PartWorld.Num(); ++P)
			{
				M[P] = AcRaySpace::Matrix(PartWorld[P]);
				const int32 Parent = Model.Parts[P].Parent;
				Visible[P] = (Pose.Visible.IsValidIndex(P) ? Pose.Visible[P] : true) && (Parent == INDEX_NONE || Visible[Parent]);
			}
			// Far enough past the box's entry to cross the whole model.
			const double Len = T + 40.0;
			const FAcF3 A{float(Ray.origin.x), float(Ray.origin.y), float(Ray.origin.z)};
			const FAcF3 B{float(Ray.origin.x + Ray.direction.x * Len), float(Ray.origin.y + Ray.direction.y * Len),
				float(Ray.origin.z + Ray.direction.z * Len)};
			if (const std::optional<float> Hit = Shape->Hit(M.data(), Visible.data(), A, B, 1.f)) Out = double(*Hit) * Len;
			else if (Mode == 2) Out = std::nullopt;
		});
		return Out;
	};
}

void AAcPilotPawn::SetupPlayerInputComponent(UInputComponent* InputComponent)
{
	Super::SetupPlayerInputComponent(InputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input) return;
	auto Bind = [&](const TCHAR* Name, const ETriggerEvent Event, void (AAcPilotPawn::*Handler)(const FInputActionValue&))
	{
		if (const TObjectPtr<UInputAction>* A = Actions.Find(Name)) Input->BindAction(*A, Event, this, Handler);
	};
	Bind(TEXT("IA_PilotMove"), ETriggerEvent::Triggered, &AAcPilotPawn::OnMove);
	Bind(TEXT("IA_PilotMove"), ETriggerEvent::Completed, &AAcPilotPawn::OnMoveEnd);
	Bind(TEXT("IA_PilotLook"), ETriggerEvent::Triggered, &AAcPilotPawn::OnLook);
	Bind(TEXT("IA_PilotAct"), ETriggerEvent::Started, &AAcPilotPawn::OnActStart);
	Bind(TEXT("IA_PilotAct"), ETriggerEvent::Completed, &AAcPilotPawn::OnActEnd);
	Bind(TEXT("IA_PilotAbility"), ETriggerEvent::Started, &AAcPilotPawn::OnAbilityStart);
	Bind(TEXT("IA_PilotAbility"), ETriggerEvent::Completed, &AAcPilotPawn::OnAbilityEnd);
	Bind(TEXT("IA_PilotView"), ETriggerEvent::Started, &AAcPilotPawn::OnView);
	Bind(TEXT("IA_PilotNext"), ETriggerEvent::Started, &AAcPilotPawn::OnNext);
	Bind(TEXT("IA_PilotLeave"), ETriggerEvent::Started, &AAcPilotPawn::OnLeave);
	Bind(TEXT("IA_PilotKey"), ETriggerEvent::Started, &AAcPilotPawn::OnKeyAction);
	Bind(TEXT("IA_PilotFree"), ETriggerEvent::Started, &AAcPilotPawn::OnFreeStart);
	Bind(TEXT("IA_PilotFree"), ETriggerEvent::Completed, &AAcPilotPawn::OnFreeEnd);
}

void AAcPilotPawn::OnMove(const FInputActionValue& V) { MoveInput = V.Get<FVector2D>(); }
void AAcPilotPawn::OnMoveEnd(const FInputActionValue&) { MoveInput = FVector2D::ZeroVector; }

void AAcPilotPawn::OnLook(const FInputActionValue& V)
{
	// A scripted drive ignores the real mouse (headless capture warps it).
	if (!bCaptured || !ScriptKind.IsEmpty()) return;
	// Unreal's mouse Y grows up; Swift's deltaY grows down. Unreal's deltas
	// are raw counts, without the acceleration macOS gave Swift's points: a
	// base gain plus up to as much again for a fast flick (a 180 at full
	// speed is a short flick, a slow pan stays fine).
	const FVector2D Raw = V.Get<FVector2D>();
	const float Dt = FMath::Max(GetWorld() ? GetWorld()->GetDeltaSeconds() : 1.f / 60.f, 1e-3f);
	const double Flick = FMath::Clamp((Raw.Size() / Dt - 300.0) / 1500.0, 0.0, 1.0);
	const double Gain = CVarLookScale.GetValueOnGameThread() * FMath::Clamp(UAcSettings::Get().LookSpeed, 0.25f, 3.f)
		* CVarLookBase.GetValueOnGameThread() * (1.0 + Flick);
	const FVector2D D = Raw * Gain;
	Look(D.X, -D.Y);
}

void AAcPilotPawn::OnActStart(const FInputActionValue&)
{
	// A click with the pointer free (Option held, or the mouse not taken
	// back yet) is the HUD's, never a shot (Swift `mouseDown`: act only
	// while captured; else the first click takes the mouse again). E and
	// Space still act.
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!bCaptured && ScriptKind.IsEmpty() && PC && PC->IsInputKeyDown(EKeys::LeftMouseButton) &&
		!PC->IsInputKeyDown(EKeys::E) && !PC->IsInputKeyDown(EKeys::SpaceBar))
	{
		if (!bPointing && Driving()) SetCapture(true);
		return;
	}
	Press(true);
}
void AAcPilotPawn::OnActEnd(const FInputActionValue&) { Press(false); }
void AAcPilotPawn::OnAbilityStart(const FInputActionValue&)
{
	bAbilityHeld = true;
	AbilityPress();
}
void AAcPilotPawn::OnAbilityEnd(const FInputActionValue&) { bAbilityHeld = false; }
void AAcPilotPawn::OnView(const FInputActionValue&)
{
	if (!OnKey || !OnKey(TEXT("v"))) ToggleView();
}
void AAcPilotPawn::OnNext(const FInputActionValue&) { Next(); }
void AAcPilotPawn::OnLeave(const FInputActionValue&)
{
	// Esc and F first close whatever is open (a menu, a placement). The
	// drive ends after the pointer's tick (F also drives in the top-down
	// view: it must not see this press again).
	if (OnKey && OnKey(TEXT("esc"))) return;
	bLeaveWanted = true;
}
void AAcPilotPawn::OnKeyAction(const FInputActionValue& V)
{
	const int32 K = FMath::RoundToInt(V.Get<float>());
	const FString Key = K == -1 ? TEXT("b") : K == -2 ? TEXT("z") : K == -3 ? TEXT("x") : FString::FromInt(K);
	if (OnKey) OnKey(Key);
}
void AAcPilotPawn::OnFreeStart(const FInputActionValue&)
{
	// Option, with something to point at (pick cards): the pointer out.
	if (!Pointable || !Pointable()) return;
	bPointing = true;
	SetCapture(false);
}
void AAcPilotPawn::OnFreeEnd(const FInputActionValue&)
{
	if (!bPointing) return;
	bPointing = false;
	if (Driving()) SetCapture(true);
}

// MARK: - Take over and leave

bool AAcPilotPawn::DriveNear(const ac::Vec2 Ground)
{
	UAcSimSubsystem* Sim = SimOf(this);
	if (!Sim || !Sim->IsRunning()) return false;
	const ac::Unit* Best = nullptr;
	for (const ac::Unit& U : Sim->State().units)
	{
		if (U.owner != ac::Pilot::player || !ac::Pilot::drivable.contains(U.kind) || U.task == ac::Unit::Task::inBastion
			|| U.task == ac::Unit::Task::aboard) continue;
		if (!Best || ac::distance(U.position, Ground) < ac::distance(Best->position, Ground)) Best = &U;
	}
	return Best && TakeOver(Best->id);
}

bool AAcPilotPawn::TakeOver(const int64 Id)
{
	UAcSimSubsystem* Sim = SimOf(this);
	if (!Sim || !Sim->IsRunning()) return false;
	ac::Simulation& S = Sim->Simulation();
	if (!S.take(Id)) return false;
	const std::optional<ac::Unit> U = S.pilotUnit();
	if (!U) return false;
	const bool bWas = Driving();
	DrivenId = Id;
	// The mouse steers where it looks: a tank's turret, else its heading.
	PilotYaw = U->look();
	S.pilot->heading = PilotYaw;
	Cam.Reset();
	Cam.Pitch = FAcPilotCamera::StartPitch(U->kind);
	Cam.bThirdPerson = bThird && bChaseCamera;
	Chase.Reset();
	GunMark.Reset();
	Cross.Reset();
	bHold = false;
	LastStep.Reset();
	LastLocked.Reset();
	bWasMoving = false;
	if (UAcPointer* Pointer = UAcPointer::Get(this)) Pointer->Select({});

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!bWas)
	{
		StartDive(PC);
		if (PC && PC->GetPawn() != this) PC->Possess(this);
		AddContext(true);
		SetCapture(true);
		ShowSkirt(true);
		Hooks(true);
	}
	ApplyView();
	// The eye on the unit now, not a frame late.
	FAcFrame None;
	Follow(None);
	if (UAcAudioDirector* Audio = UAcAudioDirector::Get(this)) Audio->Rules().Trained(U->kind, U->position);
	UE_LOG(LogAutocraft, Log, TEXT("pilot: driving %s #%lld"), *KindName(U->kind), (long long)Id);
	OnTookOver.Broadcast(Id);
	return true;
}

void AAcPilotPawn::Leave()
{
	bLeaveWanted = false;
	if (!Driving()) return;
	UAcSimSubsystem* Sim = SimOf(this);
	std::optional<ac::Vec2> At;
	if (Sim && Sim->IsRunning())
	{
		if (const std::optional<ac::Unit> U = Sim->Simulation().pilotUnit()) At = U->position;
		Sim->Simulation().take(std::nullopt);
	}
	const int64 Was = DrivenId;
	DrivenId = INDEX_NONE;
	Dive.Reset();
	PullOut.Reset();
	bHold = false;
	bAbilityHeld = false;
	MoveInput = FVector2D::ZeroVector;
	Cross.Reset();
	Hooks(false);
	ShowSkirt(false);
	AddContext(false);
	bCaptured = false;
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (TActorIterator<AAcRtsPawn> It(GetWorld()); It)
		{
			// The RTS pawn sets its own input mode and context on possession.
			PC->Possess(*It);
			if (At) It->CenterOn(*At);
		}
	}
	UE_LOG(LogAutocraft, Log, TEXT("pilot: back to the camera"));
	OnLeft.Broadcast(Was);
}

void AAcPilotPawn::Next()
{
	UAcSimSubsystem* Sim = SimOf(this);
	if (!Driving() || !Sim || !Sim->IsRunning()) return;
	ac::Simulation& S = Sim->Simulation();
	const std::optional<ac::Unit> Me = S.pilotUnit();
	if (!Me) return;
	const ac::Unit* Best = nullptr;
	for (const ac::Unit& U : S.state.units)
	{
		if (U.id == Me->id || U.owner != ac::Pilot::player || U.kind != Me->kind || U.task == ac::Unit::Task::inBastion
			|| U.task == ac::Unit::Task::aboard || U.task == ac::Unit::Task::inDerrick) continue;
		if (!Best || ac::distance(U.position, Me->position) < ac::distance(Best->position, Me->position)) Best = &U;
	}
	if (!Best) return;
	const int64 NextId = Best->id;
	S.take(std::nullopt);
	if (!TakeOver(NextId))
	{
		// It could not be driven after all: back to the one we had.
		if (!TakeOver(Me->id)) Leave();
	}
}

void AAcPilotPawn::Look(const double DX, const double DY)
{
	if (!Driving()) return;
	// Right turns right (heading grows toward the unit's right side).
	PilotYaw = std::remainder(PilotYaw + DX * 0.0032, 2.0 * Pi);
	double Lo = -1.2, Hi = 0.9;
	if (const UAcSimSubsystem* Sim = SimOf(this); Sim && Sim->IsRunning())
	{
		const std::optional<ac::Unit> U = Sim->Simulation().pilotUnit();
		FAcPilotCamera::PitchLimits(U ? U->kind : ac::UnitKind::ranger, Lo, Hi);
	}
	Cam.Pitch = FMath::Clamp(Cam.Pitch - DY * 0.0026, Lo, Hi);
}

void AAcPilotPawn::Press(const bool bDown)
{
	bHold = bDown;
	UAcSimSubsystem* Sim = SimOf(this);
	if (!bDown || !Driving() || !Sim || !Sim->IsRunning() || !Sim->Simulation().pilot) return;
	// A building held out (E8) takes the click instead.
	if (bPlacing)
	{
		bHold = false;
		if (OnKey) OnKey(TEXT("place"));
		return;
	}
	Sim->Simulation().pilot->act = true;
}

void AAcPilotPawn::AbilityPress()
{
	UAcSimSubsystem* Sim = SimOf(this);
	if (!Driving() || !Sim || !Sim->IsRunning() || !Sim->Simulation().pilot) return;
	ac::Simulation& S = Sim->Simulation();
	const std::optional<ac::Unit> U = S.pilotUnit();
	if (!U) return;
	const std::optional<ac::PilotAbility> A = S.pilotAbility(*U);
	if (!A) return;
	S.pilot->ability = true;
	if (!A->action) OnNote.Broadcast(A->why ? UTF8_TO_TCHAR(A->why->c_str()) : TEXT("Can't do that now"));
}

void AAcPilotPawn::ToggleView()
{
	if (!bChaseCamera)
	{
		UE_LOG(LogAutocraft, Log, TEXT("pilot: third person needs the chase camera (E3)"));
		return;
	}
	bThird = !bThird;
	Cam.bThirdPerson = bThird;
	// `thirdPerson.didSet`: nothing eased across the switch (E3).
	Cam.Reset();
	Chase.Reset();
	if (UAcSettings::Remembers())
	{
		UAcSettings::Get().bThirdPerson = bThird;
		UAcSettings::Store();
	}
	ApplyView();
	UE_LOG(LogAutocraft, Log, TEXT("pilot: %s person"), bThird ? TEXT("third") : TEXT("first"));
}

void AAcPilotPawn::ApplyView()
{
	const int64 Hidden = Driving() && !Cam.bThirdPerson && !InDive() ? DrivenId : INDEX_NONE;
	if (UAcWorldRenderer* R = UAcWorldRenderer::Get(this))
	{
		R->SetDrivenUnit(DrivenId);
		R->SetHiddenUnit(Hidden);
		R->SetVeiled(Driving());
	}
	if (AAcLifeBars* Bars = AAcLifeBars::Get(this)) Bars->SetHiddenUnit(Hidden);
	// The hover tip says which view a take-over gets (the one kept).
	if (UAcPointer* Pointer = UAcPointer::Get(this)) Pointer->SetThirdPerson(bThird);
}

void AAcPilotPawn::Hooks(const bool bOn)
{
	if (AAcFog* Fog = AAcFog::Find(GetWorld())) Fog->SetSuppressed(bOn);
	if (UAcAudioDirector* Audio = UAcAudioDirector::Get(this))
	{
		if (bOn)
		{
			TWeakObjectPtr<AAcPilotPawn> Self(this);
			// E7: Swift's `soundBlocked` (ground over the line, a building's
			// box below its roof), not the camera's ray.
			Audio->Blocked = [Self](const FVector& Ears, const FVector& Sound)
			{
				const UAcPilotAudio* Pilot = Self.IsValid() ? UAcPilotAudio::Get(Self.Get()) : nullptr;
				return Pilot && Pilot->Blocked(Ears, Sound);
			};
			Audio->Rules().PilotUnit = DrivenId;
		}
		else
		{
			Audio->SetEars({});
			Audio->Blocked = nullptr;
			Audio->Rules().Heal(false);
			Audio->Rules().PilotUnit.reset();
		}
	}
	if (!bOn)
	{
		if (UAcWorldRenderer* R = UAcWorldRenderer::Get(this))
		{
			R->SetDrivenUnit(INDEX_NONE);
			R->SetHiddenUnit(INDEX_NONE);
			R->SetPilotAim({});
			R->SetVeiled(false);
		}
		// E4: the range ring and the fog veil off, no gun marker.
		if (AAcPilotAids* Aids = AAcPilotAids::Find(GetWorld())) Aids->Hide();
		GunMark.Reset();
		if (AAcLifeBars* Bars = AAcLifeBars::Get(this))
		{
			Bars->ClearPilot();
			Bars->SetHiddenUnit(INDEX_NONE);
		}
		if (UAcEffects* Fx = UAcEffects::Get(this)) Fx->SetPilot(FAcPilotFire());
		if (UAcPointer* Pointer = UAcPointer::Get(this)) Pointer->SetThirdPerson(bThird);
		if (UAcMinimapSubsystem* Minimap = UAcMinimapSubsystem::Get(this)) Minimap->SetSight({});
		if (UAcLampPool* Lamps = UAcLampPool::Get(this)) Lamps->SetCockpitLamp(nullptr);
	}
}

void AAcPilotPawn::AddContext(const bool bPilot)
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Input = Player ? Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Input) return;
	// F means "drive" in one context and "leave" in the other: never both.
	if (bPilot)
	{
		if (RtsContext) Input->RemoveMappingContext(RtsContext);
		if (PilotContext && !Input->HasMappingContext(PilotContext)) Input->AddMappingContext(PilotContext, 1);
	}
	else if (PilotContext)
	{
		Input->RemoveMappingContext(PilotContext);
	}
}

void AAcPilotPawn::SetCapture(const bool bCapture)
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return;
	bCaptured = bCapture;
	if (AcHeadless::Is())
	{
		AcHeadless::FreeMouse(GetWorld()); // headless runs never capture or move the user's mouse
		return;
	}
	if (bCapture)
	{
		// Hide the cursor and pin it, so the mouse only turns the view.
		PC->SetShowMouseCursor(false);
		FInputModeGameOnly Mode;
		Mode.SetConsumeCaptureMouseDown(true);
		PC->SetInputMode(Mode);
	}
	else
	{
		PC->SetShowMouseCursor(true);
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}
}

// MARK: - Each frame

void AAcPilotPawn::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	BindPointer();
	if (!ScriptKind.IsEmpty()) Script(FMath::Min(double(DeltaSeconds), 0.1));
	Steer();
}

void AAcPilotPawn::Steer()
{
	UAcSimSubsystem* Sim = SimOf(this);
	if (!Driving() || !Sim || !Sim->IsRunning()) return;
	ac::Simulation& S = Sim->Simulation();
	if (!S.pilot) return;
	// moveDirection: x right, y down the screen (back).
	S.pilot->walk = ac::Vec2(-MoveInput.Y, MoveInput.X);
	S.pilot->mend = bAbilityHeld;
	if (!MoveInput.IsZero())
	{
		if (const std::optional<ac::Unit> U = S.pilotUnit(); U && U->anchored == true) OnNote.Broadcast(TEXT("Anchored: R for tank mode"));
	}
	CastCrosshair();
	if (Cross)
	{
		S.pilot->crosshair = Cross->Target != INDEX_NONE ? ac::Pilot::Crosshair(ac::Pilot::Crosshair::On{Cross->Target})
		                                                 : ac::Pilot::Crosshair(ac::Pilot::Crosshair::Clear{});
	}
	else
	{
		S.pilot->crosshair = std::nullopt;
	}
	// The driven unit's held guns on the crosshair (E4, `world.pilotAim`).
	if (UAcWorldRenderer* R = UAcWorldRenderer::Get(this)) R->SetPilotAim(Cross ? TOptional<FVector>(Cross->Point) : TOptional<FVector>());
	// First person: where the mouse says; third person (E3): toward what the
	// view's middle is on (`pilotFacing`).
	if (const std::optional<ac::Unit> U = S.pilotUnit()) S.pilot->heading = AcPilotAim::Facing(*U, PilotYaw, AimPoint());
	else S.pilot->heading = PilotYaw;
	S.pilot->hold = bHold && !bPlacing;
}

void AAcPilotPawn::CastCrosshair()
{
	// What the middle of the view is on, as a shooter's ray finds it, out
	// to 60 cells; past that the sky (the point 60 out, nothing). The
	// driven unit never counts.
	if (StageCross)
	{
		// A staged jump: the crosshair cast from the edge (`StageTick`).
		Cross = StageCross;
		return;
	}
	UAcRaysSubsystem* Rays = UAcRaysSubsystem::Get(this);
	if (!LastEye || !Rays)
	{
		Cross.Reset();
		return;
	}
	const FVector O = *LastEye;
	const FVector D = LastRotation.GetForwardVector();
	// Third person (E3): nothing between the camera and the driven unit counts.
	const double Near = Cam.bThirdPerson && Chase.Eye()
		? FMath::Max(AcSpace::ToCm(0.3), FVector::DotProduct(*Chase.Eye() - O, D)) : AcSpace::ToCm(0.05);
	const FVector A = O + D * Near, B = O + D * AcSpace::ToCm(60.0);
	FCrosshair C;
	if (const TOptional<FAcRayHit> H = Rays->SightHit(A, B, DrivenId))
	{
		C.Point = H->Point;
		C.Target = H->Id.IsSet() ? *H->Id : INDEX_NONE;
	}
	else
	{
		C.Point = B;
	}
	Cross = C;
}

TOptional<FVector> AAcPilotPawn::AimPoint() const
{
	// `aimPoint`: third person only, once the chase camera has an eye.
	if (!Cam.bThirdPerson || !Chase.Eye() || !Cross) return {};
	return Cross->Point;
}

FTransform AAcPilotPawn::RootOf(const ac::Unit& U) const
{
	TOptional<FTransform> Root;
	bool bHidden = false;
	if (const UAcWorldRenderer* R = UAcWorldRenderer::Get(this))
	{
		R->ForEachObject([&](const int64 Id, const bool bUnit, const FAcModelInfo&, const FAcPose& Pose, TConstArrayView<FTransform>)
		{
			if (!bUnit || Id != U.id || Pose.Local.Num() == 0) return;
			Root = Pose.Local[0] * Pose.Placement;
			bHidden = Pose.bHidden;
		});
	}
	if (!Root)
	{
		Root = FTransform(AcSpace::QuatFromHeading(U.heading), AcSpace::ToWorld(U.position, GroundAt(GetWorld(), U.position)));
		bHidden = true;
	}
	// A hidden unit is not posed: put back what its pose lifts the root by
	// (the Comet's jump arc and run bob), as the Swift root has it.
	if (bHidden && U.kind == ac::UnitKind::comet)
	{
		const std::optional<double> J = U.jump();
		const double Lift = J ? 1.6 * std::sin(Pi * FMath::Clamp(*J, 0.0, 1.0)) : 0.0;
		Root->AddToTranslation(FVector(0, 0, AcSpace::ToCm(Lift + FAcPilotCamera::Bob(U))));
	}
	// A flyer just out of its Spacedock still rises to its hover (`liftOff`).
	if (bHidden && ac::Rules::stats(U.kind).air)
	{
		UAcWorldRenderer* R = UAcWorldRenderer::Get(this);
		const FAcUnitMemory* Memory = R ? R->Memory(U.id) : nullptr;
		const UAcSimSubsystem* Sim = SimOf(this);
		if (Memory && Memory->BornAt && Sim)
		{
			const double Hover = U.kind == ac::UnitKind::dropship ? AcPoseAir::DropshipHover : AcPoseAir::KestrelHover;
			if (const TOptional<double> Drop = AcPoseAir::LaunchDrop(Sim->Clock() - *Memory->BornAt, Hover))
			{
				Root->AddToTranslation(FVector(0, 0, -AcSpace::ToCm(*Drop)));
			}
		}
	}
	return *Root;
}

void AAcPilotPawn::OnFrame(const FAcFrame& Frame)
{
	Clock += Frame.RealDelta;
	if (!ScriptKind.IsEmpty() && Driving())
	{
		// The scripted drive logs the fire around the driven unit.
		for (const ac::GameEvent& E : Frame.Events)
		{
			if (const auto* Shot = E.as<ac::GameEvent::Shot>(); Shot && (Shot->unit == DrivenId || Shot->target == DrivenId))
			{
				UE_LOG(LogAutocraft, Log, TEXT("pilot: shot #%lld -> #%lld"), (long long)Shot->unit, (long long)Shot->target);
				if (Shot->unit == DrivenId && Stage == EStage::Act) bStageFired = true;
			}
			if (const auto* Miss = E.as<ac::GameEvent::Missed>(); Miss && Miss->unit == DrivenId)
			{
				UE_LOG(LogAutocraft, Log, TEXT("pilot: missed #%lld at %.2f,%.2f"), (long long)Miss->unit, Miss->at.x, Miss->at.y);
				if (Stage == EStage::Act) bStageFired = true;
			}
		}
	}
	// The console's view switch lights the view kept for the next ride,
	// driving or not (`hud.showView(third: pilotThird)`), and a click on
	// it is V.
	if (const UAcConsoleSubsystem* Consoles = UAcConsoleSubsystem::Get(this))
	{
		if (const TSharedPtr<SAcConsole> Console = Consoles->Console())
		{
			Console->SetView(bThird);
			if (Console != ViewSwitchBound.Pin())
			{
				ViewSwitchBound = Console;
				Console->SetOnViewSwitch(FAcOnViewSwitch::CreateWeakLambda(this, [this] { ToggleView(); }));
			}
		}
	}
	if (bLeaveWanted)
	{
		Leave();
		return;
	}
	Follow(Frame);
}

void AAcPilotPawn::Follow(const FAcFrame& Frame)
{
	if (!Driving()) return;
	UAcSimSubsystem* Sim = SimOf(this);
	if (!Sim || !Sim->IsRunning()) return;
	ac::Simulation& S = Sim->Simulation();
	const std::optional<ac::Unit> Found = S.pilotUnit();
	if (!Found || Found->id != DrivenId)
	{
		UE_LOG(LogAutocraft, Log, TEXT("pilot: the unit is gone"));
		Leave();
		return;
	}
	ac::Unit U = *Found;
	Cam.ChaseYaw = PilotYaw;
	if (ac::Pilot::steers(U.kind) && !Cam.bThirdPerson)
	{
		// A vehicle seen from inside: a turret's view follows the mouse (the
		// gun catches up); a hull-mounted weapon's view stays on the hull.
		if (ac::Pilot::turretRate(U.kind)) U.aim = PilotYaw;
		else PilotYaw = U.heading;
	}
	if (const UGameViewportClient* VC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		FVector2D Size;
		VC->GetViewportSize(Size);
		if (Size.X > 0 && Size.Y > 0) CameraComponent->SetFieldOfView(float(FAcPilotCamera::HorizontalFov(Size.X / Size.Y)));
	}
	FVector At;
	FQuat Rot;
	const FTransform Root = RootOf(U);
	if (Cam.bThirdPerson)
	{
		// E3: the chase camera over the right shoulder (AcPilotChase.h).
		const UAcWorldRenderer* R = UAcWorldRenderer::Get(this);
		Chase.Follow(GetWorld(), Root, U, R ? R->ModelOf(U.id) : nullptr, Cam.Pitch, Cam.ChaseYaw, FPlatformTime::Seconds(), At, Rot);
	}
	else
	{
		Chase.Reset();
		Cam.Follow(Root, U, FPlatformTime::Seconds(), At, Rot);
	}
	// A Quake stomp near the driven unit shakes the view a little (the
	// pitch and yaw, a degree at most).
	if (const UAcEffects* Fx = UAcEffects::Get(this))
	{
		const double Shake = Fx->StompShake(U.position, 24.0);
		if (Shake != 0.0) Rot = Rot * FRotator(float(0.6 * Shake), float(0.4 * Shake), 0.f).Quaternion();
	}
	// The dive: the camera on its way into the eye; the crosshair, the aim
	// and the cockpit stay the eye's.
	FVector View = At;
	FQuat ViewRot = Rot;
	if (Dive) DiveView(View, ViewRot);
	if (PullOut) PullOutView(View, ViewRot, Root.GetTranslation());
	SetActorLocationAndRotation(View, ViewRot);
	LastEye = At;
	LastRotation = Rot;
	Cam.Track(U, S.state.time);

	// E4: the range ring and the fog veil round the unit; the gun marker.
	// (The unit as the sim has it: its turret where it really points, not
	// where the view follows the mouse.)
	if (AAcPilotAids* Aids = AAcPilotAids::SpawnFor(GetWorld())) Aids->Update(PulledOut() ? TOptional<FAcRangeCue>() : AcPilotAim::RangeCue(S, *Found), Root.GetTranslation());
	GunMark = AcPilotAim::GunMarker(S, *Found, At, Rot, FAcPilotCamera::FovDegrees, Chase.Eye() ? *Chase.Eye() : At, AimPoint());

	// The hooks of the other chunks.
	const std::optional<ac::PilotSight> Sight = S.pilotSight(U);
	if (AAcLifeBars* Bars = AAcLifeBars::Get(this)) Bars->SetPilot(At, Sight ? Sight->target : INDEX_NONE);
	if (UAcEffects* Fx = UAcEffects::Get(this))
	{
		FAcPilotFire F;
		F.HiddenUnit = Cam.bThirdPerson || InDive() ? INDEX_NONE : DrivenId;
		F.DrivenUnit = DrivenId;
		if (Cross && Cross->Target != INDEX_NONE)
		{
			F.Aim = Cross->Point;
			F.AimOn = Cross->Target;
		}
		// E7: a miss is spent at the sight (`sightPoint(reach:)`), from the
		// unit's eye in third person.
		F.SightPoint = AcPilotAudio::SightPoint(At, Rot.Vector(), ChaseEye(), Cross ? TOptional<FVector>(Cross->Point) : TOptional<FVector>(),
			AcSpace::ToCm(S.weapon(U).range + ac::Rules::radius(U.kind)));
		Fx->SetPilot(F);
	}
	if (UAcAudioDirector* Audio = UAcAudioDirector::Get(this))
	{
		// The ears go with the eye, facing where the unit looks (in a dive,
		// with the camera).
		FAcEars Ears;
		Ears.At = View;
		Ears.Forward = ViewRot.GetForwardVector();
		Ears.Up = ViewRot.GetUpVector();
		Audio->SetEars(Ears);
	}
	Sounds(U);
	// The headlamp rides on the cockpit (`NightLamps.fit(cockpit:kind:)`);
	// third person, the model carries its own.
	if (UAcLampPool* Lamps = UAcLampPool::Get(this))
	{
		const FTransform Eye(Rot, At);
		Lamps->SetCockpitLamp(Cam.bThirdPerson || InDive() ? nullptr : &Eye, U.kind == ac::UnitKind::prospector);
	}
	// The sight wedge on the minimaps (`HUD.showPilot`: the way it looks).
	if (UAcMinimapSubsystem* Minimap = UAcMinimapSubsystem::Get(this))
	{
		Minimap->SetSight(TPair<ac::Vec2, double>(U.position, U.look()));
	}

	// -AcPilotStage: a gun on foot aims at its target's chest, a few rounds
	// (the eye moves as the view turns).
	if (ScriptAimTarget && ScriptAimRounds > 0)
	{
		if (const std::optional<ac::Unit> V = S.state.unit(*ScriptAimTarget))
		{
			AimAt(AcSpace::ToWorld(V->position, GroundAt(GetWorld(), V->position) + ChestHeight(V->kind)));
		}
		if (--ScriptAimRounds == 0) ScriptAimTarget.Reset();
	}
}

void AAcPilotPawn::Sounds(const ac::Unit& U)
{
	UAcAudioDirector* Audio = UAcAudioDirector::Get(this);
	const UAcSimSubsystem* Sim = SimOf(this);
	if (!Audio || !Sim) return;
	FAcAudioRules& Rules = Audio->Rules();
	const ac::Simulation& S = Sim->Simulation();
	// The cockpit sways with sin(stride · 3.2): a step at each crossing.
	const int64 Step = int64(std::floor(U.stride * 3.2 / Pi));
	if (U.stats().bio && LastStep && Step != *LastStep) Rules.Step();
	LastStep = Step;
	const bool bMoving = U.moving == true;
	if (bMoving && !bWasMoving) Rules.PullAway(U.kind);
	bWasMoving = bMoving;
	TOptional<int64> Locked;
	if (const std::optional<ac::PilotSight> Sight = S.pilotSight(U); Sight && Sight->inRange) Locked = Sight->target;
	if (Locked && Locked != LastLocked) Rules.LockOn();
	LastLocked = Locked;
	if (U.stats().bio && U.hp > 0 && U.hp < S.maxHP(U) / 3 && Clock - LastBreath > 4.8)
	{
		LastBreath = Clock;
		Rules.Breath();
	}
	// The heal beam's loop while a healer works on a patient (`PilotFrame.patient`).
	const bool bPatient = U.kind == ac::UnitKind::dropship && U.task == ac::Unit::Task::attacking && U.target
		&& S.state.unit(*U.target).has_value();
	Rules.Heal(bPatient);
}

void AAcPilotPawn::ShowSkirt(const bool bShow)
{
	if (bShow) MakeSkirt();
	Skirt->SetVisibility(bShow);
}

void AAcPilotPawn::MakeSkirt()
{
	// A wide plain just below the map's lowest edge, so the ground runs on
	// into the haze instead of stopping at the map's border.
	const UAcSimSubsystem* Sim = SimOf(this);
	if (!Sim || !Sim->IsRunning() || SkirtMap == &Sim->Map()) return;
	SkirtMap = &Sim->Map();
	const ac::GroundRect& B = Sim->Map().bounds;
	double Low = TNumericLimits<double>::Max();
	for (int32 I = 0; I <= 50; ++I)
	{
		const double T = I / 50.0;
		const ac::Vec2 Edges[4] = {ac::Vec2(B.minX + B.width() * T, B.minZ), ac::Vec2(B.minX + B.width() * T, B.maxZ),
			ac::Vec2(B.minX, B.minZ + B.depth() * T), ac::Vec2(B.maxX, B.minZ + B.depth() * T)};
		for (const ac::Vec2& P : Edges) Low = FMath::Min(Low, GroundAt(GetWorld(), P));
	}
	const ac::Vec2 Mid((B.minX + B.maxX) / 2, (B.minZ + B.maxZ) / 2);
	// The engine plane is 1 m square: 3000 cells across.
	Skirt->SetWorldLocation(AcSpace::ToWorld(Mid, Low - 0.08));
	Skirt->SetWorldRotation(FRotator::ZeroRotator);
	Skirt->SetWorldScale3D(FVector(3000.0, 3000.0, 1.0));
	if (!SkirtMaterial)
	{
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			SkirtMaterial = UMaterialInstanceDynamic::Create(Base, this);
			SkirtMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(FColor(61, 51, 41)));
			Skirt->SetMaterial(0, SkirtMaterial);
		}
	}
}

// MARK: - The scripted drive (-AcPilot=KIND)

/// -AcPilotTurn=X: the scripted left/right keys turn X times as fast (a test aid).
static double ScriptTurn()
{
	static const double Gain = []{ double G = 1.0; FParse::Value(FCommandLine::Get(), TEXT("AcPilotTurn="), G); return G; }();
	return Gain;
}

void AAcPilotPawn::Script(const double Dt)
{
	UAcSimSubsystem* Sim = SimOf(this);
	if (!Sim || !Sim->IsRunning()) return;
	if (!bScriptStarted)
	{
		// Wait for the renderer and the rays to have the world.
		if (++ScriptFrames < 20) return;
		// -AcPilotDiveAt: the recording shows the top-down view first
		// (an -AcPilotAt unit already on the map).
		if (ScriptDiveAt >= 0)
		{
			FString Where;
			const std::optional<ac::UnitKind> Kind = ac::parse<ac::UnitKind>(TCHAR_TO_UTF8(*ScriptKind.ToLower()));
			if (PlacedId == INDEX_NONE && Kind && FParse::Value(FCommandLine::Get(), TEXT("AcPilotAt="), Where, false)) PlacedId = PlaceAt(*Kind, Where);
			const UAcShotSubsystem* Recorder = UAcShotSubsystem::Get(this);
			const TOptional<double> Rolling = Recorder ? Recorder->GetRecordStart() : TOptional<double>();
			if (!Rolling || GetWorld()->GetTimeSeconds() - *Rolling < ScriptDiveAt) return;
		}
		bScriptStarted = true;
		if (!StageScripted())
		{
			UE_LOG(LogAutocraft, Error, TEXT("pilot: -AcPilot=%s: nothing to drive"), *ScriptKind);
			ScriptKind.Reset();
			if (bShotHeld)
			{
				if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(this)) Shot->Release();
				bShotHeld = false;
			}
			return;
		}
		ScriptStart = 0;
		ScriptLogAt = 0;
		PathIndex = 0;
		PathAt = 0;
		return;
	}
	// A staged still: settle, act, then hold (AcPilotStage.cpp); the shot
	// waits for it.
	const bool bStaging = bScriptStage && !StageTick(*Sim);
	// Seconds since the take over (frame time, clamped as the sim's; a
	// staged still is paused).
	ScriptStart += Dt;
	const double Since = ScriptStart;
	// -AcPilotPullOut: the camera leaves the unit for the gas giant.
	if (ScriptPullOutAt >= 0 && !PullOut && Since >= ScriptPullOutAt && Driving())
	{
		FPullOut P;
		P.Start = GetWorld()->GetUnpausedTimeSeconds();
		P.Seconds = ScriptPullOutFor;
		PullOut = P;
		UE_LOG(LogAutocraft, Log, TEXT("pilot: pull-out at t=%.2f over %.2f s"), Since, P.Seconds);
	}

	// The path: keys held for their seconds (game time).
	// (-AcPilotLive: a staged still runs its path once the staging is over.)
	// A recording (-AcShotRecord) starts the path on its first frame.
	const UAcShotSubsystem* Recorder = UAcShotSubsystem::IsRecordRun() ? UAcShotSubsystem::Get(this) : nullptr;
	const bool bRolling = !Recorder || Recorder->GetRecordStart().IsSet();
	if (bRolling && (!bScriptStage || (!bStaging && FParse::Param(FCommandLine::Get(), TEXT("AcPilotLive")))) && PathIndex < Path.Num())
	{
		const FStep& Step = Path[PathIndex];
		TArray<FString> Keys;
		Step.Key.ParseIntoArray(Keys, TEXT("+"));
		const bool bFirst = PathAt == 0;
		FVector2D Move = FVector2D::ZeroVector;
		for (const FString& K : Keys)
		{
			if (K == TEXT("w")) Move.Y -= 1;
			else if (K == TEXT("s")) Move.Y += 1;
			else if (K == TEXT("a")) Move.X -= 1;
			else if (K == TEXT("d")) Move.X += 1;
			else if (K == TEXT("right")) Look(0.6 / 0.0032 * Dt * ScriptTurn(), 0);
			else if (K == TEXT("left")) Look(-0.6 / 0.0032 * Dt * ScriptTurn(), 0);
			else if (K == TEXT("up")) Look(0, -0.4 / 0.0026 * Dt);
			else if (K == TEXT("down")) Look(0, 0.4 / 0.0026 * Dt);
			else if (K == TEXT("act") && bFirst) Press(true);
			else if (K == TEXT("ability") && bFirst) AbilityPress();
			else if (K == TEXT("next") && bFirst) Next();
			else if (K == TEXT("view") && bFirst) ToggleView();
			else if (K == TEXT("leave") && bFirst) bLeaveWanted = true;
			else if (K == TEXT("aim")) ScriptAimNearest();
		}
		MoveInput = Move;
		PathAt += Dt;
		if (PathAt >= Step.Seconds)
		{
			if (Keys.Contains(TEXT("act"))) Press(false);
			++PathIndex;
			PathAt = 0;
			MoveInput = FVector2D::ZeroVector;
		}
	}
	if (Since >= ScriptLogAt)
	{
		ScriptLogAt += 0.5;
		if (const std::optional<ac::Unit> U = Driving() ? Sim->Simulation().pilotUnit() : std::nullopt)
		{
			UE_LOG(LogAutocraft, Log, TEXT("pilot: t=%.2f %s #%lld at %.2f,%.2f heading %.3f look %.3f yaw %.3f pitch %.3f eye %s task %s crosshair %lld"),
				Since, *KindName(U->kind), (long long)U->id, U->position.x, U->position.y, U->heading, U->look(), PilotYaw,
				Cam.Pitch, LastEye ? *LastEye->ToCompactString() : TEXT("-"), *TaskName(U->task),
				Cross ? (long long)Cross->Target : -2ll);
			if (Cam.bThirdPerson || GunMark)
			{
				UE_LOG(LogAutocraft, Log, TEXT("pilot: t=%.2f %s person, facing %.3f, gun marker %s, ring %s"), Since,
					Cam.bThirdPerson ? TEXT("third") : TEXT("first"), Sim->Simulation().pilot ? Sim->Simulation().pilot->heading : 0.0,
					GunMark ? *GunMark->ToString() : TEXT("-"),
					AAcPilotAids::Find(GetWorld()) && AAcPilotAids::Find(GetWorld())->Shown()
						? *FString::Printf(TEXT("r %.2f tone %d"), AAcPilotAids::Find(GetWorld())->Shown()->Radius, int32(AAcPilotAids::Find(GetWorld())->Shown()->Tone))
						: TEXT("-"));
			}
		}
		else
		{
			UE_LOG(LogAutocraft, Log, TEXT("pilot: t=%.2f not driving"), Since);
		}
	}
	if (bShotHeld && !bStaging && Since >= ScriptFor + FMath::Max(0.0, StageDoneAt))
	{
		bShotHeld = false;
		if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(this)) Shot->Release();
		UE_LOG(LogAutocraft, Log, TEXT("pilot: shot released at t=%.2f"), Since);
	}
}

bool AAcPilotPawn::StageScripted()
{
	UAcSimSubsystem* Sim = SimOf(this);
	const ac::Simulation& S = Sim->Simulation();
	if (ScriptKind.IsNumeric()) return TakeOver(FCString::Atoi64(*ScriptKind));
	const std::optional<ac::UnitKind> Kind = ac::parse<ac::UnitKind>(TCHAR_TO_UTF8(*ScriptKind.ToLower()));
	if (!Kind) return false;
	if (bScriptStage) return StageFor(*Kind);
	if (FString Where; FParse::Value(FCommandLine::Get(), TEXT("AcPilotAt="), Where, false)) return StageAt(*Kind, Where);
	for (const ac::Unit& U : S.state.units)
	{
		if (U.kind == *Kind && AcPick::Drivable(U, ac::Pilot::player))
		{
			if (!TakeOver(U.id)) return false;
			// -AcPilotYaw: the view turned there first (`drivePlayground`:
			// `pilotYaw = 0`, east); the unit turns to it as the game runs.
			double Yaw = 0;
			if (FParse::Value(FCommandLine::Get(), TEXT("AcPilotYaw="), Yaw))
			{
				PilotYaw = Yaw;
				if (Sim->Simulation().pilot) Sim->Simulation().pilot->heading = Yaw;
			}
			return true;
		}
	}
	return StageFor(*Kind);
}

// StageFor and the staged stills: AcPilotStage.cpp.

// MARK: - The dive

void AAcPilotPawn::StartDive(const APlayerController* PC)
{
	Dive.Reset();
	const double Seconds = CVarDive.GetValueOnGameThread();
	// Stills cut, as before: the picture is the eye's.
	if (Seconds <= 0 || bScriptStage || (UAcShotSubsystem::IsShotRun() && !UAcShotSubsystem::IsRecordRun())) return;
	// Only from the top-down view (not a sheet's or the sky's camera).
	if (!PC || !PC->PlayerCameraManager || !Cast<AAcRtsPawn>(PC->GetViewTarget())) return;
	const FMinimalViewInfo& Was = PC->PlayerCameraManager->GetCameraCacheView();
	FDive D;
	D.From = Was.Location;
	D.FromRot = Was.Rotation.Quaternion();
	// Both cameras keep the horizontal field of view (MaintainXFOV).
	D.FromFov = Was.FOV;
	D.Start = GetWorld()->GetUnpausedTimeSeconds();
	D.Seconds = Seconds;
	Dive = D;
	UE_LOG(LogAutocraft, Log, TEXT("pilot: dive from %s over %.2f s"), *D.From.ToCompactString(), Seconds);
}

void AAcPilotPawn::DiveView(FVector& At, FQuat& Rot)
{
	const double T = FMath::Clamp((GetWorld()->GetUnpausedTimeSeconds() - Dive->Start) / Dive->Seconds, 0.0, 1.0);
	if (T >= 1.0)
	{
		Dive.Reset();
		ApplyView();
		return;
	}
	// Smootherstep: no jolt at either end.
	const double S = T * T * T * (T * (T * 6.0 - 15.0) + 10.0);
	const FVector Pos = FMath::Lerp(Dive->From, At, S);
	if (!Dive->bInside && (FVector::Dist(Pos, At) < AcSpace::ToCm(2.5) || T > 0.8))
	{
		// In the unit now: the model off, the cockpit on (first person).
		Dive->bInside = true;
		ApplyView();
	}
	// The camera turns onto the unit first (it stays in the picture on the
	// way down), then levels into the eye's own view at the end.
	const FVector ToEye = At - Pos;
	const FQuat OnUnit = ToEye.Size() > AcSpace::ToCm(0.5) ? ToEye.Rotation().Quaternion() : Rot;
	const FQuat Turned = FQuat::Slerp(Dive->FromRot, OnUnit, FMath::SmoothStep(0.0, 0.35, T));
	At = Pos;
	Rot = FQuat::Slerp(Turned, Rot, FMath::SmoothStep(0.55, 1.0, T));
	CameraComponent->SetFieldOfView(FMath::Lerp(Dive->FromFov, CameraComponent->FieldOfView, float(S)));
}

void AAcPilotPawn::PullOutView(FVector& At, FQuat& Rot, const FVector& Body)
{
	// From the eye up and back from the unit, away from the gas giant,
	// turned to it: the unit low in the middle of the picture, the planet
	// over the horizon. It drifts on slowly once there.
	const double Since = GetWorld()->GetUnpausedTimeSeconds() - PullOut->Start;
	const double T = FMath::Clamp(Since / PullOut->Seconds, 0.0, 1.0);
	const double S = T * T * T * (T * (T * 6.0 - 15.0) + 10.0);
	const FVector Giant = FAcGasGiant::Direction();
	const FVector Back = -FVector(Giant.X, Giant.Y, 0).GetSafeNormal();
	const double Drift = FMath::Max(0.0, Since - PullOut->Seconds);
	const double Away = AcSpace::ToCm(14 + 0.6 * Drift), Up = AcSpace::ToCm(4 + 0.2 * Drift);
	const FVector End = Body + Back * Away + FVector::UpVector * Up;
	const FVector Pos = FMath::Lerp(At, End, S);
	if (!PullOut->bOutside && (FVector::Dist(Pos, At) > AcSpace::ToCm(2.5) || T > 0.2))
	{
		// Out of the unit: the model on, the cockpit and the HUD off (the
		// recording's whole UI too: the end of a video).
		PullOut->bOutside = true;
		ApplyView();
		if (UAcShotSubsystem* Shot = UAcShotSubsystem::Get(this)) Shot->SetShowUI(false);
		UE_LOG(LogAutocraft, Log, TEXT("pilot: pull-out: body %s, giant %s, to %s"), *Body.ToCompactString(), *Giant.ToCompactString(), *End.ToCompactString());
	}
	// Toward the giant, pitched so the unit sits 19 degrees under the middle.
	FRotator Look = Giant.Rotation();
	Look.Pitch = float(FMath::RadiansToDegrees(std::atan2(-Up, Away))) + 19.f;
	At = Pos;
	Rot = FQuat::Slerp(Rot, Look.Quaternion(), FMath::SmoothStep(0.0, 0.85, T));
}

void AAcPilotPawn::ScriptAimNearest()
{
	const UAcSimSubsystem* Sim = SimOf(this);
	const std::optional<ac::Unit> Me = Sim && Driving() ? Sim->Simulation().pilotUnit() : std::nullopt;
	if (!Me) return;
	const ac::GameState& St = Sim->Simulation().state;
	const ac::Unit* Best = nullptr;
	for (const ac::Unit& U : St.units)
	{
		if (!St.hostile(U.owner, Me->owner) || U.task == ac::Unit::Task::aboard || U.task == ac::Unit::Task::inBastion
			|| U.task == ac::Unit::Task::inDerrick || ac::distance(U.position, Me->position) > 25) continue;
		if (!Best || ac::distance(U.position, Me->position) < ac::distance(Best->position, Me->position)) Best = &U;
	}
	// `Follow` turns the view onto its chest this frame (the target may walk).
	if (Best)
	{
		ScriptAimTarget = Best->id;
		ScriptAimRounds = 1;
	}
}

void AAcPilotPawn::AimAt(const FVector& Target)
{
	if (!LastEye) return;
	const FVector D = Target - *LastEye;
	// World radians as `heading` (UE yaw = +heading).
	PilotYaw = std::atan2(D.Y, D.X);
	Cam.Pitch = std::atan2(D.Z, FVector2D(D.X, D.Y).Size());
	UAcSimSubsystem* Sim = SimOf(this);
	if (!Sim) return;
	// Each of Swift's rounds then steers and steps: the unit turns to
	// `pilotFacing`, cast from the eye as last followed (third person:
	// toward what the view's middle is on; first person: the yaw). The
	// still is paused, so the turn is set here.
	CastCrosshair();
	double Facing = PilotYaw;
	if (const std::optional<ac::Unit> Me = Sim->Simulation().pilotUnit()) Facing = AcPilotAim::Facing(*Me, PilotYaw, AimPoint());
	for (ac::Unit& U : Sim->Simulation().state.units)
	{
		if (U.id == DrivenId) { U.heading = Facing; U.aim.reset(); }
	}
	if (Sim->Simulation().pilot) Sim->Simulation().pilot->heading = Facing;
}

// MARK: - Console

static FAutoConsoleCommandWithWorldAndArgs GPilotCommand(TEXT("ac.Pilot"),
	TEXT("ac.Pilot KIND|ID|off: drive the player's first unit of a kind (or an id), or leave."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AAcPilotPawn* Pawn = AAcPilotPawn::Find(World);
		UAcSimSubsystem* Sim = UAcSimSubsystem::Get(World);
		if (!Pawn || !Sim || !Sim->IsRunning() || Args.Num() == 0) return;
		if (Args[0] == TEXT("off")) { Pawn->Leave(); return; }
		if (Args[0].IsNumeric()) { Pawn->TakeOver(FCString::Atoi64(*Args[0])); return; }
		const std::optional<ac::UnitKind> Kind = ac::parse<ac::UnitKind>(TCHAR_TO_UTF8(*Args[0].ToLower()));
		if (!Kind) return;
		for (const ac::Unit& U : Sim->State().units)
		{
			if (U.kind == *Kind && AcPick::Drivable(U, ac::Pilot::player)) { Pawn->TakeOver(U.id); return; }
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GPilotLookCommand(TEXT("ac.PilotLook"),
	TEXT("ac.PilotLook DX DY: turn the driven unit's view as the mouse would (points; DY down)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (AAcPilotPawn* Pawn = AAcPilotPawn::Find(World); Pawn && Args.Num() >= 2)
		{
			Pawn->Look(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]));
		}
	}));
