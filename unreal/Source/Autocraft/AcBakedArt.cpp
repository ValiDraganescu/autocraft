#include "AcBakedArt.h"

#include "AcArtList.h"
#include "AcCabArt.h"

#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

#include <atomic>

namespace
{
	/// Paints one list, the part in `Area` filling the widget.
	class SAcArtCanvas : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SAcArtCanvas) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&) {}

		const FAcArtList* List = nullptr;
		FAcRect Area;

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Area.W, Area.H); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out,
			int32 Layer, const FWidgetStyle&, bool) const override
		{
			return List ? List->Paint(Out, Geometry, Layer, Area.MaxY(), FVector2D(-Area.X, 0), 1.0f, ESlateDrawEffect::None) : Layer;
		}
	};

	FWidgetRenderer& Renderer()
	{
		// Gamma space, as the HUD drawn straight to the window (and the
		// console's warp, AcConsoleWarp.cpp). Never freed: it must outlive
		// every frame it queued, up to the engine's exit.
		static FWidgetRenderer* R = new FWidgetRenderer(/*bUseGammaCorrection*/ true, /*bInClearTarget*/ true);
		return *R;
	}
}

struct FAcBakedArt::FPending
{
	TUniquePtr<FRHIGPUTextureReadback> Readback;
	FIntPoint Pixels = FIntPoint::ZeroValue;
	TArray<FColor> Data;
	std::atomic<bool> bDone{false};
	std::atomic<bool> bAsked{false};
};

void FAcBakedArt::Reset()
{
	Target.Reset();
	Texture.Reset();
	Brush.Reset();
	Pending.Reset();
}

FVector2D FAcBakedArt::Size() const
{
	return Brush ? Brush->ImageSize : FVector2D::ZeroVector;
}

const FSlateBrush* FAcBakedArt::Get() const
{
	Poll();
	return Texture || Target ? Brush.Get() : nullptr;
}

ESlateDrawEffect FAcBakedArt::Effects() const
{
	// The target holds Slate's gamma-space output, premultiplied (it blends
	// into a clear target); the texture straight sRGB.
	return Texture ? ESlateDrawEffect::None : ESlateDrawEffect::NoGamma | ESlateDrawEffect::PreMultipliedAlpha;
}

FLinearColor FAcBakedArt::Tint(const float Opacity) const
{
	return Texture ? FLinearColor(1, 1, 1, Opacity) : FLinearColor(Opacity, Opacity, Opacity, Opacity);
}

void FAcBakedArt::Bake(const FAcArtList& List, const FAcRect& Area)
{
	Reset();
	if (!FApp::CanEverRender() || !FSlateApplication::IsInitialized() || Area.IsEmpty() || List.IsEmpty()) return;
	const double S = List.Scale();
	const FIntPoint Px(FMath::Max(1, (int32)FMath::CeilToDouble(Area.W * S)), FMath::Max(1, (int32)FMath::CeilToDouble(Area.H * S)));
	// Not FWidgetRenderer::CreateTargetFor: in gamma mode its target is
	// sRGB, which encodes Slate's gamma-space output once more on the way in
	// (undone when it is sampled, not when it is read back). A plain target
	// holds what Slate would put on screen.
	UTextureRenderTarget2D* T = NewObject<UTextureRenderTarget2D>();
	T->ClearColor = FLinearColor::Transparent;
	T->InitCustomFormat(Px.X, Px.Y, PF_B8G8R8A8, /*bInForceLinearGamma*/ true);
	T->UpdateResourceImmediate(true);
	Target.Reset(T);
	const TSharedRef<SAcArtCanvas> Canvas = SNew(SAcArtCanvas);
	Canvas->List = &List;
	// The texture's pixels are whole: the drawing covers `Px / S` points.
	Canvas->Area = FAcRect(Area.X, Area.MaxY() - Px.Y / S, Px.X / S, Px.Y / S);
	Renderer().DrawWidget(T, Canvas, (float)S, FVector2D(Px), 0.0f, false);
	Canvas->List = nullptr;
	Brush = MakeShared<FSlateBrush>();
	Brush->DrawAs = ESlateBrushDrawType::Image;
	Brush->ImageSize = FVector2D(Px) / S;
	Brush->SetResourceObject(T);

	// The copy back, queued behind the drawing.
	Pending = MakeShared<FPending, ESPMode::ThreadSafe>();
	Pending->Pixels = Px;
	FTextureRenderTargetResource* Resource = T->GameThread_GetRenderTargetResource();
	ENQUEUE_RENDER_COMMAND(AcBakedArtCopy)([P = Pending, Resource](FRHICommandListImmediate& RHICmdList)
	{
		P->Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("AcBakedArt"));
		P->Readback->EnqueueCopy(RHICmdList, Resource->GetRenderTargetTexture());
	});
}

void FAcBakedArt::Poll() const
{
	if (!Pending) return;
	if (Pending->bDone)
	{
		// Straight alpha for the texture (Core Graphics' bitmap was
		// premultiplied too, and unpremultiplied the same way).
		const TSharedPtr<FPending, ESPMode::ThreadSafe> P = MoveTemp(Pending);
		if (P->Data.Num() != P->Pixels.X * P->Pixels.Y) return;  // keep drawing the target
		for (FColor& C : P->Data)
		{
			const uint8 A = C.A;
			auto Un = [A](uint8 V) -> uint8 { return A == 0 ? 0 : (uint8)FMath::Min(255, (V * 255 + A / 2) / A); };
			C = FColor(Un(C.R), Un(C.G), Un(C.B), A);
		}
		FAcArtImage Image;
		Image.Width = P->Pixels.X;
		Image.Height = P->Pixels.Y;
		Image.Pixels = MoveTemp(P->Data);
		Image.Points = Brush ? Brush->ImageSize : FVector2D(P->Pixels);
		UTexture2D* T = AcCabArt::ToTexture(Image, TEXT("AcBakedArt"));
		if (!T) return;
		Texture.Reset(T);
		Brush = MakeShared<FSlateBrush>();
		Brush->DrawAs = ESlateBrushDrawType::Image;
		Brush->ImageSize = Image.Points;
		Brush->SetResourceObject(T);
		Target.Reset();
		return;
	}
	if (Pending->bAsked.exchange(true)) return;
	ENQUEUE_RENDER_COMMAND(AcBakedArtPoll)([P = Pending](FRHICommandListImmediate&)
	{
		if (P->Readback && P->Readback->IsReady())
		{
			int32 Pitch = 0;
			if (const FColor* Src = static_cast<const FColor*>(P->Readback->Lock(Pitch)))
			{
				P->Data.SetNumUninitialized(P->Pixels.X * P->Pixels.Y);
				for (int32 Y = 0; Y < P->Pixels.Y; ++Y)
				{
					FMemory::Memcpy(&P->Data[Y * P->Pixels.X], Src + (int64)Y * Pitch, P->Pixels.X * sizeof(FColor));
				}
			}
			P->Readback->Unlock();
			P->Readback.Reset();
			P->bDone = true;
		}
		P->bAsked = false;
	});
}
