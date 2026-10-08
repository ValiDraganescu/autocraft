#include "SAcConsole.h"

#include "AcCabArt.h"
#include "AcConsolePaint.h"
#include "AcConsoleWarp.h"
#include "AcHudStyle.h"
#include "AcIcons.h"
#include "AcLog.h"

#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Rules.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SCanvas.h"

namespace
{
	using FPaint = FAcConsolePaint;

	FLinearColor Rgb(double R, double G, double B, double A = 1) { return FAcHudStyle::Srgb(R, G, B, A); }
	FLinearColor Gray(double W, double A = 1) { return FAcHudStyle::Srgb(W, W, W, A); }
	FLinearColor Alpha(const FLinearColor& C, double A) { return FPaint::WithAlpha(C, A); }

	/// `Console.hologram`: cyan while healthy, then the health colours.
	FLinearColor Hologram(double F) { return F > 0.6 ? FAcHudStyle::Cyan() : FAcHudStyle::Health(F); }
	/// `PilotInfo.Tone.color`.
	FLinearColor ToneColor(EAcTone T)
	{
		switch (T)
		{
		case EAcTone::Ready:
		case EAcTone::Working: return Rgb(0.45, 1, 0.55);
		case EAcTone::Blocked: return Rgb(1, 0.72, 0.25);
		case EAcTone::Enemy: return Rgb(1, 0.3, 0.25);
		default: return Rgb(0.45, 0.8, 1, 0.9);
		}
	}
	/// `PilotOverlay.blocked`: the note's colour.
	FLinearColor NoteColor() { return Rgb(1, 0.72, 0.25); }
	constexpr double TipTime = 6.0;

	FString Upper(const FString& S) { return S.ToUpper(); }

	TAutoConsoleVariable<bool> CVarConsoleWarp(TEXT("ac.ConsoleWarp"), true,
		TEXT("Set the console's faces in perspective as Swift does (0: flat, for checking the flat drawing)."));

	TAutoConsoleVariable<float> CVarConsoleWarpScale(TEXT("ac.ConsoleWarpScale"), 2.0f,
		TEXT("Least pixels per point the console's faces are drawn at before the warp (finer lines on a 1x screen)."));

	/// Cab.lampColor, Cab.lockedColor.
	FLinearColor LampColor() { return Rgb(1, 0.62, 0.15); }
	FLinearColor LockedColor() { return Rgb(0.3, 1, 0.35); }
}

// MARK: - Construction and layout

void SAcConsole::Construct(const FArguments& InArgs)
{
	OnCardButton = InArgs._OnCardButton;
	OnViewSwitch = InArgs._OnViewSwitch;
	SetCanTick(true);
	SetVisibility(EVisibility::Visible);
	// The screens' live parts sit in the strip of faces laid out flat, which
	// the warp draws (y down from the strip's top, FaceTop).
	auto TopLeft = [this](const FAcRect& R) { return FVector2D(R.X, Lay.FaceTop - R.MaxY()); };
	SAssignNew(Strip, SAcConsoleFaces)
	.OnPaintFlat([this](const FGeometry& G, FSlateWindowElementList& Out, int32 Layer) { return PaintFaces(G, Out, Layer); })
	[
		SAssignNew(Canvas, SCanvas)
		+ SCanvas::Slot()
			.Position_Lambda([this, TopLeft] { return TopLeft(Lay.Minimap); })
			.Size_Lambda([this] { return FVector2D(Lay.Minimap.W, Lay.Minimap.H); })
		[
			SAssignNew(MinimapBox, SBox)
		]
		+ SCanvas::Slot()
			.Position_Lambda([this, TopLeft] { return TopLeft(FAcCabLayout::DeckScreen(Lay.Blocks[0])); })
			.Size_Lambda([this]
			{
				const FAcRect R = FAcCabLayout::DeckScreen(Lay.Blocks[0]);
				return Lay.Blocks[0].W > 60 ? FVector2D(R.W, R.H) : FVector2D::ZeroVector;
			})
		[
			SAssignNew(DeckBox, SBox)
		]
	];
	Warp = MakeShared<FAcConsoleWarp>(Strip.ToSharedRef());
}

SAcConsole::~SAcConsole() = default;

void SAcConsole::MakeArt(FArt& Art, const FAcArtImage& Image, const TCHAR* Name)
{
	Art.Texture.Reset(AcCabArt::ToTexture(Image, Name));
	Art.Brush = MakeShared<FSlateBrush>();
	Art.Brush->DrawAs = ESlateBrushDrawType::Image;
	Art.Brush->ImageSize = Image.Points;
	if (Art.Texture) Art.Brush->SetResourceObject(Art.Texture.Get());
	else Art.Brush->TintColor = FSlateColor(FLinearColor::Transparent);
}

