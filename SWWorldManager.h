#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWTypes.h"
#include "SWLogger.h"
#include "SWTraceField.h"
#include "SWPolicyClient.h"
#include "SWWorldManager.generated.h"

class ASWAgent;
class ASWResourcePatch;
class ASWLeviathan;

USTRUCT()
struct FSWSpeciesStats
{
	GENERATED_BODY()
	int32 N = 0;
	float MeanAlpha = 0.f, SdAlpha = 0.f;
	float MeanEps = 0.f, SdEps = 0.f;
	float MeanSocial = 0.f, SdSocial = 0.f;
	float MeanEnv = 0.f, SdEnv = 0.f;
	float MeanGeneration = 0.f;
	int32 MaxGeneration = 0;
	float MeanEnergy = 0.f;
	int32 ActionCounts[SW_NUM_ACTIONS] = {0};
};

// Owns the whole simulation: seeded RNG, agents, resource patches, logical
// clock, time scale, drought, learning mode, selection and logging. Runs a
// fixed-step logical loop inside Tick so that 1x / 10x / 50x are the same
// simulation executed more times per frame, not a different simulation.
UCLASS()
class SYMBIOTICWORLD_API ASWWorldManager : public AActor
{
	GENERATED_BODY()

public:
	ASWWorldManager();

	static ASWWorldManager* Get(UWorld* World);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- Configuration (editable in the Details panel) ----
	UPROPERTY(EditAnywhere, Category = "Symbiotic World") FSWRunSettings Settings;
	UPROPERTY(EditAnywhere, Category = "Symbiotic World") FSWSpeciesParams LumenParams;
	UPROPERTY(EditAnywhere, Category = "Symbiotic World") FSWSpeciesParams TectonParams;
	UPROPERTY(EditAnywhere, Category = "Symbiotic World") FSWLookSettings Look;

	const FSWLookSettings& GetLook() const { return Look; }
	// Height an organism stands at, from the terrain field (pure function; no traces).
	float GetGroundZ(float X, float Y) const;
	// How far the drought has currently lowered the water (0 when not in drought).
	float GetDroughtWaterDrop() const;
	void SetEnvironment(class ASWEnvironment* Env) { Environment = Env; }
	class ASWEnvironment* GetEnvironment() const { return Environment; }

	// ---- Run control ----
	void StartRun();
	void ResetRun();
	void SetTimeScale(float Scale) { TimeScale = FMath::Clamp(Scale, 0.f, 1000.f); }
	float GetTimeScale() const { return TimeScale; }
	void TogglePause() { bPaused = !bPaused; }
	bool IsPaused() const { return bPaused; }
	void ToggleDrought();
	bool IsDrought() const { return bDrought; }
	void CycleMode();   // A -> B -> C -> N -> A, then resets the run

	// ---- Simulation services used by agents ----
	FRandomStream& GetRng() { return Rng; }
	const FSWRunSettings& GetSettings() const { return Settings; }
	float GetSimTime() const { return SimTime; }
	void BuildPercept(const ASWAgent* Agent, FSWPercept& Out) const;
	float TakeFromPatch(ASWResourcePatch* Patch, float Amount);
	// Trace fields (spec §7). Deposit returns the "useful interaction" score in [0,1].
	float DepositTraceX(const ASWAgent* Agent, float Amount);
	float DepositTraceY(const ASWAgent* Agent, float Amount);
	const FSWTraceField& GetTraceX() const { return TraceX; }
	const FSWTraceField& GetTraceY() const { return TraceY; }
	bool TryReproduce(ASWAgent* Parent);
	bool ClampToArena(FVector& Loc) const;   // returns true if clamped
	void RegroundAll();                       // snap every organism/patch to the terrain (after the environment exists)

	// ---- Selection (HUD) ----
	void SelectAgent(ASWAgent* Agent);
	ASWAgent* GetSelectedAgent() const { return SelectedAgent; }
	void CycleSelection();
	bool IsAutoSelect() const;

	// ---- Stats (HUD / logger) ----
	const FSWSpeciesStats& GetStats(ESWSpecies S) const { return S == ESWSpecies::Lumen ? LumenStats : TectonStats; }
	int32 GetBirths() const { return Births; }
	int32 GetDeaths() const { return Deaths; }
	int32 GetDeathsStarvation() const { return DeathsStarvation; }
	int32 GetDeathsPredation() const { return DeathsPredation; }
	const TArray<ASWLeviathan*>& GetLeviathans() const { return Leviathans; }
	float GetResourceTotal(int32 Type) const { return Type == 0 ? ResourceTotalA : ResourceTotalB; }
	float GetResourceCapacity(int32 Type) const { return Type == 0 ? ResourceCapA : ResourceCapB; }
	int32 GetLivingCount() const { return Agents.Num(); }
	const TArray<ASWAgent*>& GetAgents() const { return Agents; }
	const TArray<ASWResourcePatch*>& GetPatches() const { return Patches; }
	// Population steadiness over the last ~30 logical s: 1 - (max - min) / max. Descriptive only.
	float GetStability() const;
	FString GetRunId() const { return RunId; }
	FString GetLogDirectory() const { return Logger.GetDirectory(); }
	float GetLastStepMs() const { return LastStepMs; }

