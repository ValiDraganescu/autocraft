#include "AcArtList.h"

#include "AcHudStyle.h"

#include "Algo/Reverse.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"

#include <cmath>

namespace
{
	constexpr double Pi = 3.14159265358979323846;
	/// Core Graphics' shadow: a Gaussian of 0.47 × `blur`, in pixels (measured).
	constexpr double SigmaPerBlur = 0.47;

	double Cross(const FVector2D A, const FVector2D B) { return A.X * B.Y - A.Y * B.X; }
	/// The outward normal of the edge A→B of a counter-clockwise polygon.
	FVector2D OutNormal(const FVector2D A, const FVector2D B)
	{
		const FVector2D D = (B - A).GetSafeNormal();
		return FVector2D(D.Y, -D.X);
	}
	FVector2D LeftNormal(const FVector2D A, const FVector2D B)
	{
		const FVector2D D = (B - A).GetSafeNormal();
		return FVector2D(-D.Y, D.X);
	}
	/// The standard normal distribution's cumulative function.
	double Phi(const double X) { return 0.5 * (1 + std::erf(X / FMath::Sqrt(2.0))); }
	FColor Pack(const FLinearColor& C)
	{
		return FAcHudStyle::Srgb(C.R, C.G, C.B, FMath::Clamp(C.A, 0.0f, 1.0f)).ToFColor(true);
	}
	FBox2D BoundsOf(const FAcPoly& P)
	{
		FBox2D B(ForceInit);
		for (const FVector2D& Q : P) B += Q;
		return B;
	}
	/// Where `A`→`B` is inside the convex `P` (Cyrus-Beck), as 0…1 along it.
	bool Inside(const FVector2D A, const FVector2D B, const FAcPoly& P, double& T0, double& T1)
	{
		T0 = 0;
		T1 = 1;
		for (int32 I = 0; I < P.Num(); ++I)
		{
			const FVector2D E = P[(I + 1) % P.Num()] - P[I];
			const double Num = Cross(E, A - P[I]);
			const double Den = Cross(E, B - A);
			if (FMath::Abs(Den) < 1e-12)
			{
				if (Num < 0) return false;
				continue;
			}
			const double T = -Num / Den;
			if (Den > 0) T0 = FMath::Max(T0, T);
			else T1 = FMath::Min(T1, T);
			if (T0 >= T1) return false;
		}
		return T1 - T0 > 1e-9;
	}
}

// MARK: - Regions and gradients

FAcRegion::FAcRegion(const FAcRect& R)
{
	Pieces.Add(AcArt::RectPoly(R));
}

FBox2D FAcRegion::Bounds() const
{
	FBox2D B(ForceInit);
	for (const FAcPoly& P : Pieces) B += BoundsOf(P);
	return B;
}

bool FAcRegion::Contains(const FVector2D Q) const
{
	for (const FAcPoly& P : Pieces)
	{
		bool bIn = P.Num() >= 3;
		for (int32 I = 0; I < P.Num() && bIn; ++I) bIn = Cross(P[(I + 1) % P.Num()] - P[I], Q - P[I]) >= 0;
		if (bIn) return true;
	}
	return false;
}

FAcStops::FAcStops(std::initializer_list<FLinearColor> InColors, std::initializer_list<double> InAt)
	: Colors(InColors), At(InAt)
{
	check(Colors.Num() == At.Num() && Colors.Num() > 0);
}

FLinearColor FAcStops::Color(double T) const
{
	T = FMath::Clamp(T, 0.0, 1.0);
	if (T <= At[0]) return Colors[0];
	for (int32 I = 1; I < At.Num(); ++I)
	{
		if (T <= At[I])
		{
			const double F = At[I] > At[I - 1] ? (T - At[I - 1]) / (At[I] - At[I - 1]) : 1.0;
			const FLinearColor& A = Colors[I - 1];
			const FLinearColor& B = Colors[I];
			return A + (B - A) * (float)F;
		}
	}
	return Colors.Last();
}

// MARK: - Polygons

