#include "SWCameraPawn.h"
#include "SWScientistAvatar.h"
#include "SWAgent.h"
#include "SWWorldManager.h"
#include "SWLeviathan.h"
#include "SymbioticWorld.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "EngineUtils.h"

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
	// -SWFollowSpecies=Lumen|Tecton: creature inspection camera (docs/CREATURE_RENDERING.md).
	FString FollowSpec;
	if (FParse::Value(FCommandLine::Get(), TEXT("SWFollowSpecies="), FollowSpec) && !FollowSpec.IsEmpty())
	{
		for (const ESWSpecies S : { ESWSpecies::Lumen, ESWSpecies::Tecton })
		{
			if (FollowSpec.Equals(SWSpeciesName(S), ESearchCase::IgnoreCase)) RequestedFollowSpecies = S;
		}
		bRequestedLeviathan = FollowSpec.Equals(TEXT("Leviathan"), ESearchCase::IgnoreCase);
		if (!RequestedFollowSpecies.IsSet() && !bRequestedLeviathan)
		{
			UE_LOG(LogSymbioticWorld, Warning, TEXT("-SWFollowSpecies=%s ignored: use Lumen, Tecton or Leviathan"), *FollowSpec);
		}
	}
	// -SWCam=x:y:z:pitch:yaw  (':' because FParse::Value stops at ',') for scripted screenshots: placed now.
	// -SWFollowScientist=<Name>|any: chase-cam on a field-team avatar once it joins (docs/POLICY_API.md §5).
	FParse::Value(FCommandLine::Get(), TEXT("SWFollowScientist="), RequestedScientist);
	RequestedScientist.TrimStartAndEndInline();

	// Otherwise the arena-relative start framing waits for the first Tick, when the manager has parsed
	// -SWSet (its BeginPlay may run after this one).
	FString CamSpec;
	if (FParse::Value(FCommandLine::Get(), TEXT("SWCam="), CamSpec))
	{
		TArray<FString> P; CamSpec.ParseIntoArray(P, TEXT(":"), true);
		if (P.Num() >= 5)
		{
			PlaceCamera(FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2])),
			            FRotator(FCString::Atof(*P[3]), FCString::Atof(*P[4]), 0.f));
		}
	}
}

void ASWCameraPawn::PlaceCamera(const FVector& Loc, const FRotator& Rot)
{
	bStartPlaced = true;
	SetActorLocation(Loc);
	SetActorRotation(Rot);
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetControlRotation(GetActorRotation());
	}
}

void ASWCameraPawn::PlaceStartCamera()
{
	// Start behind the arena (-X), above the floor, looking down the valley axis toward the far sun gap.
	// Framed relative to the arena (the arches are placed relative to it too), so a bigger world keeps
	// the composition: 4500 is the hack-build arena the numbers were tuned on.
	const ASWWorldManager* M = ASWWorldManager::Get(GetWorld());
	const FSWRunSettings Defaults;
	const float ArenaX = M ? M->GetSettings().WorldHalfSize : Defaults.WorldHalfSize;
	const float ArenaY = M ? SWArenaHalfY(M->GetSettings()) : SWArenaHalfY(Defaults);
	const float Grow = FMath::Max(ArenaX / 4500.f, 0.5f);
	PlaceCamera(FVector(-(ArenaX + 4100.f), 1200.f * ArenaY / 4500.f, 3000.f * (0.5f + 0.5f * Grow)), FRotator(-15.f, -8.f, 0.f));
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
	// Any viewer-initiated follow change (F key, a click, a mode change) ends the scripted request:
	// releasing clears everything, following an organism replaces a predator follow.
	FollowActor = nullptr;
	FollowScientist = nullptr;
	bRequestedLeviathan = false;
	RequestedScientist.Empty();
	if (!Agent) RequestedFollowSpecies.Reset();
}

