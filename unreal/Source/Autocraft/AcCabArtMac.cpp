// AcCabArt on the Mac: the Swift `Cab` (ConsoleCab.swift) and `Chrome`
// (HUDChrome.swift) drawing, call for call in Core Graphics' C API. Keep the
// order of the random draws (`SplitMix`) as Swift evaluates them: C++ does
// not sequence function arguments, so each draw is its own statement.
#include "AcCabArt.h"

#if PLATFORM_MAC

THIRD_PARTY_INCLUDES_START
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
THIRD_PARTY_INCLUDES_END

#include <initializer_list>

namespace
{
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

	CGRect CgRect(const FAcRect& R) { return CGRectMake(R.X, R.Y, R.W, R.H); }
	CGRect Inset(CGRect R, double Dx, double Dy) { return CGRectInset(R, Dx, Dy); }
	CGPoint Pt(double X, double Y) { return CGPointMake(X, Y); }
	CGPoint Pt(FVector2D P) { return CGPointMake(P.X, P.Y); }

	/// One drawing: the context and everything made for it (released at the end).
	class FDraw
	{
	public:
		CGContextRef C = nullptr;

		/// `Chrome.context(size, scale:)`: device RGB, premultiplied, y up in points.
		FDraw(double W, double H, double Scale)
		{
			Space = CGColorSpaceCreateDeviceRGB();
			Px = FMath::Max(1, (int32)FMath::CeilToDouble(W * Scale));
			Py = FMath::Max(1, (int32)FMath::CeilToDouble(H * Scale));
			C = CGBitmapContextCreate(nullptr, Px, Py, 8, 0, Space, kCGImageAlphaPremultipliedLast);
			CGContextScaleCTM(C, Scale, Scale);
			Points = FVector2D(W, H);
		}
		~FDraw()
		{
			for (CFTypeRef O : Owned) CFRelease(O);
			if (C) CGContextRelease(C);
			if (Space) CGColorSpaceRelease(Space);
		}

		FAcArtImage Image() const
		{
			FAcArtImage Out;
			Out.Width = Px;
			Out.Height = Py;
			Out.Points = Points;
			Out.Pixels.SetNumUninitialized(Px * Py);
			const uint8* Data = (const uint8*)CGBitmapContextGetData(C);
			const size_t Row = CGBitmapContextGetBytesPerRow(C);
			for (int32 Y = 0; Y < Py; ++Y)
			{
				const uint8* S = Data + Row * Y;
				FColor* D = Out.Pixels.GetData() + Px * Y;
				for (int32 X = 0; X < Px; ++X, S += 4)
				{
					const uint8 A = S[3];
					auto Un = [A](uint8 V) -> uint8 { return A == 0 ? 0 : (uint8)FMath::Min(255, (V * 255 + A / 2) / A); };
					D[X] = FColor(Un(S[0]), Un(S[1]), Un(S[2]), A);
				}
			}
			return Out;
		}

		// Colours as the Swift code makes them: NSColor(calibratedRed:) is
		// generic RGB, NSColor(white:) generic grey; CG converts them.
		CGColorRef Rgb(double R, double G, double B, double A = 1)
		{
			return Own(CGColorCreateGenericRGB(R, G, B, A));
		}
		CGColorRef Gray(double W, double A = 1) { return Own(CGColorCreateGenericGray(W, A)); }
		CGColorRef Alpha(CGColorRef Color, double A) { return Own(CGColorCreateCopyWithAlpha(Color, A)); }
		CGColorRef Cyan() { return Rgb(0.35, 0.85, 1); }
		CGColorRef Ice() { return Rgb(0.8, 0.95, 1); }
		CGColorRef Glass() { return Rgb(0.01, 0.035, 0.07); }

		/// `Chrome.gradient` (device RGB).
		CGGradientRef Gradient(std::initializer_list<CGColorRef> Colors, std::initializer_list<CGFloat> At)
		{
			TArray<const void*> Refs;
			for (CGColorRef Cc : Colors) Refs.Add(Cc);
			CFArrayRef Array = CFArrayCreate(nullptr, Refs.GetData(), Refs.Num(), &kCFTypeArrayCallBacks);
			TArray<CGFloat> Locs(At.begin(), (int32)At.size());
			CGGradientRef G = CGGradientCreateWithColors(Space, Array, Locs.GetData());
			CFRelease(Array);
			return (CGGradientRef)Own(G);
		}

		CGMutablePathRef Path() { return (CGMutablePathRef)Own(CGPathCreateMutable()); }
		CGPathRef RoundedRect(CGRect R, double Rw, double Rh)
		{
			return (CGPathRef)Own(CGPathCreateWithRoundedRect(R, Rw, Rh, nullptr));
		}

		void Linear(CGGradientRef G, CGPoint A, CGPoint B) { CGContextDrawLinearGradient(C, G, A, B, 0); }
		void Radial(CGGradientRef G, CGPoint P, double R)
		{
			CGContextDrawRadialGradient(C, G, P, 0, P, R, 0);
		}
		void Shadow(double Dx, double Dy, double Blur, CGColorRef Color)
		{
			CGContextSetShadowWithColor(C, CGSizeMake(Dx, Dy), Blur, Color);
		}
		void Stroke(CGPathRef P, CGColorRef Color, double Width)
		{
			CGContextAddPath(C, P);
			CGContextSetStrokeColorWithColor(C, Color);
			CGContextSetLineWidth(C, Width);
			CGContextStrokePath(C);
		}
		void Fill(CGPathRef P, CGColorRef Color)
		{
			CGContextAddPath(C, P);
			CGContextSetFillColorWithColor(C, Color);
			CGContextFillPath(C);
		}
		void FillRect(CGRect R, CGColorRef Color)
		{
			CGContextSetFillColorWithColor(C, Color);
			CGContextFillRect(C, R);
		}
		void Save() { CGContextSaveGState(C); }
		void Restore() { CGContextRestoreGState(C); }

	private:
		template <typename T>
		T Own(T Ref)
		{
			if (Ref) Owned.Add((CFTypeRef)Ref);
			return Ref;
		}

		CGColorSpaceRef Space = nullptr;
		TArray<CFTypeRef> Owned;
		int32 Px = 0, Py = 0;
		FVector2D Points;
	};

	// MARK: - Chrome (HUDChrome.swift)

