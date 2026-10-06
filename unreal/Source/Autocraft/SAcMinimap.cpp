#include "SAcMinimap.h"

#include "AcConsolePaint.h"
#include "AcHudStyle.h"
#include "AcMinimapBake.h"

#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateBrush.h"

#include "Rules.h"
#include "Types.h"

namespace
{
	FLinearColor Rgb(double R, double G, double B, double A = 1) { return FAcHudStyle::Srgb(R, G, B, A); }
	FLinearColor White(double A) { return FLinearColor(1, 1, 1, (float)A); }
	FLinearColor Faded(const FLinearColor& C, float A) { return FLinearColor(C.R, C.G, C.B, C.A * A); }

	/// `Minimap.sightHalfAngle`: about half the first-person view's width.
	constexpr double SightHalfAngle = 0.77;
}

void SAcMinimap::Construct(const FArguments& InArgs)
{
	S = InArgs._Scale;
	bShowFootprint = InArgs._ShowFootprint;
	OnGround = InArgs._OnGround;
	SetVisibility(EVisibility::Visible);
	TerrainBrush = MakeShared<FSlateBrush>();
	FogBrush = MakeShared<FSlateBrush>();
	MakeImage(DiscImage, AcMinimapBake::Disc(), TEXT("AcMinimapDisc"));
	MakeImage(DiamondImage, AcMinimapBake::Diamond(), TEXT("AcMinimapDiamond"));
}

void SAcMinimap::MakeImage(FImage& Image, const FAcArtImage& Art, const TCHAR* Name)
{
	Image.Texture.Reset(AcCabArt::ToTexture(Art, Name));
	Image.Brush = MakeShared<FSlateBrush>();
	Image.Brush->DrawAs = ESlateBrushDrawType::Image;
	Image.Brush->ImageSize = Art.Points;
	if (Image.Texture) Image.Brush->SetResourceObject(Image.Texture.Get());
	else Image.Brush->TintColor = FSlateColor(FLinearColor::Transparent);
}

void SAcMinimap::SetBrushTexture(FSlateBrush& Brush, UTexture2D* Texture)
{
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.SetResourceObject(Texture);
	if (Texture) Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
}

FLinearColor SAcMinimap::Team(const int64 Player)
{
	// Player colours, lifted so they read on the dark terrain.
	return FAcHudStyle::Blend(FAcHudStyle::PlayerColor(Player), 0.35, FLinearColor::White);
}

// MARK: - Feeding

