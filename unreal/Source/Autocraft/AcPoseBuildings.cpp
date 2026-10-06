// Buildings 1 (chunk B6): construction, the light language, and the
// Citadel, Hab Dome, Garrison and Bastion. See AcPoseBuildings.h.
#include "AcPoseBuildings.h"

#include "Rules.h"
#include "Types.h"

namespace AcBuildingPose
{
	FParts::FParts(const TCHAR* InName, const int32 InCount)
	{
		for (int32 K = 0; K < InCount; ++K) Index.Emplace(*FString::Printf(TEXT("%s_%d"), InName, K));
	}

	int32 FParts::Num(const FAcModelInfo& Model) const
	{
		int32 N = 0;
		while (N < Index.Num() && Index[N].Get(Model) != INDEX_NONE) ++N;
		return N;
	}

	int32 FParts::operator()(const FAcModelInfo& Model, const int32 K) const
	{
		return Index.IsValidIndex(K) ? Index[K].Get(Model) : INDEX_NONE;
	}

	void SetEuler(const FAcModelInfo& Model, FAcPose& P, const int32 Part, const double X, const double Y, const double Z)
	{
		if (Part == INDEX_NONE) return;
		// The export merges nodes into their part, so a part's rest can hold
		// a merged parent's turn as well (the Lab's scanner sits in its eye):
		// keep that, swap only the node's own euler angles.
		const FAcModelPart& R = Model.Parts[Part];
		const FQuat Own = SkEuler(R.SkEuler.X, R.SkEuler.Y, R.SkEuler.Z);
		P.Local[Part].SetRotation(R.Rest.GetRotation() * Own.Inverse() * SkEuler(X, Y, Z));
	}

	void SetPosition(FAcPose& P, const int32 Part, const double X, const double Y, const double Z)
	{
		if (Part != INDEX_NONE) P.Local[Part].SetTranslation(AcSpace::FromSceneKit(X, Y, Z));
	}

	void SetScale(FAcPose& P, const int32 Part, const double X, const double Y, const double Z)
	{
		if (Part != INDEX_NONE) P.Local[Part].SetScale3D(AcSpace::AxesFromSceneKit(X, Y, Z));
	}

	void GlowGroup(const FAcModelInfo& Model, FAcPose& P, const FName Material, const double Intensity, const double Exported)
	{
		const float V = float(Intensity / Exported);
		for (int32 I = 0; I < Model.Parts.Num(); ++I)
		{
			const FAcModelPart& Part = Model.Parts[I];
			for (const FAcModelMesh& M : Part.Meshes)
			{
				if (M.Material != Material) continue;
				// A part of that material alone: the part's own emission.
				if (Part.Meshes.Num() == 1)
				{
					P.Emission[I] = V;
				}
				else
				{
					P.MeshEmission.Add({I, Material, V});
				}
				break;
			}
		}
	}

	void ProductionLamps(const FAcModelInfo& Model, FAcPose& P, const FParts& Progress, const FParts& Queue, const bool bOn,
		const double Progress01, const int64 QueueCount, const bool bLive, const double Phase, const double Exported)
	{
		const int32 N = Progress.Num(Model);
		const int32 Filled = int32(Progress01 * double(N));
		for (int32 K = 0; K < N; ++K)
		{
			const bool bLit = bOn && (K < Filled || (K == Filled && std::fmod(Phase, 0.5) < 0.3));
			Lamp(P, Progress(Model, K), bLit ? 1.9 : 0.04, Exported);
		}
		const int32 Q = Queue.Num(Model);
		for (int32 K = 0; K < Q; ++K) Lamp(P, Queue(Model, K), bLive && K < QueueCount ? 2.0 : 0.04, Exported);
	}

	FVector RestWorld(const FAcModelInfo& Model, const FAcPose& P, int32 Part)
	{
		FTransform T = FTransform::Identity;
		for (; Part != INDEX_NONE; Part = Model.Parts[Part].Parent) T = T * P.Local[Part];
		return (T * P.Placement).GetTranslation();
	}

	FVector Site(const FAcPoseContext& C, const FAcPose& P)
	{
		const ac::Structure& S = *C.Structure;
		return P.Placement.GetTranslation() - FVector(0, 0, S.complete() ? 0.0 : AcSpace::ToCm(AcPose::ConstructionSink(S)));
	}

	void Light(const FAcPoseContext& C, FAcPose& P, const int32 Part, const double Intensity)
	{
		if (Part == INDEX_NONE || Intensity <= 0.01) return;
		P.Cues.Add({C.Model.Parts[Part].Name, RestWorld(C.Model, P, Part), float(Intensity)});
	}

	void Emit(const FAcPoseContext& C, FAcPose& P, const FName What, const int32 Part, const double Rate)
	{
		if (Part == INDEX_NONE || Rate <= 0.0) return;
		P.Cues.Add({What, RestWorld(C.Model, P, Part), float(Rate)});
	}

