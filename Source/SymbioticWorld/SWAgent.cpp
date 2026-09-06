#include "SWAgent.h"
#include "SWWorldManager.h"
#include "SWResourcePatch.h"
#include "SWProcMesh.h"
#include "SymbioticWorld.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ASWAgent::ASWAgent()
{
	PrimaryActorTick.bCanEverTick = false;

	// Root: an invisible sphere that only exists so the cursor can pick the organism.
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickSphere"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Mesh->SetCastShadow(false);
	Mesh->SetHiddenInGame(true);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Mesh->SetStaticMesh(SphereMesh.Object);
	}

	Body = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Mesh);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(true);
	Body->bUseAsyncCooking = true;

	Trail = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Trail"));
	Trail->SetupAttachment(Mesh);
	Trail->SetUsingAbsoluteLocation(true);
	Trail->SetUsingAbsoluteRotation(true);
	Trail->SetUsingAbsoluteScale(true);
	Trail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Trail->SetCastShadow(false);
	Trail->bUseAsyncCooking = true;
}

void ASWAgent::Init(ASWWorldManager* InManager, ESWSpecies InSpecies, const FSWSpeciesParams& InParams,
                    const FSWGenome& InGenome, int32 InId, int32 InParentId, int32 InGeneration, float InEnergy)
{
	Manager = InManager;
	Species = InSpecies;
	Params = InParams;
	Genome = InGenome;
	Genome.Clamp();
	Id = InId;
	ParentId = InParentId;
	Generation = InGeneration;
	Energy = FMath::Clamp(InEnergy, 1.f, Params.MaxEnergy);
	Age = 0.f;
	bAlive = true;

	Bandit.InitRandom(Manager->GetRng(), Manager->GetSettings().QInitMax);
	InitialBandit = Bandit;

	CurrentAction = ESWAction::Rest;
	CurrentContext = 0;
	EnergyAtDecision = Energy;
	DecisionAccumulator = 0.f;
	bHasPendingUpdate = false;
	DecisionCount = 0;

	// Random initial heading from the seeded stream.
	const float Ang = Manager->GetRng().FRandRange(0.f, 2.f * PI);
	ExploreDir = FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f);
	ExploreTimer = Manager->GetRng().FRandRange(2.f, 6.f);

	BuildBody();
	SnapToGround();
	SetActorRotation(ExploreDir.Rotation());
	UpdateVisual();

	// First decision happens immediately so the agent does not idle for a full interval.
	Decide();
}

void ASWAgent::BuildBody()
{
	const FSWLookSettings& L = Manager->GetLook();
	// Visual variation uses its own stream so it never perturbs the simulation RNG.
	FRandomStream VisRng(Id * 7919 + L.LookSeed * 131 + (Species == ESWSpecies::Lumen ? 0 : 17));

	SWProc::FMeshData M;
	if (Species == ESWSpecies::Lumen) SWProc::BuildLumen(VisRng, M);
	else                              SWProc::BuildTecton(VisRng, M);
	Body->CreateMeshSection_LinearColor(0, M.Verts, M.Tris, M.Normals, M.UV0, M.Colors, TArray<FProcMeshTangent>(), false);
	Body->SetRelativeScale3D(FVector(Params.MeshScale));
	Body->SetCastShadow(L.bCreatureShadows);

	// Pick sphere sized to the body (engine sphere is 100 uu across).
	Mesh->SetRelativeScale3D(FVector(Params.PickRadius / 50.f));

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SW_Creature.M_SW_Creature"));
	if (!Base) Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Base)
	{
		MID = UMaterialInstanceDynamic::Create(Base, this);
		const FLinearColor BodyCol = Species == ESWSpecies::Lumen ? L.LumenBody : L.TectonBody;
		const FLinearColor Glow = Species == ESWSpecies::Lumen ? L.LumenGlow : L.TectonGlow;
		MID->SetVectorParameterValue(TEXT("BodyColor"), BodyCol);
		MID->SetVectorParameterValue(TEXT("EmissiveColor"), Glow);
		MID->SetVectorParameterValue(TEXT("Color"), Glow);   // BasicShapeMaterial fallback
		MID->SetScalarParameterValue(TEXT("PulseSpeed"), Species == ESWSpecies::Lumen ? 2.4f : 0.8f);
		// Tecton: emissive = per-pixel crack network gated by the armour region; Lumen: vertex markings as painted.
		MID->SetScalarParameterValue(TEXT("CrackMode"), Species == ESWSpecies::Lumen ? 0.f : 1.f);
		MID->SetScalarParameterValue(TEXT("CrackScale"), 0.05f);
		MID->SetScalarParameterValue(TEXT("CrackWidth"), 0.10f);
		MID->SetScalarParameterValue(TEXT("Roughness"), Species == ESWSpecies::Lumen ? 0.35f : 0.8f);
		Body->SetMaterial(0, MID);
	}
}

