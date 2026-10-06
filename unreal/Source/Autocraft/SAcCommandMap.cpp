#include "SAcCommandMap.h"

#include "AcCabArt.h"
#include "AcConsoleInfo.h"
#include "AcConsolePaint.h"
#include "AcHudStyle.h"
#include "AcIcons.h"
#include "AcLog.h"
#include "SAcMinimap.h"

#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/Layout/SBox.h"

#include "Rules.h"

namespace
{
	FLinearColor C(double R, double G, double B, double A = 1) { return FAcHudStyle::Srgb(R, G, B, A); }
	FLinearColor White(double W, double A = 1) { return FAcHudStyle::Srgb(W, W, W, A); }
	FLinearColor Alpha(const FLinearColor& Col, double A) { return FLinearColor(Col.R, Col.G, Col.B, float(Col.A * A)); }

	// CommandMap's palette.
	const FLinearColor& Ink() { static const FLinearColor X = C(0.92, 0.96, 1); return X; }
	const FLinearColor& Dim() { static const FLinearColor X = White(0.62); return X; }
	const FLinearColor& Accent() { static const FLinearColor X = C(0.45, 0.75, 1); return X; }
	const FLinearColor& Amber() { static const FLinearColor X = C(1, 0.76, 0.3); return X; }
	const FLinearColor& Foe() { static const FLinearColor X = C(1, 0.38, 0.3); return X; }
	const FLinearColor& Friend() { static const FLinearColor X = C(0.4, 0.95, 0.5); return X; }
	const FLinearColor& ObjectiveInk() { static const FLinearColor X = C(1, 0.45, 0.35); return X; }
	FLinearColor Dark(double A) { return C(0.02, 0.04, 0.07, A); }

