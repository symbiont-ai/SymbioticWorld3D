using UnrealBuildTool;
using System.Collections.Generic;

public class SymbioticWorldTarget : TargetRules
{
	public SymbioticWorldTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("SymbioticWorld");
	}
}
