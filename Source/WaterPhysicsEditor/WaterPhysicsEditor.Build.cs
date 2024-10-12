// Copyright Mans Isaksson. All Rights Reserved.

using UnrealBuildTool;

public class WaterPhysicsEditor : ModuleRules
{
    public WaterPhysicsEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "WaterPhysics",
            "Slate",
            "SlateCore",
            "UnrealEd",
            "InputCore",
            "EditorStyle"
        });
    }
}
