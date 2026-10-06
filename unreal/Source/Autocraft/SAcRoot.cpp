#include "SAcRoot.h"

#include "AcHudStyle.h"
#include "SAcPopups.h"
#include "SAcResourceBar.h"
#include "SAcScoreboard.h"
#include "SAcVictoryBanner.h"

#include "Layout/WidgetPath.h"
#include "Widgets/Layout/SDPIScaler.h"

#include "Types.h"

void SAcRoot::Construct(const FArguments& InArgs)
{
	PixelsPerPointAttr = InArgs._PixelsPerPoint;
	EngineScaleAttr = InArgs._EngineScale;
	FAcHudStyle::Initialize();
	SetVisibility(EVisibility::SelfHitTestInvisible);

	ChildSlot
	[
		SNew(SDPIScaler)
		.DPIScale_Lambda([this]
		{
			// What the engine and the window already scale this widget by
			// (its DPI curve, the screen): measured, as the engine's own
			// numbers do not add up to it exactly; asked before the first paint.
			const float Applied = GetPaintSpaceGeometry().Scale;
			const float Parent = Applied > 0.0f && GetPaintSpaceGeometry().GetLocalSize().X > 0 ? Applied : EngineScaleAttr.Get();
			return PixelsPerPointAttr.Get() / FMath::Max(Parent, 0.01f);
		})
		[
			SAssignNew(Layers, SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible)
		]
	];

	// HUD.buildBar: 14 points in from the right, 10 down.
	Layers->AddSlot(AcHudLayer::Bar)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(0, 10, 14, 0)
	[
		SAssignNew(Bar, SAcResourceBar)
	];
	// HUD.buildScoreboard: 10 points left of the bar.
	Layers->AddSlot(AcHudLayer::Bar)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(0, 10, 14 + SAcResourceBar::Width + 10, 0)
	[
		SAssignNew(Scoreboard, SAcScoreboard)
	];
	Layers->AddSlot(AcHudLayer::Banner)
	[
		SAssignNew(Banner, SAcVictoryBanner)
	];
	Layers->AddSlot(AcHudLayer::Popups)
	[
		SAssignNew(Popups, SAcPopups)
	];
}

void SAcRoot::Update(const ac::GameState& State, const int64 LocalPlayer, const double RealDelta, const double Now)
{
	if (LocalPlayer >= 0 && LocalPlayer < (int64)State.players.size())
	{
		const ac::Player& P = State.players[(size_t)LocalPlayer];
		Bar->Update(P.ore, P.hydrogen, P.supplyUsed, P.supplyCap, RealDelta);
	}
	Scoreboard->Update(State, LocalPlayer);
	Banner->Update(State, Now);
	Popups->Update(Now);
}

void SAcRoot::Popup(const int64 Amount, const FVector2f At, const double Now)
{
	Popups->Add(Amount, At, Now);
}

void SAcRoot::Reset()
{
	Bar->Reset();
}

void SAcRoot::SetDriving(const bool bDriving)
{
	Scoreboard->SetVisibility(bDriving ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
}

SOverlay::FScopedWidgetSlotArguments SAcRoot::AddLayer(const int32 ZOrder)
{
	return Layers->AddSlot(ZOrder);
}

void SAcRoot::RemoveLayer(const TSharedRef<SWidget>& Widget)
{
	Layers->RemoveSlot(Widget);
}
