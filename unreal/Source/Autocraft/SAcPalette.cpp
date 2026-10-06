#include "SAcPalette.h"

#include "AcHudStyle.h"
#include "AcHudText.h"

#include "Rendering/DrawElements.h"

namespace
{
	void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FBox2f& B, const FLinearColor& C)
	{
		const FVector2f S = B.GetSize();
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2D(S), FSlateLayoutTransform(FVector2D(B.Min))), FAcHudStyle::White(),
			ESlateDrawEffect::None, C);
	}

	/// An outline `W` points wide inside the box (square corners, as SAcNewGame).
	void Frame(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FBox2f& B, const FLinearColor& C, float W = 1)
	{
		const FVector2f S = B.GetSize();
		Box(Out, Layer, G, FBox2f(B.Min, B.Min + FVector2f(S.X, W)), C);
		Box(Out, Layer, G, FBox2f(FVector2f(B.Min.X, B.Max.Y - W), B.Max), C);
		Box(Out, Layer, G, FBox2f(B.Min + FVector2f(0, W), FVector2f(B.Min.X + W, B.Max.Y - W)), C);
		Box(Out, Layer, G, FBox2f(FVector2f(B.Max.X - W, B.Min.Y + W), FVector2f(B.Max.X, B.Max.Y - W)), C);
	}

	FAcHudText Label(const FString& S, float Size, const FLinearColor& C, EAcHAlign H = EAcHAlign::Left, EAcVAlign V = EAcVAlign::Center,
		EAcFontWeight Weight = EAcFontWeight::Bold)
	{
		FAcHudText T(S, FAcHudStyle::Font(Size, Weight), C, H, V);
		T.Accent = FLinearColor::Transparent;
		return T;
	}

	/// The chrome's glass, steel rim and cyan line (SAcNewGame's panel).
	FLinearColor GlassFill() { return FAcHudStyle::Srgb(0.025, 0.045, 0.075, 0.9); }
	FLinearColor Steel() { return FAcHudStyle::Srgb(0.42, 0.5, 0.58, 0.9); }
	FLinearColor Inner() { return FAcHudStyle::Srgb(0.35, 0.85, 1, 0.28); }
	FLinearColor HeaderInk() { return FAcHudStyle::Srgb(0.55, 0.7, 0.8); }
	FLinearColor RowInk() { return FAcHudStyle::Srgb(0.78, 0.86, 0.92); }

	/// An option or a button (SAcNewGame's look).
	void Button(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FBox2f& B, const FString& Text, bool bSelected, bool bHover,
		bool bDown, float Size = 14)
	{
		FLinearColor Fill = FAcHudStyle::Srgb(0.05, 0.09, 0.14, 1);
		FLinearColor Rim = FAcHudStyle::Srgb(0.3, 0.42, 0.52, 0.8);
		FLinearColor Ink = FAcHudStyle::Srgb(0.75, 0.85, 0.92);
		if (bSelected)
		{
			Fill = FAcHudStyle::Srgb(0.08, 0.3, 0.42, 1);
			Rim = FAcHudStyle::Cyan();
			Ink = FAcHudStyle::Text();
		}
		if (bHover) Rim = FAcHudStyle::Ice();
		if (bDown) Fill = FAcHudStyle::Srgb(0.12, 0.4, 0.55, 1);
		Box(Out, L, G, B, Fill);
		Frame(Out, L + 1, G, B, Rim, bSelected || bHover ? 1.5f : 1.0f);
		if (!Text.IsEmpty()) Label(Text, Size, Ink, EAcHAlign::Center).Paint(Out, L + 2, G, B.GetCenter() + FVector2f(0, 0.5f));
	}

	constexpr int32 IdFog = 10, IdUnlimited = 11, IdClear = 12;
}

// MARK: - The column