	/// A circle's outline from angle 0 counter-clockwise (y up), closed.
	TArray<FVector2D> Circle(FVector2D At, double R, int32 Segments = 48)
	{
		TArray<FVector2D> P;
		for (int32 K = 0; K <= Segments; ++K)
		{
			const double A = 2 * PI * K / Segments;
			P.Add(At + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		return P;
	}

	/// `CGPath.copy(dashingWithPhase: 0, lengths: [On, Off])` of a polyline.
	TArray<TArray<FVector2D>> Dashes(const TArray<FVector2D>& Path, double On, double Off)
	{
		TArray<TArray<FVector2D>> Out;
		bool bOn = true;
		double Left = On;
		TArray<FVector2D> Cur;
		Cur.Add(Path.IsEmpty() ? FVector2D::ZeroVector : Path[0]);
		for (int32 I = 0; I + 1 < Path.Num(); ++I)
		{
			FVector2D A = Path[I];
			const FVector2D B = Path[I + 1];
			double Len = FVector2D::Distance(A, B);
			while (Len > 0)
			{
				const double Step = FMath::Min(Len, Left);
				const FVector2D Mid = A + (B - A).GetSafeNormal() * Step;
				if (bOn) Cur.Add(Mid);
				Len -= Step;
				Left -= Step;
				A = Mid;
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
}

/// Drawing in Swift points (y up), one Slate layer per call, in call order.
struct SAcCommandMap::FPainter
{
	FSlateWindowElementList& Out;
	const FGeometry& Geometry;
	int32 Layer;
	double H;

	FAcConsolePaint P() const { return FAcConsolePaint(Out, Geometry, Layer, H); }
	FVector2f Slate(FVector2D Pt) const { return FVector2f(float(Pt.X), float(H - Pt.Y)); }

	void Fill(const FAcRect& R, const FLinearColor& Col)
	{
		FAcConsolePaint Q = P();
		Q.Fill(R, Col);
		Layer = Q.NextLayer();
	}
	/// `box`: a rounded rect (3), filled and stroked 1 wide.
	void Box(const FAcRect& R, const FLinearColor& FillCol, const FLinearColor& Stroke, bool bGlow)
	{
		FAcConsolePaint Q = P();
		Q.FillRounded(R, 3, FillCol);
		Q.Stroke(R, 3, Stroke, 1, bGlow ? 1 : 0);
		Layer = Q.NextLayer();
	}
	void RoundedRect(const FAcRect& R, double Radius, const FLinearColor& FillCol, const FLinearColor& Stroke, double Width)
	{
		FAcConsolePaint Q = P();
		Q.FillRounded(R, Radius, FillCol);
		Q.Stroke(R, Radius, Stroke, Width, 0);
		Layer = Q.NextLayer();
	}
	void Image(const FSlateBrush* Brush, const FAcRect& R, float Opacity = 1)
	{
		FAcConsolePaint Q = P();
		Q.Image(Brush, R, FLinearColor(1, 1, 1, Opacity));
		Layer = Q.NextLayer();
	}
	void Text(const FString& S, double Size, const FLinearColor& Col, FVector2D At, EAcHAlign Align = EAcHAlign::Left)
	{
		FAcConsolePaint Q = P();
		Q.Text(S, Size, Col, At, Align);
		Layer = Q.NextLayer();
	}
	void Lines(const TArray<FVector2D>& Pts, const FLinearColor& Col, double Width, double Glow = 0)
	{
		if (Pts.Num() < 2 || Col.A <= 0) return;
		TArray<FVector2f> S;
		for (const FVector2D& X : Pts) S.Add(Slate(X));
		if (Glow > 0)
		{
			FSlateDrawElement::MakeLines(Out, ++Layer, Geometry.ToPaintGeometry(), S, ESlateDrawEffect::None, Alpha(Col, 0.22), true,
				float(Width + 2 * Glow));
			FSlateDrawElement::MakeLines(Out, ++Layer, Geometry.ToPaintGeometry(), S, ESlateDrawEffect::None, Alpha(Col, 0.3), true,
				float(Width + Glow));
		}
		FSlateDrawElement::MakeLines(Out, ++Layer, Geometry.ToPaintGeometry(), S, ESlateDrawEffect::None, Col, true, float(Width));
	}
	void Disc(FVector2D At, double R, const FLinearColor& Col)
	{
		if (Col.A <= 0 || !FSlateApplication::IsInitialized()) return;
		const TArray<FVector2D> Ring = Circle(At, R);
		const FSlateRenderTransform& T = Geometry.GetAccumulatedRenderTransform();
		const FColor Packed = Col.ToFColor(true);
		// The white brush may sit in an atlas: sample the middle of its own region.
		const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FAcHudStyle::White());
		const FSlateShaderResourceProxy* Proxy = Handle.GetResourceProxy();
		const FVector2f Uv = Proxy ? Proxy->StartUV + Proxy->SizeUV * 0.5f : FVector2f(0.5f, 0.5f);
		TArray<FSlateVertex> V;
		TArray<SlateIndex> I;
		V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, Slate(At), Uv, Packed));
		for (const FVector2D& X : Ring) V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, Slate(X), Uv, Packed));
		for (int32 K = 1; K + 1 < V.Num(); ++K)
		{
			I.Add(0);
			I.Add(SlateIndex(K));
			I.Add(SlateIndex(K + 1));
		}
		FSlateDrawElement::MakeCustomVerts(Out, ++Layer, Handle, V, I, nullptr, 0, 0);
	}
	/// `SKShapeNode(circleOfRadius:)`: fill, then the outline (dashed: 5 on, 4 off).
	void Ring(FVector2D At, double R, const FLinearColor& FillCol, const FLinearColor& Stroke, double Width, double Glow, bool bDashed)
	{
		// A dashed path is open dashes: SpriteKit fills nothing inside it.
		if (!bDashed) Disc(At, R, FillCol);
		const TArray<FVector2D> Path = Circle(At, R);
		if (!bDashed)
		{
			Lines(Path, Stroke, Width, Glow);
			return;
		}
		for (const TArray<FVector2D>& D : Dashes(Path, 5, 4)) Lines(D, Stroke, Width, Glow);
	}
};

FVector2D SAcCommandMap::MapArea(const FVector2D Size)
{
	return FVector2D(Size.X - 3 * Margin - Column - 24, Size.Y - 2 * Margin - Header - 24);
}

