#include "AcPose.h"

#include "AcModelCatalog.h"
#include "AcPoseNewKinds.h"
#include "AcSpace.h"

#include "TerrainField.h"

#include <cmath>

namespace
{
	const int32 UnitKinds = (int32)ac::allCases<ac::UnitKind>().size();
	constexpr int32 StructureKinds = 9;

	FAcPoseFn* UnitTable()
	{
		static FAcPoseFn Table[ac::allCases<ac::UnitKind>().size()] = {};
		return Table;
	}
	FAcPoseFn* StructureTable()
	{
		static FAcPoseFn Table[StructureKinds] = {};
		return Table;
	}

	double Smoothstep(const double A, const double B, const double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}
}

double FAcPoseContext::Ground(const ac::Vec2 P) const
{
	return Field.height(P);
}

bool FAcPoseContext::HasUpgrade(const int64 Player, const ac::Upgrade Upgrade) const
{
	if (Player < 0 || Player >= (int64)State.players.size()) return false;
	const auto& U = State.players[(size_t)Player].upgrades;
	return U && U->count(Upgrade) > 0;
}

void FAcPose::Reset(const FAcModelInfo& Model)
{
	const int32 N = Model.Parts.Num();
	Local.SetNumUninitialized(N);
	Visible.SetNumUninitialized(N);
	Emission.SetNumUninitialized(N);
	for (int32 I = 0; I < N; ++I)
	{
		const FAcModelPart& P = Model.Parts[I];
		Local[I] = P.Rest;
		Visible[I] = !P.bHidden;
		Emission[I] = 1.f;
	}
	Placement = FTransform::Identity;
	bAnimated = false;
	bHidden = false;
	bFrozen = false;
	Cues.Reset();
	MeshEmission.Reset();
}

int32 FAcPartIndex::Get(const FAcModelInfo& Model) const
{
	const int32* Found = Model.PartIndex.Find(Name);
	return Found ? *Found : INDEX_NONE;
}

namespace AcPose
{
	void Register(const ac::UnitKind Kind, const FAcPoseFn Fn) { UnitTable()[(int32)Kind] = Fn; }
	void Register(const ac::StructureKind Kind, const FAcPoseFn Fn) { StructureTable()[(int32)Kind] = Fn; }
	FAcPoseFn Find(const ac::UnitKind Kind) { return (int32)Kind < UnitKinds ? UnitTable()[(int32)Kind] : nullptr; }
	FAcPoseFn Find(const ac::StructureKind Kind)
	{
		return (int32)Kind < StructureKinds ? StructureTable()[(int32)Kind] : nullptr;
	}

	const TCHAR* ModelBase(const ac::UnitKind Kind)
	{
		switch (Kind)
		{
		case ac::UnitKind::prospector: return TEXT("prospector");
		case ac::UnitKind::ranger: return TEXT("ranger");
		case ac::UnitKind::comet: return TEXT("comet");
		case ac::UnitKind::firefly: return TEXT("firefly");
		case ac::UnitKind::juggernaut: return TEXT("juggernaut");
		case ac::UnitKind::dropship: return TEXT("dropship");
		case ac::UnitKind::longbow: return TEXT("longbow");
		case ac::UnitKind::kestrel: return TEXT("kestrel");
		case ac::UnitKind::hailstorm: return TEXT("hailstorm");
		case ac::UnitKind::peregrine: return TEXT("peregrine");
		case ac::UnitKind::atlas: return TEXT("atlas");
		case ac::UnitKind::scorpion: return TEXT("scorpion");
		}
		return TEXT("ranger");
	}

	const TCHAR* ModelBase(const ac::StructureKind Kind)
	{
		switch (Kind)
		{
		case ac::StructureKind::citadel: return TEXT("citadel");
		case ac::StructureKind::habDome: return TEXT("habdome");
		case ac::StructureKind::garrison: return TEXT("garrison");
		case ac::StructureKind::bastion: return TEXT("bastion");
		case ac::StructureKind::derrick: return TEXT("derrick");
		case ac::StructureKind::foundry: return TEXT("foundry");
		case ac::StructureKind::spacedock: return TEXT("spacedock");
		case ac::StructureKind::lab: return TEXT("lab");
		case ac::StructureKind::sentinel: return TEXT("sentinel");
		}
		return TEXT("citadel");
	}

