#include "SAcPilotOverlay.h"

#include "AcConsolePaint.h"
#include "AcHudStyle.h"
#include "AcHudText.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/Clipping.h"
#include "Rendering/SlateRenderer.h"

#include <cmath>

/// Drawing in Swift points, y up from the view's bottom: `FAcConsolePaint`
/// for fills, rounded rectangles and labels, plus polylines with SpriteKit's
/// glow and filled polygons. Each call takes the next layer.
struct FAcPilotPen
{
	FSlateWindowElementList& Out;
	const FGeometry& Geometry;
	int32 Layer;
	double H;

	template <class F>
	void Paint(F&& Fn)
	{
		FAcConsolePaint P(Out, Geometry, Layer, H);
		Fn(P);
		Layer = P.NextLayer();
	}

	FVector2f Slate(FVector2D P) const { return FVector2f((float)P.X, (float)(H - P.Y)); }

	/// A polyline (`SKShapeNode` stroke) with `glowWidth` as a soft halo.
	void Lines(const TArray<FVector2D>& Points, const FLinearColor& Color, double Width, double Glow = 0, bool bRoundCaps = false)
	{
		if (Points.Num() < 2 || Color.A <= 0) return;
		TArray<FVector2f> S;
		S.Reserve(Points.Num());
		for (const FVector2D& Pt : Points) S.Add(Slate(Pt));
		auto Line = [&](const FLinearColor& C, double W)
		{
			FSlateDrawElement::MakeLines(Out, ++Layer, Geometry.ToPaintGeometry(), S, ESlateDrawEffect::None, C, true, (float)W);
		};
		if (Glow > 0)
		{
			Line(FAcConsolePaint::WithAlpha(Color, Color.A * 0.22), Width + 2 * Glow);
			Line(FAcConsolePaint::WithAlpha(Color, Color.A * 0.3), Width + Glow);
		}
		Line(Color, Width);
		if (bRoundCaps)
		{
			Disc(Points[0], Width / 2, Color);
			Disc(Points.Last(), Width / 2, Color);
		}
	}

