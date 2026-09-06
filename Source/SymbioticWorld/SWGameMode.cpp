#include "SWGameMode.h"
#include "SWWorldManager.h"
#include "SWCameraPawn.h"
#include "SWPlayerController.h"
#include "SWHUD.h"
#include "SWEnvironment.h"
#include "SymbioticWorld.h"
#include "Engine/World.h"

ASWGameMode::ASWGameMode()
{
	DefaultPawnClass = ASWCameraPawn::StaticClass();
	PlayerControllerClass = ASWPlayerController::StaticClass();
	HUDClass = ASWHUD::StaticClass();
}

void ASWGameMode::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World) return;

	// The manager parses -SWSet (including Look.*) in its BeginPlay, so it must
	// exist before the environment is built.
	WorldManager = ASWWorldManager::Get(World);
	if (!WorldManager)
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		WorldManager = World->SpawnActor<ASWWorldManager>(ASWWorldManager::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);
	}

	if (bSpawnEnvironment && WorldManager)
	{
		SpawnEnvironment();
	}
}

void ASWGameMode::SpawnEnvironment()
{
	UWorld* World = GetWorld();
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASWEnvironment* Env = World->SpawnActor<ASWEnvironment>(ASWEnvironment::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);
	if (Env)
	{
		Env->Build(WorldManager);
		WorldManager->SetEnvironment(Env);
		// Founders were spawned in the manager's BeginPlay before the environment
		// existed; re-ground them now (no-op for the flat case).
		WorldManager->RegroundAll();
	}
}
