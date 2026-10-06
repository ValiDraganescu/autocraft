// Buildings 2 (chunk B7): the Foundry, Spacedock, Lab, Derrick and
// Sentinel. See AcPoseBuildings.h.
#include "AcPoseBuildings.h"

#include "Rules.h"
#include "Types.h"

#include <algorithm>

using namespace AcBuildingPose;

namespace
{
	const FName FloodGlow(TEXT("floodGlow")), VentGlow(TEXT("ventGlow")), BayGlow(TEXT("bayGlow")), WeldGlow(TEXT("weldGlow")),
		CrateGlow(TEXT("crateGlow")), PodGlow(TEXT("podGlow")), WorkGlowA(TEXT("workGlowA")), WorkGlowB(TEXT("workGlowB")),
		WindowGlow(TEXT("windowGlow")), StatusGlow(TEXT("statusGlow")), ValveGlow(TEXT("valveGlow")), TankGlow(TEXT("tankGlow")),
		ThroatGlow(TEXT("throatGlow")), WorkGlow(TEXT("workGlow"));
	const FName Steam(TEXT("AcSteam")), Vapour(TEXT("AcVapour")), Puffs(TEXT("AcPuffs")), Sparks(TEXT("AcSparks"));

	const FVector& Rest(const FAcModelInfo& M, const int32 Part)
	{
		static const FVector Zero = FVector::ZeroVector;
		return Part == INDEX_NONE ? Zero : M.Parts[Part].SkPosition;
	}

	// MARK: Foundry (Models+Foundry.swift:327)