	/// A convex polygon filled.
	void Poly(const TArray<FVector2D>& Points, const FLinearColor& Color)
	{
		if (Points.Num() < 3 || Color.A <= 0 || !FSlateApplication::IsInitialized()) return;
		const FSlateRenderTransform& T = Geometry.GetAccumulatedRenderTransform();
		const FColor C = Color.ToFColor(true);
		// The white brush sits in Slate's atlas: sample the middle of its own region.
		const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FAcHudStyle::White());
		const FSlateShaderResourceProxy* Proxy = Handle.GetResourceProxy();
		const FVector2f Uv = Proxy ? Proxy->StartUV + Proxy->SizeUV * 0.5f : FVector2f(0.5f, 0.5f);
		TArray<FSlateVertex> V;
		TArray<SlateIndex> I;
		for (const FVector2D& Pt : Points)
		{
			V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, Slate(Pt), Uv, C));
		}
		for (int32 K = 1; K + 1 < V.Num(); ++K)
		{
			I.Add(0);
			I.Add((SlateIndex)K);
			I.Add((SlateIndex)(K + 1));
		}
		FSlateDrawElement::MakeCustomVerts(Out, ++Layer, Handle, V, I, nullptr, 0, 0);
	}

	static TArray<FVector2D> Circle(FVector2D C, double R, int32 N = 48)
	{
		TArray<FVector2D> P;
		for (int32 K = 0; K <= N; ++K)
		{
			const double A = 2 * PI * K / N;
			P.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		return P;
	}

	void Disc(FVector2D C, double R, const FLinearColor& Color)
	{
		TArray<FVector2D> P = Circle(C, R, 16);
		P.Pop();
		Poly(P, Color);
	}

	/// A label (SpriteKit-aligned) with `PilotOverlay.shadow` if asked.
	void Label(const FString& Text, double Size, const FLinearColor& Color, FVector2D At, EAcHAlign HA = EAcHAlign::Center,
		EAcVAlign VA = EAcVAlign::Baseline, bool bShadow = true, bool bNames = true, float Opacity = 1,
		TOptional<FLinearColor> Accent = {})
	{
		if (Text.IsEmpty() || Opacity <= 0) return;
		FAcHudText T(Text, FAcHudStyle::Font((float)Size), Color, HA, VA);
		T.Accent = bNames ? Accent.Get(FAcHudStyle::NameAccent()) : FLinearColor::Transparent;
		if (bShadow)
		{
			T.ShadowOffset = FVector2f(1.2f, 1.2f);
			T.ShadowColor = FLinearColor(0, 0, 0, 0.75f);
		}
		Paint([&](FAcConsolePaint& P) { P.Text(T, At, Opacity); });
	}

	void Fill(const FAcRect& R, const FLinearColor& C) { Paint([&](FAcConsolePaint& P) { P.Fill(R, C); }); }
	void FillRounded(const FAcRect& R, double Radius, const FLinearColor& C)
	{
		Paint([&](FAcConsolePaint& P) { P.FillRounded(R, Radius, C); });
	}
	void Stroke(const FAcRect& R, double Radius, const FLinearColor& C, double Width, double Glow = 0)
	{
		Paint([&](FAcConsolePaint& P) { P.Stroke(R, Radius, C, Width, Glow); });
	}
	/// `SKShapeNode(rect:cornerRadius:)` filled and stroked.
	void Plate(const FAcRect& R, double Radius, const FLinearColor& Fill, const FLinearColor& Stroke, double Width)
	{
		FillRounded(R, Radius, Fill);
		this->Stroke(R, Radius, Stroke, Width);
	}
	void Image(const FSlateBrush* Brush, const FAcRect& R)
	{
		Paint([&](FAcConsolePaint& P) { P.Image(Brush, R); });
	}

	/// Clip what follows to `R` (points, y up) until `PopClip`.
	void PushClip(const FAcRect& R)
	{
		const FVector2f A = Geometry.LocalToAbsolute(Slate(FVector2D(R.MinX(), R.MaxY())));
		const FVector2f B = Geometry.LocalToAbsolute(Slate(FVector2D(R.MaxX(), R.MinY())));
		Out.PushClip(FSlateClippingZone(FSlateRect(A.X, A.Y, B.X, B.Y)));
	}
	void PopClip() { Out.PopClip(); }
};

namespace
{
	FLinearColor Srgb(double R, double G, double B, double A = 1) { return FAcHudStyle::Srgb(R, G, B, A); }
	FLinearColor White(double W, double A = 1) { return Srgb(W, W, W, A); }
	FLinearColor Alpha(const FLinearColor& C, double A) { return FAcConsolePaint::WithAlpha(C, A); }

	/// SpriteKit's `.easeOut` (a quadratic ease).
	double EaseOut(double T)
	{
		T = FMath::Clamp(T, 0.0, 1.0);
		return 1 - (1 - T) * (1 - T);
	}

	/// An action's fade: 1 until `Wait`, then down to 0 over `Fade`.
	double FadeAfter(double T, double Wait, double Fade) { return T < Wait ? 1 : FMath::Max(0.0, 1 - (T - Wait) / Fade); }

	/// Health bar colours: green over half, amber over a quarter, red.
	FLinearColor Health(double F, const FLinearColor& Red)
	{
		return F > 0.5 ? Srgb(0.3, 0.9, 0.4) : F > 0.25 ? SAcPilotOverlay::BlockedColor() : Red;
	}

	constexpr double HitWaitKill = 0.35, HitWait = 0.1, HitFadeKill = 0.35, HitFade = 0.15;
}

