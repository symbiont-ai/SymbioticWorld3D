using UnrealBuildTool;
using System.Collections.Generic;

public class SymbioticWorldEditorTarget : TargetRules
{
	public SymbioticWorldEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("SymbioticWorld");
	}
}
