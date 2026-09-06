#include "SWWorldManager.h"
#include "SWAgent.h"
#include "SWResourcePatch.h"
#include "SWEnvironment.h"
#include "SWLeviathan.h"
#include "SWProcMesh.h"
#include "SymbioticWorld.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "UObject/UnrealType.h"

ASWWorldManager::ASWWorldManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	// Species defaults. Lumen: fast, short-lived, eats Resource A.
	LumenParams.MoveSpeed = 350.f;
	LumenParams.MaxEnergy = 100.f;
	LumenParams.StartEnergy = 60.f;
	LumenParams.BasalBurn = 0.9f;
	LumenParams.MoveBurn = 1.2f;
	LumenParams.SignalBurn = 1.0f;
	LumenParams.ForageRate = 7.0f;
	LumenParams.ForageRadius = 220.f;
	LumenParams.SenseRange = 1600.f;
	LumenParams.NeighbourRange = 900.f;
	LumenParams.CrowdRadius = 250.f;
	LumenParams.MaxAge = 150.f;
	LumenParams.MinReproAge = 20.f;
	LumenParams.ReproThreshold = 78.f;
	LumenParams.ReproCost = 40.f;
	LumenParams.MeshScale = 1.2f;
	LumenParams.GroundOffset = 0.f;
	LumenParams.PickRadius = 85.f;
	LumenParams.GaitFrequency = 3.6f;
	LumenParams.GaitAmplitude = 4.f;
	LumenParams.Color = FLinearColor(0.10f, 0.85f, 1.0f);
	LumenParams.PreferredResourceType = 0;

	// Tecton: slow, long-lived, big, eats Resource B. No terrain modification yet (P1).
	TectonParams.MoveSpeed = 140.f;
	TectonParams.MaxEnergy = 160.f;
	TectonParams.StartEnergy = 100.f;
	TectonParams.BasalBurn = 0.5f;
	TectonParams.MoveBurn = 0.6f;
	TectonParams.SignalBurn = 0.5f;
	TectonParams.ForageRate = 5.0f;
	TectonParams.ForageRadius = 300.f;
	TectonParams.SenseRange = 2200.f;
	TectonParams.NeighbourRange = 1200.f;
	TectonParams.CrowdRadius = 400.f;
	TectonParams.MaxAge = 300.f;
	TectonParams.MinReproAge = 50.f;
	TectonParams.ReproThreshold = 130.f;
	TectonParams.ReproCost = 60.f;
	TectonParams.MeshScale = 0.75f;
	TectonParams.GroundOffset = 0.f;
	TectonParams.PickRadius = 150.f;
	TectonParams.GaitFrequency = 1.1f;
	TectonParams.GaitAmplitude = 6.f;
	TectonParams.Color = FLinearColor(1.0f, 0.55f, 0.10f);
	TectonParams.PreferredResourceType = 1;
}

float ASWWorldManager::GetDroughtWaterDrop() const
{
	return Environment ? Look.DroughtWaterDrop * Environment->GetDroughtFactor() : 0.f;
}

float ASWWorldManager::GetGroundZ(float X, float Y) const
{
	return SWProc::GroundZ(Look, X, Y, GetDroughtWaterDrop());
}

ASWWorldManager* ASWWorldManager::Get(UWorld* World)
{
	if (!World) return nullptr;
	for (TActorIterator<ASWWorldManager> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ASWWorldManager::BeginPlay()
{
	Super::BeginPlay();
	ApplyCommandLineOverrides();
	if (!Settings.PolicyServers.IsEmpty())
	{
		PolicyClient.Configure(Settings.PolicyServers, Settings.PolicyTimeoutMs);
	}
	StartRun();
}

void ASWWorldManager::EndPlay(const EEndPlayReason::Type Reason)
{
	PolicyClient.Shutdown();
	Logger.Close();
	Super::EndPlay(Reason);
}

void ASWWorldManager::ApplyCommandLineOverrides()
{
	int32 SeedOverride = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("SWSeed="), SeedOverride))
	{
		Settings.Seed = SeedOverride;
	}
	FString ModeStr;
	if (FParse::Value(FCommandLine::Get(), TEXT("SWMode="), ModeStr))
	{
		ModeStr = ModeStr.ToUpper();
		if (ModeStr.StartsWith(TEXT("A"))) Settings.Mode = ESWLearningMode::LearningOff;
		else if (ModeStr.StartsWith(TEXT("B"))) Settings.Mode = ESWLearningMode::LearningOn;
		else if (ModeStr.StartsWith(TEXT("C"))) Settings.Mode = ESWLearningMode::LearningEvolution;
		else if (ModeStr.StartsWith(TEXT("N"))) Settings.Mode = ESWLearningMode::NeutralControl;
	}
	float ScaleOverride = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SWSpeed="), ScaleOverride) && ScaleOverride > 0.f)
	{
		TimeScale = FMath::Clamp(ScaleOverride, 0.f, 1000.f);
	}
	float DurationOverride = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SWDuration="), DurationOverride) && DurationOverride > 0.f)
	{
		QuitAtSimTime = DurationOverride;
	}
	bool bNoLogs = false;
	if (FParse::Bool(FCommandLine::Get(), TEXT("SWNoLogs="), bNoLogs) && bNoLogs)
	{
		Settings.bWriteLogs = false;
	}
	FString SetSpec;
	if (FParse::Value(FCommandLine::Get(), TEXT("SWSet="), SetSpec))
	{
		ApplyParameterOverrides(SetSpec);
	}
	// External policy servers. FParse::Value stops at ',' and -SWSet owns ';', so the value uses '|' and '='.
	FString PolicySpec;
	if (FParse::Value(FCommandLine::Get(), TEXT("SWPolicy="), PolicySpec))
	{
		Settings.PolicyServers = PolicySpec;
	}
	int32 PolicyTimeout = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("SWPolicyTimeoutMs="), PolicyTimeout) && PolicyTimeout > 0)
	{
		Settings.PolicyTimeoutMs = PolicyTimeout;
	}
	float PolicyShare = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SWPolicyShare="), PolicyShare))
	{
		Settings.PolicyShare = FMath::Clamp(PolicyShare, 0.f, 1.f);
	}
	bool bAuto = false;
	if (FParse::Bool(FCommandLine::Get(), TEXT("SWAutoSelect="), bAuto) && bAuto)
	{
		bAutoSelect = true;
	}
	FString ShotSpec;
	if (FParse::Value(FCommandLine::Get(), TEXT("SWShot="), ShotSpec))
	{
		// FParse::Value stops at commas, so the separator is ':' (Tools/run_sim.py converts).
		TArray<FString> Parts;
		ShotSpec.ReplaceInline(TEXT(","), TEXT(":"));
		ShotSpec.ParseIntoArray(Parts, TEXT(":"), true);
		for (const FString& P : Parts) ScreenshotTimes.Add(FCString::Atof(*P));
		ScreenshotTimes.Sort();
		NextScreenshotIdx = 0;
	}
}

