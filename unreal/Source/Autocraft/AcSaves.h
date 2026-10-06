// Saved games of the Unreal game (chunk F1, GAME-LAYER.md §2.14): where
// they live, the window maps by name, and importing a Swift session.
//
// - The Unreal game keeps its own sessions (the user's answer, §6):
//   `~/Library/Application Support/Autocraft/Unreal/sessions/<id>.json`, the
//   same JSON as the Swift game's (`ac::SessionStore`). The window game is
//   always id `window`, signature `window|<map name>` (Swift
//   `GameController.windowSessionID`). `-AcSaveDir=PATH` puts them elsewhere
//   (tests, so a check never touches the real saves).
// - The Swift game's sessions stay in `~/Library/Application Support/
//   Autocraft/sessions/`. `Import` reads one (any path) and makes it the
//   Unreal window game: same state, map, AI/fog/listener settings and game
//   id; the Swift file is left alone.
#pragma once

#include "CoreMinimal.h"

#include "Session.h"
#include "WindowMaps.h"

#include <optional>
#include <vector>

namespace AcSaves
{
	/// The window game's session id (Swift `GameController.windowSessionID`).
	inline constexpr const char* WindowId = "window";

	/// Where the Unreal game's sessions are (see above; `-AcSaveDir`).
	AUTOCRAFT_API FString Directory();
	/// The leveling tracking database (docs/leveling.md, "Where it goes"):
	/// `tracking.sqlite` next to the sessions folder,
	/// `~/Library/Application Support/Autocraft/Unreal/tracking.sqlite`. With
	/// `-AcSaveDir=DIR` (or the playground test's scratch store) it sits
	/// inside that folder, so a test never touches the player's file.
	AUTOCRAFT_API FString TrackingFile();
	/// Where the Swift game's are (`SessionStore.defaultDirectory`).
	AUTOCRAFT_API FString SwiftDirectory();
	/// The Swift game's window session, if it has one.
	AUTOCRAFT_API FString SwiftWindowFile();

	/// The window session's signature for a map (`"window|" + name`).
	AUTOCRAFT_API std::string Signature(const ac::MapChoice& Choice);
	/// `highlands-medium`, `badlands-large-8`: the `-AcMap` form of a choice.
	AUTOCRAFT_API FString ShortName(const ac::MapChoice& Choice);
	/// "Highlands · Medium" (· 8 players).
	AUTOCRAFT_API FString Title(const ac::MapChoice& Choice);
	/// The window map whose full name is `MapName` (2, 4 or 8 players).
	AUTOCRAFT_API std::optional<ac::MapChoice> ChoiceNamed(const std::string& MapName);
	/// The map of a choice (2 players: the Swift mirrored maps; 4 or 8: square).
	AUTOCRAFT_API ac::MapDefinition Build(const ac::MapChoice& Choice, bool bDecorated = true);

	/// `4v4`, `2v2v2v2`, `1v1`, `0,0,1,1` → one team per player; `ffa` or
	/// empty → none (each for itself).
	AUTOCRAFT_API std::optional<std::vector<int64_t>> ParseTeams(const FString& Text);
	/// A state's teams as `4v4`/`2v2v2v2` when the teams are even and in runs,
	/// else `0,0,1,1`; `ffa` with no teams.
	AUTOCRAFT_API FString TeamsText(const std::optional<std::vector<int64_t>>& Teams);

	/// The result of `Import`.
	struct FImported
	{
		ac::Session Session;
		ac::MapChoice Choice;
	};
	/// Read a Swift (or Unreal) session file and make it the Unreal window
	/// game's session: its map must be a window map. `Error` says why not.
	AUTOCRAFT_API std::optional<FImported> Import(const FString& Path, FString& Error);
}
