// Copyright MassDestruction. All Rights Reserved.

using UnrealBuildTool;

public class MassbilityGAS : ModuleRules
{
	public MassbilityGAS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"DeveloperSettings",
				"GameplayTags",
				"GameplayAbilities",
				"Massbility",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"GameplayTasks",
				"MassCore",
				"MassEntity",
			});
	}
}