bool ASWWorldManager::SetStructPropertyByName(UScriptStruct* StructType, void* StructPtr, const FString& Name, const FString& Value)
{
	FProperty* Prop = StructType->FindPropertyByName(FName(*Name));
	if (!Prop)
	{
		UE_LOG(LogSymbioticWorld, Warning, TEXT("SWSet: no property '%s' on %s"), *Name, *StructType->GetName());
		return false;
	}
	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(StructPtr);
	if (FFloatProperty* F = CastField<FFloatProperty>(Prop))       { F->SetPropertyValue(ValuePtr, FCString::Atof(*Value)); }
	else if (FDoubleProperty* D = CastField<FDoubleProperty>(Prop)) { D->SetPropertyValue(ValuePtr, FCString::Atod(*Value)); }
	else if (FIntProperty* I = CastField<FIntProperty>(Prop))       { I->SetPropertyValue(ValuePtr, FCString::Atoi(*Value)); }
	else if (FBoolProperty* B = CastField<FBoolProperty>(Prop))     { B->SetPropertyValue(ValuePtr, Value.ToBool()); }
	else if (FStrProperty* Str = CastField<FStrProperty>(Prop))     { Str->SetPropertyValue(ValuePtr, Value); }
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
		{
			// "r:g:b" or "r:g:b:a" (':' because FParse::Value stops at ',')
			TArray<FString> Parts; Value.ParseIntoArray(Parts, TEXT(":"), true);
			if (Parts.Num() < 3) { UE_LOG(LogSymbioticWorld, Warning, TEXT("SWSet: colour '%s' needs r:g:b"), *Name); return false; }
			FLinearColor C(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]), Parts.Num() > 3 ? FCString::Atof(*Parts[3]) : 1.f);
			*static_cast<FLinearColor*>(ValuePtr) = C;
		}
		else { UE_LOG(LogSymbioticWorld, Warning, TEXT("SWSet: unsupported struct for '%s'"), *Name); return false; }
	}
	else if (FEnumProperty* E = CastField<FEnumProperty>(Prop))
	{
		const int64 V = E->GetEnum()->GetValueByNameString(Value);
		if (V == INDEX_NONE) { UE_LOG(LogSymbioticWorld, Warning, TEXT("SWSet: bad enum value '%s' for %s"), *Value, *Name); return false; }
		E->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, V);
	}
	else
	{
		UE_LOG(LogSymbioticWorld, Warning, TEXT("SWSet: unsupported property type for '%s'"), *Name);
		return false;
	}
	UE_LOG(LogSymbioticWorld, Log, TEXT("SWSet: %s.%s = %s"), *StructType->GetName(), *Name, *Value);
	return true;
}

void ASWWorldManager::ApplyParameterOverrides(const FString& Spec)
{
	TArray<FString> Items;
	Spec.ParseIntoArray(Items, TEXT(";"), true);
	for (FString Item : Items)
	{
		Item.TrimStartAndEndInline();
		FString Key, Value;
		if (!Item.Split(TEXT("="), &Key, &Value)) continue;
		FString Scope, Name;
		if (!Key.Split(TEXT("."), &Scope, &Name)) { Scope = TEXT("Settings"); Name = Key; }
		Scope = Scope.ToLower();
		if (Scope == TEXT("settings"))     SetStructPropertyByName(FSWRunSettings::StaticStruct(),   &Settings,     Name, Value);
		else if (Scope == TEXT("lumen"))   SetStructPropertyByName(FSWSpeciesParams::StaticStruct(), &LumenParams,  Name, Value);
		else if (Scope == TEXT("tecton"))  SetStructPropertyByName(FSWSpeciesParams::StaticStruct(), &TectonParams, Name, Value);
		else if (Scope == TEXT("genome") || Scope == TEXT("founder"))
			SetStructPropertyByName(FSWGenome::StaticStruct(), &Settings.FounderGenome, Name, Value);
		else if (Scope == TEXT("look"))
			SetStructPropertyByName(FSWLookSettings::StaticStruct(), &Look, Name, Value);
		else UE_LOG(LogSymbioticWorld, Warning, TEXT("SWSet: unknown scope '%s' (use Settings/Lumen/Tecton/Genome/Look)"), *Scope);
	}
}

// ---------------------------------------------------------------------------
// Run lifecycle
// ---------------------------------------------------------------------------

void ASWWorldManager::StartRun()
{
	ClearWorld();

	Rng.Initialize(Settings.Seed);
	SimTime = 0.f;
	Accumulator = 0.f;
	Births = Deaths = DeathsStarvation = DeathsPredation = 0;
	NextAgentId = 1;
	bDrought = false;
	NeutralBirthTimer = AgentLogTimer = PopLogTimer = StatsTimer = 0.f;
	ExtDecisions[0] = ExtDecisions[1] = ExtFallbacks[0] = ExtFallbacks[1] = 0;
	ExternalCount = 0;
	StepCounter = 0;
	InitialLumenTarget = Settings.InitialLumen;
	InitialTectonTarget = Settings.InitialTecton;

	RunId = FString::Printf(TEXT("%s_seed%d_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")), Settings.Seed, SWModeName(Settings.Mode));
	if (Settings.bWriteLogs)
	{
		Logger.Open(RunId, Settings.Seed, Settings.Mode);
	}

	TraceX.Init(Settings.TraceCells, Settings.WorldHalfSize, Settings.TraceXHalfLife, Settings.TraceMax);
	TraceY.Init(Settings.TraceCells, Settings.WorldHalfSize, Settings.TraceYHalfLife, Settings.TraceMax);

	if (PolicyClient.HasServers())
	{
		PolicyClient.Tick();                     // first connection attempt (bounded by the timeout)
		PolicyClient.SetHello(BuildHelloLine()); // sent now to connected servers, and again on every (re)connect
	}

	PopHistory.Reset();
	SpawnPatches();
	SpawnFounders();
	SpawnLeviathans();
	RecomputeStats();
	if (bAutoSelect) CycleSelection();

	UE_LOG(LogSymbioticWorld, Log, TEXT("Run started: %s  mode=%s seed=%d lumen=%d tecton=%d"),
		*RunId, SWModeName(Settings.Mode), Settings.Seed, Settings.InitialLumen, Settings.InitialTecton);
}

void ASWWorldManager::ResetRun()
{
	StartRun();
}

void ASWWorldManager::CycleMode()
{
	switch (Settings.Mode)
	{
	case ESWLearningMode::LearningOff:       Settings.Mode = ESWLearningMode::LearningOn; break;
	case ESWLearningMode::LearningOn:        Settings.Mode = ESWLearningMode::LearningEvolution; break;
	case ESWLearningMode::LearningEvolution: Settings.Mode = ESWLearningMode::NeutralControl; break;
	default:                                 Settings.Mode = ESWLearningMode::LearningOff; break;
	}
	ResetRun();
}