	void PoseFoundry(const FAcPoseContext& C, FAcPose& P)
	{
		const ac::Structure& S = *C.Structure;
		const FAcModelInfo& M = C.Model;
		const bool bLive = CommonPose(C, P, Welding(C));
		const double Ph = Phase(C);
		const bool bWorking = bLive && S.training.has_value();
		const std::optional<double> Progress01 = S.trainingProgress();
		static const FParts Progress(TEXT("progressLamps"), 4), Queue(TEXT("queueLamps"), 5), Leaves(TEXT("doorLeaves"), 2);
		static const FPart Trolley(TEXT("trolley")), Arm(TEXT("weldArm")), Dish(TEXT("dish")), DoorLight(TEXT("light_2")),
			WeldLight(TEXT("light_4")), BaySparksAt(TEXT("emitter_1")), SparksAt(TEXT("emitter_3")), SteamAt(TEXT("emitter_5"));
		ProductionLamps(M, P, Progress, Queue, bWorking, Progress01.value_or(0.0), S.queueCount(), bLive, Ph, 0.04);

		GlowGroup(M, P, FloodGlow, !bLive ? 0.1 : bWorking ? Glow(Ph, 0.65) : 1.2, 2.0);
		GlowGroup(M, P, VentGlow, !bLive ? 0.1 : bWorking ? Glow(Ph, 0) : 1.1 + 0.25 * std::sin(Ph * 1.1), 1.8);
		Emit(C, P, Steam, SteamAt.Get(M), !bLive ? 0.0 : bWorking ? 6.0 : 0.8);

		// Assembly: the trolley shuttles between weld stations; at each one
		// the arm dips and welds in bursts.
		const double Station = std::floor(Ph / 2.2);
		const double Within = Ph - Station * 2.2;
		const double Id = double(S.id);
		const double Target = bWorking ? 0.42 * std::sin(Station * 2.39996 + Id) : 0.0;
		const double Prev = bWorking ? 0.42 * std::sin((Station - 1.0) * 2.39996 + Id) : 0.0;
		const double Move = Smoothstep(0, 0.6, Within);
		const FVector& TR = Rest(M, Trolley.Get(M));
		SetPosition(P, Trolley.Get(M), 0.18 + Prev + (Target - Prev) * Move, TR.Y, TR.Z);
		const double Dip = bWorking ? Smoothstep(0.6, 0.9, Within) * (1.0 - Smoothstep(1.9, 2.2, Within)) : 0.0;
		const FVector& AR = Rest(M, Arm.Get(M));
		SetPosition(P, Arm.Get(M), AR.X, -0.08 * Dip, AR.Z);
		const bool bArcing = bWorking && Dip > 0.9;
		const double Flicker = 0.5 + 0.5 * std::sin(Ph * 47.0) * std::sin(Ph * 23.0 + 1.3);
		Emit(C, P, Sparks, SparksAt.Get(M), bArcing ? 70.0 : 0.0);
		Emit(C, P, Sparks, BaySparksAt.Get(M), bWorking && std::fmod(Ph + 1.1, 2.2) < 1.1 ? 45.0 : 0.0);
		GlowGroup(M, P, WeldGlow, !bWorking ? 0.05 : bArcing ? 0.6 + 2.2 * Flicker : 0.25 + 0.2 * Flicker, 0.05);
		Light(C, P, WeldLight.Get(M), bArcing ? 6.0 + 14.0 * Flicker : 0.0);
		SetEuler(M, P, Dish.Get(M), 0, bLive ? 0.9 * std::sin(Ph * 0.35) : 0.0, 0);

		// The door leaves slide into the pods while a vehicle rolls out.
		bool bLeaving = false;
		if (bLive)
		{
			const ac::Vec2 Door = S.position + ac::Vec2(0, ac::Rules::radius(S.kind) + 0.8);
			static const std::vector<ac::UnitKind> Trains = ac::Rules::trains(ac::StructureKind::foundry);
			for (const ac::Unit& U : C.State.units)
			{
				if (U.owner == S.owner && ac::distance(U.position, Door) < 1.3
					&& std::find(Trains.begin(), Trains.end(), U.kind) != Trains.end())
				{
					bLeaving = true;
					break;
				}
			}
		}
		constexpr double Travel = 0.64;  // Models.Foundry.doorTravel
		FDoorState& Mem = C.Memory.State<FDoorState>();
		const double Want = bLive && (bLeaving || Progress01.value_or(0.0) > 0.94) ? Travel : 0.0;
		const double Now = FMath::Clamp(Ease(Mem.Value.Get(0.0), Want, 0.05, C.Dt), 0.0, Travel);
		Mem.Value = Now;
		const double Open = Now / Travel;
		for (int32 K = 0; K < Leaves.Num(M); ++K)
		{
			const FVector& LR = Rest(M, Leaves(M, K));
			SetPosition(P, Leaves(M, K), (K == 0 ? -1.0 : 1.0) * (0.31 + Open * Travel), LR.Y, LR.Z);
		}
		GlowGroup(M, P, BayGlow, 0.1 + 2.0 * Open, 0.1);
		Light(C, P, DoorLight.Get(M), 30.0 * Open);
		if (bWorking || Open > 0.0) P.bAnimated = true;
	}

	// MARK: Spacedock (Models+Spacedock.swift:314)