FLinearColor AcArt::Rgb(const double R, const double G, const double B, const double A)
{
	// Generic RGB → linear (gamma 1.8) → sRGB's primaries (least squares
	// over Core Graphics' own conversion) → the sRGB curve.
	const double Lr = FMath::Pow(FMath::Clamp(R, 0.0, 1.0), 1.8);
	const double Lg = FMath::Pow(FMath::Clamp(G, 0.0, 1.0), 1.8);
	const double Lb = FMath::Pow(FMath::Clamp(B, 0.0, 1.0), 1.8);
	auto Encode = [](double L)
	{
		L = FMath::Clamp(L, 0.0, 1.0);
		return (float)(L <= 0.0031308 ? L * 12.92 : 1.055 * FMath::Pow(L, 1 / 2.4) - 0.055);
	};
	return FLinearColor(Encode(1.02546 * Lr - 0.02667 * Lg + 0.00125 * Lb), Encode(0.01933 * Lr + 0.94756 * Lg + 0.03269 * Lb),
		Encode(-0.00198 * Lr - 0.00116 * Lg + 1.00313 * Lb), (float)A);
}

FAcPoly AcArt::RectPoly(const FAcRect& R)
{
	return {FVector2D(R.MinX(), R.MinY()), FVector2D(R.MaxX(), R.MinY()), FVector2D(R.MaxX(), R.MaxY()), FVector2D(R.MinX(), R.MaxY())};
}

double AcArt::Area(const FAcPoly& P)
{
	double A = 0;
	for (int32 I = 0; I < P.Num(); ++I) A += Cross(P[I], P[(I + 1) % P.Num()]);
	return A / 2;
}

FAcPoly AcArt::Clean(FAcPoly P)
{
	FAcPoly Out;
	Out.Reserve(P.Num());
	for (const FVector2D& Q : P)
	{
		if (Out.IsEmpty() || !Out.Last().Equals(Q, 1e-7)) Out.Add(Q);
	}
	while (Out.Num() > 1 && Out.Last().Equals(Out[0], 1e-7)) Out.Pop();
	if (Area(Out) < 0) Algo::Reverse(Out);
	return Out;
}

FAcPoly AcArt::Moved(const FAcPoly& P, const FVector2D By)
{
	FAcPoly Out = P;
	for (FVector2D& Q : Out) Q += By;
	return Out;
}

FAcPoly AcArt::ClipHalf(const FAcPoly& P, const FVector2D A, const FVector2D B)
{
	FAcPoly Out;
	const int32 N = P.Num();
	if (N < 3) return Out;
	const FVector2D E = B - A;
	for (int32 I = 0; I < N; ++I)
	{
		const FVector2D Cur = P[I], Next = P[(I + 1) % N];
		const double Dc = Cross(E, Cur - A), Dn = Cross(E, Next - A);
		if (Dc >= 0) Out.Add(Cur);
		if ((Dc >= 0) != (Dn >= 0)) Out.Add(Cur + (Next - Cur) * (Dc / (Dc - Dn)));
	}
	return Out.Num() >= 3 ? Out : FAcPoly();
}

FAcPoly AcArt::Clip(const FAcPoly& P, const FAcPoly& C)
{
	FAcPoly Out = P;
	for (int32 I = 0; I < C.Num() && Out.Num() >= 3; ++I) Out = ClipHalf(Out, C[I], C[(I + 1) % C.Num()]);
	return Out;
}

FAcRegion AcArt::Minus(const FAcPoly& Outer, const FAcPoly& InHole)
{
	// A point outside the hole is outside some first edge `I` and inside every
	// edge before it: one convex piece per edge.
	const FAcPoly Hole = Clean(InHole);
	FAcRegion R;
	const int32 N = Hole.Num();
	for (int32 I = 0; I < N; ++I)
	{
		FAcPoly Piece = ClipHalf(Outer, Hole[(I + 1) % N], Hole[I]);
		for (int32 J = 0; J < I && Piece.Num() >= 3; ++J) Piece = ClipHalf(Piece, Hole[J], Hole[(J + 1) % N]);
		if (Piece.Num() >= 3 && Area(Piece) > 1e-9) R.Pieces.Add(MoveTemp(Piece));
	}
	return R;
}

FAcRegion AcArt::Clip(const FAcRegion& Region, const FAcPoly& C)
{
	FAcRegion R;
	const FBox2D Cb = BoundsOf(C);
	for (const FAcPoly& P : Region.Pieces)
	{
		if (!BoundsOf(P).Intersect(Cb)) continue;
		FAcPoly Q = Clip(P, C);
		if (Q.Num() >= 3 && Area(Q) > 1e-9) R.Pieces.Add(MoveTemp(Q));
	}
	return R;
}