void SAcPalette::Construct(const FArguments& InArgs)
{
	Playground = InArgs._Playground;
	for (const auto& [Group, Items] : FAcPaletteItem::Groups())
	{
		Rows.Add({Group.ToUpper(), {}});
		for (const FAcPaletteItem& I : Items) Rows.Add({I.Title(), I});
	}
	// The controls (Swift's bar), top to bottom.
	const float SideY = TitleH + 24;
	for (int32 S = 0; S < UAcPlaygroundSubsystem::Sides; ++S)
	{
		const float X = 8 + S * 21.0f;
		Controls.Add({FBox2f(FVector2f(X, SideY), FVector2f(X + 18, SideY + 18)), S});
	}
	Controls.Add({FBox2f(FVector2f(8, SideY + 28), FVector2f(Width - 10, SideY + 48)), IdFog});
	Controls.Add({FBox2f(FVector2f(8, SideY + 52), FVector2f(Width - 10, SideY + 72)), IdUnlimited});
	Controls.Add({FBox2f(FVector2f(8, SideY + 80), FVector2f(Width - 10, SideY + 104)), IdClear});
	// Redrawn every frame: what is held changes from the keys and the game.
	ForceVolatile(true);
}

int32 SAcPalette::RowAt(const FVector2f Local) const
{
	if (Local.X < 0 || Local.X > Width || Local.Y < HeaderH) return INDEX_NONE;
	const float Y = Local.Y - HeaderH + Scroll;
	const int32 R = FMath::FloorToInt(Y / RowH);
	return Rows.IsValidIndex(R) && Rows[R].Item ? R : INDEX_NONE;
}

int32 SAcPalette::ControlAt(const FVector2f Local) const
{
	for (const FControl& C : Controls)
		if (C.Box.IsInsideOrOn(Local)) return C.Id;
	return INDEX_NONE;
}

void SAcPalette::Activate(const int32 Row)
{
	UAcPlaygroundSubsystem* P = Playground.Get();
	if (!P || !Rows.IsValidIndex(Row) || !Rows[Row].Item) return;
	P->PickRow(*Rows[Row].Item);
}

void SAcPalette::Press(const int32 Id)
{
	UAcPlaygroundSubsystem* P = Playground.Get();
	if (!P) return;
	if (Id >= 0 && Id < UAcPlaygroundSubsystem::Sides) P->SetTeam(Id);
	else if (Id == IdFog) P->SetFog(!P->IsFogOn());
	else if (Id == IdUnlimited) P->SetUnlimited(!P->IsUnlimited());
	else if (Id == IdClear) P->AskClear();
}

void SAcPalette::ClickItem(const FAcPaletteItem& Item)
{
	for (int32 R = 0; R < Rows.Num(); ++R)
	{
		if (!Rows[R].Item || *Rows[R].Item != Item) continue;
		// Scrolled out of sight: bring it in first, as a person would.
		const float Top = HeaderH + R * RowH - Scroll;
		if (Top < HeaderH || (ViewH > 0 && Top + RowH > ViewH))
		{
			Scroll = FMath::Clamp(R * RowH, 0.f, FMath::Max(0.f, ContentHeight() - (ViewH - HeaderH)));
		}
		// Its row's middle, through the same hit test as a click.
		const int32 Hit = RowAt(FVector2f(Width / 2, HeaderH + (R + 0.5f) * RowH - Scroll));
		Activate(Hit);
		return;
	}
}

FReply SAcPalette::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2f L(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
		const int32 C = ControlAt(L);
		Pressed = C != INDEX_NONE ? ControlBase + C : RowAt(L);
	}
	// Handled, but no focus: the keys stay the game's.
	return FReply::Handled();
}

FReply SAcPalette::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2f L(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
		const int32 C = ControlAt(L);
		const int32 Hit = C != INDEX_NONE ? ControlBase + C : RowAt(L);
		const int32 Was = Pressed;
		Pressed = INDEX_NONE;
		if (Hit != INDEX_NONE && Hit == Was)
		{
			if (Hit >= ControlBase) Press(Hit - ControlBase);
			else Activate(Hit);
		}
	}
	return FReply::Handled();
}

