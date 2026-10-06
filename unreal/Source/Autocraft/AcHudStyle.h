// `FAcHudStyle`: the HUD's look (GAME-LAYER.md §2.4 "Chrome", chunk D1), the
// Unreal side of `Chrome` (Sources/Autocraft/HUDChrome.swift), the team
// colours (`MaterialLibrary.teams`) and the name accent (`Names.accent`).
//
// Units: every HUD widget lays out in Swift **points** (the numbers in
// HUD.swift and Console.swift read as they are). `SAcRoot` scales points to
// pixels by the screen's backing scale (2 on a Retina Mac), so the HUD is as
// big as the Swift game's and drawn at full resolution.
//
// Fonts: Barlow Condensed (Google Fonts, OFL) stands in for DIN Condensed
// Bold, a macOS system font. `Font(Size)` takes the Swift point size (Slate
// sizes are at 96 dpi, so it converts).
//
// Art: plates baked from the Swift code itself (`unreal/Tools/hud/`), imported
// by `unreal/Tools/Editor/import_hud.py` to /Game/UI/Hud and /Game/UI/Fonts.
// A missing asset falls back to a flat fill (and a log line), never a crash.
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Geometry.h"
#include "Layout/SlateRect.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateBrush.h"

class UTexture2D;
class UFontFace;
struct FCompositeFont;

/// Which plate (`Chrome.plate` as the Swift HUD calls it).
enum class EAcPlate : uint8
{
	/// The resource bar: 340×42, cuts 4/4/14/14, LEDs.
	Bar,
	/// The scoreboard: cuts 4/4/14/14, no LEDs; stretches sideways.
	Score,
	/// The level-up banner: cuts 14/14/4/4, no LEDs; stretches.
	Level,
	Count
};

/// Weight of Barlow Condensed.
enum class EAcFontWeight : uint8
{
	Medium,
	SemiBold,
	Bold,
};

class AUTOCRAFT_API FAcHudStyle
{
public:
	/// Load the fonts and textures (once; they stay rooted). Safe to call again.
	static void Initialize();
	static void Shutdown();

	// MARK: - Palette (HUDChrome.swift `Chrome`), sRGB components as Swift's NSColor

	/// sRGB components (0..1, as NSColor calibrated) to the linear colour
	/// Slate needs to put those sRGB values on screen. Slate on the Mac
	/// encodes its output with a plain pow(1/2.2), not the sRGB curve
	/// (GammaCorrectionCommon.ush), so this decodes with pow(2.2) there
	/// (FromPow22): without it every dark HUD tone came out ~+6/255 too
	/// bright. HUD only: for a colour in the 3D world use `World`.
	static FLinearColor Srgb(double R, double G, double B, double A = 1.0);
	/// sRGB components to true linear (the sRGB curve), for the 3D world
	/// (materials, lights): the same numbers as Swift's NSColor in SceneKit.
	static FLinearColor World(double R, double G, double B, double A = 1.0);
	/// The inverse of `Srgb`, one channel: a HUD linear value back to its sRGB component.
	static double HudSrgb(float Linear);
	/// A HUD colour (from `Srgb`) turned into its `World` twin.
	static FLinearColor HudToWorld(const FLinearColor& Hud);
	/// sRGB-encoded pixels (8-bit, as a PNG or Core Graphics writes them)
	/// re-encoded for an sRGB texture Slate draws: the byte whose sRGB
	/// decode is pow(byte, 2.2), so the picture lands on screen as drawn.
	/// Imported HUD textures get the same through their source encoding
	/// (`Gamma22` in import_hud.py/import_icons.py). Identity off the Mac.
	static uint8 SlateTextureByte(uint8 Srgb);
	static void CompensateForSlate(TArrayView<FColor> Pixels);
	/// A transient texture for Slate from an sRGB picture (FImageUtils'
	/// CreateTexture2DFromImage), compensated as above.
	static UTexture2D* HudTexture(const struct FImageView& Image);
	/// The same from an encoded PNG/JPEG (FImageUtils::ImportBufferAsTexture2D).
	static UTexture2D* ImportHudTexture(TArrayView64<const uint8> Encoded);
	/// `a.blended(withFraction: f, of: b)`, done on the sRGB components as AppKit does.
	static FLinearColor Blend(const FLinearColor& A, double Fraction, const FLinearColor& B);