	double BuildingHeight(const ac::StructureKind Kind)
	{
		// GameScene.makeBuilding (GameScene.swift:363); Models.labHeight 1.9.
		switch (Kind)
		{
		case ac::StructureKind::citadel: return 4.6;
		case ac::StructureKind::habDome: return 1.2;
		case ac::StructureKind::garrison: return 2.5;
		case ac::StructureKind::bastion: return 1.4;
		case ac::StructureKind::derrick: return 2.5;
		case ac::StructureKind::foundry: return 2.4;
		case ac::StructureKind::spacedock: return 3.2;
		case ac::StructureKind::lab: return 1.9;
		case ac::StructureKind::sentinel: return 1.7;
		}
		return 2.0;
	}

	double ConstructionSink(const ac::Structure& S)
	{
		return -(1.0 - S.progress()) * BuildingHeight(S.kind) * 0.92;
	}

	double Hover(const ac::UnitKind Kind)
	{
		// Models.hover (Models+Kestrel.swift:268).
		switch (Kind)
		{
		case ac::UnitKind::dropship: return 2.4;
		case ac::UnitKind::kestrel: return 2.1;
		case ac::UnitKind::peregrine: return AcPoseNew::PeregrineHover;
		default: return 0.0;
		}
	}

	double FlightBase(const FAcPoseContext& C)
	{
		const ac::Unit& U = *C.Unit;
		const ac::Vec2 Ahead = U.position + ac::Vec2(std::cos(U.heading), std::sin(U.heading)) * 1.5;
		const double Ground = FMath::Max(C.Ground(U.position), C.Ground(Ahead));
		double Y = C.Memory.AirY.Get(Ground);
		Y += (Ground - Y) * FMath::Min(1.0, C.Dt * (Ground > Y ? 3.0 : 1.2));
		C.Memory.AirY = Y;
		return Y;
	}

	double FlightY(const FAcPoseContext& C)
	{
		const ac::Unit& U = *C.Unit;
		const std::optional<double> J = U.jump();
		if (!J || !U.jumpFrom || !U.jumpTo) return C.Ground(U.position);
		const double A = C.Ground(*U.jumpFrom), B = C.Ground(*U.jumpTo);
		return A + (B - A) * Smoothstep(0.0, 1.0, *J);
	}

	double Turning(const FAcPoseContext& C)
	{
		const ac::Unit& U = *C.Unit;
		const double Last = C.Memory.LastHeading.Get(U.heading);
		C.Memory.LastHeading = U.heading;
		if (C.Dt <= 1e-4) return C.Memory.TurnRate;
		const double Raw = std::remainder(U.heading - Last, 2.0 * UE_DOUBLE_PI) / C.Dt;
		const double K = FMath::Min(1.0, C.Dt * 6.0);
		C.Memory.TurnRate = C.Memory.TurnRate * (1.0 - K) + Raw * K;
		return C.Memory.TurnRate;
	}

	void RestUnit(const FAcPoseContext& C, FAcPose& P)
	{
		P.Reset(C.Model);
		const ac::Unit& U = *C.Unit;
		double Y;
		if (U.stats().air) Y = FlightBase(C);
		else if (U.kind == ac::UnitKind::comet) Y = FlightY(C);
		else Y = C.Ground(U.position);
		P.Placement = FTransform(AcSpace::QuatFromHeading(U.heading), AcSpace::ToWorld(U.position, Y));
	}

	void RestStructure(const FAcPoseContext& C, FAcPose& P)
	{
		P.Reset(C.Model);
		const ac::Structure& S = *C.Structure;
		// Buildings face +Z in SceneKit (UE +Y) and are never turned; the
		// body rises out of the ground as it is built (`animate` :489).
		const double Y = C.Ground(S.position) + (S.complete() ? 0.0 : ConstructionSink(S));
		P.Placement = FTransform(FQuat::Identity, AcSpace::ToWorld(S.position, Y));
	}
}
