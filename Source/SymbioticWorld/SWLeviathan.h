#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWTypes.h"
#include "SWLeviathan.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class ASWWorldManager;
class ASWAgent;

// The river predator.
//
// Deliberately NOT an organism and NOT an ESWSpecies. It has no genome, no bandit,
// no reproduction and no entry in the action set, and it never appears in the
// external policy protocol (docs/POLICY_API.md stays at 7 actions / 2 species, so
// every participant's policy server keeps working unchanged). It is a perturbation
// of the environment, in the same category as the drought — the difference being
// that this one is spatial, so an organism's position decides whether it applies.
//
// Selection pressure it creates: an organism is at risk exactly when it is in the
// water, which is exactly when FSWPercept::bOnLand is false. That flag and the
// 'avoid' action already exist, so both the built-in bandit and any external policy
// can learn to keep out of the channel with no new machinery.
//
// Like ASWAgent it does not tick on its own: the world manager advances it on the
// fixed logical substep, so the simulation stays reproducible and independent of
// frame rate and time scale.
UCLASS()
class SYMBIOTICWORLD_API ASWLeviathan : public AActor
{
	GENERATED_BODY()

public:
	ASWLeviathan();

	void Init(ASWWorldManager* InManager, int32 InIndex);

	// Advance by Dt logical seconds. Organisms killed this step are APPENDED to
	// OutVictims; the manager reaps them (cause "predation") so all death
	// accounting and logging stays in one place, and so two leviathans cannot
	// both claim the same organism in one substep.
	void Step(float Dt, TArray<ASWAgent*>& OutVictims);

	// ---- Read-only accessors (HUD / minimap) ----
	bool IsSurfacing() const { return BreachPhase > 0.f; }
	int32 GetKills() const { return Kills; }
	float GetTravelX() const { return TravelX; }

protected:
	UPROPERTY(VisibleAnywhere) UProceduralMeshComponent* Body;
	UPROPERTY() UMaterialInstanceDynamic* MID = nullptr;
	UPROPERTY() ASWWorldManager* Manager = nullptr;

	int32 Index = 0;
	int32 Kills = 0;
	float TravelX = 0.f;        // position along the channel's X axis
	float Direction = 1.f;      // +1 travelling up +X, -1 down
	float StrikeTimer = 0.f;    // logical s until the next strike is possible
	float BreachPhase = 0.f;    // > 0 while surfacing, counts down
	float CruiseTimer = 0.f;    // drives the idle surfacing rhythm
	float WeavePhase = 0.f;     // lateral weave across the channel
	bool bWasSurfacing = false; // so the emissive is only pushed to the material on a change

	void BuildBody();
	void PlaceAlongChannel();
	// Effective water surface Z (the drought lowers it).
	float SurfaceZ() const;
	// Mirrors how FSWPercept::bOnLand is computed in ASWWorldManager::BuildPercept.
	bool IsInWater(const FVector& Loc) const;
};
