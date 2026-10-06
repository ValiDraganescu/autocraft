#include "SAcNewGame.h"

#include "AcCabArt.h"
#include "AcHudStyle.h"
#include "AcHudText.h"
#include "AcMinimapBake.h"
#include "AcSaves.h"

#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateBrush.h"

#include "TerrainField.h"
#include "Types.h"

#include <vector>

struct SAcNewGame::FPreview
{
	FAcArtImage Image;
	ac::GroundRect Bounds;
	std::vector<ac::Ramp> Ramps;
	/// Each base's centre and, for a start, its index in `starts` (else -1).
	std::vector<std::pair<ac::Vec2, int32>> Bases;
	double PixelsPerCell = 2;
	int32 Starts = 0;
	/// With teams (C++ only): the player each base goes to, per team option
	/// (`4v4`...), as the new game deals them (`GameState::new_` with no
	/// score and round 0 is what `NewGame` plays), else -1.
	TMap<FString, std::vector<int32>> Owners;
};

namespace
{
	// The panel, in Swift points (MapPicker: a 560-point preview, 340 high).
	constexpr float PanelW = 620;
	constexpr float Pad = 28;
	constexpr float RowH = 30;
	constexpr float RowGap = 8;
	constexpr float LabelW = 100;
	constexpr float PreviewH = 340;
	constexpr float ButtonH = 34;

	enum ERow : int32
	{
		RowMap = 0,
		RowSize = 1,
		RowPlayers = 2,
		RowTeams = 3,
		RowAI = 4,
		RowFog = 5,
		RowStart = 10,
		RowCancel = 11,
		RowImport = 12,
	};