FAcPoly AcArt::Grown(const FAcPoly& P, const double D)
{
	const int32 N = P.Num();
	FAcPoly Out;
	if (N < 3) return Out;
	Out.Reserve(N * 2);
	for (int32 I = 0; I < N; ++I)
	{
		const FVector2D Prev = P[(I + N - 1) % N], Cur = P[I], Next = P[(I + 1) % N];
		const FVector2D N0 = OutNormal(Prev, Cur), N1 = OutNormal(Cur, Next);
		const double Turn = FMath::Atan2(Cross(N0, N1), N0 | N1);
		// Corners sharper than 15° get an arc outside (a shadow's or a
		// halo's rounded corner); the same count of points at every D.
		const int32 K = Turn > Pi / 12 ? FMath::Clamp((int32)FMath::CeilToDouble(Turn / (Pi / 12)), 2, 8) : 1;
		if (D > 0 && K >= 2)
		{
			for (int32 J = 0; J < K; ++J)
			{
				const double A = Turn * J / (K - 1);
				const FVector2D Nr(N0.X * FMath::Cos(A) - N0.Y * FMath::Sin(A), N0.X * FMath::Sin(A) + N0.Y * FMath::Cos(A));
				Out.Add(Cur + Nr * D);
			}
		}
		else
		{
			const double Den = 1 + (N0 | N1);
			const FVector2D M = Den > 1e-3 ? (N0 + N1) / Den : N0;
			for (int32 J = 0; J < K; ++J) Out.Add(Cur + M * D);
		}
	}
	return Out;
}

TArray<FVector2D> AcArt::Offset(const TArray<FVector2D>& L, const double D, const bool bClosed)
{
	const int32 N = L.Num();
	TArray<FVector2D> Out;
	Out.Reserve(N);
	for (int32 I = 0; I < N; ++I)
	{
		const bool bFirst = I == 0 && !bClosed, bLast = I == N - 1 && !bClosed;
		const FVector2D Prev = L[(I + N - 1) % N], Cur = L[I], Next = L[(I + 1) % N];
		const FVector2D N0 = bFirst ? LeftNormal(Cur, Next) : LeftNormal(Prev, Cur);
		const FVector2D N1 = bLast ? N0 : LeftNormal(Cur, Next);
		const double Den = 1 + (N0 | N1);
		FVector2D M = Den > 1e-3 ? (N0 + N1) / Den : N0;
		if (M.SizeSquared() > 16) M = M.GetSafeNormal() * 4;  // a miter limit
		Out.Add(Cur + M * D);
	}
	return Out;
}

TArray<TArray<FVector2D>> AcArt::ClipLine(const TArray<FVector2D>& L, const FAcRegion& Region)
{
	TArray<TArray<FVector2D>> Runs;
	TArray<FVector2D> Run;
	TArray<FBox2D> Boxes;
	for (const FAcPoly& P : Region.Pieces) Boxes.Add(BoundsOf(P));
	for (int32 I = 0; I + 1 < L.Num(); ++I)
	{
		const FVector2D A = L[I], B = L[I + 1];
		FBox2D Seg(ForceInit);
		Seg += A;
		Seg += B;
		TArray<TPair<double, double>> Spans;
		for (int32 K = 0; K < Region.Pieces.Num(); ++K)
		{
			double T0, T1;
			if (Boxes[K].Intersect(Seg) && Inside(A, B, Region.Pieces[K], T0, T1)) Spans.Emplace(T0, T1);
		}
		Spans.Sort([](const TPair<double, double>& X, const TPair<double, double>& Y) { return X.Key < Y.Key; });
		// Pieces meet edge to edge: join the spans that touch.
		TArray<TPair<double, double>> Joined;
		for (const TPair<double, double>& S : Spans)
		{
			if (!Joined.IsEmpty() && S.Key <= Joined.Last().Value + 1e-7) Joined.Last().Value = FMath::Max(Joined.Last().Value, S.Value);
			else Joined.Add(S);
		}
		for (const TPair<double, double>& S : Joined)
		{
			const FVector2D Pa = A + (B - A) * S.Key, Pb = A + (B - A) * S.Value;
			if (!Run.IsEmpty() && Run.Last().Equals(Pa, 1e-6)) Run.Add(Pb);
			else
			{
				if (Run.Num() >= 2) Runs.Add(MoveTemp(Run));
				Run = {Pa, Pb};
			}
		}
	}
	if (Run.Num() >= 2) Runs.Add(MoveTemp(Run));
	return Runs;
}

