#include "SAcLevelUp.h"

#include "AcCabArt.h"
#include "AcConsoleInfo.h"
#include "AcConsolePaint.h"
#include "AcHudStyle.h"
#include "AcIcons.h"
#include "AcPilotText.h"
#include "SAcCommandMap.h"

#include "Engine/Texture2D.h"
#include "Widgets/Layout/SBox.h"

namespace
{
	FLinearColor C(double R, double G, double B, double A = 1) { return FAcHudStyle::Srgb(R, G, B, A); }
	FLinearColor White(double W, double A = 1) { return FAcHudStyle::Srgb(W, W, W, A); }
	FLinearColor Alpha(const FLinearColor& Col, double A) { return FLinearColor(Col.R, Col.G, Col.B, float(Col.A * A)); }

	/// `LevelInfo.color`.
	FLinearColor Gold() { return FAcHudStyle::LevelGold(); }
	/// `Console.queueColor`, `Console.keyColor`.
	FLinearColor QueueAmber() { return FAcHudStyle::QueueAmber(); }
	FLinearColor KeyYellow() { return FAcHudStyle::KeyYellow(); }

	/// Lines in Swift points (y up) over a view `H` high, SpriteKit's `glowWidth` as two soft passes.
	void DrawLines(FSlateWindowElementList& Out, const FGeometry& G, int32& Layer, double H, const TArray<FVector2D>& Pts,
		const FLinearColor& Col, double Width, double Glow = 0)
	{
		if (Pts.Num() < 2 || Col.A <= 0) return;
		TArray<FVector2f> S;
		for (const FVector2D& P : Pts) S.Add(FVector2f(float(P.X), float(H - P.Y)));
		if (Glow > 0)
		{
			FSlateDrawElement::MakeLines(Out, ++Layer, G.ToPaintGeometry(), S, ESlateDrawEffect::None, Alpha(Col, 0.22), true,
				float(Width + 2 * Glow));
			FSlateDrawElement::MakeLines(Out, ++Layer, G.ToPaintGeometry(), S, ESlateDrawEffect::None, Alpha(Col, 0.3), true,
				float(Width + Glow));
		}
		FSlateDrawElement::MakeLines(Out, ++Layer, G.ToPaintGeometry(), S, ESlateDrawEffect::None, Col, true, float(Width));
	}

	/// `CGPath.copy(dashingWithPhase: 0, lengths: [On, Off])` of a polyline.
	TArray<TArray<FVector2D>> Dashes(const TArray<FVector2D>& Path, double On, double Off)
	{
		TArray<TArray<FVector2D>> Out;
		bool bOn = true;
		double Left = On;
		TArray<FVector2D> Cur;
		if (Path.IsEmpty()) return Out;
		Cur.Add(Path[0]);
		for (int32 I = 0; I + 1 < Path.Num(); ++I)
		{
			FVector2D A = Path[I];
			const FVector2D B = Path[I + 1];
			double Len = FVector2D::Distance(A, B);
			while (Len > 1e-9)
			{
				const double Step = FMath::Min(Len, Left);
				A = A + (B - A).GetSafeNormal() * Step;
				if (bOn) Cur.Add(A);
				Len -= Step;
				Left -= Step;
				if (Left <= 1e-9)
				{
					if (bOn && Cur.Num() > 1) Out.Add(Cur);
					bOn = !bOn;
					Left = bOn ? On : Off;
					Cur.Reset();
					Cur.Add(A);
				}
			}
		}
		if (bOn && Cur.Num() > 1) Out.Add(Cur);
		return Out;
	}

	/// The one painter for a run of calls: `FAcConsolePaint` on a moving layer.
	struct FPaint
	{
		FSlateWindowElementList* Out;
		const FGeometry* G;
		int32& Layer;
		double H;

