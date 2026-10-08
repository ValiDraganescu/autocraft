#include "AcCabArt.h"

#include "AcArtList.h"
#include "AcBakedArt.h"
#include "AcHudStyle.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "UObject/Package.h"

UTexture2D* AcCabArt::ToTexture(const FAcArtImage& Image, const TCHAR* Name)
{
	if (!Image.IsValid()) return nullptr;
	UTexture2D* T = UTexture2D::CreateTransient(Image.Width, Image.Height, PF_B8G8R8A8,
		// A unique name: a second texture under a name still held would replace a live object.
		MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), FName(Name)));
	if (!T) return nullptr;
	T->SRGB = true;
	T->Filter = TF_Bilinear;
	T->AddressX = TA_Clamp;
	T->AddressY = TA_Clamp;
	T->LODGroup = TEXTUREGROUP_UI;
	T->NeverStream = true;
	FTexture2DMipMap& Mip = T->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Image.Pixels.GetData(), Image.Pixels.Num() * sizeof(FColor));
	// Slate on the Mac writes pow(1/2.2), not sRGB: re-encode so the art
	// lands on screen as Core Graphics drew it (FAcHudStyle::Srgb).
	FAcHudStyle::CompensateForSlate(TArrayView<FColor>(static_cast<FColor*>(Data), Image.Pixels.Num()));
	Mip.BulkData.Unlock();
	T->UpdateResource();
	return T;
}


// MARK: - The drawing (Swift's `Chrome` and `Cab`, call for call)
//
// Keep the order of the random draws (`SplitMix`) as Swift evaluates them:
// C++ does not sequence function arguments, so each draw is its own
// statement. Core Graphics' clips are the regions passed to each call.

namespace
{
	using namespace AcArt;

	constexpr double Pi = 3.14159265358979323846;

	/// HUDChrome.swift `SplitMix`.
	struct FSplitMix
	{
		uint64 State;
		explicit FSplitMix(uint64 Seed) : State(Seed + 0x9E3779B97F4A7C15ull) {}
		uint64 Next()
		{
			State += 0x9E3779B97F4A7C15ull;
			uint64 Z = State;
			Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
			Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
			return Z ^ (Z >> 31);
		}
		double Unit() { return (double)(Next() >> 11) / (double)(1ull << 53); }
	};

	FLinearColor Cyan() { return Rgb(0.35, 0.85, 1); }
	FLinearColor Ice() { return Rgb(0.8, 0.95, 1); }
	FLinearColor Glass() { return Rgb(0.01, 0.035, 0.07); }

	FAcRect RectOf(const FBox2D& B) { return FAcRect(B.Min.X, B.Min.Y, B.Max.X - B.Min.X, B.Max.Y - B.Min.Y); }
	FBox2D BoxOf(const FAcPoly& P)
	{
		FBox2D B(ForceInit);
		for (const FVector2D& Q : P) B += Q;
		return B;
	}
	TArray<FVector2D> Closed(const FAcPoly& P)
	{
		TArray<FVector2D> L = P;
		if (!L.IsEmpty()) L.Add(P[0]);
		return L;
	}

	/// The glow Core Graphics' `setShadow` leaves on everything drawn after
	/// it (Swift's deck bay leaves one on: see DeckBay). Null: none.
	struct FGlow
	{
		FLinearColor Color;
		double Blur = 4;
	};

	void Stroke(FAcArtList& G, const TArray<FVector2D>& L, bool bClosed, const FLinearColor& C, double Width,
		const FGlow* Glow = nullptr)
	{
		if (Glow)
		{
			const FLinearColor S = WithAlpha(Glow->Color, Glow->Color.A * C.A);
			if (bClosed) G.StrokeShadow(L, Width, Glow->Blur, S);
			else G.LineShadow(L, Width, Glow->Blur, S);
		}
		G.Stroke(L, bClosed, C, Width);
	}
	void Fill(FAcArtList& G, const FAcPoly& P, const FLinearColor& C, const FGlow* Glow = nullptr)
	{
		if (Glow) G.Shadow(P, FVector2D::ZeroVector, Glow->Blur, WithAlpha(Glow->Color, Glow->Color.A * C.A));
		G.Fill(P, C);
	}

	// MARK: - Chrome (HUDChrome.swift)

	/// `Chrome.path`: corners cut at 45°, inset by `D`.
	FAcPoly CutPath(const FAcRect& R0, const FAcCuts Cuts, const double D = 0)
	{
		const FAcRect R = R0.Inset(D, D);
		const double K = D * 0.41;
		auto Cut = [K](double V) { return V > 0 ? FMath::Max(V - K, 0.0) : 0.0; };
		const double Tl = Cut(Cuts.TL), Tr = Cut(Cuts.TR), Br = Cut(Cuts.BR), Bl = Cut(Cuts.BL);
		const double X0 = R.MinX(), X1 = R.MaxX(), Y0 = R.MinY(), Y1 = R.MaxY();
		return Clean({FVector2D(X0 + Bl, Y0), FVector2D(X1 - Br, Y0), FVector2D(X1, Y0 + Br), FVector2D(X1, Y1 - Tr),
			FVector2D(X1 - Tr, Y1), FVector2D(X0 + Tl, Y1), FVector2D(X0, Y1 - Tl), FVector2D(X0, Y0 + Bl)});
	}
	FAcCuts All(double V) { return FAcCuts{V, V, V, V}; }