FReply SAcPalette::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2f L(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	const int32 C = ControlAt(L);
	Hovered = C != INDEX_NONE ? ControlBase + C : RowAt(L);
	return FReply::Handled();
}

void SAcPalette::OnMouseLeave(const FPointerEvent& Event)
{
	Hovered = INDEX_NONE;
	Pressed = INDEX_NONE;
}

FReply SAcPalette::OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	Scroll = FMath::Clamp(Scroll - Event.GetWheelDelta() * RowH * 2, 0.f, FMath::Max(0.f, ContentHeight() - (ViewH - HeaderH)));
	return FReply::Handled();
}

FCursorReply SAcPalette::OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const
{
	return FCursorReply::Cursor(Hovered != INDEX_NONE ? EMouseCursor::Hand : EMouseCursor::Default);
}

int32 SAcPalette::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2f Size(Geometry.GetLocalSize());
	ViewH = Size.Y;
	const UAcPlaygroundSubsystem* P = Playground.Get();
	Box(Out, Layer, Geometry, FBox2f(FVector2f::ZeroVector, FVector2f(Width, Size.Y)), GlassFill());
	// The rim on the side facing the view.
	Box(Out, Layer + 1, Geometry, FBox2f(FVector2f(Width - 2, 0), FVector2f(Width, Size.Y)), Steel());
	Box(Out, Layer + 1, Geometry, FBox2f(FVector2f(Width - 5, 0), FVector2f(Width - 4, Size.Y)), Inner());
	// And along its foot, over the console.
	Box(Out, Layer + 1, Geometry, FBox2f(FVector2f(0, Size.Y - 2), FVector2f(Width, Size.Y)), Steel());
	const int32 L = Layer + 2;
	if (!P) return L;

	// The list, clipped under the controls.
	const FSlateRect Clip = Geometry.GetLayoutBoundingRect(FSlateRect(0, HeaderH, Width - 5, Size.Y));
	Out.PushClip(FSlateClippingZone(Clip));
	for (int32 R = 0; R < Rows.Num(); ++R)
	{
		const float Y = HeaderH + R * RowH - Scroll;
		if (Y + RowH < HeaderH || Y > Size.Y) continue;
		const FRow& Row = Rows[R];
		if (!Row.Item)
		{
			// A group header (`isGroupRow`): small, semibold, secondary.
			Label(Row.Title, 12, HeaderInk(), EAcHAlign::Left, EAcVAlign::Center, EAcFontWeight::SemiBold)
				.Paint(Out, L, Geometry, FVector2f(12, Y + RowH / 2 + 2));
			continue;
		}
		const bool bHeld = P->Held() && *P->Held() == *Row.Item;
		const FBox2f B(FVector2f(6, Y + 1), FVector2f(Width - 10, Y + RowH - 1));
		if (bHeld)
		{
			Box(Out, L, Geometry, B, FAcHudStyle::Srgb(0.08, 0.3, 0.42, 1));
			Frame(Out, L + 1, Geometry, B, FAcHudStyle::Cyan(), 1);
		}
		else if (R == Hovered)
		{
			Box(Out, L, Geometry, B, FAcHudStyle::Srgb(0.06, 0.12, 0.18, 1));
		}
		// An owned item shows the side's colour on its left.
		if (Row.Item->Owned())
		{
			const FLinearColor C = FAcHudStyle::PlayerColor(P->Team());
			Box(Out, L + 1, Geometry, FBox2f(FVector2f(12, Y + 8), FVector2f(18, Y + 14)), bHeld ? C : C * FLinearColor(1, 1, 1, 0.5f));
		}
		Label(Row.Title, 15, bHeld ? FAcHudStyle::Text() : RowInk(), EAcHAlign::Left, EAcVAlign::Center, EAcFontWeight::Medium)
			.Paint(Out, L + 2, Geometry, FVector2f(24, Y + RowH / 2 + 0.5f));
	}
	Out.PopClip();

	// The title and the controls over it.
	const int32 H = L + 3;
	Box(Out, H, Geometry, FBox2f(FVector2f::ZeroVector, FVector2f(Width - 2, HeaderH)), GlassFill());
	Label(TEXT("PLAYGROUND"), 18, FAcHudStyle::Ice(), EAcHAlign::Left, EAcVAlign::Center).Paint(Out, H + 1, Geometry, FVector2f(10, TitleH / 2 + 1));
	Box(Out, H + 1, Geometry, FBox2f(FVector2f(8, TitleH - 1), FVector2f(Width - 10, TitleH)), Inner());
	Label(TEXT("SIDE"), 12, HeaderInk(), EAcHAlign::Left, EAcVAlign::Center, EAcFontWeight::SemiBold)
		.Paint(Out, H + 1, Geometry, FVector2f(10, TitleH + 13));
	Label(FAcHudStyle::PlayerName(P->Team()), 14, FAcHudStyle::PlayerTextColor(P->Team()), EAcHAlign::Right)
		.Paint(Out, H + 1, Geometry, FVector2f(Width - 12, TitleH + 13));
	for (const FControl& C : Controls)
	{
		const bool bHover = ControlBase + C.Id == Hovered;
		const bool bDown = bHover && Pressed == Hovered;
		if (C.Id < UAcPlaygroundSubsystem::Sides)
		{
			// A swatch in the side's colour; the chosen one ringed.
			const bool bSel = P->Team() == C.Id;
			Box(Out, H + 1, Geometry, C.Box.ExpandBy(-2.5f), FAcHudStyle::PlayerColor(C.Id));
			Frame(Out, H + 2, Geometry, C.Box,
				bSel ? FAcHudStyle::Ice() : bHover ? FAcHudStyle::Cyan() : FAcHudStyle::Srgb(0.3, 0.42, 0.52, 0.8), bSel ? 2.f : 1.f);
		}
		else if (C.Id == IdFog || C.Id == IdUnlimited)
		{
			// A check box and its label.
			const bool bOn = C.Id == IdFog ? P->IsFogOn() : P->IsUnlimited();
			const FBox2f Check(FVector2f(C.Box.Min.X + 2, C.Box.Min.Y + 2), FVector2f(C.Box.Min.X + 18, C.Box.Min.Y + 18));
			Box(Out, H + 1, Geometry, Check, FAcHudStyle::Srgb(0.05, 0.09, 0.14, 1));
			Frame(Out, H + 2, Geometry, Check, bHover ? FAcHudStyle::Ice() : bOn ? FAcHudStyle::Cyan() : FAcHudStyle::Srgb(0.3, 0.42, 0.52, 0.8));
			if (bOn) Box(Out, H + 3, Geometry, Check.ExpandBy(-4), FAcHudStyle::Cyan());
			Label(C.Id == IdFog ? TEXT("FOG OF WAR") : TEXT("UNLIMITED ORE & MH"), 14, bOn ? FAcHudStyle::Text() : RowInk())
				.Paint(Out, H + 1, Geometry, FVector2f(C.Box.Min.X + 26, C.Box.GetCenter().Y + 0.5f));
		}
		else
		{
			Button(Out, H + 1, Geometry, C.Box, TEXT("CLEAR…"), false, bHover, bDown);
		}
	}
	Box(Out, H + 1, Geometry, FBox2f(FVector2f(8, HeaderH - 1), FVector2f(Width - 10, HeaderH)), Inner());
	return H + 5;
}