void SAcCommandMap::Construct(const FArguments& InArgs)
{
	OnHit = InArgs._OnHit;
	SetCanTick(true);
	ChildSlot
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(TAttribute<FMargin>::CreateLambda([this]()
		{
			if (!Map || View.X <= 0) return FMargin(0);
			const FAcRect R = MapRect(View);
			return FMargin(float(R.X), float(View.Y - R.MaxY()), 0, 0);
		}))
		[
			SNew(SBox)
			.Visibility(EVisibility::HitTestInvisible)
			.WidthOverride(TAttribute<FOptionalSize>::CreateLambda([this]() { return Map ? FOptionalSize(float(Map->Size().X)) : FOptionalSize(0); }))
			.HeightOverride(TAttribute<FOptionalSize>::CreateLambda([this]() { return Map ? FOptionalSize(float(Map->Size().Y)) : FOptionalSize(0); }))
		];
}

void SAcCommandMap::SetMinimap(const TSharedPtr<SAcMinimap>& Minimap)
{
	Map = Minimap;
	// The box keeps its size overrides; only its content changes.
	TSharedRef<SWidget> Box = ChildSlot.GetWidget();
	if (Box->GetType() == FName(TEXT("SBox")))
	{
		StaticCastSharedRef<SBox>(Box)->SetContent(Minimap ? Minimap.ToSharedRef() : SNullWidget::NullWidget);
	}
	if (Minimap) Minimap->SetVisibility(EVisibility::HitTestInvisible);
}

void SAcCommandMap::Show(const FAcCommandMapInfo& Info)
{
	Shown = Info;
}

FAcRect SAcCommandMap::Panel(const FVector2D Size) const
{
	return FAcRect(Margin, Margin, Size.X - 2 * Margin, Size.Y - 2 * Margin);
}

FAcRect SAcCommandMap::MapRect(const FVector2D Size) const
{
	const FAcRect P = Panel(Size);
	const FVector2D Area = MapArea(Size);
	const FVector2D M = Map ? Map->Size() : Area;
	return FAcRect(P.MinX() + 12 + (Area.X - M.X) / 2, P.MinY() + 12 + (Area.Y - M.Y) / 2, M.X, M.Y);
}

FVector2D SAcCommandMap::MapPoint(const ac::Vec2 G) const
{
	if (!Map) return FVector2D::ZeroVector;
	const FVector2D P = Map->Point(G);
	return FVector2D(P.X, Map->Size().Y - P.Y);
}

FVector2D SAcCommandMap::ViewPointOf(const ac::Vec2 G) const
{
	const FAcRect R = MapRect(View);
	const FVector2D P = MapPoint(G);
	return FVector2D(R.X + P.X, View.Y - (R.Y + P.Y));
}

void SAcCommandMap::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
	View = FVector2D(Geometry.GetLocalSize());
}

void SAcCommandMap::Prebake(const FVector2D ViewSize) const
{
	if (ViewSize.X <= 2 * Margin || ViewSize.Y <= 2 * Margin) return;
	const FAcRect P = Panel(ViewSize);
	EnsurePlate(FVector2D(P.W, P.H));
}

void SAcCommandMap::EnsurePlate(const FVector2D PanelSize) const
{
	if (PlateBrush && PlateFor == PanelSize) return;
	// `Chrome.plateNode(panel, cuts: .all(22), seed: 31)`, baked on a worker
	// (Core Graphics draws into its own bitmap there); until it is done the
	// last plate stays (stretched), or none.
	if (!Baking.IsValid() || BakingFor != PanelSize)
	{
		BakingFor = PanelSize;
		Baking = Async(EAsyncExecution::ThreadPool, [PanelSize]()
		{
			const double Start = FPlatformTime::Seconds();
			FAcArtImage Art = AcCabArt::Plate(PanelSize, FAcCuts{22, 22, 22, 22}, true, 31);
			UE_LOG(LogAutocraft, Log, TEXT("command map: plate %.0fx%.0f baked in %.0f ms (worker)"), PanelSize.X, PanelSize.Y,
				(FPlatformTime::Seconds() - Start) * 1000.0);
			return Art;
		});
	}
	if (!Baking.IsReady()) return;
	const double Start = FPlatformTime::Seconds();
	const FAcArtImage Art = Baking.Consume();
	PlateFor = PanelSize;
	PlateTexture.Reset(AcCabArt::ToTexture(Art, TEXT("AcCommandMapPlate")));
	PlateBrush = MakeShared<FSlateBrush>();
	PlateBrush->DrawAs = ESlateBrushDrawType::Image;
	PlateBrush->ImageSize = Art.Points;
	if (PlateTexture) PlateBrush->SetResourceObject(PlateTexture.Get());
	else PlateBrush->TintColor = FSlateColor(FLinearColor::Transparent);
	UE_LOG(LogAutocraft, Log, TEXT("command map: plate texture made in %.1f ms"), (FPlatformTime::Seconds() - Start) * 1000.0);
}