void ASWAgent::UpdateTrailVisual()
{
	if (!Trail || !Manager || Species != ESWSpecies::Lumen) return;
	const FSWLookSettings& L = Manager->GetLook();
	if (!L.bLumenTrails) return;
	if (!bTrailDirty) return;
	bTrailDirty = false;

	if (!TrailMID)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SW_Trail.M_SW_Trail"));
		if (!Base) Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (Base)
		{
			TrailMID = UMaterialInstanceDynamic::Create(Base, this);
			TrailMID->SetVectorParameterValue(TEXT("TrailColor"), L.LumenGlow);
			TrailMID->SetVectorParameterValue(TEXT("Color"), L.LumenGlow);
			TrailMID->SetScalarParameterValue(TEXT("TrailGlow"), L.TrailGlow);
			Trail->SetMaterial(0, TrailMID);
		}
	}

	const int32 N = TrailPoints.Num();
	if (N < 2)
	{
		Trail->ClearAllMeshSections();
		return;
	}
	// Ribbon: two vertices per sample, offset sideways; width and brightness taper toward the tail.
	TArray<FVector> V; TArray<int32> T; TArray<FVector> Nrm; TArray<FVector2D> UV; TArray<FLinearColor> C;
	V.Reserve(N * 2); T.Reserve((N - 1) * 6); Nrm.Reserve(N * 2); UV.Reserve(N * 2); C.Reserve(N * 2);
	for (int32 i = 0; i < N; ++i)
	{
		const float F = N > 1 ? i / (float)(N - 1) : 1.f;          // 0 tail .. 1 head
		const FVector P = TrailPoints[i];
		const FVector Next = TrailPoints[FMath::Min(i + 1, N - 1)];
		const FVector Prev = TrailPoints[FMath::Max(i - 1, 0)];
		FVector Dir = (Next - Prev); Dir.Z = 0.f;
		Dir = Dir.IsNearlyZero() ? FVector::ForwardVector : Dir.GetSafeNormal();
		const FVector Side = FVector::CrossProduct(Dir, FVector::UpVector);
		const float W = L.TrailWidth * Params.MeshScale * (0.15f + 0.85f * F);
		V.Add(P + Side * W); V.Add(P - Side * W);
		Nrm.Add(FVector::UpVector); Nrm.Add(FVector::UpVector);
		UV.Add(FVector2D(0.f, F)); UV.Add(FVector2D(1.f, F));
		// R = emissive mask, G = fade along the ribbon (drives opacity in M_SW_Trail).
		C.Add(FLinearColor(1.f, F, 0.f, 1.f)); C.Add(FLinearColor(1.f, F, 0.f, 1.f));
	}
	for (int32 i = 0; i < N - 1; ++i)
	{
		const int32 A = i * 2, B = A + 1, Cc = A + 2, D = A + 3;
		T.Add(A); T.Add(Cc); T.Add(B);
		T.Add(B); T.Add(Cc); T.Add(D);
	}
	if (Trail->GetNumSections() == 0 || Trail->GetProcMeshSection(0) == nullptr || Trail->GetProcMeshSection(0)->ProcVertexBuffer.Num() != V.Num())
	{
		Trail->CreateMeshSection_LinearColor(0, V, T, Nrm, UV, C, TArray<FProcMeshTangent>(), false);
	}
	else
	{
		Trail->UpdateMeshSection_LinearColor(0, V, Nrm, UV, C, TArray<FProcMeshTangent>());
	}
}

FString ASWAgent::GetLabel() const
{
	return FString::Printf(TEXT("%s-%04d"), Species == ESWSpecies::Lumen ? TEXT("L") : TEXT("T"), Id);
}

float ASWAgent::EffectiveAlpha() const
{
	if (!Manager) return 0.f;
	return Manager->GetSettings().Mode == ESWLearningMode::LearningOff ? 0.f : Genome.Alpha;
}

void ASWAgent::ReceiveSignal(const FVector& Loc, float SimTime)
{
	// Social responsiveness gates acceptance of a received signal.
	if (Manager->GetRng().FRand() < Genome.Social)
	{
		bHasSignal = true;
		SignalLoc = Loc;
		SignalTime = SimTime;
	}
}

