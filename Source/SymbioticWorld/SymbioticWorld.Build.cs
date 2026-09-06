using UnrealBuildTool;

public class SymbioticWorld : ModuleRules
{
	public SymbioticWorld(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"ProceduralMeshComponent",
			"AssetRegistry",
			"Json",
			"JsonUtilities",
			"Niagara",
			"Sockets",
			"Networking"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
