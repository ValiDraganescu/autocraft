#include "AcShotScenes.h"

#include "AcLog.h"
#include "AcRtsPawn.h"
#include "AcSimSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Pilot.h"

#include <tuple>

namespace
{
	TArray<FString> Strings(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object && Object->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& V : *Values) Out.Add(V->AsString());
		}
		return Out;
	}

	/// A flag whose value has a comma or a space is quoted, as FParse wants
	/// (`-AcCamAt="-14,-17"`).
	FString Quoted(const FString& Flag)
	{
		FString Key, Value;
		if (!Flag.Split(TEXT("="), &Key, &Value) || Value.StartsWith(TEXT("\""))) return Flag;
		if (!Value.Contains(TEXT(",")) && !Value.Contains(TEXT(" "))) return Flag;
		return FString::Printf(TEXT("%s=\"%s\""), *Key, *Value);
	}

	FString Number(const double V)
	{
		FString S = FString::SanitizeFloat(V);
		if (S.EndsWith(TEXT(".0"))) S.LeftChopInline(2);
		return S;
	}
}

FString AcShotScenes::TablePath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/shots/scenes.json"));
}

TArray<FAcShotScene> AcShotScenes::Load(FString* Error)
{
	TArray<FAcShotScene> Out;
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *TablePath()))
	{
		if (Error) *Error = FString::Printf(TEXT("cannot read %s"), *TablePath());
		return Out;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
	{
		if (Error) *Error = FString::Printf(TEXT("%s is not JSON"), *TablePath());
		return Out;
	}
	const TSharedPtr<FJsonObject>* DefaultsPtr = nullptr;
	Root->TryGetObjectField(TEXT("defaults"), DefaultsPtr);
	const TSharedPtr<FJsonObject> Defaults = DefaultsPtr ? *DefaultsPtr : MakeShared<FJsonObject>();
	const TArray<TSharedPtr<FJsonValue>>* Scenes = nullptr;
	if (!Root->TryGetArrayField(TEXT("scenes"), Scenes)) return Out;

	for (const TSharedPtr<FJsonValue>& Value : *Scenes)
	{
		const TSharedPtr<FJsonObject> S = Value->AsObject();
		if (!S) continue;
		FAcShotScene Scene;
		Scene.Name = S->GetStringField(TEXT("name"));
		auto Field = [&](const TCHAR* Name) -> TSharedPtr<FJsonValue>
		{
			if (S->HasField(Name)) return S->TryGetField(Name);
			return Defaults->TryGetField(Name);
		};
		if (const TSharedPtr<FJsonValue> W = Field(TEXT("width"))) Scene.Width = int32(W->AsNumber());
		if (const TSharedPtr<FJsonValue> H = Field(TEXT("height"))) Scene.Height = int32(H->AsNumber());
		const TSharedPtr<FJsonValue> Ue = S->TryGetField(TEXT("ue"));
		if (!Ue || Ue->IsNull())
		{
			Scene.bStaged = false;
			Out.Add(Scene);
			continue;
		}
		// A playground scene has no map of its own.
		const bool bPlayground = S->HasTypedField<EJson::Boolean>(TEXT("playground")) && S->GetBoolField(TEXT("playground"));
		// The scene's own flags first: a chunk reads the first match.
		for (const TSharedPtr<FJsonValue>& F : Ue->AsArray()) Scene.Flags.Add(F->AsString());
		if (!bPlayground)
		{
			if (const TSharedPtr<FJsonValue> Map = Field(TEXT("map"))) Scene.Flags.Add(TEXT("-AcMap=") + Map->AsString());
		}
		if (const TSharedPtr<FJsonValue> Zoom = Field(TEXT("zoom")); Zoom && !Zoom->IsNull())
		{
			Scene.Flags.Add(TEXT("-AcZoom=") + Number(Zoom->AsNumber()));
		}
		if (const TSharedPtr<FJsonValue> At = Field(TEXT("at")); At && At->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& P = At->AsArray();
			if (P.Num() == 2) Scene.Flags.Add(FString::Printf(TEXT("-AcCamAt=%s,%s"), *Number(P[0]->AsNumber()), *Number(P[1]->AsNumber())));
		}
		if (const TSharedPtr<FJsonValue> Warm = Field(TEXT("warm")); Warm && Warm->AsNumber() > 0)
		{
			Scene.Flags.Add(TEXT("-AcWarm=") + Number(Warm->AsNumber()));
		}
		if (const TSharedPtr<FJsonValue> Still = Field(TEXT("still")); Still && Still->AsBool())
		{
			Scene.Flags.Add(TEXT("-AcPaused"));
		}
		if (const TSharedPtr<FJsonValue> Frames = Field(TEXT("frames")); Frames && Frames->AsNumber() > 0)
		{
			Scene.Flags.Add(FString::Printf(TEXT("-AcShotFrames=%d"), int32(Frames->AsNumber())));
		}
		Scene.Flags.Append(Strings(Defaults, TEXT("ue")));
		Out.Add(Scene);
	}
	return Out;
}

