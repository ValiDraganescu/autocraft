#include "AcAppIcon.h"
#include "AcLog.h"
#include "Modules/ModuleManager.h"

/// The game module: starts the Autocraft log file (AcLog.h) and, in a game
/// run, puts up the Dock icon (AcAppIcon.h).
class FAutocraftModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		AcLog::Start();
		AcAppIcon::Apply();
	}
	virtual void ShutdownModule() override { AcLog::Stop(); }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FAutocraftModule, Autocraft, "Autocraft");
