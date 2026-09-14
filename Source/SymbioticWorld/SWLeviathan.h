#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWTypes.h"
#include "SWLeviathan.generated.h"

class USceneComponent;
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
// frame rate and time scale. Since 2026-09-11 it hunts: an organism in the water
// within Settings.LeviathanSenseRadius is chased at LeviathanChaseSpeed; otherwise
// it patrols with seeded random reversals, speed changes and loitering
// (LeviathanTurnInterval / TurnChance / LoiterChance), drawing from the manager's
// stream in substep order so the run stays byte-identical for its seed. The chase brakes into
// the prey, never swims against its own facing, and the heading turns at a bounded rate
// (Settings.LeviathanChaseBand / TurnRate / PreyHold), and the rendered body is interpolated
// between substeps by UpdateVisual, exactly as
// ASWAgent does for its authored body -- without both, an 18 m animal on a 10 Hz substep reads as
// jitter next to organisms that glide.
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

	// Visual only, once per rendered frame: interpolates the body between the last two substeps.
	// InterpolationDt is the manager's leftover accumulator, as for ASWAgent::UpdateAuthoredVisual.
	// It never touches the actor transform, which is what the strike test and the camera read.
	void UpdateVisual(float InterpolationDt);

	// ---- Read-only accessors (HUD / minimap) ----
	bool IsSurfacing() const { return BreachPhase > 0.f; }
	bool IsHunting() const { return Prey.IsValid(); }
	int32 GetKills() const { return Kills; }
	float GetTravelX() const { return TravelX; }

protected:
	// Root = simulation truth, moved once per logical substep. Body rides it with an absolute
	// transform so UpdateVisual can interpolate the visible animal per rendered frame without
	// moving what the strike test reads (the same split ASWAgent uses for its authored body).
	UPROPERTY(VisibleAnywhere) USceneComponent* Pivot;
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
	float Lateral = 0.f;        // current offset from the channel centreline (uu), eased toward the weave or the prey
	float DecisionTimer = 0.f;  // logical s until the next seeded patrol decision
	float SpeedScale = 1.f;     // patrol speed multiplier chosen at the last decision (0.25 = loiter)
	TWeakObjectPtr<ASWAgent> Prey;   // organism being hunted (in the water, within LeviathanSenseRadius)
	float Heading = 0.f;        // current yaw (deg), eased toward the travel direction at LeviathanTurnRate
	float HeadingAlign = 1.f;   // cos(yaw error) at the last substep: 1 aligned, 0 broadside; scales travel
	float BedFloor = 0.f;       // low-passed riverbed clamp (uu), so floor noise does not chatter the body
	bool bHeadingSet = false;   // first placement snaps the heading and the bed clamp instead of easing
	bool bWasSurfacing = false; // so the emissive is only pushed to the material on a change
	FVector PrevLocation = FVector::ZeroVector;   // actor transform before the current substep's move,
	FQuat PrevRotation = FQuat::Identity;         // i.e. what UpdateVisual interpolates FROM (set by
	                                              // Init and by every real substep, never by Init's
	                                              // own placement -- see PlaceAlongChannel)

	void BuildBody();
	// Dt = 0 (Init) snaps the heading and the bed clamp; otherwise both ease over the substep.
	void PlaceAlongChannel(float Dt);
	// Effective water surface Z (the drought lowers it).
	float SurfaceZ() const;
	// Mirrors how FSWPercept::bOnLand is computed in ASWWorldManager::BuildPercept.
	bool IsInWater(const FVector& Loc) const;
};
