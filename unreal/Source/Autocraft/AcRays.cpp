// Port of `RayBox`, `RayShape` and `GameScene.cast`
// (Sources/Autocraft/GameScene+Rays.swift). See AcRays.h.
#include "AcRays.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	FAcF3 Sub(const FAcF3 A, const FAcF3 B) { return {A.x - B.x, A.y - B.y, A.z - B.z}; }
	FAcF3 Add(const FAcF3 A, const FAcF3 B) { return {A.x + B.x, A.y + B.y, A.z + B.z}; }
	FAcF3 Mul(const FAcF3 A, const float S) { return {A.x * S, A.y * S, A.z * S}; }
	float Dot(const FAcF3 A, const FAcF3 B) { return A.x * B.x + A.y * B.y + A.z * B.z; }
	float Length(const FAcF3 A) { return std::sqrt(Dot(A, A)); }
	float At(const FAcF3 V, const int K) { return K == 0 ? V.x : (K == 1 ? V.y : V.z); }

	using FSpan = std::pair<float, float>;

	/// Where a line `O` + `V`·t is inside the box `Lo`…`Hi`; nullopt: never.
	std::optional<FSpan> Slab(const FAcF3 O, const FAcF3 V, const FAcF3 Lo, const FAcF3 Hi)
	{
		float Enter = -std::numeric_limits<float>::infinity(), Leave = std::numeric_limits<float>::infinity();
		for (int K = 0; K < 3; K++)
		{
			const float o = At(O, K), v = At(V, K), lo = At(Lo, K), hi = At(Hi, K);
			if (std::abs(v) < 1e-9f)
			{
				if (o < lo || o > hi) return std::nullopt;
				continue;
			}
			const float T1 = (lo - o) / v, T2 = (hi - o) / v;
			Enter = std::max(Enter, std::min(T1, T2));
			Leave = std::min(Leave, std::max(T1, T2));
		}
		if (Enter <= Leave) return FSpan(Enter, Leave);
		return std::nullopt;
	}

	std::optional<FSpan> Sphere(const FAcF3 O, const FAcF3 V, const float R)
	{
		const float A = Dot(V, V), B = Dot(O, V), C = Dot(O, O) - R * R;
		const float Disc = B * B - A * C;
		if (!(A > 1e-12f && Disc >= 0)) return std::nullopt;
		const float Q = std::sqrt(Disc);
		return FSpan((-B - Q) / A, (-B + Q) / A);
	}

	/// Along y, `Half` up and down from the origin.
	std::optional<FSpan> Cylinder(const FAcF3 O, const FAcF3 V, const float R, const float Half)
	{
		FSpan Span(-std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());
		const float A = V.x * V.x + V.z * V.z, B = O.x * V.x + O.z * V.z, C = O.x * O.x + O.z * O.z - R * R;
		if (A > 1e-12f)
		{
			const float Disc = B * B - A * C;
			if (!(Disc >= 0)) return std::nullopt;
			const float Q = std::sqrt(Disc);
			Span = FSpan((-B - Q) / A, (-B + Q) / A);
		}
		else if (C > 0)
		{
			return std::nullopt;
		}
		if (std::abs(V.y) > 1e-9f)
		{
			const float T1 = (-Half - O.y) / V.y, T2 = (Half - O.y) / V.y;
			Span = FSpan(std::max(Span.first, std::min(T1, T2)), std::min(Span.second, std::max(T1, T2)));
		}
		else if (std::abs(O.y) > Half)
		{
			return std::nullopt;
		}
		if (Span.first <= Span.second) return Span;
		return std::nullopt;
	}
}

// --- FAcMat4 -------------------------------------------------------------------

FAcMat4 FAcMat4::FromColumns(const float* V)
{
	FAcMat4 Out;
	for (int K = 0; K < 16; K++) Out.M[K] = V[K];
	return Out;
}

FAcMat4 FAcMat4::operator*(const FAcMat4& B) const
{
	FAcMat4 Out;
	for (int C = 0; C < 4; C++)
	{
		for (int R = 0; R < 4; R++)
		{
			float S = 0;
			for (int K = 0; K < 4; K++) S += (*this)(R, K) * B(K, C);
			Out(R, C) = S;
		}
	}
	return Out;
}

FAcMat4 FAcMat4::Inverse() const
{
	// Cofactors (as MESA's gluInvertMatrix), in double: simd's float inverse
	// is exact to an ulp or two, and so is this.
	double m[16], inv[16];
	for (int K = 0; K < 16; K++) m[K] = M[K];
	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
	const double Det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (Det == 0) return FAcMat4();
	FAcMat4 Out;
	for (int K = 0; K < 16; K++) Out.M[K] = float(inv[K] / Det);
	return Out;
}

FAcF3 FAcMat4::Point(const FAcF3 P) const
{
	return {M[0] * P.x + M[4] * P.y + M[8] * P.z + M[12], M[1] * P.x + M[5] * P.y + M[9] * P.z + M[13],
		M[2] * P.x + M[6] * P.y + M[10] * P.z + M[14]};
}

FAcF3 FAcMat4::Vector(const FAcF3 V) const
{
	return {M[0] * V.x + M[4] * V.y + M[8] * V.z, M[1] * V.x + M[5] * V.y + M[9] * V.z, M[2] * V.x + M[6] * V.y + M[10] * V.z};
}

float FAcMat4::MaxScale() const
{
	const float X = Length({M[0], M[1], M[2]}), Y = Length({M[4], M[5], M[6]}), Z = Length({M[8], M[9], M[10]});
	return std::max(X, std::max(Y, Z));
}

// --- FAcRayBox -----------------------------------------------------------------