	/// `Chrome.path`: corners cut at 45°, inset by `D`.
	CGPathRef CutPath(FDraw& G, CGRect R0, FAcCuts Cuts, double D = 0)
	{
		const CGRect R = CGRectInset(R0, D, D);
		const double K = D * 0.41;
		auto Cut = [K](double V) { return V > 0 ? FMath::Max(V - K, 0.0) : 0.0; };
		const double Tl = Cut(Cuts.TL), Tr = Cut(Cuts.TR), Br = Cut(Cuts.BR), Bl = Cut(Cuts.BL);
		const double X0 = CGRectGetMinX(R), X1 = CGRectGetMaxX(R), Y0 = CGRectGetMinY(R), Y1 = CGRectGetMaxY(R);
		CGMutablePathRef P = G.Path();
		CGPathMoveToPoint(P, nullptr, X0 + Bl, Y0);
		CGPathAddLineToPoint(P, nullptr, X1 - Br, Y0);
		CGPathAddLineToPoint(P, nullptr, X1, Y0 + Br);
		CGPathAddLineToPoint(P, nullptr, X1, Y1 - Tr);
		CGPathAddLineToPoint(P, nullptr, X1 - Tr, Y1);
		CGPathAddLineToPoint(P, nullptr, X0 + Tl, Y1);
		CGPathAddLineToPoint(P, nullptr, X0, Y1 - Tl);
		CGPathAddLineToPoint(P, nullptr, X0, Y0 + Bl);
		CGPathCloseSubpath(P);
		return P;
	}
	FAcCuts All(double V) { return FAcCuts{V, V, V, V}; }

	void GlowLine(FDraw& G, CGPathRef P, CGColorRef Color, double Width = 1.3, double Blur = 6, double Alpha = 0.95)
	{
		G.Save();
		G.Shadow(0, 0, Blur, G.Alpha(Color, Alpha));
		CGContextAddPath(G.C, P);
		CGContextSetStrokeColorWithColor(G.C, G.Alpha(Color, Alpha));
		CGContextSetLineWidth(G.C, Width);
		CGContextSetLineJoin(G.C, kCGLineJoinMiter);
		CGContextStrokePath(G.C);
		CGContextAddPath(G.C, P);
		CGContextSetStrokeColorWithColor(G.C, G.Alpha(G.Ice(), Alpha * 0.6));
		CGContextSetLineWidth(G.C, Width * 0.4);
		CGContextStrokePath(G.C);
		G.Restore();
	}

	void Rim(FDraw& G, CGPathRef P)
	{
		G.Stroke(P, G.Rgb(0.02, 0.03, 0.05, 1), 3);
		G.Stroke(P, G.Rgb(0.5, 0.6, 0.72, 0.8), 1);
	}

	void Steel(FDraw& G, CGPathRef P, CGRect R, int32 Seed)
	{
		G.Save();
		CGContextAddPath(G.C, P);
		CGContextClip(G.C);
		G.Linear(G.Gradient({G.Rgb(0.14, 0.2, 0.3), G.Rgb(0.06, 0.09, 0.15), G.Rgb(0.03, 0.04, 0.08)}, {0, 0.45, 1}),
			Pt(0, CGRectGetMaxY(R)), Pt(0, CGRectGetMinY(R)));
		FSplitMix M((uint64)Seed);
		const int32 N = (int32)(R.size.width * R.size.height / 900) + 4;
		for (int32 I = 0; I < N; ++I)
		{
			const double Y = CGRectGetMinY(R) + M.Unit() * R.size.height;
			const double X = CGRectGetMinX(R) + M.Unit() * R.size.width;
			const double White = M.Unit() < 0.5 ? 1 : 0;
			CGContextSetStrokeColorWithColor(G.C, G.Gray(White, 0.04));
			CGContextSetLineWidth(G.C, 0.7);
			CGContextMoveToPoint(G.C, X, Y);
			const double Len = 20 + M.Unit() * 70;
			CGContextAddLineToPoint(G.C, X + Len, Y);
			CGContextStrokePath(G.C);
		}
		G.Restore();
	}

	void Leds(FDraw& G, CGPoint A, int32 Count, CGColorRef Color, CGSize Size, double Gap)
	{
		G.Save();
		G.Shadow(0, 0, 5, Color);
		CGContextSetFillColorWithColor(G.C, G.Alpha(Color, 0.9));
		for (int32 K = 0; K < Count; ++K)
		{
			CGContextFillRect(G.C, CGRectMake(A.x + K * (Size.width + Gap), A.y, Size.width, Size.height));
		}
		G.Restore();
	}

	/// `Chrome.screen`: a sunk screen of dark glass.
	void ChromeScreen(FDraw& G, CGPathRef P, CGRect R, bool bGrid, double Edge)
	{
		G.Save();
		G.Shadow(0, -1, 4, G.Gray(0, 1));
		G.Fill(P, G.Glass());
		G.Restore();
		G.Save();
		CGContextAddPath(G.C, P);
		CGContextClip(G.C);
		G.Radial(G.Gradient({G.Alpha(G.Cyan(), 0.16), G.Alpha(G.Cyan(), 0)}, {0, 1}), Pt(CGRectGetMidX(R), CGRectGetMinY(R)),
			FMath::Max(R.size.width, R.size.height) * 0.8);
		const double X0 = CGRectGetMinX(R), X1 = CGRectGetMaxX(R), Y0 = CGRectGetMinY(R), Y1 = CGRectGetMaxY(R);
		if (bGrid)
		{
			CGContextSetStrokeColorWithColor(G.C, G.Alpha(G.Cyan(), 0.05));
			CGContextSetLineWidth(G.C, 0.6);
			for (double X = X0 + 14; X < X1; X += 14)
			{
				CGContextMoveToPoint(G.C, X, Y0);
				CGContextAddLineToPoint(G.C, X, Y1);
			}
			for (double Y = Y0 + 14; Y < Y1; Y += 14)
			{
				CGContextMoveToPoint(G.C, X0, Y);
				CGContextAddLineToPoint(G.C, X1, Y);
			}
			CGContextStrokePath(G.C);
		}
		CGContextSetStrokeColorWithColor(G.C, G.Gray(0, 0.2));
		CGContextSetLineWidth(G.C, 1);
		for (double Y = Y0 + 1.5; Y < Y1; Y += 3)
		{
			CGContextMoveToPoint(G.C, X0, Y);
			CGContextAddLineToPoint(G.C, X1, Y);
		}
		CGContextStrokePath(G.C);
		G.Restore();
		GlowLine(G, P, G.Cyan(), 1, 4, Edge);
	}