	// ---- External policy servers (docs/POLICY_API.md) ----
	bool HasPolicyServers() const { return PolicyClient.HasServers(); }
	const FSWPolicyClient& GetPolicyClient() const { return PolicyClient; }
	int32 GetExternalCount() const { return ExternalCount; }            // living organisms assigned to a server
	FString GetPolicyName(const ASWAgent& A) const;                      // "builtin" or "ext:host:port" (CSV / HUD)
	int32 GetExtDecisions(ESWSpecies S) const { return ExtDecisions[static_cast<int32>(S)]; }
	int32 GetExtFallbacks(ESWSpecies S) const { return ExtFallbacks[static_cast<int32>(S)]; }

protected:
	UPROPERTY() TArray<ASWAgent*> Agents;
	UPROPERTY() TArray<ASWResourcePatch*> Patches;
	UPROPERTY() TArray<ASWLeviathan*> Leviathans;
	UPROPERTY() ASWAgent* SelectedAgent = nullptr;
	UPROPERTY() class ASWEnvironment* Environment = nullptr;

	FRandomStream Rng;
	FString RunId;
	float SimTime = 0.f;
	float TimeScale = 1.f;
	float Accumulator = 0.f;
	bool bPaused = false;
	bool bDrought = false;
	int32 NextAgentId = 1;
	int32 Births = 0;
	int32 Deaths = 0;
	int32 DeathsStarvation = 0;
	int32 DeathsPredation = 0;
	float ResourceTotalA = 0.f, ResourceTotalB = 0.f;
	float ResourceCapA = 0.f, ResourceCapB = 0.f;
	float NeutralBirthTimer = 0.f;
	float AgentLogTimer = 0.f;
	float PopLogTimer = 0.f;
	float StatsTimer = 0.f;
	float LastStepMs = 0.f;
	int32 InitialLumenTarget = 0, InitialTectonTarget = 0;
	// -SWDuration=<logical s>: quit the process when SimTime reaches it (headless runs).
	float QuitAtSimTime = 0.f;
	bool bQuitRequested = false;

	FSWSpeciesStats LumenStats, TectonStats;
	FSWRunLogger Logger;
	FSWTraceField TraceX, TraceY;
	TArray<int32> PopHistory;   // total population sampled every RecomputeStats (0.5 s), last 60 samples

	// Agents waiting to be added (children spawned during a step).
	TArray<ASWAgent*> PendingSpawns;

	void StepWorld(float Dt);
	void SpawnFounders();
	void SpawnPatches();
	void SpawnLeviathans();
	// Advances every leviathan and reaps the organisms they took (cause "predation").
	void LeviathanStep(float Dt);
	ASWAgent* SpawnAgent(ESWSpecies Species, const FSWGenome& Genome, const FVector& Loc, int32 ParentId, int32 Generation, float Energy);
	FSWGenome MakeFounderGenome();
	FSWGenome MakeChildGenome(const FSWGenome& Parent);
	void ClearWorld();
	void ApplyCommandLineOverrides();
	// -SWSet="Settings.PatchRegenPerSec=5;Lumen.ReproThreshold=85;Tecton.MaxAge=400"
	// Sets any numeric/bool UPROPERTY on Settings / LumenParams / TectonParams
	// by name via reflection, so parameter sweeps need no recompile.
	void ApplyParameterOverrides(const FString& Spec);
	bool SetStructPropertyByName(UScriptStruct* StructType, void* StructPtr, const FString& Name, const FString& Value);
	// -SWShot=5,60,120 : request a screenshot (Saved/Screenshots) at these sim times.
	TArray<float> ScreenshotTimes;
	int32 NextScreenshotIdx = 0;
	// -SWAutoSelect=1 : select the youngest Lumen at start (and re-select when
	// it dies) so unattended screenshots show the inspector.
	bool bAutoSelect = false;
	void RecomputeStats();
	void ComputeSpeciesStats(ESWSpecies S, FSWSpeciesStats& Out) const;

	// External policy: one connection per server; per substep one "decide" request per server for
	// every assigned organism whose decision is due, then ResolveDecision() on each with the reply.
	FSWPolicyClient PolicyClient;
	int32 ExtDecisions[2] = { 0, 0 };   // per species: decisions taken from a server
	int32 ExtFallbacks[2] = { 0, 0 };   // per species: server-assigned decisions the built-in bandit had to make
	int32 ExternalCount = 0;
	int32 StepCounter = 0;              // substeps since StartRun (echoed by replies to detect stale ones)
	void AssignPolicy(ASWAgent* A);     // at birth, with the seeded stream when PolicyShare < 1
	void PolicyExchange();
	FString BuildHelloLine() const;
	FString BuildDecideLine(int32 ServerIdx, const TArray<ASWAgent*>& Due) const;
	void NeutralBirthStep(float Dt);
	void LogTick(float Dt);
	FVector RandomArenaPoint(float Margin);
};
