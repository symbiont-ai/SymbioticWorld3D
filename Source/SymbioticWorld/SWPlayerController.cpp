#include "SWPlayerController.h"
#include "SWWorldManager.h"
#include "SWAgent.h"
#include "SWCameraPawn.h"
#include "SymbioticWorld.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"

ASWPlayerController::ASWPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = false;
}

void ASWPlayerController::BeginPlay()
{
	Super::BeginPlay();
	// Game-only input keeps the legacy axis bindings (MouseX/MouseY for RMB look)
	// alive while the cursor stays visible for picking.
	FInputModeGameOnly Mode;
	Mode.SetConsumeCaptureMouseDown(false);
	SetInputMode(Mode);
	bShowMouseCursor = true;
}

ASWWorldManager* ASWPlayerController::GetManager() const
{
	return ASWWorldManager::Get(GetWorld());
}

void ASWPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent) return;

	InputComponent->BindAction("SelectAgent",    IE_Pressed, this, &ASWPlayerController::OnSelectAgent);
	InputComponent->BindAction("CycleSelect",    IE_Pressed, this, &ASWPlayerController::OnCycleSelect);
	InputComponent->BindAction("Speed1",         IE_Pressed, this, &ASWPlayerController::OnSpeed1);
	InputComponent->BindAction("Speed10",        IE_Pressed, this, &ASWPlayerController::OnSpeed10);
	InputComponent->BindAction("Speed50",        IE_Pressed, this, &ASWPlayerController::OnSpeed50);
	InputComponent->BindAction("TogglePause",    IE_Pressed, this, &ASWPlayerController::OnTogglePause);
	InputComponent->BindAction("ToggleDrought",  IE_Pressed, this, &ASWPlayerController::OnToggleDrought);
	InputComponent->BindAction("CycleMode",      IE_Pressed, this, &ASWPlayerController::OnCycleMode);
	InputComponent->BindAction("ResetRun",       IE_Pressed, this, &ASWPlayerController::OnResetRun);
	InputComponent->BindAction("ToggleHelp",     IE_Pressed, this, &ASWPlayerController::OnToggleHelp);
	InputComponent->BindAction("FollowSelected", IE_Pressed, this, &ASWPlayerController::OnFollowSelected);
	InputComponent->BindAction("ToggleScientists", IE_Pressed, this, &ASWPlayerController::OnToggleScientists);
}

void ASWPlayerController::OnSelectAgent()
{
	ASWWorldManager* M = GetManager();
	if (!M) return;
	FHitResult Hit;
	if (GetHitResultUnderCursor(ECC_Visibility, false, Hit))
	{
		if (ASWAgent* A = Cast<ASWAgent>(Hit.GetActor()))
		{
			M->SelectAgent(A);
			return;
		}
	}
	// Clicking empty space clears the selection (and follow mode), except in
	// auto-select runs (unattended screenshots) where a stray focus click must
	// not blank the inspector.
	if (M->IsAutoSelect()) return;
	M->SelectAgent(nullptr);
	if (ASWCameraPawn* Cam = Cast<ASWCameraPawn>(GetPawn())) Cam->SetFollowTarget(nullptr);
}

void ASWPlayerController::OnCycleSelect()
{
	if (ASWWorldManager* M = GetManager()) M->CycleSelection();
}

void ASWPlayerController::OnSpeed1()  { if (ASWWorldManager* M = GetManager()) M->SetTimeScale(1.f); }
void ASWPlayerController::OnSpeed10() { if (ASWWorldManager* M = GetManager()) M->SetTimeScale(10.f); }
void ASWPlayerController::OnSpeed50() { if (ASWWorldManager* M = GetManager()) M->SetTimeScale(50.f); }

void ASWPlayerController::OnTogglePause()
{
	if (ASWWorldManager* M = GetManager()) M->TogglePause();
}

void ASWPlayerController::OnToggleDrought()
{
	if (ASWWorldManager* M = GetManager()) M->ToggleDrought();
}

void ASWPlayerController::OnToggleScientists()
{
	// The mode toggle between god-view (default) and the embodied field team.
	// Visual only, so flipping it mid-run cannot change the run (the avatars
	// spawn/despawn on the next frame; witnessing continues bridge-side).
	if (ASWWorldManager* M = GetManager())
	{
		M->Look.bScientistAvatars = !M->Look.bScientistAvatars;
		UE_LOG(LogSymbioticWorld, Log, TEXT("Scientist avatars %s (V)"),
			M->Look.bScientistAvatars ? TEXT("ON") : TEXT("OFF"));
	}
}

void ASWPlayerController::OnCycleMode()
{
	if (ASWWorldManager* M = GetManager()) M->CycleMode();
	if (ASWCameraPawn* Cam = Cast<ASWCameraPawn>(GetPawn())) Cam->SetFollowTarget(nullptr);
}

void ASWPlayerController::OnResetRun()
{
	if (ASWWorldManager* M = GetManager()) M->ResetRun();
	if (ASWCameraPawn* Cam = Cast<ASWCameraPawn>(GetPawn())) Cam->SetFollowTarget(nullptr);
}

void ASWPlayerController::OnToggleHelp()
{
	bShowHelp = !bShowHelp;
}

void ASWPlayerController::OnFollowSelected()
{
	ASWWorldManager* M = GetManager();
	ASWCameraPawn* Cam = Cast<ASWCameraPawn>(GetPawn());
	if (!M || !Cam) return;
	Cam->SetFollowTarget(Cam->IsFollowing() ? nullptr : M->GetSelectedAgent());
}
