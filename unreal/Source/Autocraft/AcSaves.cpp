#include "AcSaves.h"

#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace
{
	FString Text(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

	/// `~/Library/Application Support` (UserSettingsDir is Epic's folder
	/// under it on the Mac, so not that).
	FString ApplicationSupport()
	{
#if PLATFORM_MAC
		return FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT("Library/Application Support"));
#else
		return FPlatformProcess::UserSettingsDir();
#endif
	}
}

FString AcSaves::Directory()
{
	FString Dir;
	if (FParse::Value(FCommandLine::Get(), TEXT("AcSaveDir="), Dir) && !Dir.IsEmpty())
	{
		return FPaths::ConvertRelativePathToFull(FPaths::LaunchDir(), Dir);
	}
	// A self-test (the playground's place/remove/Clear check) works in a
	// scratch store, as `windowshot` does: never the player's playground.
	if (FParse::Param(FCommandLine::Get(), TEXT("AcPlaygroundTest")))
	{
		return FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("autocraft-test-sessions"));
	}
	return FPaths::Combine(ApplicationSupport(), TEXT("Autocraft/Unreal/sessions"));
}

FString AcSaves::TrackingFile()
{
	const FString Sessions = Directory();
	const bool bOwnFolder = FParse::Param(FCommandLine::Get(), TEXT("AcPlaygroundTest"))
		|| [] { FString Dir; return FParse::Value(FCommandLine::Get(), TEXT("AcSaveDir="), Dir) && !Dir.IsEmpty(); }();
	return bOwnFolder ? FPaths::Combine(Sessions, TEXT("tracking.sqlite"))
					  : FPaths::Combine(FPaths::GetPath(Sessions), TEXT("tracking.sqlite"));
}

FString AcSaves::SwiftDirectory()
{
	return FPaths::Combine(ApplicationSupport(), TEXT("Autocraft/sessions"));
}

FString AcSaves::SwiftWindowFile()
{
	return FPaths::Combine(SwiftDirectory(), TEXT("window.json"));
}

std::string AcSaves::Signature(const ac::MapChoice& Choice)
{
	return "window|" + Choice.name();
}

FString AcSaves::ShortName(const ac::MapChoice& Choice)
{
	FString S = FString::Printf(TEXT("%s-%s"), *Text(std::string(ac::rawValue(Choice.style))).ToLower(),
		*Text(std::string(ac::rawValue(Choice.size))));
	if (Choice.players > 2) S += FString::Printf(TEXT("-%lld"), (long long)Choice.players);
	return S;
}

FString AcSaves::Title(const ac::MapChoice& Choice)
{
	FString S = Text(ac::title(Choice.style)) + TEXT(" · ") + Text(ac::title(Choice.size));
	if (Choice.players > 2) S += FString::Printf(TEXT(" · %lld players"), (long long)Choice.players);
	return S;
}

std::optional<ac::MapChoice> AcSaves::ChoiceNamed(const std::string& MapName)
{
	for (int64_t Players : {2, 4, 8})
	{
		for (ac::MapStyle Style : ac::allCases<ac::MapStyle>())
		{
			for (ac::MapSize Size : ac::allCases<ac::MapSize>())
			{
				const ac::MapChoice C{Style, Size, Players};
				if (C.name() == MapName) return C;
			}
		}
	}
	return std::nullopt;
}

ac::MapDefinition AcSaves::Build(const ac::MapChoice& Choice, const bool bDecorated)
{
	return Choice.players > 2 ? ac::WindowMaps::buildSquare(Choice, bDecorated) : ac::WindowMaps::build(Choice, bDecorated);
}

std::optional<std::vector<int64_t>> AcSaves::ParseTeams(const FString& In)
{
	const FString T = In.TrimStartAndEnd().ToLower();
	if (T.IsEmpty() || T == TEXT("ffa") || T == TEXT("none")) return std::nullopt;
	std::vector<int64_t> Teams;
	TArray<FString> Parts;
	if (T.Contains(TEXT("v")))
	{
		T.ParseIntoArray(Parts, TEXT("v"));
		for (int32 K = 0; K < Parts.Num(); ++K)
		{
			const int32 N = FCString::Atoi(*Parts[K]);
			if (N <= 0) return std::nullopt;
			for (int32 I = 0; I < N; ++I) Teams.push_back(K);
		}
	}
	else
	{
		T.ParseIntoArray(Parts, TEXT(","));
		for (const FString& P : Parts)
		{
			if (!P.IsNumeric()) return std::nullopt;
			Teams.push_back(FCString::Atoi64(*P));
		}
	}
	if (Teams.size() < 2) return std::nullopt;
	return Teams;
}

FString AcSaves::TeamsText(const std::optional<std::vector<int64_t>>& Teams)
{
	if (!Teams || Teams->empty()) return TEXT("ffa");
	// Runs of the same team, numbered 0, 1, 2... in order, all as long.
	TArray<int32> Runs;
	int64_t Expected = 0;
	bool bRuns = true;
	for (size_t I = 0; I < Teams->size(); ++I)
	{
		if (I == 0 || (*Teams)[I] != (*Teams)[I - 1])
		{
			if ((*Teams)[I] != Expected++) bRuns = false;
			Runs.Add(0);
		}
		Runs.Last() += 1;
	}
	for (const int32 R : Runs) bRuns = bRuns && R == Runs[0];
	if (bRuns && Runs.Num() > 1)
	{
		TArray<FString> N;
		for (const int32 R : Runs) N.Add(FString::FromInt(R));
		return FString::Join(N, TEXT("v"));
	}
	TArray<FString> N;
	for (const int64_t V : *Teams) N.Add(FString::Printf(TEXT("%lld"), (long long)V));
	return FString::Join(N, TEXT(","));
}

std::optional<AcSaves::FImported> AcSaves::Import(const FString& Path, FString& Error)
{
	std::string Why;
	std::optional<ac::Session> Loaded = ac::SessionStore::load(std::string(TCHAR_TO_UTF8(*Path)), &Why);
	if (!Loaded)
	{
		Error = Text(Why);
		return std::nullopt;
	}
	const std::optional<ac::MapChoice> Choice = ChoiceNamed(Loaded->mapName);
	if (!Choice)
	{
		Error = FString::Printf(TEXT("\"%s\" is not a window map (the playground and the wallpaper's maps are not played here)"),
			*Text(Loaded->mapName));
		return std::nullopt;
	}
	const ac::MapDefinition Map = Build(*Choice, false);
	if (Loaded->mapVersion != Map.version)
	{
		Error = FString::Printf(TEXT("saved on map version %lld, the map is now version %lld"), (long long)Loaded->mapVersion,
			(long long)Map.version);
		return std::nullopt;
	}
	if (Loaded->state.players.size() > Map.starts.size())
	{
		Error = TEXT("more players than the map has starts");
		return std::nullopt;
	}
	FImported Out{MoveTemp(*Loaded), *Choice};
	Out.Session.id = WindowId;
	Out.Session.signature = Signature(*Choice);
	Out.Session.fillGame();
	return Out;
}