	FString Text(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

	FString Key(const ac::MapChoice& C) { return AcSaves::ShortName(C); }

	/// Ground size of a choice, cells (`MapSize.cells`; square maps are 2·half a side).
	FVector2D Cells(const ac::MapChoice& C)
	{
		if (C.players > 2)
		{
			const double Side = 2 * ac::WindowMaps::squareHalf(C.size, C.players);
			return FVector2D(Side, Side);
		}
		const ac::MapCells M = ac::cells(C.size);
		return FVector2D(M.width, M.depth);
	}

	/// Previews made so far (game thread only).
	TMap<FString, TSharedPtr<SAcNewGame::FPreview>>& Cache()
	{
		static TMap<FString, TSharedPtr<SAcNewGame::FPreview>> C;
		return C;
	}

	/// `MapPreview.render`'s inputs: the ground at 2 px a cell, ramps, bases.
	TSharedPtr<SAcNewGame::FPreview> MakePreview(const ac::MapChoice C)
	{
		const ac::MapDefinition Map = AcSaves::Build(C, /*decorated*/ false);
		const ac::TerrainField Field(Map);
		TSharedPtr<SAcNewGame::FPreview> P = MakeShared<SAcNewGame::FPreview>();
		P->PixelsPerCell = 2;
		P->Bounds = Map.bounds;
		P->Image = AcMinimapBake::Terrain(Field, Map.bounds, P->PixelsPerCell);
		P->Ramps = Map.ramps;
		P->Starts = (int32)Map.starts.size();
		for (size_t I = 0; I < Map.bases.size(); ++I)
		{
			int32 Start = -1;
			for (size_t S = 0; S < Map.starts.size(); ++S)
			{
				if (Map.starts[S] == (int64_t)I) Start = (int32)S;
			}
			P->Bases.push_back({Map.bases[I].center, Start});
		}
		for (const FString& Option : SAcNewGame::TeamOptions(C.players))
		{
			const std::optional<std::vector<int64_t>> Teams = AcSaves::ParseTeams(Option);
			if (!Teams || C.players <= 2) continue;
			const ac::GameState S = ac::GameState::new_(Map, std::nullopt, 0, Teams);
			std::vector<int32> Owner(Map.bases.size(), -1);
			for (size_t K = 0; K < S.players.size(); ++K)
			{
				if (S.players[K].start && *S.players[K].start >= 0 && *S.players[K].start < (int64_t)Owner.size())
				{
					Owner[(size_t)*S.players[K].start] = (int32)K;
				}
			}
			P->Owners.Add(Option, MoveTemp(Owner));
		}
		return P;
	}

	void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FBox2f& B, const FLinearColor& C)
	{
		const FVector2f S = B.GetSize();
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2D(S), FSlateLayoutTransform(FVector2D(B.Min))),
			FAcHudStyle::White(), ESlateDrawEffect::None, C);
	}

	/// A rectangle's outline `W` points wide, inside it (square corners: the
	/// rounded-box shader draws white on Metal, see SAcVictoryBanner).
	void Frame(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FBox2f& B, const FLinearColor& C, float W = 1)
	{
		const FVector2f S = B.GetSize();
		Box(Out, Layer, G, FBox2f(B.Min, B.Min + FVector2f(S.X, W)), C);
		Box(Out, Layer, G, FBox2f(FVector2f(B.Min.X, B.Max.Y - W), B.Max), C);
		Box(Out, Layer, G, FBox2f(B.Min + FVector2f(0, W), FVector2f(B.Min.X + W, B.Max.Y - W)), C);
		Box(Out, Layer, G, FBox2f(FVector2f(B.Max.X - W, B.Min.Y + W), FVector2f(B.Max.X, B.Max.Y - W)), C);
	}

	FAcHudText Label(const FString& S, float Size, const FLinearColor& C, EAcHAlign H = EAcHAlign::Left,
		EAcVAlign V = EAcVAlign::Center, EAcFontWeight Weight = EAcFontWeight::Bold)
	{
		FAcHudText T(S, FAcHudStyle::Font(Size, Weight), C, H, V);
		T.Accent = FLinearColor::Transparent;
		return T;
	}

	/// `Text` cut into lines no wider than `Width` at `Size` points.
	TArray<FString> Wrap(const FString& S, float Size, float Width)
	{
		TArray<FString> Words, Lines;
		S.ParseIntoArrayWS(Words);
		FString Line;
		for (const FString& W : Words)
		{
			const FString Try = Line.IsEmpty() ? W : Line + TEXT(" ") + W;
			if (!Line.IsEmpty() && Label(Try, Size, FLinearColor::White, EAcHAlign::Left, EAcVAlign::Top, EAcFontWeight::Medium).Measure().X > Width)
			{
				Lines.Add(Line);
				Line = W;
			}
			else
			{
				Line = Try;
			}
		}
		if (!Line.IsEmpty()) Lines.Add(Line);
		return Lines;
	}
}

// MARK: - Construction

void SAcNewGame::Construct(const FArguments& InArgs)
{
	Current = InArgs._Initial;
	Message = InArgs._Message;
	ImportNote = InArgs._ImportNote;
	OnStartDelegate = InArgs._OnStart;
	OnCancelDelegate = InArgs._OnCancel;
	OnImportDelegate = InArgs._OnImport;
	if (!TeamOptions(Current.Map.players).Contains(Current.Teams)) Current.Teams = TeamOptions(Current.Map.players)[0];
	SetCanTick(true);
	const FAcArtImage Disc = AcMinimapBake::Disc(64);
	DiscTexture.Reset(AcCabArt::ToTexture(Disc, TEXT("AcNewGameDisc")));
	DiscBrush = MakeShared<FSlateBrush>();
	DiscBrush->SetResourceObject(DiscTexture.Get());
	DiscBrush->ImageSize = FVector2D(64, 64);
	DiscBrush->DrawAs = ESlateBrushDrawType::Image;
	PreviewBrush = MakeShared<FSlateBrush>();
	PreviewBrush->DrawAs = ESlateBrushDrawType::Image;
	RequestPreview();
}

SAcNewGame::~SAcNewGame() = default;