FLinearColor SAcPilotOverlay::LineColor() { return Srgb(0.45, 0.8, 1, 0.9); }
FLinearColor SAcPilotOverlay::ReadyColor() { return Srgb(0.45, 1, 0.55); }
FLinearColor SAcPilotOverlay::BlockedColor() { return Srgb(1, 0.72, 0.25); }
FLinearColor SAcPilotOverlay::EnemyColor() { return Srgb(1, 0.3, 0.25); }
FLinearColor SAcPilotOverlay::PanelFill() { return Srgb(0.02, 0.05, 0.09, 0.72); }
FLinearColor SAcPilotOverlay::PanelStroke() { return Srgb(0.3, 0.55, 0.75, 0.55); }

FLinearColor SAcPilotOverlay::ToneColor(const EAcTone Tone)
{
	switch (Tone)
	{
	case EAcTone::Ready:
	case EAcTone::Working: return ReadyColor();
	case EAcTone::Blocked: return BlockedColor();
	case EAcTone::Enemy: return EnemyColor();
	default: return LineColor();
	}
}

void SAcPilotOverlay::Construct(const FArguments& InArgs)
{
	bPanel = InArgs._Panel;
	SetVisibility(EVisibility::HitTestInvisible);
}

void SAcPilotOverlay::Reset()
{
	bHasInfo = false;
	LastHp.Reset();
	HurtSince = -10;
	HitSince = -10;
	ShownHit = 0;
	Floats.Reset();
	ShownNote.Reset();
	ScaleFrom = ScaleTo = 1;
	ScaleSince = -10;
}

double SAcPilotOverlay::ReticleScale(const double Now) const
{
	return FMath::Lerp(ScaleFrom, ScaleTo, FMath::Clamp((Now - ScaleSince) / 0.12, 0.0, 1.0));
}

void SAcPilotOverlay::Show(const FAcPilotInfo& In, const double Now)
{
	Clock = Now;
	// Open over something to work; closed in on an enemy in range.
	const double Open = In.Tone == EAcTone::Ready ? 1.25 : In.Tone == EAcTone::Enemy ? 0.8 : 1;
	const double Scale = ReticleScale(Now);
	if (FMath::Abs(Scale - Open) > 0.01 && ScaleTo != Open)
	{
		ScaleFrom = Scale;
		ScaleTo = Open;
		ScaleSince = Now;
	}
	if (In.Hit && In.Hit->Serial != ShownHit)
	{
		// `markHit`: the ticks flash, the damage floats off (KILLED for a kill).
		ShownHit = In.Hit->Serial;
		HitSince = Now;
		bHitKill = In.Hit->bKill;
		const double D = In.Hit->Damage;
		const FString Text = In.Hit->bKill ? FString(TEXT("KILLED"))
			: TEXT("-") + (FMath::RoundToDouble(D) == D ? FString::Printf(TEXT("%lld"), (long long)D) : FString::Printf(TEXT("%.1f"), D));
		Floats.Add({Text, In.Hit->bKill, Now});
	}
	if (!HoldMarkAge) Floats.RemoveAll([Now](const FFloat& F) { return Now - F.Born > (F.bKill ? 1.0 : 0.55); });
	if (In.Note != ShownNote)
	{
		ShownNote = In.Note;
		NoteSince = Now;
	}
	if (LastHp && In.Hp < *LastHp - 0.01) HurtSince = Now;
	LastHp = In.Hp;
	Info = In;
	bHasInfo = true;
}

