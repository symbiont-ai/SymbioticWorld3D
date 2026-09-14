#include "SWLeviathan.h"
#include "SWWorldManager.h"
#include "SWAgent.h"
#include "SWProcMesh.h"
#include "SymbioticWorld.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

// Half-height of the trunk in SWProc::BuildLeviathan local space (trunk Z radius 180,
// less the belly flattening). Used to keep the body off the riverbed.
static constexpr float SWLeviathanBellyRadius = 178.f;

ASWLeviathan::ASWLeviathan()
{
	PrimaryActorTick.bCanEverTick = false;

	Body = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	// No collision at all: organisms are not physical either, and the cursor must
	// keep picking the organism behind it rather than the whale.
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(true);
	Body->bUseAsyncCooking = true;
}

void ASWLeviathan::Init(ASWWorldManager* InManager, int32 InIndex)
{
	Manager = InManager;
	if (!Manager) return;
	Index = InIndex;
	Kills = 0;
	StrikeTimer = 0.f;
	BreachPhase = 0.f;
	bWasSurfacing = false;

	const FSWRunSettings& S = Manager->GetSettings();
	const int32 N = FMath::Max(S.LeviathanCount, 1);
	const float Limit = FMath::Max(S.WorldHalfSize - 250.f, 400.f);

	// Spread the animals evenly down the channel and alternate their direction, so
	// two of them do not shadow each other along the same stretch of river.
	TravelX = -Limit + 2.f * Limit * (Index + 0.5f) / N;
	Direction = (Index % 2 == 0) ? 1.f : -1.f;
	// Stagger the surfacing rhythm so the breaches do not synchronise.
	CruiseTimer = S.LeviathanSurfaceInterval * (Index + 1) / (N + 1);
	WeavePhase = Index * 3.7f;
	Lateral = 0.f;
	DecisionTimer = S.LeviathanTurnInterval * (Index + 1) / (N + 1);   // staggered, no draw at Init
	SpeedScale = 1.f;
	Prey = nullptr;

	BuildBody();
	PlaceAlongChannel();
}

void ASWLeviathan::BuildBody()
{
	if (!Manager) return;
	const FSWLookSettings& L = Manager->GetLook();
	// Visual variation uses its own stream so it never perturbs the simulation RNG.
	FRandomStream VisRng(L.LookSeed * 977 + Index * 131 + 6011);

	SWProc::FMeshData M;
	SWProc::BuildLeviathan(VisRng, M);
	Body->CreateMeshSection_LinearColor(0, M.Verts, M.Tris, M.Normals, M.UV0, M.Colors, TArray<FProcMeshTangent>(), false);
	Body->SetRelativeScale3D(FVector(L.LeviathanScale));
	Body->SetCastShadow(L.bCreatureShadows);

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SW_Creature.M_SW_Creature"));
	if (!Base) Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Base)
	{
		MID = UMaterialInstanceDynamic::Create(Base, this);
		MID->SetVectorParameterValue(TEXT("BodyColor"), L.LeviathanBody);
		MID->SetVectorParameterValue(TEXT("EmissiveColor"), L.LeviathanGlow);
		MID->SetVectorParameterValue(TEXT("Color"), L.LeviathanGlow);   // BasicShapeMaterial fallback
		MID->SetScalarParameterValue(TEXT("PulseSpeed"), 0.45f);        // slower than either organism: a big, cold animal
		MID->SetScalarParameterValue(TEXT("CrackMode"), 0.f);           // vertex markings as painted, like Lumen
		MID->SetScalarParameterValue(TEXT("CrackScale"), 0.05f);
		MID->SetScalarParameterValue(TEXT("CrackWidth"), 0.10f);
		MID->SetScalarParameterValue(TEXT("Roughness"), 0.28f);         // wet skin
		MID->SetScalarParameterValue(TEXT("EmissiveStrength"), L.CreatureGlow * L.LeviathanGlowScale);
		Body->SetMaterial(0, MID);
	}
}

float ASWLeviathan::SurfaceZ() const
{
	if (!Manager) return 0.f;
	const FSWLookSettings& L = Manager->GetLook();
	return L.WaterLevel - Manager->GetDroughtWaterDrop();
}

bool ASWLeviathan::IsInWater(const FVector& Loc) const
{
	if (!Manager) return false;
	const FSWLookSettings& L = Manager->GetLook();
	// Same test as FSWPercept::bOnLand in ASWWorldManager::BuildPercept, negated.
	// With LeviathanWaterMargin = 0 the danger zone is EXACTLY { on_land == false },
	// which is what makes this predator learnable through the existing percept.
	return SWProc::TerrainHeight(L, Loc.X, Loc.Y) <= L.WaterLevel + Manager->GetSettings().LeviathanWaterMargin;
}