void ASWWorldManager::ToggleDrought()
{
	bDrought = !bDrought;
	UE_LOG(LogSymbioticWorld, Log, TEXT("Drought %s at sim_time %.1f"), bDrought ? TEXT("ON") : TEXT("OFF"), SimTime);
}

void ASWWorldManager::ClearWorld()
{
	Logger.Close();
	SelectedAgent = nullptr;
	for (ASWAgent* A : Agents) if (IsValid(A)) A->Destroy();
	for (ASWAgent* A : PendingSpawns) if (IsValid(A)) A->Destroy();
	for (ASWResourcePatch* P : Patches) if (IsValid(P)) P->Destroy();
	for (ASWLeviathan* Lv : Leviathans) if (IsValid(Lv)) Lv->Destroy();
	Agents.Reset();
	PendingSpawns.Reset();
	Patches.Reset();
	Leviathans.Reset();
}

void ASWWorldManager::SpawnLeviathans()
{
	UWorld* World = GetWorld();
	if (!World || !Settings.bLeviathan) return;

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 i = 0; i < Settings.LeviathanCount; ++i)
	{
		ASWLeviathan* Lv = World->SpawnActor<ASWLeviathan>(ASWLeviathan::StaticClass(), FTransform::Identity, SP);
		if (!Lv) continue;
		// Init() places the animal on the channel; it draws only from its own visual
		// stream, so adding or removing leviathans does not shift the simulation RNG.
		Lv->Init(this, i);
		Leviathans.Add(Lv);
	}
	if (Leviathans.Num() > 0)
	{
		UE_LOG(LogSymbioticWorld, Log, TEXT("Leviathan: %d patrolling the channel (strike radius %.0f uu, cooldown %.1f s)"),
			Leviathans.Num(), Settings.LeviathanStrikeRadius, Settings.LeviathanStrikeCooldown);
	}
}

void ASWWorldManager::LeviathanStep(float Dt)
{
	// Every leviathan moves and nominates its victims first; the reaping happens
	// here, in one place, so death accounting matches the starvation/age path and
	// two animals cannot both claim the same organism.
	TArray<ASWAgent*> Victims;
	for (ASWLeviathan* Lv : Leviathans)
	{
		if (IsValid(Lv)) Lv->Step(Dt, Victims);
	}
	for (ASWAgent* V : Victims)
	{
		if (!IsValid(V)) continue;
		const int32 Idx = Agents.Find(V);
		if (Idx == INDEX_NONE) continue;
		Deaths++;
		DeathsPredation++;
		// deaths.csv already carries a 'cause' column, so this needs no schema change.
		Logger.LogDeath(SimTime, *V, TEXT("predation"));
		const bool bWasSelected = (SelectedAgent == V);
		if (bWasSelected) SelectedAgent = nullptr;
		V->Destroy();
		Agents.RemoveAtSwap(Idx);
		if (bWasSelected && bAutoSelect && Agents.Num() > 0)
		{
			// Deliberately NOT CycleSelection(): that draws from the seeded simulation
			// stream, so a predation event would shift the RNG and make -SWAutoSelect=1
			// (a screenshot-only flag) change the run. Lowest living id is deterministic.
			ASWAgent* Next = nullptr;
			for (ASWAgent* A : Agents)
			{
				if (!IsValid(A)) continue;
				if (!Next || A->GetAgentId() < Next->GetAgentId()) Next = A;
			}
			if (Next) SelectAgent(Next);
		}
	}
}

FVector ASWWorldManager::RandomArenaPoint(float Margin)
{
	const float H = FMath::Max(Settings.WorldHalfSize - Margin, 100.f);
	FVector P(Rng.FRandRange(-H, H), Rng.FRandRange(-H, H), 0.f);
	P.Z = SWProc::GroundZ(Look, P.X, P.Y);
	return P;
}

void ASWWorldManager::SpawnPatches()
{
	UWorld* World = GetWorld();
	if (!World) return;

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	auto SpawnType = [&](int32 Type, int32 Count)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			FVector Loc = RandomArenaPoint(300.f);
			// Keep patches out of the river channel so they are not under water.
			for (int32 Try = 0; Try < 8 && FMath::Abs(Loc.Y - SWProc::RiverCenterY(Look, Loc.X)) < Look.RiverWidth * 1.6f; ++Try)
			{
				Loc = RandomArenaPoint(300.f);
			}
			Loc.Z = SWProc::TerrainHeight(Look, Loc.X, Loc.Y);
			ASWResourcePatch* P = World->SpawnActor<ASWResourcePatch>(ASWResourcePatch::StaticClass(), Loc, FRotator::ZeroRotator, SP);
			if (!P) continue;
			// Regen varies per patch so the landscape is not uniform.
			const float Regen = Settings.PatchRegenPerSec * Rng.FRandRange(0.6f, 1.4f);
			P->Init(Type, Settings.PatchCapacity, Regen, Rng.FRandRange(0.5f, 1.0f));
			Patches.Add(P);
		}
	};
	SpawnType(0, Settings.ResourcePatchesA);
	SpawnType(1, Settings.ResourcePatchesB);
}

FSWGenome ASWWorldManager::MakeFounderGenome()
{
	FSWGenome G = Settings.FounderGenome;
	if (Settings.Mode == ESWLearningMode::LearningOn)
	{
		// Mode B: identical learning parameters for everyone; nothing to evolve.
		G.Clamp();
		return G;
	}
	// Modes A / C / N: founders vary so selection (or drift) has standing variation.
	return G.Mutated(Rng, Settings.FounderSpread);
}

FSWGenome ASWWorldManager::MakeChildGenome(const FSWGenome& Parent)
{
	if (Settings.Mode == ESWLearningMode::LearningOn)
	{
		return Parent;   // exact copy: meta-parameters fixed
	}
	return Parent.Mutated(Rng, Settings.MutationSigma);
}

void ASWWorldManager::SpawnFounders()
{
	const float AgeSpread = FMath::Clamp(Settings.FounderAgeSpread, 0.f, 0.95f);
	for (int32 i = 0; i < Settings.InitialLumen; ++i)
	{
		ASWAgent* A = SpawnAgent(ESWSpecies::Lumen, MakeFounderGenome(), RandomArenaPoint(200.f), -1, 0, LumenParams.StartEnergy);
		if (!A) continue;
		A->SetAge(Rng.FRandRange(0.f, AgeSpread * LumenParams.MaxAge));
		Agents.Add(A);
	}
	for (int32 i = 0; i < Settings.InitialTecton; ++i)
	{
		ASWAgent* A = SpawnAgent(ESWSpecies::Tecton, MakeFounderGenome(), RandomArenaPoint(200.f), -1, 0, TectonParams.StartEnergy);
		if (!A) continue;
		A->SetAge(Rng.FRandRange(0.f, AgeSpread * TectonParams.MaxAge));
		Agents.Add(A);
	}
}

