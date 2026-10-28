// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Hello : ModuleRules
{
	public Hello(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"GameplayAbilities", "GameplayTags", "GameplayTasks", "AnimationCore",
			"UMG", "Slate", "SlateCore"});
	}
}
