#include "SAcScoreboard.h"

#include "AcHudStyle.h"
#include "AcHudText.h"

#include "Types.h"

namespace
{
	const float ArmyPoints = 20.0f;
	const float ScorePoints = 24.0f;
	/// From a label's end to the next chip's centre.
	const float Gap = 16.0f;

	float TextWidth(const FString& S, float Points)
	{
		FAcHudText T(S, FAcHudStyle::Font(Points), FLinearColor::White);
		T.Accent = FLinearColor::Transparent;
		return T.Measure().X;
	}

	FString ArmyText(int64 N) { return FString::Printf(TEXT("%lld"), (long long)N); }
}

void SAcScoreboard::Construct(const FArguments& InArgs)
{
	SetCanTick(false);
	SetVisibility(EVisibility::HitTestInvisible);
}

void SAcScoreboard::Update(const ac::GameState& State, const int64 LocalPlayer)
{
	const int64 N = (int64)State.players.size();
	TArray<int64> Army, Things;
	Army.SetNumZeroed(N);
	Things.SetNumZeroed(N);
	for (const ac::Unit& U : State.units)
	{
		if (U.owner < 0 || U.owner >= N) continue;
		++Things[U.owner];
		if (U.soldier()) ++Army[U.owner];
	}
	for (const ac::Structure& S : State.structures)
	{
		if (S.owner >= 0 && S.owner < N) ++Things[S.owner];
	}

	// Sides by team, the local player's first, then in order of first member.
	TArray<FSide> NewSides;
	auto SideOf = [&](int64 Team) -> FSide&
	{
		for (FSide& S : NewSides)
		{
			if (S.Team == Team) return S;
		}
		FSide& S = NewSides.AddDefaulted_GetRef();
		S.Team = Team;
		return S;
	};
	SideOf(State.team(LocalPlayer));
	FString NewKey;
	for (int64 P = 0; P < N; ++P)
	{
		FSide& S = SideOf(State.team(P));
		FMember M;
		M.Player = P;
		M.Army = Army[P];
		M.bOut = Things[P] == 0;
		if (S.Members.IsEmpty()) S.Score = P < (int64)State.score.size() ? State.score[(size_t)P] : 0;
		S.Members.Add(M);
		NewKey += FString::Printf(TEXT("%lld:%lld:%lld:%d,"), (long long)S.Team, (long long)P, (long long)M.Army, M.bOut ? 1 : 0);
	}
	for (const FSide& S : NewSides) NewKey += FString::Printf(TEXT("s%lld,"), (long long)S.Score);
	if (NewKey == Key) return;
	Key = NewKey;
	Sides = MoveTemp(NewSides);
	Layout();
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SAcScoreboard::Layout()
{
	Middle.Reset();
	if (Sides.Num() == 2)
	{
		// HUD.buildScoreboard: chips 14 in from each end, labels 12 past
		// the chip, the games won centred.
		TArray<FString> Scores;
		for (const FSide& S : Sides) Scores.Add(FString::Printf(TEXT("%lld"), (long long)S.Score));
		Middle = FString::Join(Scores, TEXT("  :  "));
		float Left = 14, Right = 14;  // extents from each end
		for (FMember& M : Sides[0].Members)
		{
			M.ChipX = Left;
			M.LabelX = Left + 12;
			M.bRightAligned = false;
			Left = M.LabelX + TextWidth(ArmyText(M.Army), ArmyPoints) + Gap;
		}
		for (FMember& M : Sides[1].Members)
		{
			M.ChipX = Right;  // from the right end for now
			M.LabelX = Right + 12;
			M.bRightAligned = true;
			Right = M.LabelX + TextWidth(ArmyText(M.Army), ArmyPoints) + Gap;
		}
		const float Need = 2 * FMath::Max(Left, Right) + TextWidth(Middle, ScorePoints);
		Width = FMath::Max(MinWidth, FMath::CeilToFloat(Need));
		for (FMember& M : Sides[1].Members)
		{
			M.ChipX = Width - M.ChipX;
			M.LabelX = Width - M.LabelX;
		}
		return;
	}
	float X = 14;
	for (int32 I = 0; I < Sides.Num(); ++I)
	{
		FSide& S = Sides[I];
		for (FMember& M : S.Members)
		{
			M.ChipX = X;
			M.LabelX = X + 12;
			M.bRightAligned = false;
			X = M.LabelX + TextWidth(ArmyText(M.Army), ArmyPoints) + Gap;
		}
		S.ScoreX = X - 4;
		X = S.ScoreX + TextWidth(FString::Printf(TEXT("%lld"), (long long)S.Score), ScorePoints) + 10;
		S.RuleX = I + 1 < Sides.Num() ? X : 0;
		X += 14;
	}
	Width = FMath::Max(MinWidth, FMath::CeilToFloat(X - 4));
}

int32 SAcScoreboard::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	FAcHudStyle::PaintPlate(Out, Layer, Geometry, EAcPlate::Score, FSlateRect(0, 0, Width, Height));

	const float Cy = Height / 2;
	const FSlateFontInfo ArmyFont = FAcHudStyle::Font(ArmyPoints);
	const FSlateBrush* White = FAcHudStyle::White();
	for (const FSide& S : Sides)
	{
		for (const FMember& M : S.Members)
		{
			const float Fade = M.bOut ? 0.35f : 1.0f;
			FLinearColor Color = FAcHudStyle::PlayerTextColor(M.Player);
			// The chip: 10×10, a white 0.8 rim.
			FLinearColor Rim(1, 1, 1, 0.8f * Fade);
			FSlateDrawElement::MakeBox(Out, Layer + 1,
				Geometry.ToPaintGeometry(FVector2D(10.8, 10.8), FSlateLayoutTransform(FVector2D(M.ChipX - 5.4, Cy - 5.4))),
				White, ESlateDrawEffect::None, Rim);
			FLinearColor Fill = Color;
			Fill.A = Fade;
			FSlateDrawElement::MakeBox(Out, Layer + 2,
				Geometry.ToPaintGeometry(FVector2D(9.2, 9.2), FSlateLayoutTransform(FVector2D(M.ChipX - 4.6, Cy - 4.6))),
				White, ESlateDrawEffect::None, Fill);
			FAcHudText L(ArmyText(M.Army), ArmyFont, Color, M.bRightAligned ? EAcHAlign::Right : EAcHAlign::Left, EAcVAlign::Center);
			L.Accent = FLinearColor::Transparent;
			L.Paint(Out, Layer + 2, Geometry, FVector2f(M.LabelX, Cy + 1), Fade);
		}
		if (S.ScoreX >= 0)
		{
			FAcHudText T(FString::Printf(TEXT("%lld"), (long long)S.Score), FAcHudStyle::Font(ScorePoints), FAcHudStyle::Text(),
				EAcHAlign::Left, EAcVAlign::Center);
			T.Accent = FLinearColor::Transparent;
			T.Paint(Out, Layer + 2, Geometry, FVector2f(S.ScoreX, Cy + 1));
		}
		if (S.RuleX > 0)
		{
			FSlateDrawElement::MakeBox(Out, Layer + 1,
				Geometry.ToPaintGeometry(FVector2D(1, Height - 18), FSlateLayoutTransform(FVector2D(S.RuleX, 9))),
				White, ESlateDrawEffect::None, FAcHudStyle::Cyan().CopyWithNewOpacity(0.35f));
		}
	}
	if (!Middle.IsEmpty())
	{
		FAcHudText T(Middle, FAcHudStyle::Font(ScorePoints), FAcHudStyle::Text(), EAcHAlign::Center, EAcVAlign::Center);
		T.Accent = FLinearColor::Transparent;
		T.Paint(Out, Layer + 2, Geometry, FVector2f(Width / 2, Cy + 1));
	}
	return Layer + 4;
}
