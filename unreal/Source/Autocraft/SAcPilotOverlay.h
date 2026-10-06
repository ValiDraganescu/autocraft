// `SAcPilotOverlay` (chunk E6): the first-person cockpit's linework over the
// view, the port of `PilotOverlay` (Sources/Autocraft/PilotOverlay.swift).
// A reticle (four brackets and a pip) in the action's tone, opening over
// something to work and closing in on an enemy in range; the work ring and
// its label (DRILLING 42%); the compass tape up top (120° over 420 points,
// N in amber, the goal's diamond); the sight readout under it ("ENEMY
// RANGER  3.8" and a health bar); the hit marker and the damage floating
// off it (KILLED for a kill); a vehicle's gun marker; the note over the
// reticle; the hurt flash round the view; the green shade inside a Derrick;
// the build menu (E8 fills it). Without a console (`bPanel`): the prompt
// under the reticle, the keys bottom left and the unit's panel (name, hit
// points, cargo bay) at the bottom; with one, its screen says those.
//
// Drawn in `OnPaint` in Swift points with y up from the view's bottom (the
// numbers read as in PilotOverlay.swift), animated on the real clock.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

#include "AcPilotText.h"

struct FAcPilotPen;

class AUTOCRAFT_API SAcPilotOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAcPilotOverlay) : _Panel(true) {}
		/// Draw the prompt, the keys and the unit's panel (no console).
		SLATE_ARGUMENT(bool, Panel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/// This frame's cockpit (`PilotOverlay.show`); `Now`: real seconds.
	void Show(const FAcPilotInfo& Info, double Now);
	/// A new ride: no hurt flash from the last unit's health, no marks.
	void Reset();
	void SetPanel(bool bInPanel) { bPanel = bInPanel; }
	/// Stills: marks drawn `MarkAge` seconds after they land, and the hurt
	/// flash held, whatever the clock says.
	void Hold(TOptional<double> MarkAge, bool bHurt, bool bNote)
	{
		HoldMarkAge = MarkAge;
		bHoldHurt = bHurt;
		bHoldNote = bNote;
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(0, 0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

	// The Swift colours (`PilotOverlay.line`...).
	static FLinearColor LineColor();
	static FLinearColor ReadyColor();
	static FLinearColor BlockedColor();
	static FLinearColor EnemyColor();
	static FLinearColor PanelFill();
	static FLinearColor PanelStroke();
	static FLinearColor ToneColor(EAcTone Tone);

	/// Points across the compass tape, and degrees of heading on it.
	static constexpr double TapeWidth = 420;
	static constexpr double TapeSpan = 120;

private:
	void PaintReticle(FAcPilotPen& P, FVector2D C, double Now) const;
	void PaintCompass(FAcPilotPen& P, double W, double H) const;
	void PaintSight(FAcPilotPen& P, double W, double H) const;
	void PaintMarks(FAcPilotPen& P, FVector2D C, double Now) const;
	void PaintPanel(FAcPilotPen& P, double W) const;
	void PaintMenu(FAcPilotPen& P, double H) const;
	double ReticleScale(double Now) const;

	bool bPanel = true;
	bool bHasInfo = false;
	FAcPilotInfo Info;

	// The reticle's scale, eased over 0.12 s (`reticle.run(.scale(to:))`).
	double ScaleFrom = 1, ScaleTo = 1, ScaleSince = -10;
	// The hit marker (`markHit`).
	int32 ShownHit = 0;
	double HitSince = -10;
	bool bHitKill = false;
	struct FFloat
	{
		FString Text;
		bool bKill = false;
		double Born = 0;
	};
	TArray<FFloat> Floats;
	// The note over the reticle: full for 1.4 s, then fades over 0.5 s.
	TOptional<FString> ShownNote;
	double NoteSince = -10;
	// The hurt flash: 0.9 down to nothing over 0.5 s.
	TOptional<double> LastHp;
	double HurtSince = -10;
	double Clock = 0;
	TOptional<double> HoldMarkAge;
	bool bHoldHurt = false;
	bool bHoldNote = false;
};