	void PoseSpacedock(const FAcPoseContext& C, FAcPose& P)
	{
		const ac::Structure& S = *C.Structure;
		const FAcModelInfo& M = C.Model;
		const bool bLive = CommonPose(C, P, Welding(C));
		const double Ph = Phase(C);
		const bool bBusy = bLive && S.training.has_value();
		const std::optional<double> Progress01 = S.trainingProgress();
		static const FParts Progress(TEXT("progressLamps"), 4), Queue(TEXT("queueLamps"), 5), Guide(TEXT("guideLamps"), 12),
			Doors(TEXT("padDoors"), 2);
		static const FPart Radar(TEXT("radar")), BayLight(TEXT("light_2")), SteamAt(TEXT("emitter_1"));
		ProductionLamps(M, P, Progress, Queue, bBusy && Progress01.has_value(), Progress01.value_or(0.0), S.queueCount(), bLive, Ph,
			0.04);

		// A ship lifting off the pad (it appears over the middle).
		bool bLeaving = false;
		if (bLive)
		{
			for (const ac::Unit& U : C.State.units)
			{
				if (U.owner == S.owner && U.stats().air && ac::distance(U.position, S.position) < 2.0)
				{
					bLeaving = true;
					break;
				}
			}
		}
		GlowGroup(M, P, CrateGlow, !bLive ? 0.08 : bBusy || bLeaving ? Glow(Ph, 0.65) : 1.2, 2.0);
		GlowGroup(M, P, PodGlow, !bLive ? 0.08 : bBusy || bLeaving ? Glow(Ph, 0) : 1.1 + 0.2 * std::sin(Ph * 1.1), 1.8);
		Emit(C, P, Steam, SteamAt.Get(M), !bLive ? 0.0 : bBusy ? 5.0 : 0.8);

		// Guide lights: a pair chases round the pad while a ship is built;
		// while one lifts off they chase fast and the ring flashes with them.
		const int32 N = Guide.Num(M);
		if (N > 0)
		{
			const double Speed = bLeaving ? 18.0 : 7.0;
			const int32 Lead = int32(std::floor(Ph * Speed)) % N;
			for (int32 I = 0; I < N; ++I)
			{
				const int32 D = (Lead - I + N) % N;
				double E;
				if (!bLive) E = 0.05;
				else if (bLeaving) E = D < 3 ? 2.8 : std::fmod(Ph, 0.4) < 0.2 ? 1.2 : 0.3;
				else if (bBusy) E = D == 0 ? 2.8 : D == 1 ? 1.4 : D == 2 ? 0.6 : 0.15;
				else E = 1.0;
				Lamp(P, Guide(M, I), E, 1.0);
			}
		}

		// The iris doors drop into the drum, then slide apart.
		constexpr double Top = 1.71, Drop = 0.09, Slide = 0.26, PadX = 0.25;
		FDoorState& Mem = C.Memory.State<FDoorState>();
		const double Want = bLive && (bLeaving || Progress01.value_or(0.0) > 0.9) ? 1.0 : 0.0;
		const double Open = FMath::Clamp(Ease(Mem.Value.Get(0.0), Want, 0.07, C.Dt), 0.0, 1.0);
		Mem.Value = Open;
		for (int32 K = 0; K < Doors.Num(M); ++K)
		{
			const FVector& DR = Rest(M, Doors(M, K));
			SetPosition(P, Doors(M, K), PadX + (K == 0 ? -1.0 : 1.0) * Slide * FMath::Max(0.0, Open * 2.0 - 1.0),
				Top - Drop * FMath::Min(1.0, Open * 2.0), DR.Z);
		}
		GlowGroup(M, P, BayGlow, 0.1 + 1.6 * Open, 0.1);
		Light(C, P, BayLight.Get(M), 10.0 * Open + (bLeaving ? 4.0 * std::sin(Ph * 9.0) : 0.0));
		SetEuler(M, P, Radar.Get(M), 0, bLive ? Ph * 1.1 : 0.0, 0);
		if (Open > 0.0) P.bAnimated = true;
	}

	// MARK: Lab (Models+Lab.swift:254; GameScene.animate :447)

