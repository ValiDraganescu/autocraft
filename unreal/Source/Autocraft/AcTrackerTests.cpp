// Automation tests for the tracking writer (AcTracker.h) on the engine's own
// SQLite (the plugin SQLiteCore; core-tests cover the store on the system's).
// A hand-built game; nothing is played. Run headless:
//   UnrealEditor Autocraft.uproject -nullrhi -unattended -nosplash -nosound
//     -ExecCmds="Automation RunTests Autocraft.Tracker; Quit" -TestExit="Automation Test Queue Empty"
#include "AcSaves.h"
#include "AcTracker.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#include "Leveling.h"
#include "WindowMaps.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/// A scratch database path, its files removed on destruction.
	struct FTempDb
	{
		FString Path = FPaths::Combine(FPlatformProcess::UserTempDir(), FString::Printf(TEXT("autocraft-tracker-%s.sqlite"), *FGuid::NewGuid().ToString()));
		~FTempDb()
		{
			for (const TCHAR* Suffix : {TEXT(""), TEXT("-wal"), TEXT("-shm")}) IFileManager::Get().Delete(*(Path + Suffix));
		}
		std::string Utf8() const { return std::string(TCHAR_TO_UTF8(*Path)); }
	};

	/// A game at 20 minutes: twelve kinds driven, nine levels each, all taken,
	/// every level stamped, eight players.
	ac::GameState BigGame()
	{
		const ac::MapDefinition Map = ac::WindowMaps::buildSquare(ac::MapChoice{ac::MapStyle::highlands, ac::MapSize::small, 8});
		ac::GameState State = ac::GameState::new_(Map);
		ac::PilotRecord Pilot;
		for (const ac::UnitKind Kind : ac::allCases<ac::UnitKind>())
		{
			ac::KindRecord Record;
			Record.xp = ac::Leveling::xp(10);
			Record.tally.seconds = 90;
			Record.tally.kills = 12;
			std::vector<ac::Standing> Standings(State.players.size(), ac::Standing{});
			for (int64_t L = 2; L <= 10; ++L)
			{
				if (const auto Offer = ac::perkOffer(Kind, L))
				{
					Record.picks.push_back(Offer->first);
					Record.stamps.push_back(ac::Stamp{L, double(L) * 100, double(L) * 10, Standings, double(L) * 100 + 5});
				}
			}
			Pilot.kinds[Kind] = Record;
		}
		State.players[0].pilot = Pilot;
		State.time = 1200;
		return State;
	}

	ac::Session SessionOf(const ac::GameState& State, const char* Game)
	{
		ac::Session S;
		S.id = "window";
		S.created = ac::Date::now();
		S.mapName = "Highlands · Small";
		S.mapVersion = 1;
		S.state = State;
		S.game = Game;
		S.gameStarted = S.created;
		return S;
	}

	std::string Cell(ac::TrackingStore& Store, const char* Sql)
	{
		const auto Rows = Store.rows(Sql);
		return Rows.empty() ? "<none>" : Rows[0][0];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcTrackerWritesTest, "Autocraft.Tracker.Writes", Flags)