int32 SAcPilotOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	if (!bHasInfo) return Layer;
	const double Now = FPlatformTime::Seconds();
	const FVector2D Size(Geometry.GetLocalSize());
	const double W = Size.X, H = Size.Y;
	if (W <= 0 || H <= 0) return Layer;
	FAcPilotPen P{Out, Geometry, Layer, H};
	const FVector2D C(W / 2, H / 2);

	// Inside a building: the view is dimmed green, its name over it.
	if (Info.Inside)
	{
		P.Fill(FAcRect(0, 0, W, H), Srgb(0.02, 0.12, 0.05, 0.78));
		P.Label(*Info.Inside, 30, Srgb(0.5, 1, 0.55, 0.9), FVector2D(C.X, C.Y + 70), EAcHAlign::Center, EAcVAlign::Baseline, false);
	}

	// The hurt flash: a red band round the view, fading over half a second.
	if (const double T = bHoldHurt ? 0.05 : Now - HurtSince; T >= 0 && T < 0.5)
	{
		const double A = 0.9 * (1 - T / 0.5);
		const TArray<FVector2D> Edge = {FVector2D(0, 0), FVector2D(W, 0), FVector2D(W, H), FVector2D(0, H), FVector2D(0, 0)};
		P.Lines(Edge, Srgb(1, 0.15, 0.1, 0.8 * A), 26, 20);
	}

	if (Info.Menu) PaintMenu(P, H);
	// The note over the reticle.
	if (ShownNote)
	{
		const double Opacity = bHoldNote ? 1 : FadeAfter(Now - NoteSince, 1.4, 0.5);
		P.Label(*ShownNote, 18, BlockedColor(), FVector2D(C.X, C.Y + 84), EAcHAlign::Center, EAcVAlign::Baseline, true, true,
			(float)Opacity);
	}
	PaintReticle(P, C, Now);
	PaintSight(P, W, H);
	PaintMarks(P, C, Now);
	PaintCompass(P, W, H);
	if (bPanel)
	{
		PaintPanel(P, W);
		// The keys, bottom left, bottom line first.
		TArray<FString> Lines;
		Info.Help.ParseIntoArray(Lines, TEXT("\n"), true);
		for (int32 I = 0; I < Lines.Num(); ++I)
		{
			P.Label(Lines[I], 13, White(0.85, 0.85), FVector2D(18, 22 + I * 17), EAcHAlign::Left);
		}
	}
	return P.Layer;
}

void SAcPilotOverlay::PaintReticle(FAcPilotPen& P, const FVector2D C, const double Now) const
{
	const double S = ReticleScale(Now);
	const FLinearColor Tone = ToneColor(Info.Tone);
	auto At = [&](double X, double Y) { return C + FVector2D(X, Y) * S; };
	// Four corner brackets and a centre pip.
	for (const FVector2D Sign : {FVector2D(-1, 1), FVector2D(1, 1), FVector2D(1, -1), FVector2D(-1, -1)})
	{
		P.Lines({At(Sign.X * 14, Sign.Y * 6), At(Sign.X * 14, Sign.Y * 14), At(Sign.X * 6, Sign.Y * 14)}, Tone, 1.6 * S, 1.2 * S);
	}
	P.Disc(C, 1.6 * S, LineColor());
	// The work ring: from the top, clockwise.
	if (Info.Progress)
	{
		const double Pr = FMath::Clamp(*Info.Progress, 0.0, 1.0);
		P.Lines(FAcPilotPen::Circle(C, 30 * S, 64), White(1, 0.12), 4 * S);
		TArray<FVector2D> Arc;
		const int32 N = FMath::Max(2, (int32)FMath::CeilToInt(64 * Pr));
		for (int32 K = 0; K <= N; ++K)
		{
			const double A = PI / 2 - Pr * 2 * PI * K / N;
			Arc.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 30 * S);
		}
		if (Pr > 0) P.Lines(Arc, ReadyColor(), 4 * S, 2 * S, true);
		P.Label(FString::Printf(TEXT("%s  %d%%"), *Info.ProgressLabel, (int32)(Pr * 100)), 17 * S, ReadyColor(), At(0, 46),
			EAcHAlign::Center, EAcVAlign::Center);
	}
	// The prompt under it, on a dark plate (only without a console).
	if (bPanel && Info.Prompt)
	{
		const double Size = 17 * S;
		const double Wd = FAcConsolePaint::TextWidth(*Info.Prompt, Size) + 22 * S;
		const FVector2D Pc = At(0, -52);
		const FAcRect Plate(Pc.X - Wd / 2, Pc.Y - 12 * S, Wd, 24 * S);
		P.Plate(Plate, 5 * S, FLinearColor(0, 0, 0, 0.55f), White(1, 0.12), 0.8 * S);
		P.Label(*Info.Prompt, Size, Tone, Pc, EAcHAlign::Center, EAcVAlign::Center);
	}
	// A vehicle's gun marker: where its weapon really points.
	if (Info.Gun)
	{
		const double Hh = P.H / 2;
		const FVector2D G(C.X + Info.Gun->X * Hh, Hh + Info.Gun->Y * Hh);
		P.Lines(FAcPilotPen::Circle(G, 9, 40), Alpha(Tone, 0.9), 1.6, 1);
		P.Disc(G, 1.6, Tone);
	}
}