TArray<FVector2D> AcArt::Quad(const FVector2D A, const FVector2D C, const FVector2D B, const int32 Steps)
{
	TArray<FVector2D> Out;
	const int32 N = FMath::Max(1, Steps);
	Out.Reserve(N + 1);
	for (int32 I = 0; I <= N; ++I)
	{
		const double T = (double)I / N, U = 1 - T;
		Out.Add(A * (U * U) + C * (2 * U * T) + B * (T * T));
	}
	return Out;
}

// MARK: - FAcArtList

FAcArtList::FAcArtList(const double PixelsPerPoint)
	: PxPerPt(FMath::Max(PixelsPerPoint, 0.25))
{
}

FAcPoly FAcArtList::RoundRect(const FAcRect& R, double Rw, double Rh) const
{
	Rw = FMath::Clamp(Rw, 0.0, R.W / 2);
	Rh = FMath::Clamp(Rh, 0.0, R.H / 2);
	if (Rw < 0.01 || Rh < 0.01) return AcArt::RectPoly(R);
	const int32 Steps = FMath::Clamp((int32)FMath::CeilToDouble(FMath::Max(Rw, Rh) * PxPerPt / 2.5), 2, 16);
	const FVector2D Centres[4] = {FVector2D(R.MaxX() - Rw, R.MinY() + Rh), FVector2D(R.MaxX() - Rw, R.MaxY() - Rh),
		FVector2D(R.MinX() + Rw, R.MaxY() - Rh), FVector2D(R.MinX() + Rw, R.MinY() + Rh)};
	FAcPoly P;
	for (int32 C = 0; C < 4; ++C)
	{
		for (int32 S = 0; S <= Steps; ++S)
		{
			const double A = -Pi / 2 + C * Pi / 2 + (Pi / 2) * S / Steps;
			P.Add(Centres[C] + FVector2D(Rw * FMath::Cos(A), Rh * FMath::Sin(A)));
		}
	}
	return AcArt::Clean(MoveTemp(P));
}

FAcPoly FAcArtList::Ellipse(const FAcRect& R) const
{
	const int32 N = FMath::Clamp((int32)FMath::CeilToDouble(Pi * FMath::Max(R.W, R.H) * PxPerPt / 3), 12, 96);
	FAcPoly P;
	P.Reserve(N);
	for (int32 I = 0; I < N; ++I)
	{
		const double A = 2 * Pi * I / N;
		P.Add(FVector2D(R.MidX() + R.W / 2 * FMath::Cos(A), R.MidY() + R.H / 2 * FMath::Sin(A)));
	}
	return P;
}

int32 FAcArtList::Add(const FVector2D P, const FLinearColor& C)
{
	Verts.Add(FVector2f(P));
	Colors.Add(Pack(C));
	return Verts.Num() - 1;
}

void FAcArtList::Fan(const FVPoly& P)
{
	if (P.Num() < 3) return;
	const uint32 Base = (uint32)Verts.Num();
	for (const FV& V : P) Add(V.P, V.C);
	for (int32 K = 1; K + 1 < P.Num(); ++K) Indices.Append({Base, Base + K, Base + K + 1});
}

void FAcArtList::Band(const FVPoly& A, const FVPoly& B, const bool bClosed)
{
	const int32 N = A.Num();
	if (N < 2 || B.Num() != N) return;
	const uint32 Ba = (uint32)Verts.Num();
	for (const FV& V : A) Add(V.P, V.C);
	const uint32 Bb = (uint32)Verts.Num();
	for (const FV& V : B) Add(V.P, V.C);
	const int32 Segments = bClosed ? N : N - 1;
	for (int32 I = 0; I < Segments; ++I)
	{
		const uint32 J = (uint32)((I + 1) % N);
		Indices.Append({Ba + I, Ba + J, Bb + J, Ba + I, Bb + J, Bb + I});
	}
}