void SAcConsole::Relayout(const FVector2D Size)
{
	const double T0 = FPlatformTime::Seconds();
	bLaidWarped = CVarConsoleWarp.GetValueOnGameThread();
	Lay = FAcCabLayout::Make(Size, MinimapSize, bLaidWarped);
	LaidOutSize = Size;
	bLaidOut = true;
	Strip->SetSize(FVector2D(Lay.Size.X, Lay.FaceTop));
	bFrameMade = false;
	Frame.Reset();
	if (Look == EAcConsoleStyle::Cab) MakeFrame();
	MakeArt(Dash, AcCabArt::DashTop(Lay), TEXT("AcConsoleDash"));
	static const TCHAR* FaceNames[3] = {TEXT("AcConsoleFaceLeft"), TEXT("AcConsoleFaceCenter"), TEXT("AcConsoleFaceRight")};
	for (int32 K = 0; K < 3; ++K) MakeArt(Faces[K], AcCabArt::Face(K, Lay), FaceNames[K]);
	if (!ButtonOn.Get())
	{
		MakeArt(ButtonOn, AcCabArt::ButtonFace(FAcCabLayout::Button, true), TEXT("AcConsoleButtonOn"));
		MakeArt(ButtonOff, AcCabArt::ButtonFace(FAcCabLayout::Button, false), TEXT("AcConsoleButtonOff"));
		MakeArt(Scan, AcCabArt::Scanlines(FVector2D(64, 240)), TEXT("AcConsoleScan"));
		// Console.drawViewSwitch: the plate round the switch, 4 and 3 points out.
		MakeArt(SwitchPlate, AcCabArt::Plate(FVector2D(168 + 8, 24 + 6), FAcCuts{8, 2, 2, 8}, false, 14),
			TEXT("AcConsoleSwitchPlate"));
	}
	UE_LOG(LogAutocraft, Log, TEXT("console: laid out for %.0fx%.0f points%s, cover %.0f, art in %.0f ms"), Size.X, Size.Y,
		bLaidWarped ? TEXT("") : TEXT(" (flat)"), Lay.Cover(), (FPlatformTime::Seconds() - T0) * 1000);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAcConsole::MakeFrame()
{
	// Cab.node(frame: true), drawn when the cab is first shown at this size.
	if (bFrameMade || !bLaidOut) return;
	const double T0 = FPlatformTime::Seconds();
	bFrameMade = true;
	Frame.Reset();
	int32 K = 0;
	for (const FAcArtBand& Band : AcCabArt::Framed(Lay))
	{
		TPair<FAcRect, FArt>& Part = Frame.AddDefaulted_GetRef();
		Part.Key = Band.At;
		MakeArt(Part.Value, Band.Image, *FString::Printf(TEXT("AcConsoleCabFrame%d"), K++));
	}
	UE_LOG(LogAutocraft, Log, TEXT("console: cab frame in %.0f ms"), (FPlatformTime::Seconds() - T0) * 1000);
}

void SAcConsole::SetStyle(const EAcConsoleStyle InStyle)
{
	if (Look == InStyle) return;
	Look = InStyle;
	if (Look == EAcConsoleStyle::Cab) MakeFrame();
	Hovered.Reset();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAcConsole::SetLamps(const EAcCabLamps InLamps)
{
	if (Lamps == InLamps) return;
	Lamps = InLamps;
	LampsSince = FPlatformTime::Seconds();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAcConsole::SetMinimapSize(const FVector2D Size)
{
	if (Size.Equals(MinimapSize, 0.01) || Size.X <= 0 || Size.Y <= 0) return;
	MinimapSize = Size;
	if (bLaidOut) Relayout(LaidOutSize);
}

void SAcConsole::SetMinimap(TSharedPtr<SWidget> Widget)
{
	MinimapBox->SetContent(Widget ? Widget.ToSharedRef() : SNullWidget::NullWidget);
}

void SAcConsole::SetDeck(TSharedPtr<SWidget> Widget)
{
	DeckBox->SetContent(Widget ? Widget.ToSharedRef() : SNullWidget::NullWidget);
}

void SAcConsole::SetDeckTip(TArray<FAcDeckTipLine> Lines)
{
	DeckTip = MoveTemp(Lines);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAcConsole::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
	SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
	const FVector2D Size = FVector2D(Geometry.GetLocalSize());
	if (Size.X < 100 || Size.Y < 100) return;
	if (!bLaidOut || bLaidWarped != CVarConsoleWarp.GetValueOnGameThread())
	{
		Relayout(bLaidOut ? LaidOutSize : Size);
		return;
	}
	if (Size.Equals(LaidOutSize, 0.5)) return;
	// GameController.viewResized: wait for the size to settle (0.15 s).
	if (!Size.Equals(PendingSize, 0.5))
	{
		PendingSize = Size;
		PendingSince = CurrentTime;
	}
	else if (CurrentTime - PendingSince >= 0.15)
	{
		Relayout(Size);
	}
}

// MARK: - State

void SAcConsole::Show(const TOptional<FAcCardInfo>& InInfo, const double InNow)
{
	Now = InNow;
	const TOptional<FString> Note = InInfo ? InInfo->Note : TOptional<FString>();
	if (Note != ShownNote)
	{
		ShownNote = Note;
		NoteSince = Now;
	}
	if (!InInfo || InInfo->Tips.IsEmpty()) TipsSince.Reset();
	else if (!TipsSince) TipsSince = Now;
	if (InInfo.IsSet() != Info.IsSet() || (InInfo && Info && InInfo->LayoutKey() != Info->LayoutKey())) Hovered.Reset();
	Info = InInfo;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAcConsole::SetView(const bool bInThird)
{
	if (bThird == bInThird) return;
	bThird = bInThird;
	Invalidate(EInvalidateWidgetReason::Paint);
}

FLinearColor SAcConsole::Accent() const
{
	return Look == EAcConsoleStyle::Rts ? Rgb(0.45, 0.75, 1) : Rgb(0.35, 0.95, 1);
}

const FSlateBrush* SAcConsole::GlowBrush(const FString& Icon) const
{
	if (const TSharedPtr<FSlateBrush>* Found = GlowBrushes.Find(Icon)) return Found->Get();
	TSharedPtr<FSlateBrush> B;
	static UMaterialInterface* Additive = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/UI/Materials/M_AcUiAdditive.M_AcUiAdditive"), nullptr, LOAD_NoWarn);
	const FSlateBrush* Plain = FAcIcons::Brush(Icon);
	UTexture* Texture = Plain ? Cast<UTexture>(Plain->GetResourceObject()) : nullptr;
	if (Additive && Texture)
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Additive, GetTransientPackage());
		Mid->SetTextureParameterValue(TEXT("Tex"), Texture);
		GlowMaterials.Add(Icon, TStrongObjectPtr<UMaterialInstanceDynamic>(Mid));
		B = MakeShared<FSlateBrush>();
		B->DrawAs = ESlateBrushDrawType::Image;
		B->ImageSize = Plain->ImageSize;
		B->SetResourceObject(Mid);
	}
	else if (!Additive)
	{
		UE_LOG(LogAutocraft, Warning, TEXT("console: no M_AcUiAdditive (run Tools/Editor/make_console_materials.py)"));
	}
	GlowBrushes.Add(Icon, B);
	return B.Get();
}

bool SAcConsole::IsNoteShown() const
{
	return ShownNote.IsSet() && (bHoldNote || Now - NoteSince < 2.1);
}

// MARK: - Hit tests

TOptional<FVector2D> SAcConsole::FlatAt(const FVector2D P, int32* Face) const
{
	if (!bLaidOut) return {};
	FVector2D Q;
	const int32 K = Lay.FaceAt(Lay.FromSlate(P), &Q);
	if (Face) *Face = K;
	return K >= 0 ? TOptional<FVector2D>(Q) : TOptional<FVector2D>();
}

TOptional<FVector2D> SAcConsole::MinimapPoint(const FVector2D P, const double Pad) const
{
	if (!bLaidOut || !MinimapBox || MinimapBox->GetChildren()->Num() == 0) return {};
	// Back from the left face as seen to where the minimap is laid out.
	const FVector2D Q = Lay.Faces[0].Projection().Inverse()(Lay.FromSlate(P));
	const double K = FMath::Max(Lay.MinimapScale, 0.01);
	const FVector2D Local((Q.X - Lay.Minimap.X) / K, (Q.Y - Lay.Minimap.Y) / K);
	const FVector2D Size = MinimapSize;
	if (Local.X < -Pad || Local.Y < -Pad || Local.X > Size.X + Pad || Local.Y > Size.Y + Pad) return {};
	return FVector2D(FMath::Clamp(Local.X, 0.0, Size.X), FMath::Clamp(Local.Y, 0.0, Size.Y));
}

TOptional<int32> SAcConsole::ButtonAt(const FVector2D P) const
{
	if (!bLaidOut || !Info) return {};
	// Back from the face as seen to where the card is laid out.
	const FVector2D Q = Lay.Faces[2].Projection().Inverse()(Lay.FromSlate(P));
	if (!Lay.Faces[2].Flat.Contains(Q)) return {};
	for (int32 I = 0; I < Info->Buttons.Num(); ++I)
	{
		const int32 Slot = Info->Buttons[I].Slot;
		if (Slot >= 0 && Slot < FAcCabLayout::Columns * FAcCabLayout::Rows && Lay.SlotRect(Slot).Contains(Q)) return I;
	}
	return {};
}

bool SAcConsole::ViewSwitchAt(const FVector2D P) const
{
	return bLaidOut && Lay.ViewSwitch.Contains(Lay.FromSlate(P));
}

bool SAcConsole::Covers(const FVector2D P) const
{
	if (!bLaidOut) return false;
	const FVector2D Q = Lay.FromSlate(P);
	if (Q.Y < 0) return false;
	if (Lay.ViewSwitch.Contains(Q)) return true;
	// In a cab the frame round the window covers the view too.
	if (Look == EAcConsoleStyle::Cab) return !(Lay.Window.Contains(Q) && Q.Y > Lay.DashTop(Q.X));
	return Q.Y <= Lay.DashTop(Q.X);
}

void SAcConsole::Hover(const TOptional<FVector2D> P)
{
	const TOptional<int32> B = P ? ButtonAt(*P) : TOptional<int32>();
	if (B == Hovered) return;
	Hovered = B;
	Invalidate(EInvalidateWidgetReason::Paint);
}

// MARK: - Input handed to the screens' widgets

bool SAcConsole::ForwardAt(const FVector2D P, FForward& Out, FVector2D& Local) const
{
	int32 Face = -1;
	const TOptional<FVector2D> Q = FlatAt(P, &Face);
	if (!Q) return false;
	const FAcRect Deck = FAcCabLayout::DeckScreen(Lay.Blocks[0]);
	const struct { TSharedPtr<SBox> Box; FAcRect Rect; int32 Face; } Slots[2] = {
		{MinimapBox, Lay.Minimap, 0}, {DeckBox, Deck, 0}};
	for (const auto& S : Slots)
	{
		// The minimap also takes presses up to 6 of its points off its edge
		// (GameController.jumpFromMinimap: `minimapPoint(at:pad: 6)`).
		const double Pad = S.Box.Get() == MinimapBox.Get() ? 6 * Lay.MinimapScale : 0;
		if (Face != S.Face || !S.Box || S.Rect.IsEmpty() || !S.Rect.Inset(-Pad, -Pad).Contains(*Q)) continue;
		if (S.Box.Get() == DeckBox.Get() && Lay.Blocks[0].W <= 60) continue;
		FChildren* Children = S.Box->GetChildren();
		if (!Children || Children->Num() == 0) continue;
		const TSharedRef<SWidget> W = Children->GetChildAt(0);
		if (W == SNullWidget::NullWidget || !W->GetVisibility().IsHitTestVisible()) continue;
		Out.Widget = W;
		Out.Rect = S.Rect;
		Out.Face = S.Face;
		Local = FVector2D(Q->X - S.Rect.X, S.Rect.MaxY() - Q->Y);
		return true;
	}
	return false;
}

FVector2D SAcConsole::ForwardLocal(const FForward& F, const FVector2D P) const
{
	const FVector2D Q = Lay.Faces[FMath::Clamp(F.Face, 0, 2)].Projection().Inverse()(Lay.FromSlate(P));
	return FVector2D(Q.X - F.Rect.X, F.Rect.MaxY() - Q.Y);
}

FReply SAcConsole::ForwardTo(const FForward& F, const FGeometry& Geometry, const FPointerEvent& Event,
	FReply (SWidget::*Handler)(const FGeometry&, const FPointerEvent&))
{
	// The widget's own box at 1 Slate unit a point, the event's positions in it.
	const FVector2D Local = ForwardLocal(F, FVector2D(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())));
	const FVector2D Last = ForwardLocal(F, FVector2D(Geometry.AbsoluteToLocal(Event.GetLastScreenSpacePosition())));
	const FGeometry Box = FGeometry::MakeRoot(FVector2f(F.Rect.W, F.Rect.H), FSlateLayoutTransform());
	const FPointerEvent Moved(Event, Local, Last);
	FReply Reply = (F.Widget.Get()->*Handler)(Box, Moved);
	if (!Reply.IsEventHandled()) return Reply;
	// A drag the widget holds on to: the console takes the capture and
	// hands it the rest of the drag.
	if (Reply.GetMouseCaptor().IsValid() || Reply.ShouldReleaseMouse())
	{
		const bool bCapture = Reply.GetMouseCaptor().IsValid();
		Reply = FReply::Handled();
		if (bCapture)
		{
			Captured = F;
			Reply.CaptureMouse(SharedThis(this));
		}
		else
		{
			Captured.Reset();
			Reply.ReleaseMouseCapture();
		}
	}
	return Reply;
}

void SAcConsole::ForwardHover(const TOptional<FForward>& Over, const FGeometry& Geometry, const FPointerEvent& Event)
{
	const SWidget* Was = Hovering ? Hovering->Widget.Get() : nullptr;
	const SWidget* Now_ = Over ? Over->Widget.Get() : nullptr;
	if (Was == Now_) return;
	if (Hovering && Hovering->Widget) Hovering->Widget->OnMouseLeave(Event);
	Hovering = Over;
	if (Over && Over->Widget)
	{
		const FGeometry Box = FGeometry::MakeRoot(FVector2f(Over->Rect.W, Over->Rect.H), FSlateLayoutTransform());
		const FVector2D Local = ForwardLocal(*Over, FVector2D(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())));
		Over->Widget->OnMouseEnter(Box, FPointerEvent(Event, Local, Local));
	}
}