// MARK: - The status line

void SAcPaletteStatus::Construct(const FArguments& InArgs)
{
	Playground = InArgs._Playground;
	ForceVolatile(true);
}

int32 SAcPaletteStatus::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const UAcPlaygroundSubsystem* P = Playground.Get();
	if (!P) return Layer;
	// `showStatus`: what the palette holds and why it can't go where the
	// pointer is, or what to do.
	FAcHudText S = Label(P->StatusText(), 14, FAcHudStyle::Srgb(0.78, 0.86, 0.92), EAcHAlign::Left, EAcVAlign::Center, EAcFontWeight::Medium);
	S.ShadowOffset = FVector2f(1, 1);
	S.ShadowColor = FLinearColor(0, 0, 0, 0.7f);
	const float W = S.Measure().X + 20;
	Box(Out, Layer, Geometry, FBox2f(FVector2f::ZeroVector, FVector2f(W, 24)), FAcHudStyle::Srgb(0.025, 0.045, 0.075, 0.72));
	Box(Out, Layer + 1, Geometry, FBox2f(FVector2f(0, 23), FVector2f(W, 24)), Inner());
	S.Paint(Out, Layer + 1, Geometry, FVector2f(10, 12.5f));
	return Layer + 2;
}

// MARK: - Clear's question

