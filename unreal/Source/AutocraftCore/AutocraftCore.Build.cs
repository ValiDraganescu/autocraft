using UnrealBuildTool;

// The simulation and the AI, engine-free C++20 (see unreal/PORTING.md):
// Unreal builds it as a module, and unreal/core-tests builds the same
// files with plain clang++.
public class AutocraftCore : ModuleRules
{
	public AutocraftCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		// Every file compiles on its own: the sources keep file-local helpers
		// (Alpha, FPaint...) that clash once unity merges files. Until the
		// first commit, adaptive unity built every (untracked) file alone anyway.
		bUseUnity = false;
		CppStandard = CppStandardVersion.Cpp20;
		bEnableExceptions = false;
		bUseRTTI = false;
		PublicDependencyModuleNames.Add("Core");
		// TrackingStore.cpp speaks SQLite's C API: the engine's SQLiteCore plugin
		// provides it here (core-tests link the system's libsqlite3 instead).
		PrivateDependencyModuleNames.Add("SQLiteCore");
		PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory, "Plugins", "Runtime", "Database", "SQLiteCore", "Source", "ThirdParty", "sqlite"));
		PublicDefinitions.Add("JSON_NOEXCEPTION=1");
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "ThirdParty"));
		// The game module links what the public headers mark AUTOCRAFTCORE_API
		// (Public/AutocraftCoreApi.h); nothing else leaves the module.
	}
}
