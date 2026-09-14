#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SWTypes.h"
#include "SWCameraPawn.generated.h"

class UCameraComponent;
class ASWAgent;
class ASWScientistAvatar;

// Free-flying observer camera. WASD/QE move, mouse wheel zooms, hold right
// mouse to look. F toggles following the selected agent.
// -SWFollowSpecies=Lumen|Tecton|Leviathan (inspection): follows the selected
// organism of that species, selecting one if none is; re-acquires when it dies;
// Leviathan follows the river predator (Settings.bLeviathan). Any manual camera
// input releases it.
// G cycles a chase-cam through the field-team scientists (Look.bScientistAvatars);
// -SWFollowScientist=<Name>|any does the same from the command line for scripted
// screenshots, waiting for the avatar to join. Camera only, like every follow.
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
	void SetFollowScientist(ASWScientistAvatar* Avatar);
	ASWScientistAvatar* GetFollowedScientist() const { return FollowScientist.Get(); }
	bool IsFollowing() const { return FollowTarget != nullptr || FollowActor.IsValid() || FollowScientist.IsValid(); }

protected:
	UPROPERTY(VisibleAnywhere) USceneComponent* Root;
	UPROPERTY(VisibleAnywhere) UCameraComponent* Camera;
	UPROPERTY() ASWAgent* FollowTarget = nullptr;
	TOptional<ESWSpecies> RequestedFollowSpecies;   // -SWFollowSpecies, until the viewer takes the camera
	bool bRequestedLeviathan = false;               // -SWFollowSpecies=Leviathan
	bool bWarnedNoLeviathan = false;
	TWeakObjectPtr<AActor> FollowActor;             // the predator being followed (visual only)
	TWeakObjectPtr<ASWScientistAvatar> FollowScientist;   // the field-team member being followed (G / -SWFollowScientist)
	FString RequestedScientist;                     // -SWFollowScientist name (or "any"), until the viewer takes the camera
	bool bWarnedNoScientist = false;

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

	// -SWFollowSpecies: keep FollowTarget on a living, selected organism of the requested species.
	void UpdateRequestedFollow();
	// Start framing (arena-relative) on the first Tick, once the manager has its settings; -SWCam places at BeginPlay.
	bool bStartPlaced = false;
	void PlaceCamera(const FVector& Loc, const FRotator& Rot);
	void PlaceStartCamera();
};
