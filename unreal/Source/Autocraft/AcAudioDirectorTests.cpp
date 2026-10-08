// The audio director's decisions against the Swift `AudioDirector`
// (Sources/Autocraft/Audio.swift), with no audio. Run:
//   UnrealEditor Autocraft.uproject -ExecCmds="Automation RunTests Autocraft.Audio.Rules; Quit"
//     -unattended -nullrhi -nosplash -nosound -log
#include "AcAudioDirector.h"
#include "AcPoseNewKinds.h"

#include "Misc/AutomationTest.h"

#include "Rules.h"
#include "Simulation.h"
#include "Types.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	int32 Count(const std::vector<FAcSoundCue>& Cues, FName Name)
	{
		int32 N = 0;
		for (const FAcSoundCue& C : Cues) N += C.Name == Name ? 1 : 0;
		return N;
	}

	const FAcSoundCue* First(const std::vector<FAcSoundCue>& Cues, FName Name)
	{
		for (const FAcSoundCue& C : Cues)
		{
			if (C.Name == Name) return &C;
		}
		return nullptr;
	}

	const FAcLoopWant* Loop(const std::vector<FAcLoopWant>& Ls, FName Name)
	{
		for (const FAcLoopWant& L : Ls)
		{
			if (L.Name == Name) return &L;
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcAudioWeaponsTest, "Autocraft.Audio.Rules.Weapons", Flags)
bool FAcAudioWeaponsTest::RunTest(const FString&)
{
	// The switch in `AudioDirector.shot` (Audio.swift:519-529).
	struct FRow { std::optional<ac::UnitKind> Kind; bool bAnchored; bool bTower; const char* Name; float Gain; int32 Rate; };
	const FRow Rows[] = {
		{std::nullopt, false, true, "missile", 0.35f, 5},
		{ac::UnitKind::comet, false, false, "pistol", 0.3f, 8},
		{ac::UnitKind::kestrel, false, false, "rockets", 0.45f, 6},
		{ac::UnitKind::hailstorm, false, false, "flak", 0.85f, 6},
		{ac::UnitKind::prospector, false, false, "weld", 0.3f, 6},
		{ac::UnitKind::firefly, false, false, "flame", 0.4f, 5},
		{ac::UnitKind::juggernaut, false, false, "breacher", 0.4f, 6},
		{ac::UnitKind::peregrine, false, false, "seekers", 0.45f, 6},
		{ac::UnitKind::atlas, false, false, "atlasshot", 0.35f, 3},
		{ac::UnitKind::scorpion, false, false, "scorpionsting", 0.3f, 3},
		{ac::UnitKind::longbow, true, false, "anchorshot", 0.55f, 4},
		{ac::UnitKind::longbow, false, false, "cannon", 0.7f, 5},
		{ac::UnitKind::ranger, false, false, "gun", 0.32f, 9},
		{ac::UnitKind::dropship, false, false, "gun", 0.32f, 9},
		{std::nullopt, false, false, "gun", 0.32f, 9},
	};
	for (const FRow& R : Rows)
	{
		const FAcAudioRules::FWeapon W = FAcAudioRules::WeaponOf(R.Kind, R.bAnchored, R.bTower);
		TestEqual(FString::Printf(TEXT("%hs name"), R.Name), FString(W.Name), FString(R.Name));
		TestEqual(FString::Printf(TEXT("%hs gain"), R.Name), W.Gain, R.Gain);
		TestEqual(FString::Printf(TEXT("%hs per second"), R.Name), W.PerSecond, R.Rate);
	}
	TestEqual(TEXT("prospector death"), FString(FAcAudioRules::DeathOf(ac::UnitKind::prospector)), FString(TEXT("prospectordeath")));
	TestEqual(TEXT("hailstorm voice"), FString(FAcAudioRules::VoiceOf(ac::UnitKind::hailstorm)), FString(TEXT("vhailstorm")));
	// The Scorpion's robot chirps are its own; the Peregrine pilot and the Atlas crew
	// speak with library voices of their own.
	TestEqual(TEXT("scorpion voice"), FString(FAcAudioRules::VoiceOf(ac::UnitKind::scorpion)), FString(TEXT("vscorpion")));
	TestEqual(TEXT("scorpion death"), FString(FAcAudioRules::DeathOf(ac::UnitKind::scorpion)), FString(TEXT("scorpiondeath")));
	TestEqual(TEXT("peregrine voice"), FString(FAcAudioRules::VoiceOf(ac::UnitKind::peregrine)), FString(TEXT("vperegrine")));
	TestEqual(TEXT("peregrine death"), FString(FAcAudioRules::DeathOf(ac::UnitKind::peregrine)), FString(TEXT("peregrinedeath")));
	TestEqual(TEXT("atlas voice"), FString(FAcAudioRules::VoiceOf(ac::UnitKind::atlas)), FString(TEXT("vatlas")));
	TestEqual(TEXT("atlas death"), FString(FAcAudioRules::DeathOf(ac::UnitKind::atlas)), FString(TEXT("atlasdeath")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcAudioNewKindsTest, "Autocraft.Audio.Rules.NewKinds", Flags)
bool FAcAudioNewKindsTest::RunTest(const FString&)
{
	FAcAudioRules R;
	R.Footfall(ac::Vec2(3, 3));
	TestEqual(TEXT("an Atlas footfall"), Count(R.TakeCues(), "atlasstep"), 1);
	R.PullAway(ac::UnitKind::atlas);
	TestEqual(TEXT("a driven Atlas's engine"), Count(R.TakeCues(), "atlasmove"), 1);
	R.Advance(3.0);
	R.PullAway(ac::UnitKind::scorpion);
	TestEqual(TEXT("a driven Scorpion's legs"), Count(R.TakeCues(), "scorpionmove"), 1);
	R.Advance(3.0);
	R.PullAway(ac::UnitKind::peregrine);
	TestEqual(TEXT("a driven Peregrine's shriek"), Count(R.TakeCues(), "peregrinepass"), 1);
	// A Scorpion digs in, locks on, digs out.
	ac::GameState S;
	S.players.resize(1);
	S.units = {ac::Unit(1, ac::UnitKind::scorpion, 0, ac::Vec2(1, 1), 0, ac::Unit::Task::idle)};
	R.Watch(S);
	TestEqual(TEXT("the first look is silent"), static_cast<int32>(R.TakeCues().size()), 0);
	S.units[0].anchored = true;
	R.Watch(S);
	TestEqual(TEXT("burying"), Count(R.TakeCues(), "scorpionbury"), 1);
	S.units[0].lockTarget = 5;
	R.Watch(S);
	R.Watch(S);
	TestEqual(TEXT("one chirp for a lock"), Count(R.TakeCues(), "scorpionlock"), 1);
	S.units[0].lockTarget.reset();
	S.units[0].anchored = false;
	R.Watch(S);
	TestEqual(TEXT("digging out"), Count(R.TakeCues(), "scorpionunbury"), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcAudioLimiterTest, "Autocraft.Audio.Rules.Limiter", Flags)
bool FAcAudioLimiterTest::RunTest(const FString&)
{
	FAcAudioRules R;
	R.Advance(10);
	// 20 rifle bursts at once: 9 a second (`allow("gun", perSecond: 9)`).
	for (int32 I = 0; I < 20; ++I) R.Shot(ac::UnitKind::ranger, false, ac::Vec2(1, 1), ac::Vec2(5, 5), 1, false);
	std::vector<FAcSoundCue> Cues = R.TakeCues();
	TestEqual(TEXT("9 rifle shots in a second"), Count(Cues, "gun"), 9);
	const FAcSoundCue* Gun = First(Cues, "gun");
	TestTrue(TEXT("gun at the shooter, in the fight's pool"),
		Gun && Gun->At == ac::Vec2(1, 1) && Gun->Pool == EAcSoundPool::Combat && FMath::IsNearlyEqual(Gun->Volume, 0.32f));
	R.Advance(0.5);
	R.Shot(ac::UnitKind::ranger, false, ac::Vec2(1, 1), ac::Vec2(5, 5), 1, false);
	TestEqual(TEXT("still full half a second on"), Count(R.TakeCues(), "gun"), 0);
	R.Advance(0.51);
	R.Shot(ac::UnitKind::ranger, false, ac::Vec2(1, 1), ac::Vec2(5, 5), 1, false);
	TestEqual(TEXT("a second on, one more"), Count(R.TakeCues(), "gun"), 1);
	// Deaths: 3 a second, machines blow up too.
	for (int32 I = 0; I < 5; ++I) R.Died(ac::UnitKind::firefly, ac::Vec2(2, 2));
	Cues = R.TakeCues();
	TestEqual(TEXT("3 Firefly deaths"), Count(Cues, "fireflydeath"), 3);
	TestEqual(TEXT("3 blasts"), Count(Cues, "blast"), 3);
	R.Advance(2);
	R.Died(ac::UnitKind::ranger, ac::Vec2(2, 2));
	Cues = R.TakeCues();
	TestEqual(TEXT("a Ranger cries out"), Count(Cues, "rangerdeath"), 1);
	TestEqual(TEXT("a Ranger does not blow up"), Count(Cues, "blast"), 0);
	// Buildings: no limit; a Hab Dome's is smaller.
	R.Destroyed(ac::StructureKind::habDome, ac::Vec2(3, 3));
	R.Destroyed(ac::StructureKind::garrison, ac::Vec2(3, 3));
	Cues = R.TakeCues();
	TestEqual(TEXT("two booms"), Count(Cues, "boom"), 2);
	TestTrue(TEXT("Hab Dome 0.7, Garrison 1"), Cues.size() == 2 && FMath::IsNearlyEqual(Cues[0].Volume, 0.7f) && FMath::IsNearlyEqual(Cues[1].Volume, 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcAudioStompTest, "Autocraft.Audio.Rules.Stomp", Flags)
bool FAcAudioStompTest::RunTest(const FString&)
{
	// An Atlas's Quake stomp: its own thud when the foot lands (0.3 s after
	// the event), once a stomp, at the spot, and not more than 3 a second.
	FAcAudioRules R;
	R.Advance(10);
	const ac::Vec2 At(4, 5);
	R.Stomp(At);
	R.Due();
	TestEqual(TEXT("not before the foot lands"), Count(R.TakeCues(), "stomp"), 0);
	R.Advance(AcPoseNew::AtlasStompImpact);
	R.Due();
	std::vector<FAcSoundCue> Cues = R.TakeCues();
	const FAcSoundCue* Thud = First(Cues, "stomp");
	TestTrue(TEXT("a thud where it stomped, at 0.9, in the combat pool"),
		Thud && Thud->At == At && FMath::IsNearlyEqual(Thud->Volume, AcSound::StompGain) && Thud->Pool == EAcSoundPool::Combat);
	TestEqual(TEXT("no blast with it"), Count(Cues, "blast"), 0);
	for (int32 K = 0; K < 6; ++K) R.Stomp(At);
	R.Advance(AcPoseNew::AtlasStompImpact);
	R.Due();
	TestEqual(TEXT("at most 3 a second"), Count(R.TakeCues(), "stomp"), 2);
	// The event itself is what plays it.
	R.Advance(2);
	ac::Simulation Sim(ac::GameState{});
	R.Consume({ac::GameEvent::Stomp{1, ac::Vec2(1, 1), 2.0}}, Sim);
	R.Advance(AcPoseNew::AtlasStompImpact);
	R.Due();
	TestEqual(TEXT("a Stomp event is a thud"), Count(R.TakeCues(), "stomp"), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcAudioOracleTest, "Autocraft.Audio.Rules.Oracle", Flags)
bool FAcAudioOracleTest::RunTest(const FString&)
{
	FAcAudioRules R;
	R.LocalPlayer = 2;
	R.Reach = [](ac::Vec2) { return 0.f; }; // nothing on screen: the Oracle still speaks, in your ear
	R.Advance(1);
	R.Shot(ac::UnitKind::ranger, false, ac::Vec2(0, 0), ac::Vec2(1, 0), 2, true);
	std::vector<FAcSoundCue> Cues = R.TakeCues();
	const FAcSoundCue* Alert = First(Cues, "alert");
	TestTrue(TEXT("the Oracle warns, in your ear, at 0.7"), Alert && Alert->Pool == EAcSoundPool::InEar && FMath::IsNearlyEqual(Alert->Volume, 0.7f));
	TestEqual(TEXT("the rifle is off screen"), Count(Cues, "gun"), 0);
	R.Advance(29);
	R.Shot(ac::UnitKind::ranger, false, ac::Vec2(0, 0), ac::Vec2(1, 0), 2, true);
	TestEqual(TEXT("not again within 30 s"), Count(R.TakeCues(), "alert"), 0);
	R.Advance(1.5);
	R.Shot(ac::UnitKind::ranger, false, ac::Vec2(0, 0), ac::Vec2(1, 0), 3, true);
	TestEqual(TEXT("another player's building: no"), Count(R.TakeCues(), "alert"), 0);
	R.Shot(ac::UnitKind::ranger, false, ac::Vec2(0, 0), ac::Vec2(1, 0), 2, false);
	TestEqual(TEXT("its unit: no"), Count(R.TakeCues(), "alert"), 0);
	R.Shot(ac::UnitKind::ranger, false, ac::Vec2(0, 0), ac::Vec2(1, 0), 2, true, /*minigun*/ true);
	Cues = R.TakeCues();
	TestEqual(TEXT("30 s on, again (a mini gun's round too)"), Count(Cues, "alert"), 1);
	TestEqual(TEXT("a mini gun Ranger's round makes no rifle sound"), Count(Cues, "gun"), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcAudioDelayedTest, "Autocraft.Audio.Rules.Delayed", Flags)
bool FAcAudioDelayedTest::RunTest(const FString&)
{
	FAcAudioRules R;
	R.Advance(5);
	// An anchored Longbow 8 cells off: the report now, the burst when the shell lands.
	const ac::Vec2 From(0, 0), At(8, 0);
	const double Flight = ac::Rules::flight(ac::UnitKind::longbow, true, 8);
	TestTrue(TEXT("a shell flies"), Flight > 0.1);
	R.Shot(ac::UnitKind::longbow, true, From, At, 1, false);
	std::vector<FAcSoundCue> Cues = R.TakeCues();
	TestEqual(TEXT("anchorshot now"), Count(Cues, "anchorshot"), 1);
	TestEqual(TEXT("no burst yet"), Count(Cues, "anchorhit"), 0);
	R.Advance(Flight * 0.9);
	R.Due();
	TestEqual(TEXT("still in the air"), Count(R.TakeCues(), "anchorhit"), 0);
	R.Advance(Flight * 0.11);
	R.Due();
	Cues = R.TakeCues();
	const FAcSoundCue* Hit = First(Cues, "anchorhit");
	TestTrue(TEXT("the burst where it lands, at 0.8"), Hit && Hit->At == At && FMath::IsNearlyEqual(Hit->Volume, 0.8f));
	// A Hailstorm's flak bursts at the flyer after its flight.
	R.Shot(ac::UnitKind::hailstorm, false, From, ac::Vec2(5, 0), 1, false);
	Cues = R.TakeCues();
	TestEqual(TEXT("flak now"), Count(Cues, "flak"), 1);
	R.Advance(ac::Rules::flight(ac::UnitKind::hailstorm, false, 5) + 0.001);
	R.Due();
	TestEqual(TEXT("its air burst later"), Count(R.TakeCues(), "flakhit"), 1);
	// A Sentinel: the lock-on servo only after 3 s quiet.
	R.Shot(std::nullopt, false, ac::Vec2(9, 9), At, 1, false, false, /*tower*/ true);
	Cues = R.TakeCues();
	TestEqual(TEXT("servo on the first shot"), Count(Cues, "sentineltrack"), 1);
	TestEqual(TEXT("missiles"), Count(Cues, "missile"), 1);
	R.Advance(2);
	R.Shot(std::nullopt, false, ac::Vec2(9, 9), At, 1, false, false, true);
	TestEqual(TEXT("no servo 2 s on"), Count(R.TakeCues(), "sentineltrack"), 0);
	R.Advance(3.5);
	R.Shot(std::nullopt, false, ac::Vec2(9, 9), At, 1, false, false, true);
	TestEqual(TEXT("servo after 3.5 s quiet"), Count(R.TakeCues(), "sentineltrack"), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcAudioVoicesTest, "Autocraft.Audio.Rules.Voices", Flags)
bool FAcAudioVoicesTest::RunTest(const FString&)
{
	FAcAudioRules R;
	bool bOnScreen = false;
	R.Reach = [&bOnScreen](ac::Vec2) { return bOnScreen ? 0.5f : 0.f; };
	R.Advance(10);
	// Off screen: no line, and the cool-down is not spent.
	R.Trained(ac::UnitKind::ranger, ac::Vec2(1, 1));
	TestEqual(TEXT("off screen: silent"), static_cast<int32>(R.TakeCues().size()), 0);
	bOnScreen = true;
	R.Trained(ac::UnitKind::ranger, ac::Vec2(1, 1));
	std::vector<FAcSoundCue> Cues = R.TakeCues();
	const FAcSoundCue* V = First(Cues, "vranger");
	TestTrue(TEXT("a Ranger reports in at 0.6 × reach on the deposits' voices"), V && FMath::IsNearlyEqual(V->Volume, 0.3f) && V->Pool == EAcSoundPool::Deposit);
	R.Advance(2.9);
	R.Trained(ac::UnitKind::prospector, ac::Vec2(1, 1));
	TestEqual(TEXT("one line every 3 s"), static_cast<int32>(R.TakeCues().size()), 0);
	R.Advance(0.2);
	R.Trained(ac::UnitKind::prospector, ac::Vec2(1, 1));
	TestEqual(TEXT("3 s on, the next"), Count(R.TakeCues(), "vprospector"), 1);
	// Deposits: no limit.
	for (int32 I = 0; I < 12; ++I) R.Deposited(ac::Vec2(2, 2));
	TestEqual(TEXT("every deposit"), Count(R.TakeCues(), "deposit"), 12);
	// The driven unit's marks: in your ear, whatever the reach.
	bOnScreen = false;
	R.HitMark(true, true);
	Cues = R.TakeCues();
	TestEqual(TEXT("kill ping"), Count(Cues, "killmark"), 1);
	TestEqual(TEXT("a clank on a building"), Count(Cues, "hitclank"), 1);
	for (int32 I = 0; I < 10; ++I) R.Step();
	TestEqual(TEXT("6 footfalls a second"), Count(R.TakeCues(), "step"), 6);
	R.PullAway(ac::UnitKind::firefly);
	R.PullAway(ac::UnitKind::firefly);
	R.PullAway(ac::UnitKind::ranger);
	TestEqual(TEXT("one engine every 2 s; none on foot"), Count(R.TakeCues(), "fireflymove"), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcAudioLoopsTest, "Autocraft.Audio.Rules.Loops", Flags)
bool FAcAudioLoopsTest::RunTest(const FString&)
{
	FAcAudioRules R;
	R.Variants = [](FName N) { return N == FName("wind") || N == FName("hum") || N == FName("heal") ? 1 : 3; };
	ac::GameState S;
	S.players.resize(2);
	S.time = 100;
	S.units.push_back(ac::Unit(7, ac::UnitKind::prospector, 0, ac::Vec2(4, 4), 0, ac::Unit::Task::mining));
	S.units.push_back(ac::Unit(8, ac::UnitKind::prospector, 0, ac::Vec2(5, 4), 0, ac::Unit::Task::toPatch));
	S.structures.push_back(ac::Structure(20, ac::StructureKind::citadel, 0, ac::Vec2(0, 0)));
	S.structures.push_back(ac::Structure(21, ac::StructureKind::citadel, 1, ac::Vec2(30, 30), 12.0));
	std::vector<FAcLoopWant> Ls;
	R.Loops(S, 0.1, Ls);
	TestEqual(TEXT("two wind layers"), static_cast<int32>(std::count_if(Ls.begin(), Ls.end(), [](const FAcLoopWant& L) { return L.Name == FName("wind"); })), 2);
	const FAcLoopWant* Wind = Loop(Ls, "wind");
	TestTrue(TEXT("wind at 0.16, panned"), Wind && FMath::IsNearlyEqual(Wind->Volume, 0.16f) && Wind->Pan.IsSet());
	int32 Hums = 0;
	for (const FAcLoopWant& L : Ls) Hums += L.Name == FName("hum") ? 1 : 0;
	TestEqual(TEXT("only the built Citadel hums"), Hums, 1);
	// The drill fades in over 0.15 s while it mines.
	auto Drill = [&](int64 Id) -> float
	{
		for (const FAcLoopWant& L : Ls)
		{
			if (L.Key == Id * 8) return L.Volume;
		}
		return -1.f;
	};
	TestTrue(TEXT("two thirds in after 0.1 s"), FMath::IsNearlyEqual(Drill(7), 0.25f * (0.1f / 0.15f), 1e-4f));
	TestTrue(TEXT("idle Prospector silent"), FMath::IsNearlyEqual(Drill(8), 0.f));
	Ls.clear();
	R.Loops(S, 0.1, Ls);
	TestTrue(TEXT("full after 0.15 s"), FMath::IsNearlyEqual(Drill(7), 0.25f));
	// It starts building: the drill fades out (0.4 s), then the welder fades in.
	S.units[0].task = ac::Unit::Task::building;
	Ls.clear();
	R.Loops(S, 0.2, Ls);
	const FAcLoopWant* Tool = nullptr;
	for (const FAcLoopWant& L : Ls) if (L.Key == 7 * 8) Tool = &L;
	TestTrue(TEXT("still the drill, half out"), Tool && Tool->Name == FName("drill") && FMath::IsNearlyEqual(Tool->Volume, 0.125f, 1e-4f));
	Ls.clear();
	R.Loops(S, 0.2, Ls);
	Ls.clear();
	R.Loops(S, 0.15, Ls);
	for (const FAcLoopWant& L : Ls) if (L.Key == 7 * 8) Tool = &L;
	TestTrue(TEXT("then the welder, at 0.4"), Tool && Tool->Name == FName("weld") && FMath::IsNearlyEqual(Tool->Volume, 0.4f, 1e-4f));
	// Mini guns: the nearest 6 of those firing.
	S.players[1].upgrades = std::set<ac::Upgrade>{ac::Upgrade::minigun};
	for (int32 I = 0; I < 9; ++I)
	{
		ac::Unit U(100 + I, ac::UnitKind::ranger, 1, ac::Vec2(10 + I, 10), 0, ac::Unit::Task::attacking);
		U.firedAt = S.time - 0.05;
		S.units.push_back(U);
	}
	R.Reach = [](ac::Vec2 P) { return static_cast<float>(1.0 / (1.0 + P.x)); };
	Ls.clear();
	R.Loops(S, 1.0 / 60, Ls);
	TArray<int64> Guns;
	for (const FAcLoopWant& L : Ls) if (L.Name == FName("minigun")) Guns.Add(L.Key / 8);
	Guns.Sort();
	TestEqual(TEXT("6 roars"), Guns.Num(), 6);
	TestTrue(TEXT("the nearest (loudest) six"), Guns.Num() == 6 && Guns[0] == 100 && Guns[5] == 105);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcAudioWatchTest, "Autocraft.Audio.Rules.Watch", Flags)
bool FAcAudioWatchTest::RunTest(const FString&)
{
	FAcAudioRules R;
	ac::GameState S;
	S.players.resize(1);
	ac::Unit Comet(1, ac::UnitKind::comet, 0, ac::Vec2(1, 1), 0, ac::Unit::Task::idle);
	ac::Unit Tank(2, ac::UnitKind::longbow, 0, ac::Vec2(2, 2), 0, ac::Unit::Task::idle);
	S.units = {Comet, Tank};
	R.Watch(S);
	TestEqual(TEXT("the first look is silent"), static_cast<int32>(R.TakeCues().size()), 0);
	S.units[0].jumpFrom = ac::Vec2(1, 1);
	S.units[1].anchored = true;
	R.Watch(S);
	std::vector<FAcSoundCue> Cues = R.TakeCues();
	TestEqual(TEXT("jet pack"), Count(Cues, "jetup"), 1);
	TestEqual(TEXT("anchor up"), Count(Cues, "anchorset"), 1);
	S.units[0].jumpFrom.reset();
	S.units[1].anchored = false;
	R.Watch(S);
	Cues = R.TakeCues();
	TestEqual(TEXT("landing"), Count(Cues, "jetland"), 1);
	TestEqual(TEXT("pack up"), Count(Cues, "anchorlift"), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcAudioWatchSightTest, "Autocraft.Audio.Rules.WatchSight", Flags)
bool FAcAudioWatchSightTest::RunTest(const FString&)
{
	// Swift watches every unit and sounds only the heard ones: an enemy
	// Longbow that anchored in the fog and then walks into sight is silent.
	FAcAudioRules R;
	ac::GameState S;
	S.players.resize(2);
	ac::Unit Tank(7, ac::UnitKind::longbow, 1, ac::Vec2(2, 2), 0, ac::Unit::Task::idle);
	ac::Unit Comet(8, ac::UnitKind::comet, 1, ac::Vec2(3, 3), 0, ac::Unit::Task::idle);
	S.units = {Tank, Comet};
	ac::Sight Fog;
	R.Watch(S, &Fog);
	S.units[0].anchored = true;
	S.units[1].jumpFrom = ac::Vec2(3, 3);
	R.Watch(S, &Fog);
	TestEqual(TEXT("unheard in the fog"), static_cast<int32>(R.TakeCues().size()), 0);
	ac::Sight Seen;
	Seen.ids = {7, 8};
	R.Watch(S, &Seen);
	TestEqual(TEXT("walking into sight already anchored / in the air"), static_cast<int32>(R.TakeCues().size()), 0);
	S.units[0].anchored = false;
	S.units[1].jumpFrom.reset();
	R.Watch(S, &Seen);
	std::vector<FAcSoundCue> Cues = R.TakeCues();
	TestEqual(TEXT("pack up in sight"), Count(Cues, "anchorlift"), 1);
	TestEqual(TEXT("landing in sight"), Count(Cues, "jetland"), 1);
	return true;
}

#endif