	void PoseLab(const FAcPoseContext& C, FAcPose& P)
	{
		const ac::Structure& S = *C.Structure;
		const FAcModelInfo& M = C.Model;
		// Rises while the building it serves stands; works while that one trains.
		const ac::Structure* Parent = nullptr;
		if (S.parent)
		{
			for (const ac::Structure& X : C.State.structures)
			{
				if (X.id == *S.parent)
				{
					Parent = &X;
					break;
				}
			}
		}
		const bool bLive = CommonPose(C, P, !S.complete() && Parent);
		static const FPart Dish(TEXT("dish")), Scanner(TEXT("scanner"));
		if (!bLive)
		{
			GlowGroup(M, P, WorkGlowA, 0.05, 2.0);
			GlowGroup(M, P, WorkGlowB, 0.05, 1.8);
			GlowGroup(M, P, StatusGlow, 0.05, 1.2);
			GlowGroup(M, P, WindowGlow, 0.05, 0.6);
			return;
		}
		const double Ph = Phase(C);
		const bool bWorking = Parent && Parent->training.has_value();
		// Yellow: steady while idle, the two groups alternating while working.
		GlowGroup(M, P, WorkGlowA, bWorking ? Glow(Ph, 0) : 1.2, 2.0);
		GlowGroup(M, P, WorkGlowB, bWorking ? Glow(Ph, 0.65) : 1.2, 1.8);
		// Green: slow breathing status; the window follows it, softer.
		const double Breath = 0.5 + 0.5 * std::sin(Ph * 2.2);
		GlowGroup(M, P, StatusGlow, (0.5 + 0.5 * Breath) * 1.6, 1.2);
		GlowGroup(M, P, WindowGlow, 0.35 + 0.35 * Breath, 0.6);
		// The dish sweeps and the iris spins only while working; at rest the
		// dish drifts slowly back and forth.
		const double Seed = double(S.id % 7) * 0.9;
		SetEuler(M, P, Dish.Get(M), 0, bWorking ? Ph * 2.4 : Seed + 0.5 * std::sin(Ph * 0.35), 0);
		SetEuler(M, P, Scanner.Get(M), 0, 0, bWorking ? -Ph * 3.2 : Seed);
	}

	// MARK: Derrick (Models+Derrick.swift:348; GameScene.animate :470)

	void PoseDerrick(const FAcPoseContext& C, FAcPose& P)
	{
		const ac::Structure& S = *C.Structure;
		const FAcModelInfo& M = C.Model;
		const bool bLive = CommonPose(C, P, Welding(C));
		const double Ph = Phase(C);
		// Working while a Prospector is inside or on its way in; dry once the well is spent.
		int64 Remaining = 0;
		if (C.State.wells)
		{
			for (const ac::Well& W : *C.State.wells)
			{
				if (ac::distance(W.position, S.position) < 0.5)
				{
					Remaining = W.remaining;
					break;
				}
			}
		}
		const bool bEmpty = Remaining <= 0;
		bool bWorking = false;
		for (const ac::Unit& U : C.State.units)
		{
			if ((U.task == ac::Unit::Task::inDerrick || U.task == ac::Unit::Task::toDerrick) && U.structure == S.id)
			{
				bWorking = true;
				break;
			}
		}
		const bool bBusy = bLive && bWorking && !bEmpty;
		static const FParts Pistons(TEXT("pistons"), 2), Valves(TEXT("valves"), 2);
		static const FPart DoorPart(TEXT("door")), BayLight(TEXT("light_1")), SteamAt(TEXT("emitter_2")),
			PuffsAt(TEXT("emitter_3"));

		GlowGroup(M, P, FloodGlow, !bLive ? 0.05 : bBusy ? Glow(Ph, 0) : bEmpty ? 0.15 : 1.2, 2.0);
		GlowGroup(M, P, ValveGlow, !bLive ? 0.05 : bBusy ? Glow(Ph, 0.65) : bEmpty ? 0.15 : 1.2, 1.8);
		// Green: MH in the tanks, breathing; dark once the well is dry.
		GlowGroup(M, P, TankGlow, !bLive || bEmpty ? 0.04 : 0.5 + 0.5 * (0.5 + 0.5 * std::sin(Ph * 2.2)), 0.7);
		GlowGroup(M, P, ThroatGlow, !bLive ? 0.05 : bEmpty ? 0.03 : (bBusy ? 1.5 : 1.1) + 0.25 * std::sin(Ph * 1.7), 1.1);

		// The intake shutter rolls up while Prospectors go in and out.
		FDoorState& Mem = C.Memory.State<FDoorState>();
		const double Scale = Ease(Mem.Value.Get(1.0), bBusy ? 0.12 : 1.0, 0.08, C.Dt);
		Mem.Value = Scale;
		SetScale(P, DoorPart.Get(M), 1, Scale, 1);
		const double Open = (1.0 - Scale) / 0.88;
		GlowGroup(M, P, BayGlow, 0.15 + 1.9 * Open, 0.2);
		Light(C, P, BayLight.Get(M), 18.0 * Open);

		// Pumps stroke and valve wheels turn while MH flows (they stop where they are).
		for (int32 K = 0; K < Pistons.Num(M); ++K)
		{
			const FVector& R = Rest(M, Pistons(M, K));
			SetPosition(P, Pistons(M, K), R.X, 1.79 + (bBusy ? 0.07 * std::sin(Ph * 7.0 + double(K) * UE_DOUBLE_PI) : 0.0), R.Z);
		}
		for (int32 K = 0; K < Valves.Num(M) && K < 2; ++K)
		{
			if (bBusy) Mem.Angle[K] = Ph * 2.2 * (K == 0 ? 1.0 : -1.0);
			SetEuler(M, P, Valves(M, K), 0, 0, Mem.Angle[K]);
		}
		// MH steam: heavy while working, a wisp when idle, none when dry.
		Emit(C, P, Vapour, SteamAt.Get(M), !bLive || bEmpty ? 0.0 : bBusy ? 10.0 : 2.5);
		Emit(C, P, Puffs, PuffsAt.Get(M), !bLive || bEmpty ? 0.0 : bBusy ? 4.0 : 0.6);
		if (bBusy || Scale < 0.999) P.bAnimated = true;
	}