ASWAgent* ASWWorldManager::SpawnAgent(ESWSpecies Species, const FSWGenome& Genome, const FVector& Loc, int32 ParentId, int32 Generation, float Energy)
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASWAgent* A = World->SpawnActor<ASWAgent>(ASWAgent::StaticClass(), Loc, FRotator::ZeroRotator, SP);
	if (!A) return nullptr;
	const FSWSpeciesParams& P = Species == ESWSpecies::Lumen ? LumenParams : TectonParams;
	A->Init(this, Species, P, Genome, NextAgentId++, ParentId, Generation, Energy);
	AssignPolicy(A);
	return A;
}

bool ASWWorldManager::TryReproduce(ASWAgent* Parent)
{
	if (!Parent || !Parent->IsAlive()) return false;
	if (Agents.Num() + PendingSpawns.Num() >= Settings.MaxPopulation) return false;

	const FSWSpeciesParams& P = Parent->GetParams();
	if (Parent->GetEnergy() < P.ReproThreshold) return false;

	// Parent pays the cost; child is born with it. Selection pressure comes from
	// this cost plus the energy threshold, nothing else.
	const FSWGenome ChildGenome = MakeChildGenome(Parent->GetGenome());
	const FVector Offset = FVector(Rng.FRandRange(-120.f, 120.f), Rng.FRandRange(-120.f, 120.f), 0.f);
	FVector Loc = Parent->GetActorLocation() + Offset;
	ClampToArena(Loc);
	Loc.Z = GetGroundZ(Loc.X, Loc.Y);

	ASWAgent* Child = SpawnAgent(Parent->GetSpecies(), ChildGenome, Loc, Parent->GetAgentId(), Parent->GetGeneration() + 1, P.ReproCost);
	if (!Child) return false;

	Parent->PayEnergy(P.ReproCost);

	PendingSpawns.Add(Child);
	Births++;
	Logger.LogBirth(SimTime, *Parent, *Child);
	return true;
}

// ---------------------------------------------------------------------------
// Per-frame driver and fixed logical step
// ---------------------------------------------------------------------------

void ASWWorldManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (PolicyClient.HasServers()) PolicyClient.Tick();
	if (bPaused || TimeScale <= 0.f) return;

	const double T0 = FPlatformTime::Seconds();

	Accumulator += DeltaSeconds * TimeScale;
	const float Dt = FMath::Max(Settings.LogicalSubstep, 0.01f);
	int32 Steps = 0;
	while (Accumulator >= Dt && Steps < Settings.MaxSubstepsPerFrame)
	{
		if (QuitAtSimTime > 0.f && SimTime >= QuitAtSimTime) break;   // exact, frame-rate independent end
		StepWorld(Dt);
		Accumulator -= Dt;
		Steps++;
	}
	// If we hit the substep cap, drop the backlog rather than spiralling.
	if (Steps >= Settings.MaxSubstepsPerFrame) Accumulator = 0.f;

	LastStepMs = static_cast<float>((FPlatformTime::Seconds() - T0) * 1000.0);

	// Visual-only per-frame work (never per substep): trail ribbons.
	if (Steps > 0 && Look.bLumenTrails)
	{
		for (ASWAgent* A : Agents) if (IsValid(A)) A->UpdateTrailVisual();
	}

	while (NextScreenshotIdx < ScreenshotTimes.Num() && SimTime >= ScreenshotTimes[NextScreenshotIdx])
	{
		const FString Name = FString::Printf(TEXT("SW_%s_t%04d.png"), *RunId, FMath::RoundToInt(ScreenshotTimes[NextScreenshotIdx]));
		FScreenshotRequest::RequestScreenshot(Name, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogSymbioticWorld, Log, TEXT("Screenshot requested: %s (sim %.1f)"), *Name, SimTime);
		NextScreenshotIdx++;
	}

	if (QuitAtSimTime > 0.f && SimTime >= QuitAtSimTime && !bQuitRequested)
	{
		bQuitRequested = true;
		Logger.Flush();
		UE_LOG(LogSymbioticWorld, Log, TEXT("SWDuration reached (%.1f s); quitting. Logs: %s"), SimTime, *Logger.GetDirectory());
		if (UWorld* W = GetWorld())
		{
			if (APlayerController* PC = W->GetFirstPlayerController())
			{
				PC->ConsoleCommand(TEXT("quit"));
				return;
			}
		}
		FPlatformMisc::RequestExit(false);
	}
}

void ASWWorldManager::StepWorld(float Dt)
{
	SimTime += Dt;
	StepCounter++;

	// 0) Trace fields decay on the logical clock.
	if (Settings.bTraceFields)
	{
		TraceX.Decay(Dt);
		TraceY.Decay(Dt);
	}

	// 1) Resources regrow. Trace Y (Tecton soil work) multiplies regrowth in its cell,
	//    on top of (not instead of) the drought multiplier.
	const float RegenMul = bDrought ? Settings.DroughtRegenMultiplier : 1.f;
	const float CapMul = bDrought ? Settings.DroughtCapacityMultiplier : 1.f;
	ResourceTotalA = ResourceTotalB = 0.f;
	ResourceCapA = ResourceCapB = 0.f;
	for (ASWResourcePatch* P : Patches)
	{
		float LocalMul = RegenMul;
		if (Settings.bTraceFields)
		{
			const FVector PL = P->GetActorLocation();
			LocalMul *= 1.f + Settings.TraceYRegenGain * FMath::Clamp(TraceY.Sample(PL.X, PL.Y), 0.f, 1.f);
		}
		P->Step(Dt, LocalMul, CapMul);
		if (P->GetResourceType() == 0) { ResourceTotalA += P->GetStock(); ResourceCapA += P->GetCapacity(); }
		else                            { ResourceTotalB += P->GetStock(); ResourceCapB += P->GetCapacity(); }
	}

	// 2) Agents act. Iterate over a stable copy: children go to PendingSpawns.
	for (int32 i = Agents.Num() - 1; i >= 0; --i)
	{
		ASWAgent* A = Agents[i];
		if (!IsValid(A)) { Agents.RemoveAtSwap(i); continue; }
		const bool bAliveBefore = A->IsAlive();
		const bool bAlive = A->Step(Dt);
		if (bAliveBefore && !bAlive)
		{
			const TCHAR* Cause = A->GetEnergy() <= 0.f ? TEXT("starvation") : TEXT("age");
			if (A->GetEnergy() <= 0.f) DeathsStarvation++;
			Deaths++;
			Logger.LogDeath(SimTime, *A, Cause);
			const bool bWasSelected = (SelectedAgent == A);
			if (bWasSelected) SelectedAgent = nullptr;
			A->Destroy();
			Agents.RemoveAtSwap(i);
			if (bWasSelected && bAutoSelect) CycleSelection();
		}
	}

	// 2a) Leviathan: the river predator patrols the channel and strikes organisms
	//     that are in the water. Spatial selection pressure, not a species — see
	//     ASWLeviathan. Runs after the agents have moved this substep so a kill
	//     reflects where the organism actually ended up.
	if (Settings.bLeviathan && Leviathans.Num() > 0)
	{
		LeviathanStep(Dt);
	}

	// 2b) External policies: organisms assigned to a server prepared their decision in Step()
	//     (percept, context, feasibility mask) and now get the server's action, or the built-in
	//     bandit's if none arrived. Nothing here runs when no server is configured.
	if (PolicyClient.HasServers())
	{
		PolicyExchange();
	}

	// 3) Signals: every agent currently signalling broadcasts its known resource
	//    location to same-species neighbours within its social range.
	for (ASWAgent* A : Agents)
	{
		if (!A->IsSignalling()) continue;
		FSWPercept Pc;
		BuildPercept(A, Pc);
		if (!Pc.bResourceKnown) continue;
		const float Range = A->GetParams().NeighbourRange * (0.5f + A->GetGenome().Social);
		for (ASWAgent* B : Agents)
		{
			if (B == A || B->GetSpecies() != A->GetSpecies()) continue;
			if (FVector::DistSquared2D(A->GetActorLocation(), B->GetActorLocation()) <= Range * Range)
			{
				B->ReceiveSignal(Pc.NearestResourceLoc, SimTime);
			}
		}
	}

	// 4) Admit children.
	if (PendingSpawns.Num() > 0)
	{
		Agents.Append(PendingSpawns);
		PendingSpawns.Reset();
	}

	// 5) Neutral control births (fitness-independent).
	if (Settings.Mode == ESWLearningMode::NeutralControl)
	{
		NeutralBirthStep(Dt);
	}

	// 6) Stats + logging.
	StatsTimer += Dt;
	if (StatsTimer >= 0.5f)
	{
		StatsTimer = 0.f;
		RecomputeStats();
	}
	LogTick(Dt);
}