	static FLinearColor Cyan() { return Srgb(0.35, 0.85, 1); }
	static FLinearColor Ice() { return Srgb(0.8, 0.95, 1); }
	static FLinearColor Amber() { return Srgb(1, 0.7, 0.2); }
	static FLinearColor Glass() { return Srgb(0.01, 0.035, 0.07); }
	/// Text on the plates.
	static FLinearColor Text() { return Srgb(0.92, 0.97, 1); }
	static FLinearColor Soft() { return Srgb(0.55, 0.85, 0.95); }
	/// Keys in tips and prompts.
	static FLinearColor KeyYellow() { return Srgb(1, 0.85, 0.3); }
	/// Energy (Dropship), violet.
	static FLinearColor Energy() { return Srgb(0.75, 0.45, 1); }
	/// Levels and XP, gold (`LevelInfo.color`).
	static FLinearColor LevelGold() { return Srgb(1, 0.8, 0.32); }
	/// A press queued for the team's AI (`Console.queueColor`).
	static FLinearColor QueueAmber() { return Srgb(1, 0.76, 0.3); }
	/// Unit and building names in text (`Names.accent`), orchid.
	static FLinearColor NameAccent() { return Srgb(0.95, 0.6, 1); }
	/// Deposit pop-ups (`HUD.applyPopup`).
	static FLinearColor Deposit() { return Srgb(0.55, 0.85, 1); }
	/// Health: green over 0.6, yellow over 0.3, else red.
	static FLinearColor Health(double Fraction);

	// MARK: - Players (MaterialLibrary.teams, widened to 8; make_materials.py PALETTE)

	static constexpr int32 MaxPlayers = 8;
	/// Player `P`'s colour (blue, red, green, gold, violet, teal, orange, pink).
	static FLinearColor PlayerColor(int64 P);
	/// The colour as the HUD writes a player's numbers: 30% toward white.
	static FLinearColor PlayerTextColor(int64 P, double Toward = 0.3);
	/// "Blue", "Red", "Green"...
	static FString PlayerName(int64 P);

	// MARK: - Fonts

	/// Barlow Condensed at a Swift point size (DIN Condensed Bold's place).
	static FSlateFontInfo Font(float SwiftPoints, EAcFontWeight Weight = EAcFontWeight::Bold);
	/// The fixed-width font (Swift's Menlo, for the perf panel): Slate's mono.
	static FSlateFontInfo Mono(float SwiftPoints);
	/// Height of capitals as a share of the font's em (Barlow: 0.7).
	static constexpr float CapHeight = 0.7f;

	// MARK: - Art

	/// The plate's box brush (9-slice). Draw it over the plate's rect grown
	/// by `PlatePad` on every side (the shadow and glow live there).
	static const FSlateBrush* Plate(EAcPlate Which);
	/// Draw a plate over `Rect` (points: the plate itself, without its pad).
	/// Use this, not `MakeBox` with `Plate()`: Slate measures box margins in
	/// texture pixels, so the plate is drawn in a space of texture pixels.
	static void PaintPlate(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, EAcPlate Which,
		const FSlateRect& Rect, float Opacity = 1.0f);
	/// Texture pixels per point in the baked plates (`Chrome.context`'s 2x).
	static constexpr float PlateScale = 2.0f;
	/// Points round a plate's rect that its texture adds (`Chrome.plateNode`'s pad).
	static constexpr float PlatePad = 6.0f;
	static const FSlateBrush* OreIcon();
	static const FSlateBrush* HydrogenIcon();
	static const FSlateBrush* SupplyIcon();
	/// A plain white brush (for fills tinted by the draw colour).
	static const FSlateBrush* White();

private:
	static bool bInitialized;
};
