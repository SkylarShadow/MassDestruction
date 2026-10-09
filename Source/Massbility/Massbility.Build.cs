// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Massbility : ModuleRules
{
	public Massbility(ReadOnlyTargetRules Target) : base(Target)
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
				"MassCore",
				"MassEntity",
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"MassCommon",
			}
			);
	}
}
