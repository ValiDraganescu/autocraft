// `LogAutocraft`: the game layer's log category, and the file it also goes
// to (`~/Library/Logs/Autocraft/unreal.log`, beside the Swift game's
// `autocraft.log`, in the Swift line format `yyyy-MM-dd HH:mm:ss.SSS text`,
// rolled to `unreal.log.1` at 5 MB on launch). The Unreal game keeps its own
// file so the two games never write or roll the same one.
#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAutocraft, Log, All);

namespace AcLog
{
	/// Open the file and start copying `LogAutocraft` lines (and warnings and
	/// errors of every category) into it. Called once by the module.
	void Start();
	/// Flush and close the file.
	void Stop();
	/// The file's path.
	FString FilePath();
}