void ASWWorldManager::NeutralBirthStep(float Dt)
{
	NeutralBirthTimer += Dt;
	if (NeutralBirthTimer < Settings.NeutralBirthInterval) return;
	NeutralBirthTimer = 0.f;

	auto BirthFor = [&](ESWSpecies S, int32 Target)
	{
		TArray<ASWAgent*> Candidates;
		for (ASWAgent* A : Agents) if (A->GetSpecies() == S && A->IsAlive()) Candidates.Add(A);
		if (Candidates.Num() == 0 || Candidates.Num() >= Target) return;
		if (Agents.Num() + PendingSpawns.Num() >= Settings.MaxPopulation) return;
		// Uniformly random parent: reproduction carries no information about fitness.
		ASWAgent* Parent = Candidates[Rng.RandRange(0, Candidates.Num() - 1)];
		const FSWSpeciesParams& P = Parent->GetParams();
		FVector Loc = Parent->GetActorLocation() + FVector(Rng.FRandRange(-120.f, 120.f), Rng.FRandRange(-120.f, 120.f), 0.f);
		ClampToArena(Loc);
		Loc.Z = GetGroundZ(Loc.X, Loc.Y);
		ASWAgent* Child = SpawnAgent(S, MakeChildGenome(Parent->GetGenome()), Loc, Parent->GetAgentId(), Parent->GetGeneration() + 1, P.StartEnergy);
		if (!Child) return;
		PendingSpawns.Add(Child);
		Births++;
		Logger.LogBirth(SimTime, *Parent, *Child);
	};
	BirthFor(ESWSpecies::Lumen, InitialLumenTarget);
	BirthFor(ESWSpecies::Tecton, InitialTectonTarget);

	if (PendingSpawns.Num() > 0)
	{
		Agents.Append(PendingSpawns);
		PendingSpawns.Reset();
	}
}

// ---------------------------------------------------------------------------
// Perception
// ---------------------------------------------------------------------------

void ASWWorldManager::BuildPercept(const ASWAgent* Agent, FSWPercept& Out) const
{
	Out = FSWPercept();
	if (!Agent) return;

	const FVector Loc = Agent->GetActorLocation();
	const FSWSpeciesParams& P = Agent->GetParams();
	Out.Energy = Agent->GetEnergy();
	Out.MaxEnergy = P.MaxEnergy;

	// Nearest patch of the preferred type within sense range, with stock > 0.
	float BestD2 = P.SenseRange * P.SenseRange;
	for (ASWResourcePatch* Patch : Patches)
	{
		if (Patch->GetResourceType() != P.PreferredResourceType) continue;
		if (Patch->GetStock() <= 0.5f) continue;
		const float D2 = FVector::DistSquared2D(Loc, Patch->GetActorLocation());
		if (D2 < BestD2)
		{
			BestD2 = D2;
			Out.NearestPatch = Patch;
		}
	}
	if (Out.NearestPatch)
	{
		Out.bResourceKnown = true;
		Out.NearestResourceLoc = Out.NearestPatch->GetActorLocation();
		Out.NearestResourceDist = FMath::Sqrt(BestD2);
		Out.NearestResourceStock = Out.NearestPatch->GetStock();
	}

	// Neighbours. Social responsiveness scales the effective range [0.5x, 1.5x].
	const float Range = P.NeighbourRange * (0.5f + Agent->GetGenome().Social);
	const float R2 = Range * Range;
	FVector Sum = FVector::ZeroVector;
	int32 NSame = 0;
	for (const ASWAgent* Other : Agents)
	{
		if (Other == Agent || !Other->IsAlive()) continue;
		const float D2 = FVector::DistSquared2D(Loc, Other->GetActorLocation());
		const float D = FMath::Sqrt(D2);
		Out.NearestAnyAgentDist = FMath::Min(Out.NearestAnyAgentDist, D);
		if (D2 > R2) continue;
		if (Other->GetSpecies() == Agent->GetSpecies())
		{
			NSame++;
			Sum += Other->GetActorLocation();
		}
		else
		{
			Out.OtherSpeciesInRange++;
		}
	}
	Out.SameSpeciesInRange = NSame;
	if (NSame > 0)
	{
		Out.bNeighbourKnown = true;
		Out.NeighbourCentroid = Sum / static_cast<float>(NSame);
	}

	// Trace fields and soil state.
	Out.bOnLand = SWProc::TerrainHeight(Look, Loc.X, Loc.Y) > Look.WaterLevel;
	if (Settings.bTraceFields)
	{
		Out.TraceX = TraceX.Sample(Loc.X, Loc.Y);
		Out.TraceY = TraceY.Sample(Loc.X, Loc.Y);
		Out.bTraceXGradient = TraceX.Gradient(Loc.X, Loc.Y, Out.TraceXGradientDir);
		const float Cell = TraceY.CellSize();
		for (ASWResourcePatch* Patch : Patches)
		{
			const FVector PL = Patch->GetActorLocation();
			if (FMath::Abs(PL.X - Loc.X) <= Cell && FMath::Abs(PL.Y - Loc.Y) <= Cell && Patch->GetStock() < 0.5f * Patch->GetCapacity())
			{
				Out.bPatchInCellNeedsSoil = true;
				break;
			}
		}
	}
}

