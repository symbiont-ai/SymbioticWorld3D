#include "SWScientistAvatar.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
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
	// The engine ships no /Engine/BasicShapes/Capsule; the cylinder is what exists.
	const TCHAR* FallbackMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	// Vector parameter of M_Mannequin (set by MI_Manny_01_New / MI_Quinn_01) that colours the body paint.
	const FName PaintTintParam(TEXT("Paint Tint"));

	// Team tints (body paint and HUD name tag) so the eight scientists read apart at a
	// glance (no species colour: cyan is Lumen's, amber is Tecton's — the team gets its own hues).
	const FColor TeamColors[8] = {
		FColor(240, 240, 240),   // Vesper  - white
		FColor(255, 140, 100),   // Bastion - coral
		FColor(180, 255, 140),   // Mendel  - leaf
		FColor(200, 160, 255),   // Ada     - violet
		FColor(255, 220, 120),   // Fisher  - straw
		FColor(255, 120, 170),   // Karla   - rose
		FColor(140, 210, 255),   // Archie  - sky
		FColor(160, 255, 230),   // Vega    - mint
	};

	constexpr float BodyHeightUu = 180.f;         // Manny at scale 1: a 1.8 m person (1 uu = 1 cm, like the valley)
	constexpr float TagClearanceUu = 30.f;        // HUD tag anchor above the head
	constexpr float WalkSpeedUu = 400.f;          // visual catch-up speed at 1x (the Lab walks 260 uu/logical s)
	constexpr float StandingSpeedUu = 20.f;       // smoothed on-screen speed below which the avatar idles
	constexpr float GaitSmoothing = 4.f;          // FInterpTo speed of the gait's speed estimate (reports land every 0.5 sim-s)
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

	SetActorEnableCollision(false);   // nothing in the world can bump into a scientist
}

void ASWScientistAvatar::Init(ASWWorldManager* InManager, const FString& InName, int32 InIndex)
{
	Manager = InManager;
	ScientistName = InName;
	TagColor = FLinearColor(TeamColors[InIndex % 8]);

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
		if (UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, FallbackMeshPath, nullptr, Quiet))
		{
			FallbackBody->SetStaticMesh(Cylinder);
			FallbackBody->SetVisibility(true);
			FallbackBody->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
			FallbackBody->SetRelativeScale3D(FVector(0.8f, 0.8f, 1.8f));
		}
		UE_LOG(LogSymbioticWorld, Warning,
			TEXT("Field team: mannequin meshes missing, %s is a plain cylinder; run: python Tools/import_mannequins.py"), *InName);
	}
}

FVector ASWScientistAvatar::GetTagAnchor() const
{
	return GetActorLocation() + FVector(0.f, 0.f, BodyHeightUu + TagClearanceUu);
}

void ASWScientistAvatar::SetGait(UAnimSequence* Anim, float PlayRate)
{
	if (!Anim) return;
	if (Anim != CurrentAnim)
	{
		Body->PlayAnimation(Anim, true);
		CurrentAnim = Anim;
	}
	Body->SetPlayRate(PlayRate);
}

void ASWScientistAvatar::SetTargetXY(float X, float Y)
{
	TargetXY = FVector2D(X, Y);
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
	// Catch-up speed follows the time scale so 10x / 50x fast-forward does not
	// leave the avatars crawling behind their reported positions.
	const float Speed = WalkSpeedUu * FMath::Max(1.f, Manager->GetTimeScale());
	const FVector2D Now2D(Loc.X, Loc.Y);
	const FVector2D Next2D = FMath::Vector2DInterpConstantTo(Now2D, TargetXY, DeltaSeconds, Speed);
	const FVector Next(Next2D.X, Next2D.Y, Manager->GetGroundZ(Next2D.X, Next2D.Y));

	const FVector2D Delta = Next2D - Now2D;
	if (!Delta.IsNearlyZero(0.1f))
	{
		FRotator Face = GetActorRotation();
		Face.Yaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
		SetActorRotation(Face);
	}
	SetActorLocation(Next);

	// Gait from the avatar's own on-screen speed, smoothed so the stop-and-go catch-up between
	// reports does not restart a clip every frame; the play rate matches the clip's stride.
	if (bHasMannequin)
	{
		const float MovedPerSec = DeltaSeconds > KINDA_SMALL_NUMBER ? Delta.Size() / DeltaSeconds : 0.f;
		SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, MovedPerSec, DeltaSeconds, GaitSmoothing);
		const float Midpoint = 0.5f * (WalkStrideUu + JogStrideUu);
		const bool bJog = SmoothedSpeed > Midpoint * (CurrentAnim == JogAnim ? 1.f - GaitHysteresis : 1.f + GaitHysteresis);
		UAnimSequence* Locomotion = bJog ? (JogAnim ? JogAnim : WalkAnim) : (WalkAnim ? WalkAnim : JogAnim);
		if (SmoothedSpeed < StandingSpeedUu || !Locomotion)
		{
			SetGait(IdleAnim, 1.f);
		}
		else
		{
			const float Stride = Locomotion == JogAnim ? JogStrideUu : WalkStrideUu;
			SetGait(Locomotion, FMath::Clamp(SmoothedSpeed / Stride, 0.5f, 2.5f));
		}
	}
}
