// `SAcPerfPanel`: the perf panel under the resource bar (GAME-LAYER.md
// §2.14, chunk D10), the Slate side of `PerfPanel.swift`: FPS now with a
// five-minute graph and the target line, GPU (this app's share, or GPU ms
// against the frame budget where the driver says nothing) with its graph,
// then seven lines in a fixed-width font: FPS and frame time, GPU, CPU and
// memory, the tick, Unreal's threads (game, render, RHI), draw calls and the
// view. Reads `FAcPerf`'s last sample when it paints; `UAcPerfSubsystem`
// hangs it (⌘D / `ac.PerfPanel` show and hide it). Lays out in Swift points.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class AUTOCRAFT_API SAcPerfPanel : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAcPerfPanel) {}
	SLATE_END_ARGS()

	/// As wide as the resource bar (`HUD.attachPerf`).
	static constexpr float Width = 340.0f;
	static constexpr float GraphHeight = 34.0f;
	static constexpr int32 LineCount = 7;
	/// `(rows · 15 + 2 · (graph + 8) + 14)` points.
	static constexpr float Height = LineCount * 15.0f + 2 * (GraphHeight + 8.0f) + 14.0f;

	void Construct(const FArguments& InArgs);

	/// The panel's text lines for the sample (also what the log could show).
	static TArray<FString> Lines(const struct FAcPerfSample& Sample);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, Height); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
};