		bool On() const { return Out && G; }
		FAcConsolePaint P() const { return FAcConsolePaint(*Out, *G, Layer, H); }
		void Box(const FAcRect& R, double Radius, const FLinearColor& Fill, const FLinearColor& Stroke, double Width = 1, double Glow = 0)
		{
			if (!On()) return;
			FAcConsolePaint Q = P();
			if (Fill.A > 0) Q.FillRounded(R, Radius, Fill);
			if (Stroke.A > 0) Q.Stroke(R, Radius, Stroke, Width, Glow);
			Layer = Q.NextLayer();
		}
		void Fill(const FAcRect& R, const FLinearColor& Col)
		{
			if (!On()) return;
			FAcConsolePaint Q = P();
			Q.Fill(R, Col);
			Layer = Q.NextLayer();
		}
		void Image(const FSlateBrush* B, const FAcRect& R, const FLinearColor& Tint = FLinearColor::White)
		{
			if (!On()) return;
			FAcConsolePaint Q = P();
			Q.Image(B, R, Tint);
			Layer = Q.NextLayer();
		}
		void Text(const FString& S, double Size, const FLinearColor& Col, FVector2D At, EAcHAlign Align = EAcHAlign::Left,
			bool bNames = true, float Opacity = 1)
		{
			if (!On()) return;
			FAcConsolePaint Q = P();
			Q.Text(S, Size, Col, At, Align, true, bNames, Opacity);
			Layer = Q.NextLayer();
		}
		void Mark(EAcPerkReach R, FVector2D At, double S, const FLinearColor& Col)
		{
			if (On()) AcLevelPaint::Mark(*Out, *G, Layer, H, R, At, S, Col);
		}
	};
}

const FSlateBrush* FAcLevelPlate::Get(const FVector2D Size, const double TL, const double TR, const double BR, const double BL,
	const int32 Seed, const TCHAR* Name)
{
	if (Brush && For == Size) return Brush.Get();
	For = Size;
	const FAcArtImage Art = AcCabArt::Plate(Size, FAcCuts{TL, TR, BR, BL}, false, Seed);
	Texture.Reset(AcCabArt::ToTexture(Art, Name));
	Brush = MakeShared<FSlateBrush>();
	Brush->DrawAs = ESlateBrushDrawType::Image;
	Brush->ImageSize = Art.Points;
	if (Texture) Brush->SetResourceObject(Texture.Get());
	else Brush->TintColor = FSlateColor(FLinearColor::Transparent);
	return Brush.Get();
}

// MARK: - The marks

void AcLevelPaint::Mark(FSlateWindowElementList& Out, const FGeometry& G, int32& Layer, const double H, const EAcPerkReach Reach,
	const FVector2D At, const double S, const FLinearColor& Color)
{
	auto Chevron = [&](double X, double Y, double W)
	{
		const FVector2D O = At + FVector2D(X, Y);
		DrawLines(Out, G, Layer, H, {O + FVector2D(-W / 2, -W * 0.22), O + FVector2D(0, W * 0.28), O + FVector2D(W / 2, -W * 0.22)}, Color,
			FMath::Max(W * 0.2, 1.5), 1);
	};
	switch (Reach)
	{
	case EAcPerkReach::Driven:
		Chevron(0, S * 0.1, S * 0.6);
		Chevron(0, -S * 0.18, S * 0.6);
		break;
	case EAcPerkReach::Nearby:
	{
		TArray<FVector2D> Ring;
		const double R = S * 0.42;
		for (int32 K = 0; K <= 48; ++K)
		{
			const double A = 2 * PI * K / 48;
			Ring.Add(At + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		for (const TArray<FVector2D>& D : Dashes(Ring, 3, 2.5)) DrawLines(Out, G, Layer, H, D, Alpha(Color, 0.8), 1.2);
		Chevron(0, 0, S * 0.42);
		break;
	}
	case EAcPerkReach::Team:
		for (const double X : {-S * 0.3, 0.0, S * 0.3}) Chevron(X, X == 0 ? S * 0.06 : -S * 0.04, S * 0.3);
		break;
	}
}

// MARK: - The banner

void SAcLevelBanner::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::HitTestInvisible);
}

