// Drawing in Swift console points (chunk D2): a small painter over Slate's
// draw elements that takes SpriteKit-style coordinates (y up from the
// view's bottom) and draws what the Swift console's nodes draw: filled and
// stroked rectangles with rounded corners (`SKShapeNode`, `glowWidth`),
// images tinted as `colorBlendFactor` does, and labels on their baselines.
// Each call goes on its own layer, so the order of calls is the order on
// screen. Shared by the console's faces (D2), and the warp (D3) and card (D5).
#pragma once

#include "CoreMinimal.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"

#include "AcCab.h"
#include "AcHudText.h"

struct FSlateBrush;

class AUTOCRAFT_API FAcConsolePaint
{
public:
	/// `ViewHeight`: the view's height in points (to turn y up into y down).
	FAcConsolePaint(FSlateWindowElementList& InOut, const FGeometry& InGeometry, int32 InLayer, double InViewHeight)
		: Out(InOut), Geometry(InGeometry), Layer(InLayer), ViewHeight(InViewHeight)
	{
	}

	/// The next free layer (after everything drawn so far).
	int32 NextLayer() const { return Layer + 1; }

	/// A filled rectangle (`SKShapeNode(rect:)` fill; corners are not rounded).
	void Fill(const FAcRect& R, const FLinearColor& Color);
	/// A filled rectangle with its corners rounded by `Radius` (`SKShapeNode(rect:
	/// cornerRadius:)` fill; chunk D3).
	void FillRounded(const FAcRect& R, double Radius, const FLinearColor& Color);
	/// A rectangle's outline, its corners rounded by `Radius`, with SpriteKit's
	/// `glowWidth` (a soft halo `Glow` points wide).
	void Stroke(const FAcRect& R, double Radius, const FLinearColor& Color, double Width = 1, double Glow = 0);
	/// An image over `R`. `Tint` multiplies it (SpriteKit's `colorBlendFactor`
	/// mixes toward the texture times the colour: see `BlendTint`).
	void Image(const FSlateBrush* Brush, const FAcRect& R, const FLinearColor& Tint = FLinearColor::White);
	/// Baked art (AcBakedArt.h) over `R`, at `Opacity`.
	void Baked(const class FAcBakedArt& Art, const FAcRect& R, float Opacity = 1.0f);
	/// A label with its point at `At` (Swift points, y up), SpriteKit-aligned.
	/// `bShadow`: `PilotOverlay.shadow` (black at 0.75, 1.2 points down right).
	void Text(const FString& Text, double Size, const FLinearColor& Color, FVector2D At, EAcHAlign Align = EAcHAlign::Left,
		bool bShadow = true, bool bNames = true, float Opacity = 1.0f);
	/// A label set up by hand (letter spacing, a glow for a shadow...), its
	/// point at `At` (Swift points, y up).
	void Text(const FAcHudText& Label, FVector2D At, float Opacity = 1.0f);
	/// A label's width in points.
	static double TextWidth(const FString& Text, double Size);

	/// `SKSpriteNode.color` at `colorBlendFactor` `Factor`: the tint that
	/// multiplies the texture (sRGB components mixed toward `Color`).
	static FLinearColor BlendTint(const FLinearColor& Color, double Factor, double Alpha = 1.0);
	/// A colour's alpha times `A` (`withAlphaComponent` on an opaque colour).
	static FLinearColor WithAlpha(const FLinearColor& C, double A) { return FLinearColor(C.R, C.G, C.B, (float)A); }

	FVector2D ToSlate(FVector2D P) const { return FVector2D(P.X, ViewHeight - P.Y); }

private:
	int32 Take() { return ++Layer; }

	FSlateWindowElementList& Out;
	const FGeometry& Geometry;
	int32 Layer;
	double ViewHeight;
};
