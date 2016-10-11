// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "SoftBonePluginPrivatePCH.h"
#include "../Public/AnimNode_SoftBone.h"
#include "AnimInstanceProxy.h"

DEFINE_LOG_CATEGORY_STATIC(LogSoftBone, Log, All);

/////////////////////////////////////////////////////
// FAnimNode_SpringBone

FAnimNode_SoftBone::FAnimNode_SoftBone()
	: Stiffness(0.1f)
	, DampingRatio(0.1f)
	, GravityScale(0.25f)
	, RestoringWeightType(ERestoringWeight::RW_Quadratic)
	, RemainingTime(0.f)
	, bBoneLengthConstraint(true)
	, bGuaranteeSameSimulationResult(true)
	, bAllowTipBoneRotation(true)
	, SimulationHertz(ESimulationHertz::SH_60Hz)
	, bUseWeightCurve(true)
{
	FRichCurve* Curve = WeightCurve.GetRichCurve();

#if WITH_EDITOR
	// initialize with Quadratic curve
	InitialzeWeightCurve();
#endif // #if WITH_EDITOR
}

void FAnimNode_SoftBone::Initialize(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize(Context);
	RemainingTime = 0.0f;

	for (int32 Index = 0; Index < ChainInfos.Num(); Index++)
	{
		ChainInfos[Index].Empty();
	}

	ChainInfos.Empty();
}

void FAnimNode_SoftBone::CacheBones(const FAnimationCacheBonesContext& Context)
{
	FAnimNode_SkeletalControlBase::CacheBones(Context);
}

void FAnimNode_SoftBone::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);

	RemainingTime += Context.GetDeltaTime();

	const USkeletalMeshComponent* SkelComp = Context.AnimInstanceProxy->GetSkelMeshComponent();
	const UWorld* World = SkelComp->GetWorld();
	check(World->GetWorldSettings());
	// Fixed step simulation at 60hz or 120hz
	FixedTimeStep = (1.f / (float)SimulationHertz) * World->GetWorldSettings()->GetEffectiveTimeDilation();

	DeltaTimeStep = Context.GetDeltaTime();
	GravityZ = World->GetGravityZ();
}

void FAnimNode_SoftBone::GatherDebugData(FNodeDebugData& DebugData)
{
	// @TODO : Add more output info?
	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf(TEXT("(DeltaTimeStep: %.3f%% RemainingTime: %.3f)"), DeltaTimeStep, RemainingTime);

	DebugData.AddDebugItem(DebugLine);
	ComponentPose.GatherDebugData(DebugData);
}

static void SetSoftBoneIndices(FCSPose<FCompactPose>& MeshBases, FCompactPoseBoneIndex RootIndex, FCompactPoseBoneIndex TipIndex, TArray<FCompactPoseBoneIndex>& BoneIndices)
{
	FCompactPoseBoneIndex BoneIndex = TipIndex;

	do
	{
		BoneIndices.Insert(BoneIndex, 0);
		BoneIndex = MeshBases.GetPose().GetParentBoneIndex(BoneIndex);
	} while (BoneIndex != RootIndex);
	BoneIndices.Insert(BoneIndex, 0);
}

static bool IsValidBonePair(const FBoneContainer& BoneContainer, const FBoneReference& RootBone, const FBoneReference& TipBone)
{
	return (TipBone.IsValid(BoneContainer)
		&& RootBone.IsValid(BoneContainer)
		&& BoneContainer.BoneIsChildOf(TipBone.BoneIndex, RootBone.BoneIndex));
}

