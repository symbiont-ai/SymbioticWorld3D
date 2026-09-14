#include "SWLeviathan.h"
#include "SWWorldManager.h"
#include "SWAgent.h"
#include "SWProcMesh.h"
#include "SymbioticWorld.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

// Half-height of the trunk in SWProc::BuildLeviathan local space (trunk Z radius 180,
// less the belly flattening). Used to keep the body off the riverbed.
static constexpr float SWLeviathanBellyRadius = 178.f;

ASWLeviathan::ASWLeviathan()
{
	PrimaryActorTick.bCanEverTick = false;

	// The actor root is the simulation's position, set once per logical substep by
	// PlaceAlongChannel. The body hangs off it with an absolute transform and is placed by
	// UpdateVisual once per rendered frame, so the animal glides between substeps like the
	// organisms do without the strike test ever reading an interpolated position.
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	SetRootComponent(Pivot);

	Body = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Pivot);
	Body->SetAbsolute(true, true, false);   // world location/rotation from UpdateVisual; scale stays relative (LeviathanScale)
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
	Heading = 0.f;
	HeadingAlign = 1.f;
	BedFloor = 0.f;
	bHeadingSet = false;

	BuildBody();
	PlaceAlongChannel(0.f);   // Dt = 0: snap the heading and the bed clamp
	// Interpolate from where the animal actually starts, so any frame rendered before the first
	// substep (a paused start, a reset while paused) draws the body on the animal, not part-way to
	// the world origin.
	PrevLocation = GetActorLocation();
	PrevRotation = GetActorQuat();
	UpdateVisual(0.f);        // the body carries an absolute transform, so put it on the animal now
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

void ASWLeviathan::PlaceAlongChannel(float Dt)
{
	if (!Manager) return;
	const FSWRunSettings& S = Manager->GetSettings();
	const FSWLookSettings& L = Manager->GetLook();

	// Where the rendered body interpolates FROM until the next substep (UpdateVisual). Only a real
	// substep may set it: on the placement from Init the actor is still at its spawn transform, and
	// recording that made every rendered frame before the first substep lerp the body toward the
	// world origin -- permanently, if the run was reset or started while paused.
	if (Dt > 0.f)
	{
		PrevLocation = GetActorLocation();
		PrevRotation = GetActorQuat();
	}

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
	// Sampling that bed raw bobbed the body every time the clamp engaged and released between
	// substeps (worst while hunting, which steers up to 0.6 x RiverWidth off the centreline), so the
	// clamp follows the bed at LeviathanBedFollowRate. The breach rise is added AFTER the clamp, so
	// the arc keeps its full height over a shallow bend instead of being flattened by it.
	const float BellyClearance = SWLeviathanBellyRadius * L.LeviathanScale;
	const float BedZ = SWProc::TerrainHeight(L, X, Y) + BellyClearance;
	BedFloor = (Dt > 0.f && bHeadingSet)
		? FMath::FInterpTo(BedFloor, BedZ, Dt, FMath::Max(S.LeviathanBedFollowRate, 0.1f))
		: BedZ;
	const float Z = FMath::Max(SurfaceZ() - S.LeviathanSubmersion, BedFloor) + Rise;
	const FVector Loc(X, Y, Z);

	// Face along travel, following the channel's tangent. Reversing Direction flips this target by
	// ~180 deg, so the heading is eased into it at LeviathanTurnRate instead of snapping. Step reads
	// HeadingAlign (cos of what is left of the error) to slow the animal through the turn, and never
	// steps against Direction, so a reversal is a slow turn near station, not a body sliding tail-first.
	const float Ahead = 200.f * Direction;
	const FVector Fwd = FVector(Ahead, SWProc::RiverCenterY(L, X + Ahead) - SWProc::RiverCenterY(L, X), 0.f).GetSafeNormal();
	const float TargetYaw = Fwd.IsNearlyZero() ? (Direction > 0.f ? 0.f : 180.f) : Fwd.Rotation().Yaw;
	if (Dt > 0.f && bHeadingSet)
	{
		const float MaxTurn = FMath::Max(S.LeviathanTurnRate, 1.f) * Dt;
		Heading = FRotator::NormalizeAxis(Heading + FMath::Clamp(FMath::FindDeltaAngleDegrees(Heading, TargetYaw), -MaxTurn, MaxTurn));
	}
	else
	{
		Heading = TargetYaw;
	}
	bHeadingSet = true;
	HeadingAlign = FMath::Max(FMath::Cos(FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(Heading, TargetYaw))), 0.f);

	FRotator Rot(Pitch, Heading, 6.f * FMath::Cos(WeavePhase * 0.35f + Index * 2.1f));   // bank into the weave

	SetActorLocationAndRotation(Loc, Rot);
}

void ASWLeviathan::UpdateVisual(float InterpolationDt)
{
	if (!Manager || !Body) return;
	// The actor moves once per logical substep (10 Hz at the default LogicalSubstep) while the
	// organisms interpolate their bodies every rendered frame in ASWAgent::UpdateAuthoredVisual;
	// without the same treatment this animal stepped visibly while they glided past it. Visual only:
	// the actor transform, which the strike test and the chase camera read, is untouched.
	const float Dt = FMath::Max(Manager->GetSettings().LogicalSubstep, 0.01f);
	const float Alpha = FMath::Clamp(InterpolationDt / Dt, 0.f, 1.f);
	Body->SetWorldLocationAndRotation(FMath::Lerp(PrevLocation, GetActorLocation(), Alpha),
	                                  FQuat::Slerp(PrevRotation, GetActorQuat(), Alpha).GetNormalized());
}