float ASWWorldManager::DepositTraceX(const ASWAgent* Agent, float Amount)
{
	if (!Settings.bTraceFields || !Agent) return 0.f;
	const FVector L = Agent->GetActorLocation();
	// Usefulness is diminishing: new information where there was none, near a stocked resource.
	const float Before = TraceX.Sample(L.X, L.Y);
	TraceX.Deposit(L.X, L.Y, Amount);
	FSWPercept Pc;
	BuildPercept(Agent, Pc);
	return Pc.bResourceKnown ? FMath::Clamp(1.f - Before / FMath::Max(Settings.TraceMax, 0.01f), 0.f, 1.f) : 0.f;
}

float ASWWorldManager::DepositTraceY(const ASWAgent* Agent, float Amount)
{
	if (!Settings.bTraceFields || !Agent) return 0.f;
	const FVector L = Agent->GetActorLocation();
	const float Before = TraceY.Sample(L.X, L.Y);
	TraceY.Deposit(L.X, L.Y, Amount);
	FSWPercept Pc;
	BuildPercept(Agent, Pc);
	return Pc.bPatchInCellNeedsSoil ? FMath::Clamp(1.f - Before / FMath::Max(Settings.TraceMax, 0.01f), 0.f, 1.f) : 0.f;
}

float ASWWorldManager::TakeFromPatch(ASWResourcePatch* Patch, float Amount)
{
	if (!IsValid(Patch)) return 0.f;
	return Patch->Take(Amount);
}

void ASWWorldManager::RegroundAll()
{
	for (ASWAgent* A : Agents) if (IsValid(A)) A->SnapToGround();
	for (ASWResourcePatch* P : Patches)
	{
		if (!IsValid(P)) continue;
		FVector L = P->GetActorLocation();
		L.Z = SWProc::TerrainHeight(Look, L.X, L.Y);
		P->SetActorLocation(L);
	}
}

bool ASWWorldManager::ClampToArena(FVector& Loc) const
{
	const float H = Settings.WorldHalfSize;
	bool bClamped = false;
	if (Loc.X < -H) { Loc.X = -H; bClamped = true; }
	if (Loc.X >  H) { Loc.X =  H; bClamped = true; }
	if (Loc.Y < -H) { Loc.Y = -H; bClamped = true; }
	if (Loc.Y >  H) { Loc.Y =  H; bClamped = true; }
	return bClamped;
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

void ASWWorldManager::SelectAgent(ASWAgent* Agent)
{
	if (IsValid(SelectedAgent)) SelectedAgent->SetSelected(false);
	SelectedAgent = Agent;
	if (IsValid(SelectedAgent)) SelectedAgent->SetSelected(true);
	UE_LOG(LogSymbioticWorld, Log, TEXT("Selection -> %s (sim %.1f)"), IsValid(SelectedAgent) ? *SelectedAgent->GetLabel() : TEXT("none"), SimTime);
}

bool ASWWorldManager::IsAutoSelect() const
{
	return bAutoSelect;
}

void ASWWorldManager::CycleSelection()
{
	if (Agents.Num() == 0) { SelectAgent(nullptr); return; }
	// Prefer Lumen (the demo species); pick the youngest so the audience sees a
	// full lifetime of learning.
	ASWAgent* Best = nullptr;
	for (ASWAgent* A : Agents)
	{
		if (!A->IsAlive() || A->GetSpecies() != ESWSpecies::Lumen || A == SelectedAgent) continue;
		if (!Best || A->GetAge() < Best->GetAge()) Best = A;
	}
	if (!Best) Best = Agents[Rng.RandRange(0, Agents.Num() - 1)];
	SelectAgent(Best);
}

// ---------------------------------------------------------------------------
// Stats + logging
// ---------------------------------------------------------------------------

void ASWWorldManager::ComputeSpeciesStats(ESWSpecies S, FSWSpeciesStats& Out) const
{
	Out = FSWSpeciesStats();
	double SumA = 0, SumA2 = 0, SumE = 0, SumE2 = 0, SumS = 0, SumS2 = 0, SumG = 0, SumEn = 0, SumV = 0, SumV2 = 0;
	for (const ASWAgent* A : Agents)
	{
		if (A->GetSpecies() != S || !A->IsAlive()) continue;
		const FSWGenome& G = A->GetGenome();
		Out.N++;
		SumA += G.Alpha;   SumA2 += G.Alpha * G.Alpha;
		SumE += G.Epsilon; SumE2 += G.Epsilon * G.Epsilon;
		SumS += G.Social;  SumS2 += G.Social * G.Social;
		SumV += G.EnvEffect; SumV2 += G.EnvEffect * G.EnvEffect;
		SumG += A->GetGeneration();
		SumEn += A->GetEnergy();
		Out.MaxGeneration = FMath::Max(Out.MaxGeneration, A->GetGeneration());
		Out.ActionCounts[static_cast<int32>(A->GetCurrentAction())]++;
	}
	if (Out.N == 0) return;
	const double N = Out.N;
	auto Sd = [N](double S1, double S2) { const double V = S2 / N - (S1 / N) * (S1 / N); return static_cast<float>(FMath::Sqrt(FMath::Max(V, 0.0))); };
	Out.MeanAlpha = static_cast<float>(SumA / N);  Out.SdAlpha = Sd(SumA, SumA2);
	Out.MeanEps = static_cast<float>(SumE / N);    Out.SdEps = Sd(SumE, SumE2);
	Out.MeanSocial = static_cast<float>(SumS / N); Out.SdSocial = Sd(SumS, SumS2);
	Out.MeanEnv = static_cast<float>(SumV / N);    Out.SdEnv = Sd(SumV, SumV2);
	Out.MeanGeneration = static_cast<float>(SumG / N);
	Out.MeanEnergy = static_cast<float>(SumEn / N);
}

void ASWWorldManager::RecomputeStats()
{
	ComputeSpeciesStats(ESWSpecies::Lumen, LumenStats);
	ComputeSpeciesStats(ESWSpecies::Tecton, TectonStats);
	PopHistory.Add(LumenStats.N + TectonStats.N);
	if (PopHistory.Num() > 60) PopHistory.RemoveAt(0);
	ExternalCount = 0;
	for (const ASWAgent* A : Agents) if (A->IsAlive() && A->IsExternal()) ExternalCount++;
}

float ASWWorldManager::GetStability() const
{
	if (PopHistory.Num() < 2) return 1.f;
	int32 Mn = PopHistory[0], Mx = PopHistory[0];
	for (int32 V : PopHistory) { Mn = FMath::Min(Mn, V); Mx = FMath::Max(Mx, V); }
	return Mx > 0 ? FMath::Clamp(1.f - (Mx - Mn) / (float)Mx, 0.f, 1.f) : 0.f;
}

void ASWWorldManager::LogTick(float Dt)
{
	if (!Logger.IsOpen()) return;

	AgentLogTimer += Dt;
	if (AgentLogTimer >= Settings.AgentLogInterval)
	{
		AgentLogTimer = 0.f;
		for (const ASWAgent* A : Agents)
		{
			Logger.LogAgent(*A, SimTime, bDrought, Births, Deaths, LumenStats.N, TectonStats.N, ResourceTotalA, ResourceTotalB, GetPolicyName(*A));
		}
	}

	PopLogTimer += Dt;
	if (PopLogTimer >= 5.f)
	{
		PopLogTimer = 0.f;
		RecomputeStats();
		const FSWSpeciesStats* Both[2] = { &LumenStats, &TectonStats };
		const ESWSpecies Sp[2] = { ESWSpecies::Lumen, ESWSpecies::Tecton };
		for (int32 i = 0; i < 2; ++i)
		{
			const FSWSpeciesStats& St = *Both[i];
			Logger.LogPopulation(SimTime, Sp[i], St.N, St.MeanAlpha, St.SdAlpha, St.MeanEps, St.SdEps,
				St.MeanSocial, St.SdSocial, St.MeanEnv, St.SdEnv, St.MeanGeneration, St.MaxGeneration, Births, Deaths,
				ResourceTotalA, ResourceTotalB, bDrought, TraceX.Mean(), TraceY.Mean(),
				ExtDecisions[static_cast<int32>(Sp[i])], ExtFallbacks[static_cast<int32>(Sp[i])]);
		}
		Logger.Flush();
	}
}

// ---------------------------------------------------------------------------
// External policy servers (docs/POLICY_API.md)
// ---------------------------------------------------------------------------

FString ASWWorldManager::GetPolicyName(const ASWAgent& A) const
{
	const int32 Idx = A.GetPolicyServer();
	if (Idx < 0 || Idx >= PolicyClient.NumServers()) return TEXT("builtin");
	return FString::Printf(TEXT("ext:%s"), *PolicyClient.GetServer(Idx).Name);
}

void ASWWorldManager::AssignPolicy(ASWAgent* A)
{
	if (!A || !PolicyClient.HasServers()) return;
	TArray<int32> Candidates;
	PolicyClient.ServersFor(A->GetSpecies(), Candidates);
	if (Candidates.Num() == 0) return;
	const float Share = FMath::Clamp(Settings.PolicyShare, 0.f, 1.f);
	if (Share <= 0.f) return;
	// Seeded draws only when they decide something: share < 1, or more than one server for the species.
	if (Share < 1.f && Rng.FRand() >= Share) return;
	const int32 Pick = Candidates.Num() == 1 ? Candidates[0] : Candidates[Rng.RandRange(0, Candidates.Num() - 1)];
	A->SetPolicyServer(Pick);
}

namespace
{
	FString JsonStr(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len() + 2);
		Out += TEXT("\"");
		for (TCHAR C : In)
		{
			switch (C)
			{
			case TEXT('"'):  Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default:
				if (C < 32) Out += FString::Printf(TEXT("\\u%04x"), static_cast<int32>(C));
				else Out.AppendChar(C);
			}
		}
		Out += TEXT("\"");
		return Out;
	}
	const TCHAR* JsonBool(bool B) { return B ? TEXT("true") : TEXT("false"); }
	FString JsonVec2(const FVector& V) { return FString::Printf(TEXT("[%.1f,%.1f]"), V.X, V.Y); }
	FString JsonDir2(const FVector& V) { return FString::Printf(TEXT("[%.4f,%.4f]"), V.X, V.Y); }
	const TCHAR* BinName(int32 Bin) { return Bin == 0 ? TEXT("LOW") : (Bin == 1 ? TEXT("MID") : TEXT("HIGH")); }
}