FAcArtList::FVPoly FAcArtList::ClipV(const FVPoly& InP, const FAcPoly& C)
{
	FVPoly P = InP;
	for (int32 E = 0; E < C.Num() && P.Num() >= 3; ++E)
	{
		const FVector2D A = C[E], Dir = C[(E + 1) % C.Num()] - A;
		FVPoly Out;
		const int32 N = P.Num();
		for (int32 I = 0; I < N; ++I)
		{
			const FV& Cur = P[I];
			const FV& Next = P[(I + 1) % N];
			const double Dc = Cross(Dir, Cur.P - A), Dn = Cross(Dir, Next.P - A);
			if (Dc >= 0) Out.Add(Cur);
			if ((Dc >= 0) != (Dn >= 0))
			{
				const double T = Dc / (Dc - Dn);
				Out.Add({Cur.P + (Next.P - Cur.P) * T, Cur.C + (Next.C - Cur.C) * (float)T});
			}
		}
		P = MoveTemp(Out);
	}
	return P.Num() >= 3 ? P : FVPoly();
}

template <typename F>
FAcArtList::FVPoly FAcArtList::Shade(const FAcPoly& P, F&& Of)
{
	FVPoly Out;
	Out.Reserve(P.Num());
	for (const FVector2D& Q : P) Out.Add({Q, Of(Q)});
	return Out;
}

void FAcArtList::Fill(const FAcPoly& InP, const FLinearColor& Color, const bool bSmooth)
{
	const FAcPoly P = AcArt::Clean(InP);
	if (P.Num() < 3 || Color.A <= 0) return;
	auto Solid = [&Color](FVector2D) { return Color; };
	if (bSmooth)
	{
		const FAcPoly Inner = AcArt::Grown(P, -Feather());
		const double A = AcArt::Area(Inner);
		if (A > 1e-9 && A < AcArt::Area(P))
		{
			Fan(Shade(Inner, Solid));
			Band(Shade(Inner, Solid), Shade(AcArt::Grown(P, Feather()), [&Color](FVector2D) { return AcArt::WithAlpha(Color, 0); }), true);
			return;
		}
	}
	Fan(Shade(P, Solid));
}

void FAcArtList::Fill(const FAcRegion& R, const FLinearColor& Color)
{
	if (R.IsSingle()) Fill(R.Pieces[0], Color, true);
	else for (const FAcPoly& P : R.Pieces) Fill(P, Color, false);
}

void FAcArtList::Linear(const FAcRegion& Region, const FAcStops& Stops, const FVector2D A, const FVector2D B)
{
	const FVector2D Ab = B - A;
	const double Len2 = Ab.SizeSquared();
	if (Len2 < 1e-12) return;
	auto T = [&](FVector2D P) { return ((P - A) | Ab) / Len2; };
	// Bands at the stops and every 8 points or so: the colour is mixed per
	// point, as Core Graphics mixes it, not across a long triangle.
	TArray<double> Cuts = {0, 1};
	for (const double S : Stops.At) Cuts.Add(FMath::Clamp(S, 0.0, 1.0));
	const int32 Sub = FMath::Clamp((int32)FMath::CeilToDouble(FMath::Sqrt(Len2) / 8), 1, 64);
	for (int32 K = 1; K < Sub; ++K) Cuts.Add((double)K / Sub);
	Cuts.Sort();
	const FVector2D Perp(Ab.Y, -Ab.X);  // left of it is toward B
	auto Bands = [&](const FAcPoly& Piece)
	{
		for (int32 K = 0; K + 1 < Cuts.Num(); ++K)
		{
			if (Cuts[K + 1] - Cuts[K] < 1e-9) continue;
			const FVector2D P0 = A + Ab * Cuts[K], P1 = A + Ab * Cuts[K + 1];
			FAcPoly Slice = AcArt::ClipHalf(Piece, P0, P0 + Perp);
			Slice = AcArt::ClipHalf(Slice, P1, P1 - Perp);
			if (Slice.Num() >= 3) Fan(Shade(Slice, [&](FVector2D P) { return Stops.Color(T(P)); }));
		}
	};
	if (Region.IsSingle())
	{
		const FAcPoly P = AcArt::Clean(Region.Pieces[0]);
		const FAcPoly Inner = AcArt::Grown(P, -Feather());
		const double Ai = AcArt::Area(Inner);
		if (Ai > 1e-9 && Ai < AcArt::Area(P))
		{
			Bands(Inner);
			auto Edge = [&](FVector2D Q)
			{
				const double U = T(Q);
				return U < -1e-6 || U > 1 + 1e-6 ? AcArt::WithAlpha(Stops.Color(U), 0) : Stops.Color(U);
			};
			auto Out = [&](FVector2D Q) { return AcArt::WithAlpha(Stops.Color(T(Q)), 0); };
			Band(Shade(Inner, Edge), Shade(AcArt::Grown(P, Feather()), Out), true);
			return;
		}
	}
	for (const FAcPoly& P : Region.Pieces) Bands(P);
}

