#include "SWScientistAvatar.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SWProcMesh.h"
#include "SWWorldManager.h"
#include "SymbioticWorld.h"

namespace
{
	// Manny / Quinn from Epic's third-person mannequin content (optional; NOT part of
	// this project). Tools/import_mannequins.py copies it from the engine's template
	// resources; that pack ships only the _Simple meshes, the full ones are tried first.
	// Everything is soft-loaded with a cylinder fallback and a logged warning, so a clone
	// without the pack still runs and still shows the field team.
	const TCHAR* MannequinMesh[2] = {
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny.SKM_Manny"),
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn.SKM_Quinn"),
	};
	const TCHAR* MannequinMeshSimple[2] = {
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"),
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"),
	};
	// Unarmed clips (same SK_Mannequin skeleton as both meshes), played as single-node
	// animations. The pack has no ABP_Manny / ABP_Quinn, and its ABP_Unarmed reads the
	// velocity of a CharacterMovement component this actor does not have.
	const TCHAR* IdleAnimPath = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle");
	const TCHAR* WalkAnimPath = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.MF_Unarmed_Walk_Fwd");
	const TCHAR* JogAnimPath  = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd.MF_Unarmed_Jog_Fwd");
	// Engine shapes: the cylinder fallback body and the jet ski (there is no Capsule in BasicShapes).
	const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* SpherePath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	// Vector parameter of M_Mannequin (set by MI_Manny_01_New / MI_Quinn_01) that colours the body paint.
	const FName PaintTintParam(TEXT("Paint Tint"));

	// Team tints (body paint, jet ski hull and HUD name tag) so the nine scientists read apart at a
	// glance (no species colour: cyan is Lumen's, amber is Tecton's — the team gets its own hues).
	// The first eight are the field roster, in the order the bridge reports them; the ninth is
	// Humboldt, the PI, who holds the camp instead of going out (Lab/duty.py).
	const FColor TeamColors[9] = {
		FColor(240, 240, 240),   // Vesper   - white
		FColor(255, 140, 100),   // Bastion  - coral
		FColor(180, 255, 140),   // Mendel   - leaf
		FColor(200, 160, 255),   // Ada      - violet
		FColor(255, 220, 120),   // Fisher   - straw
		FColor(255, 120, 170),   // Karla    - rose
		FColor(140, 210, 255),   // Archie   - sky
		FColor(160, 255, 230),   // Vega     - mint
		FColor(255, 178, 60),    // Humboldt - ember (the PI at camp)
	};

	constexpr float BodyHeightUu = 180.f;         // Manny at scale 1: a 1.8 m person (1 uu = 1 cm, like the valley)
	constexpr float TagClearanceUu = 30.f;        // HUD tag anchor above the head
	constexpr float JetSkiDeckUu = 14.f;          // the deck (the feet) above the water line while riding
	constexpr float CatchUpSpeedUu = 400.f;       // catch-up speed at 1x when more than a report's travel behind (the Lab walks 260 uu/logical s)
	constexpr float PaceGain = 1.05f;             // walk a little faster than the reported pace so the lag never grows
	constexpr float CreepSpeedUu = 20.f;          // slowest on-screen speed toward the target (closing a residual)
	constexpr float IdleBelowUu = 15.f;           // smoothed pace below which a walking avatar idles ...
	constexpr float WalkAboveUu = 45.f;           // ... and above which an idling one walks (hysteresis: a step per report must not flap)
	constexpr float GaitSmoothing = 2.f;          // FInterpTo speed of the pace estimate (tau 0.5 s = one report period)
	constexpr float GaitHysteresis = 0.15f;       // walk <-> jog switches 15 % either side of the stride midpoint
	// Ground speed of the walk / jog clips at play rate 1 (uu/s, approximate): the clips are root-locked,
	// so their extracted root motion is ~0 and cannot be measured at load.
	constexpr float WalkStrideUu = 170.f;
	constexpr float JogStrideUu = 380.f;
}