TArray<FString> SAcNewGame::TeamOptions(const int64 Players)
{
	if (Players > 4) return {TEXT("4v4"), TEXT("2v2v2v2"), TEXT("ffa")};
	if (Players > 2) return {TEXT("2v2"), TEXT("ffa")};
	return {TEXT("1v1")};
}

FString SAcNewGame::TeamTitle(const FString& Teams)
{
	if (Teams == TEXT("ffa")) return TEXT("Free for all");
	return Teams.Replace(TEXT("v"), TEXT(" v "));
}

void SAcNewGame::SetChoice(const FAcNewGameChoice& Choice)
{
	Current = Choice;
	if (!TeamOptions(Current.Map.players).Contains(Current.Teams)) Current.Teams = TeamOptions(Current.Map.players)[0];
	LaidOutFor = FVector2f(-1, -1);
	RequestPreview();
	Invalidate(EInvalidateWidgetReason::Paint);
}

bool SAcNewGame::PreviewReady() const
{
	return Shown.IsValid() && ShownKey == Key(Current.Map);
}

void SAcNewGame::Start()
{
	OnStartDelegate.ExecuteIfBound(Current);
}

void SAcNewGame::Cancel()
{
	OnCancelDelegate.ExecuteIfBound();
}

void SAcNewGame::Import()
{
	OnImportDelegate.ExecuteIfBound();
}

// MARK: - The preview

void SAcNewGame::RequestPreview()
{
	const FString K = Key(Current.Map);
	if (const TSharedPtr<FPreview>* Hit = Cache().Find(K))
	{
		if (ShownKey != K)
		{
			Shown = *Hit;
			ShownKey = K;
			PreviewTexture.Reset(AcCabArt::ToTexture(Shown->Image, TEXT("AcNewGamePreview")));
			PreviewBrush->SetResourceObject(PreviewTexture.Get());
			PreviewBrush->ImageSize = FVector2D(Shown->Image.Width, Shown->Image.Height);
		}
		return;
	}
	if (Pending) return;  // Tick asks again when that one is in.
	PendingKey = K;
	Pending = MakeShared<TFuture<TSharedPtr<FPreview>>>(
		Async(EAsyncExecution::ThreadPool, [C = Current.Map]() { return MakePreview(C); }));
}

void SAcNewGame::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
	if (Pending && Pending->IsReady())
	{
		Cache().Add(PendingKey, Pending->Get());
		Pending.Reset();
		RequestPreview();
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	else if (!Pending && ShownKey != Key(Current.Map))
	{
		RequestPreview();
	}
}

FString SAcNewGame::FactsLine() const
{
	const FVector2D C = Cells(Current.Map);
	const int32 Bases = Shown && ShownKey == Key(Current.Map) ? (int32)Shown->Bases.size() : 0;
	const FString BasesText = Bases > 0 ? FString::Printf(TEXT("%d bases"), Bases) : FString(TEXT("… bases"));
	if (Current.Map.players > 2)
	{
		return FString::Printf(TEXT("%d × %d cells · %s · %lld starts, each on the same ground"), (int32)C.X, (int32)C.Y, *BasesText,
			(long long)Current.Map.players);
	}
	return FString::Printf(TEXT("%d × %d cells · %s · mirrored, so neither side has an edge"), (int32)C.X, (int32)C.Y, *BasesText);
}

// MARK: - Layout