void FAcArtList::Radial(const FAcRegion& Region, const FAcStops& Stops, const FVector2D C, const double R)
{
	if (R <= 0) return;
	FBox2D Circle(C - FVector2D(R), C + FVector2D(R));
	TArray<FBox2D> Boxes;
	for (const FAcPoly& P : Region.Pieces) Boxes.Add(BoundsOf(P));
	constexpr int32 Rings = 8, Around = 48;
	auto At = [&](double Rad, double A) { return FV{C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rad, Stops.Color(Rad / R)}; };
	for (int32 K = 0; K < Boxes.Num(); ++K)
	{
		if (!Boxes[K].Intersect(Circle)) continue;
		for (int32 J = 0; J < Rings; ++J)
		{
			const double R0 = R * J / Rings, R1 = R * (J + 1) / Rings;
			for (int32 I = 0; I < Around; ++I)
			{
				const double A0 = 2 * Pi * I / Around, A1 = 2 * Pi * (I + 1) / Around;
				FVPoly Cell = J == 0 ? FVPoly{FV{C, Stops.Color(0)}, At(R1, A0), At(R1, A1)}
					: FVPoly{At(R0, A0), At(R1, A0), At(R1, A1), At(R0, A1)};
				FBox2D Cb(ForceInit);
				for (const FV& V : Cell) Cb += V.P;
				if (!Cb.Intersect(Boxes[K])) continue;
				Fan(ClipV(Cell, Region.Pieces[K]));
			}
		}
	}
}

void FAcArtList::Stroke(const TArray<FVector2D>& InLine, const bool bClosed, const FLinearColor& Color, const double Width,
	const bool bRound)
{
	if (Color.A <= 0 || Width <= 0) return;
	TArray<FVector2D> L;
	for (const FVector2D& Q : InLine)
	{
		if (L.IsEmpty() || !L.Last().Equals(Q, 1e-7)) L.Add(Q);
	}
	if (bClosed) while (L.Num() > 2 && L.Last().Equals(L[0], 1e-7)) L.Pop();
	if (L.Num() < 2) return;
	if (bRound && !bClosed)
	{
		L[0] -= (L[1] - L[0]).GetSafeNormal() * (Width / 2);
		L.Last() += (L.Last() - L[L.Num() - 2]).GetSafeNormal() * (Width / 2);
	}
	// Anti-aliased: a pixel-wide ramp either side; thinner than a pixel, a
	// fainter line a pixel wide (as much ink).
	const double Px = Width * PxPerPt, F = Feather(), Hw = Width / 2;
	const double Core = Px >= 1 ? Hw - F : 0, Outer = Hw + F;
	const FLinearColor Peak = AcArt::WithAlpha(Color, Color.A * FMath::Min(1.0, Px));
	const FLinearColor Clear = AcArt::WithAlpha(Color, 0);
	auto Line = [&](double D, const FLinearColor& C)
	{
		FVPoly Out;
		for (const FVector2D& Q : AcArt::Offset(L, D, bClosed)) Out.Add({Q, C});
		return Out;
	};
	const FVPoly Lo = Line(Outer, Clear), Lc = Line(Core, Peak), Rc = Line(-Core, Peak), Ro = Line(-Outer, Clear);
	Band(Lo, Lc, bClosed);
	if (Core > 0) Band(Lc, Rc, bClosed);
	Band(Rc, Ro, bClosed);
}

void FAcArtList::Stroke(const TArray<FVector2D>& Line, const FAcRegion& Clip, const FLinearColor& Color, const double Width,
	const bool bRound)
{
	for (const TArray<FVector2D>& Run : AcArt::ClipLine(Line, Clip)) Stroke(Run, false, Color, Width, bRound);
}

// MARK: - Shadows

template <typename F>
void FAcArtList::Rings(const FAcPoly& P, const TArray<double>& Ds, F&& Of, const bool bFillInside)
{
	const double Area = AcArt::Area(P);
	TArray<FAcPoly> Gs;
	int32 First = INDEX_NONE;
	for (int32 K = 0; K < Ds.Num(); ++K)
	{
		Gs.Add(AcArt::Grown(P, Ds[K]));
		const double A = AcArt::Area(Gs.Last());
		if (First == INDEX_NONE && A > 1e-9 && (Ds[K] >= 0 || A < Area)) First = K;
	}
	if (First == INDEX_NONE) return;
	auto Colored = [&](int32 K)
	{
		const FLinearColor C = Of(Ds[K]);
		return Shade(Gs[K], [&C](FVector2D) { return C; });
	};
	if (bFillInside) Fan(Colored(First));
	for (int32 K = First; K + 1 < Ds.Num(); ++K) Band(Colored(K), Colored(K + 1), true);
}