int32 ASWAgent::CellIndex(const FVector& Loc) const
{
	const float Half = Manager ? Manager->GetSettings().WorldHalfSize : 5000.f;
	const int32 Cells = 24;
	const int32 X = FMath::Clamp(static_cast<int32>((Loc.X + Half) / (2.f * Half) * Cells), 0, Cells - 1);
	const int32 Y = FMath::Clamp(static_cast<int32>((Loc.Y + Half) / (2.f * Half) * Cells), 0, Cells - 1);
	return Y * Cells + X;
}

bool ASWAgent::Step(float Dt)
{
	if (!bAlive || !Manager) return false;

	Age += Dt;
	DecisionAccumulator += Dt;
	bMovedThisStep = false;

	ApplyAction(Dt);
	UpdateGait(Dt);

	// Trail sampling on the logical clock so the ribbon spans the same sim distance at any time scale.
	if (Species == ESWSpecies::Lumen && Manager->GetLook().bLumenTrails)
	{
		TrailTimer += Dt;
		if (TrailTimer >= Manager->GetLook().TrailSampleInterval)
		{
			TrailTimer = 0.f;
			const int32 MaxPts = FMath::Max(Manager->GetLook().TrailSamples, 2);
			if (bMovedThisStep)
			{
				TrailPoints.Add(GetActorLocation() + FVector(0.f, 0.f, 28.f * Params.MeshScale));
				if (TrailPoints.Num() > MaxPts) TrailPoints.RemoveAt(0);
				bTrailDirty = true;
			}
			else if (TrailPoints.Num() > 0)
			{
				TrailPoints.RemoveAt(0);   // standing still: the trail fades away from the tail
				bTrailDirty = true;
			}
		}
	}

	// Novelty bookkeeping (cheap; only affects reward if WeightNovelty > 0).
	if (Manager->GetSettings().WeightNovelty > 0.f)
	{
		const int32 Cell = CellIndex(GetActorLocation());
		if (!VisitedCells.Contains(Cell))
		{
			VisitedCells.Add(Cell);
			NoveltyThisInterval = 1.f;
		}
	}

	// Death rules. In the neutral control starvation is disabled so that
	// selection is removed entirely (see ESWLearningMode::NeutralControl).
	const bool bNeutral = Manager->GetSettings().Mode == ESWLearningMode::NeutralControl;
	if (bNeutral)
	{
		Energy = FMath::Max(Energy, 0.5f);
	}
	if (Age >= Params.MaxAge || (!bNeutral && Energy <= 0.f))
	{
		bAlive = false;
		return false;
	}

	if (DecisionAccumulator >= Manager->GetSettings().DecisionInterval)
	{
		DecisionAccumulator -= Manager->GetSettings().DecisionInterval;
		Decide();
	}

	// Reproduction is energy-gated (except neutral mode, where the manager
	// schedules fitness-independent births itself).
	if (!bNeutral && Age >= Params.MinReproAge && Energy >= Params.ReproThreshold)
	{
		Manager->TryReproduce(this);
	}

	UpdateVisual();
	return true;
}

void ASWAgent::Decide()
{
	const FSWRunSettings& S = Manager->GetSettings();

	// 1) Credit the action taken over the last interval with the reward that
	//    accrued during it. Reward is energy change (scaled) + optional novelty.
	if (bHasPendingUpdate)
	{
		const float DEnergy = (Energy - EnergyAtDecision) / FMath::Max(S.RewardScale, 1e-3f);
		LastReward = S.WeightEnergy * DEnergy + S.WeightNovelty * NoveltyThisInterval + S.WeightInteraction * InteractionThisInterval;
		Bandit.Update(CurrentContext, CurrentAction, LastReward, EffectiveAlpha());
	}
	NoveltyThisInterval = 0.f;
	InteractionThisInterval = 0.f;

	// 2) Sense, gate, select.
	Manager->BuildPercept(this, Percept);
	CurrentContext = Percept.EnergyBin();
	const uint32 Mask = BuildFeasibleMask();
	LastFeasibleMask = Mask;
	CurrentAction = Bandit.Select(CurrentContext, Mask, Genome.Epsilon, Manager->GetRng(), bLastExplored);

	EnergyAtDecision = Energy;
	bHasPendingUpdate = true;
	DecisionCount++;

	// Modify: one deposit per decision. Lumen writes Trace X (information), Tecton writes Trace Y (soil).
	if (CurrentAction == ESWAction::Modify)
	{
		const float Scale = Genome.EnvScale();   // inherited e: deposit strength
		if (Species == ESWSpecies::Lumen) InteractionThisInterval += Manager->DepositTraceX(this, S.TraceXDeposit * Scale);
		else                              InteractionThisInterval += Manager->DepositTraceY(this, S.TraceYDeposit * Scale);
	}

	if (CurrentAction == ESWAction::Explore)
	{
		// Re-roll heading occasionally so Explore is a random walk, not a straight line.
		ExploreTimer -= S.DecisionInterval;
		if (ExploreTimer <= 0.f)
		{
			const float Ang = Manager->GetRng().FRandRange(0.f, 2.f * PI);
			ExploreDir = FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f);
			ExploreTimer = Manager->GetRng().FRandRange(2.f, 6.f);
		}
	}
}