bool FAcTrackerWritesTest::RunTest(const FString&)
{
	FTempDb Db;
	const ac::GameState State = BigGame();
	int32 Kinds = 0;
	{
		FAcTracker Tracker(Db.Path);
		const ac::Session Session = SessionOf(State, "GAME-A");
		const double T0 = FPlatformTime::Seconds();
		ac::TrackedGame Game = ac::TrackedGame::make(Session, State, Tracker.Build(), Tracker.OS(), ac::Date::now());
		const double MakeMs = (FPlatformTime::Seconds() - T0) * 1000.0;
		Kinds = (int32)Game.kinds.size();
		AddInfo(FString::Printf(TEXT("rows built on the game thread: %.3f ms (%d players, %d kinds, %d levels)"), MakeMs,
			(int32)Game.players.size(), Kinds, (int32)Game.levels.size()));
		Tracker.Write(Game);
		Tracker.Finish();
		FAcTracker::FStats First = Tracker.Stats();
		TestEqual(TEXT("one write"), First.Writes, 1);
		TestEqual(TEXT("it worked"), First.Failures, 0);
		AddInfo(FString::Printf(TEXT("first write (opens the file, makes the tables and views): %.3f ms"), First.LastMs));
		// The same game again, later (a level-up), then the end.
		Game.result = ac::TrackedGame::Result::won;
		Tracker.Write(Game);
		Game.result = ac::TrackedGame::Result::lost;
		Tracker.Write(Game);
		Tracker.Finish();
		const FAcTracker::FStats After = Tracker.Stats();
		TestEqual(TEXT("three writes"), After.Writes, 3);
		TestEqual(TEXT("no failure"), After.Failures, 0);
		AddInfo(FString::Printf(TEXT("a write of the whole game (upsert): last %.3f ms, slowest %.3f ms, mean %.3f ms"), After.LastMs, After.MaxMs, After.TotalMs / After.Writes));
	}
	std::string Error;
	std::unique_ptr<ac::TrackingStore> Store = ac::TrackingStore::open(Db.Utf8(), &Error);
	if (!TestNotNull(TEXT("the file reopens"), Store.get())) return false;
	TestEqual(TEXT("one game, written in order: the last write stands"), FString(UTF8_TO_TCHAR(Cell(*Store, "SELECT result FROM games").c_str())), FString(TEXT("lost")));
	TestEqual(TEXT("one games row"), FString(UTF8_TO_TCHAR(Cell(*Store, "SELECT COUNT(*) FROM games").c_str())), FString(TEXT("1")));
	TestEqual(TEXT("its kinds"), FString(UTF8_TO_TCHAR(Cell(*Store, "SELECT COUNT(*) FROM kinds").c_str())), FString::FromInt(Kinds));
	TestEqual(TEXT("its levels"), FString(UTF8_TO_TCHAR(Cell(*Store, "SELECT COUNT(*) FROM levels").c_str())), FString::FromInt(Kinds * 9));
	TestEqual(TEXT("its players"), FString(UTF8_TO_TCHAR(Cell(*Store, "SELECT COUNT(*) FROM players").c_str())), FString::FromInt((int32)State.players.size()));
	// The engine's SQLite is built for a custom platform without shared memory,
	// so a WAL request may stay in the rollback journal: the store takes the
	// mode SQLite grants. What this build has is logged.
	std::string Options;
	for (const auto& Row : Store->rows("PRAGMA compile_options")) Options += Row[0] + " ";
	AddInfo(FString::Printf(TEXT("engine SQLite %s, journal_mode %s; options: %s"), UTF8_TO_TCHAR(Cell(*Store, "SELECT sqlite_version()").c_str()),
		UTF8_TO_TCHAR(Cell(*Store, "PRAGMA journal_mode").c_str()), UTF8_TO_TCHAR(Options.c_str())));
	std::string MathError;
	Store->rows("SELECT sqrt(4.0)", {}, &MathError);
	AddInfo(FString::Printf(TEXT("sqrt in the engine's SQLite: %s"), MathError.empty() ? TEXT("yes") : UTF8_TO_TCHAR(MathError.c_str())));
	TestEqual(TEXT("the views are made"), FString(UTF8_TO_TCHAR(Cell(*Store, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'view'").c_str())), FString(TEXT("4")));
	TestEqual(TEXT("the install id"), (int32)Store->installID().size(), 36);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcTrackerRealStateTest, "Autocraft.Tracker.RealState", Flags)
bool FAcTrackerRealStateTest::RunTest(const FString&)
{
	// What a row set costs on the game thread with a real mid-game state (the
	// repo's bench fixture: 19.6 min, about 150 units), and its write.
	const FString File = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../bench/badlands-large.json")));
	const std::optional<ac::Session> Loaded = ac::SessionStore::load(std::string(TCHAR_TO_UTF8(*File)));
	if (!Loaded)
	{
		AddInfo(FString::Printf(TEXT("no fixture at %s: skipped"), *File));
		return true;
	}
	ac::Session Session = *Loaded;
	Session.fillGame();
	FTempDb Db;
	FAcTracker Tracker(Db.Path);
	double WorstMake = 0.0, TotalMake = 0.0;
	const int32 Rounds = 50;
	for (int32 I = 0; I < Rounds; ++I)
	{
		const double T0 = FPlatformTime::Seconds();
		ac::TrackedGame Game = ac::TrackedGame::make(Session, Session.state, Tracker.Build(), Tracker.OS(), ac::Date::now());
		const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
		WorstMake = FMath::Max(WorstMake, Ms);
		TotalMake += Ms;
		Tracker.Write(std::move(Game));
		Tracker.Finish();
	}
	const FAcTracker::FStats Stats = Tracker.Stats();
	AddInfo(FString::Printf(TEXT("%d units, %d players: rows built on the game thread: mean %.3f ms, worst %.3f ms; write on the worker: mean %.3f ms, slowest %.3f ms"),
		(int32)Session.state.units.size(), (int32)Session.state.players.size(), TotalMake / Rounds, WorstMake, Stats.TotalMs / Stats.Writes, Stats.MaxMs));
	TestEqual(TEXT("every write worked"), Stats.Failures, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcTrackerBrokenTest, "Autocraft.Tracker.BrokenFile", Flags)
bool FAcTrackerBrokenTest::RunTest(const FString&)
{
	// A path that cannot be a database (a folder): the writer says so once,
	// drops the games and does not hang `Finish`.
	FAcTracker Tracker(FPlatformProcess::UserTempDir());
	const ac::GameState State = ac::GameState::new_(ac::WindowMaps::build(ac::MapChoice{ac::MapStyle::highlands, ac::MapSize::small}));
	Tracker.Write(ac::TrackedGame::make(SessionOf(State, "GAME-B"), State, Tracker.Build(), Tracker.OS(), ac::Date::now()));
	Tracker.Finish();
	Tracker.Write(ac::TrackedGame::make(SessionOf(State, "GAME-B"), State, Tracker.Build(), Tracker.OS(), ac::Date::now()));
	Tracker.Finish();
	TestEqual(TEXT("nothing was written"), Tracker.Stats().Writes, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcTrackerPathTest, "Autocraft.Tracker.Path", Flags)
bool FAcTrackerPathTest::RunTest(const FString&)
{
	// With no -AcSaveDir the file is next to the sessions folder.
	if (!FParse::Param(FCommandLine::Get(), TEXT("AcSaveDir")))
	{
		TestEqual(TEXT("next to sessions"), AcSaves::TrackingFile(), FPaths::Combine(FPaths::GetPath(AcSaves::Directory()), TEXT("tracking.sqlite")));
		TestTrue(TEXT("under Autocraft/Unreal"), AcSaves::TrackingFile().EndsWith(TEXT("Autocraft/Unreal/tracking.sqlite")));
	}
	return true;
}

#endif