	/// `Chrome.glowLine`: a line in its own colour's glow, an ice core in it.
	/// The glow is each stroke's shadow: the shadow's colour at `Alpha` times
	/// the stroke's alpha.
	void GlowLine(FAcArtList& G, const FAcPoly& P, const FLinearColor& Color, double Width = 1.3, double Blur = 6, double Alpha = 0.95)
	{
		const FLinearColor Line = WithAlpha(Color, Alpha), Core = WithAlpha(Ice(), Alpha * 0.6);
		G.StrokeShadow(P, Width, Blur, WithAlpha(Color, Alpha * Alpha));
		G.Stroke(P, true, Line, Width);
		G.StrokeShadow(P, Width * 0.4, Blur, WithAlpha(Color, Alpha * Core.A));
		G.Stroke(P, true, Core, Width * 0.4);
	}

	void Rim(FAcArtList& G, const FAcPoly& P, const FGlow* Glow = nullptr)
	{
		Stroke(G, P, true, Rgb(0.02, 0.03, 0.05, 1), 3, Glow);
		Stroke(G, P, true, Rgb(0.5, 0.6, 0.72, 0.8), 1, Glow);
	}

	void Steel(FAcArtList& G, const FAcPoly& P, const FAcRect& R, const int32 Seed)
	{
		const FAcRegion Clip(P);
		G.Linear(Clip, FAcStops({Rgb(0.14, 0.2, 0.3), Rgb(0.06, 0.09, 0.15), Rgb(0.03, 0.04, 0.08)}, {0, 0.45, 1}),
			FVector2D(0, R.MaxY()), FVector2D(0, R.MinY()));
		FSplitMix M((uint64)Seed);
		const int32 N = (int32)(R.W * R.H / 900) + 4;
		for (int32 I = 0; I < N; ++I)
		{
			const double Y = R.MinY() + M.Unit() * R.H;
			const double X = R.MinX() + M.Unit() * R.W;
			const double White = M.Unit() < 0.5 ? 1 : 0;
			const double Len = 20 + M.Unit() * 70;
			G.Stroke({FVector2D(X, Y), FVector2D(X + Len, Y)}, Clip, Gray(White, 0.04), 0.7);
		}
	}

	void Leds(FAcArtList& G, const FVector2D A, const int32 Count, const FLinearColor& Color, const FVector2D Size, const double Gap)
	{
		for (int32 K = 0; K < Count; ++K)
		{
			const FAcPoly R = RectPoly(FAcRect(A.X + K * (Size.X + Gap), A.Y, Size.X, Size.Y));
			G.Shadow(R, FVector2D::ZeroVector, 5, WithAlpha(Color, 0.9));
			G.Fill(R, WithAlpha(Color, 0.9));
		}
	}

	/// `Chrome.screen`: a sunk screen of dark glass. `Haze`: the cyan the
	/// deck bay's lingering glow leaves under the scan lines (see DeckBay).
	void ChromeScreen(FAcArtList& G, const FAcPoly& P, const FAcRect& R, const bool bGrid, const double Edge, const double Haze = 0)
	{
		G.Shadow(P, FVector2D(0, -1), 4, Gray(0, 1));
		G.Fill(P, Glass());
		const FAcRegion Clip(P);
		G.Radial(Clip, FAcStops({WithAlpha(Cyan(), 0.16), WithAlpha(Cyan(), 0)}, {0, 1}), FVector2D(R.MidX(), R.MinY()),
			FMath::Max(R.W, R.H) * 0.8);
		const double X0 = R.MinX(), X1 = R.MaxX(), Y0 = R.MinY(), Y1 = R.MaxY();
		if (bGrid)
		{
			for (double X = X0 + 14; X < X1; X += 14) G.Stroke({FVector2D(X, Y0), FVector2D(X, Y1)}, Clip, WithAlpha(Cyan(), 0.05), 0.6);
			for (double Y = Y0 + 14; Y < Y1; Y += 14) G.Stroke({FVector2D(X0, Y), FVector2D(X1, Y)}, Clip, WithAlpha(Cyan(), 0.05), 0.6);
		}
		if (Haze > 0) G.Fill(P, WithAlpha(Cyan(), Haze));
		for (double Y = Y0 + 1.5; Y < Y1; Y += 3) G.Stroke({FVector2D(X0, Y), FVector2D(X1, Y)}, Clip, Gray(0, 0.2), 1);
		GlowLine(G, P, Cyan(), 1, 4, Edge);
	}

	// MARK: - Cab material (ConsoleCab.swift)