uint32 ASWAgent::BuildFeasibleMask() const
{
	uint32 Mask = 0;
	Mask |= FSWContextualBandit::Bit(ESWAction::Rest);
	Mask |= FSWContextualBandit::Bit(ESWAction::Explore);

	if (Percept.bResourceKnown && Percept.NearestResourceStock > 0.5f)
	{
		Mask |= FSWContextualBandit::Bit(ESWAction::Forage);
	}

	const float SimTime = Manager->GetSimTime();
	const bool bFreshSignal = bHasSignal && (SimTime - SignalTime) < 12.f;
	if (bFreshSignal || Percept.bNeighbourKnown)
	{
		Mask |= FSWContextualBandit::Bit(ESWAction::Follow);
	}
	if (Percept.NearestAnyAgentDist < Params.CrowdRadius)
	{
		Mask |= FSWContextualBandit::Bit(ESWAction::Avoid);
	}
	// Signal: only meaningful if there is something to signal about and someone to hear it.
	if (Species == ESWSpecies::Lumen && Percept.bResourceKnown && Percept.SameSpeciesInRange > 0)
	{
		Mask |= FSWContextualBandit::Bit(ESWAction::Signal);
	}
	// Modify: Lumen deposits an information marker (only meaningful with a stocked
	// resource in range); Tecton works the soil (only on land, with some energy).
	if (Manager->GetSettings().bTraceFields)
	{
		if (Species == ESWSpecies::Lumen && Percept.bResourceKnown)
		{
			Mask |= FSWContextualBandit::Bit(ESWAction::Modify);
		}
		else if (Species == ESWSpecies::Tecton && Percept.bOnLand && Energy > 0.25f * Params.MaxEnergy)
		{
			Mask |= FSWContextualBandit::Bit(ESWAction::Modify);
		}
	}
	return Mask;
}

void ASWAgent::ApplyAction(float Dt)
{
	const FSWRunSettings& S = Manager->GetSettings();
	float Burn = Params.BasalBurn;

	switch (CurrentAction)
	{
	case ESWAction::Rest:
		break;

	case ESWAction::Explore:
		MoveAlong(ExploreDir, Dt);
		Burn += Params.MoveBurn;
		break;

	case ESWAction::Forage:
		if (Percept.NearestPatch)
		{
			const float Dist = FVector::Dist2D(GetActorLocation(), Percept.NearestPatch->GetActorLocation());
			if (Dist > Params.ForageRadius)
			{
				MoveToward(Percept.NearestPatch->GetActorLocation(), Dt);
				Burn += Params.MoveBurn;
			}
			else
			{
				const float Want = FMath::Min(Params.ForageRate * Dt, Params.MaxEnergy - Energy);
				Energy += Manager->TakeFromPatch(Percept.NearestPatch, Want);
			}
		}
		break;

	case ESWAction::Follow:
	{
		const bool bFreshSignal = bHasSignal && (Manager->GetSimTime() - SignalTime) < 12.f;
		if (bFreshSignal)
		{
			MoveToward(SignalLoc, Dt);
			if (FVector::Dist2D(GetActorLocation(), SignalLoc) < Params.ForageRadius) bHasSignal = false;
		}
		else if (Percept.bNeighbourKnown)
		{
			MoveToward(Percept.NeighbourCentroid, Dt);
		}
		else if (Percept.bTraceXGradient && Percept.TraceX >= S.TraceXFollowMin)
		{
			MoveAlong(Percept.TraceXGradientDir, Dt);   // follow the information trail
		}
		Burn += Params.MoveBurn;
		break;
	}

	case ESWAction::Modify:
		Burn += S.ModifyBurn * Genome.EnvScale();   // stationary; the deposit happened at decision time; cost scales with e
		break;

	case ESWAction::Avoid:
		if (Percept.bNeighbourKnown)
		{
			FVector Away = GetActorLocation() - Percept.NeighbourCentroid;
			Away.Z = 0.f;
			if (Away.SizeSquared() < 1.f) Away = ExploreDir;
			MoveAlong(Away.GetSafeNormal(), Dt);
		}
		else
		{
			MoveAlong(ExploreDir, Dt);
		}
		Burn += Params.MoveBurn;
		break;

	case ESWAction::Signal:
		Burn += Params.SignalBurn;
		// Broadcast is performed once per decision by the manager (see StepWorld).
		break;

	default:
		break;
	}

	Energy -= Burn * Dt;
	Energy = FMath::Min(Energy, Params.MaxEnergy);
}