FReply SAcConsole::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2D P = FVector2D(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	FForward F;
	FVector2D Local;
	if (ForwardAt(P, F, Local))
	{
		const FReply Reply = ForwardTo(F, Geometry, Event, &SWidget::OnMouseButtonDown);
		if (Reply.IsEventHandled()) return Reply;
	}
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return Covers(P) ? FReply::Handled() : FReply::Unhandled();
	if (ViewSwitchAt(P))
	{
		OnViewSwitch.ExecuteIfBound();
		return FReply::Handled();
	}
	if (const TOptional<int32> B = ButtonAt(P))
	{
		OnCardButton.ExecuteIfBound(*B);
		return FReply::Handled();
	}
	// The card area and the rest of the dashboard swallow the click.
	return Covers(P) ? FReply::Handled() : FReply::Unhandled();
}

FReply SAcConsole::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Captured)
	{
		const FForward F = *Captured;
		FReply Reply = ForwardTo(F, Geometry, Event, &SWidget::OnMouseButtonUp);
		if (Captured && Event.GetPressedButtons().Num() == 0)
		{
			// The widget let go of the drag (or never says): so does the console.
			Captured.Reset();
			Reply = FReply::Handled().ReleaseMouseCapture();
		}
		return Reply;
	}
	const FVector2D P = FVector2D(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	FForward F;
	FVector2D Local;
	if (ForwardAt(P, F, Local)) return ForwardTo(F, Geometry, Event, &SWidget::OnMouseButtonUp);
	return FReply::Unhandled();
}