	/// `Cab.armour`: scuffed navy armour over `R`, drawn in `Clip`, worst
	/// round `Wear`.
	void Armour(FAcArtList& G, const FAcRegion& Clip, const FAcRect& R, const int32 Seed, const FAcRect* Wear = nullptr)
	{
		G.Linear(Clip, FAcStops({Rgb(0.11, 0.16, 0.32), Rgb(0.07, 0.1, 0.22), Rgb(0.035, 0.05, 0.11)}, {0, 0.6, 1}),
			FVector2D(0, R.MaxY()), FVector2D(0, R.MinY()));
		FSplitMix M((uint64)Seed);
		auto Unit = [&M] { return M.Unit(); };
		auto Point = [&]
		{
			while (true)
			{
				const double X = R.MinX() + Unit() * R.W;
				const double Y = R.MinY() + Unit() * R.H;
				const FVector2D P(X, Y);
				if (!Wear || !Wear->Contains(P)) return P;
			}
		};
		double HoleArea = 0;
		if (Wear)
		{
			const double W = FMath::Min(Wear->MaxX(), R.MaxX()) - FMath::Max(Wear->MinX(), R.MinX());
			const double H = FMath::Min(Wear->MaxY(), R.MaxY()) - FMath::Max(Wear->MinY(), R.MinY());
			if (W > 0 && H > 0) HoleArea = W * H;
		}
		const double Area = FMath::Max(0.0, R.W * R.H - HoleArea);
		// Grime and paler patches.
		for (int32 I = 0, N = (int32)(Area / 7000) + 2; I < N; ++I)
		{
			const FVector2D Cc = Point();
			const double Radius = 16 + Unit() * 90;
			FLinearColor Tone, Clear;
			if (Unit() < 0.75)
			{
				const double A = 0.3 * Unit();
				Tone = Gray(0, A);
				Clear = Gray(0, 0);
			}
			else
			{
				const double A = 0.07 * Unit();
				Tone = Rgb(0.45, 0.6, 0.85, A);
				Clear = Rgb(0.45, 0.6, 0.85, 0);
			}
			G.Radial(Clip, FAcStops({Tone, Clear}, {0, 1}), Cc, Radius);
		}
		// Scratches, stroked tone by tone with round ends.
		struct FTone { FLinearColor Color; double Width; };
		const FTone Tones[4] = {{Rgb(0.75, 0.82, 0.95, 0.26), 0.6}, {Rgb(0.92, 0.58, 0.28, 0.42), 0.7},
			{Rgb(0.85, 0.55, 0.3, 0.2), 1.2}, {Gray(0, 0.5), 0.9}};
		TArray<TArray<FVector2D>> Paths[4];
		for (int32 I = 0, N = (int32)(Area / 170); I < N; ++I)
		{
			FVector2D P = Point();
			if (I % 3 == 0 && Wear)
			{
				const FAcRect& W = *Wear;
				const double U1 = Unit();
				const double U2 = Unit();
				const double Out = 2 + U1 * U2 * 46;
				const int32 Side = (int32)(Unit() * 4);
				const double U = Unit();
				switch (Side)
				{
				case 0: P = FVector2D(W.MinX() + U * W.W, W.MaxY() + Out); break;
				case 1: P = FVector2D(W.MinX() + U * W.W, W.MinY() - Out); break;
				case 2: P = FVector2D(W.MinX() - Out, W.MinY() + U * W.H); break;
				default: P = FVector2D(W.MaxX() + Out, W.MinY() + U * W.H); break;
				}
			}
			const double A = Unit() * 2 * Pi;
			double Length;
			if (Unit() < 0.88) Length = 2 + Unit() * 9;
			else Length = 14 + Unit() * 46;
			const FVector2D End(P.X + FMath::Cos(A) * Length, P.Y + FMath::Sin(A) * Length);
			const double Bend = (Unit() - 0.5) * Length * 0.3;
			const FVector2D Mid((P.X + End.X) / 2 - FMath::Sin(A) * Bend, (P.Y + End.Y) / 2 + FMath::Cos(A) * Bend);
			const int32 Steps = FMath::Clamp((int32)FMath::CeilToDouble(Length * G.Scale() / 3), 2, 16);
			Paths[(int32)(Unit() * 4) % 4].Add(Quad(P, Mid, End, Steps));
		}
		for (int32 K = 0; K < 4; ++K)
		{
			for (const TArray<FVector2D>& Path : Paths[K]) G.Stroke(Path, Clip, Tones[K].Color, Tones[K].Width, true);
		}
		// Chips of paint knocked off down to the primer.
		const FLinearColor Primer = Rgb(0.85, 0.52, 0.25, 0.4);
		for (int32 I = 0, N = (int32)(Area / 1400); I < N; ++I)
		{
			const FVector2D P = Point();
			const double U1 = Unit();
			const double U2 = Unit();
			const double D = 0.6 + U1 * U2 * 2.4;
			const double Wd = D * (0.6 + Unit());
			const FAcPoly Chip = G.Ellipse(FAcRect(P.X, P.Y, Wd, D));
			bool bInside = true;
			for (const FVector2D& Q : Chip) bInside = bInside && Clip.Contains(Q);
			if (bInside) G.Fill(Chip, Primer);
			else for (const FAcPoly& Part : AcArt::Clip(Clip, Chip).Pieces) G.Fill(Part, Primer, false);
		}
		// Hairline cracks.
		for (int32 I = 0, N = (int32)(Area / 9000); I < N; ++I)
		{
			FVector2D P = Point();
			double A = Unit() * 2 * Pi;
			TArray<FVector2D> Crack = {P};
			const int32 Steps = 3 + (int32)(Unit() * 8);
			for (int32 S = 0; S < Steps; ++S)
			{
				A += (Unit() - 0.5) * 1.4;
				const double Step = 3 + Unit() * 7;
				P = FVector2D(P.X + FMath::Cos(A) * Step, P.Y + FMath::Sin(A) * Step);
				Crack.Add(P);
			}
			G.Stroke(Crack, Clip, Gray(0, 0.45), 0.6, true);
		}
	}