bool UAcShotScenes::ShouldCreateSubsystem(UObject* Outer) const
{
	FString Name;
	return FParse::Value(FCommandLine::Get(), TEXT("AcScene="), Name);
}

void UAcShotScenes::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Wanted;
	FParse::Value(FCommandLine::Get(), TEXT("AcScene="), Wanted);
	FString Error;
	const TArray<FAcShotScene> Scenes = AcShotScenes::Load(&Error);
	if (!Error.IsEmpty())
	{
		UE_LOG(LogAutocraft, Error, TEXT("scene: %s"), *Error);
		return;
	}
	if (Wanted.Equals(TEXT("list"), ESearchCase::IgnoreCase))
	{
		for (const FAcShotScene& S : Scenes)
		{
			UE_LOG(LogAutocraft, Display, TEXT("scene: %s %dx%d%s %s"), *S.Name, S.Width, S.Height,
				S.bStaged ? TEXT("") : TEXT(" (not staged in Unreal)"), *FString::Join(S.Flags, TEXT(" ")));
		}
		return;
	}
	const FAcShotScene* Scene = Scenes.FindByPredicate([&](const FAcShotScene& S) { return S.Name.Equals(Wanted, ESearchCase::IgnoreCase); });
	if (!Scene)
	{
		UE_LOG(LogAutocraft, Error, TEXT("scene: no scene %s in %s (-AcScene=list)"), *Wanted, *AcShotScenes::TablePath());
		return;
	}
	if (!Scene->bStaged)
	{
		UE_LOG(LogAutocraft, Error, TEXT("scene: %s has no Unreal staging yet (see unreal/SHOTS.md)"), *Scene->Name);
		return;
	}
	FString Added;
	for (const FString& Flag : Scene->Flags) Added += TEXT(" ") + Quoted(Flag);
	FCommandLine::Append(*Added);
	UE_LOG(LogAutocraft, Log, TEXT("scene: %s (%dx%d) adds%s"), *Scene->Name, Scene->Width, Scene->Height, *Added);
}

bool UAcShotWarm::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return (WorldType == EWorldType::Game || WorldType == EWorldType::PIE) && FCString::Strifind(FCommandLine::Get(), TEXT("AcWarm=")) != nullptr;
}

void UAcShotWarm::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// The playground warms once its layout is down (`UAcPlaygroundSubsystem::Staged`).
	if (FParse::Param(FCommandLine::Get(), TEXT("AcPlaygroundLayout"))) return;
	UAcSimSubsystem* Sim = InWorld.GetSubsystem<UAcSimSubsystem>();
	if (!Sim) return;
	if (Sim->IsRunning()) Warm();
	else StartedHandle = Sim->OnGameStarted.AddWeakLambda(this, [this](UAcSimSubsystem&) { Warm(); });
}

void UAcShotWarm::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (UAcSimSubsystem* Sim = World->GetSubsystem<UAcSimSubsystem>()) Sim->OnGameStarted.Remove(StartedHandle);
	}
	Super::Deinitialize();
}

void UAcShotWarm::Warm()
{
	if (bWarmed) return;
	UAcSimSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<UAcSimSubsystem>() : nullptr;
	if (!Sim || !Sim->IsRunning()) return;
	bWarmed = true;
	double Seconds = 0;
	FParse::Value(FCommandLine::Get(), TEXT("AcWarm="), Seconds);
	// As `GameController.warm(seconds:)`: whole 1/30 s steps; the events
	// are dropped (Swift draws only the shots of them, long gone by now).
	int32 Steps = 0;
	for (double T = 0; T < Seconds; T += 1.0 / 30.0)
	{
		Sim->Simulation().step(1.0 / 30.0);
		++Steps;
	}
	// -AcRailDemo: a Kestrel of the player's and an enemy Dropship six cells
	// off the Citadel, then 0.4 s of game: the rail gun's first shot, held.
	if (FParse::Param(FCommandLine::Get(), TEXT("AcRailDemo")))
	{
		ac::Simulation& S = Sim->Simulation();
		for (const ac::Structure& B : S.state.structures)
		{
			if (B.owner != ac::Pilot::player || B.kind != ac::StructureKind::citadel) continue;
			const ac::Vec2 Gun = B.position + ac::Vec2(-5, 7), Foe = B.position + ac::Vec2(2, 7);
			for (const auto& [Kind, Owner, At, Heading] : {std::make_tuple(ac::UnitKind::kestrel, ac::Pilot::player, Gun, 0.0),
				std::make_tuple(ac::UnitKind::dropship, int64_t(1), Foe, 3.14159)})
			{
				ac::Unit U = S.freshUnit(Kind, S.state.nextID, Owner, At, Heading);
				S.state.nextID += 1;
				S.state.units.push_back(U);
			}
			if (AAcRtsPawn* Pawn = AAcRtsPawn::Get(this)) Pawn->CenterOn((Gun + Foe) * 0.5);
			break;
		}
		S.lookNow();
		Sim->StepThenPause(12);
	}
	Sim->MarkEdited();
	UE_LOG(LogAutocraft, Log, TEXT("scene: warmed %d steps to t=%.2f"), Steps, Sim->GameTime());
}
