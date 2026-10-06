#include "AcConsoleInfo.h"

#include "Rules.h"
#include "Simulation.h"
#include "Types.h"

namespace
{
	FString Utf8(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

	/// `GameController.splitName`.
	const TCHAR* SplitName(const ac::Harvest H)
	{
		switch (H)
		{
		case ac::Harvest::ore: return TEXT("Ore");
		case ac::Harvest::balanced: return TEXT("Balanced");
		default: return TEXT("MH");
		}
	}
}

FString FAcCardInfo::LayoutKey() const
{
	TArray<FString> Parts = {Title, Status, Icon.Get(TEXT("")), CargoIcon.Get(TEXT("")), Cargo.Get(TEXT("")),
		ProgressLabel.Get(TEXT("")), Energy.IsSet() ? TEXT("energy") : TEXT(""), Tips.IsEmpty() ? TEXT("") : TEXT("tips"),
		Level.IsSet() ? FString::Printf(TEXT("lv%d"), Level->Level) : FString()};
	Parts.Append(Queue);
	for (const FAcCardButton& B : Buttons)
	{
		Parts.Add(FString::Printf(TEXT("%d%s%s%s%d%d"), B.Slot, *B.Key, *B.Icon, *B.Title, B.bEnabled, B.bLit));
	}
	return FString::Join(Parts, TEXT("|"));
}

FString FAcConsoleInfo::IconName(const int32 UnitKind)
{
	static const TCHAR* Names[] = {TEXT("prospector"), TEXT("ranger"), TEXT("comet"), TEXT("firefly"), TEXT("juggernaut"),
		TEXT("dropship"), TEXT("longbow"), TEXT("kestrel"), TEXT("hailstorm"),
		TEXT("peregrine"), TEXT("atlas"), TEXT("scorpion")};
	return UnitKind >= 0 && UnitKind < (int32)UE_ARRAY_COUNT(Names) ? Names[UnitKind] : TEXT("");
}

FString FAcConsoleInfo::StructureIconName(const int32 StructureKind)
{
	static const TCHAR* Names[] = {TEXT("citadel"), TEXT("habdome"), TEXT("garrison"), TEXT("bastion"), TEXT("derrick"),
		TEXT("foundry"), TEXT("spacedock"), TEXT("lab"), TEXT("sentinel")};
	return StructureKind >= 0 && StructureKind < (int32)UE_ARRAY_COUNT(Names) ? Names[StructureKind] : TEXT("");
}

FAcCardInfo FAcConsoleInfo::ForStructure(const ac::Simulation& Sim, const ac::Structure& St, const bool bCommanded)
{
	using Kind = ac::StructureKind;
	const ac::GameState& S = Sim.state;
	FAcCardInfo Info;
	Info.Title = Utf8(ac::Simulation::title(St.kind));
	Info.Hp = FMath::Max(St.hp, 0.0);
	Info.MaxHp = ac::Rules::hp(St.kind);
	Info.Icon = StructureIconName((int32)St.kind);

	std::optional<ac::Structure> Host;
	if (St.kind == Kind::lab)
	{
		if (St.parent) Host = S.structure(*St.parent);
	}
	else
	{
		Host = St;
	}
	// The Lab doing the research for the host (itself, or its add-on).
	std::optional<ac::Structure> Lab;
	if (Host)
	{
		if (Host->kind == Kind::lab) Lab = Host;
		else if (Host->addon) Lab = S.structure(*Host->addon);
	}
	const ac::Player* Owner = St.owner >= 0 && St.owner < (int64)S.players.size() ? &S.players[(size_t)St.owner] : nullptr;

	if (!St.complete())
	{
		Info.Status = TEXT("Under construction");
		if (!St.builder && St.kind != Kind::lab) Info.Status += TEXT(" (no Prospector on it)");
		Info.ProgressLabel = FString(TEXT("BUILDING"));
		Info.Progress = St.progress();
	}
	else if (St.kind != Kind::lab && St.inTraining())
	{
		Info.Status = TEXT("Training ") + Utf8(ac::Simulation::title(*St.inTraining()));
		Info.ProgressLabel = FString(TEXT("TRAINING"));
		Info.Progress = St.trainingProgress().value_or(0.0);
	}
	else if (Lab && Lab->complete() && Lab->research && Lab->researchLeft)
	{
		Info.Status = TEXT("Researching ") + Utf8(ac::name(*Lab->research));
		Info.ProgressLabel = FString(TEXT("RESEARCH"));
		Info.Progress = 1 - *Lab->researchLeft / ac::time(*Lab->research);
	}
	else if (St.kind == Kind::habDome && Owner)
	{
		Info.Status = FString::Printf(TEXT("Supply %lld / %lld"), (long long)Owner->supplyUsed, (long long)Owner->supplyCap);
	}
	else if (St.kind == Kind::derrick)
	{
		int64 Left = 0;
		if (S.wells)
		{
			for (const ac::Well& W : *S.wells)
			{
				if (ac::distance(W.position, St.position) < 0.5)
				{
					Left = W.remaining;
					break;
				}
			}
		}
		Info.Status = FString::Printf(TEXT("%lld MH left"), (long long)Left);
	}
	else if (St.kind == Kind::bastion)
	{
		Info.Status = FString::Printf(TEXT("%lld / %lld Rangers inside"), (long long)(St.crew ? St.crew->size() : 0),
			(long long)Sim.bastionCapacity(St.owner));
	}
	else if (St.kind == Kind::sentinel)
	{
		Info.Status = St.target ? TEXT("Tracking a flyer") : TEXT("Watching the sky");
	}
	else
	{
		Info.Status = TEXT("Idle");
	}
	if (St.complete() && St.kind != Kind::lab)
	{
		if (St.line)
		{
			for (const ac::UnitKind K : *St.line) Info.Queue.Add(IconName((int32)K));
		}
		else if (St.inTraining())
		{
			Info.Queue.Add(IconName((int32)*St.inTraining()));
		}
	}
	if (Lab && St.kind != Kind::lab) Info.Status += Lab->complete() ? TEXT(" · Lab") : TEXT(" · Lab building");
	if (St.kind == Kind::citadel && St.complete() && bCommanded && Owner)
	{
		const ac::Directives D = Owner->orders();
		Info.Status += FString::Printf(TEXT(" · Prospectors: %s · Keep %lld ore, %lld MH"), SplitName(D.harvest),
			(long long)D.keepOre, (long long)D.keepHydrogen);
	}
	return Info;
}
