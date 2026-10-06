#include "AcCab.h"

// MARK: - Projection (ConsoleCab.swift:739)

FAcProjection::FAcProjection()
{
	for (int32 I = 0; I < 3; ++I)
		for (int32 J = 0; J < 3; ++J) M[I][J] = I == J ? 1.0 : 0.0;
}

FAcProjection::FAcProjection(const FAcRect& R, const FVector2D Q[4])
{
	const double X[4] = {Q[0].X, Q[1].X, Q[2].X, Q[3].X};
	const double Y[4] = {Q[0].Y, Q[1].Y, Q[2].Y, Q[3].Y};
	const double Dx1 = X[1] - X[2], Dx2 = X[3] - X[2], Dx3 = X[0] - X[1] + X[2] - X[3];
	const double Dy1 = Y[1] - Y[2], Dy2 = Y[3] - Y[2], Dy3 = Y[0] - Y[1] + Y[2] - Y[3];
	const double Den = Dx1 * Dy2 - Dx2 * Dy1;
	const double G = (Dx3 * Dy2 - Dx2 * Dy3) / Den, H = (Dx1 * Dy3 - Dx3 * Dy1) / Den;
	const double S[3][3] = {
		{X[1] - X[0] + G * X[1], X[3] - X[0] + H * X[3], X[0]},
		{Y[1] - Y[0] + G * Y[1], Y[3] - Y[0] + H * Y[3], Y[0]},
		{G, H, 1},
	};
	const double W = R.W, Ht = R.H;
	const double U[3][3] = {{1 / W, 0, -R.X / W}, {0, 1 / Ht, -R.Y / Ht}, {0, 0, 1}};
	for (int32 I = 0; I < 3; ++I)
		for (int32 J = 0; J < 3; ++J)
		{
			M[I][J] = S[I][0] * U[0][J] + S[I][1] * U[1][J] + S[I][2] * U[2][J];
		}
}

FVector2D FAcProjection::operator()(const FVector2D P) const
{
	const double X = M[0][0] * P.X + M[0][1] * P.Y + M[0][2];
	const double Y = M[1][0] * P.X + M[1][1] * P.Y + M[1][2];
	const double Z = M[2][0] * P.X + M[2][1] * P.Y + M[2][2];
	return FVector2D(X / Z, Y / Z);
}

FAcProjection FAcProjection::Inverse() const
{
	const auto& A = M;
	const double C00 = A[1][1] * A[2][2] - A[1][2] * A[2][1];
	const double C01 = A[1][2] * A[2][0] - A[1][0] * A[2][2];
	const double C02 = A[1][0] * A[2][1] - A[1][1] * A[2][0];
	const double Det = A[0][0] * C00 + A[0][1] * C01 + A[0][2] * C02;
	const double K = Det != 0 ? 1.0 / Det : 0.0;
	FAcProjection R;
	R.M[0][0] = C00 * K;
	R.M[0][1] = (A[0][2] * A[2][1] - A[0][1] * A[2][2]) * K;
	R.M[0][2] = (A[0][1] * A[1][2] - A[0][2] * A[1][1]) * K;
	R.M[1][0] = C01 * K;
	R.M[1][1] = (A[0][0] * A[2][2] - A[0][2] * A[2][0]) * K;
	R.M[1][2] = (A[0][2] * A[1][0] - A[0][0] * A[1][2]) * K;
	R.M[2][0] = C02 * K;
	R.M[2][1] = (A[0][1] * A[2][0] - A[0][0] * A[2][1]) * K;
	R.M[2][2] = (A[0][0] * A[1][1] - A[0][1] * A[1][0]) * K;
	return R;
}

// MARK: - Layout

FVector2D FAcCabLayout::MinimapSizeFor(const double Width, const double Depth, const double Height)
{
	double H = Height;
	double W = H * Width / FMath::Max(Depth, 1e-6);
	if (W > Height * 1.8)
	{
		W = Height * 1.8;
		H = W * Depth / FMath::Max(Width, 1e-6);
	}
	return FVector2D(W, H);
}