	// MARK: Sentinel (GameScene.animate :524; Simulation.stepTurret)

	struct FSentinelState : FAcPoseState
	{
		double Pod[2] = {0.3, 0.3};
	};

	void PoseSentinel(const FAcPoseContext& C, FAcPose& P)
	{
		const ac::Structure& S = *C.Structure;
		const FAcModelInfo& M = C.Model;
		const bool bLive = CommonPose(C, P, Welding(C));
		const double Ph = Phase(C);
		static const FPart Head(TEXT("head"));
		static const FParts Pods(TEXT("pods"), 2), Flashes(TEXT("flashes"), 2);
		// The yellow lamps are steady while it watches the sky, on and off
		// while it tracks or fires; a pod flashes as its missiles leave.
		const double Last = C.Memory.LastShot.Get(-10.0);
		const bool bTracking = bLive && (S.target.has_value() || C.Time - Last < 1.2);
		GlowGroup(M, P, WorkGlow, !bLive ? 0.05 : bTracking ? Glow(Ph, 0) : 1.2, 2.0);
		if (bLive)
		{
			// The head follows its target (`aim`, world radians), scanning slowly with none.
			const double Aim = S.target ? S.aim.value_or(UE_DOUBLE_HALF_PI) : UE_DOUBLE_HALF_PI + 0.6 * std::sin(Ph * 0.35);
			SetEuler(M, P, Head.Get(M), 0, -Aim, 0);
		}
		FSentinelState& Mem = C.Memory.State<FSentinelState>();
		for (int32 K = 0; K < Pods.Num(M) && K < 2; ++K)
		{
			const double Since = C.Time - Last - double(K) * 0.12;
			if (const int32 F = Flashes(M, K); F != INDEX_NONE) P.Visible[F] = Since >= 0.0 && Since < 0.06;
			Mem.Pod[K] = Ease(Mem.Pod[K], bTracking ? 0.55 : 0.3, 0.04, C.Dt);
			SetEuler(M, P, Pods(M, K), 0, 0, Mem.Pod[K]);
		}
	}

	FAcPoseRegistration RegFoundry(ac::StructureKind::foundry, &PoseFoundry);
	FAcPoseRegistration RegSpacedock(ac::StructureKind::spacedock, &PoseSpacedock);
	FAcPoseRegistration RegLab(ac::StructureKind::lab, &PoseLab);
	FAcPoseRegistration RegDerrick(ac::StructureKind::derrick, &PoseDerrick);
	FAcPoseRegistration RegSentinel(ac::StructureKind::sentinel, &PoseSentinel);
}