	// MARK: - Cab material (ConsoleCab.swift)

	/// `Cab.armour`: scuffed navy armour over `R`, worst round `Wear`.
	void Armour(FDraw& G, CGRect R, int32 Seed, const CGRect* Wear = nullptr)
	{
		G.Linear(G.Gradient({G.Rgb(0.11, 0.16, 0.32), G.Rgb(0.07, 0.1, 0.22), G.Rgb(0.035, 0.05, 0.11)}, {0, 0.6, 1}),
			Pt(0, CGRectGetMaxY(R)), Pt(0, CGRectGetMinY(R)));
		FSplitMix M((uint64)Seed);
		auto Unit = [&M] { return M.Unit(); };
		auto Point = [&]
		{
			while (true)
			{
				const double X = CGRectGetMinX(R) + Unit() * R.size.width;
				const double Y = CGRectGetMinY(R) + Unit() * R.size.height;
				const CGPoint P = Pt(X, Y);
				if (!Wear || !CGRectContainsPoint(*Wear, P)) return P;
			}
		};
		double HoleArea = 0;
		if (Wear)
		{
			const CGRect H = CGRectIntersection(*Wear, R);
			if (!CGRectIsNull(H)) HoleArea = H.size.width * H.size.height;
		}
		const double Area = FMath::Max(0.0, R.size.width * R.size.height - HoleArea);
		// Grime and paler patches.
		for (int32 I = 0, N = (int32)(Area / 7000) + 2; I < N; ++I)
		{
			const CGPoint Cc = Point();
			const double Radius = 16 + Unit() * 90;
			CGColorRef Tone, Clear;
			if (Unit() < 0.75)
			{
				const double A = 0.3 * Unit();
				Tone = G.Gray(0, A);
				Clear = G.Gray(0, 0);
			}
			else
			{
				const double A = 0.07 * Unit();
				Tone = G.Rgb(0.45, 0.6, 0.85, A);
				Clear = G.Rgb(0.45, 0.6, 0.85, 0);
			}
			G.Radial(G.Gradient({Tone, Clear}, {0, 1}), Cc, Radius);
		}
		// Scratches.
		struct FTone { CGColorRef Color; double Width; };
		const FTone Tones[4] = {{G.Rgb(0.75, 0.82, 0.95, 0.26), 0.6}, {G.Rgb(0.92, 0.58, 0.28, 0.42), 0.7},
			{G.Rgb(0.85, 0.55, 0.3, 0.2), 1.2}, {G.Gray(0, 0.5), 0.9}};
		CGMutablePathRef Paths[4] = {G.Path(), G.Path(), G.Path(), G.Path()};
		for (int32 I = 0, N = (int32)(Area / 170); I < N; ++I)
		{
			CGPoint P = Point();
			if (I % 3 == 0 && Wear)
			{
				const CGRect& W = *Wear;
				const double U1 = Unit();
				const double U2 = Unit();
				const double Out = 2 + U1 * U2 * 46;
				const int32 Side = (int32)(Unit() * 4);
				const double U = Unit();
				switch (Side)
				{
				case 0: P = Pt(CGRectGetMinX(W) + U * W.size.width, CGRectGetMaxY(W) + Out); break;
				case 1: P = Pt(CGRectGetMinX(W) + U * W.size.width, CGRectGetMinY(W) - Out); break;
				case 2: P = Pt(CGRectGetMinX(W) - Out, CGRectGetMinY(W) + U * W.size.height); break;
				default: P = Pt(CGRectGetMaxX(W) + Out, CGRectGetMinY(W) + U * W.size.height); break;
				}
			}
			const double A = Unit() * 2 * Pi;
			double Length;
			if (Unit() < 0.88) Length = 2 + Unit() * 9;
			else Length = 14 + Unit() * 46;
			const CGPoint End = Pt(P.x + FMath::Cos(A) * Length, P.y + FMath::Sin(A) * Length);
			const double Bend = (Unit() - 0.5) * Length * 0.3;
			const CGPoint Mid = Pt((P.x + End.x) / 2 - FMath::Sin(A) * Bend, (P.y + End.y) / 2 + FMath::Cos(A) * Bend);
			CGMutablePathRef Path = Paths[(int32)(Unit() * 4) % 4];
			CGPathMoveToPoint(Path, nullptr, P.x, P.y);
			CGPathAddQuadCurveToPoint(Path, nullptr, Mid.x, Mid.y, End.x, End.y);
		}
		CGContextSetLineCap(G.C, kCGLineCapRound);
		for (int32 K = 0; K < 4; ++K) G.Stroke(Paths[K], Tones[K].Color, Tones[K].Width);
		// Chips of paint knocked off down to the primer.
		CGMutablePathRef Chips = G.Path();
		for (int32 I = 0, N = (int32)(Area / 1400); I < N; ++I)
		{
			const CGPoint P = Point();
			const double U1 = Unit();
			const double U2 = Unit();
			const double D = 0.6 + U1 * U2 * 2.4;
			const double Wd = D * (0.6 + Unit());
			CGPathAddEllipseInRect(Chips, nullptr, CGRectMake(P.x, P.y, Wd, D));
		}
		G.Fill(Chips, G.Rgb(0.85, 0.52, 0.25, 0.4));
		// Hairline cracks.
		CGMutablePathRef Cracks = G.Path();
		for (int32 I = 0, N = (int32)(Area / 9000); I < N; ++I)
		{
			CGPoint P = Point();
			double A = Unit() * 2 * Pi;
			CGPathMoveToPoint(Cracks, nullptr, P.x, P.y);
			const int32 Steps = 3 + (int32)(Unit() * 8);
			for (int32 S = 0; S < Steps; ++S)
			{
				A += (Unit() - 0.5) * 1.4;
				const double Step = 3 + Unit() * 7;
				P = Pt(P.x + FMath::Cos(A) * Step, P.y + FMath::Sin(A) * Step);
				CGPathAddLineToPoint(Cracks, nullptr, P.x, P.y);
			}
		}
		G.Stroke(Cracks, G.Gray(0, 0.45), 0.6);
	}

	/// An engraved line: a dark cut with a lit lower lip.
	void Seam(FDraw& G, CGPathRef P)
	{
		G.Stroke(P, G.Gray(0, 0.6), 1.4);
		G.Save();
		CGContextTranslateCTM(G.C, 0.8, -1);
		G.Stroke(P, G.Rgb(0.6, 0.72, 0.9, 0.16), 0.8);
		G.Restore();
	}