FAcCabLayout FAcCabLayout::Make(const FVector2D View, const FVector2D MinimapSize, const bool bInWarped)
{
	FAcCabLayout L;
	L.Size = View;
	L.bWarped = bInWarped;
	const double W = View.X;
	const FVector2D MapSize = MinimapSize.X > 0 && MinimapSize.Y > 0 ? MinimapSize : FVector2D(220, 170);

	// Console.layOut
	const double CardW = Columns * Button + (Columns - 1) * Gap;
	const double CardH = Rows * Button + (Rows - 1) * Gap;
	const double SideH = CardH + 40, CenterH = SideH + 14;
	const double MapH = CardH + 8;
	const double MapW = MapH * MapSize.X / FMath::Max(MapSize.Y, 1.0);
	const double Deck = FMath::Min(FMath::Max(W * 0.09, 110.0), 170.0);
	const double Edge = FMath::Max(W * 0.1, Pillar(W) + 12 + Deck + 24);
	L.MapWell = FAcRect(Edge, 14, MapW + 16, MapH + 8);
	L.CardWell = FAcRect(W - Edge - CardW - 16, 14, CardW + 16, CardH + 16);
	const double Cx0 = L.MapWell.MaxX() + 40, Cx1 = L.CardWell.MinX() - 40;
	L.CenterWell = FAcRect(Cx0, 14, FMath::Max(Cx1 - Cx0, 200.0), CenterH - 30);
	const double K = (L.MapWell.H - 8) / FMath::Max(MapSize.Y, 1.0);
	L.MinimapScale = K;
	L.Minimap = FAcRect(L.MapWell.MinX() + 8 * K, L.MapWell.MinY() + 4, MapSize.X * K, MapSize.Y * K);

	// Cab.Layout.init(size:map:center:card:top:)
	const double T = FMath::RoundHalfFromZero(SideH + 22);
	L.FaceTop = T;
	const double Rise = bInWarped ? FMath::RoundHalfFromZero(T * 0.36) : 0.0;
	const double Lean = bInWarped ? FMath::RoundHalfFromZero(T * 0.14) : 0.0;
	const double Flare = bInWarped ? FMath::RoundHalfFromZero(T * 0.05) : 0.0;
	const double Depth = 18, Recede = FMath::RoundHalfFromZero(Depth * 0.6);
	const FAcRect& C = L.CenterWell;
	const double HoodL = C.MinX() - 20, HoodR = C.MaxX() + 20;
	const FVector2D FootL(HoodL - Flare, 0), FootR(HoodR + Flare, 0);
	const FVector2D BrowL(HoodL + Lean, T), BrowR(HoodR - Lean, T);
	const FVector2D OuterL(0, T + Rise), OuterR(W, T + Rise);
	L.Faces[0].Flat = FAcRect(0, 0, HoodL, T);
	L.Faces[0].Quad[0] = FVector2D(0, 0);
	L.Faces[0].Quad[1] = FootL;
	L.Faces[0].Quad[2] = BrowL;
	L.Faces[0].Quad[3] = OuterL;
	L.Faces[1].Flat = FAcRect(HoodL, 0, HoodR - HoodL, T);
	L.Faces[1].Quad[0] = FootL;
	L.Faces[1].Quad[1] = FootR;
	L.Faces[1].Quad[2] = BrowR;
	L.Faces[1].Quad[3] = BrowL;
	L.Faces[2].Flat = FAcRect(HoodR, 0, W - HoodR, T);
	L.Faces[2].Quad[0] = FootR;
	L.Faces[2].Quad[1] = FVector2D(W, 0);
	L.Faces[2].Quad[2] = OuterR;
	L.Faces[2].Quad[3] = BrowR;
	L.Brow[0] = OuterL;
	L.Brow[1] = BrowL;
	L.Brow[2] = BrowR;
	L.Brow[3] = OuterR;
	L.Skyline[0] = FVector2D(0, OuterL.Y + Depth);
	L.Skyline[1] = FVector2D(BrowL.X + Recede, T + Depth);
	L.Skyline[2] = FVector2D(BrowR.X - Recede, T + Depth);
	L.Skyline[3] = FVector2D(W, OuterR.Y + Depth);
	const double RailH = Rail(View.Y), PillarW = Pillar(W);
	L.Window = FAcRect(PillarW, T + 4, W - 2 * PillarW, View.Y - RailH - T - 4);
	L.Blocks[0] = FAcRect(L.Window.MinX() + 12, 22, L.MapWell.MinX() - 24 - L.Window.MinX() - 12, T - 52);
	L.Blocks[1] = FAcRect(L.CardWell.MaxX() + 24, 22, L.Window.MaxX() - 12 - L.CardWell.MaxX() - 24, T - 52);

	// Console.apply: over the dashboard, clear of its sides rising to the pillars.
	const double Over = L.Top() + 14;
	L.NoteAt = FVector2D(L.CardWell.MidX(), Over + 36);
	L.ViewSwitch = FAcRect(L.CardWell.MaxX() - 168, Over, 168, 24);
	return L;
}

double FAcCabLayout::Top() const
{
	double M = Skyline[0].Y;
	for (const FVector2D& P : Skyline) M = FMath::Max(M, P.Y);
	return M;
}

double FAcCabLayout::DashTop(const double X) const
{
	for (int32 I = 0; I + 1 < 4; ++I)
	{
		const FVector2D A = Skyline[I], B = Skyline[I + 1];
		if (X <= B.X)
		{
			return A.Y + (B.Y - A.Y) * FMath::Max(0.0, X - A.X) / FMath::Max(B.X - A.X, 1.0);
		}
	}
	return Skyline[3].Y;
}

FAcRect FAcCabLayout::SlotRect(const int32 Slot) const
{
	const int32 C = Slot % Columns, Row = Slot / Columns;
	return FAcRect(CardWell.MinX() + 8 + C * (Button + Gap), CardWell.MaxY() - 8 - Button - Row * (Button + Gap), Button, Button);
}

FAcRect FAcCabLayout::DeckScreen(const FAcRect& Block)
{
	return FAcRect(Block.MinX() + 6, Block.MinY() + 6, Block.W - 12, Block.H - 20);
}

int32 FAcCabLayout::FaceAt(const FVector2D P, FVector2D* OutFlat) const
{
	for (int32 K = 0; K < 3; ++K)
	{
		const FVector2D Q = Faces[K].Projection().Inverse()(P);
		// Half-open on the right so a point on a shared edge has one face.
		if (Q.X >= Faces[K].Flat.MinX() && Q.X < Faces[K].Flat.MaxX() && Q.Y >= Faces[K].Flat.MinY() && Q.Y <= Faces[K].Flat.MaxY())
		{
			if (OutFlat) *OutFlat = Q;
			return K;
		}
	}
	return -1;
}
