#include "AcPick.h"

#include "Pilot.h"
#include "Rules.h"

#include <algorithm>
#include <cmath>
#include <limits>

std::optional<double> AcPick::Hit(const ac::Vec3& O, const ac::Vec3& D, const ac::Vec3& Lo, const ac::Vec3& Hi)
{
	double T0 = 0.0, T1 = std::numeric_limits<double>::infinity();
	for (int K = 0; K < 3; K++)
	{
		if (std::abs(D[K]) < 1e-9)
		{
			if (O[K] < Lo[K] || O[K] > Hi[K]) return std::nullopt;
			continue;
		}
		const double A = (Lo[K] - O[K]) / D[K], B = (Hi[K] - O[K]) / D[K];
		T0 = std::max(T0, std::min(A, B));
		T1 = std::min(T1, std::max(A, B));
	}
	if (T0 <= T1) return T0;
	return std::nullopt;
}

bool AcPick::Pickable(const ac::Unit& U)
{
	return U.task != ac::Unit::Task::inBastion && U.task != ac::Unit::Task::aboard && U.task != ac::Unit::Task::inDerrick;
}

bool AcPick::Drivable(const ac::Unit& U, const int64 Player)
{
	return U.owner == Player && ac::Pilot::drivable.contains(U.kind) && Pickable(U);
}

std::optional<FAcHover> AcPick::Pick(const ac::FreeView::Ray& Ray, const ac::GameState& State, const int64 Player,
	const FAcPickShapes& Shapes)
{
	std::optional<FAcHover> Best;
	double BestT = 0.0;
	auto Consider = [&](const FAcHover& H, const ac::Vec3& Lo, const ac::Vec3& Hi, const double Bias) {
		std::optional<double> T = Hit(Ray.origin, Ray.direction, Lo, Hi);
		if (!T) return;
		if (Shapes.Refine)
		{
			T = Shapes.Refine(H, Ray, *T);
			if (!T) return;
		}
		const double Score = *T - Bias;
		if (!Best || Score < BestT)
		{
			Best = H;
			BestT = Score;
		}
	};
	for (const ac::Unit& U : State.units)
	{
		if (!Pickable(U)) continue;
		// A little larger than the body, so a small unit is easy to hit.
		const double Radius = ac::Rules::radius(U.kind);
		const double R = Radius + 0.2;
		const double Y0 = Shapes.UnitY ? Shapes.UnitY(U) : 0.0;
		const double H = std::max(1.0, Radius * 2.4);
		FAcHover What;
		What.Kind = FAcHover::EKind::Unit;
		What.Id = U.id;
		What.bDrivable = Drivable(U, Player);
		Consider(What, ac::Vec3(U.position.x - R, Y0 - 0.1, U.position.y - R), ac::Vec3(U.position.x + R, Y0 + H, U.position.y + R), 1.5);
	}
	for (const ac::Structure& S : State.structures)
	{
		const double R = ac::Rules::radius(S.kind) + 0.15;
		const double Y0 = Shapes.GroundY ? Shapes.GroundY(S.position) : 0.0;
		FAcHover What;
		What.Kind = FAcHover::EKind::Building;
		What.Id = S.id;
		Consider(What, ac::Vec3(S.position.x - R, Y0 - 0.2, S.position.y - R),
			ac::Vec3(S.position.x + R, Y0 + std::max(1.2, R * 1.5), S.position.y + R), 0.0);
	}
	return Best;
}
