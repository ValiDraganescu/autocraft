// HUD text (chunk D1): name highlighting (`Names`, Sources/Autocraft/
// NameHighlight.swift) and the one text-drawing helper every `SAc*` widget
// paints its labels with, aligned the way SpriteKit's labels are.
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"

/// A stretch of a string: `Start`, `Len` characters, a name or not.
struct FAcTextRun
{
	int32 Start = 0;
	int32 Len = 0;
	bool bName = false;
};

/// Unit and building names in the game's text, picked out in orchid
/// (docs/naming.md "Writing the names"): every name as `Simulation.title`
/// writes it, singular or plural ("Fireflies"), Title Case or UPPER CASE,
/// as a whole word, with a possessive "'s" going with it. Lower case is left
/// alone ("the lab" is the word, not the building).
struct AUTOCRAFT_API FAcNames
{
	/// "Ranger" → "Rangers", "Firefly" → "Fireflies".
	static FString Plural(const FString& Name);
	/// Every form looked for, longest first.
	static const TArray<FString>& Forms();
	/// Where the names are in `Text` (start, length). Cached per text.
	static const TArray<FAcTextRun>& Ranges(const FString& Text);
	/// `Text` cut into runs, names and the rest, in order (one plain run
	/// when it names nothing).
	static TArray<FAcTextRun> Runs(const FString& Text);
};

/// How a label sits on its point (SpriteKit's alignment modes).
enum class EAcHAlign : uint8 { Left, Center, Right };
enum class EAcVAlign : uint8
{
	/// The point is the baseline (SpriteKit's default).
	Baseline,
	/// The point is the middle of the capitals (`.center`).
	Center,
	/// The point is the top of the capitals (`.top`).
	Top,
};

struct AUTOCRAFT_API FAcHudText
{
	FString Text;
	FSlateFontInfo Font;
	FLinearColor Color = FLinearColor::White;
	/// Names in the text are drawn in this (`Names.accent`); transparent: no highlighting.
	FLinearColor Accent;
	EAcHAlign HAlign = EAcHAlign::Left;
	EAcVAlign VAlign = EAcVAlign::Baseline;
	/// A drop shadow `ShadowOffset` points off (y down), in `ShadowColor`.
	FVector2f ShadowOffset = FVector2f::ZeroVector;
	FLinearColor ShadowColor = FLinearColor::Transparent;
	/// Extra space after each letter, points (NSAttributedString
	/// `.kern`). Slate's `FSlateFontInfo::LetterSpacing` only reaches shaped
	/// text (STextBlock), not MakeText or the font measure, so a kerned
	/// label is drawn letter by letter.
	float Kern = 0.0f;

	FAcHudText();
	FAcHudText(const FString& InText, const FSlateFontInfo& InFont, const FLinearColor& InColor,
		EAcHAlign H = EAcHAlign::Left, EAcVAlign V = EAcVAlign::Baseline);

	/// Width and height (line height) in the widget's units (points).
	FVector2f Measure() const;
	/// Draw at `At` (points, y down, in `Geometry`'s space), faded by `Opacity`.
	void Paint(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, FVector2f At,
		float Opacity = 1.0f) const;
};
