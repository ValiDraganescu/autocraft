using UnrealBuildTool;

public class AutocraftEditorTarget : TargetRules
{
	public AutocraftEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "Autocraft", "AutocraftCore" });
	}
}
