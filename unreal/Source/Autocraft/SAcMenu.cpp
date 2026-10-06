#include "SAcMenu.h"

#include "AcHudStyle.h"
#include "AcHudText.h"
#include "AcSettings.h"
#include "AcLauncherArt.h"

#include "Rendering/DrawElements.h"
#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	// The panel, in Swift points (the HUD root's units).
	constexpr float PanelW = 440;
	constexpr float Pad = 32;
	constexpr float ButtonH = 44;
	constexpr float ButtonGap = 10;
	constexpr float CardH = 66;
	constexpr float RowH = 40;
	constexpr float LabelW = 130;
	constexpr float ValueW = 56;
	constexpr float LookMin = 0.25f, LookMax = 3.f;

	void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FBox2f& B, const FLinearColor& C)
	{
		const FVector2f S = B.GetSize();
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2D(S), FSlateLayoutTransform(FVector2D(B.Min))),
			FAcHudStyle::White(), ESlateDrawEffect::None, C);
	}

	/// An outline `W` points wide inside `B` (square corners, as SAcNewGame).
	void Frame(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FBox2f& B, const FLinearColor& C, float W = 1)
	{
		const FVector2f S = B.GetSize();
		Box(Out, Layer, G, FBox2f(B.Min, B.Min + FVector2f(S.X, W)), C);
		Box(Out, Layer, G, FBox2f(FVector2f(B.Min.X, B.Max.Y - W), B.Max), C);
		Box(Out, Layer, G, FBox2f(B.Min + FVector2f(0, W), FVector2f(B.Min.X + W, B.Max.Y - W)), C);
		Box(Out, Layer, G, FBox2f(FVector2f(B.Max.X - W, B.Min.Y + W), FVector2f(B.Max.X, B.Max.Y - W)), C);
	}

	FAcHudText Label(const FString& S, float Size, const FLinearColor& C, EAcHAlign H = EAcHAlign::Left,
		EAcVAlign V = EAcVAlign::Top, EAcFontWeight Weight = EAcFontWeight::Bold)
	{
		FAcHudText T(S, FAcHudStyle::Font(Size, Weight), C, H, V);
		T.Accent = FLinearColor::Transparent;
		return T;
	}
}

void SAcMenu::Construct(const FArguments& InArgs)
{
	MenuMode = InArgs._Mode;
	OnPickDelegate = InArgs._OnPick;
	Art = MakeShared<FAcLauncherArt>();
	// Drift and crossfade repaint every frame, only while the home art shows.
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SAcMenu::OnArtTimer));
}

bool SAcMenu::HasArt() const
{
	return Art.IsValid() && Art->Num() > 0 && Art->Brush(Art->Start()) != nullptr;
}

EActiveTimerReturnType SAcMenu::OnArtTimer(double, float)
{
	// A pinned image (-AcLauncherArt=N, stills) does not move.
	if (ArtShowing() && Art->Pinned() == INDEX_NONE) Invalidate(EInvalidateWidgetReason::Paint);
	return EActiveTimerReturnType::Continue;
}