void SAcLevelBanner::Show(const ac::UnitKind Kind, const int32 Level, const double Now, const bool bHold)
{
	Title = AcLeveling::BannerTitle(Kind, Level);
	Sub = AcLeveling::BannerSub(Level);
	Since = Now;
	bHeld = bHold;
	bOn = true;
	Invalidate(EInvalidateWidgetReason::Paint);
	// Painted again every frame while it runs (Slate keeps a still widget's last paint).
	if (!bHold && !Timer.IsValid())
	{
		Timer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SAcLevelBanner::Animate));
	}
}

EActiveTimerReturnType SAcLevelBanner::Animate(double CurrentTime, float DeltaTime)
{
	Invalidate(EInvalidateWidgetReason::Paint);
	if (IsShowing(FPlatformTime::Seconds())) return EActiveTimerReturnType::Continue;
	Timer.Reset();
	return EActiveTimerReturnType::Stop;
}

bool SAcLevelBanner::IsShowing(const double Now) const
{
	return bOn && (bHeld || Now - Since < 0.18 + 2.4 + 0.6);
}

int32 SAcLevelBanner::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const double Now = FPlatformTime::Seconds();
	if (!IsShowing(Now)) return Layer;
	const FVector2D Size(Geometry.GetLocalSize());
	if (Size.X <= 0 || Size.Y <= 0) return Layer;

	// The run: in (0.18 s: fade in, ×1.15 → 1), 2.4 s, out (0.6 s fade).
	double A = 1, Scale = 1;
	if (!bHeld)
	{
		const double T = Now - Since;
		if (T < 0.18)
		{
			const double K = T / 0.18;
			A = K;
			Scale = 1.15 + (1 - 1.15) * K;
		}
		else if (T > 0.18 + 2.4)
		{
			A = FMath::Max(0.0, 1 - (T - 0.18 - 2.4) / 0.6);
		}
	}
	// `n.position`: the middle, 74 % up the view (Swift points, y up).
	const FVector2D O(FMath::RoundHalfFromZero(Size.X / 2), FMath::RoundHalfFromZero(Size.Y * 0.74));
	const FGeometry G = Scale == 1 ? Geometry
		: Geometry.MakeChild(FSlateRenderTransform(float(Scale)), FVector2f(float(O.X / Size.X), float((Size.Y - O.Y) / Size.Y)));

	const double TitleW = FAcConsolePaint::TextWidth(Title, 40), SubW = FAcConsolePaint::TextWidth(Sub, 14);
	const double W = FMath::Max(TitleW, SubW) + 80, H = 82;
	int32 L = Layer;
	FPaint P{&Out, &G, L, Size.Y};
	// `Chrome.plateNode(..., cuts: (14, 14, 4, 4), leds: false, seed: 23)`.
	const FSlateBrush* Brush = Plate.Get(FVector2D(W, H), 14, 14, 4, 4, 23, TEXT("AcLevelBannerPlate"));
	const double Pad = AcCabArt::PlatePad;
	P.Image(Brush, FAcRect(O.X - W / 2 - Pad, O.Y - 32 - Pad, W + 2 * Pad, H + 2 * Pad), FLinearColor(1, 1, 1, float(A)));
	// Gold rules either side of the title.
	for (const double Side : {-1.0, 1.0})
	{
		const double X = O.X + Side * (TitleW / 2 + 22);
		P.Fill(FAcRect(X - 11, O.Y + 16 - 1, 22, 2), Alpha(Gold(), A));
	}
	// One amber line: no name tint on the kind (Swift's banner has none).
	P.Text(Title, 40, Gold(), FVector2D(O.X, O.Y + 2), EAcHAlign::Center, false, float(A));
	P.Text(Sub, 14, FAcHudStyle::Soft(), FVector2D(O.X, O.Y - 20), EAcHAlign::Center, false, float(A));
	return L;
}

// MARK: - The pick cards