void SAcNewGame::Layout(const FVector2f ViewSize) const
{
	Targets.Reset();
	float Y = 92;
	auto Row = [&](int32 RowId, float X0, const TArray<FString>& Labels, int32 Selected, float Size = 15.0f) {
		float X = X0;
		for (int32 I = 0; I < Labels.Num(); ++I)
		{
			const float W = FMath::Max(52.0f, Label(Labels[I], Size, FLinearColor::White).Measure().X + 26);
			FTarget T;
			T.Box = FBox2f(FVector2f(X, Y), FVector2f(X + W, Y + RowH));
			T.Row = RowId;
			T.Index = I;
			T.Label = Labels[I];
			T.bSelected = I == Selected;
			Targets.Add(T);
			X += W + 6;
		}
		return X;
	};
	// Map.
	TArray<FString> Styles;
	int32 StyleIndex = 0;
	for (const ac::MapStyle S : ac::allCases<ac::MapStyle>())
	{
		if (S == Current.Map.style) StyleIndex = Styles.Num();
		Styles.Add(Text(ac::title(S)));
	}
	Row(RowMap, Pad + LabelW, Styles, StyleIndex);
	Y += RowH + RowGap;
	// Size, with its cells (Swift: "Small (96 × 64 cells)").
	TArray<FString> Sizes;
	int32 SizeIndex = 0;
	for (const ac::MapSize S : ac::allCases<ac::MapSize>())
	{
		if (S == Current.Map.size) SizeIndex = Sizes.Num();
		const FVector2D C = Cells(ac::MapChoice{Current.Map.style, S, Current.Map.players});
		Sizes.Add(FString::Printf(TEXT("%s  %d×%d"), *Text(ac::title(S)), (int32)C.X, (int32)C.Y));
	}
	Row(RowSize, Pad + LabelW, Sizes, SizeIndex);
	Y += RowH + RowGap;
	// Players.
	const int32 PlayersIndex = Current.Map.players > 4 ? 2 : Current.Map.players > 2 ? 1 : 0;
	Row(RowPlayers, Pad + LabelW, {TEXT("2"), TEXT("4"), TEXT("8")}, PlayersIndex);
	Y += RowH + RowGap;
	// Teams.
	TArray<FString> Teams;
	const TArray<FString> Options = TeamOptions(Current.Map.players);
	for (const FString& O : Options) Teams.Add(TeamTitle(O));
	Row(RowTeams, Pad + LabelW, Teams, Options.IndexOfByKey(Current.Teams));
	Y += RowH + RowGap;
	// AI and fog on one line.
	const float AfterAI = Row(RowAI, Pad + LabelW, {TEXT("On"), TEXT("Off")}, Current.bAI ? 0 : 1);
	Row(RowFog, AfterAI + 18 + 110, {TEXT("On"), TEXT("Off")}, Current.bFog ? 0 : 1);
	Y += RowH + 14;
	// The preview.
	PreviewBox = FBox2f(FVector2f(Pad, Y), FVector2f(PanelW - Pad, Y + PreviewH));
	Y += PreviewH + 12;
	// Blurb and facts.
	const int32 Lines = Wrap(Text(ac::blurb(Current.Map.style)), 15, PanelW - 2 * Pad).Num();
	Y += Lines * 20 + 4 + 18 + 18;
	// Buttons.
	auto Button = [&](int32 RowId, float X, float W, const FString& S) {
		FTarget T;
		T.Box = FBox2f(FVector2f(X, Y), FVector2f(X + W, Y + ButtonH));
		T.Row = RowId;
		T.Label = S;
		T.bSelected = RowId == RowStart;
		Targets.Add(T);
	};
	Button(RowStart, PanelW - Pad - 120, 120, TEXT("START"));
	Button(RowCancel, PanelW - Pad - 120 - 10 - 110, 110, TEXT("CANCEL"));
	if (!ImportNote.IsEmpty())
	{
		Button(RowImport, Pad, Label(TEXT("IMPORT SWIFT GAME"), 15, FLinearColor::White).Measure().X + 30, TEXT("IMPORT SWIFT GAME"));
	}
	Y += ButtonH + 24;
	PanelSize = FVector2f(PanelW, Y);
	// Centred; shrunk to fit a small view, 20 points clear.
	PanelScale = FMath::Min(1.0f, FMath::Min((ViewSize.X - 40) / PanelSize.X, (ViewSize.Y - 40) / PanelSize.Y));
	PanelScale = FMath::Max(PanelScale, 0.3f);
	PanelOrigin = (ViewSize - PanelSize * PanelScale) * 0.5f;
	LaidOutFor = ViewSize;
}

