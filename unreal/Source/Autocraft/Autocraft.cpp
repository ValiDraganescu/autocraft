#include "AcLog.h"
#include "Modules/ModuleManager.h"

/// The game module: starts the Autocraft log file (AcLog.h).
class FAutocraftModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override { AcLog::Start(); }
	virtual void ShutdownModule() override { AcLog::Stop(); }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FAutocraftModule, Autocraft, "Autocraft");
