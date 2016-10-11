// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNode_SkeletalControlBase.h"
#include "AnimNode_SoftBone.generated.h"

/**
 *	Simple controller that makes a series of bones to jiggle and move like a soft body but satisfies bone length constraints.
 */

UENUM(BlueprintType)
namespace ESimulationHertz
{
	enum Type
	{
		SH_30Hz = 30 UMETA(DisplayName = "30Hz"),
		SH_60Hz = 60 UMETA(DisplayName = "60Hz"),
		SH_120Hz = 120 UMETA(DisplayName = "120Hz"),
	};
}

UENUM(BlueprintType)
namespace ERestoringWeight
{
	enum Type
	{
		// All bones have same weight
		RW_Constant UMETA(DisplayName = "Constant"),
		// Linearly decrease
		RW_Linear UMETA(DisplayName = "Linear"),
		// Quadratically decrease
		RW_Quadratic UMETA(DisplayName = "Quadratic"),
		// Use custom curve
		RW_Custom UMETA(DisplayName = "Custom"),
	};
}

/** Transient structure for SoftBone node evaluation */
struct FSoftBoneLink
{
public:
	FVector Velocity;

	/** Current Simulated Position of bone in world space. */
	FVector Position;

	/** Position of bone in world space for rendering. */
	FVector RenderPosition;

	/** Current Position of bone in component space. */
	FVector PositionInCS;

	/** Distance to its parent link. */
	float Length;

	/** Bone Index in SkeletalMesh */
	FCompactPoseBoneIndex BoneIndex;

	/** Transform Index that this control will output */
	int32 TransformIndex;

	/** Pre-calculated weight for restoring  */
	float RestoringWeight;

	FSoftBoneLink()
		: Position(FVector::ZeroVector)
		, Velocity(FVector::ZeroVector)
		, Length(0.f)
		, BoneIndex(INDEX_NONE)
	{
	}

	FSoftBoneLink(const FVector& InPosition, const float& InLength, const FCompactPoseBoneIndex& InBoneIndex)
		: Position(InPosition)
		, Length(InLength)
		, BoneIndex(InBoneIndex)
		, Velocity(FVector::ZeroVector)
		, PositionInCS(FVector::ZeroVector)
	{
	}
};

USTRUCT()
struct FBonePair
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BoneChain)
	FBoneReference RootBone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BoneChain)
	FBoneReference TipBone;

	FBonePair()
	{
	}

	FBonePair(FBoneReference& InRootBone, FBoneReference& InTipBone)
		: RootBone(InRootBone)
		, TipBone(InTipBone)
	{
	}
};

struct FChainInfo
{
	/** stored bone indices when initializing */
	TArray<FCompactPoseBoneIndex> BoneIndices;

	/** Previous bone locations for this chain. Num of this array should be same as Num of Bone indices. */
	/** in world space */
	TArray<FSoftBoneLink> PrevBoneLinks;

	void Empty()
	{
		BoneIndices.Empty();
		PrevBoneLinks.Empty();
	}
};

USTRUCT()
struct SOFTBONE_API FAnimNode_SoftBone : public FAnimNode_SkeletalControlBase
{
	GENERATED_USTRUCT_BODY()

