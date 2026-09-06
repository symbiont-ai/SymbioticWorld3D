#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SWGameMode.generated.h"

class ASWWorldManager;

// Sets up the camera pawn, controller and HUD, and spawns the environment
// (floor, lights, sky) and the world manager at runtime so the project runs in
// a completely empty level with no hand-authored assets.
UCLASS()
class SYMBIOTICWORLD_API ASWGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASWGameMode();

	virtual void BeginPlay() override;

	// If false, only the world manager is spawned (use when you have built your
	// own valley level and want to keep its lighting).
	UPROPERTY(EditAnywhere, Category = "Symbiotic World") bool bSpawnEnvironment = true;

	ASWWorldManager* GetWorldManager() const { return WorldManager; }

protected:
	UPROPERTY() ASWWorldManager* WorldManager = nullptr;

	void SpawnEnvironment();
};