	bool Welding(const FAcPoseContext& C)
	{
		const ac::Structure& S = *C.Structure;
		if (S.complete() || !S.builder) return false;
		for (const ac::Unit& U : C.State.units)
		{
			if (U.id == *S.builder) return U.task == ac::Unit::Task::building;
		}
		return false;
	}

	void Weld(const FAcPoseContext& C, FAcPose& P, const bool bWelding, const double Radius)
	{
		if (!bWelding) return;
		const ac::Structure& S = *C.Structure;
		const double Ph = Phase(C);
		const double Hop = std::floor(Ph / 0.7);
		const double A = Hop * 2.39996;
		const double E = Radius + 0.1;
		const double Height = AcPose::BuildingHeight(S.kind);
		const FVector At = Site(C, P) + AcSpace::FromSceneKit(std::cos(A) * E, S.progress() * Height * 0.95 + 0.15, std::sin(A) * E);
		static const FName WeldCue(TEXT("AcWeld"));
		P.Cues.Add({WeldCue, At, float(30.0 + 25.0 * std::sin(Ph * 43.0) * std::sin(Ph * 17.0))});
		P.bAnimated = true;
	}
}

using namespace AcBuildingPose;

namespace
{
	const FName VentGlow(TEXT("ventGlow")), FloodGlow(TEXT("floodGlow")), MarkerGlow(TEXT("markerGlow")),
		BayGlow(TEXT("bayGlow")), SlitGlow(TEXT("slitGlow"));
	const FName Steam(TEXT("AcSteam"));

	/// The radius `GameScene.makeBuilding` gives the weld (:363-420).
	double WeldRadius(const ac::StructureKind K)
	{
		switch (K)
		{
		case ac::StructureKind::citadel: return 2.5;
		case ac::StructureKind::habDome:
		case ac::StructureKind::lab:
		case ac::StructureKind::sentinel: return 1.0;
		default: return 1.5;
		}
	}

	bool Near(const ac::Vec2 A, const ac::Vec2 B, const double R)
	{
		return ac::distance(A, B) < R;
	}

	/// Construction for every kind, then whether it is complete.
	bool Common(const FAcPoseContext& C, FAcPose& P, const bool bWelding)
	{
		const ac::Structure& S = *C.Structure;
		Weld(C, P, bWelding, WeldRadius(S.kind));
		// Every kind passes here, in parallel: no FAcPartIndex cache (it holds
		// one model at a time), a plain lookup in the model's own map.
		static const FName BeaconName(TEXT("beacon"));
		const bool bLive = S.complete();
		if (const int32* B = C.Model.PartIndex.Find(BeaconName)) P.Visible[*B] = Beacon(C.Time, bLive);
		// Finished buildings blink (the beacon) and glow on and off: drawn every frame.
		if (bLive) P.bAnimated = true;
		return bLive;
	}

	// MARK: Citadel

	void PoseCitadel(const FAcPoseContext& C, FAcPose& P)
	{
		const ac::Structure& S = *C.Structure;
		const FAcModelInfo& M = C.Model;
		const bool bLive = Common(C, P, Welding(C));
		const double Ph = Phase(C);
		const bool bTraining = S.training.has_value();
		static const FParts Progress(TEXT("progressLamps"), 4), Queue(TEXT("queueLamps"), 5), Door(TEXT("doorSegments"), 6);
		static const FPart Dish(TEXT("dish")), DoorLight(TEXT("light_2")), SteamAt(TEXT("emitter_1"));
		ProductionLamps(M, P, Progress, Queue, bTraining, S.trainingProgress().value_or(0.0), S.queueCount(), bLive, Ph, 0.04);
		SetEuler(M, P, Dish.Get(M), 0, bLive ? Ph * 0.5 : 0.0, 0);
		// Training: the hangar strip chases toward the door.
		const int32 N = Door.Num(M);
		if (N > 0)
		{
			const int32 Lead = int32(Ph * 9.0) % N;
			const double DoorGlow = !bLive ? 0.1 : bTraining ? 2.6 : 0.8;
			for (int32 I = 0; I < N; ++I)
			{
				const int32 D = (I - Lead + N) % N;
				const int32 Seg = Door(M, I);
				if (Seg == INDEX_NONE) continue;
				P.Visible[Seg] = !(bTraining && D > 1);
				Lamp(P, Seg, DoorGlow, 2.0);
			}
		}
		GlowGroup(M, P, VentGlow, !bLive ? 0.1 : bTraining ? Glow(Ph, 0) : 1.1 + 0.25 * std::sin(Ph * 1.1), 1.8);
		GlowGroup(M, P, MarkerGlow, !bLive ? 0.1 : bTraining ? Glow(Ph, 0) : 1.2, 2.0);
		GlowGroup(M, P, FloodGlow, !bLive ? 0.1 : bTraining ? Glow(Ph, 0.65) : 1.2, 2.0);
		Emit(C, P, Steam, SteamAt.Get(M), !bLive ? 0.0 : bTraining ? 7.0 : 1.2);
		Light(C, P, DoorLight.Get(M), bTraining ? 32.0 + 18.0 * std::sin(Ph * 5.0) : 0.0);
	}

	// MARK: Hab Dome

