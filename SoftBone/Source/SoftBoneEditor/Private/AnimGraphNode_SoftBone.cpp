// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.
#include "SoftBoneEditorPluginPrivatePCH.h"
#include "../Public/AnimGraphNode_SoftBone.h"
#include "CompilerResultsLog.h"

/////////////////////////////////////////////////////
// UAnimGraphNode_SpringBone

#define LOCTEXT_NAMESPACE "A3Nodes"

UAnimGraphNode_SoftBone::UAnimGraphNode_SoftBone(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAnimGraphNode_SoftBone::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	if (ForSkeleton->GetReferenceSkeleton().FindBoneIndex(Node.RootBone.BoneName) == INDEX_NONE 
	|| ForSkeleton->GetReferenceSkeleton().FindBoneIndex(Node.TipBone.BoneName) == INDEX_NONE)
	{
		MessageLog.Warning(*LOCTEXT("NoBoneToModify", "@@ - You must pick a root bone and a tip bone to simulate").ToString(), this);
	}

	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);
}

FText UAnimGraphNode_SoftBone::GetControllerDescription() const
{
	return LOCTEXT("SoftBonController", "SoftBone controller");
}

FText UAnimGraphNode_SoftBone::GetTooltipText() const
{
	return LOCTEXT("AnimGraphNode_SoftBone_Tooltip", "The SoftBone Controller is for simple jiggle bones which uses a fake physics solver with soft body's time integration and bone length constraints.");
}

FText UAnimGraphNode_SoftBone::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if ((TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle) && ((Node.RootBone.BoneName == NAME_None) || (Node.TipBone.BoneName == NAME_None)) )
	{
		return GetControllerDescription();
	}
	// @TODO: the bone can be altered in the property editor, so we have to 
	//        choose to mark this dirty when that happens for this to properly work
	else //if(!CachedNodeTitles.IsTitleCached(TitleType, this))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ControllerDescription"), GetControllerDescription());
		Args.Add(TEXT("RootBoneName"), FText::FromName(Node.RootBone.BoneName));
		Args.Add(TEXT("TipBoneName"), FText::FromName(Node.TipBone.BoneName));

		// FText::Format() is slow, so we cache this to save on performance
		if (TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
		{
			CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(LOCTEXT("AnimGraphNode_SoftBone_ListTitle", "{ControllerDescription} - RootBone : {RootBoneName} TipBone: {TipBoneName}"), Args), this);
		}
		else
		{
			CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(LOCTEXT("AnimGraphNode_SoftBone_Title", "{ControllerDescription}\nRootBone : {RootBoneName} TipBone: {TipBoneName}"), Args), this);
		}
	}

	return CachedNodeTitles[TitleType];
}

void UAnimGraphNode_SoftBone::CopyNodeDataFrom(const FAnimNode_Base* NewAnimNode)
{
	const FAnimNode_SoftBone* SoftBoneNode = static_cast<const FAnimNode_SoftBone*>(NewAnimNode);

	if (SoftBoneNode)
	{
		const TArray<FChainInfo>& Chains = SoftBoneNode->GetChainInfos();

		int32 NumChains = Chains.Num();

		if (BonePositionsArray.Num() != NumChains)
		{
			BonePositionsArray.Empty();
			BonePositionsArray.AddZeroed(NumChains);
		}

		for (int32 ChainIndex = 0; ChainIndex < NumChains; ChainIndex++)
		{
			TArray<FVector>& Positions = BonePositionsArray[ChainIndex];

			const TArray<FSoftBoneLink>& Links = Chains[ChainIndex].PrevBoneLinks;
			int32 NumLinks = Links.Num();

			// don't need to show a virtual link
			if (SoftBoneNode->bAllowTipBoneRotation)
			{
				NumLinks = Links.Num() - 1;
			}

			if (Positions.Num() != NumLinks)
			{
				Positions.Empty();
				Positions.AddUninitialized(NumLinks);
			}

			for (int32 PosIndex = 0; PosIndex < NumLinks; PosIndex++)
			{
				Positions[PosIndex] = Links[PosIndex].RenderPosition;
			}
		}
	}
}

void UAnimGraphNode_SoftBone::Draw(FPrimitiveDrawInterface* PDI, USkeletalMeshComponent* SkelMeshComp) const
{
	// It was impossible to get data from "Node.GetBoneLinks()" because Node in this class doesn't have correct positions at the moment.
	// So used BonePositions temporarily instead of "Node.GetBoneLinks()"

	int32 NumChains = BonePositionsArray.Num();

	for (int32 ChainIndex = 0; ChainIndex < NumChains; ChainIndex++)
	{
		const TArray<FVector>& Positions = BonePositionsArray[ChainIndex];
		int32 NumLinks = Positions.Num();

		if (NumLinks > 0)
		{
			PDI->DrawPoint(Positions[0], FColor::Red, 4.0f, SDPG_Foreground);

			for (int32 Index = 1; Index < NumLinks; Index++)
			{
				PDI->DrawPoint(Positions[Index], FColor::Red, 4.0f, SDPG_Foreground);
				PDI->DrawLine(Positions[Index - 1], Positions[Index], FColor::Red, SDPG_Foreground);
			}
		}
	}
}

void UAnimGraphNode_SoftBone::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const UProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(GetClass());

	// If the user changed curve data from editor, then change the curve type to Custom
	if (PropertyThatChanged && PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNode_SoftBone, WeightCurve))
	{
		Node.RestoringWeightType = ERestoringWeight::RW_Custom;
	}

	if (PropertyThatChanged && PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNode_SoftBone, RestoringWeightType))
	{
		Node.InitialzeWeightCurve();
	}

}

#undef LOCTEXT_NAMESPACE