void FAnimNode_SoftBone::InitializeBoneIndices(FCSPose<FCompactPose>& MeshBases)
{
	const FBoneContainer& BoneContainer = MeshBases.GetPose().GetBoneContainer();

	// sort chains by bone index order
	TArray<FBonePair> SortedPairArray;

	if (IsValidBonePair(BoneContainer, RootBone, TipBone))
	{
		SortedPairArray.Add(FBonePair(RootBone, TipBone));
	}

	for (int32 Index = 0; Index < AdditionalChains.Num(); Index++)
	{
		const FBonePair& Pair = AdditionalChains[Index];
		if (IsValidBonePair(BoneContainer, Pair.RootBone, Pair.TipBone))
		{
			SortedPairArray.Add(FBonePair(AdditionalChains[Index].RootBone, AdditionalChains[Index].TipBone));
		}
	}

	struct FCompareRootBone
	{
		FORCEINLINE bool operator()(const FBonePair& A, const FBonePair& B) const
		{
			return (A.RootBone.BoneIndex < B.RootBone.BoneIndex);
		}
	};

	// Sort by root bone indices
	// It could be possible to sort all OutBoneTransforms at the end of Evaluation but selected this way to reduce sorting cost
	SortedPairArray.Sort(FCompareRootBone());

	ChainInfos.Empty();
	ChainInfos.AddZeroed(SortedPairArray.Num());

	for (int32 Index = 0; Index < SortedPairArray.Num(); Index++)
	{
		const FCompactPoseBoneIndex RootIndex = SortedPairArray[Index].RootBone.GetCompactPoseIndex(BoneContainer);
		const FCompactPoseBoneIndex TipIndex = SortedPairArray[Index].TipBone.GetCompactPoseIndex(BoneContainer);

		SetSoftBoneIndices(MeshBases, RootIndex, TipIndex, ChainInfos[Index].BoneIndices);
	}
}

#if WITH_EDITOR
void FAnimNode_SoftBone::InitialzeWeightCurve()
{
	// construct curve templates
	FRichCurve* Curve = WeightCurve.GetRichCurve();

	switch (RestoringWeightType)
	{
	case ERestoringWeight::RW_Constant:
		Curve->Reset();
		Curve->AddKey(0.0f, 1.0f);
		Curve->AddKey(1.0f, 1.0f);
		break;
	case ERestoringWeight::RW_Linear:
		Curve->Reset();
		Curve->AddKey(0.0f, 1.1f);
		Curve->AddKey(1.1f, 0.0f);
		break;
	case ERestoringWeight::RW_Quadratic:
		Curve->Reset();
		Curve->AddKey(0.0f, 1.1f);
		Curve->AddKey(0.1f, 1.0f);
		Curve->AddKey(0.2f, 0.5f);
		Curve->AddKey(0.3f, 0.33f);
		Curve->AddKey(0.4f, 0.25f);
		Curve->AddKey(0.5f, 0.2f);
		Curve->AddKey(0.6f, 0.16f);
		Curve->AddKey(1.0f, 0.1f);
		break;
	}
}
#endif // #if WITH_EDITOR

void FAnimNode_SoftBone::InitializeChains(USkeletalMeshComponent* SkelComp, FCSPose<FCompactPose>& MeshBases, TArray<FBoneTransform>& OutBoneTransforms)
{
	int32 NumChains = ChainInfos.Num();
	int32 TransformStartIndex = 0;

	for (int32 Index = 0; Index < NumChains; Index++)
	{
		if (ChainInfos[Index].BoneIndices.Num() >= 2)
		{
			InitializeChain(ChainInfos[Index], SkelComp, MeshBases, OutBoneTransforms, TransformStartIndex);
			TransformStartIndex += ChainInfos[Index].BoneIndices.Num();
		}
	}
}

