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
			"ProceduralMeshComponent",  // AAcPilotAids: the driven unit's range ring
			"MovieSceneCapture"  // UAcShotSubsystem: -AcShotRaw reads frames back with FFrameGrabber
		});
		// Plain files the game reads from the project folder: not assets, so
		// nothing cooks them. They are staged loose, at the same paths, so the
		// code reads them as it does in the editor (docs/builds.md).
		if (Target.Type == TargetType.Game)
		{
			foreach (string Path in new string[] {
				"Resources/Sounds/music/*.mp3",  // UAcMusicPlayer::OwnMusicFolder
				"Content-src/launcher/*.jpg",  // FAcLauncherArt::Folder
				"Content/Audio/Sounds.json",  // UAcAudioDirector, UAcMusicDeck, UAcMusicPlayer
				"Content/Models/ModelCatalog.json",  // FAcModelCatalog
				"Content/UI/Cursors/*.tiff",  // UAcPointer::InstallCursors: the Mac's
				"Content/UI/Cursors/*.png"  // and the others' (LoadCursorFromPngs)
			})
			{
				RuntimeDependencies.Add("$(ProjectDir)/" + Path, StagedFileType.NonUFS);
			}
		}
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.Add("IOKit");  // FAcPerf: the GPU's per-process time (AcPerfMac.cpp)
		}
	}
}