ASWScientistAvatar::ASWScientistAvatar()
{
	PrimaryActorTick.bCanEverTick = false;   // the manager drives UpdateVisual from its own Tick

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// The mannequin faces +Y in its asset space; the actor's yaw points +X along the walk.
	Body->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));

	FallbackBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FallbackBody"));
	FallbackBody->SetupAttachment(Root);
	FallbackBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FallbackBody->SetVisibility(false);

	// Stand-up jet ski in actor space (X forward, the feet at the origin): a 2.2 m hull whose deck is
	// the origin and whose keel sits 45 uu under it, a rounded bow, a steering pole hinged at the bow
	// leaning back to a handlebar at chest height. Shapes are assigned in Init (assets load at runtime).
	JetSki = CreateDefaultSubobject<USceneComponent>(TEXT("JetSki"));
	JetSki->SetupAttachment(Root);
	JetSki->SetVisibility(false, true);
	auto MakePart = [this](const TCHAR* Name, UStaticMeshComponent*& Out, const FVector& Loc, const FRotator& Rot, const FVector& Scale)
	{
		Out = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Out->SetupAttachment(JetSki);
		Out->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Out->SetRelativeLocation(Loc);
		Out->SetRelativeRotation(Rot);
		Out->SetRelativeScale3D(Scale);
		Out->SetVisibility(false);
	};
	MakePart(TEXT("JetSkiHull"), JetSkiHull, FVector(0.f, 0.f, -22.f), FRotator::ZeroRotator, FVector(2.2f, 0.8f, 0.45f));
	MakePart(TEXT("JetSkiBow"), JetSkiBow, FVector(110.f, 0.f, -22.f), FRotator::ZeroRotator, FVector(0.9f, 0.8f, 0.45f));
	// Pole from the bow (100, 0, 0) to the bar (40, 0, 110): 125 uu long, leaning 29 degrees toward the rider.
	MakePart(TEXT("JetSkiPole"), JetSkiPole, FVector(70.f, 0.f, 55.f), FRotator(29.f, 0.f, 0.f), FVector(0.08f, 0.08f, 1.25f));
	MakePart(TEXT("JetSkiBar"), JetSkiBar, FVector(40.f, 0.f, 112.f), FRotator(0.f, 0.f, 90.f), FVector(0.06f, 0.06f, 0.6f));

	SetActorEnableCollision(false);   // nothing in the world can bump into a scientist
}

void ASWScientistAvatar::Init(ASWWorldManager* InManager, const FString& InName, int32 InIndex)
{
	Manager = InManager;
	ScientistName = InName;
	TagColor = FLinearColor(TeamColors[InIndex % 9]);

	const int32 Variant = InIndex % 2;   // even = Manny, odd = Quinn
	const uint32 Quiet = LOAD_NoWarn | LOAD_Quiet;   // misses are reported below in plain words
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MannequinMesh[Variant], nullptr, Quiet);
	if (!Mesh) Mesh = LoadObject<USkeletalMesh>(nullptr, MannequinMeshSimple[Variant], nullptr, Quiet);
	if (Mesh)
	{
		bHasMannequin = true;
		Body->SetSkeletalMesh(Mesh);
		// Paint in the scientist's colour: the stock grey vanishes into the dusk terrain at demo distance.
		for (int32 Slot = 0; Slot < Body->GetNumMaterials(); ++Slot)
		{
			if (UMaterialInstanceDynamic* MID = Body->CreateDynamicMaterialInstance(Slot))
			{
				MID->SetVectorParameterValue(PaintTintParam, TagColor);
			}
		}
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		IdleAnim = LoadObject<UAnimSequence>(nullptr, IdleAnimPath, nullptr, Quiet);
		WalkAnim = LoadObject<UAnimSequence>(nullptr, WalkAnimPath, nullptr, Quiet);
		JogAnim  = LoadObject<UAnimSequence>(nullptr, JogAnimPath,  nullptr, Quiet);
		if (!IdleAnim || !WalkAnim || !JogAnim)
		{
			UE_LOG(LogSymbioticWorld, Warning,
				TEXT("Field team: %s is missing animation clips (idle %s, walk %s, jog %s)%s; run: python Tools/import_mannequins.py"),
				*InName, IdleAnim ? TEXT("ok") : TEXT("MISSING"), WalkAnim ? TEXT("ok") : TEXT("MISSING"), JogAnim ? TEXT("ok") : TEXT("MISSING"),
				(!IdleAnim && !WalkAnim && !JogAnim) ? TEXT(", the body stays in its reference pose") : TEXT(""));
		}
		else if (InIndex == 0)
		{
			UE_LOG(LogSymbioticWorld, Log, TEXT("Field team: mannequin idle / walk / jog clips loaded (%.2f / %.2f / %.2f s), %d material slots tinted"),
				IdleAnim->GetPlayLength(), WalkAnim->GetPlayLength(), JogAnim->GetPlayLength(), Body->GetNumMaterials());
		}
		SetGait(IdleAnim ? IdleAnim : WalkAnim, 1.f);
	}
	else
	{
		// No mannequin content: an engine cylinder stands in (procedural-only clones).
		Body->SetVisibility(false);
		if (UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, CylinderPath, nullptr, Quiet))
		{
			FallbackBody->SetStaticMesh(Cylinder);
			FallbackBody->SetVisibility(true);
			FallbackBody->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
			FallbackBody->SetRelativeScale3D(FVector(0.8f, 0.8f, 1.8f));
		}
		UE_LOG(LogSymbioticWorld, Warning,
			TEXT("Field team: mannequin meshes missing, %s is a plain cylinder; run: python Tools/import_mannequins.py"), *InName);
	}

	SetupJetSki();
}

