#include "SWScientistAvatar.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Animation/AnimInstance.h"
#include "Kismet/GameplayStatics.h"
#include "SWWorldManager.h"

namespace
{
	// Manny / Quinn from the standard mannequin content (present on the host as
	// Content examples). Every path is soft-loaded with a capsule fallback, so a
	// clone without the pack still runs and still shows the field team.
	const TCHAR* MannequinMesh[2] = {
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny.SKM_Manny"),
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn.SKM_Quinn"),
	};
	const TCHAR* MannequinMeshSimple[2] = {
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"),
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"),
	};
	const TCHAR* MannequinAnim[2] = {
		TEXT("/Game/Characters/Mannequins/Animations/ABP_Manny.ABP_Manny_C"),
		TEXT("/Game/Characters/Mannequins/Animations/ABP_Quinn.ABP_Quinn_C"),
	};

	// Label tints so the eight scientists read apart at a glance (no species
	// colour: cyan is Lumen's, amber is Tecton's — the team gets its own hues).
	const FColor LabelColors[8] = {
		FColor(240, 240, 240),   // Vesper  - white
		FColor(255, 140, 100),   // Bastion - coral
		FColor(180, 255, 140),   // Mendel  - leaf
		FColor(200, 160, 255),   // Ada     - violet
		FColor(255, 220, 120),   // Fisher  - straw
		FColor(255, 120, 170),   // Karla   - rose
		FColor(140, 210, 255),   // Archie  - sky
		FColor(160, 255, 230),   // Vega    - mint
	};

	constexpr float LabelHeight = 210.f;   // uu above the feet
	constexpr float WalkSpeedUu = 400.f;   // visual catch-up speed at 1x (the Lab walks 260 uu/logical s)
}

ASWScientistAvatar::ASWScientistAvatar()
{
	PrimaryActorTick.bCanEverTick = false;   // the manager drives UpdateVisual from its own Tick

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	FallbackBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FallbackBody"));
	FallbackBody->SetupAttachment(Root);
	FallbackBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FallbackBody->SetVisibility(false);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Root);
	Label->SetRelativeLocation(FVector(0.f, 0.f, LabelHeight));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(34.f);

	SetActorEnableCollision(false);   // nothing in the world can bump into a scientist
}

void ASWScientistAvatar::Init(ASWWorldManager* InManager, const FString& InName, int32 InIndex)
{
	Manager = InManager;
	ScientistName = InName;

	const int32 Variant = InIndex % 2;   // even = Manny, odd = Quinn
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MannequinMesh[Variant]);
	if (!Mesh) Mesh = LoadObject<USkeletalMesh>(nullptr, MannequinMeshSimple[Variant]);
	if (Mesh)
	{
		Body->SetSkeletalMesh(Mesh);
		if (UClass* AnimClass = LoadClass<UAnimInstance>(nullptr, MannequinAnim[Variant]))
		{
			Body->SetAnimInstanceClass(AnimClass);
		}
		else if (UClass* MannyAnim = LoadClass<UAnimInstance>(nullptr, MannequinAnim[0]))
		{
			Body->SetAnimInstanceClass(MannyAnim);   // Quinn shares Manny's skeleton
		}
	}
	else
	{
		// No mannequin content: an engine capsule stands in (procedural-only clones).
		Body->SetVisibility(false);
		if (UStaticMesh* Capsule = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Capsule.Capsule")))
		{
			FallbackBody->SetStaticMesh(Capsule);
			FallbackBody->SetVisibility(true);
			FallbackBody->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
			FallbackBody->SetRelativeScale3D(FVector(0.8f, 0.8f, 1.8f));
		}
	}

	Label->SetText(FText::FromString(InName));
	Label->SetTextRenderColor(LabelColors[InIndex % 8]);
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

	// The label turns toward the viewer (TextRender does not billboard itself).
	if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0))
	{
		const FVector ToCam = Cam->GetCameraLocation() - Label->GetComponentLocation();
		Label->SetWorldRotation(FRotationMatrix::MakeFromX(ToCam).Rotator());
	}
}
