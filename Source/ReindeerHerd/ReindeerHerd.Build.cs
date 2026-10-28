// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ReindeerHerd : ModuleRules
{
	public ReindeerHerd(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"Niagara",
			"RenderCore",
			"RHI"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"ReindeerHerd",
			"ReindeerHerd/Variant_Platforming",
			"ReindeerHerd/Variant_Platforming/Animation",
			"ReindeerHerd/Variant_Combat",
			"ReindeerHerd/Variant_Combat/AI",
			"ReindeerHerd/Variant_Combat/Animation",
			"ReindeerHerd/Variant_Combat/Gameplay",
			"ReindeerHerd/Variant_Combat/Interfaces",
			"ReindeerHerd/Variant_Combat/UI",
			"ReindeerHerd/Variant_SideScrolling",
			"ReindeerHerd/Variant_SideScrolling/AI",
			"ReindeerHerd/Variant_SideScrolling/Gameplay",
			"ReindeerHerd/Variant_SideScrolling/Interfaces",
			"ReindeerHerd/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
