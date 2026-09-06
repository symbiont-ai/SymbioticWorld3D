#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SWPlayerController.generated.h"

class ASWWorldManager;
class ASWAgent;

// Binds the simulation-control keys declared in Config/DefaultInput.ini and
// routes them to the world manager. Camera movement lives on ASWCameraPawn.
//
//   LMB    select agent under cursor      Tab  cycle to youngest Lumen
//   1/2/3  1x / 10x / 50x                 Space pause
//   P      toggle drought                 M    cycle mode A>B>C>N (resets run)
//   R      reset run (same seed)          F    follow selected agent
//   H      toggle help overlay
UCLASS()
class SYMBIOTICWORLD_API ASWPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASWPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	bool IsHelpVisible() const { return bShowHelp; }
	ASWWorldManager* GetManager() const;

protected:
	bool bShowHelp = true;

	void OnSelectAgent();
	void OnCycleSelect();
	void OnSpeed1();
	void OnSpeed10();
	void OnSpeed50();
	void OnTogglePause();
	void OnToggleDrought();
	void OnCycleMode();
	void OnResetRun();
	void OnToggleHelp();
	void OnFollowSelected();
};