namespace
{
	constexpr float AskW = 440, AskH = 150;
}

void SAcPaletteConfirm::Construct(const FArguments& InArgs)
{
	Playground = InArgs._Playground;
	ForceVolatile(true);
}

int32 SAcPaletteConfirm::ButtonAt(const FVector2f Local) const
{
	for (int32 I = 0; I < 2; ++I)
		if (Buttons[I].IsInsideOrOn(Local)) return I;
	return INDEX_NONE;
}

FReply SAcPaletteConfirm::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton) Pressed = ButtonAt(FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())));
	return FReply::Handled();
}

FReply SAcPaletteConfirm::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const int32 Hit = ButtonAt(FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())));
	const int32 Was = Pressed;
	Pressed = INDEX_NONE;
	if (Hit != INDEX_NONE && Hit == Was)
	{
		if (UAcPlaygroundSubsystem* P = Playground.Get())
		{
			// Last: this widget goes away in either.
			if (Hit == 0) P->ClearNow();
			else P->CancelClear();
		}
	}
	return FReply::Handled();
}

FReply SAcPaletteConfirm::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	Hovered = ButtonAt(FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())));
	return FReply::Handled();
}

int32 SAcPaletteConfirm::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2f View(Geometry.GetLocalSize());
	// The playground dimmed behind (the alert).
	Box(Out, Layer, Geometry, FBox2f(FVector2f::ZeroVector, View), FLinearColor(0, 0, 0, 0.5f));
	const FVector2f O = ((View - FVector2f(AskW, AskH)) * 0.5f).RoundToVector();
	const FBox2f Panel(O, O + FVector2f(AskW, AskH));
	Box(Out, Layer + 1, Geometry, Panel, FAcHudStyle::Srgb(0.025, 0.045, 0.075, 0.96));
	Frame(Out, Layer + 2, Geometry, Panel, Steel(), 2);
	Frame(Out, Layer + 2, Geometry, Panel.ExpandBy(-4), Inner(), 1);
	const int32 L = Layer + 3;
	Label(TEXT("Clear the playground?"), 22, FAcHudStyle::Ice(), EAcHAlign::Left, EAcVAlign::Top).Paint(Out, L, Geometry, O + FVector2f(24, 22));
	Label(TEXT("Everything on it goes: units, buildings, ore, wells and doodads."), 15, FAcHudStyle::Soft(), EAcHAlign::Left, EAcVAlign::Top,
		EAcFontWeight::Medium)
		.Paint(Out, L, Geometry, O + FVector2f(24, 58));
	Buttons[1] = FBox2f(O + FVector2f(AskW - 24 - 104, AskH - 24 - 34), O + FVector2f(AskW - 24, AskH - 24));
	Buttons[0] = FBox2f(Buttons[1].Min - FVector2f(116, 0), Buttons[1].Max - FVector2f(116, 0));
	Button(Out, L, Geometry, Buttons[0], TEXT("CLEAR"), true, Hovered == 0, Pressed == 0 && Hovered == 0, 16);
	Button(Out, L, Geometry, Buttons[1], TEXT("CANCEL"), false, Hovered == 1, Pressed == 1 && Hovered == 1, 16);
	return L + 3;
}
