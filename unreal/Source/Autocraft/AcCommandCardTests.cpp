// Automation tests for the command card (D5): `FAcCommandCard` against the
// Swift `card`, `refusal` and `press` (GameController+Command.swift :176-:410),
// on hand-built states. Run headless:
//   UnrealEditor Autocraft.uproject -nullrhi -unattended -nosplash -nosound
//     -ExecCmds="Automation RunTests Autocraft.Card; Quit"
#include "AcCommandCard.h"

#include "Misc/AutomationTest.h"
#include "Rules.h"
#include "Simulation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	using SKind = ac::StructureKind;
	using UKind = ac::UnitKind;

	ac::Structure Building(int64 Id, SKind Kind, int64 Owner = 0)
	{
		ac::Structure S;
		S.id = Id;
		S.kind = Kind;
		S.owner = Owner;
		S.position = ac::Vec2((double)Id * 10, 0);
		S.hp = ac::Rules::hp(Kind);
		return S;
	}

	/// Two players, rich unless told otherwise, and the buildings given
	/// (plus a Citadel, id 99, last).
	ac::GameState State(std::vector<ac::Structure> Structures, int64 Ore = 1000, int64 Hydrogen = 1000)
	{
		ac::GameState S;
		S.players.assign(2, ac::Player{});
		S.players[0].ore = Ore;
		S.players[0].hydrogen = Hydrogen;
		S.score.assign(2, 0);
		S.structures = std::move(Structures);
		// A Citadel far off for supply (the sim works the cap out from buildings).
		S.structures.push_back(Building(99, SKind::citadel));
		S.nextID = 100;
		return S;
	}

	/// Button by slot (null when the card has none there).
	const FAcCardButton* AtSlot(const FAcCardInfo& Info, int32 Slot)
	{
		return Info.Buttons.FindByPredicate([Slot](const FAcCardButton& B) { return B.Slot == Slot; });
	}

	/// Captures the commands a press issues.
	struct FIssued
	{
		TArray<ac::Command> Commands;
		bool bAccept = true;
		auto Fn()
		{
			return [this](const ac::Command& C) { Commands.Add(C); return bAccept; };
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcCardCitadelTest, "Autocraft.Card.Citadel", Flags)
bool FAcCardCitadelTest::RunTest(const FString&)
{
	// The card `windowshot --select citadel` shows: the team's AI on, 0 ore.
	ac::Simulation Sim(State({Building(1, SKind::citadel)}, 0, 0));
	FAcCardState Card;
	const FAcCardInfo Info = FAcCommandCard::Card(Sim, Sim.state.structures[0], true, Card, 0);
	TestEqual(TEXT("nine buttons"), Info.Buttons.Num(), 9);
	const TCHAR* Titles[] = {TEXT("Prospector"), TEXT("Ore"), TEXT("Balanced"), TEXT("MH"), TEXT("Build"), TEXT("−100 ore"),
		TEXT("+100 ore"), TEXT("−100 MH"), TEXT("+100 MH")};
	const int32 Slots[] = {0, 5, 6, 7, 10, 11, 12, 13, 14};
	for (int32 I = 0; I < Info.Buttons.Num() && I < 9; ++I)
	{
		TestEqual(FString::Printf(TEXT("title %d"), I), Info.Buttons[I].Title, FString(Titles[I]));
		TestEqual(FString::Printf(TEXT("slot %d"), I), Info.Buttons[I].Slot, Slots[I]);
		TestEqual(FString::Printf(TEXT("key %d"), I), Info.Buttons[I].Key, FString::FromInt(I + 1));
	}
	const FAcCardButton& P = Info.Buttons[0];
	TestTrue(TEXT("prospector queues"), P.bQueues && P.bEnabled);
	TestEqual(TEXT("prospector why"), P.Why.Get(TEXT("")), FString(TEXT("Not enough ore: click to queue it")));
	TestEqual(TEXT("prospector price"), P.Ore, 50);
	TestTrue(TEXT("prospector time"), P.Time.IsSet() && *P.Time == 12.0);
	TestEqual(TEXT("prospector icon"), P.Icon, FString(TEXT("prospector")));
	TestTrue(TEXT("balanced lit (the default split)"), Info.Buttons[2].bLit && !Info.Buttons[1].bLit && !Info.Buttons[3].bLit);
	TestEqual(TEXT("ore icon"), Info.Buttons[1].Icon, FString(TEXT("ore")));
	TestEqual(TEXT("balanced has no icon"), Info.Buttons[2].Icon, FString());
	TestEqual(TEXT("MH icon"), Info.Buttons[3].Icon, FString(TEXT("derrick")));
	TestEqual(TEXT("build icon"), Info.Buttons[4].Icon, FString(TEXT("act.build")));
	TestTrue(TEXT("keep detail says the bank"), Info.Buttons[5].Detail.Get(TEXT("")).EndsWith(TEXT("Now: 0 ore, 0 MH")));
	TestEqual(TEXT("status says the split"), Info.Status, FString(TEXT("Idle · Prospectors: Balanced · Keep 0 ore, 0 MH")));

	// Without the team's AI: only the Prospector, refused outright.
	FAcCardState Alone;
	const FAcCardInfo Solo = FAcCommandCard::Card(Sim, Sim.state.structures[0], false, Alone, 0);
	TestEqual(TEXT("one button alone"), Solo.Buttons.Num(), 1);
	TestTrue(TEXT("greyed, not queued"), !Solo.Buttons[0].bEnabled && !Solo.Buttons[0].bQueues);
	TestEqual(TEXT("why alone"), Solo.Buttons[0].Why.Get(TEXT("")), FString(TEXT("Not enough ore")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcCardGarrisonTest, "Autocraft.Card.Garrison", Flags)
bool FAcCardGarrisonTest::RunTest(const FString&)
{
	ac::Simulation Sim(State({Building(1, SKind::garrison)}));
	FAcCardState Card;
	const FAcCardInfo Info = FAcCommandCard::Card(Sim, Sim.state.structures[0], true, Card, 0);
	// Ranger, Comet, Juggernaut on top; Lab bottom left; two upgrades in the middle.
	TestEqual(TEXT("six buttons"), Info.Buttons.Num(), 6);
	TestEqual(TEXT("ranger slot 0"), AtSlot(Info, 0) ? AtSlot(Info, 0)->Title : FString(), FString(TEXT("Ranger")));
	TestEqual(TEXT("comet slot 1"), AtSlot(Info, 1) ? AtSlot(Info, 1)->Title : FString(), FString(TEXT("Comet")));
	const FAcCardButton* J = AtSlot(Info, 2);
	TestTrue(TEXT("juggernaut slot 2"), J && J->Title == TEXT("Juggernaut"));
	if (J)
	{
		TestEqual(TEXT("juggernaut needs a lab"), J->Why.Get(TEXT("")), FString(TEXT("Needs a Lab: click to queue it")));
		TestEqual(TEXT("juggernaut detail"), J->Detail.Get(TEXT("")), FString(TEXT("Needs a Lab")));
		TestTrue(TEXT("juggernaut queues"), J->bQueues);
	}
	const FAcCardButton* Lab = AtSlot(Info, 10);
	TestTrue(TEXT("lab at slot 10"), Lab && Lab->Title == TEXT("Lab") && Lab->Icon == TEXT("lab") && Lab->Ore == 50 && Lab->Hydrogen == 25);
	if (Lab) TestTrue(TEXT("lab enabled"), Lab->bEnabled && !Lab->Why.IsSet());
	const FAcCardButton* Mini = AtSlot(Info, 5);
	TestTrue(TEXT("mini gun slot 5"), Mini && Mini->Title == TEXT("Mini gun") && Mini->Icon == TEXT("up.minigun"));
	const FAcCardButton* Aegis = AtSlot(Info, 6);
	TestTrue(TEXT("aegis slot 6"), Aegis && Aegis->Title == TEXT("Aegis shield"));
	if (Aegis) TestEqual(TEXT("aegis detail"), Aegis->Detail.Get(TEXT("")), FString(TEXT("Rangers +10 hit points")));
	if (Mini) TestEqual(TEXT("research needs a lab"), Mini->Why.Get(TEXT("")), FString(TEXT("Needs a Lab: click to queue it")));

	// With a finished Lab, already researching the mini gun, and the shield done.
	ac::Structure G = Building(1, SKind::garrison);
	G.addon = 2;
	ac::Structure L = Building(2, SKind::lab);
	L.parent = 1;
	L.research = ac::Upgrade::minigun;
	L.researchLeft = 25.0;
	ac::GameState S2 = State({G, L});
	S2.players[0].upgrades = std::set<ac::Upgrade>{ac::Upgrade::aegisShield};
	ac::Simulation Sim2(S2);
	FAcCardState Card2;
	const FAcCardInfo Info2 = FAcCommandCard::Card(Sim2, Sim2.state.structures[0], true, Card2, 0);
	TestEqual(TEXT("no lab button, shield gone: Ranger Comet Juggernaut Mini gun"), Info2.Buttons.Num(), 4);
	TestEqual(TEXT("status says research and lab"), Info2.Status, FString(TEXT("Researching Mini gun · Lab")));
	TestTrue(TEXT("progress 75%"), Info2.Progress.IsSet() && FMath::IsNearlyEqual(*Info2.Progress, 0.75));
	const FAcCardButton* Mini2 = AtSlot(Info2, 5);
	TestTrue(TEXT("already researching, not queued"), Mini2 && !Mini2->bEnabled && !Mini2->bQueues
		&& Mini2->Why.Get(TEXT("")) == TEXT("Already researching"));
	TestTrue(TEXT("juggernaut free with a lab"), AtSlot(Info2, 2) && AtSlot(Info2, 2)->bEnabled && !AtSlot(Info2, 2)->Why.IsSet());

	// The Lab's own card: its parent's research only, along the top row.
	FAcCardState LabCard;
	const FAcCardInfo LabInfo = FAcCommandCard::Card(Sim2, Sim2.state.structures[1], true, LabCard, 0);
	TestEqual(TEXT("lab card: one research"), LabInfo.Buttons.Num(), 1);
	TestTrue(TEXT("lab card slot 0"), LabInfo.Buttons.Num() == 1 && LabInfo.Buttons[0].Slot == 0 && LabInfo.Buttons[0].Title == TEXT("Mini gun"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcCardRefusalTest, "Autocraft.Card.Refusal", Flags)
bool FAcCardRefusalTest::RunTest(const FString&)
{
	using A = FAcCardAction;
	auto Why = [](const ac::Simulation& Sim, const A& Act, int32 Index)
	{
		const TOptional<FAcCardRefusal> R = FAcCommandCard::Refusal(Sim, Act, Sim.state.structures[Index]);
		return R ? R->Why + (R->bQueues ? TEXT(" [q]") : TEXT("")) : FString(TEXT("ok"));
	};
	{
		ac::Structure G = Building(1, SKind::garrison);
		G.buildLeft = 10.0;
		ac::Simulation Sim(State({G}));
		TestEqual(TEXT("under construction"), Why(Sim, A::Train(UKind::ranger), 0), FString(TEXT("Still under construction [q]")));
		TestEqual(TEXT("menu buttons never refuse"), Why(Sim, A::Split(ac::Harvest::ore), 0), FString(TEXT("ok")));
		TestEqual(TEXT("build always queues"), Why(Sim, A::Build(SKind::bastion), 0), FString(TEXT("Placed by the AI commander [q]")));
	}
	{
		ac::Structure G = Building(1, SKind::garrison);
		G.line = std::vector<UKind>(5, UKind::ranger);
		G.training = 3.0;
		ac::Simulation Sim(State({G}));
		TestEqual(TEXT("full queue"), Why(Sim, A::Train(UKind::ranger), 0), FString(TEXT("The queue is full [q]")));
		TestEqual(TEXT("busy for a lab"), Why(Sim, A::Addon(), 0), FString(TEXT("Busy training [q]")));
	}
	{
		ac::Structure G = Building(1, SKind::garrison);
		G.addon = 2;
		ac::Structure L = Building(2, SKind::lab);
		L.parent = 1;
		L.buildLeft = 5.0;
		ac::Simulation Sim(State({G, L}));
		TestEqual(TEXT("wait for the lab"), Why(Sim, A::Train(UKind::ranger), 0), FString(TEXT("Wait for the Lab [q]")));
		TestEqual(TEXT("already has a lab"), Why(Sim, A::Addon(), 0), FString(TEXT("Already has a Lab")));
		TestEqual(TEXT("research waits for the lab"), Why(Sim, A::Research(ac::Upgrade::minigun), 0), FString(TEXT("Needs a Lab [q]")));
	}
	{
		ac::GameState S = State({Building(1, SKind::garrison)}, 1000, 0);
		ac::Simulation Sim(S);
		TestEqual(TEXT("comet: MH"), Why(Sim, A::Train(UKind::comet), 0), FString(TEXT("Not enough MH [q]")));
		TestEqual(TEXT("ranger ok"), Why(Sim, A::Train(UKind::ranger), 0), FString(TEXT("ok")));
		Sim.state.players[0].supplyUsed = Sim.state.players[0].supplyCap;
		TestEqual(TEXT("supply"), Why(Sim, A::Train(UKind::ranger), 0), FString(TEXT("Not enough supply: build a Hab Dome [q]")));
		Sim.state.players[0].upgrades = std::set<ac::Upgrade>{ac::Upgrade::minigun};
		TestEqual(TEXT("already researched"), Why(Sim, A::Research(ac::Upgrade::minigun), 0), FString(TEXT("Already researched")));
	}
	{
		// A Lab with no parent: no building.
		ac::Simulation Sim(State({Building(2, SKind::lab)}));
		TestEqual(TEXT("orphan lab"), Why(Sim, A::Research(ac::Upgrade::minigun), 0), FString(TEXT("No building")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcCardPressTest, "Autocraft.Card.Press", Flags)
bool FAcCardPressTest::RunTest(const FString&)
{
	using A = FAcCardAction;
	// A rich Garrison, the team's AI on.
	{
		ac::Simulation Sim(State({Building(7, SKind::garrison)}));
		const ac::Structure St = Sim.state.structures[0];
		FAcCardState Card;
		FAcCommandCard::Card(Sim, St, true, Card, 0);
		FIssued Out;
		// A plain press it can afford: trains it now.
		FAcCommandCard::Press(Sim, St, 0, {}, true, 0, Card, 1, Out.Fn());
		TestTrue(TEXT("train now"), Out.Commands.Num() == 1 && Out.Commands[0] == ac::Command(ac::Command::Train{7, UKind::ranger}));
		TestFalse(TEXT("no note when it took"), Card.Note.IsSet());
		// Shift: five, queued for the commander.
		FAcCommandCard::Press(Sim, St, 0, FAcCardMods{true, false}, true, 0, Card, 1, Out.Fn());
		const ac::Command Five(ac::Command::Request{0, ac::Request::What(ac::Request::What::Unit{UKind::ranger}), 5, false, 7, std::nullopt});
		TestTrue(TEXT("shift queues five"), Out.Commands.Num() == 2 && Out.Commands[1] == Five);
		TestEqual(TEXT("note x5"), Card.Note.Get(TEXT("")), FString(TEXT("Queued for the commander: Ranger ×5")));
		TestTrue(TEXT("note for 2 s"), FMath::IsNearlyEqual(Card.NoteUntil, 3.0));
		// Option: keep making it.
		FAcCommandCard::Press(Sim, St, 1, FAcCardMods{false, true}, true, 0, Card, 1, Out.Fn());
		const ac::Command Keep(ac::Command::Request{0, ac::Request::What(ac::Request::What::Unit{UKind::comet}), 1, true, 7, std::nullopt});
		TestTrue(TEXT("option repeats"), Out.Commands.Num() == 3 && Out.Commands[2] == Keep);
		TestEqual(TEXT("note keep"), Card.Note.Get(TEXT("")), FString(TEXT("Queued for the commander: Comet (keep making)")));
		// Shift + Option on a unit: keep making, not five.
		FAcCommandCard::Press(Sim, St, 0, FAcCardMods{true, true}, true, 0, Card, 1, Out.Fn());
		const auto* Both = Out.Commands.Last().as<ac::Command::Request>();
		TestTrue(TEXT("shift+option: one, repeating"), Both && Both->count == 1 && Both->repeats);
		// Option on an upgrade: one, not repeating (only units repeat).
		FAcCommandCard::Press(Sim, St, 4, FAcCardMods{false, true}, true, 0, Card, 1, Out.Fn());
		const auto* Up = Out.Commands.Last().as<ac::Command::Request>();
		TestTrue(TEXT("option on research: once"), Up && Up->count == 1 && !Up->repeats && Up->what.is<ac::Request::What::Upgrade>());
		// A press the commander refuses.
		Out.bAccept = false;
		FAcCommandCard::Press(Sim, St, 0, FAcCardMods{true, false}, true, 0, Card, 1, Out.Fn());
		TestEqual(TEXT("can't queue"), Card.Note.Get(TEXT("")), FString(TEXT("Can't queue that")));
		// A plain press the sim refuses.
		FAcCommandCard::Press(Sim, St, 0, {}, true, 0, Card, 1, Out.Fn());
		TestEqual(TEXT("can't do"), Card.Note.Get(TEXT("")), FString(TEXT("Can't do that now")));
		// The note goes after 2 s.
		const FAcCardInfo Later = FAcCommandCard::Card(Sim, St, true, Card, 3.5);
		TestFalse(TEXT("note gone"), Later.Note.IsSet());
	}
	// Poor, the team's AI on: queued; with it off: the reason, nothing issued.
	{
		ac::Simulation Sim(State({Building(7, SKind::garrison)}, 0, 0));
		const ac::Structure St = Sim.state.structures[0];
		FAcCardState Card;
		FAcCommandCard::Card(Sim, St, true, Card, 0);
		FIssued Out;
		FAcCommandCard::Press(Sim, St, 0, {}, true, 0, Card, 0, Out.Fn());
		const auto* R = Out.Commands.Num() == 1 ? Out.Commands[0].as<ac::Command::Request>() : nullptr;
		TestTrue(TEXT("poor: queued once"), R && R->count == 1 && !R->repeats && R->at == std::optional<int64_t>(7));
		TestEqual(TEXT("note queued"), Card.Note.Get(TEXT("")), FString(TEXT("Queued for the commander: Ranger")));
		FAcCardState Off;
		FAcCommandCard::Card(Sim, St, false, Off, 0);
		FIssued None;
		FAcCommandCard::Press(Sim, St, 0, FAcCardMods{true, true}, false, 0, Off, 0, None.Fn());
		TestEqual(TEXT("no AI: nothing issued"), None.Commands.Num(), 0);
		TestEqual(TEXT("no AI: the reason"), Off.Note.Get(TEXT("")), FString(TEXT("Not enough ore")));
	}
	// A Lab's press: research at the Lab, a train request at its parent.
	{
		ac::Structure G = Building(1, SKind::garrison);
		G.addon = 2;
		ac::Structure L = Building(2, SKind::lab);
		L.parent = 1;
		ac::Simulation Sim(State({G, L}));
		FAcCardState Card;
		FAcCommandCard::Card(Sim, Sim.state.structures[1], true, Card, 0);
		FIssued Out;
		FAcCommandCard::Press(Sim, Sim.state.structures[1], 0, {}, true, 0, Card, 0, Out.Fn());
		TestTrue(TEXT("lab researches"), Out.Commands.Num() == 1 && Out.Commands[0] == ac::Command(ac::Command::Research{2, ac::Upgrade::minigun}));
		FAcCommandCard::Press(Sim, Sim.state.structures[1], 0, FAcCardMods{true, false}, true, 0, Card, 0, Out.Fn());
		const auto* R = Out.Commands.Last().as<ac::Command::Request>();
		TestTrue(TEXT("lab request at the parent, upgrades are not ×5'd away"), R && R->at == std::optional<int64_t>(1) && R->count == 5);
		// From the Garrison: research through its add-on.
		FAcCardState GCard;
		FAcCommandCard::Card(Sim, Sim.state.structures[0], true, GCard, 0);
		FIssued GOut;
		const int32 MiniIndex = GCard.Actions.IndexOfByKey(A::Research(ac::Upgrade::minigun));
		FAcCommandCard::Press(Sim, Sim.state.structures[0], MiniIndex, {}, true, 0, GCard, 0, GOut.Fn());
		TestTrue(TEXT("garrison researches at its lab"), GOut.Commands.Num() == 1 && GOut.Commands[0] == ac::Command(ac::Command::Research{2, ac::Upgrade::minigun}));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcCardCitadelPressTest, "Autocraft.Card.CitadelPress", Flags)
bool FAcCardCitadelPressTest::RunTest(const FString&)
{
	ac::Simulation Sim(State({Building(3, SKind::citadel)}));
	Sim.state.players[0].directives = ac::Directives();
	Sim.state.players[0].directives->keepOre = 50;
	const ac::Structure St = Sim.state.structures[0];
	FAcCardState Card;
	FAcCommandCard::Card(Sim, St, true, Card, 0);
	FIssued Out;
	// Split MH (button 4).
	FAcCardPress P = FAcCommandCard::Press(Sim, St, 3, {}, true, 0, Card, 0, Out.Fn());
	TestTrue(TEXT("split"), Out.Commands.Num() == 1 && Out.Commands[0] == ac::Command(ac::Command::Split{0, ac::Harvest::hydrogen}));
	TestTrue(TEXT("split refreshes the commander"), P.bCommander);
	TestEqual(TEXT("split log"), P.Log, FString(TEXT("card: Prospector split hydrogen")));
	// −100 ore from 50: floored at 0. +100 MH.
	FAcCommandCard::Press(Sim, St, 5, {}, true, 0, Card, 0, Out.Fn());
	TestTrue(TEXT("keep floored"), Out.Commands.Last() == ac::Command(ac::Command::Keep{0, 0, 0}));
	FAcCommandCard::Press(Sim, St, 8, {}, true, 0, Card, 0, Out.Fn());
	TestTrue(TEXT("keep +100 MH"), Out.Commands.Last() == ac::Command(ac::Command::Keep{0, 50, 100}));
	// Build (button 5) opens the menu: the eight buildings and Back.
	FAcCommandCard::Press(Sim, St, 4, {}, true, 0, Card, 0, Out.Fn());
	TestTrue(TEXT("menu open"), Card.bBuildMenu);
	const FAcCardInfo Menu = FAcCommandCard::Card(Sim, St, true, Card, 0);
	TestEqual(TEXT("menu: 9 buttons"), Menu.Buttons.Num(), 9);
	if (Menu.Buttons.Num() == 9)
	{
		TestTrue(TEXT("hab dome first, slot 0"), Menu.Buttons[0].Title == TEXT("Hab Dome") && Menu.Buttons[0].Slot == 0);
		TestTrue(TEXT("buildings queue, enabled, no why"), Menu.Buttons[1].bQueues && Menu.Buttons[1].bEnabled && !Menu.Buttons[1].Why.IsSet());
		TestEqual(TEXT("garrison detail"), Menu.Buttons[1].Detail.Get(TEXT("")), FString(TEXT("The AI commander places it, after a Hab Dome")));
		TestTrue(TEXT("back at 14"), Menu.Buttons[8].Slot == 14 && Menu.Buttons[8].Title == TEXT("Back") && Menu.Buttons[8].Icon == TEXT("act.back"));
		TestTrue(TEXT("sentinel at slot 7"), Menu.Buttons[7].Slot == 7);
	}
	// Garrison (button 2): a request for the AI, and the menu closes.
	FAcCommandCard::Press(Sim, St, 1, {}, true, 0, Card, 0, Out.Fn());
	const ac::Command Want(ac::Command::Request{0, ac::Request::What(ac::Request::What::Building{SKind::garrison}), 1, false, 3, std::nullopt});
	TestTrue(TEXT("building requested"), Out.Commands.Last() == Want);
	TestFalse(TEXT("menu closed"), Card.bBuildMenu);
	TestEqual(TEXT("note"), Card.Note.Get(TEXT("")), FString(TEXT("Queued for the commander: Garrison")));
	// Back closes the menu without orders.
	Card.bBuildMenu = true;
	FAcCommandCard::Card(Sim, St, true, Card, 0);
	const int32 Before = Out.Commands.Num();
	FAcCommandCard::Press(Sim, St, 8, {}, true, 0, Card, 0, Out.Fn());
	TestTrue(TEXT("back"), !Card.bBuildMenu && Out.Commands.Num() == Before);
	// Another building drops the menu.
	Card.bBuildMenu = true;
	ac::Simulation Sim2(State({Building(3, SKind::citadel), Building(4, SKind::citadel)}));
	FAcCommandCard::Card(Sim2, Sim2.state.structures[1], true, Card, 0);
	TestFalse(TEXT("new selection: no menu"), Card.bBuildMenu);
	// A stale index does nothing.
	FIssued None;
	FAcCommandCard::Press(Sim2, Sim2.state.structures[1], 42, {}, true, 0, Card, 0, None.Fn());
	TestEqual(TEXT("stale index"), None.Commands.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcCardRealSimTest, "Autocraft.Card.RealSim", Flags)
bool FAcCardRealSimTest::RunTest(const FString&)
{
	// Through the real `issue`: a train fills the queue, a Keep and a Split land.
	ac::Simulation Sim(State({Building(3, SKind::citadel)}));
	FAcCardState Card;
	FAcCommandCard::Card(Sim, Sim.state.structures[0], true, Card, 0);
	auto Issue = [&Sim](const ac::Command& C) { return Sim.issue(C); };
	FAcCommandCard::Press(Sim, *Sim.state.structure(3), 0, {}, true, 0, Card, 0, Issue);
	TestEqual(TEXT("a Prospector in training"), Sim.state.structure(3)->queueCount(), (int64_t)1);
	FAcCommandCard::Press(Sim, *Sim.state.structure(3), 1, {}, true, 0, Card, 0, Issue);
	TestTrue(TEXT("split ore"), Sim.state.players[0].orders().harvest == ac::Harvest::ore);
	const FAcCardPress K = FAcCommandCard::Press(Sim, *Sim.state.structure(3), 6, {}, true, 0, Card, 0, Issue);
	TestEqual(TEXT("keep 100 ore"), Sim.state.players[0].orders().keepOre, (int64_t)100);
	TestEqual(TEXT("keep log after the order"), K.Log, FString(TEXT("card: keep 100 ore, 0 MH")));
	const FAcCardInfo Info = FAcCommandCard::Card(Sim, *Sim.state.structure(3), true, Card, 0);
	TestTrue(TEXT("ore lit now"), Info.Buttons[1].bLit && !Info.Buttons[2].bLit);
	return true;
}

#endif