void FAnimNode_SoftBone::InitializeChain(FChainInfo& Chain, USkeletalMeshComponent* SkelComp, FCSPose<FCompactPose>& MeshBases, TArray<FBoneTransform>& OutBoneTransforms, int32 OutTransformStartIndex)
{
	TArray<FCompactPoseBoneIndex>& BoneIndices =  Chain.BoneIndices;
	TArray<FSoftBoneLink>& PrevBoneLinks = Chain.PrevBoneLinks;

	if (BoneIndices.Num() < 2)
	{
		return;
	}

	// Gather chain links. These are non zero length bones.
	const FBoneContainer& BoneContainer = MeshBases.GetPose().GetBoneContainer();

	int32 const NumTransforms = BoneIndices.Num();
	int32 MaxWeightKeyIndex = NumTransforms - 1;

	if (bAllowTipBoneRotation)
	{
		PrevBoneLinks.Reserve(NumTransforms + 1);
		MaxWeightKeyIndex = NumTransforms;
	}
	else
	{
		PrevBoneLinks.Reserve(NumTransforms);
	}

	FRichCurve* Curve = WeightCurve.GetRichCurve();

	// Start with Root Bone
	{
		const FCompactPoseBoneIndex& RootBoneIndex = BoneIndices[0];
		const FTransform& BoneCSTransform = MeshBases.GetComponentSpaceTransform(RootBoneIndex);

		OutBoneTransforms[OutTransformStartIndex] = FBoneTransform(RootBoneIndex, BoneCSTransform);

		FTransform BoneTransformInWorldSpace = (SkelComp != NULL) ? BoneCSTransform * SkelComp->GetComponentToWorld() : BoneCSTransform;

		Chain.PrevBoneLinks.Add(FSoftBoneLink(BoneTransformInWorldSpace.GetLocation(), 0.f, RootBoneIndex));
	}

	// Go through remaining transforms
	for (int32 TransformIndex = 1; TransformIndex < NumTransforms; TransformIndex++)
	{
		const FCompactPoseBoneIndex& BoneIndex = BoneIndices[TransformIndex];

		const FTransform& BoneCSTransform = MeshBases.GetComponentSpaceTransform(BoneIndex);
		FVector const BoneCSPosition = BoneCSTransform.GetLocation();

		int32 OutTransformIndex = OutTransformStartIndex + TransformIndex;
		OutBoneTransforms[OutTransformIndex] = FBoneTransform(BoneIndex, BoneCSTransform);

		FTransform BoneTransformInWorldSpace = (SkelComp != NULL) ? BoneCSTransform * SkelComp->GetComponentToWorld() : BoneCSTransform;
		// Calculate the combined length of this segment of skeleton
		float const BoneLength = FVector::Dist(BoneCSPosition, OutBoneTransforms[OutTransformIndex - 1].Transform.GetLocation());

		PrevBoneLinks.Add(FSoftBoneLink(BoneTransformInWorldSpace.GetLocation(), BoneLength, BoneIndex));

		if (bUseWeightCurve)
		{
			PrevBoneLinks[TransformIndex].RestoringWeight = Stiffness * Curve->Eval((float)TransformIndex / (float)MaxWeightKeyIndex);
		}
		else
		{
			PrevBoneLinks[TransformIndex].RestoringWeight = Stiffness / TransformIndex;
		}

	}

	// create a virtual link to the tip bone for natural rotation of tip bone
	if (bAllowTipBoneRotation)
	{
		const FTransform& ParentBoneCSTransform = MeshBases.GetComponentSpaceTransform(BoneIndices[BoneIndices.Num() - 2]);
		FVector const ParentBoneCSPosition = ParentBoneCSTransform.GetLocation();

		const FTransform& TipBoneCSTransform = MeshBases.GetComponentSpaceTransform(BoneIndices[BoneIndices.Num() - 1]);
		FVector const TipBoneCSPosition = TipBoneCSTransform.GetLocation();

		FTransform ParentBoneTransformInWS = (SkelComp != NULL) ? ParentBoneCSTransform * SkelComp->GetComponentToWorld() : ParentBoneCSTransform;
		FTransform TipBoneTransformInWS = (SkelComp != NULL) ? TipBoneCSTransform * SkelComp->GetComponentToWorld() : TipBoneCSTransform;
		// Calculate the combined length of this segment of skeleton
		float const BoneLength = FVector::Dist(TipBoneCSPosition, ParentBoneCSPosition);

		FVector VirtualBonePositionInWS = TipBoneTransformInWS.GetLocation() + (TipBoneTransformInWS.GetLocation() - ParentBoneTransformInWS.GetLocation());
		// connect a virtual link from the tip bone copying information from the parent bone
		FCompactPoseBoneIndex BoneIndex(INDEX_NONE);
		PrevBoneLinks.Add(FSoftBoneLink(VirtualBonePositionInWS, BoneLength, BoneIndex));

		if (bUseWeightCurve)
		{
			PrevBoneLinks[PrevBoneLinks.Num() - 1].RestoringWeight = Stiffness * Curve->Eval(1.0f);
		}
		else
		{
			PrevBoneLinks[PrevBoneLinks.Num() - 1].RestoringWeight = Stiffness / (float)MaxWeightKeyIndex;
		}
	}
}

