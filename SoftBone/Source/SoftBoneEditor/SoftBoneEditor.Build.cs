// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class SoftBoneEditor : ModuleRules
	{
        public SoftBoneEditor(TargetInfo Target)
		{
            PublicIncludePaths.AddRange(
                new string[] { 
                    "SoftBoneEditor/Public",
                    "SoftBone/Public"
                }
            );

            PrivateIncludePaths.AddRange(
                new string[] { 
                    "SoftBoneEditor/Private",
                    "SoftBone/Private"
                }
            );

            PrivateDependencyModuleNames.AddRange(
                 new string[] {
                "SoftBone",
                "Core", 
				"CoreUObject", 
				"Engine", 
				"AnimGraphRuntime",
                "AnimGraph",
				"BlueprintGraph",       
                "UnrealEd"
                    
			    }
             );
		}
	}
}