void ASWScientistAvatar::SetupJetSki()
{
	const uint32 Quiet = LOAD_NoWarn | LOAD_Quiet;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, CubePath, nullptr, Quiet);
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, SpherePath, nullptr, Quiet);
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, CylinderPath, nullptr, Quiet);
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, ShapeMaterialPath, nullptr, Quiet);
	if (!Cube || !Sphere || !Cylinder)
	{
		UE_LOG(LogSymbioticWorld, Warning, TEXT("Field team: engine BasicShapes missing, %s has no jet ski (walks through water)"), *ScientistName);
		return;
	}
	JetSkiHull->SetStaticMesh(Cube);
	JetSkiBow->SetStaticMesh(Sphere);
	JetSkiPole->SetStaticMesh(Cylinder);
	JetSkiBar->SetStaticMesh(Cylinder);
	// Hull and bow in the scientist's colour, pole and bar dark (BasicShapeMaterial's "Color").
	const FLinearColor Dark(0.08f, 0.08f, 0.09f);
	UStaticMeshComponent* Parts[4] = { JetSkiHull, JetSkiBow, JetSkiPole, JetSkiBar };
	for (int32 i = 0; i < 4; ++i)
	{
		if (Base)
		{
			if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this))
			{
				MID->SetVectorParameterValue(TEXT("Color"), i < 2 ? TagColor : Dark);
				Parts[i]->SetMaterial(0, MID);
			}
		}
		Parts[i]->SetVisibility(false);
	}
}

FVector ASWScientistAvatar::GetTagAnchor() const
{
	return GetActorLocation() + FVector(0.f, 0.f, BodyHeightUu + TagClearanceUu);
}

void ASWScientistAvatar::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bHasMannequin)
	{
		UE_LOG(LogSymbioticWorld, Log, TEXT("Field team: %s retired at sim %.0f s after %d gait changes, %d water crossings"),
			*ScientistName, LastReportSim, GaitChanges, WaterCrossings);
	}
	Super::EndPlay(Reason);
}

void ASWScientistAvatar::SetGait(UAnimSequence* Anim, float PlayRate)
{
	if (!Anim) return;
	if (Anim != CurrentAnim)
	{
		Body->PlayAnimation(Anim, true);
		CurrentAnim = Anim;
		++GaitChanges;
	}
	Body->SetPlayRate(PlayRate);
}