int32 SAcCommandMap::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2D Size(Geometry.GetLocalSize());
	if (Size.X <= 0 || Size.Y <= 0) return Layer;
	const FAcRect P = Panel(Size);

	// Under the map: the backdrop, the panel's fill and its plate.
	FPainter Under{Out, Geometry, Layer, Size.Y};
	Under.Fill(FAcRect(0, 0, Size.X, Size.Y), FLinearColor(0, 0, 0, 0.6f));
	Under.Fill(P.Inset(8, 8), C(0.02, 0.04, 0.07));
	EnsurePlate(FVector2D(P.W, P.H));
	const double Pad = AcCabArt::PlatePad;
	if (PlateBrush) Under.Image(PlateBrush.Get(), FAcRect(P.X - Pad, P.Y - Pad, P.W + 2 * Pad, P.H + 2 * Pad));

	// The map itself (the child).
	const int32 AfterMap = SCompoundWidget::OnPaint(Args, Geometry, Culling, Out, Under.Layer + 1, Style, bParentEnabled);

	FPainter Over{Out, Geometry, FMath::Max(AfterMap, Under.Layer) + 1, Size.Y};
	TArray<TPair<FAcRect, FAcCommandMapHit>> NewHits;
	Build(Size, &Over, NewHits);
	Hits = MoveTemp(NewHits);
	return Over.Layer;
}