FReply SAcConsole::OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2D P = FVector2D(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	FForward F;
	FVector2D Local;
	if (ForwardAt(P, F, Local)) return ForwardTo(F, Geometry, Event, &SWidget::OnMouseButtonDoubleClick);
	return OnMouseButtonDown(Geometry, Event);
}

FReply SAcConsole::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2D P = FVector2D(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	if (Captured)
	{
		const FForward F = *Captured;
		return ForwardTo(F, Geometry, Event, &SWidget::OnMouseMove);
	}
	Hover(P);
	FForward F;
	FVector2D Local;
	const bool bOver = ForwardAt(P, F, Local);
	ForwardHover(bOver ? TOptional<FForward>(F) : TOptional<FForward>(), Geometry, Event);
	if (bOver) return ForwardTo(F, Geometry, Event, &SWidget::OnMouseMove);
	return FReply::Unhandled();
}

FReply SAcConsole::OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2D P = FVector2D(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	FForward F;
	FVector2D Local;
	if (ForwardAt(P, F, Local)) return ForwardTo(F, Geometry, Event, &SWidget::OnMouseWheel);
	return FReply::Unhandled();
}

void SAcConsole::OnMouseLeave(const FPointerEvent& Event)
{
	SCompoundWidget::OnMouseLeave(Event);
	Hover({});
	if (Hovering && Hovering->Widget) Hovering->Widget->OnMouseLeave(Event);
	Hovering.Reset();
}

void SAcConsole::OnMouseCaptureLost(const FCaptureLostEvent& Event)
{
	SCompoundWidget::OnMouseCaptureLost(Event);
	if (Captured && Captured->Widget) Captured->Widget->OnMouseCaptureLost(Event);
	Captured.Reset();
}

FCursorReply SAcConsole::OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const
{
	const FVector2D P = FVector2D(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	FForward F;
	FVector2D Local;
	if (Captured) F = *Captured;
	else if (!ForwardAt(P, F, Local)) return FCursorReply::Unhandled();
	const FVector2D L = ForwardLocal(F, P);
	const FGeometry Box = FGeometry::MakeRoot(FVector2f(F.Rect.W, F.Rect.H), FSlateLayoutTransform());
	return F.Widget->OnCursorQuery(Box, FPointerEvent(Event, L, L));
}

// MARK: - Paint

int32 SAcConsole::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	if (!bLaidOut) return Layer;
	// The faces laid out flat into their target (as SRetainerWidget does from
	// its paint: the target is drawn before the window), at this widget's
	// pixels per point.
	const float Scale = Geometry.GetAccumulatedLayoutTransform().GetScale();
	Warp->Render(FVector2D(Lay.Size.X, Lay.FaceTop), FMath::Max(Scale, CVarConsoleWarpScale.GetValueOnGameThread()),
		(float)FApp::GetDeltaTime());
	FPaint Base(Out, Geometry, Layer, Lay.Size.Y);
	PaintDash(Base);
	// The faces as seen (Console.warpFaces), then what stands over them flat.
	const int32 Warped = Base.NextLayer() + 1;
	Warp->Paint(Out, Warped, Geometry, Lay);
	FPaint Over(Out, Geometry, Warped + 1, Lay.Size.Y);
	PaintOver(Over);
	return Over.NextLayer();
}

void SAcConsole::PaintDash(FPaint& P) const
{
	if (Look != EAcConsoleStyle::Cab)
	{
		// The dashboard's top alone (the view frame stands round the view).
		if (const FSlateBrush* B = Dash.Get()) P.Image(B, FAcRect(0, 0, B->ImageSize.X, B->ImageSize.Y));
		return;
	}
	// A machine's cab: the frame round the window, and its two lamps
	// (Console.buildCab, setLamps): amber pulsing while it works, green
	// while locked down, amber and dim at rest.
	for (const TPair<FAcRect, FArt>& Part : Frame)
	{
		if (const FSlateBrush* B = Part.Value.Get()) P.Image(B, Part.Key);
	}
	const FLinearColor Color = Lamps == EAcCabLamps::Locked ? LockedColor() : LampColor();
	double A = Lamps == EAcCabLamps::Idle ? 0.6 : 1.0;
	if (Lamps == EAcCabLamps::Working)
	{
		// .fadeAlpha(to: 0.45, duration: 0.35), .fadeAlpha(to: 1, duration: 0.35), forever.
		const double T = FMath::Fmod(FPlatformTime::Seconds() - LampsSince, 0.7);
		A = T < 0.35 ? 1 - 0.55 * T / 0.35 : 0.45 + 0.55 * (T - 0.35) / 0.35;
	}
	for (int32 K = 0; K < 2; ++K)
	{
		const FVector2D L = Lay.Lamp(K);
		const FAcRect R(L.X - 3, L.Y - 9, 6, 18);
		// SKShapeNode, glowWidth 5: the halo, then the lens.
		P.Stroke(R, 3, Alpha(Color, A), 0.01, 5);
		P.FillRounded(R, 3, Alpha(Color, A));
	}
}

