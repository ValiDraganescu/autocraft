#include "SAcPerfPanel.h"

#include "AcHudStyle.h"
#include "AcHudText.h"
#include "AcPerf.h"

#include "Rendering/DrawElements.h"

namespace
{
	/// Polyline of the last `HistoryLength` values, newest at the right, in a
	/// `W`×`H` box whose top left is `Origin` (points, y down).
	TArray<FVector2f> Graph(const TArray<double>& Values, const double MaxValue, const FVector2f Origin, const float W, const float H)
	{
		TArray<FVector2f> Points;
		const int32 N = FAcPerf::HistoryLength;
		const int32 Start = N - Values.Num();
		for (int32 I = 0; I < Values.Num(); ++I)
		{
			const float X = (float)(Start + I) / (float)(N - 1) * W;
			const float Y = (float)FMath::Min(Values[I] / FMath::Max(MaxValue, 1e-6), 1.0) * H;
			Points.Add(Origin + FVector2f(X, H - Y));
		}
		return Points;
	}

	double MaxOf(const TArray<double>& Values)
	{
		double M = 0;
		for (const double V : Values) M = FMath::Max(M, V);
		return M;
	}
}

void SAcPerfPanel::Construct(const FArguments& InArgs)
{
	FAcHudStyle::Initialize();
}

TArray<FString> SAcPerfPanel::Lines(const FAcPerfSample& P)
{
	auto Pct = [](double V) { return V >= 0 ? FString::Printf(TEXT("%3.0f%%"), V) : FString(TEXT("  n/a")); };
	const FString Prims = P.Primitives >= 1'000'000 ? FString::Printf(TEXT("%.2fM"), P.Primitives / 1e6)
	                                                : FString::Printf(TEXT("%.0fk"), P.Primitives / 1e3);
	return {
		FString::Printf(TEXT("FPS %4.0f / %d    frame %5.1f ms  max %5.1f"), P.Fps, P.TargetFps, P.FrameP50, P.FrameMax),
		FString::Printf(TEXT("GPU %s  app %s  chip %s"),
			P.GpuFrameMs >= 0 ? *FString::Printf(TEXT("%5.1f ms/frame"), P.GpuFrameMs) : TEXT("  n/a ms/frame"), *Pct(P.GpuApp), *Pct(P.GpuChip)),
		FString::Printf(TEXT("CPU %4.0f%%   mem %5.0f MB  gfx %5.0f MB"), P.Cpu, P.MemoryMB, P.GraphicsMB),
		FString::Printf(TEXT("tick %5.2f ms  p95 %5.2f  max %5.1f"), P.TickMs, P.TickP95, P.TickMax),
		FString::Printf(TEXT("game %5.1f  render %5.1f  rhi %5.1f ms"), P.GameThreadMs, P.RenderThreadMs, P.RhiThreadMs),
		// The RHI counts draws only while its GPU profiler gathers them (`stat rhi`).
		P.DrawCalls > 0 ? FString::Printf(TEXT("scene %d draws  %s prims"), P.DrawCalls, *Prims) : FString(TEXT("scene  n/a draws (stat rhi)")),
		FString::Printf(TEXT("%-10s %s %3.0f fps"), *P.ViewName.Left(10), *P.ViewPixels, P.Fps),
	};
}