void SAcMinimap::SetMap(const ac::GroundRect& InBounds, const FVector2D InSize, UTexture2D* Terrain)
{
	GroundBounds = InBounds;
	MapSize = InSize;
	TerrainTexture = Terrain;
	SetBrushTexture(*TerrainBrush, Terrain);
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SAcMinimap::SetMarks(const ac::GameState& Shown, const ac::MapDefinition& Map)
{
	Marks.Reset();
	const double CellW = MapSize.X / FMath::Max(GroundBounds.width(), 1e-6);
	// Buildings: squares as wide as their footprint, at least 3 points.
	for (const ac::Structure& St : Shown.structures)
	{
		FAcMinimapMark M;
		M.Shape = FAcMinimapMark::EShape::Square;
		M.At = Point(St.position);
		M.Radius = FMath::Max(2 * ac::Rules::radius(St.kind) * CellW, 3 * S) / 2;
		M.Fill = Team(St.owner);
		M.Stroke = White(0.8);
		M.Line = 1 * S;
		M.Alpha = St.complete() ? 1.0f : 0.45f;
		Marks.Add(M);
	}
	// Ore and wells, over the buildings: MH wells in liquid silver, ore in opal violet.
	auto Dot = [this](ac::Vec2 At, double R, const FLinearColor& Color)
	{
		FAcMinimapMark M;
		M.At = Point(At);
		M.Radius = R * S;
		M.Fill = Color;
		Marks.Add(M);
	};
	if (Shown.wells)
	{
		for (const ac::Well& W : *Shown.wells) Dot(W.position, 2.2, Rgb(0.84, 0.9, 0.98));
	}
	else
	{
		for (const ac::BaseSite& B : Map.bases)
		{
			for (const ac::Vec2& W : B.wells) Dot(W, 2.2, Rgb(0.84, 0.9, 0.98));
		}
	}
	for (const ac::OreDeposit& P : Shown.patches)
	{
		if (P.remaining > 0) Dot(P.position, 1.6, Rgb(0.74, 0.56, 1));
	}
	// Units, sized by bulk: infantry small, vehicles larger, flyers diamonds
	// over the ground units. Aboard a Dropship or in a Bastion: not on the map.
	for (const ac::Unit& U : Shown.units)
	{
		if (U.task == ac::Unit::Task::aboard || U.task == ac::Unit::Task::inBastion) continue;
		FAcMinimapMark M;
		M.At = Point(U.position);
		M.Fill = Team(U.owner);
		M.Stroke = White(0.9);
		M.Line = 0.8 * S;
		switch (U.kind)
		{
		case ac::UnitKind::dropship:
		case ac::UnitKind::kestrel:
		case ac::UnitKind::peregrine:
			M.Shape = FAcMinimapMark::EShape::Diamond;
			M.Radius = (U.kind == ac::UnitKind::dropship ? 2.6 : U.kind == ac::UnitKind::peregrine ? 1.8 : 2.2) * S;
			M.Z = 2;
			break;
		case ac::UnitKind::ranger: M.Radius = 1.4 * S; M.Z = 1; break;
		case ac::UnitKind::longbow: M.Radius = 2.5 * S; M.Z = 1; break;
		case ac::UnitKind::atlas: M.Radius = 3.2 * S; M.Z = 1; break;
		case ac::UnitKind::scorpion: M.Radius = 1.6 * S; M.Z = 1; break;
		case ac::UnitKind::firefly:
		case ac::UnitKind::juggernaut:
		case ac::UnitKind::hailstorm: M.Radius = 2.0 * S; M.Z = 1; break;
		default: M.Radius = 1.8 * S; M.Z = 1; break;
		}
		Marks.Add(M);
	}
	// SpriteKit draws by global z, then in tree order.
	Marks.StableSort([](const FAcMinimapMark& A, const FAcMinimapMark& B) { return A.Z < B.Z; });
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAcMinimap::SetFootprint(const std::vector<ac::Vec2>& Quad)
{
	Outline.Reset();
	if (!bShowFootprint || Quad.size() < 2) return;
	for (const ac::Vec2& G : Quad) Outline.Add(FVector2f(Point(G)));
	const FVector2f First = Outline[0];
	Outline.Add(First);
}

void SAcMinimap::SetFog(UTexture2D* Fog, const ac::Vec2 Origin, const ac::Vec2 InFogSize)
{
	if (FogTexture.Get() != Fog) SetBrushTexture(*FogBrush, Fog);
	FogTexture = Fog;
	FogOrigin = Origin;
	FogSize = InFogSize;
}

void SAcMinimap::SetSight(TOptional<TPair<ac::Vec2, double>> InSight)
{
	Sight = MoveTemp(InSight);
	if (Sight && !SightImage.Brush)
	{
		const double Reach = 0.24 * FMath::Min(MapSize.X, MapSize.Y);
		MakeImage(SightImage, AcMinimapBake::SightFan(Reach, SightHalfAngle, 1 * S, 1.4 * S), TEXT("AcMinimapSight"));
	}
}

// MARK: - Coordinates

FVector2D SAcMinimap::Point(const ac::Vec2 G) const
{
	// North (far, min y) up.
	return FVector2D((G.x - GroundBounds.minX) / FMath::Max(GroundBounds.width(), 1e-6) * MapSize.X,
		(G.y - GroundBounds.minZ) / FMath::Max(GroundBounds.depth(), 1e-6) * MapSize.Y);
}

ac::Vec2 SAcMinimap::Ground(const FVector2D P) const
{
	return ac::Vec2(GroundBounds.minX + P.X / FMath::Max(MapSize.X, 1e-6) * GroundBounds.width(),
		GroundBounds.minZ + P.Y / FMath::Max(MapSize.Y, 1e-6) * GroundBounds.depth());
}

FVector2D SAcMinimap::ToOwn(const FGeometry& Geometry, const FVector2D Local) const
{
	const FVector2D L = FVector2D(Geometry.GetLocalSize());
	return FVector2D(Local.X * MapSize.X / FMath::Max(L.X, 1e-6), Local.Y * MapSize.Y / FMath::Max(L.Y, 1e-6));
}

TOptional<ac::Vec2> SAcMinimap::GroundAt(const FGeometry& Geometry, const FVector2D Local, const double Pad) const
{
	const FVector2D P = ToOwn(Geometry, Local);
	if (P.X < -Pad || P.Y < -Pad || P.X > MapSize.X + Pad || P.Y > MapSize.Y + Pad) return {};
	return Ground(FVector2D(FMath::Clamp(P.X, 0.0, MapSize.X), FMath::Clamp(P.Y, 0.0, MapSize.Y)));
}

bool SAcMinimap::Press(const FGeometry& Geometry, const FVector2D Local)
{
	const TOptional<ac::Vec2> G = GroundAt(Geometry, Local);
	if (!G) return false;
	OnGround.ExecuteIfBound(*G);
	return true;
}

// MARK: - Input (GameController.jumpFromMinimap, GameView's onMinimap drag)

FReply SAcMinimap::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	if (!Press(Geometry, Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()))) return FReply::Unhandled();
	bPressed = true;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SAcMinimap::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	// (The console holds the capture for it: the minimap is drawn warped, off
	// screen, and gets its events from SAcConsole, so no HasMouseCapture here.)
	if (!bPressed) return FReply::Unhandled();
	// Further drags move the camera while they stay on (or near) the map.
	Press(Geometry, Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	return FReply::Handled();
}

FReply SAcMinimap::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!bPressed || Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	bPressed = false;
	return FReply::Handled().ReleaseMouseCapture();
}