	/** Name of root bone to control. This is the main bone chain to modify from. **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BoneChain)
	FBoneReference RootBone;

	/** Name of tip bone which is the last bone of the chain. **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BoneChain)
	FBoneReference TipBone;

	/** Never duplicate same bones already included in other chains **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = BoneChain)
	TArray<FBonePair> AdditionalChains;

	/** Decrease this scale when the chain is unstable against gravity. GravityZ comes from WorldSetting. **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Parameters, meta = (PinShownByDefault))
	float GravityScale;

	/** in the range [0..1] This means Restoring Force Ratio,  if 0, it doesn't restore to the original shape at all. **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Parameters, meta = (PinShownByDefault))
	float Stiffness;

	/** ranged [0..1] Velocity Damping Ratio, 0 means No damping, 1 means Velocity will be 0 at next tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Parameters, meta = (PinShownByDefault))
	float DampingRatio;

	/** allow tip bone rotation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Solver)
	bool bAllowTipBoneRotation;

	/** This parameter is experimental for now */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Solver)
	TEnumAsByte<ERestoringWeight::Type> RestoringWeightType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Solver)
	bool bUseWeightCurve;
	/**
	* Restoring weight scale factor as a function of normalized bone number (i.e. Current Bone Number / Max Bone Number).
	* X = 0 corresponds to Root bone, X = 1 corresponds to Tip bone.
	* Stiffness*Y, Y = 0 corresponds to No Restore, Y = 1 corresponds to "Stiffness" the user set above.
	*/
	UPROPERTY(EditAnywhere, Category = Solver, meta = (DisplayName = "Restoring Weight Curve", XAxisName = "Normalized Bone Number", YAxisName = "Restoring Weight"))
	FRuntimeFloatCurve WeightCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Solver)
	bool bShowDebugBones;

	/** if false, bone length will be stretched like a spring */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Solver)
	bool bBoneLengthConstraint;

	/** 60 hertz by default. Recommend higher than 60Hz for smooth simulation but You can also choose 30Hz for performance and adjust stiffness and damping */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Solver)
	TEnumAsByte<ESimulationHertz::Type> SimulationHertz;

	/** If true, keeps same simulated results regardless of frame rates or delta time. 
	    If false, It doesn't guarantee the same results and SimulationHertz will not be used but good at performance up */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Solver)
	bool bGuaranteeSameSimulationResult;

private:

	/** Internal use - Fixed timestep divided by SimulationFPS */
	float FixedTimeStep;
	/** Internal use - Current timestep */
	float DeltaTimeStep;
	/** Internal use - get this from World Setting */
	float GravityZ;

	/** Internal use - Amount of time we need to simulate. */
	float RemainingTime;

	/**  info array of all chains including bone indices and previous bone positions */
	TArray<FChainInfo> ChainInfos;

protected:
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
public:
	FAnimNode_SoftBone();

	// FAnimNode_Base interface
	virtual void Initialize(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones(const FAnimationCacheBonesContext& Context) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	// FAnimNode_SkeletalControlBase interface
	virtual void EvaluateBoneTransforms(USkeletalMeshComponent* SkelComp, FCSPose<FCompactPose>& MeshBases, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

#if WITH_EDITOR
	void InitialzeWeightCurve();
	const TArray<FChainInfo>& GetChainInfos() const
	{
		return ChainInfos;
	}
#endif // #if WITH_EDITOR

private:
	// FAnimNode_SkeletalControlBase interface
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

	void InitializeBoneIndices(FCSPose<FCompactPose>& MeshBases);
	void InitializeChain(FChainInfo& Chain, USkeletalMeshComponent* SkelComp, FCSPose<FCompactPose>& MeshBases, TArray<FBoneTransform>& OutBoneTransforms, int32 OutTransformStartIndex);
	void InitializeChains(USkeletalMeshComponent* SkelComp, FCSPose<FCompactPose>& MeshBases, TArray<FBoneTransform>& OutBoneTransforms);

	float SimulateSoftBoneChain(FChainInfo& Chain, USkeletalMeshComponent* SkelComp, FCSPose<FCompactPose>& MeshBases, TArray<FBoneTransform>& OutBoneTransforms, int32 OutTransformStartIndex, float InRemainingTime);

	void ComputeTargetPositions(FChainInfo& Chain, USkeletalMeshComponent* SkelComp, FCSPose<FCompactPose>& MeshBases, TArray<FBoneTransform>& OutBoneTransforms, int32 OutTransformStartIndex, TArray<FVector>& TargetPositions);
	void TimeIntegration(FChainInfo& Chain, float TimeDelta, TArray<FVector>& TargetPositions);

	// make the final positions by pulling simulated positions to destinations
	void PullBonesToFinalPosition(TArray<FSoftBoneLink>& PrevBoneLinks, FVector& DiffVec);

	// re-orientation of bone local axes after translation calculation
	void ReOrientBoneRotations(FChainInfo& Chain, FCSPose<FCompactPose>& MeshBases, TArray<FBoneTransform>& OutBoneTransforms, int32 OutTransformStartIndex);

#if WITH_EDITOR
	void DrawDebugData(UWorld* World, TArray<FSoftBoneLink>& PrevBoneLinks, TArray<FVector>& TargetPositions);
#endif // #if WITH_EDITOR
};