void ASWAgent::PlaceAt(FVector Loc, const FVector& Facing)
{
	Loc.Z = Manager->GetGroundZ(Loc.X, Loc.Y) + Params.GroundOffset;
	SetActorLocation(Loc);
	FVector F = Facing; F.Z = 0.f;
	if (!F.IsNearlyZero()) SetActorRotation(F.Rotation());
	bMovedThisStep = true;
}

void ASWAgent::SnapToGround()
{
	if (!Manager) return;
	FVector Loc = GetActorLocation();
	Loc.Z = Manager->GetGroundZ(Loc.X, Loc.Y) + Params.GroundOffset;
	SetActorLocation(Loc);
}

void ASWAgent::MoveToward(const FVector& Target, float Dt)
{
	FVector Dir = Target - GetActorLocation();
	Dir.Z = 0.f;
	const float Dist = Dir.Size();
	if (Dist < 1.f) return;
	Dir /= Dist;
	const float StepLen = FMath::Min(Params.MoveSpeed * Dt, Dist);
	FVector Loc = GetActorLocation() + Dir * StepLen;
	Manager->ClampToArena(Loc);
	PlaceAt(Loc, Dir);
}

void ASWAgent::MoveAlong(const FVector& Dir, float Dt)
{
	FVector Loc = GetActorLocation() + Dir * Params.MoveSpeed * Dt;
	const bool bClamped = Manager->ClampToArena(Loc);
	if (bClamped)
	{
		// Bounce off the arena edge: point back toward the centre with some jitter.
		FVector ToCentre = -Loc;
		ToCentre.Z = 0.f;
		const float Ang = Manager->GetRng().FRandRange(-0.8f, 0.8f);
		ExploreDir = ToCentre.GetSafeNormal().RotateAngleAxis(FMath::RadiansToDegrees(Ang), FVector::UpVector);
	}
	PlaceAt(Loc, Dir);
}

void ASWAgent::UpdateGait(float Dt)
{
	// Body bob + slight pitch while moving; settles when still. Purely visual.
	if (bMovedThisStep)
	{
		GaitPhase += Dt * Params.GaitFrequency * 2.f * PI;
		if (GaitPhase > 2.f * PI) GaitPhase -= 2.f * PI;
	}
	else
	{
		GaitPhase = FMath::FInterpTo(GaitPhase, 0.f, Dt, 4.f);
	}
	const float Bob = Params.GaitAmplitude * FMath::Abs(FMath::Sin(GaitPhase));
	const float Pitch = 2.5f * FMath::Sin(GaitPhase);
	Body->SetRelativeLocation(FVector(0.f, 0.f, Bob));
	Body->SetRelativeRotation(FRotator(Pitch, 0.f, 0.f));
}

void ASWAgent::SetSelected(bool bInSelected)
{
	bSelected = bInSelected;
	UpdateVisual();
}

void ASWAgent::UpdateVisual()
{
	if (!MID || !Manager) return;
	const FSWLookSettings& L = Manager->GetLook();
	// Glow tracks energy; signalling organisms flare; the selected one is brighter.
	const float E = FMath::Clamp(Energy / FMath::Max(Params.MaxEnergy, 1.f), 0.f, 1.f);
	float Strength = L.CreatureGlow * (0.3f + 0.7f * E) * (Species == ESWSpecies::Tecton ? 1.8f : 1.f);
	if (IsSignalling()) Strength *= L.SignalGlowBoost;
	if (bSelected) Strength *= 1.6f;
	MID->SetScalarParameterValue(TEXT("EmissiveStrength"), Strength);
	// BasicShapeMaterial fallback: brightness as colour.
	const FLinearColor Glow = Species == ESWSpecies::Lumen ? L.LumenGlow : L.TectonGlow;
	MID->SetVectorParameterValue(TEXT("Color"), Glow * (0.25f + 0.75f * E));
}