int32 SAcConsole::PaintFaces(const FGeometry& Geometry, FSlateWindowElementList& Out, const int32 Layer) const
{
	if (!bLaidOut) return Layer;
	// The strip: y up from its foot, FaceTop high.
	FPaint P(Out, Geometry, Layer, Lay.FaceTop);
	PaintBase(P);
	if (Info)
	{
		// The selection stays on its screen (the hazard stripes are 9 points out).
		const FAcRect& C = Lay.CenterWell;
		const FVector2f TL((float)C.X - 6, (float)(Lay.FaceTop - C.MaxY()));
		const FSlateRect Clip = TransformRect(Geometry.GetAccumulatedRenderTransform(),
			FSlateRotatedRect(FSlateRect(TL.X, TL.Y, TL.X + (float)C.W + 12, TL.Y + (float)C.H + 14))).ToBoundingRect();
		Out.PushClip(FSlateClippingZone(Clip));
		PaintCenter(P, *Info);
		Out.PopClip();
	}
	PaintCard(P);
	return P.NextLayer();
}

void SAcConsole::PaintBase(FPaint& P) const
{
	// The faces drawn flat.
	for (int32 K = 0; K < 3; ++K)
	{
		if (const FSlateBrush* B = Faces[K].Get()) P.Image(B, Lay.Faces[K].Flat);
	}
	// Text the Swift art draws with Core Graphics: the deck's station name
	// (`Cab.deckBay`) and the centre face's stencil (`Cab.centerFace`).
	// NSString.draw(at:) puts the line's foot at the point: the baseline is
	// a descender (about 0.2 em) higher.
	auto Kerned = [](const FString& Text, double Size, double Kern, const FLinearColor& Color)
	{
		FSlateFontInfo Font = FAcHudStyle::Font((float)Size);
		FAcHudText T(Text, Font, Color);
		T.Accent = FLinearColor::Transparent;
		T.Kern = (float)Kern;
		return T;
	};
	const FAcRect& Block = Lay.Blocks[0];
	if (Block.W > 60)
	{
		const double Size = FMath::Min(10.0, (Block.W - 30) / 10);
		FAcHudText Name = Kerned(TEXT("KSTR 88.7 STARDUST"), Size, 1.5, FAcHudStyle::Cyan());
		Name.ShadowColor = Alpha(FAcHudStyle::Cyan(), 0.35);  // its glow (a 4-point blur in Swift)
		const double W = Name.Measure().X;
		P.Text(Name, FVector2D(FMath::RoundHalfFromZero(Block.MidX() - W / 2), Block.MaxY() - 12.5 + 0.2 * Size));
	}
	const FAcRect& C = Lay.CenterWell;
	P.Text(Kerned(TEXT("FRONTIER · UPLINK MK II"), 10, 2, Gray(1, 0.3)), FVector2D(C.MinX() + 14, C.MinY() - 11 + 2));
}

void SAcConsole::PaintKeyed(FPaint& P, const FString& Text, const double Size, const FLinearColor& Key,
	const FLinearColor& Rest, const FVector2D At) const
{
	// Console.setKeyed: before each double space the key, the rest after;
	// the dots between parts dimmed.
	const FString Dot = TEXT("   ·   ");
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, *Dot, false);
	double X = At.X;
	auto Run = [&](const FString& S, const FLinearColor& Color)
	{
		if (S.IsEmpty()) return;
		P.Text(S, Size, Color, FVector2D(X, At.Y), EAcHAlign::Left, true, false);
		X += FPaint::TextWidth(S, Size);
	};
	for (int32 I = 0; I < Parts.Num(); ++I)
	{
		if (I > 0) Run(Dot, Alpha(Rest, Rest.A * 0.45));
		const FString& Part = Parts[I];
		const int32 Cut = Part.Find(TEXT("  "));
		if (Cut != INDEX_NONE)
		{
			Run(Part.Left(Cut + 2), Key);
			Run(Part.Mid(Cut + 2), Rest);
		}
		else
		{
			Run(Part, Rest);
		}
	}
}

