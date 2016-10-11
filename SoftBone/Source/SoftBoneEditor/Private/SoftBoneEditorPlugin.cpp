// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "SoftBoneEditorPluginPrivatePCH.h"




class FSoftBoneEditorPlugin : public IModuleInterface
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FSoftBoneEditorPlugin, SoftBoneEditor)



void FSoftBoneEditorPlugin::StartupModule()
{

}


void FSoftBoneEditorPlugin::ShutdownModule()
{

}