void FAnimNode_SoftBone::ComputeTargetPositions(FChainInfo& Chain, USkeletalMeshComponent* SkelComp, FCSPose<FCompactPose>& MeshBases, TArray<FBoneTransform>& OutBoneTransforms, int32 OutTransformStartIndex, TArray<FVector>& TargetPositions)
{
	TArray<FCompactPoseBoneIndex>& BoneIndices = Chain.BoneIndices;

	int32 const NumTransforms = BoneIndices.Num();

	if (bAllowTipBoneRotation)
	{
		TargetPositions.Reserve(NumTransforms + 1);
	}
	else
	{
		TargetPositions.Reserve(NumTransforms);
	}

	// Start with Root Bone
	{
		const FCompactPoseBoneIndex& RootBoneIndex = BoneIndices[0];
		const FTransform BoneCSTransform = MeshBases.GetComponentSpaceTransform(RootBoneIndex);

		OutBoneTransforms[OutTransformStartIndex] = FBoneTransform(RootBoneIndex, BoneCSTransform);

		FTransform BoneTransformInWorldSpace = (SkelComp != NULL) ? BoneCSTransform * SkelComp->GetComponentToWorld() : BoneCSTransform;
		FVector BoneWSPosition = BoneTransformInWorldSpace.GetLocation();
		TargetPositions.Add(BoneWSPosition);
	}

	// Go through remaining transforms
	for (int32 TransformIndex = 1; TransformIndex < NumTransforms; TransformIndex++)
	{
		const FCompactPoseBoneIndex& BoneIndex = BoneIndices[TransformIndex];

		const FTransform& BoneCSTransform = MeshBases.GetComponentSpaceTransform(BoneIndex);

		FTransform BoneTransformInWorldSpace = (SkelComp != NULL) ? BoneCSTransform * SkelComp->GetComponentToWorld() : BoneCSTransform;

		FVector const BoneWSPosition = BoneTransformInWorldSpace.GetLocation();

		OutBoneTransforms[OutTransformStartIndex + TransformIndex] = FBoneTransform(BoneIndex, BoneCSTransform);

		TargetPositions.Add(BoneWSPosition);
	}

	if (bAllowTipBoneRotation)
	{
		const FTransform& ParentBoneCSTransform = MeshBases.GetComponentSpaceTransform(BoneIndices[BoneIndices.Num() - 2]);
		FVector const ParentBoneCSPosition = ParentBoneCSTransform.GetLocation();

		const FTransform& TipBoneCSTransform = MeshBases.GetComponentSpaceTransform(BoneIndices[BoneIndices.Num() - 1]);
		FVector const TipBoneCSPosition = TipBoneCSTransform.GetLocation();

		FTransform ParentBoneTransformInWS = (SkelComp != NULL) ? ParentBoneCSTransform * SkelComp->GetComponentToWorld() : ParentBoneCSTransform;
		FTransform TipBoneTransformInWS = (SkelComp != NULL) ? TipBoneCSTransform * SkelComp->GetComponentToWorld() : TipBoneCSTransform;

		FVector VirtualBonePositionInWS = TipBoneTransformInWS.GetLocation() + (TipBoneTransformInWS.GetLocation() - ParentBoneTransformInWS.GetLocation());
		// connect a virtual link from the tip bone copying information from the parent bone
		TargetPositions.Add(VirtualBonePositionInWS);
	}
}