void ASWLeviathan::PlaceAlongChannel()
{
	if (!Manager) return;
	const FSWRunSettings& S = Manager->GetSettings();
	const FSWLookSettings& L = Manager->GetLook();

	// Lateral: the gentle weave while patrolling, steering toward the prey while hunting (Step eases it).
	const float X = TravelX;
	const float Y = SWProc::RiverCenterY(L, X) + Lateral;

	// Depth: cruising low in the channel, arcing up during a breach.
	float Rise = 0.f;
	float Pitch = 0.f;
	if (BreachPhase > 0.f && S.LeviathanSurfaceDuration > 0.f)
	{
		// U runs 1 -> 0 across the breach; sin(pi U) is a smooth up-and-back-down arc.
		const float U = FMath::Clamp(BreachPhase / S.LeviathanSurfaceDuration, 0.f, 1.f);
		Rise = FMath::Sin(U * PI) * S.LeviathanBreachRise;
		Pitch = -13.f * FMath::Sin(2.f * PI * U);   // level at both ends and at the apex, so the jaw and flukes never pitch into the bed while the body sits on the clamp
	}

	// Since 2026-09-11 the main channel bed sits ~440 uu under the surface (Look.RiverDepth 400 plus
	// the bank term, +-70 floor noise). The belly clamp below (178 x LeviathanScale above the bed)
	// floors the spine near -244, above the LeviathanSubmersion target, so the 2x animal cruises awash
	// with its back and dorsal fin proud and comes fully clear on a breach; the clamp also lifts it
	// over the shallower bends instead of burying it. Organisms never sink with the bed:
	// SWProc::GroundZ floors them 8 uu under the surface, so they wade across.
	const float BellyClearance = SWLeviathanBellyRadius * L.LeviathanScale;
	float Z = SurfaceZ() - S.LeviathanSubmersion + Rise;
	Z = FMath::Max(Z, SWProc::TerrainHeight(L, X, Y) + BellyClearance);
	const FVector Loc(X, Y, Z);

	// Face along travel, following the channel's tangent.
	const float Ahead = 200.f * Direction;
	const FVector Fwd = FVector(Ahead, SWProc::RiverCenterY(L, X + Ahead) - SWProc::RiverCenterY(L, X), 0.f).GetSafeNormal();
	FRotator Rot = Fwd.IsNearlyZero() ? FRotator(0.f, Direction > 0.f ? 0.f : 180.f, 0.f) : Fwd.Rotation();
	Rot.Pitch = Pitch;
	Rot.Roll = 6.f * FMath::Cos(WeavePhase * 0.35f + Index * 2.1f);   // bank into the weave

	SetActorLocationAndRotation(Loc, Rot);
}

