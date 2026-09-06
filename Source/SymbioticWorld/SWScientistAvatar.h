#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWScientistAvatar.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class ASWWorldManager;

// One embodied field scientist (the Symbiotic Lab's team, Lab/embodiment.py).
//
// PURELY VISUAL. The avatar is driven by "scientists" side messages arriving
// over the policy bridge (docs/POLICY_API.md): the Lab computes the positions,
// the sim only renders them. No collision, no percept entry, no trace deposit,
// no draw from the seeded stream — an organism cannot see, touch or learn from
// a scientist, so a run with avatars reproduces byte-identically without them.
// Movement smoothing runs on the rendered frame (wall clock), like the camera.
//
// Body: the Manny / Quinn mannequin meshes when the host project has them
// (Content examples), else an engine capsule. A floating name label identifies
// the scientist; the label turns toward the viewer each frame.
UCLASS()
class SYMBIOTICWORLD_API ASWScientistAvatar : public AActor
{
	GENERATED_BODY()

public:
	ASWScientistAvatar();

	void Init(ASWWorldManager* InManager, const FString& InName, int32 InIndex);

	// Latest reported position (arena uu, same space as organism positions).
	void SetTargetXY(float X, float Y);

	// Wall-clock smoothing toward the target; Z from the terrain; label faces the camera.
	void UpdateVisual(float DeltaSeconds);

	const FString& GetScientistName() const { return ScientistName; }

protected:
	UPROPERTY(VisibleAnywhere) USceneComponent* Root;
	UPROPERTY(VisibleAnywhere) USkeletalMeshComponent* Body;
	UPROPERTY(VisibleAnywhere) UStaticMeshComponent* FallbackBody;
	UPROPERTY(VisibleAnywhere) UTextRenderComponent* Label;
	UPROPERTY() ASWWorldManager* Manager = nullptr;

	FString ScientistName;
	FVector2D TargetXY = FVector2D::ZeroVector;
	bool bHasTarget = false;
};