void FAnimNode_SoftBone::TimeIntegration(FChainInfo& Chain, float TimeDelta, TArray<FVector>& TargetPositions)
{
	TArray<FSoftBoneLink>& PrevBoneLinks = Chain.PrevBoneLinks;

	float DampingCoefficient = 1.0f - DampingRatio;

	check(TargetPositions.Num() == PrevBoneLinks.Num());

	float PropagateMass = 1.0f;

	// pre-calculate inverse time step
	float InvTimeDelta = 1.0f / TimeDelta;

	// root bone should be fixed
	PrevBoneLinks[0].Position = TargetPositions[0];

	// @TODO : External force like wind or explosion
	// ExtForce -> f = ma, a = f/m
	FVector ExtForce(0, 0, 0);

	// apply a force of restitution to go back to the kinematic position except for root bone 
	// because it should be fixed
	for (int32 Index = 1; Index < TargetPositions.Num(); Index++)
	{

		FVector GravityVector(0, 0, GravityScale * GravityZ);
		FVector ExtAccel = GravityVector; 

		FVector RestoreImpulse = PrevBoneLinks[Index].RestoringWeight * (TargetPositions[Index] - PrevBoneLinks[Index].Position);

		FVector MoveDelta;

		{
			// velocity integration
			PrevBoneLinks[Index].Velocity += ((RestoreImpulse * InvTimeDelta) + (ExtAccel * TimeDelta));
			MoveDelta = PrevBoneLinks[Index].Velocity * TimeDelta;
			// damping
			PrevBoneLinks[Index].Velocity *= DampingCoefficient;
		}

		// position integration
		PrevBoneLinks[Index].Position += MoveDelta;
	}


	// if bBoneLengthConstraint is false, each bone stretches like a soft body
	if (bBoneLengthConstraint)
	{
		// solve distance constraint
		for (int32 LinkIndex = 1; LinkIndex < PrevBoneLinks.Num(); LinkIndex++)
		{
			FSoftBoneLink const & ParentLink = PrevBoneLinks[LinkIndex - 1];
			FSoftBoneLink & CurrentLink = PrevBoneLinks[LinkIndex];

			CurrentLink.Position = ParentLink.Position + (CurrentLink.Position - ParentLink.Position).GetUnsafeNormal() * CurrentLink.Length;
		}
	}
}

void FAnimNode_SoftBone::PullBonesToFinalPosition(TArray<FSoftBoneLink>& PrevBoneLinks, FVector& DiffVector)
{
	int32 NumLinks = PrevBoneLinks.Num();

	for (int32 Index = 0; Index < NumLinks; Index++)
	{
		PrevBoneLinks[Index].RenderPosition = PrevBoneLinks[Index].Position + DiffVector;
	}
}

