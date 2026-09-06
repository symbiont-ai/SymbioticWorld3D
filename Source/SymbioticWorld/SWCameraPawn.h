#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SWCameraPawn.generated.h"

class UCameraComponent;
class ASWAgent;

// Free-flying observer camera. WASD/QE move, mouse wheel zooms, hold right
// mouse to look. F toggles following the selected agent.
UCLASS()
class SYMBIOTICWORLD_API ASWCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ASWCameraPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void SetFollowTarget(ASWAgent* Agent);
	bool IsFollowing() const { return FollowTarget != nullptr; }

protected:
	UPROPERTY(VisibleAnywhere) USceneComponent* Root;
	UPROPERTY(VisibleAnywhere) UCameraComponent* Camera;
	UPROPERTY() ASWAgent* FollowTarget = nullptr;

	float MoveSpeed = 2500.f;   // uu/s
	float LookSpeed = 1.2f;     // deg per mouse unit
	bool bLooking = false;
	FVector MoveInput = FVector::ZeroVector;
	float ZoomInput = 0.f;
	FVector2D LookInput = FVector2D::ZeroVector;

	void OnForward(float V) { MoveInput.X = V; }
	void OnRight(float V) { MoveInput.Y = V; }
	void OnUp(float V) { MoveInput.Z = V; }
	void OnZoom(float V) { ZoomInput += V; }
	void OnTurn(float V) { LookInput.X = V; }
	void OnLookUp(float V) { LookInput.Y = V; }
	void OnLookPressed() { bLooking = true; }
	void OnLookReleased() { bLooking = false; }
};