void SAcMenu::SetNowPlaying(const FString& Title)
{
	if (Title == NowPlaying) return;
	NowPlaying = Title;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAcMenu::SetGame(const FString& Title, const FString& Detail)
{
	GameTitle = Title;
	GameDetail = Detail;
	if (!bSettings) Highlighted = FMath::Clamp(Highlighted, 0, Buttons().Num() - 1);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAcMenu::SetMode(const EAcMenuMode InMode)
{
	MenuMode = InMode;
	bSettings = false;
	Dragging = INDEX_NONE;
	Highlighted = 0;
	Pressed = INDEX_NONE;
	Invalidate(EInvalidateWidgetReason::Paint);
}

TArray<SAcMenu::FButton> SAcMenu::Buttons() const
{
	if (bSettings) return {};
	if (MenuMode == EAcMenuMode::Pause)
	{
		return {{EAcMenuAction::Resume, TEXT("RESUME")}, {EAcMenuAction::Settings, TEXT("SETTINGS")},
			{EAcMenuAction::QuitToHome, TEXT("QUIT TO HOME")}};
	}
	TArray<FButton> B;
	if (!GameTitle.IsEmpty()) B.Add({EAcMenuAction::Resume, TEXT("RESUME GAME")});
	B.Add({EAcMenuAction::NewGame, TEXT("NEW GAME")});
	B.Add({EAcMenuAction::Settings, TEXT("SETTINGS")});
	B.Add({EAcMenuAction::QuitApp, TEXT("QUIT")});
	return B;
}

// MARK: - Settings

int32 SAcMenu::SliderCount() { return 5; }

FString SAcMenu::SliderName(const int32 I)
{
	static const TCHAR* Names[] = {TEXT("MASTER"), TEXT("MUSIC"), TEXT("EFFECTS"), TEXT("ENVIRONMENT"), TEXT("MOUSE TURN")};
	return Names[FMath::Clamp(I, 0, 4)];
}

float SAcMenu::SliderGet(const int32 I)
{
	const UAcSettings& S = UAcSettings::Get();
	switch (I)
	{
	case 0: return FMath::Clamp(S.MasterVolume, 0.f, 1.f);
	case 1: return S.bMusic ? FMath::Clamp(S.MusicVolume, 0.f, 1.f) : 0.f;
	case 2: return FMath::Clamp(S.SfxVolume, 0.f, 1.f);
	case 3: return FMath::Clamp(S.AmbientVolume, 0.f, 1.f);
	default: return FMath::Clamp((S.LookSpeed - LookMin) / (LookMax - LookMin), 0.f, 1.f);
	}
}

void SAcMenu::SliderSet(const int32 I, float F)
{
	UAcSettings& S = UAcSettings::Get();
	F = FMath::Clamp(F, 0.f, 1.f);
	switch (I)
	{
	case 0: S.MasterVolume = F; break;
	case 1: S.MusicVolume = F; S.bMusic = F > 0.f; break;
	case 2: S.SfxVolume = F; break;
	case 3: S.AmbientVolume = F; break;
	default: S.LookSpeed = LookMin + F * (LookMax - LookMin); break;
	}
}

FString SAcMenu::SliderText(const int32 I)
{
	if (I == 4) return FString::Printf(TEXT("%.2f×"), UAcSettings::Get().LookSpeed);
	return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(SliderGet(I) * 100));
}

void SAcMenu::Adjust(const int32 Step)
{
	if (!bSettings || Highlighted >= SliderCount()) return;
	// Volumes in 5% steps; the mouse turn in 0.25× steps (1/11 of its range).
	const float Unit = Highlighted == 4 ? 0.25f / (LookMax - LookMin) : 0.05f;
	SliderSet(Highlighted, FMath::RoundToFloat(SliderGet(Highlighted) / Unit + Step) * Unit);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAcMenu::OpenSettings()
{
	const int32 I = Buttons().IndexOfByPredicate([](const FButton& B) { return B.Action == EAcMenuAction::Settings; });
	if (I != INDEX_NONE) Pick(I);
}

bool SAcMenu::Back()
{
	if (!bSettings) return false;
	if (UAcSettings::Remembers()) UAcSettings::Store();
	bSettings = false;
	Dragging = INDEX_NONE;
	Highlighted = Buttons().IndexOfByPredicate([](const FButton& B) { return B.Action == EAcMenuAction::Settings; });
	Highlighted = FMath::Max(Highlighted, 0);
	Invalidate(EInvalidateWidgetReason::Paint);
	return true;
}

void SAcMenu::DragTo(const FVector2f Local)
{
	if (!Tracks.IsValidIndex(Dragging)) return;
	const FVector2f P = (Local - PanelOrigin) / PanelScale;
	const FBox2f& T = Tracks[Dragging];
	SliderSet(Dragging, (P.X - T.Min.X) / FMath::Max(T.GetSize().X, 1.f));
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAcMenu::Move(const int32 Step)
{
	const int32 N = bSettings ? SliderCount() + 1 : Buttons().Num();
	Highlighted = (Highlighted + Step + N) % N;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAcMenu::PickHighlighted()
{
	Pick(Highlighted);
}

void SAcMenu::Pick(const int32 Index)
{
	if (bSettings)
	{
		if (Index == SliderCount()) Back();
		return;
	}
	const TArray<FButton> B = Buttons();
	if (!B.IsValidIndex(Index)) return;
	if (B[Index].Action == EAcMenuAction::Settings)
	{
		bSettings = true;
		Highlighted = 0;
		Invalidate(EInvalidateWidgetReason::Paint);
		return;
	}
	OnPickDelegate.ExecuteIfBound(B[Index].Action);
}

// MARK: - Layout

void SAcMenu::Layout(const FVector2f ViewSize) const
{
	Boxes.Reset();
	Tracks.Reset();
	if (bSettings)
	{
		float Y = 84;
		for (int32 I = 0; I < SliderCount(); ++I)
		{
			Boxes.Add(FBox2f(FVector2f(Pad, Y), FVector2f(PanelW - Pad, Y + RowH)));
			const float Mid = Y + RowH / 2;
			Tracks.Add(FBox2f(FVector2f(Pad + LabelW, Mid - 3), FVector2f(PanelW - Pad - ValueW, Mid + 3)));
			Y += RowH + 4;
		}
		Y += 18;
		Boxes.Add(FBox2f(FVector2f(Pad, Y), FVector2f(PanelW - Pad, Y + ButtonH)));
		PanelSize = FVector2f(PanelW, Y + ButtonH + Pad);
		PanelScale = FMath::Clamp(FMath::Min((ViewSize.X - 40) / PanelSize.X, (ViewSize.Y - 40) / PanelSize.Y), 0.3f, 1.0f);
		PanelOrigin = (ViewSize - PanelSize * PanelScale) * 0.5f;
		if (ArtShowing()) PanelOrigin.X = FMath::Min(ViewSize.X * 0.08f, ViewSize.X - PanelSize.X * PanelScale - 20.f);
		return;
	}
	float Y = MenuMode == EAcMenuMode::Home ? 112 : 84;
	CardBox = FBox2f(FVector2f::ZeroVector, FVector2f::ZeroVector);
	if (!GameTitle.IsEmpty())
	{
		CardBox = FBox2f(FVector2f(Pad, Y), FVector2f(PanelW - Pad, Y + CardH));
		Y += CardH + 22;
	}
	for (int32 I = 0; I < Buttons().Num(); ++I)
	{
		Boxes.Add(FBox2f(FVector2f(Pad, Y), FVector2f(PanelW - Pad, Y + ButtonH)));
		Y += ButtonH + ButtonGap;
	}
	PanelSize = FVector2f(PanelW, Y - ButtonGap + Pad);
	PanelScale = FMath::Clamp(FMath::Min((ViewSize.X - 40) / PanelSize.X, (ViewSize.Y - 40) / PanelSize.Y), 0.3f, 1.0f);
	PanelOrigin = (ViewSize - PanelSize * PanelScale) * 0.5f;
	if (ArtShowing()) PanelOrigin.X = FMath::Min(ViewSize.X * 0.08f, ViewSize.X - PanelSize.X * PanelScale - 20.f);
}

int32 SAcMenu::ButtonAt(const FVector2f Local) const
{
	const FVector2f P = (Local - PanelOrigin) / PanelScale;
	for (int32 I = 0; I < Boxes.Num(); ++I)
	{
		if (Boxes[I].IsInsideOrOn(P)) return I;
	}
	return INDEX_NONE;
}

// MARK: - Input (modal)

FReply SAcMenu::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2f Local(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
		Pressed = ButtonAt(Local);
		Invalidate(EInvalidateWidgetReason::Paint);
		if (bSettings && Pressed != INDEX_NONE && Pressed < SliderCount())
		{
			// A slider row: the value under the pointer, then dragged.
			Highlighted = Dragging = Pressed;
			Pressed = INDEX_NONE;
			DragTo(Local);
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
	}
	return FReply::Handled();
}

FReply SAcMenu::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Dragging != INDEX_NONE)
	{
		Dragging = INDEX_NONE;
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton && Pressed != INDEX_NONE)
	{
		const int32 Hit = ButtonAt(FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())));
		const int32 Was = Pressed;
		Pressed = INDEX_NONE;
		Invalidate(EInvalidateWidgetReason::Paint);
		if (Hit == Was) Pick(Hit);
	}
	return FReply::Handled();
}

