#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWResourcePatch.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

// A spatial resource patch with logistic regrowth. Type 0 = Resource A
// (Lumen food), Type 1 = Resource B (Tecton food). Purely numerical state
// plus a cluster of glowing stones whose brightness and height track the stock.
UCLASS()
class SYMBIOTICWORLD_API ASWResourcePatch : public AActor
{
	GENERATED_BODY()

public:
	ASWResourcePatch();

	void Init(int32 InType, float InCapacity, float InRegen, float InitialFraction);

	// Advance by Dt logical seconds. Multipliers implement drought.
	void Step(float Dt, float RegenMultiplier, float CapacityMultiplier);

	// Remove up to Amount; returns what was actually taken.
	float Take(float Amount);

	int32 GetResourceType() const { return ResourceType; }
	float GetStock() const { return Stock; }
	float GetCapacity() const { return Capacity; }
	float GetRegen() const { return Regen; }
	// Side of the main channel's centreline this patch is on (+1 / -1), set in SpawnPatches (SWProc::BankSide).
	void SetBankSide(int32 InSide) { BankSide = InSide >= 0 ? 1 : -1; }
	int32 GetBankSide() const { return BankSide; }
	// Order in which this patch switches off when its bank is inactive: 0 = first (farthest from the river among
	// its bank's patches of its type), 1 = last (nearest). Used with Settings.BankCycleRamp.
	void SetOffOrder(float InOrder) { OffOrder = FMath::Clamp(InOrder, 0.f, 1.f); }
	float GetOffOrder() const { return OffOrder; }

protected:
	UPROPERTY(VisibleAnywhere) USceneComponent* Root;
	UPROPERTY(VisibleAnywhere) UProceduralMeshComponent* Cluster;
	UPROPERTY() UMaterialInstanceDynamic* MID;

	UPROPERTY(VisibleAnywhere) int32 ResourceType = 0;
	UPROPERTY(VisibleAnywhere) float Stock = 0.f;
	UPROPERTY(VisibleAnywhere) float Capacity = 100.f;
	UPROPERTY(VisibleAnywhere) float Regen = 1.f;
	UPROPERTY(VisibleAnywhere) int32 BankSide = 0;
	UPROPERTY(VisibleAnywhere) float OffOrder = 0.f;

	float LastVisualFrac = -1.f;
	float GlowStrength = 5.f;

	void UpdateVisual(float EffectiveCapacity);
};