void SAcConsole::PaintCenter(FPaint& P, const FAcCardInfo& In) const
{
	// Console.buildCenter, with the live parts of `show` (bars, colours).
	const FAcRect& R = Lay.CenterWell;
	const FLinearColor White = Gray(0.95);
	const FLinearColor Acc = Accent();
	const double F = FMath::Clamp(In.Hp / FMath::Max(In.MaxHp, 1.0), 0.0, 1.0);

	// The portrait: the model as a hologram tinted by health, glowing, under
	// scan lines; its hit points under it.
	const double Ws = FMath::Min(R.H - 34, 116.0);
	const FVector2D Mid(R.MinX() + 14 + Ws / 2, R.MinY() + 24 + Ws / 2);
	const bool bRts = Look == EAcConsoleStyle::Rts;
	if (const FSlateBrush* Icon = In.Icon ? FAcIcons::Brush(*In.Icon) : nullptr)
	{
		// Driving: the model in its health colour, no glow or scan lines.
		const FLinearColor Holo = bRts ? Hologram(F) : FAcHudStyle::Health(F);
		P.Image(Icon, FAcRect(Mid.X - Ws / 2, Mid.Y - Ws / 2, Ws, Ws), FPaint::BlendTint(Holo, 0.85, 0.9));
	}
	if (const FSlateBrush* Icon = bRts && In.Icon ? FAcIcons::Brush(*In.Icon) : nullptr)
	{
		const FLinearColor Holo = Hologram(F);
		// The glow: a copy 6% larger in the colour, added at 0.3.
		const double G = Ws * 1.06;
		if (const FSlateBrush* Glow = GlowBrush(*In.Icon))
		{
			P.Image(Glow, FAcRect(Mid.X - G / 2, Mid.Y - G / 2, G, G), FPaint::BlendTint(Holo, 1, 0.3));
		}
		P.Image(Scan.Get(), FAcRect(R.MinX() + 8, R.MinY() + 20, Ws + 12, Ws + 8));
	}
	P.Text(FString::Printf(TEXT("%d / %d"), (int32)FMath::RoundHalfFromZero(In.Hp), (int32)In.MaxHp), 16, FAcHudStyle::Health(F),
		FVector2D(R.MinX() + 14 + Ws / 2, R.MinY() + 8), EAcHAlign::Center, false, false);

	const double X0 = R.MinX() + Ws + 34;
	const double Right = R.MaxX() - 14;
	// Swift lets a long title or status run on; Barlow is wider than DIN
	// Condensed, so here they shrink a little (to 80%) and are cut at the
	// screen's edge (the clip in OnPaint).
	auto Fit = [](const FString& Text, double Size, double Room)
	{
		const double W = FPaint::TextWidth(Text, Size);
		// Whole points: Slate lays out runs of fractional sizes unevenly.
		return Room > 0 && W > Room ? FMath::FloorToDouble(Size * FMath::Max(Room / W, 0.8)) : Size;
	};
	const FString Title = Upper(In.Title);
	const double TitleSize = Fit(Title, 22, Right - X0 - (In.Level ? 110 : 0));
	P.Text(Title, TitleSize, White, FVector2D(X0, R.MaxY() - 30));
	// The level: a badge after the name, then its XP bar to the next.
	if (In.Level)
	{
		const FLinearColor Gold = FAcHudStyle::LevelGold();
		const double NameW = FPaint::TextWidth(Title, TitleSize);
		const FAcRect Badge(X0 + NameW + 12, R.MaxY() - 31, In.Level->Level >= 10 ? 46 : 40, 19);
		P.Fill(Badge, Alpha(Gold, 0.18));
		P.Stroke(Badge, 2, Gold, 1.2);
		P.Text(FString::Printf(TEXT("LV %d"), In.Level->Level), 14, Gold, FVector2D(Badge.MidX(), Badge.MinY() + 4), EAcHAlign::Center);
		const double X = Badge.MaxX() + 10;
		const FAcRect Xp(X, Badge.MinY() + 6, FMath::Max(FMath::Min(Right - X, 240.0), 50.0), 6);
		P.Fill(Xp.Inset(-1, -1), Gray(0, 0.6));
		P.Stroke(Xp.Inset(-1, -1), 1, Alpha(Gold, 0.45));
		P.Fill(FAcRect(Xp.X, Xp.Y, Xp.W * In.Level->Fraction, Xp.H), Gold);
		P.Text(In.Level->Text, 12, Gold, FVector2D(Xp.MaxX(), Xp.MinY() - 15), EAcHAlign::Right);
	}
	P.Text(In.Status, Fit(In.Status, 17, Right - X0), bRts ? FAcHudStyle::Soft() : Gray(0.72), FVector2D(X0, R.MaxY() - 50));

	// The cockpit's tips along the foot, under a rule; the rest stands on it.
	const double Lift = In.Tips.IsEmpty() ? 0 : 20;
	const double Y = R.MinY() + 16 + Lift;
	const double Bay = In.Cargo ? 160 : 0;
	FVector2D Origin(0, 0);
	double Width = 0;
	if (!In.Queue.IsEmpty())
	{
		// The queue: the one in training large with its bar, the rest small after it.
		const double Big = 56, Small = 38;
		auto Slot = [&](const FString* Icon, const FAcRect& Box, bool bActive)
		{
			P.Fill(Box, Rgb(0.02, 0.04, 0.07, 0.9));
			P.Stroke(Box, 3, bActive ? Acc : Gray(0.35), bActive ? 1.5 : 1);
			if (const FSlateBrush* B = Icon ? FAcIcons::Brush(*Icon) : nullptr) P.Image(B, Box.Inset(2, 2));
		};
		Slot(&In.Queue[0], FAcRect(X0, Y + 12, Big, Big), true);
		for (int32 K = 1; K < (int32)ac::Rules::maxQueue; ++K)
		{
			const FAcRect Box(X0 + Big + 10 + (K - 1) * (Small + 6), Y + 12, Small, Small);
			Slot(K < In.Queue.Num() ? &In.Queue[K] : nullptr, Box, false);
		}
		Origin = FVector2D(X0, Y);
		Width = FMath::Min(Right - X0, Big + 10 + 4 * (Small + 6) - 6);
	}
	else if (In.Progress)
	{
		Origin = FVector2D(X0, Y + 8);
		Width = FMath::Max(FMath::Min(Right - X0 - Bay, 300.0), 60.0);
	}
	if (Width > 0)
	{
		const FAcRect Back(Origin.X, Origin.Y, Width, 8);
		P.Fill(Back, Gray(0, 0.6));
		P.Stroke(Back, 1, Alpha(Acc, 0.4));
		if (In.Progress)
		{
			const FAcRect Bar(Origin.X, Origin.Y, Width * FMath::Clamp(*In.Progress, 0.0, 1.0), 8);
			P.Fill(Bar.Inset(-1, -1), Alpha(Acc, 0.25));  // glowWidth 1
			P.Fill(Bar, Acc);
		}
	}
	const FString Label = In.Progress
		? FString::Printf(TEXT("%s  %d%%"), *In.ProgressLabel.Get(TEXT("")), (int32)(*In.Progress * 100))
		: In.ProgressLabel.Get(TEXT(""));
	P.Text(Label, 15, Acc, In.Queue.IsEmpty() ? FVector2D(X0, Origin.Y + 15) : FVector2D(X0 + 66, Y + 56), EAcHAlign::Left, false);

	// Energy (cockpit): under the status line, its numbers then a violet bar.
	if (In.Energy)
	{
		const FLinearColor Violet = FAcHudStyle::Energy();
		const FAcRect E(X0 + 118, R.MaxY() - 74, FMath::Max(FMath::Min(Right - X0 - 128, 240.0), 60.0), 7);
		P.Fill(E.Inset(-1, -1), Gray(0, 0.6));
		P.Stroke(E.Inset(-1, -1), 1, Alpha(Violet, 0.45));
		const double Ef = FMath::Clamp(*In.Energy / FMath::Max(In.MaxEnergy, 1.0), 0.0, 1.0);
		P.Fill(FAcRect(E.X, E.Y, E.W * Ef, E.H), Violet);
		P.Text(FString::Printf(TEXT("ENERGY  %d / %d"), (int32)FMath::FloorToDouble(*In.Energy), (int32)In.MaxEnergy), 15, Violet,
			FVector2D(X0, R.MaxY() - 76), EAcHAlign::Left, false);
	}

	// The quick info (cockpit) over the bars, marked in its tone, and the
	// tips under the rule, one at a time.
	if (!In.Tips.IsEmpty())
	{
		const double Py = R.MinY() + (In.Energy ? 70 : 80);
		if (In.Prompt)
		{
			const FLinearColor Tone = In.Tone ? ToneColor(*In.Tone) : Gray(0.62);
			if (In.Tone) P.Fill(FAcRect(X0, Py - 2, 3, 19), Tone);
			const FLinearColor Key = !In.Tone ? Gray(0.85) : *In.Tone == EAcTone::Blocked ? Tone : FLinearColor::White;
			PaintKeyed(P, *In.Prompt, In.Tone ? 22 : 18, Key, Tone, FVector2D(X0 + 10, Py));
		}
		P.Fill(FAcRect(X0 - 6, R.MinY() + 26, Right - X0 + 6, 1), Alpha(Acc, 0.22));
		P.Text(TEXT("TIP"), 13, Alpha(Acc, 0.85), FVector2D(X0, R.MinY() + 7));
		// E6: `Console.fitted` (a tip too wide for its room is cut at its
		// dots), the hold for renders (-AcTip=N, `AUTOCRAFT_TIP`), and the
		// change: out over 0.2 s, the next in over 0.3 s.
		const double TipRoom = Right - 48 - (X0 + 34);
		TArray<FString> Tips;
		for (const FString& Tip : In.Tips)
		{
			TArray<FString> Parts;
			Tip.ParseIntoArray(Parts, TEXT("   ·   "), false);
			FString Line;
			for (const FString& Part : Parts)
			{
				const FString Longer = Line.IsEmpty() ? Part : Line + TEXT("   ·   ") + Part;
				if (Line.IsEmpty() || TipRoom <= 0 || FPaint::TextWidth(Longer, 16) <= TipRoom) Line = Longer;
				else
				{
					Tips.Add(Line);
					Line = Part;
				}
			}
			Tips.Add(Line);
		}
		static const int32 HeldTip = [] { int32 N = -1; FParse::Value(FCommandLine::Get(), TEXT("AcTip="), N); return N; }();
		const double Run = Now - TipsSince.Get(Now);
		const int32 Step = HeldTip >= 0 ? HeldTip : (int32)(Run / TipTime);
		const double Into = HeldTip >= 0 ? TipTime : Run - Step * TipTime;
		const bool bOld = Step > 0 && Into < 0.2;
		const int32 K = (bOld ? Step - 1 : Step) % Tips.Num();
		const float TipAlpha = Step == 0 || HeldTip >= 0 ? 1.0f
			: bOld ? (float)(1 - Into / 0.2) : (float)FMath::Clamp((Into - 0.2) / 0.3, 0.0, 1.0);
		PaintKeyed(P, Tips[K], 16, Alpha(FAcHudStyle::KeyYellow(), TipAlpha), Gray(0.8, TipAlpha), FVector2D(X0 + 34, R.MinY() + 7));
		P.Text(FString::Printf(TEXT("%d / %d"), (Step % Tips.Num()) + 1, Tips.Num()), 13, Gray(0.55), FVector2D(Right, R.MinY() + 7), EAcHAlign::Right);
	}

	// Cargo (cockpit): a slim bay at the right over the rule.
	if (In.Cargo)
	{
		const FAcRect BayR(Right - 150, R.MinY() + 14 + Lift, 150, 30);
		P.Fill(BayR, Gray(0, 0.45));
		P.Stroke(BayR, 3, Alpha(Acc, 0.5));
		P.Text(TEXT("CARGO"), 12, Gray(0.7), FVector2D(BayR.MinX() + 8, BayR.MinY() + 10));
		if (const FSlateBrush* B = In.CargoIcon ? FAcIcons::Brush(*In.CargoIcon) : nullptr)
		{
			P.Image(B, FAcRect(BayR.MinX() + 60 - 13, BayR.MidY() - 13, 26, 26));
		}
		P.Text(*In.Cargo, 15, White, FVector2D(BayR.MinX() + 78, BayR.MinY() + 9));
	}
}