void SAcPilotOverlay::PaintSight(FAcPilotPen& P, const double W, const double H) const
{
	if (!Info.Sight) return;
	// Under the compass, clear of the target.
	const FAcSightInfo& S = *Info.Sight;
	const FVector2D O(W / 2, H - 132);
	const FLinearColor Color = !S.bInRange ? BlockedColor() : S.bFriend ? ReadyColor() : EnemyColor();
	P.Label(S.Title.ToUpper(), 17, Color, O + FVector2D(-90, 13), EAcHAlign::Left, EAcVAlign::Baseline, true, false);
	P.Label(FString::Printf(TEXT("%.1f"), S.Distance), 14, White(0.9, 0.9), O + FVector2D(90, 13), EAcHAlign::Right,
		EAcVAlign::Baseline, true, false);
	P.Plate(FAcRect(O.X - 90, O.Y, 180, 8), 1.5, FLinearColor(0, 0, 0, 0.55f), White(1, 0.25), 0.8);
	const double F = FMath::Clamp(S.Hp / FMath::Max(S.MaxHp, 1.0), 0.0, 1.0);
	P.Fill(FAcRect(O.X - 88.5, O.Y + 1.5, 177 * F, 5), Health(F, EnemyColor()));
}

void SAcPilotOverlay::PaintMarks(FAcPilotPen& P, const FVector2D C, const double Now) const
{
	// The hit marker: four ticks round the pip, flashed per hit.
	const double T = HoldMarkAge ? *HoldMarkAge : Now - HitSince;
	const double Wait = bHitKill ? HitWaitKill : HitWait, Fade = bHitKill ? HitFadeKill : HitFade;
	if (T >= 0 && T < Wait + Fade)
	{
		const double A = FadeAfter(T, Wait, Fade);
		const double S = FMath::Lerp(bHitKill ? 1.7 : 1.25, 1.0, FMath::Clamp(T / 0.08, 0.0, 1.0));
		const FLinearColor Color = Alpha(bHitKill ? EnemyColor() : FLinearColor::White, A);
		for (const FVector2D Sign : {FVector2D(-1, 1), FVector2D(1, 1), FVector2D(1, -1), FVector2D(-1, -1)})
		{
			P.Lines({C + Sign * 3.5 * S, C + Sign * 8.5 * S}, Color, (bHitKill ? 3 : 2) * S, 1 * S, true);
		}
	}
	// The damage floating off it; KILLED over the reticle.
	for (const FFloat& Fl : Floats)
	{
		const double Age = HoldMarkAge ? *HoldMarkAge : Now - Fl.Born;
		if (Fl.bKill)
		{
			const FVector2D At = C + FVector2D(0, 64 + 8 * EaseOut(Age / 1.0));
			P.Label(Fl.Text, 18, EnemyColor(), At, EAcHAlign::Center, EAcVAlign::Center, true, true, (float)FadeAfter(Age, 0.6, 0.4));
		}
		else
		{
			const double E = EaseOut(Age / 0.55);
			const FVector2D At = C + FVector2D(18 + 10 * E, 8 + 22 * E);
			P.Label(Fl.Text, 15, Srgb(1, 0.9, 0.55), At, EAcHAlign::Left, EAcVAlign::Center, true, true, (float)FadeAfter(Age, 0.25, 0.3));
		}
	}
}