void SAcMinimap::OnMouseCaptureLost(const FCaptureLostEvent& Event)
{
	bPressed = false;
}

// MARK: - Paint

void SAcMinimap::PaintMark(FSlateWindowElementList& Out, const int32 Layer, const FGeometry& Own, const FAcMinimapMark& M) const
{
	auto Box = [&](const FSlateBrush* Brush, int32 L, FVector2D Center, double R, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(Out, L,
			Own.ToPaintGeometry(FVector2f(2 * R, 2 * R), FSlateLayoutTransform(FVector2f(Center - FVector2D(R, R)))), Brush,
			ESlateDrawEffect::None, Color);
	};
	auto Rect = [&](int32 L, double X, double Y, double W, double H, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(Out, L, Own.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))),
			FAcHudStyle::White(), ESlateDrawEffect::None, Color);
	};
	const FLinearColor Fill = Faded(M.Fill, M.Alpha);
	const FLinearColor Stroke = Faded(M.Stroke, M.Alpha);
	const bool bStroke = M.Line > 0 && Stroke.A > 0;
	switch (M.Shape)
	{
	case FAcMinimapMark::EShape::Square:
	{
		// SKShapeNode(rectOf:): the fill, then the outline centred on its edge.
		const double R = M.Radius, X = M.At.X - R, Y = M.At.Y - R, Lw = M.Line;
		Rect(Layer, X, Y, 2 * R, 2 * R, Fill);
		if (bStroke)
		{
			Rect(Layer + 1, X - Lw / 2, Y - Lw / 2, 2 * R + Lw, Lw, Stroke);
			Rect(Layer + 1, X - Lw / 2, Y + 2 * R - Lw / 2, 2 * R + Lw, Lw, Stroke);
			Rect(Layer + 1, X - Lw / 2, Y + Lw / 2, Lw, 2 * R - Lw, Stroke);
			Rect(Layer + 1, X + 2 * R - Lw / 2, Y + Lw / 2, Lw, 2 * R - Lw, Stroke);
		}
		break;
	}
	case FAcMinimapMark::EShape::Disc:
	case FAcMinimapMark::EShape::Diamond:
	{
		const bool bDisc = M.Shape == FAcMinimapMark::EShape::Disc;
		const FSlateBrush* Brush = bDisc ? DiscImage.Brush.Get() : DiamondImage.Brush.Get();
		// The outline centred on the edge: a white shape half a line larger,
		// the fill half a line smaller over it.
		const double Half = bDisc ? M.Line / 2 : M.Line / 2 * UE_SQRT_2;
		if (bStroke)
		{
			Box(Brush, Layer, M.At, M.Radius + Half, Stroke);
			Box(Brush, Layer + 1, M.At, FMath::Max(M.Radius - Half, 0.0), Fill);
		}
		else
		{
			Box(Brush, Layer + 1, M.At, M.Radius, Fill);
		}
		break;
	}
	}
}