	void Bolt(FDraw& G, CGPoint P, double R = 2.8)
	{
		CGContextAddEllipseInRect(G.C, CGRectMake(P.x - R - 1, P.y - R - 1.4, 2 * R + 2, 2 * R + 2));
		CGContextSetFillColorWithColor(G.C, G.Gray(0, 0.55));
		CGContextFillPath(G.C);
		G.Save();
		CGContextAddEllipseInRect(G.C, CGRectMake(P.x - R, P.y - R, 2 * R, 2 * R));
		CGContextClip(G.C);
		G.Linear(G.Gradient({G.Rgb(0.7, 0.76, 0.86), G.Rgb(0.12, 0.15, 0.22)}, {0, 1}), Pt(P.x - R, P.y + R), Pt(P.x + R, P.y - R));
		G.Restore();
	}

	void Hazard(FDraw& G, CGRect Band)
	{
		G.Save();
		CGContextClipToRect(G.C, Band);
		G.FillRect(Band, G.Rgb(0.93, 0.7, 0.1));
		CGContextSetFillColorWithColor(G.C, G.Gray(0.05, 1));
		const double Step = 12;
		const bool bWide = Band.size.width >= Band.size.height;
		const double Rise = bWide ? Band.size.height : Band.size.width;
		const double X0 = CGRectGetMinX(Band), X1 = CGRectGetMaxX(Band), Y0 = CGRectGetMinY(Band), Y1 = CGRectGetMaxY(Band);
		for (double T = (bWide ? X0 : Y0) - Rise - Step; T < (bWide ? X1 : Y1) + Step; T += Step)
		{
			CGMutablePathRef P = G.Path();
			if (bWide)
			{
				CGPathMoveToPoint(P, nullptr, T, Y0);
				CGPathAddLineToPoint(P, nullptr, T + Step / 2, Y0);
				CGPathAddLineToPoint(P, nullptr, T + Step / 2 + Rise, Y1);
				CGPathAddLineToPoint(P, nullptr, T + Rise, Y1);
			}
			else
			{
				CGPathMoveToPoint(P, nullptr, X0, T);
				CGPathAddLineToPoint(P, nullptr, X0, T + Step / 2);
				CGPathAddLineToPoint(P, nullptr, X1, T + Step / 2 + Rise);
				CGPathAddLineToPoint(P, nullptr, X1, T + Rise);
			}
			CGPathCloseSubpath(P);
			CGContextAddPath(G.C, P);
			CGContextFillPath(G.C);
		}
		G.Restore();
		CGContextSetStrokeColorWithColor(G.C, G.Gray(0, 0.6));
		CGContextSetLineWidth(G.C, 1);
		CGContextStrokeRect(G.C, Band);
	}

	void Lamp(FDraw& G, CGRect R, CGColorRef Color)
	{
		G.Save();
		G.Shadow(0, 0, 6, Color);
		const double Rr = FMath::Min(R.size.width, R.size.height) / 2;
		G.Fill(G.RoundedRect(R, Rr, Rr), Color);
		G.Restore();
	}

	/// `Cab.dashTop`: the ledge from the faces' brow back to the glass.
	void DashTopArt(FDraw& G, const FAcCabLayout& L)
	{
		CGMutablePathRef Under = G.Path();
		CGPathMoveToPoint(Under, nullptr, 0, 0);
		for (const FVector2D& P : L.Skyline) CGPathAddLineToPoint(Under, nullptr, P.X, P.Y);
		CGPathAddLineToPoint(Under, nullptr, L.Size.X, 0);
		CGPathCloseSubpath(Under);
		G.Save();
		G.Shadow(0, 5, 24, G.Gray(0, 0.75));
		G.Fill(Under, G.Gray(0.02, 1));
		G.Restore();
		for (int32 K = 0; K < 3; ++K)
		{
			CGMutablePathRef Plate = G.Path();
			CGPathMoveToPoint(Plate, nullptr, L.Brow[K].X, L.Brow[K].Y);
			CGPathAddLineToPoint(Plate, nullptr, L.Brow[K + 1].X, L.Brow[K + 1].Y);
			CGPathAddLineToPoint(Plate, nullptr, L.Skyline[K + 1].X, L.Skyline[K + 1].Y);
			CGPathAddLineToPoint(Plate, nullptr, L.Skyline[K].X, L.Skyline[K].Y);
			CGPathCloseSubpath(Plate);
			const CGRect Box = CGPathGetPathBoundingBox(Plate);
			G.Save();
			CGContextAddPath(G.C, Plate);
			CGContextClip(G.C);
			Armour(G, Box, 40 + K);
			G.FillRect(Box, G.Gray(1, K == 1 ? 0.12 : 0.07));
			G.Linear(G.Gradient({G.Gray(0, 0), G.Gray(0, 0.35)}, {0, 1}), Pt(0, L.Brow[K].Y), Pt(0, L.Skyline[K].Y));
			G.Restore();
		}
		CGMutablePathRef Folds = G.Path();
		for (int32 K : {1, 2})
		{
			CGPathMoveToPoint(Folds, nullptr, L.Brow[K].X, L.Brow[K].Y);
			CGPathAddLineToPoint(Folds, nullptr, L.Skyline[K].X, L.Skyline[K].Y);
		}
		G.Stroke(Folds, G.Gray(0, 0.5), 1.2);
		CGMutablePathRef Back = G.Path();
		CGMutablePathRef Front = G.Path();
		for (int32 K = 0; K < 4; ++K)
		{
			if (K == 0)
			{
				CGPathMoveToPoint(Back, nullptr, L.Skyline[K].X, L.Skyline[K].Y);
				CGPathMoveToPoint(Front, nullptr, L.Brow[K].X, L.Brow[K].Y);
			}
			else
			{
				CGPathAddLineToPoint(Back, nullptr, L.Skyline[K].X, L.Skyline[K].Y);
				CGPathAddLineToPoint(Front, nullptr, L.Brow[K].X, L.Brow[K].Y);
			}
		}
		G.Stroke(Back, G.Gray(0, 0.85), 2);
		G.Stroke(Front, G.Rgb(0.65, 0.75, 0.92, 0.55), 1.4);
	}

