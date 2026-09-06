#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWTypes.h"
#include "SWLearner.h"
#include "SWAgent.generated.h"

class UStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class ASWWorldManager;

// One organism. Species differences are parametric (FSWSpeciesParams); the
// learning machinery is identical for Lumen and Tecton. All state advances via
// Step(), driven by the world manager's fixed logical substep — the actor does
// not tick on its own, which keeps the simulation reproducible and independent
// of frame rate and time scale.
//
// Visuals: an invisible sphere (root) carries the cursor-pick collision; the
// visible body is a procedural mesh built per species (SWProc::BuildLumen /
// BuildTecton) with vertex-colour emissive markings driven by M_SW_Creature.
UCLASS()
class SYMBIOTICWORLD_API ASWAgent : public AActor
{
	GENERATED_BODY()

public:
	ASWAgent();

	void Init(ASWWorldManager* InManager, ESWSpecies InSpecies, const FSWSpeciesParams& InParams,
	          const FSWGenome& InGenome, int32 InId, int32 InParentId, int32 InGeneration, float InEnergy);

	// Advance by Dt logical seconds. Returns false if the agent died this step.
	bool Step(float Dt);

	void SetSelected(bool bInSelected);
	void SnapToGround();
	// Called by the manager once per rendered frame (not per substep): rebuilds the trail ribbon if it changed.
	void UpdateTrailVisual();

	// ---- Read-only accessors (HUD / logger) ----
	int32 GetAgentId() const { return Id; }
	int32 GetParentId() const { return ParentId; }
	int32 GetGeneration() const { return Generation; }
	ESWSpecies GetSpecies() const { return Species; }
	const FSWGenome& GetGenome() const { return Genome; }
	const FSWSpeciesParams& GetParams() const { return Params; }
	const FSWContextualBandit& GetBandit() const { return Bandit; }
	float GetEnergy() const { return Energy; }
	float GetAge() const { return Age; }
	bool IsAlive() const { return bAlive; }
	ESWAction GetCurrentAction() const { return CurrentAction; }
	int32 GetCurrentContext() const { return CurrentContext; }
	float GetLastReward() const { return LastReward; }
	bool WasLastExplored() const { return bLastExplored; }
	int32 GetDecisionCount() const { return DecisionCount; }
	uint32 GetLastFeasibleMask() const { return LastFeasibleMask; }
	float GetLocalTraceX() const { return Percept.TraceX; }
	float GetLocalTraceY() const { return Percept.TraceY; }
	bool IsModifying() const { return bAlive && CurrentAction == ESWAction::Modify; }
	FString GetLabel() const;

	// Signal reception (called by manager when a neighbour signals).
	void ReceiveSignal(const FVector& Loc, float SimTime);

	// True while the agent's current action is Signal (manager broadcasts).
	bool IsSignalling() const { return bAlive && CurrentAction == ESWAction::Signal; }

	// Effective learning rate after mode gating (0 in mode A).
	float EffectiveAlpha() const;

	// Reproduction cost, charged by the manager when a child is spawned.
	void PayEnergy(float Cost) { Energy = FMath::Max(Energy - Cost, 0.f); }

	// Founders only: start part-way through life so the founding cohort does
	// not die of old age in a single step (which also starved mode N of births).
	void SetAge(float InAge) { Age = FMath::Clamp(InAge, 0.f, Params.MaxAge - 1.f); }

	// Q at the START of life, kept for the inspector ("initial vs now").
	const FSWContextualBandit& GetInitialBandit() const { return InitialBandit; }