	/// An engraved line: a dark cut with a lit lower lip.
	void Seam(FAcArtList& G, const TArray<FVector2D>& L, const bool bClosed, const FAcRegion* Clip = nullptr)
	{
		TArray<FVector2D> Lit = L;
		for (FVector2D& Q : Lit) Q += FVector2D(0.8, -1);
		if (Clip)
		{
			const TArray<FVector2D> A = bClosed && !L.IsEmpty() ? Closed(L) : L, B = bClosed && !Lit.IsEmpty() ? Closed(Lit) : Lit;
			G.Stroke(A, *Clip, Gray(0, 0.6), 1.4);
			G.Stroke(B, *Clip, Rgb(0.6, 0.72, 0.9, 0.16), 0.8);
			return;
		}
		G.Stroke(L, bClosed, Gray(0, 0.6), 1.4);
		G.Stroke(Lit, bClosed, Rgb(0.6, 0.72, 0.9, 0.16), 0.8);
	}

	void Bolt(FAcArtList& G, const FVector2D P, const double R = 2.8, const FGlow* Glow = nullptr)
	{
		Fill(G, G.Ellipse(FAcRect(P.X - R - 1, P.Y - R - 1.4, 2 * R + 2, 2 * R + 2)), Gray(0, 0.55), Glow);
		const FAcPoly Head = G.Ellipse(FAcRect(P.X - R, P.Y - R, 2 * R, 2 * R));
		G.Linear(FAcRegion(Head), FAcStops({Rgb(0.7, 0.76, 0.86), Rgb(0.12, 0.15, 0.22)}, {0, 1}), FVector2D(P.X - R, P.Y + R),
			FVector2D(P.X + R, P.Y - R));
	}

	void Hazard(FAcArtList& G, const FAcRect& Band)
	{
		const FAcPoly Box = RectPoly(Band);
		G.Fill(Box, Rgb(0.93, 0.7, 0.1));
		const double Step = 12;
		const bool bWide = Band.W >= Band.H;
		const double Rise = bWide ? Band.H : Band.W;
		const double X0 = Band.MinX(), X1 = Band.MaxX(), Y0 = Band.MinY(), Y1 = Band.MaxY();
		for (double T = (bWide ? X0 : Y0) - Rise - Step; T < (bWide ? X1 : Y1) + Step; T += Step)
		{
			const FAcPoly P = bWide
				? FAcPoly{FVector2D(T, Y0), FVector2D(T + Step / 2, Y0), FVector2D(T + Step / 2 + Rise, Y1), FVector2D(T + Rise, Y1)}
				: FAcPoly{FVector2D(X0, T), FVector2D(X0, T + Step / 2), FVector2D(X1, T + Step / 2 + Rise), FVector2D(X1, T + Rise)};
			const FAcPoly Stripe = AcArt::Clip(Clean(P), Box);
			if (Stripe.Num() >= 3) G.Fill(Stripe, Gray(0.05, 1));
		}
		G.Stroke(Box, true, Gray(0, 0.6), 1);
	}

	void Lamp(FAcArtList& G, const FAcRect& R, const FLinearColor& Color)
	{
		const double Rr = FMath::Min(R.W, R.H) / 2;
		const FAcPoly P = G.RoundRect(R, Rr);
		G.Shadow(P, FVector2D::ZeroVector, 6, Color);
		G.Fill(P, Color);
	}

	/// `Cab.dashTop`: the ledge from the faces' brow back to the glass.
	void DashTopArt(FAcArtList& G, const FAcCabLayout& L)
	{
		const TArray<FVector2D> Skyline(L.Skyline, 4), Brow(L.Brow, 4);
		// The dark under the ledge, and the shadow it throws up the view.
		G.EdgeShadow(Skyline, FVector2D(0, 5), 24, Gray(0, 0.75));
		FAcRegion Under;
		for (int32 K = 0; K < 3; ++K)
		{
			FAcPoly P = Clean({FVector2D(L.Skyline[K].X, 0), FVector2D(L.Skyline[K + 1].X, 0), L.Skyline[K + 1], L.Skyline[K]});
			if (P.Num() >= 3) Under.Pieces.Add(MoveTemp(P));
		}
		for (const FAcPoly& P : Under.Pieces) G.Fill(P, Gray(0.02, 1), false);
		for (int32 K = 0; K < 3; ++K)
		{
			const FAcPoly Plate = Clean({L.Brow[K], L.Brow[K + 1], L.Skyline[K + 1], L.Skyline[K]});
			if (Plate.Num() < 3) continue;
			const FAcRect Box = RectOf(BoxOf(Plate));
			const FAcRegion Clip(Plate);
			Armour(G, Clip, Box, 40 + K);
			G.Fill(Plate, Gray(1, K == 1 ? 0.12 : 0.07));
			G.Linear(Clip, FAcStops({Gray(0, 0), Gray(0, 0.35)}, {0, 1}), FVector2D(0, L.Brow[K].Y), FVector2D(0, L.Skyline[K].Y));
		}
		for (int32 K : {1, 2}) G.Stroke({L.Brow[K], L.Skyline[K]}, false, Gray(0, 0.5), 1.2);
		G.Stroke(Skyline, false, Gray(0, 0.85), 2);
		G.Stroke(Brow, false, Rgb(0.65, 0.75, 0.92, 0.55), 1.4);
	}