	void FaceShade(FDraw& G, CGRect R)
	{
		G.Linear(G.Gradient({G.Gray(0, 0), G.Gray(0, 0.42)}, {0, 1}), Pt(0, CGRectGetMaxY(R) - 30), Pt(0, CGRectGetMinY(R)));
		CGMutablePathRef Skirt = G.Path();
		CGPathMoveToPoint(Skirt, nullptr, CGRectGetMinX(R), 9);
		CGPathAddLineToPoint(Skirt, nullptr, CGRectGetMaxX(R), 9);
		Seam(G, Skirt);
	}

	void FaceEdges(FDraw& G, CGRect R, std::initializer_list<double> Creases)
	{
		G.Save();
		CGContextClipToRect(G.C, R);
		const double Y1 = CGRectGetMaxY(R);
		G.Linear(G.Gradient({G.Gray(0, 0.45), G.Gray(0, 0)}, {0, 1}), Pt(0, Y1 - 2), Pt(0, Y1 - 12));
		CGContextMoveToPoint(G.C, CGRectGetMinX(R), Y1 - 1);
		CGContextAddLineToPoint(G.C, CGRectGetMaxX(R), Y1 - 1);
		CGContextSetStrokeColorWithColor(G.C, G.Rgb(0.65, 0.75, 0.92, 0.5));
		CGContextSetLineWidth(G.C, 2);
		CGContextStrokePath(G.C);
		for (double X : Creases)
		{
			CGContextMoveToPoint(G.C, X, CGRectGetMinY(R));
			CGContextAddLineToPoint(G.C, X, Y1);
		}
		CGContextSetStrokeColorWithColor(G.C, G.Gray(0, 0.75));
		CGContextSetLineWidth(G.C, 3);
		CGContextStrokePath(G.C);
		G.Restore();
	}

	/// `Cab.housing`: a raised housing round a screen.
	void Housing(FDraw& G, CGRect R, double Cut, int32 Seed, double Rise = 0)
	{
		CGRect Outer = Inset(R, -11, -10);
		Outer.size.height += Rise;
		CGPathRef P = CutPath(G, Outer, All(Cut));
		G.Save();
		G.Shadow(0, -3, 9, G.Gray(0, 0.9));
		G.Fill(P, G.Gray(0.05, 1));
		G.Restore();
		G.Save();
		CGContextAddPath(G.C, P);
		CGContextClip(G.C);
		Armour(G, Outer, Seed);
		G.Linear(G.Gradient({G.Gray(1, 0.1), G.Gray(1, 0), G.Gray(0, 0.2)}, {0, 0.4, 1}), Pt(0, CGRectGetMaxY(Outer)),
			Pt(0, CGRectGetMinY(Outer)));
		G.Restore();
		Rim(G, P);
		G.Stroke(CutPath(G, Inset(R, -3.5, -3.5), All(FMath::Max(Cut - 7, 3.0))), G.Gray(0, 0.85), 3);
		Bolt(G, Pt(CGRectGetMinX(Outer) + 7, CGRectGetMinY(Outer) + 7), 2.2);
		Bolt(G, Pt(CGRectGetMaxX(Outer) - 7, CGRectGetMinY(Outer) + 7), 2.2);
	}

	void CabScreen(FDraw& G, CGRect R, bool bGrid)
	{
		ChromeScreen(G, G.RoundedRect(R, 6, 6), R, bGrid, 0.5);
	}

	/// `Cab.deckBay` (the station's name is Slate text, see AcCabArt.h).
	void DeckBay(FDraw& G, CGRect R)
	{
		CGPathRef Plate = CutPath(G, R, All(6));
		G.Save();
		CGContextAddPath(G.C, Plate);
		CGContextClip(G.C);
		G.FillRect(R, G.Gray(0, 0.28));
		G.Restore();
		Seam(G, Plate);
		Bolt(G, Pt(CGRectGetMinX(R) + 7, CGRectGetMaxY(R) - 7), 2);
		Bolt(G, Pt(CGRectGetMaxX(R) - 7, CGRectGetMaxY(R) - 7), 2);
		// Swift sets the station name's cyan glow (`cg.setShadow`, blur 4,
		// alpha 0.8) between NSGraphicsContext.save/restoreGraphicsState,
		// which save and restore the context that was current before, not
		// this one: the shadow stays on and lights the glass's rim, the
		// screen and the rest of the face drawn after it. Kept so the deck
		// screen is as bright as Swift's (the name itself is Slate text).
		G.Shadow(0, 0, 4, G.Alpha(G.Cyan(), 0.8));
		const FAcRect Glass = FAcCabLayout::DeckScreen(FAcRect(R.origin.x, R.origin.y, R.size.width, R.size.height));
		G.Stroke(CutPath(G, Inset(CgRect(Glass), -3, -3), All(6)), G.Gray(0, 0.85), 3);
		CabScreen(G, CgRect(Glass), false);
	}