void SAcConsole::PaintCard(FPaint& P) const
{
	// Console.buildCard (the top-down look).
	const TArray<FAcCardButton> None;
	const TArray<FAcCardButton>& Buttons = Info ? Info->Buttons : None;
	TMap<int32, int32> BySlot;
	for (int32 I = 0; I < Buttons.Num(); ++I)
	{
		if (!BySlot.Contains(Buttons[I].Slot)) BySlot.Add(Buttons[I].Slot, I);
	}
	const FLinearColor Cyan = FAcHudStyle::Cyan();
	// The top-down card has lit button faces; a driven unit's, flat ones
	// with a sheen and the cost in the corner.
	const bool bRts = Look == EAcConsoleStyle::Rts;
	const double Radius = bRts ? 6 : 3;
	for (int32 Slot = 0; Slot < FAcCabLayout::Columns * FAcCabLayout::Rows; ++Slot)
	{
		const FAcRect R = Lay.SlotRect(Slot);
		const int32* I = BySlot.Find(Slot);
		if (!I)
		{
			if (bRts)
			{
				P.Image(ButtonOff.Get(), R, FLinearColor(1, 1, 1, 0.55f));
				P.Stroke(R, 6, Alpha(Cyan, 0.16), 1);
			}
			else
			{
				P.FillRounded(R, 3, Rgb(0.02, 0.03, 0.05, 0.85));
				P.Stroke(R, 3, Gray(0.18), 1);
			}
			continue;
		}
		const FAcCardButton& B = Buttons[*I];
		if (bRts) P.Image((B.bEnabled ? ButtonOn : ButtonOff).Get(), R);
		else P.FillRounded(R, 3, B.bEnabled ? Rgb(0.05, 0.12, 0.2) : Rgb(0.05, 0.05, 0.06));
		if (const FSlateBrush* Icon = FAcIcons::Brush(B.Icon))
		{
			P.Image(Icon, R.Inset(2, 2), B.bEnabled ? FLinearColor::White : FPaint::BlendTint(Gray(0.3), 0.6, 0.85));
		}
		else
		{
			P.Text(B.Title, 11, FLinearColor::White, FVector2D(R.MidX(), R.MidY() - 4), EAcHAlign::Center);
		}
		// The rim: green when lit, amber when it queues, cyan glowing when it
		// can be used, grey when not.
		const FLinearColor Rim = B.bLit ? Rgb(0.4, 1, 0.45)
			: B.bQueues ? Alpha(FAcHudStyle::QueueAmber(), 0.85)
			: B.bEnabled ? Alpha(bRts ? Cyan : Accent(), 0.9) : Gray(0.3);
		P.Stroke(R.Inset(0.5, 0.5), Radius, Rim, B.bLit ? 2 : 1.3, B.bLit ? 3 : !B.bEnabled ? 0 : bRts ? 1.6 : 0.8);
		if (!bRts)
		{
			P.FillRounded(FAcRect(R.MinX() + 2, R.MidY() + 4, R.W - 4, R.H / 2 - 6), 2, Gray(1, 0.05));
		}
		P.Text(B.Key, 12, Alpha(FAcHudStyle::KeyYellow(), B.bEnabled ? 1 : 0.5), FVector2D(R.MinX() + 4, R.MaxY() - 13));
		if (!bRts && B.Ore + B.Hydrogen > 0)
		{
			const FString Cost = B.Hydrogen > 0 ? FString::Printf(TEXT("%d/%d"), B.Ore, B.Hydrogen) : FString::Printf(TEXT("%d"), B.Ore);
			P.Text(Cost, 10, Rgb(0.6, 0.9, 1, B.bEnabled ? 1 : 0.5), FVector2D(R.MaxX() - 3, R.MinY() + 3), EAcHAlign::Right);
		}
	}
}