FReply SAcMenu::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Dragging != INDEX_NONE)
	{
		DragTo(FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())));
		return FReply::Handled();
	}
	const int32 Hit = ButtonAt(FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())));
	if (Hit != INDEX_NONE && Hit != Highlighted)
	{
		Highlighted = Hit;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	return FReply::Handled();
}

void SAcMenu::OnMouseLeave(const FPointerEvent& Event)
{
	SLeafWidget::OnMouseLeave(Event);
	Pressed = INDEX_NONE;
}

FCursorReply SAcMenu::OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const
{
	const bool bOver = ButtonAt(FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()))) != INDEX_NONE;
	return FCursorReply::Cursor(bOver ? EMouseCursor::Hand : EMouseCursor::Default);
}

// MARK: - Paint

namespace
{
	constexpr double ArtSlot = 12.5;   // an image takes over every 12.5 s ...
	constexpr double ArtFade = 1.5;    // ... fading in over 1.5 s, so each is on screen 14 s
	constexpr float ArtZoom = 0.06f;
	constexpr float ArtPan = 0.025f;
}

/// The key art, aspect-fill with a slow zoom and pan, crossfading to the next; then the
/// left shade, the vignette and the now-playing line.
void SAcMenu::PaintArt(FSlateWindowElementList& Out, const int32 Layer, const FGeometry& G, const FVector2f View) const
{
	const double Now = FPlatformTime::Seconds();
	if (ArtStart < 0) ArtStart = Now;
	const bool bPinned = Art->Pinned() != INDEX_NONE;
	const double E = bPinned ? 0.0 : Now - ArtStart;
	const int32 N = Art->Num();
	const int32 Idx = Art->Start() + (int32)FMath::FloorToDouble(E / ArtSlot);
	const double Local = E - FMath::FloorToDouble(E / ArtSlot) * ArtSlot;

	Out.PushClip(FSlateClippingZone(G.GetLayoutBoundingRect()));
	// `Seen`: seconds this image has been up; 14 s are drifted through.
	auto Draw = [&](const int32 I, const double Seen, const float Alpha, const int32 Sub)
	{
		const FSlateBrush* B = Art->Brush(I);
		if (!B) return;
		const FVector2f Px = B->ImageSize;
		const float Prog = bPinned ? 0.3f : (float)FMath::Clamp(Seen / (ArtSlot + ArtFade), 0.0, 1.0);
		const float Cover = FMath::Max(View.X / Px.X, View.Y / Px.Y) * (1.f + ArtZoom * Prog);
		const FVector2f Size = Px * Cover;
		// Alternate the pan direction by image; it never exceeds the crop the zoom leaves.
		const float Dir = (I % 2 == 0) ? 1.f : -1.f;
		FVector2f Pan(Dir * ArtPan * Prog * View.X, -Dir * ArtPan * 0.5f * Prog * View.Y);
		const FVector2f Slack = (Size - View) * 0.5f;
		Pan.X = FMath::Clamp(Pan.X, -Slack.X, Slack.X);
		Pan.Y = FMath::Clamp(Pan.Y, -Slack.Y, Slack.Y);
		const FVector2f At = (View - Size) * 0.5f + Pan;
		FSlateDrawElement::MakeBox(Out, Layer + Sub, G.ToPaintGeometry(FVector2D(Size), FSlateLayoutTransform(FVector2D(At))), B,
			ESlateDrawEffect::None, FLinearColor(1, 1, 1, Alpha));
	};
	Box(Out, Layer, G, FBox2f(FVector2f::ZeroVector, View), FLinearColor::Black);
	if (N > 1 && !bPinned && Local < ArtFade && E >= ArtFade)
	{
		Draw(Idx - 1, Local + ArtSlot, 1.f, 0);
		Draw(Idx, Local, (float)(Local / ArtFade), 1);
	}
	else
	{
		Draw(Idx, Local, 1.f, 0);
	}
	Out.PopClip();

	// Shades: left ~45% for the menu, a vignette at the edges, the bottom for the song line.
	// One smooth texture per shade (strips of boxes showed their seams):
	// white, alpha falling 1 to 0 along X (`Ramp`), stretched and turned.
	static TStrongObjectPtr<UTexture2D> Ramp;
	static FSlateBrush RampBrush;
	if (!Ramp)
	{
		FImage Img(256, 1, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		FColor* Px = reinterpret_cast<FColor*>(Img.RawData.GetData());
		for (int32 X = 0; X < 256; ++X)
		{
			const float A = 1.f - FMath::SmoothStep(0.f, 1.f, X / 255.f);
			Px[X] = FColor(255, 255, 255, (uint8)FMath::RoundToInt(A * 255));
		}
		Ramp.Reset(FAcHudStyle::HudTexture(Img));
		if (Ramp)
		{
			Ramp->Filter = TF_Bilinear;
			Ramp->AddressX = TA_Clamp;
			Ramp->UpdateResource();
			RampBrush.SetResourceObject(Ramp.Get());
			RampBrush.ImageSize = FVector2D(256, 1);
			RampBrush.DrawAs = ESlateBrushDrawType::Image;
		}
	}
	// Dark at `Dark` (an edge: 0 left, 1 right, 2 top, 3 bottom) at alpha A, clear at the far side.
	auto Shade = [&](const FBox2f R, const int32 Dark, const float A)
	{
		if (!Ramp) return;
		const FVector2f S = R.GetSize();
		// The ramp runs along X from its dark end; turn it to face `Dark`.
		const float Angle = Dark == 0 ? 0.f : Dark == 1 ? PI : Dark == 2 ? PI / 2 : -PI / 2;
		const bool bV = Dark >= 2;
		const FVector2f Len = bV ? FVector2f(S.Y, S.X) : S;
		const FSlateRenderTransform Turn{FQuat2f(Angle)};
		const FVector2f Centre = R.GetCenter();
		const FGeometry Child = G.MakeChild(Len, FSlateLayoutTransform(FVector2f(Centre - Len * 0.5f)), Turn, FVector2f(0.5f, 0.5f));
		FSlateDrawElement::MakeBox(Out, Layer + 2, Child.ToPaintGeometry(), &RampBrush, ESlateDrawEffect::None, FLinearColor(0, 0, 0, A));
	};
	Shade(FBox2f(FVector2f::ZeroVector, FVector2f(View.X * 0.45f, View.Y)), 0, 0.82f);
	Shade(FBox2f(FVector2f(View.X * 0.82f, 0), View), 1, 0.35f);
	Shade(FBox2f(FVector2f::ZeroVector, FVector2f(View.X, View.Y * 0.14f)), 2, 0.35f);
	Shade(FBox2f(FVector2f(0, View.Y * 0.8f), View), 3, 0.55f);

	if (!NowPlaying.IsEmpty())
	{
		const float K = FMath::Clamp(View.Y / 1080.f, 0.6f, 2.f);
		const FGeometry P = G.MakeChild(FVector2D(600, 40), FSlateLayoutTransform(K, FVector2D(View.X * 0.08f, View.Y - 24 * K - 26 * K)));
		Label(TEXT("NOW PLAYING"), 11, FAcHudStyle::Srgb(0.55, 0.7, 0.8), EAcHAlign::Left, EAcVAlign::Top, EAcFontWeight::SemiBold)
			.Paint(Out, Layer + 3, P, FVector2f(0, 0));
		Label(NowPlaying, 16, FAcHudStyle::Text(), EAcHAlign::Left, EAcVAlign::Top, EAcFontWeight::Medium)
			.Paint(Out, Layer + 3, P, FVector2f(0, 16));
	}
}

int32 SAcMenu::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2f View(Geometry.GetLocalSize());
	Layout(View);
	const bool bHome = MenuMode == EAcMenuMode::Home;
	if (ArtShowing())
	{
		PaintArt(Out, Layer, Geometry, View);
	}
	else
	{
		// The paused game behind: darker on the home screen.
		Box(Out, Layer, Geometry, FBox2f(FVector2f::ZeroVector, View), FLinearColor(0, 0, 0, bHome ? 0.72f : 0.5f));
	}
	const FGeometry P = Geometry.MakeChild(FVector2D(PanelSize), FSlateLayoutTransform(PanelScale, FVector2D(PanelOrigin)));
	const FBox2f Panel(FVector2f::ZeroVector, PanelSize);
	Box(Out, Layer + 1, P, Panel, FAcHudStyle::Srgb(0.025, 0.045, 0.075, 0.96));
	Frame(Out, Layer + 2, P, Panel, FAcHudStyle::Srgb(0.42, 0.5, 0.58, 0.9), 2);
	Frame(Out, Layer + 2, P, Panel.ExpandBy(-4), FAcHudStyle::Srgb(0.35, 0.85, 1, 0.28), 1);
	const int32 L = Layer + 3;

	if (bSettings)
	{
		Label(TEXT("SETTINGS"), 34, FAcHudStyle::Ice(), EAcHAlign::Center).Paint(Out, L, P, FVector2f(PanelW / 2, 26));
		for (int32 I = 0; I < SliderCount() && I < Tracks.Num(); ++I)
		{
			const bool bHot = I == Highlighted;
			const FBox2f& R = Boxes[I];
			if (bHot) Box(Out, L, P, R, FAcHudStyle::Srgb(0.06, 0.14, 0.2, 1));
			if (bHot) Frame(Out, L + 1, P, R, FAcHudStyle::Cyan(), 1);
			Label(SliderName(I), 15, bHot ? FAcHudStyle::Text() : FAcHudStyle::Srgb(0.75, 0.85, 0.92), EAcHAlign::Left, EAcVAlign::Center)
				.Paint(Out, L + 2, P, FVector2f(R.Min.X + 12, R.GetCenter().Y + 0.5f));
			const FBox2f& T = Tracks[I];
			const float F = SliderGet(I);
			Box(Out, L + 1, P, T, FAcHudStyle::Srgb(0.12, 0.18, 0.24, 1));
			Box(Out, L + 2, P, FBox2f(T.Min, FVector2f(FMath::Lerp(T.Min.X, T.Max.X, F), T.Max.Y)), FAcHudStyle::Cyan());
			const float KX = FMath::Lerp(T.Min.X, T.Max.X, F);
			Box(Out, L + 3, P, FBox2f(FVector2f(KX - 4, T.Min.Y - 7), FVector2f(KX + 4, T.Max.Y + 7)),
				bHot ? FAcHudStyle::Text() : FAcHudStyle::Srgb(0.6, 0.72, 0.8));
			Label(SliderText(I), 14, FAcHudStyle::Text(), EAcHAlign::Right, EAcVAlign::Center, EAcFontWeight::Medium)
				.Paint(Out, L + 2, P, FVector2f(R.Max.X - 10, R.GetCenter().Y + 0.5f));
		}
		const FBox2f& BackBox = Boxes.Last();
		const bool bHot = Highlighted == SliderCount();
		Box(Out, L, P, BackBox, bHot ? FAcHudStyle::Srgb(0.08, 0.3, 0.42, 1) : FAcHudStyle::Srgb(0.05, 0.09, 0.14, 1));
		Frame(Out, L + 1, P, BackBox, bHot ? FAcHudStyle::Cyan() : FAcHudStyle::Srgb(0.3, 0.42, 0.52, 0.8), bHot ? 1.5f : 1.0f);
		Label(TEXT("BACK"), 18, bHot ? FAcHudStyle::Text() : FAcHudStyle::Srgb(0.75, 0.85, 0.92), EAcHAlign::Center, EAcVAlign::Center)
			.Paint(Out, L + 2, P, BackBox.GetCenter() + FVector2f(0, 0.5f));
		return L + 4;
	}

	if (bHome)
	{
		Label(TEXT("AUTOCRAFT"), 46, FAcHudStyle::Ice(), EAcHAlign::Center).Paint(Out, L, P, FVector2f(PanelW / 2, 26));
		Label(TEXT("Stardust Ore · Metallic Hydrogen · a frontier world"), 14, FAcHudStyle::Soft(), EAcHAlign::Center, EAcVAlign::Top,
			EAcFontWeight::Medium)
			.Paint(Out, L, P, FVector2f(PanelW / 2, 80));
	}
	else
	{
		Label(TEXT("PAUSED"), 34, FAcHudStyle::Ice(), EAcHAlign::Center).Paint(Out, L, P, FVector2f(PanelW / 2, 26));
	}

	if (!GameTitle.IsEmpty())
	{
		Box(Out, L, P, CardBox, FAcHudStyle::Srgb(0.04, 0.07, 0.11, 1));
		Frame(Out, L + 1, P, CardBox, FAcHudStyle::Srgb(0.3, 0.42, 0.52, 0.6));
		Label(bHome ? TEXT("YOUR GAME") : TEXT("THIS GAME"), 11, FAcHudStyle::Srgb(0.55, 0.7, 0.8), EAcHAlign::Left, EAcVAlign::Top,
			EAcFontWeight::SemiBold)
			.Paint(Out, L + 2, P, CardBox.Min + FVector2f(14, 10));
		Label(GameTitle, 18, FAcHudStyle::Text()).Paint(Out, L + 2, P, CardBox.Min + FVector2f(14, 25));
		Label(GameDetail, 13, FAcHudStyle::Srgb(0.6, 0.68, 0.76), EAcHAlign::Left, EAcVAlign::Top, EAcFontWeight::Medium)
			.Paint(Out, L + 2, P, CardBox.Min + FVector2f(14, 47));
	}

	const TArray<FButton> B = Buttons();
	for (int32 I = 0; I < Boxes.Num() && I < B.Num(); ++I)
	{
		const bool bHot = I == Highlighted;
		const bool bDown = I == Pressed;
		FLinearColor Fill = FAcHudStyle::Srgb(0.05, 0.09, 0.14, 1);
		FLinearColor Rim = FAcHudStyle::Srgb(0.3, 0.42, 0.52, 0.8);
		FLinearColor Ink = FAcHudStyle::Srgb(0.75, 0.85, 0.92);
		if (bHot)
		{
			Fill = FAcHudStyle::Srgb(0.08, 0.3, 0.42, 1);
			Rim = FAcHudStyle::Cyan();
			Ink = FAcHudStyle::Text();
		}
		if (bDown) Fill = FAcHudStyle::Srgb(0.12, 0.4, 0.55, 1);
		Box(Out, L, P, Boxes[I], Fill);
		Frame(Out, L + 1, P, Boxes[I], Rim, bHot ? 1.5f : 1.0f);
		Label(B[I].Label, 18, Ink, EAcHAlign::Center, EAcVAlign::Center).Paint(Out, L + 2, P, Boxes[I].GetCenter() + FVector2f(0, 0.5f));
	}
	return L + 4;
}
