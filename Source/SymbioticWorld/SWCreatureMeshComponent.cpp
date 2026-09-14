#include "SWCreatureMeshComponent.h"
#include "SWWorldManager.h"
#include "Engine/SkeletalMesh.h"

void USWCreatureMeshComponent::ConfigureGrounding(ASWWorldManager* InManager, float InSoleClearance, float InGroundOffset)
{
	GroundManager = InManager;
	SoleClearance = InSoleClearance;
	GroundOffset = InGroundOffset;
	Legs.Reset();
	if (!GetSkeletalMeshAsset()) return;
	const FReferenceSkeleton& Ref = GetSkeletalMeshAsset()->GetRefSkeleton();
	TArray<FTransform> RefCS = Ref.GetRefBonePose();
	for (int32 I = 1; I < RefCS.Num(); ++I) RefCS[I] *= RefCS[Ref.GetParentIndex(I)];
	// FName lookups are case-insensitive: the imported skeletons spell these fore_l_upper etc.
	for (const TCHAR* Prefix : {TEXT("fore_L"), TEXT("fore_R"), TEXT("hind_L"), TEXT("hind_R")})
	{
		const FString P(Prefix);
		const int32 U = Ref.FindBoneIndex(FName(*(P + TEXT("_upper"))));
		const int32 L = Ref.FindBoneIndex(FName(*(P + TEXT("_lower"))));
		const int32 F = Ref.FindBoneIndex(FName(*(P + TEXT("_foot"))));
		if (U != INDEX_NONE && L != INDEX_NONE && F != INDEX_NONE) Legs.Add({U, L, F, float(RefCS[F].GetLocation().Z)});
	}
}

void USWCreatureMeshComponent::FinalizeBoneTransform()
{
	LastGroundError = 0.f;
	if (GroundManager && GetSkeletalMeshAsset())
	{
		TArray<FTransform>& Pose = GetEditableComponentSpaceTransforms();
		const FReferenceSkeleton& Ref = GetSkeletalMeshAsset()->GetRefSkeleton();
		// Local-space copy of the evaluated pose, written straight into LastLocal (the previous
		// contents were already captured by BeginPoseTransition if a blend is running).
		LastLocal.SetNumUninitialized(Pose.Num());
		if (Pose.Num() > 0) LastLocal[0] = Pose[0];
		for (int32 I = Pose.Num() - 1; I > 0; --I) LastLocal[I] = Pose[I].GetRelativeTransform(Pose[Ref.GetParentIndex(I)]);
		if (TransitionAlpha < 1.f && TransitionLocal.Num() == LastLocal.Num())
		{
			const float Smooth = FMath::SmoothStep(0.f, 1.f, TransitionAlpha);
			for (int32 I = 0; I < LastLocal.Num(); ++I)
			{
				FTransform Blended;
				Blended.Blend(TransitionLocal[I], LastLocal[I], Smooth);
				LastLocal[I] = Blended;
				Pose[I] = I == 0 ? LastLocal[I] : LastLocal[I] * Pose[Ref.GetParentIndex(I)];
			}
		}
		const FTransform World = GetComponentTransform();
		const float Scale = FMath::Max(float(World.GetScale3D().X), 0.001f);
		for (const FLeg& Leg : Legs)
		{
			if (!Pose.IsValidIndex(Leg.Foot)) continue;
			const FVector Hip = Pose[Leg.Upper].GetLocation();
			const FVector Knee = Pose[Leg.Lower].GetLocation();
			const FVector Ankle = Pose[Leg.Foot].GetLocation();
			FVector TargetWorld = World.TransformPosition(Ankle);
			const float Lift = FMath::Max(0.f, float(Ankle.Z) - Leg.RestAnkleZ);
			TargetWorld.Z = GroundManager->GetGroundZ(TargetWorld.X, TargetWorld.Y) + GroundOffset + (Leg.RestAnkleZ + SoleClearance + Lift) * Scale;
			FVector Target = World.InverseTransformPosition(TargetWorld);
			const float A = FVector::Distance(Hip, Knee), B = FVector::Distance(Knee, Ankle);
			if (A < 0.01f || B < 0.01f) continue;
			const FVector Axis = (Target - Hip).GetSafeNormal();
			const float Reach = FMath::Clamp(float(FVector::Distance(Target, Hip)), FMath::Abs(A - B) + 0.01f, A + B - 0.01f);
			Target = Hip + Axis * Reach;
			FVector Bend = Knee - Hip;
			Bend = (Bend - Axis * FVector::DotProduct(Bend, Axis)).GetSafeNormal();
			if (Bend.IsNearlyZero()) Bend = FVector::CrossProduct(Axis, FVector::RightVector).GetSafeNormal();
			const float Along = (A * A - B * B + Reach * Reach) / (2.f * Reach);
			const FVector NewKnee = Hip + Axis * Along + Bend * FMath::Sqrt(FMath::Max(0.f, A * A - Along * Along));
			Pose[Leg.Upper].SetRotation((FQuat::FindBetweenNormals((Knee - Hip).GetSafeNormal(), (NewKnee - Hip).GetSafeNormal()) * Pose[Leg.Upper].GetRotation()).GetNormalized());
			Pose[Leg.Lower].SetRotation((FQuat::FindBetweenNormals((Ankle - Knee).GetSafeNormal(), (Target - NewKnee).GetSafeNormal()) * Pose[Leg.Lower].GetRotation()).GetNormalized());
			Pose[Leg.Lower].SetLocation(NewKnee);
			Pose[Leg.Foot].SetLocation(Target);
			LastGroundError = FMath::Max(LastGroundError, float(FVector::Distance(World.TransformPosition(Target), TargetWorld)));
		}
	}
	Super::FinalizeBoneTransform();
}
