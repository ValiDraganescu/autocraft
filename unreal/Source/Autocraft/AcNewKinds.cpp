#include "AcNewKinds.h"

#include "AcModelCatalog.h"

namespace AcNewKinds
{
	ac::UnitKind StandIn(const ac::UnitKind Kind)
	{
		switch (Kind)
		{
		case ac::UnitKind::peregrine: return ac::UnitKind::kestrel;
		case ac::UnitKind::atlas: return ac::UnitKind::juggernaut;
		default: return Kind;
		}
	}

	ac::UnitKind CockpitStandIn(const ac::UnitKind Kind)
	{
		switch (Kind)
		{
		case ac::UnitKind::peregrine: return ac::UnitKind::kestrel;
		case ac::UnitKind::atlas: return ac::UnitKind::juggernaut;
		case ac::UnitKind::scorpion: return ac::UnitKind::firefly;
		default: return Kind;
		}
	}

	namespace
	{
		const TCHAR* OwnCockpit(const ac::UnitKind Kind)
		{
			switch (Kind)
			{
			case ac::UnitKind::peregrine: return TEXT("cockpit_peregrine_blue");
			case ac::UnitKind::atlas: return TEXT("cockpit_atlas_blue");
			case ac::UnitKind::scorpion: return TEXT("cockpit_scorpion_blue");
			default: return nullptr;
			}
		}
	}

	bool HasCockpit(const ac::UnitKind Kind)
	{
		const TCHAR* Name = OwnCockpit(Kind);
		if (!Name) return true;
		// Asked every frame of a drive: once the catalog is read, the answer
		// does not change.
		static int8 Known[3] = {-1, -1, -1};
		const int32 I = (int32)Kind - (int32)ac::UnitKind::peregrine;
		const FAcModelCatalog& Catalog = FAcModelCatalog::Get();
		if (Known[I] < 0 && Catalog.IsLoaded()) Known[I] = Catalog.Find(FName(Name)) ? 1 : 0;
		return Known[I] > 0;
	}

	ac::UnitKind BorrowedCockpit(const ac::UnitKind Kind) { return HasCockpit(Kind) ? Kind : CockpitStandIn(Kind); }
}