void SAcCommandMap::Build(const FVector2D Size, FPainter* Pt, TArray<TPair<FAcRect, FAcCommandMapHit>>& OutHits) const
{
	const FAcRect P = Panel(Size);
	const FAcRect M = MapRect(Size);
	const FAcCommandMapInfo& Info = Shown;
	auto Text = [Pt](const FString& S, double Sz, const FLinearColor& Col, FVector2D At, EAcHAlign A = EAcHAlign::Left)
	{
		if (Pt) Pt->Text(S, Sz, Col, At, A);
	};
	auto Box = [Pt](const FAcRect& R, const FLinearColor& F, const FLinearColor& S, bool bGlow)
	{
		if (Pt) Pt->Box(R, F, S, bGlow);
	};
	auto Icon = [Pt](const FString& Name, const FAcRect& R, float Opacity = 1)
	{
		if (Pt) Pt->Image(FAcIcons::Brush(Name), R, Opacity);
	};
	/// A label's frame (SpriteKit's, about: descender to ascender).
	auto Frame = [](const FString& S, double Sz, FVector2D At)
	{
		const double W = FAcConsolePaint::TextWidth(S, Sz);
		return FAcRect(At.X, At.Y - 0.22 * Sz, W, 1.02 * Sz);
	};

	// Static: the title and the hint.
	Text(TEXT("COMMAND MAP"), 24, Ink(), FVector2D(P.MinX() + 16, P.MaxY() - 38));
	Text(TEXT("Click the map for orders  ·  M or Esc closes"), 13, Dim(), FVector2D(P.MaxX() - 16, P.MaxY() - 34), EAcHAlign::Right);

	const double Cell = Map && Map->Bounds().width() > 0 ? Map->Size().X / Map->Bounds().width() : 1;
	const bool bMap = Map.IsValid();

	// Each building's icon over its mark, framed in its side's colour (faded while it goes up).
	if (Pt && bMap)
	{
		for (const FAcCommandMapInfo::FBuilding& B : Info.Buildings)
		{
			const FVector2D Q = FVector2D(M.X, M.Y) + MapPoint(B.At);
			const double Span = 2 * ac::Rules::radius(B.Kind) * Cell * 1.4;
			const double Side = FMath::Min(FMath::Max(Span, 26.0), 46.0);
			const FLinearColor Col = B.bOurs ? Friend() : Foe();
			const FAcRect R(Q.X - Side / 2, Q.Y - Side / 2, Side, Side);
			Pt->RoundedRect(R, 3, Dark(0.85), Alpha(Col, B.bComplete ? 0.9 : 0.45), 1.5);
		}
		for (const FAcCommandMapInfo::FBuilding& B : Info.Buildings)
		{
			const FVector2D Q = FVector2D(M.X, M.Y) + MapPoint(B.At);
			const double Span = 2 * ac::Rules::radius(B.Kind) * Cell * 1.4;
			const double Side = FMath::Min(FMath::Max(Span, 26.0), 46.0);
			const double S = Side - 3;
			Icon(FAcConsoleInfo::StructureIconName((int32)B.Kind), FAcRect(Q.X - S / 2, Q.Y - S / 2, S, S), B.bComplete ? 1.0f : 0.5f);
		}
	}

	// Bright brackets on the map's corners.
	if (Pt)
	{
		const double L = 26;
		const FAcRect B = M.Inset(-10, -10);
		const FLinearColor Cyan = FAcHudStyle::Cyan();
		const struct { FVector2D C; double Dx, Dy; } Corners[] = {{FVector2D(B.MinX(), B.MinY()), 1, 1}, {FVector2D(B.MaxX(), B.MinY()), -1, 1},
			{FVector2D(B.MinX(), B.MaxY()), 1, -1}, {FVector2D(B.MaxX(), B.MaxY()), -1, -1}};
		for (const auto& K : Corners)
		{
			Pt->Lines({FVector2D(K.C.X + K.Dx * L, K.C.Y), K.C, FVector2D(K.C.X, K.C.Y + K.Dy * L)}, Cyan, 2.2, 2);
		}
	}

	// The overlay over the map: base sites, then the objectives.
	if (Pt && bMap)
	{
		const FVector2D O(M.X, M.Y);
		for (const FAcCommandMapInfo::FSite& Site : Info.Sites)
		{
			const FVector2D Q = O + MapPoint(Site.At);
			const double R = FMath::Max(3.2 * Cell, 14.0);
			FLinearColor Col;
			FString Tag;
			bool bDashed = false;
			if (Site.Ours.IsSet())
			{
				Col = *Site.Ours ? Friend() : Foe();
				Tag = !Site.Holder.IsEmpty() ? Site.Holder : *Site.Ours ? TEXT("Yours") : TEXT("Enemy");
			}
			else
			{
				Col = Site.bQueued ? Amber() : White(0.85, 0.9);
				Tag = Site.bQueued ? TEXT("Expanding") : Site.bNext ? TEXT("AI's next") : Site.bContested ? TEXT("Contested") : TEXT("Free");
				bDashed = true;
			}
			FLinearColor Stroke = Col;
			double Glow = 0;
			if (Site.bNext && !Site.Ours.IsSet() && !Site.bQueued)
			{
				Glow = 2;
				Stroke = Amber();
			}
			Pt->Ring(Q, R, Alpha(Col, Site.bQueued ? 0.25 : 0.08), Stroke, 2, Glow, bDashed);
			Pt->Text(FString::Printf(TEXT("Site %lld"), (long long)(Site.Index + 1)), 13, FLinearColor::White, FVector2D(Q.X, Q.Y + R + 4),
				EAcHAlign::Center);
			Pt->Text(Tag, 11, Site.bNext && !Site.Ours.IsSet() ? Amber() : Col, FVector2D(Q.X, Q.Y - R - 14), EAcHAlign::Center);
		}

		// Objectives, with a line from their squad; beside a site's ring when on one, so the two labels part.
		const double SiteRing = 8 * Cell;
		struct FLabel { FString Title, Status; FVector2D At; FLinearColor Col; };
		TArray<FLabel> Labels;
		for (const FAcCommandMapInfo::FMarker& Mk : Info.Markers)
		{
			const FVector2D Q = O + MapPoint(Mk.At);
			const FLinearColor Col = FAcCommandMapLogic::BeaconColor(Mk.Kind);
			const bool bOnSite = Info.Sites.ContainsByPredicate([&](const FAcCommandMapInfo::FSite& S) { return ac::distance(S.At, Mk.At) < 4; });
			const double Tx = Q.X + (bOnSite ? SiteRing + 8 : 20);
			if (Mk.Squad)
			{
				const FVector2D Sq = O + MapPoint(*Mk.Squad);
				for (const TArray<FVector2D>& D : Dashes({Sq, Q}, 6, 4)) Pt->Lines(D, Alpha(Col, 0.85), 2);
				Pt->Ring(Sq, 5, Col, FLinearColor::White, 1, 0, false);
			}
			Pt->Ring(Q, 15, Dark(0.85), Col, 2.5, 1.5, false);
			Icon(FAcCommandMapLogic::ObjectiveIcon(Mk.Kind), FAcRect(Q.X - 12, Q.Y - 12, 24, 24));
			Labels.Add({Mk.Title, Mk.Status, FVector2D(Tx, Q.Y), Col});
		}
		// The labels on dark plates, over everything round them (zPosition 1 and 2).
		for (const FLabel& Lb : Labels)
		{
			const FAcRect A = Frame(Lb.Title, 15, Lb.At), B = Frame(Lb.Status, 12, Lb.At - FVector2D(0, 14));
			const double X0 = FMath::Min(A.MinX(), B.MinX()), Y0 = FMath::Min(A.MinY(), B.MinY());
			const double X1 = FMath::Max(A.MaxX(), B.MaxX()), Y1 = FMath::Max(A.MaxY(), B.MaxY());
			Pt->RoundedRect(FAcRect(X0, Y0, X1 - X0, Y1 - Y0).Inset(-5, -3), 3, Dark(0.8), Alpha(Lb.Col, 0.5), 1);
		}
		for (const FLabel& Lb : Labels)
		{
			Pt->Text(Lb.Title, 15, Lb.Col, Lb.At);
			Pt->Text(Lb.Status, 12, Ink(), Lb.At - FVector2D(0, 14));
		}
	}

	// Stance, along the top.
	Text(TEXT("STANCE"), 13, Dim(), FVector2D(P.MinX() + 200, P.MaxY() - 36));
	const ac::Stance Stances[] = {ac::Stance::auto_, ac::Stance::aggressive, ac::Stance::hold, ac::Stance::allIn};
	for (int32 K = 0; K < 4; ++K)
	{
		const FAcRect R(P.MinX() + 262 + K * 112, P.MaxY() - 46, 106, 28);
		const bool bOn = Info.Stance == Stances[K];
		Box(R, bOn ? Alpha(Accent(), 0.3) : White(0.08, 0.9), bOn ? Accent() : White(0.3), bOn);
		Text(FString::Printf(TEXT("%d  %s"), K + 1, *FAcCommandMapLogic::StanceTitle(Stances[K])), 15, bOn ? FLinearColor::White : Dim(),
			FVector2D(R.MidX(), R.MinY() + 8), EAcHAlign::Center);
		FAcCommandMapHit H;
		H.Type = FAcCommandMapHit::EType::Stance;
		H.Stance = Stances[K];
		OutHits.Emplace(R, H);
	}
	Text(FAcCommandMapLogic::StanceLine(Info.Stance), 12, Accent(), FVector2D(P.MinX() + 262, P.MaxY() - 62));
	Text(Info.Economy, 13, Ink(), FVector2D(P.MaxX() - 16, P.MaxY() - 56), EAcHAlign::Right);

	// The objectives, then the queue, down the right, each with a button to
	// take it off; how squads work under them when there is room.
	const double Cx = P.MaxX() - Column - 12, Bottom = P.MinY() + 20;
	double Y = P.MaxY() - Header - 30;
	auto Entry = [&](const FString& IconName, const FLinearColor& Tint)
	{
		Y -= 46;
		const FAcRect R(Cx, Y, 36, 36);
		Box(R, Dark(0.9), Tint, false);
		Icon(IconName, FAcRect(R.MidX() - 15, R.MidY() - 15, 30, 30));
		return R;
	};
	auto Cancel = [&](int64 Id)
	{
		const FAcRect X(P.MaxX() - 40, Y + 7, 22, 22);
		Box(X, C(0.15, 0.03, 0.03, 0.9), C(0.8, 0.3, 0.25), false);
		// "✕" (13 pt), drawn as two strokes: Barlow has no such glyph.
		if (Pt)
		{
			const FVector2D Mid(X.MidX(), X.MidY());
			const double A = 4.2;
			Pt->Lines({Mid + FVector2D(-A, -A), Mid + FVector2D(A, A)}, FLinearColor::White, 1.6);
			Pt->Lines({Mid + FVector2D(-A, A), Mid + FVector2D(A, -A)}, FLinearColor::White, 1.6);
		}
		FAcCommandMapHit H;
		H.Type = FAcCommandMapHit::EType::Cancel;
		H.Id = Id;
		OutHits.Emplace(X, H);
	};
	Text(TEXT("OBJECTIVES"), 14, ObjectiveInk(), FVector2D(Cx, Y));
	if (Info.Markers.IsEmpty())
	{
		Y -= 22;
		Text(TEXT("None: the AI plays its own game."), 12, Dim(), FVector2D(Cx, Y));
		Y -= 18;
		Text(TEXT("Click the map to attack, defend a base or expand."), 12, Dim(), FVector2D(Cx, Y));
	}
	for (const FAcCommandMapInfo::FMarker& Mk : Info.Markers)
	{
		const FLinearColor Col = FAcCommandMapLogic::BeaconColor(Mk.Kind);
		const FAcRect I = Entry(FAcCommandMapLogic::ObjectiveIcon(Mk.Kind), Col);
		Text(Mk.Title, 15, FLinearColor::White, FVector2D(I.MaxX() + 8, Y + 20));
		Text(Mk.Status, 12, Col, FVector2D(I.MaxX() + 8, Y + 5));
		Cancel(Mk.Id);
	}
	Y -= 40;
	Text(TEXT("NEXT"), 14, Amber(), FVector2D(Cx, Y));
	if (Info.Queue.IsEmpty())
	{
		Y -= 22;
		Text(TEXT("Queue empty. Press an amber card button"), 12, Dim(), FVector2D(Cx, Y));
		Y -= 18;
		Text(TEXT("to queue it for the AI."), 12, Dim(), FVector2D(Cx, Y));
	}
	// The pilots come after the queue: it leaves them their room.
	const int32 Rows = FMath::Min(Info.Queue.Num(), FMath::Max(0, int32((Y - Bottom - 18 - Info.PilotsRoom) / 46)));
	for (int32 K = 0; K < Rows; ++K)
	{
		const FAcCommandMapInfo::FRequest& R = Info.Queue[K];
		const FAcRect I = Entry(R.Icon, Amber());
		const double Tx = I.MaxX() + 8;
		Text(R.Title + (R.Count.IsEmpty() ? FString() : TEXT("  ") + R.Count), 15, FLinearColor::White, FVector2D(Tx, Y + 23));
		Text(R.Status, 12, R.Funded >= 1 ? Dim() : Amber(), FVector2D(Tx, Y + 9));
		// How much of its cost is in the bank.
		const FAcRect Bar(Tx, Y + 1, P.MaxX() - 52 - Tx, 4);
		Box(Bar, White(0.15), FLinearColor::Transparent, false);
		if (R.Funded > 0) Box(FAcRect(Bar.X, Bar.Y, Bar.W * R.Funded, Bar.H), R.Funded >= 1 ? Accent() : Amber(), FLinearColor::Transparent, false);
		Cancel(R.Id);
	}
	if (Info.Queue.Num() > Rows)
	{
		Y -= 18;
		Text(FString::Printf(TEXT("+%d more"), Info.Queue.Num() - Rows), 12, Dim(), FVector2D(Cx, Y));
	}
	// PILOTS (chunk E9) take `PilotsRoom` here, drawn by its painter.
	if (PilotsPainter && Info.PilotsRoom > 0)
	{
		FAcCommandMapColumn Col;
		Col.Out = Pt ? &Pt->Out : nullptr;
		Col.Geometry = Pt ? &Pt->Geometry : nullptr;
		Col.Layer = Pt ? Pt->Layer : 0;
		Col.ViewHeight = Size.Y;
		Col.X = Cx;
		Col.Right = P.MaxX() - 16;
		Col.Bottom = Bottom;
		Col.Y = Y;
		Col.Hits = &OutHits;
		PilotsPainter(Col);
		Y = Col.Y;
		if (Pt) Pt->Layer = Col.Layer;
	}
	else
	{
		Y -= Info.PilotsRoom;
	}
	static const TCHAR* const Squads[] = {TEXT("Squads: the AI splits the army between"), TEXT("the objectives as each needs, gathers"),
		TEXT("a squad at the rally until it is strong"), TEXT("enough, and pulls back to regroup when"),
		TEXT("outfought (not All in). A taken point is"), TEXT("held. A real attack on a base comes"),
		TEXT("first (not All in). Bastions to man get"), TEXT("Rangers before anything else.")};
	if (Y - 40 - 7 * 16 >= Bottom)
	{
		Y -= 40;
		for (const TCHAR* Line : Squads)
		{
			Text(Line, 12, Dim(), FVector2D(Cx, Y));
			Y -= 16;
		}
	}

	// The order menu.
	if (Info.Menu && bMap)
	{
		const FAcMapMenu& Menu = *Info.Menu;
		const FVector2D Q = MapPoint(Menu.At);
		const double W = 210, H = 30;
		FVector2D Top(M.MinX() + Q.X + 12, M.MinY() + Q.Y + 8);
		const double Height = Menu.Items.Num() * H + 8;
		if (Top.X + W > P.MaxX() - 8) Top.X = M.MinX() + Q.X - W - 12;
		if (Top.Y - Height < P.MinY() + 8) Top.Y = P.MinY() + 8 + Height;
		if (Pt) Pt->Ring(FVector2D(M.MinX() + Q.X, M.MinY() + Q.Y), 6, FLinearColor::White, Accent(), 2, 0, false);
		Box(FAcRect(Top.X, Top.Y - Height, W, Height), C(0.02, 0.05, 0.09, 0.97), Accent(), true);
		for (int32 K = 0; K < Menu.Items.Num(); ++K)
		{
			const FAcMapOrder& Item = Menu.Items[K];
			const FAcRect R(Top.X + 4, Top.Y - 4 - (K + 1) * H, W - 8, H - 2);
			FLinearColor Col;
			switch (Item.Type)
			{
			case FAcMapOrder::EType::Attack: Col = FAcCommandMapLogic::BeaconColor(ac::Objective::Kind::attack); break;
			case FAcMapOrder::EType::Defend: Col = FAcCommandMapLogic::BeaconColor(ac::Objective::Kind::defend); break;
			case FAcMapOrder::EType::Man: Col = FAcCommandMapLogic::BeaconColor(ac::Objective::Kind::man); break;
			case FAcMapOrder::EType::Expand: Col = Amber(); break;
			case FAcMapOrder::EType::Cancel: Col = Dim(); break;
			}
			Box(R, Alpha(Col, 0.16), Alpha(Col, 0.7), false);
			Text(Item.Title(), 15, FLinearColor::White, FVector2D(R.MinX() + 10, R.MinY() + 8));
			FAcCommandMapHit Hh;
			Hh.Type = FAcCommandMapHit::EType::Order;
			Hh.Order = Item;
			OutHits.Emplace(R, Hh);
		}
	}
}