int32 SAcNewGame::TargetAt(const FVector2f Local) const
{
	const FVector2f P = (Local - PanelOrigin) / PanelScale;
	for (int32 I = 0; I < Targets.Num(); ++I)
	{
		if (Targets[I].Box.IsInsideOrOn(P)) return I;
	}
	return INDEX_NONE;
}

void SAcNewGame::Activate(const FTarget& T)
{
	FAcNewGameChoice C = Current;
	switch (T.Row)
	{
	case RowMap: C.Map.style = ac::allCases<ac::MapStyle>()[T.Index]; break;
	case RowSize: C.Map.size = ac::allCases<ac::MapSize>()[T.Index]; break;
	case RowPlayers:
	{
		const int64 Players[] = {2, 4, 8};
		C.Map.players = Players[T.Index];
		C.Teams = TeamOptions(C.Map.players)[0];
		break;
	}
	case RowTeams: C.Teams = TeamOptions(C.Map.players)[T.Index]; break;
	case RowAI: C.bAI = T.Index == 0; break;
	case RowFog: C.bFog = T.Index == 0; break;
	case RowStart: Start(); return;
	case RowCancel: Cancel(); return;
	case RowImport: Import(); return;
	default: return;
	}
	SetChoice(C);
}

// MARK: - Input (modal: everything stays here)

FReply SAcNewGame::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		Pressed = TargetAt(FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())));
	}
	return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SAcNewGame::OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton && Pressed != INDEX_NONE)
	{
		const int32 Hit = TargetAt(FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())));
		const int32 Was = Pressed;
		Pressed = INDEX_NONE;
		if (Hit == Was && Targets.IsValidIndex(Hit))
		{
			const FTarget T = Targets[Hit];
			Activate(T);
		}
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	return FReply::Handled();
}

FReply SAcNewGame::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const int32 Hit = TargetAt(FVector2f(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())));
	if (Hit != Hovered)
	{
		Hovered = Hit;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	return FReply::Handled();
}

void SAcNewGame::OnMouseLeave(const FPointerEvent& Event)
{
	SLeafWidget::OnMouseLeave(Event);
	Hovered = INDEX_NONE;
	Pressed = INDEX_NONE;
}

