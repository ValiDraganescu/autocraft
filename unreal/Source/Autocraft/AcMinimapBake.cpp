#include "AcMinimapBake.h"

#include "Async/ParallelFor.h"

#include "Projection.h"
#include "TerrainField.h"

namespace
{
	uint8 Byte(double V) { return uint8(FMath::Clamp(V, 0.0, 1.0) * 255); }
	uint8 AlphaByte(double A) { return uint8(FMath::Clamp(A, 0.0, 1.0) * 255 + 0.5); }

	/// Distance from `P` to the segment `A`-`B`.
	double SegmentDistance(FVector2D P, FVector2D A, FVector2D B)
	{
		const FVector2D AB = B - A;
		const double T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1e-12), 0.0, 1.0);
		return FVector2D::Distance(P, A + AB * T);
	}
}

FAcArtImage AcMinimapBake::Terrain(const ac::TerrainField& Field, const ac::GroundRect& B, const double PixelsPerCell)
{
	FAcArtImage I;
	const int32 W = int32(B.width() * PixelsPerCell), H = int32(B.depth() * PixelsPerCell);
	if (W <= 0 || H <= 0) return I;
	I.Width = W;
	I.Height = H;
	I.Points = FVector2D(W, H);
	I.Pixels.SetNumUninitialized(W * H);
	const double Lh = Field.map.levelHeight;
	const auto Height = [&Field](ac::Vec2 P) { return Field.height(P); };
	ParallelFor(H, [&](const int32 Y)
	{
		// Bitmap row 0 is the top of the image: the far edge (min y).
		const double Z = B.minZ + (double(Y) + 0.5) / PixelsPerCell;
		for (int32 X = 0; X < W; ++X)
		{
			const ac::Vec2 P(B.minX + (double(X) + 0.5) / PixelsPerCell, Z);
			FColor& Px = I.Pixels[Y * W + X];
			// Out of play: black.
			if (!Field.map.inPlay(P, 0, Height))
			{
				Px = FColor(8, 8, 10, 255);
				continue;
			}
			const double Hgt = Field.height(P);
			const double E = 0.5;
			const double Dx = Field.height(P + ac::Vec2(E, 0)) - Field.height(P - ac::Vec2(E, 0));
			const double Dz = Field.height(P + ac::Vec2(0, E)) - Field.height(P - ac::Vec2(0, E));
			const ac::TerrainField::Materials M = Field.materials(P);
			const double T = FMath::Min(FMath::Max(Hgt / Lh, 0.0), 1.5);
			FVector C = FVector(0.36, 0.22, 0.13) * (1 - T) + FVector(0.55, 0.45, 0.3) * FMath::Min(T, 1.0);
			C = C * (1 - M.x) + FVector(0.25, 0.36, 0.14) * M.x;
			C = C * (1 - M.z) + FVector(0.5, 0.52, 0.55) * M.z;
			// Light from the upper left; cliffs go dark.
			const double Shade = 1 - 0.35 * (Dx + Dz);
			const double Slope = FMath::Abs(Dx) + FMath::Abs(Dz);
			C *= Shade * (Slope > 1.2 ? 0.45 : 1);
			Px = FColor(Byte(C.X), Byte(C.Y), Byte(C.Z), 255);
		}
	});
	return I;
}

FAcArtImage AcMinimapBake::Disc(const int32 Size)
{
	FAcArtImage I;
	I.Width = I.Height = Size;
	I.Points = FVector2D(Size, Size);
	I.Pixels.SetNumUninitialized(Size * Size);
	const double R = Size / 2.0;
	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const double D = FVector2D(X + 0.5 - R, Y + 0.5 - R).Size();
			I.Pixels[Y * Size + X] = FColor(255, 255, 255, AlphaByte(R - D + 0.5));
		}
	}
	return I;
}

FAcArtImage AcMinimapBake::Diamond(const int32 Size)
{
	FAcArtImage I;
	I.Width = I.Height = Size;
	I.Points = FVector2D(Size, Size);
	I.Pixels.SetNumUninitialized(Size * Size);
	const double R = Size / 2.0;
	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			// Signed distance to the edge |x| + |y| = R, in pixels.
			const double D = (FMath::Abs(X + 0.5 - R) + FMath::Abs(Y + 0.5 - R) - R) / UE_SQRT_2;
			I.Pixels[Y * Size + X] = FColor(255, 255, 255, AlphaByte(0.5 - D));
		}
	}
	return I;
}

FAcArtImage AcMinimapBake::SightFan(const double Reach, const double Half, const double Pad, const double Line,
	const double Scale)
{
	// Minimap.sightFan: a white wedge fading with distance (0.6 at the apex),
	// its two edges brighter (1 at the apex), round caps.
	FAcArtImage I;
	const double Wpt = 2 * Reach * FMath::Sin(Half) + 2 * Pad, Hpt = Reach + 2 * Pad;
	I.Points = FVector2D(Wpt, Hpt);
	I.Width = FMath::Max(1, int32(FMath::CeilToDouble(Wpt * Scale)));
	I.Height = FMath::Max(1, int32(FMath::CeilToDouble(Hpt * Scale)));
	I.Pixels.SetNumUninitialized(I.Width * I.Height);
	// In points, y up from the picture's bottom.
	const FVector2D Apex(Wpt / 2, Pad);
	const FVector2D EdgeL = Apex + Reach * FVector2D(FMath::Cos(UE_HALF_PI + Half), FMath::Sin(UE_HALF_PI + Half));
	const FVector2D EdgeR = Apex + Reach * FVector2D(FMath::Cos(UE_HALF_PI - Half), FMath::Sin(UE_HALF_PI - Half));
	const double Px = 1 / Scale;
	for (int32 Row = 0; Row < I.Height; ++Row)
	{
		for (int32 Col = 0; Col < I.Width; ++Col)
		{
			const FVector2D P((Col + 0.5) / Scale, Hpt - (Row + 0.5) / Scale);
			const FVector2D D = P - Apex;
			const double Dist = D.Size();
			const double Fade = Dist < Reach ? 1 - Dist / Reach : 0;
			// Inside the wedge: within `Half` of straight up, antialiased at its sides.
			double In = 0;
			if (Dist < Reach + Px)
			{
				const double Off = FMath::Abs(FMath::Atan2(D.X, FMath::Max(D.Y, 1e-9)));
				const double Side = Dist * FMath::Sin(FMath::Max(Off - Half, 0.0));
				In = D.Y <= 0 ? 0 : FMath::Clamp(0.5 - (Off > Half ? Side : -Dist * FMath::Sin(Half - Off)) / Px, 0.0, 1.0);
			}
			const double Fan = 0.6 * Fade * In;
			const double ToEdge = FMath::Min(SegmentDistance(P, Apex, EdgeL), SegmentDistance(P, Apex, EdgeR));
			const double OnEdge = FMath::Clamp((Line / 2 - ToEdge) / Px + 0.5, 0.0, 1.0);
			const double Edge = Fade * OnEdge;
			// The edges are drawn over the wedge.
			const double A = Edge + Fan * (1 - Edge);
			I.Pixels[Row * I.Width + Col] = FColor(255, 255, 255, AlphaByte(A));
		}
	}
	return I;
}