void SAcConsole::PaintOver(FPaint& P) const
{
	const FLinearColor Acc = Accent();
	// The note over the card: 1.6 s, then a 0.5 s fade.
	if (IsNoteShown())
	{
		const double T = Now - NoteSince;
		const float Opacity = (float)(T < 1.6 || bHoldNote ? 1.0 : FMath::Max(0.0, 1 - (T - 1.6) / 0.5));
		P.Text(*ShownNote, 17, NoteColor(), Lay.NoteAt, EAcHAlign::Center, true, true, Opacity);
	}

	// "VIEW  [1ST | 3RD]  V": the lit half is the view in use.
	const FAcRect& R = Lay.ViewSwitch;
	if (Look != EAcConsoleStyle::Rts)
	{
		P.FillRounded(R, 5, Rgb(0.02, 0.06, 0.1, 0.82));
		P.Stroke(R, 5, Alpha(Acc, 0.55), 1);
	}
	else if (const FSlateBrush* Plate = SwitchPlate.Get())
	{
		const double Pad = AcCabArt::PlatePad;
		P.Image(Plate, R.Inset(-4 - Pad, -3 - Pad));
	}
	P.Text(TEXT("VIEW"), 11, Alpha(Acc, 0.8), FVector2D(R.MinX() + 8, R.MidY() - 4));
	static const TCHAR* Names[2] = {TEXT("1ST"), TEXT("3RD")};
	for (int32 K = 0; K < 2; ++K)
	{
		const FAcRect Seg(R.MinX() + 46 + K * 50, R.MinY() + 3, 48, R.H - 6);
		const bool bOn = (K == 1) == bThird;
		if (bOn) P.Fill(Seg, Alpha(Acc, 0.35));
		P.Stroke(Seg, 3, bOn ? Acc : Alpha(Acc, 0.3), bOn ? 1.4 : 1, bOn ? 1.5 : 0);
		P.Text(Names[K], 12, bOn ? FLinearColor::White : Gray(0.6), FVector2D(Seg.MidX(), Seg.MidY() - 4.5), EAcHAlign::Center);
	}
	P.Text(TEXT("V"), 12, FAcHudStyle::KeyYellow(), FVector2D(R.MaxX() - 12, R.MidY() - 4.5), EAcHAlign::Center);

	// The tooltip over the card: name, cost and time, and why not yet.
	if (Hovered && Info && Info->Buttons.IsValidIndex(*Hovered))
	{
		const FAcCardButton& B = Info->Buttons[*Hovered];
		struct FLine { FString Text; FLinearColor Color; double Size; };
		TArray<FLine> Lines = {{B.Title, FLinearColor::White, 17}};
		TArray<FString> Cost;
		if (B.Ore > 0) Cost.Add(FString::Printf(TEXT("%d ore"), B.Ore));
		if (B.Hydrogen > 0) Cost.Add(FString::Printf(TEXT("%d MH"), B.Hydrogen));
		if (B.Time) Cost.Add(FString::Printf(TEXT("%d s"), (int32)FMath::RoundHalfFromZero(*B.Time)));
		if (!Cost.IsEmpty()) Lines.Add({FString::Join(Cost, TEXT("   ·   ")), Rgb(0.6, 0.88, 1), 13});
		if (B.Detail) Lines.Add({*B.Detail, Gray(0.8), 13});
		if (B.Why) Lines.Add({*B.Why, B.bQueues ? FAcHudStyle::QueueAmber() : Rgb(1, 0.4, 0.3), 13});
		const double W = 280, H = Lines.Num() * 20 + 14;
		const FAcRect Tip(Lay.CardWell.MaxX() - 280, Lay.Top() + 44, W, H);
		P.Fill(Tip, Rgb(0.02, 0.04, 0.07, 0.94));
		P.Stroke(Tip, 3, Alpha(Acc, 0.7));
		for (int32 K = 0; K < Lines.Num(); ++K)
		{
			P.Text(Lines[K].Text, Lines[K].Size, Lines[K].Color, FVector2D(Tip.MinX() + 10, Tip.MaxY() - 22 - K * 20));
		}
	}
	else if (!DeckTip.IsEmpty() && bLaidOut && Lay.Blocks[0].W > 60)
	{
		// The music deck's tip (D9), its left edge at the deck's block.
		const double H = DeckTip.Num() * 20 + 14;
		const FAcRect Tip(Lay.Blocks[0].MinX(), Lay.Top() + 44, 280, H);
		P.Fill(Tip, Rgb(0.02, 0.04, 0.07, 0.94));
		P.Stroke(Tip, 3, Alpha(Acc, 0.7));
		for (int32 K = 0; K < DeckTip.Num(); ++K)
		{
			P.Text(DeckTip[K].Text, DeckTip[K].Size, DeckTip[K].Color, FVector2D(Tip.MinX() + 10, Tip.MaxY() - 22 - K * 20));
		}
	}
}

// MARK: - SAcViewFrame

void SAcViewFrame::Construct(const FArguments& InArgs)
{
	SetCanTick(true);
	SetVisibility(EVisibility::HitTestInvisible);
}

void SAcViewFrame::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
	const FVector2D Size = FVector2D(Geometry.GetLocalSize());
	if (Size.X < 100 || Size.Y < 100 || Size.Equals(DrawnSize, 0.5)) return;
	if (!Size.Equals(PendingSize, 0.5))
	{
		PendingSize = Size;
		PendingSince = CurrentTime;
		if (Texture) return;  // the first one at once, a resize once it settles
	}
	else if (CurrentTime - PendingSince < 0.15)
	{
		return;
	}
	const FAcArtImage Image = AcCabArt::ViewFrame(Size);
	Texture.Reset(AcCabArt::ToTexture(Image, TEXT("AcViewFrame")));
	Brush = MakeShared<FSlateBrush>();
	Brush->DrawAs = ESlateBrushDrawType::Image;
	Brush->ImageSize = Image.Points;
	if (Texture) Brush->SetResourceObject(Texture.Get());
	DrawnSize = Size;
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SAcViewFrame::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	if (!Brush || !Texture) return Layer;
	FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(DrawnSize), FSlateLayoutTransform()), Brush.Get());
	return Layer + 1;
}