FString ASWWorldManager::BuildHelloLine() const
{
	FString Actions;
	for (int32 A = 0; A < SW_NUM_ACTIONS; ++A)
	{
		Actions += FString::Printf(TEXT("%s\"%s\""), A ? TEXT(",") : TEXT(""), SWActionName(static_cast<ESWAction>(A)));
	}
	const FString ModeName = SWModeName(Settings.Mode);
	// Which species each server controls is per server; the hello is shared, so list every served species.
	FString Controls;
	{
		bool bL = false, bT = false;
		for (int32 i = 0; i < PolicyClient.NumServers(); ++i) { bL |= PolicyClient.GetServer(i).bLumen; bT |= PolicyClient.GetServer(i).bTecton; }
		if (bL) Controls += TEXT("\"Lumen\"");
		if (bT) Controls += FString(bL ? TEXT(",") : TEXT("")) + TEXT("\"Tecton\"");
	}
	return FString::Printf(TEXT("{\"type\":\"hello\",\"protocol\":1,\"actions\":[%s],\"bins\":[\"LOW\",\"MID\",\"HIGH\"],\"species\":[\"Lumen\",\"Tecton\"],")
		TEXT("\"controls\":[%s],\"seed\":%d,\"mode\":\"%c\",\"mode_name\":%s,\"run_id\":%s,\"decision_interval\":%.3f,\"substep\":%.3f,")
		TEXT("\"timeout_ms\":%d,\"share\":%.3f,\"world_half_size\":%.1f,\"max_energy\":{\"Lumen\":%.1f,\"Tecton\":%.1f},")
		TEXT("\"max_age\":{\"Lumen\":%.1f,\"Tecton\":%.1f},\"learning\":\"tabular contextual bandit, gamma 0; the sim keeps updating each organism's own table with every reward\"}"),
		*Actions, *Controls, Settings.Seed, ModeName.Len() > 0 ? ModeName[0] : TEXT('?'), *JsonStr(ModeName), *JsonStr(RunId),
		Settings.DecisionInterval, Settings.LogicalSubstep, PolicyClient.GetTimeoutMs(), Settings.PolicyShare, Settings.WorldHalfSize,
		LumenParams.MaxEnergy, TectonParams.MaxEnergy, LumenParams.MaxAge, TectonParams.MaxAge);
}