TOptional<FAcCommandMapHit> SAcCommandMap::Hit(const FVector2D P) const
{
	if (View.X <= 0) return {};
	TArray<TPair<FAcRect, FAcCommandMapHit>> Now;
	Build(View, nullptr, Now);
	for (const TPair<FAcRect, FAcCommandMapHit>& H : Now)
	{
		if (H.Key.Contains(P)) return H.Value;
	}
	FAcCommandMapHit H;
	const FAcRect M = MapRect(View);
	if (Map && M.Contains(P))
	{
		H.Type = FAcCommandMapHit::EType::Ground;
		const FVector2D Own(P.X - M.X, M.H - (P.Y - M.Y));
		H.Ground = Map->Ground(Own);
		return H;
	}
	H.Type = Panel(View).Contains(P) ? FAcCommandMapHit::EType::Inside : FAcCommandMapHit::EType::Close;
	return H;
}

FReply SAcCommandMap::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	// Every button is the map's: nothing reaches the world under it.
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton) bPressed = true;
	return FReply::Handled();
}

FReply SAcCommandMap::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton || !bPressed) return FReply::Handled();
	bPressed = false;
	View = FVector2D(Geometry.GetLocalSize());
	const FVector2D Local(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	if (const TOptional<FAcCommandMapHit> H = Hit(ToSwift(Local))) OnHit.ExecuteIfBound(*H);
	return FReply::Handled();
}