void SAcPilotOverlay::PaintCompass(FAcPilotPen& P, const double W, const double H) const
{
	const FVector2D O(W / 2, H - 64 - 16);
	const double Half = TapeWidth / 2, PerDeg = TapeWidth / TapeSpan;
	P.Plate(FAcRect(O.X - Half, O.Y - 14, TapeWidth, 28), 3, PanelFill(), PanelStroke(), 1);
	// Compass: 0° is north (−Z, up the top-down view), 90° east (+X).
	const double Deg = (Info.Heading + PI / 2) * 180 / PI;
	const double Wrapped = FMath::Fmod(FMath::Fmod(Deg, 360.0) + 360, 360.0);
	const double Shift = -Wrapped * PerDeg;
	P.PushClip(FAcRect(O.X - Half + 2, O.Y - 14, TapeWidth - 4, 28));
	// Ticks every 15°, labels every 45°, over 720° so it wraps.
	static const TMap<int32, FString> Names = {{0, TEXT("N")}, {45, TEXT("NE")}, {90, TEXT("E")}, {135, TEXT("SE")},
		{180, TEXT("S")}, {225, TEXT("SW")}, {270, TEXT("W")}, {315, TEXT("NW")}};
	for (int32 D = -360; D <= 360; D += 15)
	{
		const double X = O.X + Shift + D * PerDeg;
		if (X < O.X - Half - 20 || X > O.X + Half + 20) continue;
		const bool bMajor = D % 45 == 0;
		P.Fill(FAcRect(X - 0.5, O.Y - 13, 1, bMajor ? 9 : 5), Alpha(LineColor(), bMajor ? 0.9 : 0.5));
		if (const FString* N = bMajor ? Names.Find((D + 360) % 360) : nullptr)
		{
			P.Label(*N, N->Len() == 1 ? 15 : 12, *N == TEXT("N") ? BlockedColor() : White(0.9, 0.9), FVector2D(X, O.Y + 4),
				EAcHAlign::Center, EAcVAlign::Center, false, false);
		}
	}
	TOptional<double> GoalX;
	if (Info.Goal)
	{
		const ac::Vec2 D(Info.Goal->At.x - Info.Position.x, Info.Goal->At.y - Info.Position.y);
		const double R = std::remainder(std::atan2(D.y, D.x) - Info.Heading, 2 * PI);
		GoalX = FMath::Clamp(R * 180 / PI * PerDeg, -Half + 8, Half - 8);
		// The goal: a diamond on the tape.
		const FVector2D G(O.X + *GoalX, O.Y);
		const TArray<FVector2D> Diamond = {G + FVector2D(0, -12), G + FVector2D(6, -4), G + FVector2D(0, 4), G + FVector2D(-6, -4)};
		P.Poly(Diamond, ReadyColor());
		TArray<FVector2D> Ring = Diamond;
		Ring.Add(Diamond[0]);
		P.Lines(Ring, FLinearColor::White, 1, 2);
	}
	P.PopClip();
	if (GoalX)
	{
		P.Label(FString::Printf(TEXT("%s  %lld"), *Info.Goal->Label, (long long)FMath::RoundToDouble(Info.Goal->Distance)), 14,
			ReadyColor(), FVector2D(O.X + *GoalX, O.Y - 36));
	}
	// The centre notch and the heading in degrees.
	P.Fill(FAcRect(O.X - 1, O.Y - 16, 2, 32), FLinearColor::White);
	P.Label(FString::Printf(TEXT("%03d°"), (int32)FMath::RoundToDouble(Wrapped) % 360), 12, White(0.85), O + FVector2D(Half + 24, -5),
		EAcHAlign::Center, EAcVAlign::Baseline, false, false);
}