void ASWScientistAvatar::SetTargetXY(float X, float Y)
{
	const FVector2D New(X, Y);
	if (bHasTarget && Manager)
	{
		// The pace the Lab implies, from the last two reports (0.5 sim-s apart). The gait and the walking
		// speed follow this, not the on-screen catch-up, which would otherwise be a burst and a stop per report.
		const float Dt = FMath::Max(Manager->GetSimTime() - LastReportSim, 0.1f);
		ReportedPace = FMath::Min(FVector2D::Distance(New, TargetXY) / Dt, 1000.f);
	}
	TargetXY = New;
	if (Manager) LastReportSim = Manager->GetSimTime();
	if (!bHasTarget && Manager)
	{
		// First ping: appear there, no cross-map slide.
		SetActorLocation(FVector(X, Y, Manager->GetGroundZ(X, Y)));
		bHasTarget = true;
	}
}

void ASWScientistAvatar::UpdateVisual(float DeltaSeconds)
{
	if (!Manager || !bHasTarget) return;

	const FVector Loc = GetActorLocation();
	const FVector2D Now2D(Loc.X, Loc.Y);
	// Move at the reported pace (on screen: pace x time scale, so 10x / 50x fast-forward keeps up), which
	// makes the motion continuous between reports; more than a report's travel behind (a late report,
	// the team reappearing after a stale spell) the catch-up speed closes the gap.
	const float TS = FMath::Max(1.f, Manager->GetTimeScale());
	const float Pace = ReportedPace * TS;
	const float Behind = FVector2D::Distance(Now2D, TargetXY);
	float Speed = FMath::Max(Pace * PaceGain, CreepSpeedUu * TS);
	if (Behind > FMath::Max(0.75f * Pace, 150.f * TS)) Speed = FMath::Max(Speed, CatchUpSpeedUu * TS);
	const FVector2D Next2D = FMath::Vector2DInterpConstantTo(Now2D, TargetXY, DeltaSeconds, Speed);

	// Over water the body rides the jet ski on the visible water line: the on_land percept's terrain test,
	// but against the drought-lowered surface the environment draws, so in a drought the ski neither floats
	// above the water nor rides over the exposed bed. On land it stands on the terrain.
	const FSWLookSettings& L = Manager->GetLook();
	const float Surface = L.WaterLevel - Manager->GetDroughtWaterDrop();
	const bool bWater = !(SWProc::TerrainHeight(L, Next2D.X, Next2D.Y) > Surface);
	if (bWater != bOnWater)
	{
		bOnWater = bWater;
		JetSki->SetVisibility(bWater, true);
		if (bWater) ++WaterCrossings;
	}
	const FVector Next(Next2D.X, Next2D.Y, bWater ? Surface + JetSkiDeckUu : Manager->GetGroundZ(Next2D.X, Next2D.Y));

	const FVector2D Delta = Next2D - Now2D;
	if (!Delta.IsNearlyZero(0.1f))
	{
		FRotator Face = GetActorRotation();
		Face.Yaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
		SetActorRotation(Face);
	}
	SetActorLocation(Next);

	// Gait from the reported pace, smoothed over a report period: the pace only changes when a report
	// lands, so the clip is not restarted by the frame-to-frame motion; the play rate matches the stride.
	// Riding, the body stands (idle clip) whatever the pace.
	if (bHasMannequin)
	{
		SmoothedPace = FMath::FInterpTo(SmoothedPace, bOnWater ? 0.f : Pace, DeltaSeconds, GaitSmoothing);   // riding feeds no pace: no slow-motion jog on landing
		const float Midpoint = 0.5f * (WalkStrideUu + JogStrideUu);
		const bool bJog = SmoothedPace > Midpoint * (CurrentAnim == JogAnim ? 1.f - GaitHysteresis : 1.f + GaitHysteresis);
		UAnimSequence* Locomotion = bJog ? (JogAnim ? JogAnim : WalkAnim) : (WalkAnim ? WalkAnim : JogAnim);
		const bool bStanding = CurrentAnim == IdleAnim ? SmoothedPace < WalkAboveUu : SmoothedPace < IdleBelowUu;
		if (bOnWater || bStanding || !Locomotion)
		{
			SetGait(IdleAnim, 1.f);
		}
		else
		{
			const float Stride = Locomotion == JogAnim ? JogStrideUu : WalkStrideUu;
			SetGait(Locomotion, FMath::Clamp(SmoothedPace / Stride, 0.5f, 2.5f));
		}
	}
}