void ASWLeviathan::Step(float Dt, TArray<ASWAgent*>& OutVictims)
{
	if (!Manager) return;
	const FSWRunSettings& S = Manager->GetSettings();
	const FSWLookSettings& L = Manager->GetLook();

	// ---- 0) Hunt: the nearest organism in the water within LeviathanSenseRadius, provided it is
	//         near the main channel the animal swims in (a tributary is out of reach). Same species
	//         filter as the strike. The prey is re-evaluated every substep, so a target that climbs
	//         out is dropped at once.
	const float Limit = FMath::Max(S.WorldHalfSize - 250.f, 400.f);
	const FVector Here = GetActorLocation();
	{
		float BestD2 = S.LeviathanSenseRadius * S.LeviathanSenseRadius;
		ASWAgent* Best = nullptr;
		for (ASWAgent* A : Manager->GetAgents())
		{
			if (!IsValid(A) || !A->IsAlive() || OutVictims.Contains(A)) continue;
			if (S.LeviathanTarget == ESWLeviathanTarget::Lumen  && A->GetSpecies() != ESWSpecies::Lumen)  continue;
			if (S.LeviathanTarget == ESWLeviathanTarget::Tecton && A->GetSpecies() != ESWSpecies::Tecton) continue;
			const FVector AL = A->GetActorLocation();
			if (!IsInWater(AL)) continue;
			if (FMath::Abs(AL.Y - SWProc::RiverCenterY(L, AL.X)) > 2.5f * L.RiverWidth) continue;   // not in this channel
			const float D2 = FVector::DistSquared2D(Here, AL);
			if (D2 < BestD2) { BestD2 = D2; Best = A; }
		}
		Prey = Best;
	}

	// ---- 1) Move along the channel: chase the prey, or patrol with seeded random decisions ----
	float LateralTarget;
	if (Prey.IsValid())
	{
		const FVector PL = Prey->GetActorLocation();
		if (FMath::Abs(PL.X - TravelX) > 60.f) Direction = PL.X > TravelX ? 1.f : -1.f;
		TravelX += Direction * S.LeviathanChaseSpeed * Dt;
		LateralTarget = FMath::Clamp(PL.Y - SWProc::RiverCenterY(L, PL.X), -0.6f * L.RiverWidth, 0.6f * L.RiverWidth);
	}
	else
	{
		DecisionTimer -= Dt;
		if (DecisionTimer <= 0.f)
		{
			// Seeded draws in substep order (the manager's stream), so the patrol is reproducible.
			FRandomStream& Rng = Manager->GetRng();
			DecisionTimer = FMath::Max(S.LeviathanTurnInterval, 1.f) * Rng.FRandRange(0.5f, 1.5f);
			if (Rng.FRand() < S.LeviathanTurnChance) Direction = -Direction;
			SpeedScale = (Rng.FRand() < S.LeviathanLoiterChance) ? 0.25f : Rng.FRandRange(0.8f, 1.2f);
		}
		TravelX += Direction * S.LeviathanSpeed * SpeedScale * Dt;
		LateralTarget = 0.22f * L.RiverWidth * FMath::Sin(WeavePhase * 0.35f + Index * 2.1f);
	}
	if (TravelX > Limit)  { TravelX = Limit;  Direction = -1.f; }
	if (TravelX < -Limit) { TravelX = -Limit; Direction =  1.f; }
	Lateral = FMath::FInterpTo(Lateral, LateralTarget, Dt, Prey.IsValid() ? 2.5f : 1.f);
	WeavePhase += Dt;

	// ---- 2) Surfacing rhythm ----
	if (BreachPhase > 0.f)
	{
		BreachPhase = FMath::Max(BreachPhase - Dt, 0.f);
	}
	else
	{
		CruiseTimer += Dt;
		if (CruiseTimer >= S.LeviathanSurfaceInterval)
		{
			CruiseTimer = 0.f;
			BreachPhase = S.LeviathanSurfaceDuration;
		}
	}
	if (StrikeTimer > 0.f) StrikeTimer = FMath::Max(StrikeTimer - Dt, 0.f);

	PlaceAlongChannel();

	// ---- 3) Strike: the nearest organism that is IN THE WATER and in range ----
	// The cooldown is what keeps this a pressure rather than an extinction event:
	// one animal removes at most one organism per LeviathanStrikeCooldown seconds.
	// Predation pauses during a drought so the two perturbations never overlap and the
	// drought's own effect stays readable: the animal keeps patrolling, it just does
	// not take anything.
	const bool bPausedByDrought = S.bLeviathanPauseInDrought && Manager->IsDrought();
	if (StrikeTimer <= 0.f && !bPausedByDrought)
	{
		const FVector Jaw = GetActorLocation();   // after PlaceAlongChannel: where the animal is now
		float BestD2 = S.LeviathanStrikeRadius * S.LeviathanStrikeRadius;
		ASWAgent* Best = nullptr;
		for (ASWAgent* A : Manager->GetAgents())
		{
			if (!IsValid(A) || !A->IsAlive()) continue;
			if (OutVictims.Contains(A)) continue;          // another leviathan already claimed it this substep
			// Optional single-species filter. Reads GetSpecies() only; adds no species.
			if (S.LeviathanTarget == ESWLeviathanTarget::Lumen  && A->GetSpecies() != ESWSpecies::Lumen)  continue;
			if (S.LeviathanTarget == ESWLeviathanTarget::Tecton && A->GetSpecies() != ESWSpecies::Tecton) continue;
			const FVector AL = A->GetActorLocation();
			if (!IsInWater(AL)) continue;                  // dry ground is safe, and the organism can sense that
			const float D2 = FVector::DistSquared2D(Jaw, AL);
			if (D2 < BestD2) { BestD2 = D2; Best = A; }
		}
		if (Best)
		{
			OutVictims.Add(Best);
			Prey = nullptr;
			Kills++;
			StrikeTimer = S.LeviathanStrikeCooldown;
			// Lunge: a kill always surfaces the animal, so a predation event is visible.
			BreachPhase = FMath::Max(BreachPhase, S.LeviathanSurfaceDuration);
			CruiseTimer = 0.f;
			UE_LOG(LogSymbioticWorld, Verbose, TEXT("Leviathan %d took %s at t=%.1f"),
				Index, *Best->GetLabel(), Manager->GetSimTime());
		}
	}

	// ---- 4) Visual: brighter while it is at the surface. Only on a change, because
	//         Step runs once per logical substep (up to MaxSubstepsPerFrame times a frame).
	const bool bSurfacing = BreachPhase > 0.f;
	if (MID && bSurfacing != bWasSurfacing)
	{
		MID->SetScalarParameterValue(TEXT("EmissiveStrength"),
			L.CreatureGlow * L.LeviathanGlowScale * (bSurfacing ? 2.2f : 1.f));
		bWasSurfacing = bSurfacing;
	}
}
