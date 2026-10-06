#include "AcPoseNewKinds.h"

#include <cmath>

namespace
{
	double Smoothstep(const double A, const double B, const double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	double Fract(const double X) { return X - std::floor(X); }
}

namespace AcPoseNew
{
	// --- Peregrine --------------------------------------------------------------

	double PeregrineBank(const double TurnRate)
	{
		return FMath::Clamp(TurnRate / PeregrineBankTurn, -1.0, 1.0) * PeregrineMaxBank;
	}

	int32 MissilePair(const double ShotTime) { return int32(std::llround(ShotTime * 1000.0)) & 1; }

	FMissile MissileOnRail(const int32 Rail, const double ShotTime, const double Since)
	{
		FMissile M;
		// Pair 0 is the outer rails (0, 3), pair 1 the inner (1, 2).
		const bool bLaunched = (MissilePair(ShotTime) == 0) == (Rail == 0 || Rail == 3);
		if (!bLaunched || Since < 0.0 || Since >= MissileReload + MissileSlide) return M;
		if (Since < MissileReload)
		{
			M.bShown = false;
			M.Scale = 0.0;
			return M;
		}
		// Back on its rail from behind, growing into place.
		const double S = Smoothstep(0.0, 1.0, (Since - MissileReload) / MissileSlide);
		M.Scale = S;
		M.Back = (1.0 - S) * 45.0;
		return M;
	}

	double PeregrineBeacon(const double T, const int32 Side)
	{
		// A short flash every 1.4 s, the two tips half a beat apart.
		const double Ph = Fract(T / 1.4 + 0.5 * double(Side));
		return Ph < 0.1 ? 1.3 : 0.15;
	}

	// --- Atlas ------------------------------------------------------------------

	FAtlasLeg AtlasLeg(const double Cycle)
	{
		constexpr double Reach = 0.40;   // the hip's swing, either way
		constexpr double Landing = 0.20; // the knee gives as the foot comes down and pushes off
		constexpr double Clear = 0.95;   // the knee's extra bend at the height of the swing
		const double U = Fract(Cycle);
		FAtlasLeg L;
		if (U < AtlasStance)
		{
			// Stance: the foot stays put while the hip goes over it.
			const double S = U / AtlasStance;
			L.Hip = Reach * (1.0 - 2.0 * S);
			L.Knee = Landing * (1.0 - std::sin(UE_DOUBLE_PI * S));
			L.bStance = true;
		}
		else
		{
			const double S = (U - AtlasStance) / (1.0 - AtlasStance);
			L.Hip = Reach * (-1.0 + 2.0 * Smoothstep(0.0, 1.0, S));
			L.Knee = Landing + (Clear - Landing) * std::sin(UE_DOUBLE_PI * S);
			L.Lift = std::sin(UE_DOUBLE_PI * S);
			L.bStance = false;
		}
		L.Ankle = L.Hip - L.Knee;
		return L;
	}

	FAtlasStomp AtlasStomp(const double Since)
	{
		FAtlasStomp S;
		if (Since < 0.0 || Since >= AtlasStompTotal) return S;
		// Up on one leg, a beat at the top, then down hard at the impact.
		const double Up = Smoothstep(0.0, 0.2, Since) * (1.0 - Smoothstep(0.24, AtlasStompImpact, Since));
		const double After = Since > AtlasStompImpact ? Since - AtlasStompImpact : 0.0;
		S.Hip = 0.6 * Up;
		S.Knee = 1.3 * Up;
		S.Ankle = S.Hip - S.Knee;
		S.Support = 0.2 * Up + 0.14 * std::exp(-After * 6.0) * (Since > AtlasStompImpact ? 1.0 : 0.0);
		S.Drop = -4.0 * Up + (After > 0.0 ? 15.0 * std::exp(-After * 7.0) * std::cos(After * 13.0) : 0.0);
		S.Pitch = 0.05 * Up + (After > 0.0 ? 0.07 * std::exp(-After * 7.0) : 0.0);
		S.Weight = FMath::Min(1.0, Since / 0.08) * (1.0 - Smoothstep(0.65, AtlasStompTotal, Since));
		S.Flare = Smoothstep(0.15, AtlasStompImpact, Since) * std::exp(-After * 3.5);
		return S;
	}

	double AtlasRecoil(const double Since)
	{
		if (Since < 0.0) return 0.0;
		if (Since < 0.05) return Since / 0.05;
		return std::exp(-(Since - 0.05) * 6.0);
	}

	double AtlasVentGlow(const double Since)
	{
		constexpr double Idle = 0.25;
		if (Since < 0.0) return Idle;
		return Idle + 3.0 * std::exp(-Since / 1.1);
	}

	double AtlasSearchlight(const double Dark)
	{
		return 0.15 + 2.2 * FMath::Clamp(Dark, 0.0, 1.0);
	}

	// --- Scorpion ---------------------------------------------------------------

	int32 TripodOf(const int32 Leg) { return (Leg == 0 || Leg == 2 || Leg == 4) ? 0 : 1; }

	FScorpionLeg ScorpionLeg(const double Cycle)
	{
		constexpr double Reach = 0.42;
		const double U = Fract(Cycle);
		FScorpionLeg L;
		if (U < 0.5)
		{
			L.Swing = Reach * (1.0 - 4.0 * U);
		}
		else
		{
			const double S = (U - 0.5) / 0.5;
			L.Swing = Reach * (-1.0 + 2.0 * Smoothstep(0.0, 1.0, S));
			L.Lift = std::sin(UE_DOUBLE_PI * S);
		}
		return L;
	}

	double ScorpionSink(const double Anchor) { return ScorpionSinkCm * Smoothstep(0.0, 1.0, Anchor); }

	double ScorpionMound(const double Anchor) { return Smoothstep(0.05, 1.0, Anchor); }

	double ScorpionTailLaid(const double Anchor, const double Lock)
	{
		return Smoothstep(0.0, 0.6, Anchor) * (1.0 - Smoothstep(0.0, 0.8, Lock));
	}

	double ScorpionSnap(const double Since)
	{
		if (Since < 0.0 || Since > 0.5) return 0.0;
		return std::exp(-Since * 12.0);
	}
}