	void PoseHabDome(const FAcPoseContext& C, FAcPose& P)
	{
		const bool bLive = Common(C, P, Welding(C));
		const double Ph = Phase(C);
		static const FParts Lights(TEXT("lights"), 4);
		const double Green = bLive ? 0.5 + 0.5 * (0.5 + 0.5 * std::sin(Ph * 2.2)) : 0.05;
		for (int32 K = 0; K < Lights.Num(C.Model); ++K) Lamp(P, Lights(C.Model, K), Green, 0.7);
	}

	// MARK: Garrison

	void PoseGarrison(const FAcPoseContext& C, FAcPose& P)
	{
		const ac::Structure& S = *C.Structure;
		const FAcModelInfo& M = C.Model;
		const bool bLive = Common(C, P, Welding(C));
		const double Ph = Phase(C);
		const bool bTraining = S.training.has_value();
		static const FParts Progress(TEXT("progressLamps"), 4), Queue(TEXT("queueLamps"), 5);
		static const FPart DoorPart(TEXT("door")), DoorLight(TEXT("light_1")), SteamAt(TEXT("emitter_2"));
		ProductionLamps(M, P, Progress, Queue, bTraining, S.trainingProgress().value_or(0.0), S.queueCount(), bLive, Ph, 0.04);

		// The shutter rolls up while a Ranger walks out of the bay.
		bool bLeaving = false;
		if (bLive)
		{
			const ac::Vec2 Door = S.position + ac::Vec2(0, ac::Rules::radius(S.kind) + 0.35);
			for (const ac::Unit& U : C.State.units)
			{
				if (U.soldier() && Near(U.position, Door, 1.1))
				{
					bLeaving = true;
					break;
				}
			}
		}
		FDoorState& Mem = C.Memory.State<FDoorState>();
		const double Want = bLive && (bLeaving || S.trainingProgress().value_or(0.0) > 0.9) ? 0.1 : 1.0;
		const double Scale = Ease(Mem.Value.Get(1.0), Want, 0.09, C.Dt);
		Mem.Value = Scale;
		SetScale(P, DoorPart.Get(M), 1, Scale, 1);
		if (Scale < 0.999) P.bAnimated = true;
		const double Open = (1.0 - Scale) / 0.9;
		GlowGroup(M, P, BayGlow, 0.1 + 2.2 * Open, 0.1);
		Light(C, P, DoorLight.Get(M), 28.0 * Open);
		GlowGroup(M, P, FloodGlow, !bLive ? 0.1 : bTraining ? Glow(Ph, 0.65) : 1.2, 2.0);
		GlowGroup(M, P, VentGlow, !bLive ? 0.1 : bTraining ? Glow(Ph, 0) : 1.1 + 0.25 * std::sin(Ph * 1.1), 1.8);
		Emit(C, P, Steam, SteamAt.Get(M), !bLive ? 0.0 : bTraining ? 5.0 : 0.8);
	}

	// MARK: Bastion

	void PoseBastion(const FAcPoseContext& C, FAcPose& P)
	{
		const ac::Structure& S = *C.Structure;
		const FAcModelInfo& M = C.Model;
		const bool bLive = Common(C, P, Welding(C));
		const double Ph = Phase(C);
		const FBastionState& Mem = C.Memory.State<FBastionState>();
		static const FParts Pips(TEXT("pips"), 6), Flashes(TEXT("flashes"), 5);
		// Green pips count the Rangers inside, one per slot it has room for.
		const int32 Inside = S.crew ? int32(S.crew->size()) : 0;
		for (int32 K = 0; K < Pips.Num(M); ++K)
		{
			const int32 Pip = Pips(M, K);
			P.Visible[Pip] = K < Mem.Room;
			Lamp(P, Pip, bLive && K < Inside ? 1.3 : 0.05, 0.05);
		}
		// The slits glow on and off while it fires, steady while manned.
		bool bFiring = false;
		for (const double T : Mem.Shots) bFiring |= C.Time - T < 1.2;
		GlowGroup(M, P, SlitGlow, !bLive ? 0.05 : bFiring ? Glow(Ph, 0) : Inside > 0 ? 1.2 : 0.15, 1.8);
		for (int32 K = 0; K < Flashes.Num(M); ++K)
		{
			const double Since = C.Time - Mem.Shots[K];
			P.Visible[Flashes(M, K)] = Since >= 0.0 && Since < 0.05;
		}
	}

	FAcPoseRegistration RegCitadel(ac::StructureKind::citadel, &PoseCitadel);
	FAcPoseRegistration RegHabDome(ac::StructureKind::habDome, &PoseHabDome);
	FAcPoseRegistration RegGarrison(ac::StructureKind::garrison, &PoseGarrison);
	FAcPoseRegistration RegBastion(ac::StructureKind::bastion, &PoseBastion);
}

namespace AcBuildingPose
{
	/// For AcPoseBuildings2.cpp: the shared start of every building's pose.
	bool CommonPose(const FAcPoseContext& C, FAcPose& P, const bool bWelding) { return Common(C, P, bWelding); }
}