void SAcPickCards::Construct(const FArguments& InArgs)
{
	OnPick = InArgs._OnPick;
	SetVisibility(EVisibility::Collapsed);
	// Only the panel takes the pointer: the rest of the view stays the world's
	// (and the console's view switch).
	ChildSlot
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(TAttribute<FMargin>::CreateLambda([this]()
		{
			return FMargin(float(PanelRect.X), float(Cab.Size.Y - PanelRect.MaxY()), 0, 0);
		}))
		[
			SNew(SBox)
			.Visibility(EVisibility::Visible)
			.WidthOverride(TAttribute<FOptionalSize>::CreateLambda([this]() { return FOptionalSize(float(PanelRect.W)); }))
			.HeightOverride(TAttribute<FOptionalSize>::CreateLambda([this]() { return FOptionalSize(float(PanelRect.H)); }))
		];
}

void SAcPickCards::Show(const TOptional<FAcPickOffer>& InOffer, const FAcCabLayout& Layout, const FLinearColor& InAccent, const bool bInKeys)
{
	const bool bSameLayout = Cab.Size == Layout.Size && Cab.CenterWell.X == Layout.CenterWell.X && Cab.CenterWell.W == Layout.CenterWell.W
		&& Cab.ViewSwitch.X == Layout.ViewSwitch.X && Cab.Top() == Layout.Top();
	if (InOffer == Shown && bSameLayout && Accent == InAccent && bKeys == bInKeys) return;
	Shown = InOffer;
	Cab = Layout;
	Accent = InAccent;
	bKeys = bInKeys;
	Pressed.Reset();
	Relayout();
	SetVisibility(Shown ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SAcPickCards::Relayout()
{
	Frames.Reset();
	Lines.Reset();
	PanelRect = FAcRect();
	if (!Shown) return;
	// `showPicks`: clear of the view switch on the right, over the screens.
	const FAcRect& Center = Cab.CenterWell;
	const double Width = FMath::Min3(640.0, FMath::Max(420.0, Center.W + 40), 2 * (Cab.ViewSwitch.MinX() - 12 - Center.MidX()));
	// `PickCards.node`.
	const double Gap = 10, Pad = 8, Mark = 30;
	const double CardW = (Width - Gap - 2 * Pad) / 2;
	const double TextX = Pad + 6 + Mark + 10;
	const double TextW = CardW - TextX - 8;
	int32 Most = 1;
	for (const ac::Perk P : Shown->Perks)
	{
		Lines.Add(AcLeveling::Wrap(AcLeveling::Effect(P), 12, TextW, 3));
		Most = FMath::Max(Most, Lines.Last().Num());
	}
	CardH = FMath::Max(66.0, 38.0 + Most * 14);
	const double Header = 24;
	const FVector2D Origin(FMath::RoundHalfFromZero(Center.MidX() - Width / 2), FMath::RoundHalfFromZero(Cab.Top() + 12));
	PanelRect = FAcRect(Origin.X, Origin.Y, Width, CardH + Header + Pad + 4);
	for (int32 K = 0; K < Shown->Perks.Num(); ++K)
	{
		Frames.Emplace(FAcRect(Origin.X + Pad + K * (CardW + Gap), Origin.Y + Pad, CardW, CardH), Shown->Perks[K]);
	}
}

TOptional<ac::Perk> SAcPickCards::CardAt(const FVector2D P) const
{
	if (!Shown) return {};
	for (const TPair<FAcRect, ac::Perk>& F : Frames)
	{
		if (F.Key.Contains(P)) return F.Value;
	}
	return {};
}

TOptional<FAcRect> SAcPickCards::CardRect(const int32 Slot) const
{
	return Frames.IsValidIndex(Slot) ? TOptional<FAcRect>(Frames[Slot].Key) : TOptional<FAcRect>();
}

bool SAcPickCards::Click(const FVector2D ViewPoint)
{
	const TOptional<ac::Perk> P = CardAt(FVector2D(ViewPoint.X, Cab.Size.Y - ViewPoint.Y));
	if (!P) return false;
	OnPick.ExecuteIfBound(*P);
	return true;
}

FReply SAcPickCards::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Handled();
	const FVector2D L(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	Pressed = CardAt(FVector2D(L.X, Cab.Size.Y - L.Y));
	return FReply::Handled();
}

FReply SAcPickCards::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Handled();
	const FVector2D L(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	const TOptional<ac::Perk> Up = CardAt(FVector2D(L.X, Cab.Size.Y - L.Y));
	if (Pressed && Up && *Pressed == *Up) OnPick.ExecuteIfBound(*Up);
	Pressed.Reset();
	return FReply::Handled();
}

int32 SAcPickCards::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	if (!Shown || PanelRect.W <= 0) return Layer;
	const double ViewH = Cab.Size.Y;
	int32 L_ = Layer;
	FPaint P{&Out, &Geometry, L_, ViewH};
	const FAcPickOffer& O = *Shown;
	const FAcRect& Plate_ = PanelRect;
	const double Pad = 8, Mark = 30, TextX = Pad + 6 + Mark + 10;
	const FLinearColor Ink = White(0.95), Dim = White(0.72);

	// The plate (`cuts: (10, 10, 3, 3), leds: false, seed: 17`).
	const double PP = AcCabArt::PlatePad;
	P.Image(Plate.Get(FVector2D(Plate_.W, Plate_.H), 10, 10, 3, 3, 17, TEXT("AcPickCardsPlate")),
		FAcRect(Plate_.X - PP, Plate_.Y - PP, Plate_.W + 2 * PP, Plate_.H + 2 * PP));

	// The header: whose level, and on the right how many wait and the keys.
	const FString Title = FString::Printf(TEXT("%s  ·  LEVEL %d  ·  PICK ONE"), *AcPilotText::Title(O.Kind).ToUpper(), O.Level);
	P.Text(Title, 14, Gold(), FVector2D(Plate_.X + Pad + 4, Plate_.MaxY() - 19));
	TArray<FString> Right;
	if (O.Waiting > 1) Right.Add(FString::Printf(TEXT("%d PICKS WAITING"), O.Waiting));
	if (bKeys) Right.Add(FString::Printf(TEXT("⌥ CLICK OR %s / %s"), FAcPickOffer::Key(0), FAcPickOffer::Key(1)));
	if (!Right.IsEmpty())
	{
		// The count in amber when more than one waits, the rest dim; drawn
		// right to left from the plate's edge. Barlow has no "⌥": its strokes.
		const FString Sep = TEXT("   ·   ");
		const double Size = 12, SymW = 0.8 * Size;
		TArray<TPair<FString, FLinearColor>> Parts;
		for (int32 K = 0; K < Right.Num(); ++K)
		{
			const FLinearColor Col = O.Waiting > 1 && (K == 0 || !bKeys) ? QueueAmber() : Dim;
			Parts.Emplace((K > 0 ? Sep : FString()) + Right[K], Col);
		}
		auto Width = [&](const FString& S)
		{
			FString L, R;
			return S.Split(TEXT("⌥"), &L, &R) ? FAcConsolePaint::TextWidth(L, Size) + SymW + FAcConsolePaint::TextWidth(R, Size)
				: FAcConsolePaint::TextWidth(S, Size);
		};
		double Total = 0;
		for (const TPair<FString, FLinearColor>& Part : Parts) Total += Width(Part.Key);
		double X = Plate_.MaxX() - Pad - 4 - Total;
		const double Base = Plate_.MaxY() - 18;
		for (const TPair<FString, FLinearColor>& Part : Parts)
		{
			FString L, R;
			if (!Part.Key.Split(TEXT("⌥"), &L, &R))
			{
				P.Text(Part.Key, Size, Part.Value, FVector2D(X, Base), EAcHAlign::Left, false);
				X += FAcConsolePaint::TextWidth(Part.Key, Size);
				continue;
			}
			P.Text(L, Size, Part.Value, FVector2D(X, Base), EAcHAlign::Left, false);
			X += FAcConsolePaint::TextWidth(L, Size);
			const double Top = Base + 0.66 * Size, Bot = Base + 0.02 * Size;
			DrawLines(Out, Geometry, L_, ViewH, {FVector2D(X, Top), FVector2D(X + 0.3 * SymW, Top), FVector2D(X + 0.68 * SymW, Bot),
				FVector2D(X + SymW, Bot)}, Part.Value, 1.2);
			DrawLines(Out, Geometry, L_, ViewH, {FVector2D(X + 0.58 * SymW, Top), FVector2D(X + SymW, Top)}, Part.Value, 1.2);
			X += SymW;
			P.Text(R, Size, Part.Value, FVector2D(X, Base), EAcHAlign::Left, false);
			X += FAcConsolePaint::TextWidth(R, Size);
		}
	}

	for (int32 K = 0; K < Frames.Num(); ++K)
	{
		const FAcRect& R = Frames[K].Key;
		const ac::Perk Perk = Frames[K].Value;
		P.Box(R, 3, C(0.02, 0.06, 0.1, 0.95), Alpha(Accent, 0.85), 1.3, 0.8);
		// A lit edge down the left.
		P.Fill(FAcRect(R.MinX() + 3, R.MinY() + 4, 2, R.H - 8), Gold());
		const EAcPerkReach Reach = AcLeveling::Reach(Perk);
		const FAcRect Well(R.MinX() + 9, R.MaxY() - 8 - Mark, Mark, Mark);
		P.Box(Well, 2, FLinearColor(0, 0, 0, 0.45f), Alpha(Accent, 0.35));
		P.Mark(Reach, FVector2D(Well.MidX(), Well.MidY()), Mark - 6, Accent);
		P.Text(AcLeveling::Tag(Reach), 9, Alpha(Accent, 0.85), FVector2D(Well.MidX(), Well.MinY() - 11), EAcHAlign::Center);
		if (bKeys)
		{
			const FAcRect Key(R.MaxX() - 22, R.MaxY() - 20, 16, 16);
			P.Box(Key, 2, FLinearColor(0, 0, 0, 0.5f), Alpha(KeyYellow(), 0.8));
			P.Text(FAcPickOffer::Key(K), 12, KeyYellow(), FVector2D(Key.MidX(), Key.MinY() + 3), EAcHAlign::Center);
		}
		P.Text(AcLeveling::Title(Perk), 17, Ink, FVector2D(R.MinX() + TextX, R.MaxY() - 24));
		if (Lines.IsValidIndex(K))
		{
			for (int32 J = 0; J < Lines[K].Num(); ++J)
			{
				P.Text(Lines[K][J], 12, Dim, FVector2D(R.MinX() + TextX, R.MaxY() - 42 - J * 14));
			}
		}
	}
	return FMath::Max(L_, SCompoundWidget::OnPaint(Args, Geometry, Culling, Out, L_, Style, bParentEnabled));
}

// MARK: - The command map's PILOTS rows

void AcLevelPaint::Pilots(FAcCommandMapColumn& Col, const TArray<FAcDrivenRow>& Rows, TArray<TPair<FAcRect, ac::Perk>>& OutButtons)
{
	if (Rows.IsEmpty()) return;
	// CommandMap's palette.
	const FLinearColor Ink = C(0.92, 0.96, 1), Dim = White(0.62), Amber = C(1, 0.76, 0.3);
	const FLinearColor Dark = C(0.02, 0.04, 0.07, 0.9);
	FPaint P{Col.Out, Col.Geometry, Col.Layer, Col.ViewHeight};
	auto Box = [&P](const FAcRect& R, const FLinearColor& Fill, const FLinearColor& Stroke) { P.Box(R, 3, Fill, Stroke, 1, 0); };
	double& Y = Col.Y;
	const double Cx = Col.X, Right = Col.Right;

	Y -= 40;
	P.Text(TEXT("PILOTS"), 14, Gold(), FVector2D(Cx, Y));
	P.Text(TEXT("Levels reset every game"), 11, Dim, FVector2D(Right, Y), EAcHAlign::Right);
	for (int32 K = 0; K < Rows.Num(); ++K)
	{
		const FAcDrivenRow& Row = Rows[K];
		if (Y - 46 - (Row.Offer ? AcLeveling::PickRoom : 0) < Col.Bottom)
		{
			Y -= 18;
			P.Text(FString::Printf(TEXT("+%d more"), Rows.Num() - K), 12, Dim, FVector2D(Cx, Y));
			return;
		}
		Y -= 46;
		const FAcRect R(Cx, Y, 36, 36);
		Box(R, Dark, Alpha(Gold(), 0.8));
		P.Image(FAcIcons::Brush(FAcConsoleInfo::IconName((int32)Row.Kind)), FAcRect(R.MidX() - 15, R.MidY() - 15, 30, 30));
		const double Tx = R.MaxX() + 8;
		const FString Name = AcPilotText::Title(Row.Kind);
		P.Text(Name, 15, FLinearColor::White, FVector2D(Tx, Y + 23));
		P.Text(FString::Printf(TEXT("LV %d"), Row.Level.Level), 14, Gold(), FVector2D(Tx + FAcConsolePaint::TextWidth(Name, 15) + 8, Y + 23));
		P.Text(Row.Level.Text, 12, Gold(), FVector2D(Right, Y + 24), EAcHAlign::Right);
		const FAcRect Bar(Tx, Y + 14, Right - Tx, 3);
		Box(Bar, White(0.15), FLinearColor::Transparent);
		Box(FAcRect(Bar.X, Bar.Y, Bar.W * Row.Level.Fraction, Bar.H), Gold(), FLinearColor::Transparent);
		FString Picks;
		for (const ac::Perk Pk : Row.Picks) Picks += (Picks.IsEmpty() ? TEXT("") : TEXT(" · ")) + AcLeveling::Title(Pk);
		if (Picks.IsEmpty()) Picks = TEXT("No picks yet");
		const TArray<FString> PickLine = AcLeveling::Wrap(Picks, 12, Right - Tx, 1);
		P.Text(PickLine.IsEmpty() ? FString() : PickLine[0], 12, Row.Picks.IsEmpty() ? Dim : Ink, FVector2D(Tx, Y + 2));
		if (!Row.Offer) continue;
		const FAcPickOffer& O = *Row.Offer;
		Y -= 22;
		const FString Waiting = O.Waiting > 1 ? FString::Printf(TEXT("   ·   %d picks waiting"), O.Waiting) : FString();
		P.Text(FString::Printf(TEXT("Level %d: pick one"), O.Level) + Waiting, 12, Amber, FVector2D(Tx, Y + 6));
		for (const ac::Perk Pk : O.Perks)
		{
			Y -= 38;
			const FAcRect B(Tx, Y, Right - Tx, 34);
			Box(B, Alpha(Amber, 0.12), Alpha(Amber, 0.75));
			const EAcPerkReach Reach = AcLeveling::Reach(Pk);
			P.Mark(Reach, FVector2D(B.MinX() + 14, B.MidY() + 4), 16, Amber);
			P.Text(AcLeveling::Tag(Reach), 8, Amber, FVector2D(B.MinX() + 14, B.MinY() + 3), EAcHAlign::Center);
			P.Text(AcLeveling::Title(Pk), 14, FLinearColor::White, FVector2D(B.MinX() + 30, B.MinY() + 18));
			const TArray<FString> Eff = AcLeveling::Wrap(AcLeveling::Effect(Pk), 11, B.W - 38, 1);
			P.Text(Eff.IsEmpty() ? FString() : Eff[0], 11, Ink, FVector2D(B.MinX() + 30, B.MinY() + 5));
			FAcCommandMapHit H;
			H.Type = FAcCommandMapHit::EType::Pick;
			H.Perk = Pk;
			if (Col.Hits) Col.Hits->Emplace(B, H);
			OutButtons.Emplace(B, Pk);
		}
	}
}
