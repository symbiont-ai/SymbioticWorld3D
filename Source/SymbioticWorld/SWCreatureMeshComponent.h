#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "SWCreatureMeshComponent.generated.h"

class ASWWorldManager;

// Visual-only terrain adaptation, applied after animation evaluation and before
// the completed pose is published to rendering. Never moves simulation actors.
UCLASS()
class SYMBIOTICWORLD_API USWCreatureMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()
public:
	// InSoleClearance: mesh-space cm the ankle rests above the ground (scaled with the component);
	// InGroundOffset: world uu the species stands above SWProc ground (Params.GroundOffset), unscaled.
	void ConfigureGrounding(ASWWorldManager* InManager, float InSoleClearance, float InGroundOffset);
	float GetGroundError() const { return LastGroundError; }
	void BeginPoseTransition() { TransitionLocal = LastLocal; TransitionAlpha = 0.f; }
	void SetTransitionAlpha(float Alpha) { TransitionAlpha = FMath::Clamp(Alpha, 0.f, 1.f); }
	virtual void FinalizeBoneTransform() override;
private:
	UPROPERTY() ASWWorldManager* GroundManager = nullptr;
	struct FLeg { int32 Upper, Lower, Foot; float RestAnkleZ; };
	TArray<FLeg> Legs;
	float SoleClearance = 0.f;
	float GroundOffset = 0.f;
	float LastGroundError = 0.f;
	// LastLocal is also the working buffer of FinalizeBoneTransform (no per-pose allocation);
	// TransitionLocal is the pose the current blend starts from.
	TArray<FTransform> LastLocal, TransitionLocal;
	float TransitionAlpha = 1.f;
};
