#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWTypes.h"
#include "SWKillMark.generated.h"

class UProceduralMeshComponent;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class ASWWorldManager;

// Transient marker for one predation kill (Look.bPredationEffects): a red plume that spreads
// outward and downward through the water, and a visual stand-in for the victim that tumbles,
// sinks and fades. Both run on the rendered frame in WALL seconds
// (Look.KillPlumeSeconds / Look.KillBodySeconds); the actor destroys itself when the longer of
// the two has run out.
//
// Visual only, by construction: the simulation actor is already gone when this is spawned, this
// actor is never in ASWWorldManager::Agents, has no collision, enters no percept, is spawned from
// Tick (never inside a logical substep) and never draws from the seeded simulation stream — its
// only randomness is a private stream seeded from the victim's id, like the creature bodies.
UCLASS()
class SYMBIOTICWORLD_API ASWKillMark : public AActor
{
	GENERATED_BODY()

public:
	ASWKillMark();

	// Builds the plume and the stand-in body. Species/id/scale describe the organism that was taken,
	// so the stand-in is the same body the viewer just saw (the procedural mesh is rebuilt from the
	// same private stream; an authored creature is reproduced in its reference pose when the content
	// is installed, docs/CREATURE_RENDERING.md).
	void Init(ASWWorldManager* InManager, ESWSpecies InSpecies, int32 InAgentId, const FVector& InLocation,
	          const FRotator& InRotation, float InMeshScale);

	virtual void Tick(float DeltaSeconds) override;

protected:
	UPROPERTY(VisibleAnywhere) USceneComponent* Root;
	UPROPERTY(VisibleAnywhere) UProceduralMeshComponent* Plume;
	UPROPERTY(VisibleAnywhere) UProceduralMeshComponent* BodyCopy;
	UPROPERTY(VisibleAnywhere) USkeletalMeshComponent* AuthoredCopy;
	UPROPERTY() UMaterialInstanceDynamic* PlumeMID = nullptr;
	UPROPERTY() UMaterialInstanceDynamic* BodyMID = nullptr;
	UPROPERTY() ASWWorldManager* Manager = nullptr;

	float Age = 0.f;                 // wall seconds since the kill
	float PlumeSeconds = 1.5f;
	float BodySeconds = 1.0f;
	float PlumeRadius = 260.f;       // cloud radius, from the stand-in's own length
	float PlumeBaseZ = 0.f;          // cloud centre relative to the kill: a little under the surface
	float SinkDepth = 220.f;         // from the stand-in's own height, clamped to the bed under the kill
	static constexpr float BodyFallEnd = 0.72f;    // the descent is over by here; the shrink is strictly after it
	static constexpr float BodyShrinkStart = 0.78f;
	FRotator BodyStartRotation = FRotator::ZeroRotator;
	FRotator BodyTumble = FRotator::ZeroRotator;
	float BodyScale = 1.f;
	FLinearColor BodyBaseColor = FLinearColor::White;
	bool bAuthoredTint = false;      // the authored material tints with BodyTint/GlowTint, the procedural one with BodyColor
	float Wobble[8] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };   // per-kill plume irregularity
	bool bPlumeBuilt = false;

	void UpdatePlume();
	void UpdateBody();
	bool BuildAuthoredCopy(ESWSpecies InSpecies, float InMeshScale);
};
