// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class SoftBone : ModuleRules
	{
        public SoftBone(TargetInfo Target)
		{
            PublicIncludePaths.Add("SoftBone/Public");
            PrivateIncludePaths.Add("SoftBone/Private");

            PublicDependencyModuleNames.AddRange(
                new string[] { 
				"Core", 
				"CoreUObject", 
				"Engine", 
				"AnimGraphRuntime",
			    }
            );
		}
	}
}
