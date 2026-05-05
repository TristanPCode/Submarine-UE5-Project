// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SubmarineProject : ModuleRules
{
	public SubmarineProject(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Landscape", "Niagara", "UMG", "Slate", "SlateCore", });

		PrivateDependencyModuleNames.AddRange(new string[] {  });

        PublicIncludePaths.AddRange(new string[]
        {
            Path.Combine(ModuleDirectory, ""),
            Path.Combine(ModuleDirectory, "CameraEdit"),
            Path.Combine(ModuleDirectory, "Death"),
            Path.Combine(ModuleDirectory, "HUD"),
            Path.Combine(ModuleDirectory, "Replay"),
            Path.Combine(ModuleDirectory, "Submarine"),
            Path.Combine(ModuleDirectory, "Torpedo"),
            Path.Combine(ModuleDirectory, "UI")
        });

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}