	void FaceShade(FAcArtList& G, const FAcRect& R)
	{
		G.Linear(FAcRegion(R), FAcStops({Gray(0, 0), Gray(0, 0.42)}, {0, 1}), FVector2D(0, R.MaxY() - 30), FVector2D(0, R.MinY()));
		Seam(G, {FVector2D(R.MinX(), 9), FVector2D(R.MaxX(), 9)}, false);
	}

	void FaceEdges(FAcArtList& G, const FAcRect& R, std::initializer_list<double> Creases)
	{
		const double Y1 = R.MaxY();
		G.Linear(FAcRegion(R), FAcStops({Gray(0, 0.45), Gray(0, 0)}, {0, 1}), FVector2D(0, Y1 - 2), FVector2D(0, Y1 - 12));
		G.Stroke({FVector2D(R.MinX(), Y1 - 1), FVector2D(R.MaxX(), Y1 - 1)}, false, Rgb(0.65, 0.75, 0.92, 0.5), 2);
		// Clipped to the face: a crease on its edge shows its inner half.
		for (const double X : Creases)
		{
			const bool bLeft = X <= R.MinX() + 1e-6, bRight = X >= R.MaxX() - 1e-6;
			const double At = bLeft ? X + 0.75 : bRight ? X - 0.75 : X;
			G.Stroke({FVector2D(At, R.MinY()), FVector2D(At, Y1)}, false, Gray(0, 0.75), bLeft || bRight ? 1.5 : 3);
		}
	}

	/// `Cab.housing`: a raised housing round a screen.
	void Housing(FAcArtList& G, const FAcRect& R, const double Cut, const int32 Seed, const double Rise = 0, const FGlow* Glow = nullptr)
	{
		FAcRect Outer = R.Inset(-11, -10);
		Outer.H += Rise;
		const FAcPoly P = CutPath(Outer, All(Cut));
		G.Shadow(P, FVector2D(0, -3), 9, Gray(0, 0.9));
		G.Fill(P, Gray(0.05, 1));
		const FAcRegion Clip(P);
		Armour(G, Clip, Outer, Seed);
		G.Linear(Clip, FAcStops({Gray(1, 0.1), Gray(1, 0), Gray(0, 0.2)}, {0, 0.4, 1}), FVector2D(0, Outer.MaxY()),
			FVector2D(0, Outer.MinY()));
		Rim(G, P, Glow);
		Stroke(G, CutPath(R.Inset(-3.5, -3.5), All(FMath::Max(Cut - 7, 3.0))), true, Gray(0, 0.85), 3, Glow);
		Bolt(G, FVector2D(Outer.MinX() + 7, Outer.MinY() + 7), 2.2, Glow);
		Bolt(G, FVector2D(Outer.MaxX() - 7, Outer.MinY() + 7), 2.2, Glow);
	}

	void CabScreen(FAcArtList& G, const FAcRect& R, const bool bGrid, const double Haze = 0)
	{
		ChromeScreen(G, G.RoundRect(R, 6), R, bGrid, 0.5, Haze);
	}

	/// `Cab.deckBay` (the station's name is Slate text, see AcCabArt.h).
	/// Returns the glow it leaves on: Swift sets the station name's cyan glow
	/// (`cg.setShadow`, blur 4, alpha 0.8) between NSGraphicsContext's
	/// save and restore, which save and restore the context that was current
	/// before, not this one: the shadow stays on and lights the glass's rim,
	/// the screen and the rest of the face drawn after it. Kept, so the deck
	/// and the map's housing are as bright as Swift's.
	FGlow DeckBay(FAcArtList& G, const FAcRect& R)
	{
		const FAcPoly Plate = CutPath(R, All(6));
		G.Fill(Plate, Gray(0, 0.28));
		Seam(G, Plate, true);
		Bolt(G, FVector2D(R.MinX() + 7, R.MaxY() - 7), 2);
		Bolt(G, FVector2D(R.MaxX() - 7, R.MaxY() - 7), 2);
		const FGlow Glow{WithAlpha(Cyan(), 0.8), 4};
		const FAcRect Glass = FAcCabLayout::DeckScreen(R);
		Stroke(G, CutPath(Glass.Inset(-3, -3), All(6)), true, Gray(0, 0.85), 3, &Glow);
		// Each scan line's cyan shadow, blurred together: a haze.
		CabScreen(G, Glass, false, Glow.Color.A * 0.2 / 3);
		return Glow;
	}