void ASWLeviathan::Step(float Dt, TArray<ASWAgent*>& OutVictims)
{
	if (!Manager) return;
	const FSWRunSettings& S = Manager->GetSettings();
	const FSWLookSettings& L = Manager->GetLook();

	// ---- 0) Hunt: an organism in the water within LeviathanSenseRadius, provided it is near the
	//         main channel the animal swims in (a tributary is out of reach). Same species filter as
	//         the strike. A target that climbs out, dies or drifts past LeviathanPreyHold x the sense
	//         radius is dropped, and only then is the nearest candidate picked again: re-picking the
	//         nearest every substep let two equidistant organisms flip the target (and the direction)
	//         back and forth at the substep rate. While the strike cooldown runs the animal does not
	//         hunt at all -- it used to circle a target it could not take.
	const float Limit = FMath::Max(S.WorldHalfSize - 250.f, 400.f);
	const FVector Here = GetActorLocation();
	{
		auto Eligible = [&](ASWAgent* A, float MaxD2) -> bool
		{
			if (!IsValid(A) || !A->IsAlive() || OutVictims.Contains(A)) return false;
			if (S.LeviathanTarget == ESWLeviathanTarget::Lumen  && A->GetSpecies() != ESWSpecies::Lumen)  return false;
			if (S.LeviathanTarget == ESWLeviathanTarget::Tecton && A->GetSpecies() != ESWSpecies::Tecton) return false;
			const FVector AL = A->GetActorLocation();
			if (!IsInWater(AL)) return false;
			if (FMath::Abs(AL.Y - SWProc::RiverCenterY(L, AL.X)) > 2.5f * L.RiverWidth) return false;   // not in this channel
			return FVector::DistSquared2D(Here, AL) < MaxD2;
		};
		// While the drought pauses predation the animal cannot take anything, so it does not hunt
		// either -- otherwise it shadows one organism for the whole drought, which is exactly the
		// "keeps patrolling, it just does not take anything" that DESIGN section 4 promises.
		const bool bCooling = StrikeTimer > 0.f || (S.bLeviathanPauseInDrought && Manager->IsDrought());
		const float HoldD2 = FMath::Square(S.LeviathanSenseRadius * FMath::Max(S.LeviathanPreyHold, 1.f));
		if (bCooling || !Eligible(Prey.Get(), HoldD2))
		{
			ASWAgent* Best = nullptr;
			float BestD2 = S.LeviathanSenseRadius * S.LeviathanSenseRadius;
			if (!bCooling)
			{
				for (ASWAgent* A : Manager->GetAgents())
				{
					if (!Eligible(A, BestD2)) continue;
					BestD2 = FVector::DistSquared2D(Here, A->GetActorLocation());
					Best = A;
				}
			}
			Prey = Best;
		}
	}

	// ---- 1) Move along the channel: chase the prey, or patrol with seeded random decisions ----
	float LateralTarget;
	// Travel scales with how well the body is pointing where it is going (HeadingAlign from the last
	// substep), so the animal slows into a turn and picks the speed back up once it is aligned.
	const float Drive = FMath::Max(HeadingAlign, 0.15f);
	if (Prey.IsValid())
	{
		const FVector PL = Prey->GetActorLocation();
		const float Delta = PL.X - TravelX;
		// Commit to a direction only outside a band wider than one substep's travel, and brake into
		// the prey instead of sprinting past it. The old rule -- flip whenever |Delta| > 60 uu, then
		// always move LeviathanChaseSpeed x Dt (150 uu) -- could not settle inside its own deadband,
		// so it oscillated around the target and snapped the yaw end-for-end every substep.
		const float Band = FMath::Max(S.LeviathanChaseBand, 1.5f * S.LeviathanChaseSpeed * Dt);
		if (FMath::Abs(Delta) > Band) Direction = Delta > 0.f ? 1.f : -1.f;
		const float Reach = S.LeviathanChaseSpeed * Dt * Drive;
		float StepX = FMath::Clamp(Delta, -Reach, Reach);
		// Never translate against the facing. The brake alone let the two come apart: once it has
		// converged the error stays inside the band (an organism moves 35 uu per substep against a
		// 250 uu band), so Direction -- the only input to the yaw -- froze at whatever it was on
		// acquisition while TravelX kept copying the prey's X, including when the prey turned round.
		// The animal then swam backwards at walking pace with its nose locked forward. Holding
		// station instead lets |Delta| grow at the organism's own pace until it leaves the band,
		// Direction commits, and the turn plays out at LeviathanTurnRate.
		if (StepX * Direction < 0.f) StepX = 0.f;
		TravelX += StepX;
		// The reachable lateral span must cover the water, or an organism wading beyond it becomes a
		// target the animal can hold but never strike: the water reaches ~1.8 x RiverWidth each side
		// and the jaw needs LeviathanStrikeRadius, so 0.6 x left a third of the channel unstrikeable.
		const float LateralReach = FMath::Max(1.2f * L.RiverWidth, 0.6f * L.RiverWidth);
		LateralTarget = FMath::Clamp(PL.Y - SWProc::RiverCenterY(L, PL.X), -LateralReach, LateralReach);
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
		TravelX += Direction * S.LeviathanSpeed * SpeedScale * Dt * Drive;
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

	PlaceAlongChannel(Dt);

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
