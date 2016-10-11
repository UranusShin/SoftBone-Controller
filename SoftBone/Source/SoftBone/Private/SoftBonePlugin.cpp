// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "SoftBonePluginPrivatePCH.h"




class FSoftBonePlugin : public IModuleInterface
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FSoftBonePlugin, SoftBone)



void FSoftBonePlugin::StartupModule()
{
	
}


void FSoftBonePlugin::ShutdownModule()
{
	
}