int32 SAcPerfPanel::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FAcPerf& Perf = FAcPerf::Get();
	const FAcPerfSample& P = Perf.LastSample();

	// A chrome plate with its glass (`Chrome.plateNode`, cuts 4/4/12/12):
	// the level plate (cuts 14/14/4/4) turned upside down.
	{
		constexpr float Pad = FAcHudStyle::PlatePad, Scale = FAcHudStyle::PlateScale;
		const FVector2f Size = FVector2f(Width + 2 * Pad, Height + 2 * Pad);
		FSlateDrawElement::MakeBox(Out, Layer,
			Geometry.ToPaintGeometry(FVector2f(Size * Scale), FSlateLayoutTransform(1.0f / Scale, FVector2f(-Pad, -Pad)),
				FSlateRenderTransform(FScale2f(1.0f, -1.0f)), FVector2f(0.5f, 0.5f)),
			FAcHudStyle::Plate(EAcPlate::Level));
	}

	const float GraphW = Width - 16.0f;
	float Y = 8.0f;
	auto Frame = [&](const TCHAR* Label) -> FVector2f
	{
		const FVector2f Origin(8.0f, Y);
		FSlateDrawElement::MakeBox(Out, Layer + 1,
			Geometry.ToPaintGeometry(FVector2f(GraphW, GraphHeight), FSlateLayoutTransform(Origin)),
			FAcHudStyle::White(), ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.35f));
		const TArray<FVector2f> Box = {Origin, Origin + FVector2f(GraphW, 0), Origin + FVector2f(GraphW, GraphHeight),
			Origin + FVector2f(0, GraphHeight), Origin};
		FSlateDrawElement::MakeLines(Out, Layer + 2, Geometry.ToPaintGeometry(), Box, ESlateDrawEffect::None,
			FLinearColor(1, 1, 1, 0.12f), true, 1.0f);
		FAcHudText(Label, FAcHudStyle::Font(10), FLinearColor(1, 1, 1, 0.45f), EAcHAlign::Right, EAcVAlign::Top)
			.Paint(Out, Layer + 3, Geometry, FVector2f(Width - 11.0f, Y + 3.0f));
		Y += GraphHeight + 8.0f;
		return Origin;
	};

	// FPS over five minutes, with the target.
	const FVector2f FpsAt = Frame(TEXT("FPS · 5 MIN"));
	const TArray<double>& Fps = Perf.FpsHistory();
	const double Top = FMath::Max<double>(P.TargetFps, MaxOf(Fps)) * 1.1;
	const float TargetY = FpsAt.Y + GraphHeight - (float)(P.TargetFps / Top) * GraphHeight;
	FSlateDrawElement::MakeLines(Out, Layer + 2, Geometry.ToPaintGeometry(),
		TArray<FVector2f>{FVector2f(FpsAt.X, TargetY), FVector2f(FpsAt.X + GraphW, TargetY)}, ESlateDrawEffect::None,
		FLinearColor(1, 1, 1, 0.25f), true, 1.0f);
	if (Fps.Num() > 1)
	{
		FSlateDrawElement::MakeLines(Out, Layer + 3, Geometry.ToPaintGeometry(), Graph(Fps, Top, FpsAt, GraphW, GraphHeight),
			ESlateDrawEffect::None, FAcHudStyle::Srgb(0.45, 1, 0.55), true, 1.5f);
	}

	// GPU over five minutes.
	const FVector2f GpuAt = Frame(Perf.GpuIsAppShare() ? TEXT("GPU % (APP) · 5 MIN") : TEXT("GPU % OF FRAME · 5 MIN"));
	const TArray<double>& Gpu = Perf.GpuHistory();
	if (Gpu.Num() > 1)
	{
		FSlateDrawElement::MakeLines(Out, Layer + 3, Geometry.ToPaintGeometry(),
			Graph(Gpu, FMath::Max(25.0, MaxOf(Gpu) * 1.2), GpuAt, GraphW, GraphHeight), ESlateDrawEffect::None,
			FAcHudStyle::Srgb(1, 0.7, 0.3), true, 1.5f);
	}

	// The lines.
	const TArray<FString> Text = Lines(P);
	const FSlateFontInfo Mono = FAcHudStyle::Mono(11);
	const bool bLow = P.Fps + 1 < P.TargetFps * 0.9;
	for (int32 I = 0; I < Text.Num() && I < LineCount; ++I)
	{
		const FLinearColor Color = I > 0 ? FAcHudStyle::Srgb(0.85, 0.92, 1, 0.92)
		                         : bLow  ? FAcHudStyle::Srgb(1, 0.5, 0.4)
		                                 : FAcHudStyle::Srgb(0.6, 1, 0.65);
		FAcHudText(Text[I], Mono, Color, EAcHAlign::Left, EAcVAlign::Top).Paint(Out, Layer + 3, Geometry, FVector2f(10.0f, Y + 2.0f));
		Y += 15.0f;
	}
	return Layer + 3;
}