void ASWCameraPawn::SetFollowScientist(ASWScientistAvatar* Avatar)
{
	// A viewer-chosen scientist replaces any organism / predator follow and ends every scripted request.
	FollowTarget = nullptr;
	FollowActor = nullptr;
	bRequestedLeviathan = false;
	RequestedFollowSpecies.Reset();
	RequestedScientist.Empty();
	FollowScientist = Avatar;
}

void ASWCameraPawn::UpdateRequestedFollow()
{
	// -SWFollowScientist: the team joins a few seconds after the bridge connects, so keep looking until then;
	// re-acquire if the avatar retires or hides (stale bridge). Evaluated first: the Leviathan block below
	// returns early while no predator exists and must not swallow this request.
	if (!RequestedScientist.IsEmpty() && !(FollowScientist.IsValid() && !FollowScientist->IsHidden()))
	{
		FollowScientist = nullptr;
		if (const ASWWorldManager* M = ASWWorldManager::Get(GetWorld()))
		{
			const bool bAny = RequestedScientist.Equals(TEXT("any"), ESearchCase::IgnoreCase);
			for (ASWScientistAvatar* A : M->GetScientistAvatars())
			{
				if (!IsValid(A) || A->IsHidden()) continue;
				if (bAny || A->GetScientistName().Equals(RequestedScientist, ESearchCase::IgnoreCase))
				{
					FollowScientist = A;
					UE_LOG(LogSymbioticWorld, Log, TEXT("-SWFollowScientist: following %s"), *A->GetScientistName());
					break;
				}
			}
			// Both prerequisites are named, once, after a 5 sim-s grace (the policy file is polled every 3 s).
			if (!FollowScientist.IsValid() && !bWarnedNoScientist && M->GetSimTime() > 5.f
				&& (!M->GetLook().bScientistAvatars || !M->HasPolicyServers()))
			{
				bWarnedNoScientist = true;
				UE_LOG(LogSymbioticWorld, Warning, TEXT("-SWFollowScientist=%s: %s; nothing to follow"), *RequestedScientist,
					!M->GetLook().bScientistAvatars ? TEXT("Look.bScientistAvatars is off (add -SWSet=\"Look.bScientistAvatars=1\")")
					                                : TEXT("no policy bridge (add --policy host:port=Both with Lab.lab observe --embody running)"));
			}
		}
	}

	if (bRequestedLeviathan && !FollowActor.IsValid())
	{
		for (TActorIterator<ASWLeviathan> It(GetWorld()); It; ++It) { FollowActor = *It; break; }
		if (!FollowActor.IsValid() && !bWarnedNoLeviathan)
		{
			// A predator that has not spawned yet is a timing gap; one that can never spawn deserves a line.
			if (const ASWWorldManager* M = ASWWorldManager::Get(GetWorld()))
			{
				if (!M->GetSettings().bLeviathan || M->GetSettings().LeviathanCount <= 0)
				{
					bWarnedNoLeviathan = true;
					UE_LOG(LogSymbioticWorld, Warning, TEXT("-SWFollowSpecies=Leviathan: no predator in this run (Settings.bLeviathan=%d, LeviathanCount=%d); nothing to follow"),
						M->GetSettings().bLeviathan ? 1 : 0, M->GetSettings().LeviathanCount);
				}
			}
		}
		return;
	}
	if (!RequestedFollowSpecies.IsSet()) return;
	ASWWorldManager* M = ASWWorldManager::Get(GetWorld());
	if (!M) return;
	const ESWSpecies Wanted = RequestedFollowSpecies.GetValue();
	// The inspector, the HUD marker and the chase camera should describe the same organism, so the
	// selected one wins when it is of the requested species (-SWAutoSelect keeps re-selecting).
	ASWAgent* Selected = M->GetSelectedAgent();
	if (IsValid(Selected) && Selected->IsAlive() && Selected->GetSpecies() == Wanted)
	{
		if (FollowTarget != Selected) FollowTarget = Selected;
		return;
	}
	if (IsValid(FollowTarget) && FollowTarget->IsAlive() && FollowTarget->GetSpecies() == Wanted) return;
	for (TActorIterator<ASWAgent> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && It->GetSpecies() == Wanted)
		{
			M->SelectAgent(*It);   // HUD / material state only: no seeded draw, no sim effect
			FollowTarget = *It;
			return;
		}
	}
}

void ASWCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bStartPlaced) PlaceStartCamera();

	FRotator Rot = GetActorRotation();
	if (bLooking)
	{
		Rot.Yaw += LookInput.X * LookSpeed;
		Rot.Pitch = FMath::Clamp(Rot.Pitch + LookInput.Y * LookSpeed, -89.f, 89.f);
		Rot.Roll = 0.f;
		SetActorRotation(Rot);
	}

	// Any manual movement breaks follow mode (and a scripted -SWFollowSpecies).
	if (!MoveInput.IsNearlyZero() || FMath::Abs(ZoomInput) > KINDA_SMALL_NUMBER)
	{
		SetFollowTarget(nullptr);
	}

	UpdateRequestedFollow();

	if (FollowScientist.IsValid() && !FollowScientist->IsHidden())
	{
		// Field-team chase-cam: a 1.8 m body, so sit close (about 5 m back) at head height.
		const FVector Target = FollowScientist->GetActorLocation() + FVector(0.f, 0.f, 120.f);
		FVector Desired = Target + FVector(-420.f, 220.f, 170.f);
		if (const ASWWorldManager* M = ASWWorldManager::Get(GetWorld()))
		{
			Desired.Z = FMath::Max(Desired.Z, M->GetGroundZ(Desired.X, Desired.Y) + 120.f);
		}
		SetActorLocation(FMath::VInterpTo(GetActorLocation(), Desired, DeltaSeconds, 3.f));
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), (Target - GetActorLocation()).Rotation(), DeltaSeconds, 3.f));
	}
	else if (FollowActor.IsValid())
	{
		// Predator chase-cam: the animal is ~18 m long at the water line, so sit well back and high.
		const FVector Target = FollowActor->GetActorLocation() + FVector(0.f, 0.f, 150.f);
		FVector Desired = Target + FVector(-3000.f, 1500.f, 1300.f);
		if (const ASWWorldManager* M = ASWWorldManager::Get(GetWorld()))
		{
			Desired.Z = FMath::Max(Desired.Z, M->GetGroundZ(Desired.X, Desired.Y) + 150.f);
		}
		SetActorLocation(FMath::VInterpTo(GetActorLocation(), Desired, DeltaSeconds, 3.f));
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), (Target - GetActorLocation()).Rotation(), DeltaSeconds, 3.f));
	}
	else if (IsValid(FollowTarget) && FollowTarget->IsAlive())
	{
		// Chase-cam: sit behind/above the agent, smoothly, framed for the species' body size
		// (authored Tecton ~11.6 m long, Lumen ~5 m; docs/CREATURE_RENDERING.md).
		const bool bTecton = FollowTarget->GetSpecies() == ESWSpecies::Tecton;
		const FVector Target = FollowTarget->GetActorLocation() + FVector(0.f, 0.f, bTecton ? 280.f : 130.f);
		FVector Desired = Target + (bTecton ? FVector(-1800.f, 900.f, 650.f) : FVector(-900.f, 400.f, 340.f));
		// Never below the ground: the offsets are world-space and the valley sides rise toward the rim.
		if (const ASWWorldManager* M = ASWWorldManager::Get(GetWorld()))
		{
			Desired.Z = FMath::Max(Desired.Z, M->GetGroundZ(Desired.X, Desired.Y) + 150.f);
		}
		SetActorLocation(FMath::VInterpTo(GetActorLocation(), Desired, DeltaSeconds, 3.f));
		const FRotator LookAt = (Target - GetActorLocation()).Rotation();
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), LookAt, DeltaSeconds, 3.f));
	}
	else
	{
		if (FollowTarget && (!IsValid(FollowTarget) || !FollowTarget->IsAlive())) FollowTarget = nullptr;
		if (FollowScientist.IsValid() && FollowScientist->IsHidden()) FollowScientist = nullptr;   // stale bridge hid the team

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
