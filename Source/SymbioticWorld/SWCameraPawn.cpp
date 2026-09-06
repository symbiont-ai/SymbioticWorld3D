#include "SWCameraPawn.h"
#include "SWAgent.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

ASWCameraPawn::ASWCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->bUsePawnControlRotation = false;
	Camera->SetFieldOfView(78.f);

	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
}

void ASWCameraPawn::BeginPlay()
{
	Super::BeginPlay();
	// Start behind the arena (-X), above the floor, looking down the valley axis toward the far sun gap.
	FVector StartLoc(-8600.f, 1200.f, 3000.f);
	FRotator StartRot(-15.f, -8.f, 0.f);
	// -SWCam=x:y:z:pitch:yaw  (':' because FParse::Value stops at ',') for scripted screenshots.
	FString CamSpec;
	if (FParse::Value(FCommandLine::Get(), TEXT("SWCam="), CamSpec))
	{
		TArray<FString> P; CamSpec.ParseIntoArray(P, TEXT(":"), true);
		if (P.Num() >= 5)
		{
			StartLoc = FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
			StartRot = FRotator(FCString::Atof(*P[3]), FCString::Atof(*P[4]), 0.f);
		}
	}
	SetActorLocation(StartLoc);
	SetActorRotation(StartRot);
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetControlRotation(GetActorRotation());
	}
}

void ASWCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PlayerInputComponent->BindAxis("CamForward", this, &ASWCameraPawn::OnForward);
	PlayerInputComponent->BindAxis("CamRight", this, &ASWCameraPawn::OnRight);
	PlayerInputComponent->BindAxis("CamUp", this, &ASWCameraPawn::OnUp);
	PlayerInputComponent->BindAxis("CamZoom", this, &ASWCameraPawn::OnZoom);
	PlayerInputComponent->BindAxis("CamTurn", this, &ASWCameraPawn::OnTurn);
	PlayerInputComponent->BindAxis("CamLookUp", this, &ASWCameraPawn::OnLookUp);
	PlayerInputComponent->BindAction("CamLook", IE_Pressed, this, &ASWCameraPawn::OnLookPressed);
	PlayerInputComponent->BindAction("CamLook", IE_Released, this, &ASWCameraPawn::OnLookReleased);
}

void ASWCameraPawn::SetFollowTarget(ASWAgent* Agent)
{
	FollowTarget = Agent;
}

void ASWCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	FRotator Rot = GetActorRotation();
	if (bLooking)
	{
		Rot.Yaw += LookInput.X * LookSpeed;
		Rot.Pitch = FMath::Clamp(Rot.Pitch + LookInput.Y * LookSpeed, -89.f, 89.f);
		Rot.Roll = 0.f;
		SetActorRotation(Rot);
	}

	// Any manual movement breaks follow mode.
	if (!MoveInput.IsNearlyZero() || FMath::Abs(ZoomInput) > KINDA_SMALL_NUMBER)
	{
		FollowTarget = nullptr;
	}

	if (IsValid(FollowTarget) && FollowTarget->IsAlive())
	{
		// Chase-cam: sit behind/above the agent, smoothly.
		const FVector Target = FollowTarget->GetActorLocation();
		const FVector Desired = Target + FVector(-900.f, 0.f, 650.f);
		SetActorLocation(FMath::VInterpTo(GetActorLocation(), Desired, DeltaSeconds, 3.f));
		const FRotator LookAt = (Target - GetActorLocation()).Rotation();
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), LookAt, DeltaSeconds, 3.f));
	}
	else
	{
		if (FollowTarget && (!IsValid(FollowTarget) || !FollowTarget->IsAlive())) FollowTarget = nullptr;

		const FVector Fwd = Rot.Vector();
		const FVector Right = FRotationMatrix(Rot).GetScaledAxis(EAxis::Y);
		// Speed scales with altitude so high-level navigation is fast and close-ups are precise.
		const float AltScale = FMath::Clamp(GetActorLocation().Z / 1500.f, 0.25f, 4.f);
		FVector Delta = (Fwd * MoveInput.X + Right * MoveInput.Y + FVector::UpVector * MoveInput.Z) * MoveSpeed * AltScale * DeltaSeconds;
		Delta += Fwd * ZoomInput * 600.f * AltScale;
		FVector Loc = GetActorLocation() + Delta;
		Loc.Z = FMath::Clamp(Loc.Z, 80.f, 20000.f);
		SetActorLocation(Loc);
	}
	ZoomInput = 0.f;
}