	void Instruments(FDraw& G, CGRect R, bool bFlip)
	{
		CGPathRef Plate = CutPath(G, R, All(6));
		G.Save();
		CGContextAddPath(G.C, Plate);
		CGContextClip(G.C);
		G.FillRect(R, G.Gray(0, 0.28));
		G.Restore();
		Seam(G, Plate);
		Bolt(G, Pt(CGRectGetMinX(R) + 7, CGRectGetMaxY(R) - 7), 2);
		Bolt(G, Pt(CGRectGetMaxX(R) - 7, CGRectGetMaxY(R) - 7), 2);
		const CGRect In = Inset(R, 12, 14);
		const double Key = FMath::Min(14.0, (In.size.width - 8) / 3);
		for (int32 K = 0; K < 3; ++K)
		{
			const CGRect Q = CGRectMake(CGRectGetMinX(In) + K * (Key + 4), CGRectGetMaxY(In) - Key - 6, Key, Key);
			G.Fill(G.RoundedRect(Q, 2, 2), G.Rgb(0.02, 0.04, 0.07));
			const bool bOn = ((K == 0) != bFlip) || K == 2;
			if (bOn)
			{
				Lamp(G, CGRectMake(CGRectGetMinX(Q) + 3, CGRectGetMaxY(Q) - 5, Q.size.width - 6, 2.5),
					K == 2 ? G.Cyan() : G.Rgb(0.4, 1, 0.5));
			}
		}
		const CGRect Vent = CGRectMake(CGRectGetMinX(In), CGRectGetMinY(In) + In.size.height * 0.32, In.size.width, In.size.height * 0.3);
		for (double Y = CGRectGetMinY(Vent); Y < CGRectGetMaxY(Vent) - 2; Y += 6)
		{
			const CGRect Slot = CGRectMake(CGRectGetMinX(Vent), Y, Vent.size.width, 3);
			G.Fill(G.RoundedRect(Slot, 1.5, 1.5), G.Gray(0, 0.7));
			CGContextMoveToPoint(G.C, CGRectGetMinX(Slot) + 1.5, CGRectGetMinY(Slot) - 0.5);
			CGContextAddLineToPoint(G.C, CGRectGetMaxX(Slot) - 1.5, CGRectGetMinY(Slot) - 0.5);
			CGContextSetStrokeColorWithColor(G.C, G.Gray(1, 0.1));
			CGContextSetLineWidth(G.C, 0.8);
			CGContextStrokePath(G.C);
		}
		const double D = FMath::Min(18.0, In.size.width / 2 - 6);
		for (int32 K = 0; K < 2; ++K)
		{
			const CGPoint O = Pt(CGRectGetMinX(In) + D / 2 + 2 + K * (D + 8), CGRectGetMinY(In) + D / 2);
			const CGRect Dial = CGRectMake(O.x - D / 2, O.y - D / 2, D, D);
			CGContextAddEllipseInRect(G.C, Inset(Dial, -1.5, -1.5));
			CGContextSetFillColorWithColor(G.C, G.Gray(0, 0.6));
			CGContextFillPath(G.C);
			CGContextAddEllipseInRect(G.C, Dial);
			CGContextSetFillColorWithColor(G.C, G.Rgb(0.02, 0.06, 0.08));
			CGContextFillPath(G.C);
			const double A = (bFlip ? 0.4 : 1.1) + K * 0.9;
			CGContextMoveToPoint(G.C, O.x, O.y);
			CGContextAddLineToPoint(G.C, O.x + FMath::Cos(A) * D * 0.4, O.y + FMath::Sin(A) * D * 0.4);
			G.Save();
			G.Shadow(0, 0, 3, G.Cyan());
			CGContextSetStrokeColorWithColor(G.C, G.Cyan());
			CGContextSetLineWidth(G.C, 1.2);
			CGContextStrokePath(G.C);
			G.Restore();
		}
	}

	void SideFace(FDraw& G, CGRect R, CGRect Screen, double InnerX, int32 Seed, CGRect Block, bool bDeck)
	{
		Armour(G, R, Seed);
		const double OuterX = InnerX == CGRectGetMaxX(R) ? CGRectGetMinX(R) : CGRectGetMaxX(R);
		G.Linear(G.Gradient({G.Gray(1, 0.04), G.Gray(0, 0.25)}, {0, 1}), Pt(OuterX, 0), Pt(InnerX, 0));
		FaceShade(G, R);
		FaceEdges(G, R, {InnerX});
		if (bDeck && Block.size.width > 60) DeckBay(G, Block);
		else if (Block.size.width > 30) Instruments(G, Block, InnerX == CGRectGetMinX(R));
		Housing(G, Screen, 12, (int32)CGRectGetMinX(Screen));
		CabScreen(G, Screen, false);
	}

	void CenterFace(FDraw& G, CGRect R, CGRect Cc)
	{
		Armour(G, R, 9);
		G.Linear(G.Gradient({G.Gray(1, 0.1), G.Gray(1, 0)}, {0, 1}), Pt(0, CGRectGetMaxY(R)), Pt(0, CGRectGetMidY(R)));
		FaceShade(G, R);
		FaceEdges(G, R, {CGRectGetMinX(R), CGRectGetMaxX(R)});
		Housing(G, Cc, 16, 9);
		CabScreen(G, Cc, true);
		for (double X : {CGRectGetMinX(Cc) - 9, CGRectGetMaxX(Cc) + 3})
		{
			Hazard(G, CGRectMake(X, CGRectGetMinY(Cc) + 18, 6, Cc.size.height - 36));
		}
		Lamp(G, CGRectMake(CGRectGetMinX(Cc) - 15 - 2.5, CGRectGetMaxY(Cc) - 34, 5, 14), G.Rgb(1, 0.62, 0.15));
		Lamp(G, CGRectMake(CGRectGetMaxX(Cc) + 15 - 2.5, CGRectGetMaxY(Cc) - 34, 5, 14), G.Rgb(1, 0.2, 0.15));
	}
}

FAcArtImage AcCabArt::Face(const int32 K, const FAcCabLayout& L)
{
	const FAcRect& R = L.Faces[FMath::Clamp(K, 0, 2)].Flat;
	FDraw G(R.W, R.H, 2);
	CGContextTranslateCTM(G.C, -R.X, -R.Y);
	switch (K)
	{
	case 0: SideFace(G, CgRect(R), CgRect(L.MapWell), R.MaxX(), 31, CgRect(L.Blocks[0]), true); break;
	case 1: CenterFace(G, CgRect(R), CgRect(L.CenterWell)); break;
	default: SideFace(G, CgRect(R), CgRect(L.CardWell), R.MinX(), 32, CgRect(L.Blocks[1]), false); break;
	}
	return G.Image();
}

FAcArtImage AcCabArt::DashTop(const FAcCabLayout& L)
{
	// Cab.node(frame: false): band 0 of the full image, from the bottom to
	// the dashboard's top plus the corner and the shadow it throws.
	const double M = FMath::RoundHalfFromZero(FAcCabLayout::Corner + 22);
	const double Bottom = FMath::RoundHalfFromZero(L.Top() + M);
	FDraw G(L.Size.X, Bottom, 2);
	DashTopArt(G, L);
	return G.Image();
}

