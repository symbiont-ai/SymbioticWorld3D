#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWScientistAvatar.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UAnimSequence;
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
// Body: the Manny / Quinn mannequin meshes at true human scale (~1.8 m, 1 uu = 1 cm,
// so a Lumen stands about twice as tall and a Tecton four times) when the host project
// has them (Tools/import_mannequins.py copies Epic's template content into the git-ignored
// Content/Characters/Mannequins), else an engine cylinder; a missing mesh or clip is logged
// as a warning. The body's paint is tinted in the scientist's colour. Idle / walk / jog clips
// play as single-node animations picked from the avatar's own smoothed speed (no animation
// blueprint). The name is drawn by the HUD in screen space (ASWHUD::DrawScientistTags):
// a world-space label on a body this size is a few pixels tall from the start camera.
UCLASS()
class SYMBIOTICWORLD_API ASWScientistAvatar : public AActor
{
	GENERATED_BODY()

public:
	ASWScientistAvatar();

	void Init(ASWWorldManager* InManager, const FString& InName, int32 InIndex);

	// Latest reported position (arena uu, same space as organism positions).
	void SetTargetXY(float X, float Y);

	// Wall-clock smoothing toward the target; Z from the terrain; gait from speed.
	void UpdateVisual(float DeltaSeconds);

	const FString& GetScientistName() const { return ScientistName; }
	bool HasMannequin() const { return bHasMannequin; }
	const FLinearColor& GetTagColor() const { return TagColor; }

	// World point just above the head where the HUD anchors the name tag.
	FVector GetTagAnchor() const;

protected:
	UPROPERTY(VisibleAnywhere) USceneComponent* Root;
	UPROPERTY(VisibleAnywhere) USkeletalMeshComponent* Body;
	UPROPERTY(VisibleAnywhere) UStaticMeshComponent* FallbackBody;
	UPROPERTY() ASWWorldManager* Manager = nullptr;
	UPROPERTY() UAnimSequence* IdleAnim = nullptr;
	UPROPERTY() UAnimSequence* WalkAnim = nullptr;
	UPROPERTY() UAnimSequence* JogAnim = nullptr;
	UPROPERTY() UAnimSequence* CurrentAnim = nullptr;

	FString ScientistName;
	FLinearColor TagColor = FLinearColor::White;
	FVector2D TargetXY = FVector2D::ZeroVector;
	bool bHasTarget = false;
	bool bHasMannequin = false;
	float SmoothedSpeed = 0.f;   // on-screen uu/s; drives the gait

	// Switch the single-node clip only when the gait changes; the play rate follows the speed.
	void SetGait(UAnimSequence* Anim, float PlayRate);
};