float FAnimNode_SoftBone::SimulateSoftBoneChain(FChainInfo& Chain, USkeletalMeshComponent* SkelComp, FCSPose<FCompactPose>& MeshBases, TArray<FBoneTransform>& OutBoneTransforms, int32 OutTransformStartIndex, float InRemainingTime)
{
	TArray<FCompactPoseBoneIndex>& BoneIndices = Chain.BoneIndices;
	TArray<FSoftBoneLink>& PrevBoneLinks = Chain.PrevBoneLinks;

	const FTransform& SpaceBase = MeshBases.GetComponentSpaceTransform(Chain.BoneIndices[0]);
	// Location of the start bone in world space
	FTransform BoneTransformInWorldSpace = (SkelComp != NULL) ? SpaceBase * SkelComp->GetComponentToWorld() : SpaceBase;

	if (PrevBoneLinks.Num() == 0)
	{
		InitializeChain(Chain, SkelComp, MeshBases, OutBoneTransforms, OutTransformStartIndex);
	}

	// Calculate target positions
	TArray<FVector> FinalTargetPositions;
	ComputeTargetPositions(Chain, SkelComp, MeshBases, OutBoneTransforms, OutTransformStartIndex, FinalTargetPositions);

	TArray<FVector> TargetPositions;
	TargetPositions.AddUninitialized(FinalTargetPositions.Num());

	// copy only the root bone's position
	TargetPositions[0] = PrevBoneLinks[0].Position;

	if (bGuaranteeSameSimulationResult)
	{
		while (InRemainingTime >= FixedTimeStep)
		{
			float FixedTimeRatio = FixedTimeStep / InRemainingTime;
			float RemainedRatio = 1.0f - FixedTimeRatio;

			// interpolate target positions
			for (int32 Index = 0; Index < TargetPositions.Num(); Index++)
			{
				TargetPositions[Index] = FixedTimeRatio * FinalTargetPositions[Index] + RemainedRatio * PrevBoneLinks[Index].Position;
			}

			TimeIntegration(Chain, FixedTimeStep, TargetPositions);


			InRemainingTime -= FixedTimeStep;
		}
	}
	else
	{
		TimeIntegration(Chain, FixedTimeStep, TargetPositions);
		InRemainingTime = 0;
	}

	FVector RootBoneDiff = FinalTargetPositions[0] - TargetPositions[0];

	// pull bones to final positions and calculate positions for rendering
	PullBonesToFinalPosition(Chain.PrevBoneLinks, RootBoneDiff);

	FTransform InverseWorld = (SkelComp != NULL) ? SkelComp->GetComponentToWorld().Inverse() : FTransform::Identity;

	PrevBoneLinks[0].PositionInCS = OutBoneTransforms[OutTransformStartIndex].Transform.GetTranslation();

	int32 NumTransforms = BoneIndices.Num();

	// First step: update bone transform positions from chain links.
	for (int32 LinkIndex = 1; LinkIndex < NumTransforms; LinkIndex++)
	{
		// convert from world space to component space
		FSoftBoneLink& ChainLink = PrevBoneLinks[LinkIndex];
		FVector BoneCSPosition = InverseWorld.TransformPosition(ChainLink.RenderPosition);
		ChainLink.PositionInCS = BoneCSPosition;
		OutBoneTransforms[OutTransformStartIndex + LinkIndex].Transform.SetTranslation(BoneCSPosition);
	}

	// doesn't need to update OutBoneTransforms
	if (bAllowTipBoneRotation)
	{
		int32 LastIndex = PrevBoneLinks.Num() - 1;
		// convert from world space to component space
		FSoftBoneLink& ChainLink = PrevBoneLinks[LastIndex];
		FVector BoneCSPosition = InverseWorld.TransformPosition(ChainLink.RenderPosition);
		PrevBoneLinks[LastIndex].PositionInCS = BoneCSPosition;
	}

	// re-orientation of bone local axes after translation calculation
	ReOrientBoneRotations(Chain, MeshBases, OutBoneTransforms, OutTransformStartIndex);

#if WITH_EDITOR
	if (bShowDebugBones && SkelComp)
	{
		DrawDebugData(SkelComp->GetWorld(), PrevBoneLinks, FinalTargetPositions);
	}
#endif // #if WITH_EDITOR

	return InRemainingTime;
}