	void Instruments(FAcArtList& G, const FAcRect& R, const bool bFlip)
	{
		const FAcPoly Plate = CutPath(R, All(6));
		G.Fill(Plate, Gray(0, 0.28));
		Seam(G, Plate, true);
		Bolt(G, FVector2D(R.MinX() + 7, R.MaxY() - 7), 2);
		Bolt(G, FVector2D(R.MaxX() - 7, R.MaxY() - 7), 2);
		const FAcRect In = R.Inset(12, 14);
		const double Key = FMath::Min(14.0, (In.W - 8) / 3);
		for (int32 K = 0; K < 3 && Key > 0; ++K)
		{
			const FAcRect Q(In.MinX() + K * (Key + 4), In.MaxY() - Key - 6, Key, Key);
			G.Fill(G.RoundRect(Q, 2), Rgb(0.02, 0.04, 0.07));
			const bool bOn = ((K == 0) != bFlip) || K == 2;
			if (bOn) Lamp(G, FAcRect(Q.MinX() + 3, Q.MaxY() - 5, Q.W - 6, 2.5), K == 2 ? Cyan() : Rgb(0.4, 1, 0.5));
		}
		const FAcRect Vent(In.MinX(), In.MinY() + In.H * 0.32, In.W, In.H * 0.3);
		for (double Y = Vent.MinY(); Y < Vent.MaxY() - 2; Y += 6)
		{
			const FAcRect Slot(Vent.MinX(), Y, Vent.W, 3);
			G.Fill(G.RoundRect(Slot, 1.5), Gray(0, 0.7));
			G.Stroke({FVector2D(Slot.MinX() + 1.5, Slot.MinY() - 0.5), FVector2D(Slot.MaxX() - 1.5, Slot.MinY() - 0.5)}, false,
				Gray(1, 0.1), 0.8);
		}
		const double D = FMath::Min(18.0, In.W / 2 - 6);
		for (int32 K = 0; K < 2 && D > 0; ++K)
		{
			const FVector2D O(In.MinX() + D / 2 + 2 + K * (D + 8), In.MinY() + D / 2);
			const FAcRect Dial(O.X - D / 2, O.Y - D / 2, D, D);
			G.Fill(G.Ellipse(Dial.Inset(-1.5, -1.5)), Gray(0, 0.6));
			G.Fill(G.Ellipse(Dial), Rgb(0.02, 0.06, 0.08));
			const double A = (bFlip ? 0.4 : 1.1) + K * 0.9;
			const TArray<FVector2D> Needle = {O, FVector2D(O.X + FMath::Cos(A) * D * 0.4, O.Y + FMath::Sin(A) * D * 0.4)};
			G.LineShadow(Needle, 1.2, 3, Cyan());
			G.Stroke(Needle, false, Cyan(), 1.2);
		}
	}

	void SideFace(FAcArtList& G, const FAcRect& R, const FAcRect& Screen, const double InnerX, const int32 Seed, const FAcRect& Block,
		const bool bDeck)
	{
		Armour(G, FAcRegion(R), R, Seed);
		const double OuterX = InnerX == R.MaxX() ? R.MinX() : R.MaxX();
		G.Linear(FAcRegion(R), FAcStops({Gray(1, 0.04), Gray(0, 0.25)}, {0, 1}), FVector2D(OuterX, 0), FVector2D(InnerX, 0));
		FaceShade(G, R);
		FaceEdges(G, R, {InnerX});
		TOptional<FGlow> Glow;
		if (bDeck && Block.W > 60) Glow = DeckBay(G, Block);
		else if (Block.W > 30) Instruments(G, Block, InnerX == R.MinX());
		const FGlow* Lit = Glow.GetPtrOrNull();
		Housing(G, Screen, 12, (int32)Screen.MinX(), 0, Lit);
		CabScreen(G, Screen, false, Lit ? Lit->Color.A * 0.2 / 3 : 0);
	}

	void CenterFace(FAcArtList& G, const FAcRect& R, const FAcRect& Cc)
	{
		Armour(G, FAcRegion(R), R, 9);
		G.Linear(FAcRegion(R), FAcStops({Gray(1, 0.1), Gray(1, 0)}, {0, 1}), FVector2D(0, R.MaxY()), FVector2D(0, R.MidY()));
		FaceShade(G, R);
		FaceEdges(G, R, {R.MinX(), R.MaxX()});
		Housing(G, Cc, 16, 9);
		CabScreen(G, Cc, true);
		for (const double X : {Cc.MinX() - 9, Cc.MaxX() + 3}) Hazard(G, FAcRect(X, Cc.MinY() + 18, 6, Cc.H - 36));
		Lamp(G, FAcRect(Cc.MinX() - 15 - 2.5, Cc.MaxY() - 34, 5, 14), Rgb(1, 0.62, 0.15));
		Lamp(G, FAcRect(Cc.MaxX() + 15 - 2.5, Cc.MaxY() - 34, 5, 14), Rgb(1, 0.2, 0.15));
	}

	// MARK: - The cab's frame (chunk D3: `Cab.draw(frame: true)`, `frameSeams`, `lip`, `socket`)