FString ASWWorldManager::BuildDecideLine(int32 ServerIdx, const TArray<ASWAgent*>& Due) const
{
	FString Out;
	Out.Reserve(Due.Num() * 700 + 128);
	Out.Appendf(TEXT("{\"type\":\"decide\",\"t\":%.2f,\"step\":%d,\"server\":%s,\"agents\":["), SimTime, StepCounter, *JsonStr(PolicyClient.GetServer(ServerIdx).Name));
	for (int32 n = 0; n < Due.Num(); ++n)
	{
		const ASWAgent& A = *Due[n];
		const FSWPercept& Pc = A.GetPercept();
		const FSWGenome& G = A.GetGenome();
		const FVector Loc = A.GetActorLocation();
		const uint32 Mask = A.GetLastFeasibleMask();
		if (n) Out += TEXT(",");
		Out.Appendf(TEXT("{\"id\":%d,\"species\":\"%s\",\"generation\":%d,\"age\":%.2f,\"energy\":%.3f,\"max_energy\":%.1f,\"bin\":\"%s\",\"bin_index\":%d,\"mask\":["),
			A.GetAgentId(), SWSpeciesName(A.GetSpecies()), A.GetGeneration(), A.GetAge(), A.GetEnergy(), A.GetParams().MaxEnergy,
			BinName(A.GetCurrentContext()), A.GetCurrentContext());
		for (int32 a = 0; a < SW_NUM_ACTIONS; ++a) Out.Appendf(TEXT("%s%d"), a ? TEXT(",") : TEXT(""), FSWContextualBandit::IsFeasible(Mask, static_cast<ESWAction>(a)) ? 1 : 0);
		// Reward of the action that just ended (the server's previous choice, or the built-in's on a fallback).
		if (A.HasLastReward())
		{
			Out.Appendf(TEXT("],\"last_action\":\"%s\",\"last_reward\":%.4f,\"last_bin\":\"%s\",\"last_external\":%s,\"decisions\":%d,\"q\":["),
				SWActionName(A.GetCurrentAction()), A.GetLastReward(), BinName(A.GetLastRewardContext()), JsonBool(A.WasLastActionExternal()), A.GetDecisionCount());
		}
		else
		{
			Out.Appendf(TEXT("],\"last_action\":null,\"last_reward\":null,\"last_bin\":null,\"last_external\":false,\"decisions\":%d,\"q\":["), A.GetDecisionCount());
		}
		for (int32 b = 0; b < SW_NUM_ENERGY_BINS; ++b)
		{
			Out += b ? TEXT(",[") : TEXT("[");
			for (int32 a = 0; a < SW_NUM_ACTIONS; ++a) Out.Appendf(TEXT("%s%.4f"), a ? TEXT(",") : TEXT(""), A.GetBandit().Value(b, static_cast<ESWAction>(a)));
			Out += TEXT("]");
		}
		Out.Appendf(TEXT("],\"position\":%s,\"heading\":%.1f,\"percept\":{"), *JsonVec2(Loc), A.GetActorRotation().Yaw);
		// Every FSWPercept field by name (plus resource direction; distances are null when nothing is in range).
		Out.Appendf(TEXT("\"energy\":%.3f,\"max_energy\":%.1f,\"resource_known\":%s,"), Pc.Energy, Pc.MaxEnergy, JsonBool(Pc.bResourceKnown));
		if (Pc.bResourceKnown)
		{
			FVector Dir = Pc.NearestResourceLoc - Loc; Dir.Z = 0.f; Dir = Dir.GetSafeNormal();
			Out.Appendf(TEXT("\"resource_loc\":%s,\"resource_dist\":%.1f,\"resource_dir\":%s,\"resource_stock\":%.2f,"), *JsonVec2(Pc.NearestResourceLoc), Pc.NearestResourceDist, *JsonDir2(Dir), Pc.NearestResourceStock);
		}
		else
		{
			Out += TEXT("\"resource_loc\":null,\"resource_dist\":null,\"resource_dir\":null,\"resource_stock\":0,");
		}
		Out.Appendf(TEXT("\"same_species_in_range\":%d,\"other_species_in_range\":%d,\"neighbour_known\":%s,\"neighbour_centroid\":%s,"),
			Pc.SameSpeciesInRange, Pc.OtherSpeciesInRange, JsonBool(Pc.bNeighbourKnown), Pc.bNeighbourKnown ? *JsonVec2(Pc.NeighbourCentroid) : TEXT("null"));
		if (Pc.NearestAnyAgentDist < TNumericLimits<float>::Max()) Out.Appendf(TEXT("\"nearest_any_agent_dist\":%.1f,"), Pc.NearestAnyAgentDist);
		else Out += TEXT("\"nearest_any_agent_dist\":null,");
		const bool bSig = A.HasFreshSignal();
		Out.Appendf(TEXT("\"signal_known\":%s,\"signal_loc\":%s,"), JsonBool(bSig), bSig ? *JsonVec2(A.GetSignalLoc()) : TEXT("null"));
		Out.Appendf(TEXT("\"trace_x\":%.4f,\"trace_y\":%.4f,\"trace_x_gradient\":%s,\"trace_x_gradient_dir\":%s,\"on_land\":%s,\"patch_in_cell_needs_soil\":%s}"),
			Pc.TraceX, Pc.TraceY, JsonBool(Pc.bTraceXGradient), Pc.bTraceXGradient ? *JsonDir2(Pc.TraceXGradientDir) : TEXT("null"),
			JsonBool(Pc.bOnLand), JsonBool(Pc.bPatchInCellNeedsSoil));
		Out.Appendf(TEXT(",\"genome\":{\"alpha\":%.4f,\"epsilon\":%.4f,\"social\":%.4f,\"e\":%.4f}}"), G.Alpha, G.Epsilon, G.Social, G.EnvEffect);
	}
	Out += TEXT("]}");
	return Out;
}

void ASWWorldManager::PolicyExchange()
{
	const int32 NS = PolicyClient.NumServers();
	TArray<TArray<ASWAgent*>> Due;
	Due.SetNum(NS);
	TArray<ASWAgent*> Unserved;
	for (ASWAgent* A : Agents)
	{
		if (!IsValid(A) || !A->IsDecisionDue()) continue;
		const int32 Idx = A->GetPolicyServer();
		if (Idx >= 0 && Idx < NS) Due[Idx].Add(A); else Unserved.Add(A);
	}
	for (ASWAgent* A : Unserved)
	{
		A->ResolveDecision(nullptr);
		ExtFallbacks[static_cast<int32>(A->GetSpecies())]++;
	}

	TArray<FString> Lines;
	Lines.SetNum(NS);
	bool bAny = false;
	for (int32 i = 0; i < NS; ++i)
	{
		if (Due[i].Num() == 0 || !PolicyClient.GetServer(i).bConnected) continue;
		Lines[i] = BuildDecideLine(i, Due[i]);
		bAny = true;
	}
	TArray<TMap<int32, int32>> Actions;
	if (bAny) PolicyClient.Exchange(StepCounter, Lines, Actions);

	// Resolve in the same order every time (server, then agent order): deterministic given the replies.
	for (int32 i = 0; i < NS; ++i)
	{
		for (ASWAgent* A : Due[i])
		{
			const int32* Idx = Actions.IsValidIndex(i) ? Actions[i].Find(A->GetAgentId()) : nullptr;
			ESWAction Ext = ESWAction::Rest;
			const ESWAction* Ptr = nullptr;
			if (Idx && *Idx >= 0 && *Idx < SW_NUM_ACTIONS) { Ext = static_cast<ESWAction>(*Idx); Ptr = &Ext; }
			const bool bUsedExternal = A->ResolveDecision(Ptr);
			(bUsedExternal ? ExtDecisions : ExtFallbacks)[static_cast<int32>(A->GetSpecies())]++;
		}
	}
}