bool FAcRayBox::Known() const
{
	const bool Ordered = Hi.x >= Lo.x && Hi.y >= Lo.y && Hi.z >= Lo.z;
	const bool Same = Lo.x == Hi.x && Lo.y == Hi.y && Lo.z == Hi.z;
	return Ordered && !Same;
}

std::optional<float> FAcRayBox::Hit(const FAcMat4& Transform, const FAcF3 A, const FAcF3 B) const
{
	if (!(Hi.x >= Lo.x && Hi.y >= Lo.y && Hi.z >= Lo.z)) return std::nullopt;
	const FAcMat4 Inverse = Transform.Inverse();
	const FAcF3 O = Inverse.Point(A), V = Inverse.Vector(Sub(B, A));
	const std::optional<FSpan> Box = Slab(O, V, Lo, Hi);
	if (!Box) return std::nullopt;
	std::optional<FSpan> Spans[3];
	switch (Round)
	{
	case ERound::None: Spans[0] = Box; break;
	case ERound::Sphere: Spans[0] = Sphere(O, V, Radius); break;
	case ERound::Cylinder: Spans[0] = Cylinder(O, V, Radius, Half); break;
	case ERound::Capsule:
		Spans[0] = Cylinder(O, V, Radius, Half);
		Spans[1] = Sphere(Sub(O, {0, Half, 0}), V, Radius);
		Spans[2] = Sphere(Add(O, {0, Half, 0}), V, Radius);
		break;
	}
	// The first entry ahead. A segment that starts inside passes: the shape
	// is the part's bounds, looser than the part itself.
	std::optional<float> Best;
	for (const std::optional<FSpan>& S : Spans)
	{
		if (S && S->first >= 0 && S->first <= 1 && (!Best || S->first < *Best)) Best = S->first;
	}
	return Best;
}

// --- FAcRayShape ---------------------------------------------------------------

void FAcRayShape::Measure(const std::vector<FAcMat4>& RestLocal)
{
	float Far = 0;
	const int32_t N = int32_t(Parent.size());
	// Each part in the root's space: its rests up to (not including) the root.
	std::vector<FAcMat4> ToRoot(static_cast<size_t>(N));
	for (int32_t P = 0; P < N; P++)
	{
		const int32_t Up = Parent[size_t(P)];
		if (P == Root || Up < 0) ToRoot[size_t(P)] = FAcMat4();
		else if (Up == Root) ToRoot[size_t(P)] = RestLocal[size_t(P)];
		else ToRoot[size_t(P)] = ToRoot[size_t(Up)] * RestLocal[size_t(P)];
	}
	for (const FPiece& Piece : Pieces)
	{
		const FAcMat4 M = ToRoot[size_t(Piece.Part)] * Piece.At;
		for (int C = 0; C < 8; C++)
		{
			const FAcF3 Corner{(C & 1) == 0 ? Piece.Box.Lo.x : Piece.Box.Hi.x, (C & 2) == 0 ? Piece.Box.Lo.y : Piece.Box.Hi.y,
				(C & 4) == 0 ? Piece.Box.Lo.z : Piece.Box.Hi.z};
			Far = std::max(Far, Length(M.Point(Corner)));
		}
	}
	Reach = Far * 1.25f;
}

bool FAcRayShape::Near(const FAcMat4& RootWorld, const FAcF3 A, const FAcF3 B) const
{
	if (Pieces.empty()) return false;
	const float Scale = RootWorld.MaxScale();
	const FAcF3 C = RootWorld.Translation(), D = Sub(B, A);
	const float S = std::clamp(Dot(Sub(C, A), D) / std::max(Dot(D, D), 1e-8f), 0.0f, 1.0f);
	return Length(Sub(Add(A, Mul(D, S)), C)) <= Reach * Scale;
}

std::optional<float> FAcRayShape::HitPieces(const FAcMat4* PartWorld, const uint8_t* Visible, const FAcF3 A, const FAcF3 B,
	const float Limit) const
{
	std::optional<float> Best;
	int32_t LastPart = -1;
	FAcMat4 World;
	for (const FPiece& Piece : Pieces)
	{
		if (Visible && !Visible[Piece.Part]) continue;
		if (Piece.Part != LastPart)
		{
			World = PartWorld[Piece.Part];
			LastPart = Piece.Part;
		}
		if (const std::optional<float> T = Piece.Box.Hit(World * Piece.At, A, B); T && *T < Best.value_or(Limit)) Best = T;
	}
	return Best;
}

std::optional<float> FAcRayShape::Hit(const FAcMat4* PartWorld, const uint8_t* Visible, const FAcF3 A, const FAcF3 B,
	const float Limit) const
{
	if (Pieces.empty() || (Visible && !Visible[Root]) || !Near(PartWorld[Root], A, B)) return std::nullopt;
	return HitPieces(PartWorld, Visible, A, B, Limit);
}

// --- Cast ----------------------------------------------------------------------

std::optional<AcRays::FHit> AcRays::Cast(const FAcHeightGrid* Ground, const std::vector<FTarget>& Targets, const FAcF3 A,
	const FAcF3 B, const std::optional<int64_t> Skip)
{
	std::optional<FHit> Best;
	if (Ground && !Ground->empty())
	{
		if (const std::optional<float> T = Ground->crossing(A, B)) Best = FHit{*T, std::nullopt};
	}
	for (const FTarget& Target : Targets)
	{
		if (!Target.Shape || !Target.PartWorld) continue;
		if (Skip && Target.Id && *Target.Id == *Skip) continue;
		const float Limit = Best ? Best->T : 1.0f;
		if (const std::optional<float> T = Target.Shape->Hit(Target.PartWorld, Target.Visible, A, B, Limit)) Best = FHit{*T, Target.Id};
	}
	return Best;
}