int32 SAcMinimap::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	PaintedGeometry = Geometry;
	const FVector2D Local = FVector2D(Geometry.GetLocalSize());
	if (Local.X <= 0 || MapSize.X <= 0) return Layer;
	// The minimap's own points, scaled into the slot (`minimap.setScale`).
	const float K = (float)(Local.X / MapSize.X);
	const FGeometry Own = Geometry.MakeChild(FVector2f(MapSize), FSlateLayoutTransform(K));

	// Frame: a dark bevel, like the console.
	const double Pad = 6 * S;
	FSlateDrawElement::MakeBox(Out, ++Layer,
		Own.ToPaintGeometry(FVector2f(MapSize.X + 2 * Pad, MapSize.Y + 2 * Pad), FSlateLayoutTransform(FVector2f(-Pad, -Pad))),
		FAcHudStyle::White(), ESlateDrawEffect::None, Rgb(0.03, 0.05, 0.08, 0.9));
	{
		FAcConsolePaint P(Out, Own, ++Layer, MapSize.Y);
		P.Stroke(FAcRect(-Pad, -Pad, MapSize.X + 2 * Pad, MapSize.Y + 2 * Pad), 4 * S, Rgb(0.35, 0.55, 0.75, 0.6), 1.5 * S);
		Layer = P.NextLayer();
	}
	if (TerrainTexture.IsValid())
	{
		FSlateDrawElement::MakeBox(Out, ++Layer, Own.ToPaintGeometry(FVector2f(MapSize), FSlateLayoutTransform()),
			TerrainBrush.Get());
	}

	// Everything else is cropped to the map.
	Out.PushClip(FSlateClippingZone(Own));
	if (FogTexture.IsValid())
	{
		const FVector2D A = Point(FogOrigin), B = Point(FogOrigin + FogSize);
		FSlateDrawElement::MakeBox(Out, ++Layer, Own.ToPaintGeometry(FVector2f(B - A), FSlateLayoutTransform(FVector2f(A))),
			FogBrush.Get());
	}
	if (Sight && SightImage.Brush)
	{
		// The fan is drawn pointing up (north, min y); a heading h points
		// along (cos h, sin h) on the map (y down), so turn it by h + π/2.
		const FVector2D Size = SightImage.Brush->ImageSize;
		const double PadS = 1 * S;
		const FVector2D Apex = Point(Sight->Key);
		const FVector2D TopLeft = Apex - FVector2D(Size.X / 2, Size.Y - PadS);
		const FSlateRenderTransform Turn(FQuat2f((float)(Sight->Value + UE_HALF_PI)));
		FSlateDrawElement::MakeBox(Out, ++Layer,
			Own.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(TopLeft)), Turn,
				FVector2f(0.5f, (float)(1 - PadS / Size.Y))),
			SightImage.Brush.Get());
	}
	// The camera's outline goes with the other z-0 nodes, before the units.
	bool bOutlined = false;
	auto PaintOutline = [&]
	{
		if (!bOutlined && Outline.Num() > 1)
		{
			FSlateDrawElement::MakeLines(Out, ++Layer, Own.ToPaintGeometry(), Outline, ESlateDrawEffect::None, White(0.85),
				true, (float)(1.2 * S));
		}
		bOutlined = true;
	};
	for (const FAcMinimapMark& M : Marks)
	{
		if (M.Z > 0) PaintOutline();
		PaintMark(Out, Layer + 1, Own, M);
		Layer += 2;
	}
	PaintOutline();
	Out.PopClip();
	return Layer + 1;
}