	void FrameSeams(FAcArtList& G, const FAcCabLayout& L)
	{
		const FAcRect& W = L.Window;
		const FVector2D S = L.Size;
		for (const double F : {0.27, 0.73})
		{
			const double X = FMath::RoundHalfFromZero(W.MinX() + W.W * F);
			Seam(G, {FVector2D(X, W.MaxY() + 4), FVector2D(X, S.Y)}, false);
		}
		for (const double X : {W.MinX(), W.MaxX()})
		{
			// Where the pillar meets the rail, and its plate's joint part way down.
			const double Inner = X == W.MinX() ? X - 6 : X + 6, Outer = X == W.MinX() ? 0 : S.X;
			for (const double Y : {W.MaxY() - 70, W.MinY() + W.H * 0.42}) Seam(G, {FVector2D(Outer, Y), FVector2D(Inner, Y)}, false);
		}
		// An inset strip down the middle of each pillar.
		if (W.MinX() > 26)
		{
			for (const double Cx : {W.MinX() / 2, (W.MaxX() + S.X) / 2})
			{
				const FAcRect Strip(Cx - 4, L.Top() + 70, 8, W.MaxY() - 110 - L.Top() - 70);
				if (Strip.H <= 0) continue;
				Seam(G, G.RoundRect(Strip, 4), true);
				Bolt(G, FVector2D(Cx, Strip.MinY() - 14));
				Bolt(G, FVector2D(Cx, Strip.MaxY() + 14));
			}
		}
		// Bolts along the rail, clear of the lamps.
		for (double X = W.MinX() + 110; X < W.MaxX() - 100; X += 150)
		{
			Bolt(G, FVector2D(X, FMath::RoundHalfFromZero((W.MaxY() + S.Y) / 2)), 2.4);
		}
	}

	void Lip(FAcArtList& G, const FAcCabLayout& L, const FAcPoly& Hole)
	{
		const FAcRect& W = L.Window;
		const double Band = 10, C = FAcCabLayout::Corner;
		const FAcPoly Outer = G.RoundRect(W.Inset(-Band, -Band), C + Band);
		// A step down to the lip round the top and the sides.
		const double StepBand = 28;
		const FAcPoly Step = G.RoundRect(W.Inset(-StepBand, -StepBand), C + StepBand);
		const FAcPoly Upper = RectPoly(FAcRect(0, W.MinY(), L.Size.X, L.Size.Y - W.MinY()));
		G.Linear(AcArt::Clip(Minus(Step, Outer), Upper), FAcStops({Gray(0, 0.05), Gray(0, 0.28)}, {0, 1}),
			FVector2D(0, W.MaxY() + StepBand), FVector2D(0, W.MinY()));
		const FAcRegion Above(Upper);
		Seam(G, Step, true, &Above);
		G.Linear(Minus(Outer, Hole), FAcStops({Gray(1, 0.13), Gray(1, 0.03), Gray(0, 0.25)}, {0, 0.5, 1}),
			FVector2D(0, W.MaxY() + Band), FVector2D(0, W.MinY() - Band));
		// The groove outside, a lit edge on the lip, a dark edge at the glass.
		G.Stroke(Outer, true, Gray(0, 0.75), 2.2);
		G.Stroke(G.RoundRect(W.Inset(-Band + 1.6, -Band + 1.6), C + Band - 1.6), true, Rgb(0.65, 0.75, 0.9, 0.3), 1);
		// The shadow onto the glass, deepest under the rail.
		G.InnerShadow(Hole, FVector2D(0, -3), 14, Gray(0, 0.85));
		G.Stroke(Hole, true, Gray(0, 0.8), 1.6);
		GlowLine(G, Hole, Rgb(0.25, 0.8, 1), 1.1, 5, 0.55);
	}

	void Socket(FAcArtList& G, const FVector2D P)
	{
		const FAcRect R(P.X - 5.5, P.Y - 13, 11, 26);
		G.Fill(G.RoundRect(R.Offset(0, -1), 5.5), Gray(0, 0.7));
		G.Stroke(G.RoundRect(R, 5.5), true, Rgb(0.6, 0.7, 0.85, 0.5), 1.2);
	}

	/// The dashboard's top band: from the bottom to its top plus the corner
	/// and the shadow it throws (Cab.node).
	double DashBottom(const FAcCabLayout& L)
	{
		const double M = FMath::RoundHalfFromZero(FAcCabLayout::Corner + 22);
		return FMath::RoundHalfFromZero(L.Top() + M);
	}
}

void AcCabArt::BakeFace(FAcBakedArt& Art, const int32 K, const FAcCabLayout& L)
{
	const FAcRect& R = L.Faces[FMath::Clamp(K, 0, 2)].Flat;
	FAcArtList G(2);
	switch (K)
	{
	case 0: SideFace(G, R, L.MapWell, R.MaxX(), 31, L.Blocks[0], true); break;
	case 1: CenterFace(G, R, L.CenterWell); break;
	default: SideFace(G, R, L.CardWell, R.MinX(), 32, L.Blocks[1], false); break;
	}
	Art.Bake(G, R);
}

void AcCabArt::BakeDashTop(FAcBakedArt& Art, const FAcCabLayout& L)
{
	FAcArtList G(2);
	DashTopArt(G, L);
	Art.Bake(G, FAcRect(0, 0, L.Size.X, DashBottom(L)));
}