void FAnimNode_SoftBone::EvaluateBoneTransforms(USkeletalMeshComponent* SkelComp, FCSPose<FCompactPose>& MeshBases, TArray<FBoneTransform>& OutBoneTransforms)
{
	// Create Chain infos and Gather all bone indices between root and tip for all chains.
	if (ChainInfos.Num() == 0)
	{
		InitializeBoneIndices(MeshBases);
	}

	if (RemainingTime <= 0.0f)
	{
		return;
	}

	int32 NumChains = ChainInfos.Num();

	// Gather all transforms
	int32 NumAllTransforms = 0;
	for (int32 ChainIndex = 0; ChainIndex < NumChains; ChainIndex++)
	{
		NumAllTransforms += ChainInfos[ChainIndex].BoneIndices.Num();
	}

	OutBoneTransforms.AddUninitialized(NumAllTransforms);

	int32 OutTransformStartIndex = 0;

	float RemainedSimTime = RemainingTime;
	for (int32 ChainIndex = 0; ChainIndex < NumChains; ChainIndex++)
	{
		RemainedSimTime = SimulateSoftBoneChain(ChainInfos[ChainIndex], SkelComp, MeshBases, OutBoneTransforms, OutTransformStartIndex, RemainingTime);
		OutTransformStartIndex += ChainInfos[ChainIndex].BoneIndices.Num();
	}

	RemainingTime = RemainedSimTime;
}

void FAnimNode_SoftBone::ReOrientBoneRotations(FChainInfo& Chain, FCSPose<FCompactPose>& MeshBases, TArray<FBoneTransform>& OutBoneTransforms, int32 OutTransformStartIndex)
{
	TArray<FCompactPoseBoneIndex>& BoneIndices = Chain.BoneIndices;
	TArray<FSoftBoneLink>& PrevBoneLinks = Chain.PrevBoneLinks;

	int32 NumTransforms = BoneIndices.Num();

	for (int32 LinkIndex = 0; LinkIndex < NumTransforms - 1; LinkIndex++)
	{
		FSoftBoneLink const & CurrentLink = PrevBoneLinks[LinkIndex];
		FSoftBoneLink const & ChildLink = PrevBoneLinks[LinkIndex + 1];

		FVector ChildPosInCS = MeshBases.GetComponentSpaceTransform(ChildLink.BoneIndex).GetLocation();
		FVector CurrentPosInCS = MeshBases.GetComponentSpaceTransform(CurrentLink.BoneIndex).GetLocation();

		// Calculate pre-translation vector between this bone and child
		FVector const OldDir = (ChildPosInCS - CurrentPosInCS).GetUnsafeNormal();

		// Get vector from the post-translation bone to it's child
		FVector const NewDir = (ChildLink.PositionInCS - CurrentLink.PositionInCS).GetUnsafeNormal();

		// Calculate axis of rotation from pre-translation vector to post-translation vector
		FVector const RotationAxis = FVector::CrossProduct(OldDir, NewDir).GetSafeNormal();
		float const RotationAngle = FMath::Acos(FVector::DotProduct(OldDir, NewDir));
		FQuat const DeltaRotation = FQuat(RotationAxis, RotationAngle);
		// We're going to multiply it, in order to not have to re-normalize the final quaternion, it has to be a unit quaternion.
		checkSlow(DeltaRotation.IsNormalized());

		// Calculate absolute rotation and set it
		FTransform& CurrentBoneTransform = OutBoneTransforms[OutTransformStartIndex + LinkIndex].Transform;
		CurrentBoneTransform.SetRotation(DeltaRotation * CurrentBoneTransform.GetRotation());
	}

	// re-orient the last tip bone
	if (bAllowTipBoneRotation)
	{
		check(PrevBoneLinks.Num() - 3 >= 0);

		FSoftBoneLink const & ParentLink = PrevBoneLinks[PrevBoneLinks.Num() - 3];
		FSoftBoneLink const & CurrentLink = PrevBoneLinks[PrevBoneLinks.Num() - 2];
		FSoftBoneLink const & VirtualLink = PrevBoneLinks[PrevBoneLinks.Num() - 1];

		FVector ParentPosInCS = MeshBases.GetComponentSpaceTransform(ParentLink.BoneIndex).GetLocation();
		FVector CurrentPosInCS = MeshBases.GetComponentSpaceTransform(CurrentLink.BoneIndex).GetLocation();

		FVector VirtualBonePosInCS = CurrentPosInCS + (CurrentPosInCS - ParentPosInCS);

		FQuat CurrentRot = MeshBases.GetComponentSpaceTransform(CurrentLink.BoneIndex).GetRotation();

		// Calculate pre-translation vector between this bone and child
		FVector const OldDir = (VirtualBonePosInCS - CurrentPosInCS).GetUnsafeNormal();

		// Get vector from the post-translation bone to it's child
		FVector const NewDir = (VirtualLink.PositionInCS - CurrentLink.PositionInCS).GetUnsafeNormal();

		// Calculate axis of rotation from pre-translation vector to post-translation vector
		FVector const RotationAxis = FVector::CrossProduct(OldDir, NewDir).GetSafeNormal();
		float const RotationAngle = FMath::Acos(FVector::DotProduct(OldDir, NewDir));
		FQuat const DeltaRotation = FQuat(RotationAxis, RotationAngle);
		// We're going to multiply it, in order to not have to re-normalize the final quaternion, it has to be a unit quaternion.
		checkSlow(DeltaRotation.IsNormalized());

		// Calculate absolute rotation and set it
		FTransform& CurrentBoneTransform = OutBoneTransforms[OutTransformStartIndex + NumTransforms - 1].Transform;
		CurrentBoneTransform.SetRotation(DeltaRotation * CurrentBoneTransform.GetRotation());
	}

}