template <typename F>
void FAcArtList::OpenRings(const TArray<FVector2D>& Line, const TArray<double>& Ds, F&& Of)
{
	auto Colored = [&](int32 K)
	{
		const FLinearColor C = Of(Ds[K]);
		FVPoly Out;
		for (const FVector2D& Q : AcArt::Offset(Line, Ds[K], false)) Out.Add({Q, C});
		return Out;
	};
	for (int32 K = 0; K + 1 < Ds.Num(); ++K) Band(Colored(K), Colored(K + 1), false);
}

namespace
{
	TArray<double> Steps(const double From, const double To, const int32 N)
	{
		TArray<double> Out;
		for (int32 I = 0; I <= N; ++I) Out.Add(From + (To - From) * I / N);
		return Out;
	}
}

void FAcArtList::BoxShadow(const FBox2D& Box, const double Sigma, const FLinearColor& Color)
{
	const FBox2D B = Box.ExpandBy(3 * Sigma);
	const FVector2D S = B.GetSize();
	const int32 Nx = FMath::Clamp((int32)FMath::CeilToDouble(S.X / (Sigma * 0.75)), 2, 48);
	const int32 Ny = FMath::Clamp((int32)FMath::CeilToDouble(S.Y / (Sigma * 0.75)), 2, 48);
	auto Cover = [&](double V, double Lo, double Hi) { return Phi((V - Lo) / Sigma) - Phi((V - Hi) / Sigma); };
	const uint32 Base = (uint32)Verts.Num();
	for (int32 J = 0; J <= Ny; ++J)
	{
		for (int32 I = 0; I <= Nx; ++I)
		{
			const FVector2D P(B.Min.X + S.X * I / Nx, B.Min.Y + S.Y * J / Ny);
			const double A = Color.A * Cover(P.X, Box.Min.X, Box.Max.X) * Cover(P.Y, Box.Min.Y, Box.Max.Y);
			Add(P, AcArt::WithAlpha(Color, A));
		}
	}
	for (int32 J = 0; J < Ny; ++J)
	{
		for (int32 I = 0; I < Nx; ++I)
		{
			const uint32 A = Base + J * (Nx + 1) + I, Bq = A + 1, C = A + Nx + 1, D = C + 1;
			Indices.Append({A, Bq, D, A, D, C});
		}
	}
}

void FAcArtList::Shadow(const FAcPoly& InP, const FVector2D Offset, const double Blur, const FLinearColor& Color)
{
	const FAcPoly P = AcArt::Clean(AcArt::Moved(InP, Offset / PxPerPt));
	if (P.Num() < 3 || Color.A <= 0) return;
	const double Sigma = Blur * SigmaPerBlur / PxPerPt;
	if (Sigma * PxPerPt < 0.35)
	{
		Fill(P, Color);
		return;
	}
	const FBox2D Box = BoundsOf(P);
	if (FMath::Min(Box.GetSize().X, Box.GetSize().Y) < 6 * Sigma)
	{
		BoxShadow(Box, Sigma, Color);
		return;
	}
	Rings(P, Steps(-3 * Sigma, 3 * Sigma, 12),
		[&](double D) { return AcArt::WithAlpha(Color, Color.A * (1 - Phi(D / Sigma))); }, true);
}

void FAcArtList::StrokeShadow(const FAcPoly& InP, const double Width, const double Blur, const FLinearColor& Color)
{
	const FAcPoly P = AcArt::Clean(InP);
	if (P.Num() < 3 || Color.A <= 0) return;
	const double Sigma = FMath::Max(Blur * SigmaPerBlur, 0.25) / PxPerPt, Hw = Width / 2, Reach = Hw + 3 * Sigma;
	Rings(P, Steps(-Reach, Reach, 18),
		[&](double D) { return AcArt::WithAlpha(Color, Color.A * (Phi((D + Hw) / Sigma) - Phi((D - Hw) / Sigma))); }, true);
}

