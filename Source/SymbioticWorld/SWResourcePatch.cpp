#include "SWResourcePatch.h"
#include "SWWorldManager.h"
#include "SWProcMesh.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

ASWResourcePatch::ASWResourcePatch()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Cluster = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Cluster"));
	Cluster->SetupAttachment(Root);
	Cluster->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cluster->SetCastShadow(true);
	Cluster->bUseAsyncCooking = true;
}

void ASWResourcePatch::Init(int32 InType, float InCapacity, float InRegen, float InitialFraction)
{
	ResourceType = InType;
	Capacity = InCapacity;
	Regen = InRegen;
	Stock = FMath::Clamp(InitialFraction, 0.f, 1.f) * Capacity;

	const ASWWorldManager* M = ASWWorldManager::Get(GetWorld());
	const FSWLookSettings L = M ? M->GetLook() : FSWLookSettings();
	GlowStrength = L.ResourceGlow;

	// Deterministic visual variation from the patch position.
	const FVector P = GetActorLocation();
	FRandomStream VisRng(static_cast<int32>(P.X * 0.37f + P.Y * 1.91f) + L.LookSeed);
	SWProc::FMeshData Mesh;
	SWProc::BuildGlowCluster(VisRng, ResourceType == 0 ? 85.f : 115.f, Mesh);
	Cluster->CreateMeshSection_LinearColor(0, Mesh.Verts, Mesh.Tris, Mesh.Normals, Mesh.UV0, Mesh.Colors, TArray<FProcMeshTangent>(), false);

	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SW_Glow.M_SW_Glow"));
	if (!Base) Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Base)
	{
		MID = UMaterialInstanceDynamic::Create(Base, this);
		const FLinearColor C = ResourceType == 0 ? L.ResourceAGlow : L.ResourceBGlow;
		MID->SetVectorParameterValue(TEXT("GlowColor"), C);
		MID->SetVectorParameterValue(TEXT("Color"), C);
		Cluster->SetMaterial(0, MID);
	}
	LastVisualFrac = -1.f;
	UpdateVisual(Capacity);
}

void ASWResourcePatch::Step(float Dt, float RegenMultiplier, float CapacityMultiplier)
{
	const float EffCap = FMath::Max(1.f, Capacity * CapacityMultiplier);
	// Logistic regrowth: fastest when depleted, zero at capacity. Under drought
	// the effective capacity drops, so stock above it decays toward it.
	if (Stock < EffCap)
	{
		Stock += Regen * RegenMultiplier * (1.f - Stock / EffCap) * Dt;
		Stock = FMath::Min(Stock, EffCap);
	}
	else
	{
		Stock = FMath::FInterpTo(Stock, EffCap, Dt, 0.5f);
	}
	UpdateVisual(EffCap);
}

float ASWResourcePatch::Take(float Amount)
{
	const float Taken = FMath::Clamp(Amount, 0.f, Stock);
	Stock -= Taken;
	return Taken;
}

void ASWResourcePatch::UpdateVisual(float EffectiveCapacity)
{
	const float Frac = FMath::Clamp(Stock / FMath::Max(Capacity, 1.f), 0.f, 1.f);
	// Only touch render state when the visible fraction actually changes.
	if (FMath::Abs(Frac - LastVisualFrac) < 0.01f) return;
	LastVisualFrac = Frac;
	// Depleted patches sink into the ground and go dim; full ones stand tall and glow.
	Cluster->SetRelativeScale3D(FVector(1.f, 1.f, 0.35f + 0.65f * Frac));
	if (MID)
	{
		MID->SetScalarParameterValue(TEXT("GlowStrength"), GlowStrength * (0.08f + 0.92f * Frac * Frac));
	}
}