#if WITH_EDITOR
void FAnimNode_SoftBone::DrawDebugData(UWorld* World, TArray<FSoftBoneLink>& PrevBoneLinks, TArray<FVector>& TargetPositions)
{
	int32 NumChainLinks = TargetPositions.Num();

	if (World)
	{
		// Draw Original bones
		for (int32 LinkIndex = 1; LinkIndex < NumChainLinks; LinkIndex++)
		{
			DrawDebugLine(World, TargetPositions[LinkIndex - 1], TargetPositions[LinkIndex], FColor::White, false, -1.f, SDPG_Foreground, 2.0f);
		}

		FVector Extent(5.0f);

		for (int32 LinkIndex = 0; LinkIndex < NumChainLinks; LinkIndex++)
		{
			DrawDebugBox(World, TargetPositions[LinkIndex], Extent, FColor::Yellow, false, -1.f, SDPG_Foreground);
		}

		FVector AddVec(30.0f, 0, 0);
		// Draw soft bones
		for (int32 LinkIndex = 1; LinkIndex < NumChainLinks; LinkIndex++)
		{
			DrawDebugLine(World, PrevBoneLinks[LinkIndex - 1].Position + AddVec, PrevBoneLinks[LinkIndex].Position + AddVec, FColor::Red, false, -1.f, SDPG_Foreground, 2.0f);
		}

		for (int32 LinkIndex = 0; LinkIndex < NumChainLinks; LinkIndex++)
		{
			DrawDebugBox(World, PrevBoneLinks[LinkIndex].Position + AddVec, Extent, FColor::Blue, false, -1.f, SDPG_Foreground);
		}
	}
}
#endif // #if WITH_EDITOR

bool FAnimNode_SoftBone::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	// Allow evaluation if TipBone and RootBone are initialized
	// Basically we should check whether TipBone is child of RootBone or not but checking this in initialization code, InitializeBoneIndices(),
	// to reduce cost because this function will be called every time to check validation

	return
		(
		TipBone.IsValid(RequiredBones)
		&& RootBone.IsValid(RequiredBones)
		);
}

void FAnimNode_SoftBone::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	TipBone.Initialize(RequiredBones);
	RootBone.Initialize(RequiredBones);

	int32 NumChains = AdditionalChains.Num();

	for (int32 Index = 0; Index < NumChains; Index++)
	{
		AdditionalChains[Index].TipBone.Initialize(RequiredBones);
		AdditionalChains[Index].RootBone.Initialize(RequiredBones);
	}

	ChainInfos.Empty();
}