FAcArtImage AcCabArt::ViewFrame(const FVector2D Size, const double RailW)
{
	FDraw G(Size.X, Size.Y, 1.5);
	const CGRect Full = CGRectMake(0, 0, Size.X, Size.Y);
	const CGRect Opening = CGRectMake(RailW, -40, Size.X - 2 * RailW, Size.Y - RailW + 40);
	CGPathRef Hole = G.RoundedRect(Opening, FAcCabLayout::Corner, FAcCabLayout::Corner);
	CGMutablePathRef Body = G.Path();
	CGPathAddRect(Body, nullptr, Full);
	CGPathAddPath(Body, nullptr, Hole);
	G.Save();
	CGContextAddPath(G.C, Body);
	CGContextEOClip(G.C);
	Armour(G, Full, 7, &Opening);
	G.Restore();
	G.Save();
	CGContextAddPath(G.C, Hole);
	CGContextClip(G.C);
	G.Shadow(0, -2, 10, G.Gray(0, 0.85));
	CGContextAddPath(G.C, Body);
	CGContextSetFillColorWithColor(G.C, G.Gray(0, 1));
	CGContextEOFillPath(G.C);
	G.Restore();
	G.Stroke(Hole, G.Gray(0, 0.8), 1.6);
	GlowLine(G, Hole, G.Rgb(0.25, 0.8, 1), 1.1, 5, 0.55);
	CGContextSetStrokeColorWithColor(G.C, G.Rgb(0.65, 0.75, 0.9, 0.35));
	CGContextSetLineWidth(G.C, 1);
	CGContextStrokeRect(G.C, Inset(Full, 0.5, 0.5));
	for (double Y = Size.Y * 0.3; Y < Size.Y * 0.75; Y += 90)
	{
		Bolt(G, Pt(RailW / 2, Y), 2);
		Bolt(G, Pt(Size.X - RailW / 2, Y), 2);
	}
	return G.Image();
}

FAcArtImage AcCabArt::ButtonFace(const double Side, const bool bEnabled)
{
	FDraw G(Side, Side, 2);
	const CGRect R = Inset(CGRectMake(0, 0, Side, Side), 1, 1);
	CGPathRef P = G.RoundedRect(R, 6, 6);
	G.Save();
	CGContextAddPath(G.C, P);
	CGContextClip(G.C);
	CGColorRef Top = bEnabled ? G.Rgb(0.1, 0.22, 0.38) : G.Rgb(0.07, 0.08, 0.1);
	CGColorRef Bottom = bEnabled ? G.Rgb(0.02, 0.06, 0.13) : G.Rgb(0.03, 0.03, 0.04);
	G.Linear(G.Gradient({Top, Bottom}, {0, 1}), Pt(0, CGRectGetMaxY(R)), Pt(0, CGRectGetMinY(R)));
	if (bEnabled)
	{
		G.Radial(G.Gradient({G.Alpha(G.Cyan(), 0.25), G.Alpha(G.Cyan(), 0)}, {0, 1}), Pt(CGRectGetMidX(R), CGRectGetMidY(R)),
			Side * 0.6);
	}
	G.Fill(G.RoundedRect(CGRectMake(CGRectGetMinX(R) + 3, CGRectGetMidY(R) + 2, R.size.width - 6, R.size.height / 2 - 5), 4, 4),
		G.Gray(1, bEnabled ? 0.07 : 0.03));
	G.Restore();
	return G.Image();
}

FAcArtImage AcCabArt::Scanlines(const FVector2D Size)
{
	FDraw G(Size.X, Size.Y, 1);
	CGContextSetFillColorWithColor(G.C, G.Rgb(0, 0.02, 0.05, 0.45));
	for (double Y = 0; Y < Size.Y; Y += 3) CGContextFillRect(G.C, CGRectMake(0, Y, Size.X, 1));
	return G.Image();
}

FAcArtImage AcCabArt::Plate(const FVector2D Size, const FAcCuts Cuts, const bool bLeds, const int32 Seed)
{
	const double Pad = PlatePad;
	FDraw G(Size.X + 2 * Pad, Size.Y + 2 * Pad, 2);
	CGContextTranslateCTM(G.C, Pad, Pad);
	const CGRect R = CGRectMake(0, 0, Size.X, Size.Y);
	CGPathRef Outer = CutPath(G, R, Cuts, 1);
	G.Save();
	G.Shadow(0, -2, 6, G.Gray(0, 0.8));
	G.Fill(Outer, G.Gray(0.05, 1));
	G.Restore();
	Steel(G, Outer, R, Seed);
	Rim(G, Outer);
	GlowLine(G, CutPath(G, R, Cuts, 4.5), G.Cyan(), 1.2, 6, 0.85);
	if (bLeds && Size.X > 120)
	{
		Leds(G, Pt(CGRectGetMidX(R) - 30, 1.2), 5, G.Cyan(), CGSizeMake(8, 2), 4);
	}
	return G.Image();
}

// MARK: - The cab's frame (chunk D3: `Cab.draw(frame: true)`, `frameSeams`, `lip`, `socket`)

namespace
{
	void FrameSeams(FDraw& G, const FAcCabLayout& L)
	{
		const FAcRect& W = L.Window;
		const FVector2D S = L.Size;
		CGMutablePathRef P = G.Path();
		for (const double F : {0.27, 0.73})
		{
			const double X = FMath::RoundHalfFromZero(W.MinX() + W.W * F);
			CGPathMoveToPoint(P, nullptr, X, W.MaxY() + 4);
			CGPathAddLineToPoint(P, nullptr, X, S.Y);
		}
		for (const double X : {W.MinX(), W.MaxX()})
		{
			// Where the pillar meets the rail, and its plate's joint part way down.
			const double Inner = X == W.MinX() ? X - 6 : X + 6, Outer = X == W.MinX() ? 0 : S.X;
			for (const double Y : {W.MaxY() - 70, W.MinY() + W.H * 0.42})
			{
				CGPathMoveToPoint(P, nullptr, Outer, Y);
				CGPathAddLineToPoint(P, nullptr, Inner, Y);
			}
		}
		Seam(G, P);
		// An inset strip down the middle of each pillar.
		if (W.MinX() > 26)
		{
			for (const double Cx : {W.MinX() / 2, (W.MaxX() + S.X) / 2})
			{
				const CGRect Strip = CGRectMake(Cx - 4, L.Top() + 70, 8, W.MaxY() - 110 - L.Top() - 70);
				Seam(G, G.RoundedRect(Strip, 4, 4));
				Bolt(G, Pt(Cx, CGRectGetMinY(Strip) - 14));
				Bolt(G, Pt(Cx, CGRectGetMaxY(Strip) + 14));
			}
		}
		// Bolts along the rail, clear of the lamps.
		for (double X = W.MinX() + 110; X < W.MaxX() - 100; X += 150)
		{
			Bolt(G, Pt(X, FMath::RoundHalfFromZero((W.MaxY() + S.Y) / 2)), 2.4);
		}
	}

