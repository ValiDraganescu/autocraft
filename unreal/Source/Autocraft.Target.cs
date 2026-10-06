using UnrealBuildTool;

public class AutocraftTarget : TargetRules
{
	public AutocraftTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "Autocraft", "AutocraftCore" });
	}
}