	// ---- External policy (docs/POLICY_API.md) ----
	// An organism assigned to a policy server still senses, gates and learns exactly like the others;
	// only the choice among feasible actions comes from the server. -1 = the built-in bandit chooses.
	void SetPolicyServer(int32 ServerIdx) { PolicyServer = ServerIdx; }
	int32 GetPolicyServer() const { return PolicyServer; }
	bool IsExternal() const { return PolicyServer >= 0; }
	// True after Decide() prepared a decision (percept, context, mask) that waits for the manager's
	// exchange with the server. Built-in organisms never set this.
	bool IsDecisionDue() const { return bAlive && bAwaitingExternal; }
	// Finishes a prepared decision. ExternalAction = the server's choice (nullptr = none arrived);
	// an infeasible or missing action makes the built-in bandit choose. Returns true if the
	// external action was used.
	bool ResolveDecision(const ESWAction* ExternalAction);
	const FSWPercept& GetPercept() const { return Percept; }
	bool HasLastReward() const { return bLastRewardValid; }          // LastReward was credited at the last Decide()
	int32 GetLastRewardContext() const { return LastRewardContext; } // energy bin the credited action was chosen in
	bool WasLastActionExternal() const { return bLastActionExternal; }
	bool HasFreshSignal() const;
	const FVector& GetSignalLoc() const { return SignalLoc; }

protected:
	UPROPERTY(VisibleAnywhere) UStaticMeshComponent* Mesh;        // invisible pick sphere (root)
	UPROPERTY(VisibleAnywhere) UProceduralMeshComponent* Body;    // visible organism
	UPROPERTY(VisibleAnywhere) UProceduralMeshComponent* Trail;   // glowing ribbon (Lumen only), world space
	UPROPERTY() UMaterialInstanceDynamic* TrailMID;
	TArray<FVector> TrailPoints;      // newest last
	float TrailTimer = 0.f;
	bool bTrailDirty = false;
	UPROPERTY() UMaterialInstanceDynamic* MID;
	UPROPERTY() ASWWorldManager* Manager = nullptr;

	// Identity / lineage
	int32 Id = -1;
	int32 ParentId = -1;
	int32 Generation = 0;
	ESWSpecies Species = ESWSpecies::Lumen;
	FSWSpeciesParams Params;

	// Inherited
	FSWGenome Genome;

	// Learned
	FSWContextualBandit Bandit;
	FSWContextualBandit InitialBandit;

	// Physiology
	float Energy = 0.f;
	float Age = 0.f;
	bool bAlive = true;

	// Decision state
	ESWAction CurrentAction = ESWAction::Rest;
	int32 CurrentContext = 0;
	float EnergyAtDecision = 0.f;
	float DecisionAccumulator = 0.f;
	float LastReward = 0.f;
	bool bLastExplored = false;
	bool bHasPendingUpdate = false;
	int32 DecisionCount = 0;
	uint32 LastFeasibleMask = 0;
	FSWPercept Percept;

	// Movement helpers
	FVector ExploreDir = FVector::ForwardVector;
	float ExploreTimer = 0.f;
	bool bMovedThisStep = false;
	float GaitPhase = 0.f;

	// Social memory
	bool bHasSignal = false;
	FVector SignalLoc = FVector::ZeroVector;
	float SignalTime = -1000.f;

	// Novelty (only used if WeightNovelty > 0)
	TSet<int32> VisitedCells;
	float NoveltyThisInterval = 0.f;
	float InteractionThisInterval = 0.f;   // wI term accumulated since the last decision

	bool bSelected = false;

	// External policy state (see SetPolicyServer)
	int32 PolicyServer = -1;
	bool bAwaitingExternal = false;
	bool bLastActionExternal = false;
	bool bLastRewardValid = false;
	int32 LastRewardContext = 0;

	void Decide();
	// Sense + gate: fills Percept, CurrentContext and LastFeasibleMask. No randomness.
	void PrepareDecision();
	uint32 BuildFeasibleMask() const;
	void ApplyAction(float Dt);
	void MoveToward(const FVector& Target, float Dt);
	void MoveAlong(const FVector& Dir, float Dt);
	void PlaceAt(FVector Loc, const FVector& Facing);
	void BuildBody();
	void UpdateVisual();
	void UpdateGait(float Dt);
	int32 CellIndex(const FVector& Loc) const;
};
