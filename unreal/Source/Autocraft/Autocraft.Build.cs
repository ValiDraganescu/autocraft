using UnrealBuildTool;

public class Autocraft : ModuleRules
{
	public Autocraft(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Every file compiles on its own: the sources keep file-local helpers
		// (Alpha, FPaint...) that clash once unity merges files. Until the
		// first commit, adaptive unity built every (untracked) file alone anyway.
		bUseUnity = false;
		CppStandard = CppStandardVersion.Cpp20;
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "AutocraftCore",
			"AudioExtensions",  // UAcMp3Wave (a procedural sound wave) links IAudioProxyDataFactory
			"MeshDescription", "StaticMeshDescription",  // AAcTerrain builds its static meshes at runtime
			"Json",  // FAcModelCatalog reads Content/Models/ModelCatalog.json
			"ApplicationCore",  // AAcHUD asks the screen's backing scale
			"RenderCore", "RHI",  // FAcPerf reads the render/RHI thread and GPU frame times
			"AudioMixer", "AudioMixerCore", "NonRealtimeAudioRenderer",  // UAcAudioDirector records the mix (offline)
			"ImageCore",  // UAcMusicPlayer draws covers
			"ProceduralMeshComponent"  // AAcPilotAids: the driven unit's range ring
		});
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.Add("IOKit");  // FAcPerf: the GPU's per-process time (AcPerfMac.cpp)
			PublicFrameworks.Add("CoreGraphics");  // the console art (AcCabArtMac.cpp)
		}
	}
}
