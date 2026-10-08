// A drawing (FAcArtList) baked once into a texture: the console's art
// (faces, frames, plates) is drawn every frame but changes only with the
// layout, so it is drawn when the layout changes and the texture after that.
//
// Slate draws the list into a render target (in its gamma space, as the HUD
// on screen), which is read back, without waiting for the GPU, into an sRGB
// texture, as the Core Graphics art was: drawn smaller (2 pixels a point on
// a 1× screen) it is filtered in linear light, so its thin lit lines keep
// their weight. Until the copy is back (a frame or two) the target itself is
// drawn, premultiplied and as it is (`Effects`, `Tint`).
#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/StrongObjectPtr.h"

#include "AcCab.h"

class FAcArtList;
struct FSlateBrush;
enum class ESlateDrawEffect : uint8;

class AUTOCRAFT_API FAcBakedArt
{
public:
	/// Draws the part of `List` in `Area` (Swift points, y up) at the list's scale.
	void Bake(const FAcArtList& List, const FAcRect& Area);
	void Reset();
	/// The art's brush, `Area` points in size; null before a bake. Draw it
	/// with `Effects()` and `Tint()` (FAcConsolePaint::Baked does).
	const FSlateBrush* Get() const;
	ESlateDrawEffect Effects() const;
	FLinearColor Tint(float Opacity = 1.0f) const;
	FVector2D Size() const;

private:
	struct FPending;
	/// Takes the texture once the copy is back.
	void Poll() const;

	mutable TStrongObjectPtr<UTextureRenderTarget2D> Target;
	mutable TStrongObjectPtr<UTexture2D> Texture;
	mutable TSharedPtr<FSlateBrush> Brush;
	mutable TSharedPtr<FPending, ESPMode::ThreadSafe> Pending;
};