void FAcArtList::LineShadow(const TArray<FVector2D>& Line, const double Width, const double Blur, const FLinearColor& Color)
{
	if (Line.Num() < 2 || Color.A <= 0) return;
	const double Sigma = FMath::Max(Blur * SigmaPerBlur, 0.25) / PxPerPt, Hw = Width / 2, Reach = Hw + 3 * Sigma;
	OpenRings(Line, Steps(-Reach, Reach, 18),
		[&](double D) { return AcArt::WithAlpha(Color, Color.A * (Phi((D + Hw) / Sigma) - Phi((D - Hw) / Sigma))); });
}

void FAcArtList::EdgeShadow(const TArray<FVector2D>& Edge, const FVector2D Offset, const double Blur, const FLinearColor& Color)
{
	if (Edge.Num() < 2 || Color.A <= 0) return;
	TArray<FVector2D> L = Edge;
	for (FVector2D& Q : L) Q += Offset / PxPerPt;
	const double Sigma = Blur * SigmaPerBlur / PxPerPt;
	OpenRings(L, Steps(-3 * Sigma, 3 * Sigma, 12), [&](double D) { return AcArt::WithAlpha(Color, Color.A * (1 - Phi(D / Sigma))); });
}

void FAcArtList::InnerShadow(const FAcPoly& InHole, const FVector2D Offset, const double Blur, const FLinearColor& Color)
{
	const FAcPoly Hole = AcArt::Clean(InHole);
	if (Hole.Num() < 3 || Color.A <= 0) return;
	const FAcPoly Shifted = AcArt::Moved(Hole, Offset / PxPerPt);
	const double Sigma = Blur * SigmaPerBlur / PxPerPt;
	// Rings round the hole moved by the offset, from outside it (where the
	// frame overhangs, darkest) to 3σ in; each cut to the hole itself.
	const TArray<double> Ds = Steps(Offset.Size() / PxPerPt + 1, -3 * Sigma, 14);
	TArray<FVPoly> Rows;
	for (const double D : Ds)
	{
		const FLinearColor C = AcArt::WithAlpha(Color, Color.A * Phi(D / Sigma));
		Rows.Add(Shade(AcArt::Grown(Shifted, D), [&C](FVector2D) { return C; }));
	}
	for (int32 K = 0; K + 1 < Rows.Num(); ++K)
	{
		const FVPoly& A = Rows[K];
		const FVPoly& B = Rows[K + 1];
		if (AcArt::Area(AcArt::Grown(Shifted, Ds[K + 1])) <= 1e-9) break;
		for (int32 I = 0; I < A.Num(); ++I)
		{
			const int32 J = (I + 1) % A.Num();
			Fan(ClipV(FVPoly{A[I], A[J], B[J], B[I]}, Hole));
		}
	}
}

// MARK: - Replay

int32 FAcArtList::Paint(FSlateWindowElementList& Out, const FGeometry& Geometry, const int32 Layer, const double ViewHeight,
	const FVector2D Offset, const float Opacity, const ESlateDrawEffect Effects) const
{
	if (Indices.IsEmpty() || !FSlateApplication::IsInitialized()) return Layer;
	// The white brush sits in Slate's atlas: sample the middle of its own region.
	const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FAcHudStyle::White());
	const FSlateShaderResourceProxy* Proxy = Handle.GetResourceProxy();
	const FVector2f Uv = Proxy ? Proxy->StartUV + Proxy->SizeUV * 0.5f : FVector2f(0.5f, 0.5f);
	const FSlateRenderTransform& T = Geometry.GetAccumulatedRenderTransform();
	TArray<FSlateVertex> V;
	V.Reserve(Verts.Num());
	const float Ox = (float)Offset.X, Oy = (float)Offset.Y, H = (float)ViewHeight;
	for (int32 I = 0; I < Verts.Num(); ++I)
	{
		FColor C = Colors[I];
		if (Opacity < 1) C.A = (uint8)FMath::RoundToInt(C.A * FMath::Clamp(Opacity, 0.0f, 1.0f));
		V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(Verts[I].X + Ox, H - (Verts[I].Y + Oy)), Uv, C));
	}
	TArray<SlateIndex> I;
	I.Reserve(Indices.Num());
	for (const uint32 X : Indices) I.Add((SlateIndex)X);
	FSlateDrawElement::MakeCustomVerts(Out, Layer, Handle, V, I, nullptr, 0, 0, Effects);
	return Layer + 1;
}