void SAcPilotOverlay::PaintPanel(FAcPilotPen& P, const double W) const
{
	// The unit's panel, bottom centre (only without a console).
	const double Pw = 360, Ph = 74;
	const FVector2D O(W / 2 - Pw / 2, 18);
	auto At = [&](double X, double Y) { return O + FVector2D(X, Y); };
	// A cut-corner console plate.
	const TArray<FVector2D> Outline = {At(0, 0), At(Pw, 0), At(Pw, Ph - 14), At(Pw - 14, Ph), At(14, Ph), At(0, Ph - 14)};
	P.Poly(Outline, PanelFill());
	TArray<FVector2D> Edge = Outline;
	Edge.Add(Outline[0]);
	P.Lines(Edge, PanelStroke(), 1.2);
	P.Label(Info.Name, 22, White(0.95), At(16, Ph - 30), EAcHAlign::Left, EAcVAlign::Baseline, false);
	// Hit points: a segmented bar.
	P.Plate(FAcRect(O.X + 16, O.Y + 14, 200, 12), 2, FLinearColor(0, 0, 0, 0.5f), PanelStroke(), 1);
	const double F = FMath::Clamp(Info.Hp / FMath::Max(Info.MaxHp, 1.0), 0.0, 1.0);
	P.Fill(FAcRect(O.X + 16, O.Y + 14, 200 * F, 12), Health(F, Srgb(1, 0.25, 0.2)));
	for (int32 K = 1; K < 10; ++K) P.Fill(FAcRect(O.X + 16 + K * 20, O.Y + 14, 1, 12), FLinearColor(0, 0, 0, 0.5f));
	P.Label(FString::Printf(TEXT("%lld / %lld"), (long long)FMath::RoundToDouble(Info.Hp), (long long)Info.MaxHp), 13, White(0.85),
		At(16, 32), EAcHAlign::Left, EAcVAlign::Baseline, false, false);
	// The cargo bay: an icon and the load.
	P.Plate(FAcRect(O.X + Pw - 124, O.Y + 12, 108, 48), 3, FLinearColor(0, 0, 0, 0.4f), PanelStroke(), 1);
	P.Label(TEXT("CARGO"), 11, White(0.7), At(Pw - 70, 46), EAcHAlign::Center, EAcVAlign::Baseline, false, false);
	if (Info.Cargo > 0)
	{
		const FVector2D Ic = At(Pw - 98, 28);
		P.Image(Info.bHydrogen ? FAcHudStyle::HydrogenIcon() : FAcHudStyle::OreIcon(), FAcRect(Ic.X - 13, Ic.Y - 13, 26, 26));
		// Silver for MH, opal violet for ore.
		P.Label(FString::Printf(TEXT("%lld"), (long long)Info.Cargo), 24,
			Info.bHydrogen ? Srgb(0.84, 0.9, 0.98) : Srgb(0.78, 0.66, 1), At(Pw - 80, 28), EAcHAlign::Left, EAcVAlign::Center, false, false);
	}
	else
	{
		P.Label(TEXT("empty"), 16, White(0.55), At(Pw - 80, 28), EAcHAlign::Left, EAcVAlign::Center, false, false);
	}
}

void SAcPilotOverlay::PaintMenu(FAcPilotPen& P, const double H) const
{
	// The build menu, left of the reticle: a panel of numbered lines.
	const TArray<FAcPilotMenuLine>& Menu = *Info.Menu;
	const FVector2D O(24, H / 2 + 120);
	const double RowH = 24, Wd = 300, Hh = (Menu.Num() + 1) * RowH + 18;
	P.Plate(FAcRect(O.X, O.Y - Hh + 8, Wd, Hh), 4, PanelFill(), PanelStroke(), 1.2);
	P.Label(TEXT("BUILD  (number to pick, B to close)"), 14, LineColor(), O + FVector2D(12, -14), EAcHAlign::Left,
		EAcVAlign::Baseline, false, false);
	for (int32 I = 0; I < Menu.Num(); ++I)
	{
		const bool bOk = Menu[I].bOk;
		// Dimmed with the line when it can't be afforded.
		P.Label(Menu[I].Line, 16, bOk ? FLinearColor::White : White(0.5), O + FVector2D(12, -14 - (I + 1) * RowH), EAcHAlign::Left,
			EAcVAlign::Baseline, false, true, 1, bOk ? FAcHudStyle::NameAccent() : Alpha(FAcHudStyle::NameAccent(), 0.5));
	}
}