	void Lip(FDraw& G, const FAcCabLayout& L, CGPathRef Hole, CGPathRef Body)
	{
		const CGRect W = CgRect(L.Window);
		const double Band = 10, C = FAcCabLayout::Corner;
		CGPathRef Outer = G.RoundedRect(Inset(W, -Band, -Band), C + Band, C + Band);
		// A step down to the lip round the top and the sides.
		const double StepBand = 28;
		CGPathRef Step = G.RoundedRect(Inset(W, -StepBand, -StepBand), C + StepBand, C + StepBand);
		const CGRect Upper = CGRectMake(0, CGRectGetMinY(W), L.Size.X, L.Size.Y - CGRectGetMinY(W));
		G.Save();
		CGContextClipToRect(G.C, Upper);
		CGMutablePathRef StepRing = G.Path();
		CGPathAddPath(StepRing, nullptr, Step);
		CGPathAddPath(StepRing, nullptr, Outer);
		CGContextAddPath(G.C, StepRing);
		CGContextEOClip(G.C);
		G.Linear(G.Gradient({G.Gray(0, 0.05), G.Gray(0, 0.28)}, {0, 1}), Pt(0, CGRectGetMaxY(W) + StepBand), Pt(0, CGRectGetMinY(W)));
		G.Restore();
		G.Save();
		CGContextClipToRect(G.C, Upper);
		Seam(G, Step);
		G.Restore();
		CGMutablePathRef Ring = G.Path();
		CGPathAddPath(Ring, nullptr, Outer);
		CGPathAddPath(Ring, nullptr, Hole);
		G.Save();
		CGContextAddPath(G.C, Ring);
		CGContextEOClip(G.C);
		G.Linear(G.Gradient({G.Gray(1, 0.13), G.Gray(1, 0.03), G.Gray(0, 0.25)}, {0, 0.5, 1}), Pt(0, CGRectGetMaxY(W) + Band),
			Pt(0, CGRectGetMinY(W) - Band));
		G.Restore();
		// The groove outside, a lit edge on the lip, a dark edge at the glass.
		G.Stroke(Outer, G.Gray(0, 0.75), 2.2);
		G.Stroke(G.RoundedRect(Inset(W, -Band + 1.6, -Band + 1.6), C + Band - 1.6, C + Band - 1.6), G.Rgb(0.65, 0.75, 0.9, 0.3), 1);
		// The shadow onto the glass, deepest under the rail.
		G.Save();
		CGContextAddPath(G.C, Hole);
		CGContextClip(G.C);
		G.Shadow(0, -3, 14, G.Gray(0, 0.85));
		CGContextAddPath(G.C, Body);
		CGContextSetFillColorWithColor(G.C, G.Gray(0, 1));
		CGContextEOFillPath(G.C);
		G.Restore();
		G.Stroke(Hole, G.Gray(0, 0.8), 1.6);
		GlowLine(G, Hole, G.Rgb(0.25, 0.8, 1), 1.1, 5, 0.55);
	}

	void Socket(FDraw& G, FVector2D P)
	{
		const CGRect R = CGRectMake(P.X - 5.5, P.Y - 13, 11, 26);
		G.Fill(G.RoundedRect(CGRectOffset(R, 0, -1), 5.5, 5.5), G.Gray(0, 0.7));
		G.Stroke(G.RoundedRect(R, 5.5, 5.5), G.Rgb(0.6, 0.7, 0.85, 0.5), 1.2);
	}
}

TArray<FAcArtBand> AcCabArt::Framed(const FAcCabLayout& L)
{
	constexpr double Scale = 2;
	const FVector2D S = L.Size;
	FDraw G(S.X, S.Y, Scale);
	const CGRect Full = CGRectMake(0, 0, S.X, S.Y);
	const CGRect Win = CgRect(L.Window);
	CGPathRef Hole = G.RoundedRect(Win, FAcCabLayout::Corner, FAcCabLayout::Corner);
	CGMutablePathRef Body = G.Path();
	CGPathAddRect(Body, nullptr, Full);
	CGPathAddPath(Body, nullptr, Hole);
	G.Save();
	CGContextAddPath(G.C, Body);
	CGContextEOClip(G.C);
	Armour(G, Full, 5, &Win);
	FrameSeams(G, L);
	G.Restore();
	Lip(G, L, Hole, Body);
	DashTopArt(G, L);
	for (int32 K = 0; K < 2; ++K) Socket(G, L.Lamp(K));
	const FAcArtImage Whole = G.Image();

	// Cab.node: each band reaches past the window's edge by the rounded
	// corner and the shadow the lip (or the dashboard) casts onto the glass.
	const FAcRect& W = L.Window;
	const double M = FMath::RoundHalfFromZero(FAcCabLayout::Corner + 22);
	const double Bottom = FMath::RoundHalfFromZero(L.Top() + M);
	const FAcRect Bands[4] = {FAcRect(0, 0, S.X, Bottom), FAcRect(0, W.MaxY() - M, S.X, S.Y - W.MaxY() + M),
		FAcRect(0, Bottom, W.MinX() + M, W.MaxY() - M - Bottom), FAcRect(W.MaxX() - M, Bottom, S.X - W.MaxX() + M, W.MaxY() - M - Bottom)};
	TArray<FAcArtBand> Out;
	for (const FAcRect& B : Bands)
	{
		if (B.W <= 0 || B.H <= 0) continue;
		// Image rows run from the top; `CGRect.integral` round the band's pixels.
		const int32 X0 = FMath::Clamp((int32)FMath::FloorToDouble(B.MinX() * Scale), 0, Whole.Width);
		const int32 X1 = FMath::Clamp((int32)FMath::CeilToDouble(B.MaxX() * Scale), 0, Whole.Width);
		const int32 Y0 = FMath::Clamp((int32)FMath::FloorToDouble((S.Y - B.MaxY()) * Scale), 0, Whole.Height);
		const int32 Y1 = FMath::Clamp((int32)FMath::CeilToDouble((S.Y - B.MinY()) * Scale), 0, Whole.Height);
		if (X1 <= X0 || Y1 <= Y0) continue;
		FAcArtBand& Band = Out.AddDefaulted_GetRef();
		Band.At = B;
		Band.Image.Width = X1 - X0;
		Band.Image.Height = Y1 - Y0;
		Band.Image.Points = FVector2D(B.W, B.H);
		Band.Image.Pixels.SetNumUninitialized(Band.Image.Width * Band.Image.Height);
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			FMemory::Memcpy(&Band.Image.Pixels[(Y - Y0) * Band.Image.Width], &Whole.Pixels[Y * Whole.Width + X0],
				Band.Image.Width * sizeof(FColor));
		}
	}
	return Out;
}

#endif
