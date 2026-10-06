#include "AcOutline.h"

namespace AcOutline
{
	EClass ClassOf(const uint8 Stencil)
	{
		if (Stencil >= StencilTeamBase && Stencil < StencilTeamBase + 8) return EClass::Team;
		if (Stencil == StencilFoe) return EClass::Foe;
		return EClass::None;
	}

	int32 TeamOf(const uint8 Stencil)
	{
		return ClassOf(Stencil) == EClass::Team ? int32(Stencil - StencilTeamBase) : -1;
	}

	uint8 TeamStencil(const int64 Team)
	{
		return uint8(StencilTeamBase + FMath::Clamp<int64>(Team, 0, 7));
	}

	uint8 StencilFor(const ac::UnitKind Kind, const std::optional<double>& Anchor, const int64 Owner, const bool bAllied)
	{
		if (Kind != ac::UnitKind::scorpion || Anchor.value_or(0.0) <= 0.0) return StencilNone;
		return bAllied ? TeamStencil(Owner) : StencilFoe;
	}

	FLinearColor Colour(const uint8 Stencil, const TConstArrayView<FLinearColor> TeamPaint)
	{
		switch (ClassOf(Stencil))
		{
		case EClass::Team:
		{
			const int32 T = TeamOf(Stencil);
			const FLinearColor C = TeamPaint.IsValidIndex(T) ? TeamPaint[T] : FLinearColor::White;
			return FLinearColor(C.R, C.G, C.B, TeamStrength);
		}
		case EClass::Foe: return FLinearColor(1.0f, 0.22f, 0.14f, FoeStrength);
		default: return FLinearColor(0, 0, 0, 0);
		}
	}
}
