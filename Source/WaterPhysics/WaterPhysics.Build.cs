// Copyright Mans Isaksson. All Rights Reserved.

using UnrealBuildTool;

public class WaterPhysics : ModuleRules
{
    public WaterPhysics(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "PhysicsCore"
        });

        bool bIsDebugBuild = Target.Configuration == UnrealTargetConfiguration.Debug || Target.Configuration == UnrealTargetConfiguration.DebugGame;

        if (bIsDebugBuild)
		{
            PublicDependencyModuleNames.AddRange(new string[]
            {
                "Json",
                "JsonUtilities"
            });
		}

        PrivateDefinitions.Add("WITH_DEBUG_FORCE_CAPTURE=" + (bIsDebugBuild ? "1" : "0"));
        PrivateDefinitions.Add("WPC_WITH_CHAOS");
        
        bool bBuildWithDebug = Target.Type == TargetType.Editor 
            || (Target.Configuration != UnrealTargetConfiguration.Shipping && Target.Configuration != UnrealTargetConfiguration.Test);
        PublicDefinitions.Add("WITH_WATER_PHYS_DEBUG=" + (bBuildWithDebug ? "1" : "0"));
    }
}