FReply SAcNewGame::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Enter)
	{
		Start();
		return FReply::Handled();
	}
	if (Event.GetKey() == EKeys::Escape)
	{
		Cancel();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FCursorReply SAcNewGame::OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const
{
	return FCursorReply::Cursor(Hovered != INDEX_NONE ? EMouseCursor::Hand : EMouseCursor::Default);
}

// MARK: - Paint

int32 SAcNewGame::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2f View(Geometry.GetLocalSize());
	Layout(View);
	// The game dimmed behind (the alert's sheet).
	Box(Out, Layer, Geometry, FBox2f(FVector2f::ZeroVector, View), FLinearColor(0, 0, 0, 0.55f));
	const FGeometry P = Geometry.MakeChild(FVector2D(PanelSize), FSlateLayoutTransform(PanelScale, FVector2D(PanelOrigin)));
	const FBox2f Panel(FVector2f::ZeroVector, PanelSize);
	// The panel: dark glass, a steel rim and a cyan line inside it (Chrome).
	Box(Out, Layer + 1, P, Panel, FAcHudStyle::Srgb(0.025, 0.045, 0.075, 0.96));
	Frame(Out, Layer + 2, P, Panel, FAcHudStyle::Srgb(0.42, 0.5, 0.58, 0.9), 2);
	Frame(Out, Layer + 2, P, Panel.ExpandBy(-4), FAcHudStyle::Srgb(0.35, 0.85, 1, 0.28), 1);
	const int32 L = Layer + 3;

	// Title and message.
	Label(TEXT("NEW GAME"), 26, FAcHudStyle::Ice(), EAcHAlign::Left, EAcVAlign::Top).Paint(Out, L, P, FVector2f(Pad, 24));
	if (!Message.IsEmpty())
	{
		Label(Message, 15, FAcHudStyle::Soft(), EAcHAlign::Left, EAcVAlign::Top, EAcFontWeight::Medium).Paint(Out, L, P, FVector2f(Pad, 58));
	}

	// Row labels.
	const FLinearColor LabelColor = FAcHudStyle::Srgb(0.55, 0.7, 0.8);
	auto RowLabel = [&](const FString& S, float X, float RowY) {
		Label(S, 13, LabelColor, EAcHAlign::Left, EAcVAlign::Center, EAcFontWeight::SemiBold).Paint(Out, L, P, FVector2f(X, RowY + RowH / 2));
	};
	float FogLabelX = 0;
	for (const FTarget& T : Targets)
	{
		if (T.Index != 0) continue;
		const float RowY = T.Box.Min.Y;
		switch (T.Row)
		{
		case RowMap: RowLabel(TEXT("MAP"), Pad, RowY); break;
		case RowSize: RowLabel(TEXT("SIZE"), Pad, RowY); break;
		case RowPlayers: RowLabel(TEXT("PLAYERS"), Pad, RowY); break;
		case RowTeams: RowLabel(TEXT("TEAMS"), Pad, RowY); break;
		case RowAI: RowLabel(TEXT("AI"), Pad, RowY); break;
		case RowFog: FogLabelX = T.Box.Min.X - 110; RowLabel(TEXT("FOG OF WAR"), FogLabelX, RowY); break;
		default: break;
		}
	}

	// Options and buttons.
	for (int32 I = 0; I < Targets.Num(); ++I)
	{
		const FTarget& T = Targets[I];
		const bool bHover = I == Hovered;
		const bool bDown = I == Pressed && bHover;
		const bool bButton = T.Row >= RowStart;
		FLinearColor Fill = FAcHudStyle::Srgb(0.05, 0.09, 0.14, 1);
		FLinearColor Rim = FAcHudStyle::Srgb(0.3, 0.42, 0.52, 0.8);
		FLinearColor Ink = FAcHudStyle::Srgb(0.75, 0.85, 0.92);
		if (T.bSelected)
		{
			Fill = FAcHudStyle::Srgb(0.08, 0.3, 0.42, 1);
			Rim = FAcHudStyle::Cyan();
			Ink = FAcHudStyle::Text();
		}
		if (bHover) Rim = FAcHudStyle::Ice();
		if (bDown) Fill = FAcHudStyle::Srgb(0.12, 0.4, 0.55, 1);
		Box(Out, L, P, T.Box, Fill);
		Frame(Out, L + 1, P, T.Box, Rim, T.bSelected || bHover ? 1.5f : 1.0f);
		Label(T.Label, bButton ? 16 : 15, Ink, EAcHAlign::Center, EAcVAlign::Center).Paint(Out, L + 2, P, T.Box.GetCenter() + FVector2f(0, 0.5f));
	}

	// The preview: `MapPreview.render` scaled into the box (scaleProportionallyUpOrDown).
	Box(Out, L, P, PreviewBox, FAcHudStyle::Srgb(26 / 255.0, 26 / 255.0, 26 / 255.0));
	if (Shown && PreviewTexture && ShownKey == Key(Current.Map))
	{
		const FVector2f Box2 = PreviewBox.GetSize();
		const FVector2f Img((float)Shown->Image.Width, (float)Shown->Image.Height);
		const float S = FMath::Min(Box2.X / Img.X, Box2.Y / Img.Y);
		const FVector2f Size = Img * S;
		const FVector2f O = PreviewBox.Min + (Box2 - Size) * 0.5f;
		FSlateDrawElement::MakeBox(Out, L + 1, P.ToPaintGeometry(FVector2D(Size), FSlateLayoutTransform(FVector2D(O))), PreviewBrush.Get());
		const ac::GroundRect& B = Shown->Bounds;
		auto Point = [&](ac::Vec2 G) {
			return O + FVector2f((float)((G.x - B.minX) / B.width()) * Size.X, (float)((G.y - B.minZ) / B.depth()) * Size.Y);
		};
		const float K = (float)Shown->PixelsPerCell * S;
		for (const ac::Ramp& R : Shown->Ramps)
		{
			TArray<FVector2f> Line = {Point(R.low), Point(R.high)};
			FSlateDrawElement::MakeLines(Out, L + 2, P.ToPaintGeometry(), Line, ESlateDrawEffect::None, FLinearColor(1, 1, 1, 0.45f),
				false, (float)R.width * K);
		}
		const std::vector<int32>* Owners = Shown->Owners.Find(Current.Teams);
		for (size_t Bi = 0; Bi < Shown->Bases.size(); ++Bi)
		{
			const ac::Vec2 At = Shown->Bases[Bi].first;
			// With teams, each start in the colour of the player dealt it.
			const int32 Start = Owners && Bi < Owners->size() && Shown->Bases[Bi].second >= 0 ? (*Owners)[Bi] : Shown->Bases[Bi].second;
			const FLinearColor C = Start >= 0 ? FAcHudStyle::Blend(FAcHudStyle::PlayerColor(Start), 0.35, FLinearColor::White)
			                                  : FAcHudStyle::Srgb(0.95, 0.9, 0.5);
			const float R = (Start >= 0 ? 4.6f : 3.2f) * K;
			const FVector2f Q = Point(At);
			FSlateDrawElement::MakeBox(Out, L + 3,
				P.ToPaintGeometry(FVector2D(2 * R + 2, 2 * R + 2), FSlateLayoutTransform(FVector2D(Q - FVector2f(R + 1, R + 1)))),
				DiscBrush.Get(), ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.85f));
			FSlateDrawElement::MakeBox(Out, L + 4,
				P.ToPaintGeometry(FVector2D(2 * R, 2 * R), FSlateLayoutTransform(FVector2D(Q - FVector2f(R, R)))), DiscBrush.Get(),
				ESlateDrawEffect::None, C);
		}
	}
	else
	{
		Label(TEXT("Drawing the map…"), 15, FAcHudStyle::Soft(), EAcHAlign::Center, EAcVAlign::Center, EAcFontWeight::Medium)
			.Paint(Out, L + 1, P, PreviewBox.GetCenter());
	}
	Frame(Out, L + 5, P, PreviewBox, FAcHudStyle::Srgb(0.3, 0.42, 0.52, 0.6));

	// Blurb and facts (Swift: secondary and tertiary label colours).
	float Y = PreviewBox.Max.Y + 12;
	for (const FString& Line : Wrap(Text(ac::blurb(Current.Map.style)), 15, PanelW - 2 * Pad))
	{
		Label(Line, 15, FAcHudStyle::Srgb(0.78, 0.84, 0.9), EAcHAlign::Left, EAcVAlign::Top, EAcFontWeight::Medium).Paint(Out, L, P, FVector2f(Pad, Y));
		Y += 20;
	}
	Y += 4;
	Label(FactsLine(), 13, FAcHudStyle::Srgb(0.5, 0.58, 0.66), EAcHAlign::Left, EAcVAlign::Top, EAcFontWeight::Medium).Paint(Out, L, P, FVector2f(Pad, Y));
	Y += 18;
	if (!ImportNote.IsEmpty())
	{
		Label(ImportNote, 12, FAcHudStyle::Srgb(0.5, 0.58, 0.66), EAcHAlign::Left, EAcVAlign::Top, EAcFontWeight::Medium)
			.Paint(Out, L, P, FVector2f(Pad, Y));
	}
	return L + 8;
}