void AcCabArt::BakeViewFrame(FAcBakedArt& Art, const FVector2D Size, const double RailW)
{
	FAcArtList G(1.5);
	const FAcRect Full(0, 0, Size.X, Size.Y);
	const FAcRect Opening(RailW, -40, Size.X - 2 * RailW, Size.Y - RailW + 40);
	const FAcPoly Hole = G.RoundRect(Opening, FAcCabLayout::Corner);
	Armour(G, Minus(RectPoly(Full), Hole), Full, 7, &Opening);
	G.InnerShadow(Hole, FVector2D(0, -2), 10, Gray(0, 0.85));
	G.Stroke(Hole, true, Gray(0, 0.8), 1.6);
	GlowLine(G, Hole, Rgb(0.25, 0.8, 1), 1.1, 5, 0.55);
	G.Stroke(RectPoly(Full.Inset(0.5, 0.5)), true, Rgb(0.65, 0.75, 0.9, 0.35), 1);
	for (double Y = Size.Y * 0.3; Y < Size.Y * 0.75; Y += 90)
	{
		Bolt(G, FVector2D(RailW / 2, Y), 2);
		Bolt(G, FVector2D(Size.X - RailW / 2, Y), 2);
	}
	Art.Bake(G, Full);
}

void AcCabArt::BakeButtonFace(FAcBakedArt& Art, const double Side, const bool bEnabled)
{
	FAcArtList G(2);
	const FAcRect R = FAcRect(0, 0, Side, Side).Inset(1, 1);
	const FAcPoly P = G.RoundRect(R, 6);
	const FLinearColor Top = bEnabled ? Rgb(0.1, 0.22, 0.38) : Rgb(0.07, 0.08, 0.1);
	const FLinearColor Bottom = bEnabled ? Rgb(0.02, 0.06, 0.13) : Rgb(0.03, 0.03, 0.04);
	G.Linear(FAcRegion(P), FAcStops({Top, Bottom}, {0, 1}), FVector2D(0, R.MaxY()), FVector2D(0, R.MinY()));
	if (bEnabled)
	{
		G.Radial(FAcRegion(P), FAcStops({WithAlpha(Cyan(), 0.25), WithAlpha(Cyan(), 0)}, {0, 1}), FVector2D(R.MidX(), R.MidY()), Side * 0.6);
	}
	G.Fill(G.RoundRect(FAcRect(R.MinX() + 3, R.MidY() + 2, R.W - 6, R.H / 2 - 5), 4), Gray(1, bEnabled ? 0.07 : 0.03));
	Art.Bake(G, FAcRect(0, 0, Side, Side));
}

void AcCabArt::BakeScanlines(FAcBakedArt& Art, const FVector2D Size)
{
	FAcArtList G(1);
	for (double Y = 0; Y < Size.Y; Y += 3) G.Fill(RectPoly(FAcRect(0, Y, Size.X, 1)), Rgb(0, 0.02, 0.05, 0.45), false);
	Art.Bake(G, FAcRect(0, 0, Size.X, Size.Y));
}

void AcCabArt::BakePlate(FAcBakedArt& Art, const FVector2D Size, const FAcCuts Cuts, const bool bLeds, const int32 Seed)
{
	FAcArtList G(2);
	const FAcRect R(0, 0, Size.X, Size.Y);
	const FAcPoly Outer = CutPath(R, Cuts, 1);
	G.Shadow(Outer, FVector2D(0, -2), 6, Gray(0, 0.8));
	G.Fill(Outer, Gray(0.05, 1));
	Steel(G, Outer, R, Seed);
	Rim(G, Outer);
	GlowLine(G, CutPath(R, Cuts, 4.5), Cyan(), 1.2, 6, 0.85);
	if (bLeds && Size.X > 120) Leds(G, FVector2D(R.MidX() - 30, 1.2), 5, Cyan(), FVector2D(8, 2), 4);
	Art.Bake(G, R.Inset(-PlatePad, -PlatePad));
}

void AcCabArt::BakeFramed(TArray<TPair<FAcRect, FAcBakedArt>>& Out, const FAcCabLayout& L)
{
	Out.Reset();
	FAcArtList G(2);
	const FVector2D S = L.Size;
	const FAcRect Full(0, 0, S.X, S.Y);
	const FAcPoly Hole = G.RoundRect(L.Window, FAcCabLayout::Corner);
	Armour(G, Minus(RectPoly(Full), Hole), Full, 5, &L.Window);
	FrameSeams(G, L);
	Lip(G, L, Hole);
	DashTopArt(G, L);
	for (int32 K = 0; K < 2; ++K) Socket(G, L.Lamp(K));

	// Cab.node: each band reaches past the window's edge by the rounded
	// corner and the shadow the lip (or the dashboard) casts onto the glass.
	const FAcRect& W = L.Window;
	const double M = FMath::RoundHalfFromZero(FAcCabLayout::Corner + 22);
	const double Bottom = DashBottom(L);
	const FAcRect Bands[4] = {FAcRect(0, 0, S.X, Bottom), FAcRect(0, W.MaxY() - M, S.X, S.Y - W.MaxY() + M),
		FAcRect(0, Bottom, W.MinX() + M, W.MaxY() - M - Bottom), FAcRect(W.MaxX() - M, Bottom, S.X - W.MaxX() + M, W.MaxY() - M - Bottom)};
	for (const FAcRect& B : Bands)
	{
		if (B.W <= 0 || B.H <= 0) continue;
		TPair<FAcRect, FAcBakedArt>& Band = Out.AddDefaulted_GetRef();
		Band.Key = B;
		Band.Value.Bake(G, B);
	}
}
