#include "AcConsoleWarp.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/HittestGrid.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SVirtualWindow.h"

// MARK: - SAcConsoleFaces

void SAcConsoleFaces::Construct(const FArguments& InArgs)
{
	PaintFlat = InArgs._OnPaintFlat;
	ChildSlot[InArgs._Content.Widget];
}

int32 SAcConsoleFaces::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const int32 Under = PaintFlat ? PaintFlat(Geometry, Out, Layer) : Layer;
	return SCompoundWidget::OnPaint(Args, Geometry, Culling, Out, Under + 1, Style, bParentEnabled);
}

// MARK: - FAcConsoleWarp

FAcConsoleWarp::FAcConsoleWarp(const TSharedRef<SWidget>& InStrip)
	: Strip(InStrip)
{
	Grid = MakeUnique<FHittestGrid>();
	Window = SNew(SVirtualWindow).Size(FVector2D(100, 100));
	Window->SetContent(Strip);
	Brush = MakeShared<FSlateBrush>();
	Brush->DrawAs = ESlateBrushDrawType::Image;
}

FAcConsoleWarp::~FAcConsoleWarp()
{
	if (Window) Window->SetContent(SNullWidget::NullWidget);
	// The renderer must outlive the frames it queued (FWidgetRenderer's own
	// destructor defers to the render thread).
	Renderer.Reset();
}

void FAcConsoleWarp::Render(const FVector2D InPoints, const float PixelsPerPoint, const float DeltaTime)
{
	if (!FApp::CanEverRender() || InPoints.X < 1 || InPoints.Y < 1) return;
	const float S = FMath::Max(PixelsPerPoint, 0.25f);
	const FIntPoint Px((int32)FMath::CeilToDouble(InPoints.X * S), (int32)FMath::CeilToDouble(InPoints.Y * S));
	if (!Renderer)
	{
		// Gamma space: the strip blends as the HUD drawn straight to the
		// window does; it goes on screen with `NoGamma`.
		Renderer = MakeUnique<FWidgetRenderer>(/*bUseGammaCorrection*/ true, /*bInClearTarget*/ true);
	}
	if (!Target || Px != Pixels)
	{
		Target.Reset(FWidgetRenderer::CreateTargetFor(FVector2D(Px), TF_Bilinear, /*bUseGammaCorrection*/ true));
		if (!Target) return;
		Brush->SetResourceObject(Target.Get());
		Pixels = Px;
	}
	Points = InPoints;
	Scale = S;
	Brush->ImageSize = FVector2D(Px);
	Window->Resize(FVector2D(Px));
	Renderer->DrawWindow(Target.Get(), *Grid, Window.ToSharedRef(), S, FVector2D(Px), DeltaTime);
}

void FAcConsoleWarp::Paint(FSlateWindowElementList& Out, const int32 Layer, const FGeometry& Geometry, const FAcCabLayout& L) const
{
	if (!Target || Pixels.X <= 0 || Pixels.Y <= 0) return;
	// Console.warpFaces: the grid over each face's flat frame, each point
	// moved to where the face is seen.
	const FSlateRenderTransform& T = Geometry.GetAccumulatedRenderTransform();
	const FVector2D Uv(Scale / Pixels.X, Scale / Pixels.Y);
	TArray<FSlateVertex> V;
	TArray<SlateIndex> I;
	V.Reserve(3 * (Columns + 1) * (Rows + 1));
	I.Reserve(3 * Columns * Rows * 6);
	for (int32 K = 0; K < 3; ++K)
	{
		const FAcRect& F = L.Faces[K].Flat;
		if (F.IsEmpty()) continue;
		const FAcProjection Seen = L.Faces[K].Projection();
		const int32 Base = V.Num();
		for (int32 J = 0; J <= Rows; ++J)
		{
			for (int32 Ii = 0; Ii <= Columns; ++Ii)
			{
				const FVector2D P(F.X + F.W * Ii / Columns, F.Y + F.H * J / Rows);
				const FVector2D Q = L.ToSlate(Seen(P));
				// The strip's rows run from its top (y = FaceTop).
				const FVector2f Tex((float)(P.X * Uv.X), (float)((Points.Y - P.Y) * Uv.Y));
				V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(T, FVector2f(Q), Tex, FColor::White));
			}
		}
		for (int32 J = 0; J < Rows; ++J)
		{
			for (int32 Ii = 0; Ii < Columns; ++Ii)
			{
				const SlateIndex A = (SlateIndex)(Base + J * (Columns + 1) + Ii);
				const SlateIndex B = A + 1, C = (SlateIndex)(A + Columns + 1), D = (SlateIndex)(C + 1);
				I.Append({A, B, D, A, D, C});
			}
		}
	}
	const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*Brush);
	FSlateDrawElement::MakeCustomVerts(Out, Layer, Handle, V, I, nullptr, 0, 0, ESlateDrawEffect::NoGamma);
}
