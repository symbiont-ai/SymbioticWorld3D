#include "SWKillMark.h"
#include "SWWorldManager.h"
#include "SWProcMesh.h"
#include "SymbioticWorld.h"
#include "ProceduralMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
	// Blood: dark red, drawn with the unlit translucent ribbon material (vertex colour G = opacity).
	const FLinearColor KillPlumeColor(0.62f, 0.045f, 0.03f);
	constexpr int32 KillPlumeSegments = 10;   // per puff
	constexpr int32 KillPlumePuffs = 7;       // soft discs built in the camera's plane, so the cloud reads from any angle
}

ASWKillMark::ASWKillMark()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;   // visual layer: after the manager's step
	SetActorEnableCollision(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Plume = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Plume"));
	Plume->SetupAttachment(Root);
	Plume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Plume->SetCastShadow(false);
	Plume->bUseAsyncCooking = true;

	BodyCopy = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BodyCopy"));
	BodyCopy->SetupAttachment(Root);
	BodyCopy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyCopy->SetCastShadow(false);
	BodyCopy->bUseAsyncCooking = true;

	AuthoredCopy = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("AuthoredCopy"));
	AuthoredCopy->SetupAttachment(Root);
	AuthoredCopy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AuthoredCopy->SetCastShadow(false);
	AuthoredCopy->SetVisibility(false);
	// Reference pose only: no animation, no tick (the stand-in tumbles as a rigid body).
	AuthoredCopy->PrimaryComponentTick.bStartWithTickEnabled = false;
	AuthoredCopy->SetComponentTickEnabled(false);
	AuthoredCopy->bUseRefPoseOnInitAnim = true;
}

void ASWKillMark::Init(ASWWorldManager* InManager, ESWSpecies InSpecies, int32 InAgentId, const FVector& InLocation,
                       const FRotator& InRotation, float InMeshScale)
{
	Manager = InManager;
	if (!Manager) return;
	const FSWLookSettings& L = Manager->GetLook();
	PlumeSeconds = FMath::Max(L.KillPlumeSeconds, 0.05f);
	BodySeconds = FMath::Max(L.KillBodySeconds, 0.05f);
	SetActorLocation(InLocation);
	BodyStartRotation = InRotation;
	BodyScale = FMath::Max(InMeshScale, 0.01f);

	// Private visual stream, seeded like the creature bodies (ASWAgent::BuildBody): the same kill
	// always looks the same, and the seeded simulation stream is never touched.
	FRandomStream VisRng(InAgentId * 7919 + L.LookSeed * 131 + (InSpecies == ESWSpecies::Lumen ? 3 : 29));
	for (int32 i = 0; i < 8; ++i) Wobble[i] = VisRng.FRandRange(0.72f, 1.28f);
	BodyTumble = FRotator(VisRng.FRandRange(50.f, 140.f), VisRng.FRandRange(-60.f, 60.f), VisRng.FRandRange(-160.f, 160.f));

	// ---- Stand-in body: the authored creature when the content is installed, else the procedural one.
	if (!BuildAuthoredCopy(InSpecies, InMeshScale))
	{
		FRandomStream BodyRng(InAgentId * 7919 + L.LookSeed * 131 + (InSpecies == ESWSpecies::Lumen ? 0 : 17));
		SWProc::FMeshData M;
		if (InSpecies == ESWSpecies::Lumen) SWProc::BuildLumen(BodyRng, M);
		else                                SWProc::BuildTecton(BodyRng, M);
		BodyCopy->CreateMeshSection_LinearColor(0, M.Verts, M.Tris, M.Normals, M.UV0, M.Colors, TArray<FProcMeshTangent>(), false);
		BodyCopy->SetRelativeScale3D(FVector(BodyScale));
		BodyCopy->SetRelativeRotation(BodyStartRotation);
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SW_Creature.M_SW_Creature"));
		if (!Base) Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (Base)
		{
			BodyBaseColor = InSpecies == ESWSpecies::Lumen ? L.LumenBody : L.TectonBody;
			BodyMID = UMaterialInstanceDynamic::Create(Base, this);
			BodyMID->SetVectorParameterValue(TEXT("BodyColor"), BodyBaseColor);
			BodyMID->SetVectorParameterValue(TEXT("EmissiveColor"), KillPlumeColor);
			BodyMID->SetVectorParameterValue(TEXT("Color"), KillPlumeColor);
			BodyMID->SetScalarParameterValue(TEXT("PulseSpeed"), 0.f);
			BodyMID->SetScalarParameterValue(TEXT("Roughness"), 0.6f);
			BodyCopy->SetMaterial(0, BodyMID);
		}
	}

	// ---- Fall and cloud, from the stand-in's own bounds (a Tecton is ~7 m long and ~2.5x a Lumen
	// tall, and a species constant made it sit still inside a wide cloud). Organisms swim at the
	// surface, so the kill is a few uu under Look.WaterLevel and the body has to clear it.
	{
		USceneComponent* Visual = AuthoredCopy && AuthoredCopy->IsVisible() ? static_cast<USceneComponent*>(AuthoredCopy)
		                                                                   : static_cast<USceneComponent*>(BodyCopy);
		FVector Extent(60.f, 60.f, 60.f);
		if (Visual)
		{
			const FBoxSphereBounds Bounds = Visual->CalcBounds(Visual->GetComponentTransform());
			Extent = Bounds.BoxExtent;
		}
		const float BodyHeight = FMath::Max(Extent.Z * 2.f, 40.f);
		const float BodyLength = FMath::Max(FMath::Max(Extent.X, Extent.Y) * 2.f, 80.f);
		// Under the surface before the shrink starts: 1.5 body heights, with a floor so the small
		// Lumen still visibly drops.
		const float Wanted = FMath::Max(1.5f * BodyHeight, 160.f);
		// Never through the riverbed: the fall stops on the bed under the kill, where a body in the
		// shallows slumps and fades instead of tunnelling.
		const float BedZ = SWProc::TerrainHeight(L, InLocation.X, InLocation.Y);
		const float Room = FMath::Max(InLocation.Z - (BedZ + 15.f), 0.f);
		SinkDepth = FMath::Min(Wanted, Room);
		// Cloud about as wide as the animal is long (the puffs spread to ~3x this radius), floored so
		// the Lumen plume stays exactly as legible as it was, and centred a little below the surface
		// so it reads as blood diffusing underwater rather than splatter lying on top.
		PlumeRadius = FMath::Clamp(BodyLength * 0.45f, 400.f, 900.f);
		PlumeBaseZ = -FMath::Min(0.25f * BodyHeight + 20.f, FMath::Max(Room - 10.f, 0.f));
		// The derived numbers, so a take can be checked without guessing: a body taller than the water
		// is deep enough to slump on the bed, not to disappear under the surface.
		UE_LOG(LogSymbioticWorld, Log, TEXT("kill mark: %s body %.0fx%.0f uu, water %.0f uu deep, sink %.0f (wanted %.0f), cloud radius %.0f"),
			SWSpeciesName(InSpecies), BodyLength, BodyHeight, Room, SinkDepth, Wanted, PlumeRadius);
	}

	// ---- Plume: the ribbon material is unlit, two-sided and translucent, and reads vertex colour G
	// as opacity, which is exactly the fade this needs.
	UMaterialInterface* PlumeBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SW_Trail.M_SW_Trail"));
	if (!PlumeBase) PlumeBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (PlumeBase)
	{
		PlumeMID = UMaterialInstanceDynamic::Create(PlumeBase, this);
		PlumeMID->SetVectorParameterValue(TEXT("TrailColor"), KillPlumeColor);
		PlumeMID->SetVectorParameterValue(TEXT("Color"), KillPlumeColor);
		PlumeMID->SetScalarParameterValue(TEXT("TrailGlow"), 3.2f);
		Plume->SetMaterial(0, PlumeMID);
	}
	UpdatePlume();
	UpdateBody();
}

bool ASWKillMark::BuildAuthoredCopy(ESWSpecies InSpecies, float InMeshScale)
{
	if (!Manager) return false;
	const FSWLookSettings& L = Manager->GetLook();
	if (!L.bAuthoredCreatures || !FApp::CanEverRender()) return false;
	const FString Name = SWSpeciesName(InSpecies);
	const FString Folder = FString::Printf(TEXT("/Game/Characters/Symbiotic/%s/"), *Name);
	USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, *(Folder + TEXT("SK_") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Asset) return false;   // no creature content on this machine: the procedural stand-in is used
	AuthoredCopy->SetSkeletalMesh(Asset);
	AuthoredCopy->SetComponentTickEnabled(false);
	const float WorldScale = InMeshScale * (InSpecies == ESWSpecies::Lumen ? L.AuthoredLumenScale : L.AuthoredTectonScale);
	BodyScale = FMath::Max(WorldScale, 0.01f);
	AuthoredCopy->SetRelativeScale3D(FVector(BodyScale));
	AuthoredCopy->SetRelativeRotation(BodyStartRotation);
	AuthoredCopy->SetVisibility(true);
	BodyCopy->SetVisibility(false);
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *(Folder + TEXT("M_") + Name + TEXT("_Authored")), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Material)
	{
		// The authored material tints its base colour with BodyTint and its emission with GlowTint
		// (Tools/import_symbiotic_creatures.py); BodyColor belongs to the procedural material and does
		// nothing here. Both are set, so either kind of material takes the tint.
		BodyMID = UMaterialInstanceDynamic::Create(Material, this);
		bAuthoredTint = true;
		BodyBaseColor = FLinearColor::White;   // the tint parameters are multipliers, so white = unchanged
		BodyMID->SetVectorParameterValue(TEXT("BodyTint"), BodyBaseColor);
		AuthoredCopy->SetMaterial(0, BodyMID);
	}
	return true;
}

void ASWKillMark::UpdatePlume()
{
	if (!Plume) return;
	const float T = FMath::Clamp(Age / PlumeSeconds, 0.f, 1.f);
	// Spreads fast, then slows (sqrt), and sinks: it is in the water, not in the air.
	const float Spread = PlumeRadius * FMath::Sqrt(T);
	const float PuffR = PlumeRadius * (0.34f + 0.26f * T);
	const float Alpha = FMath::Clamp(1.f - T * T, 0.f, 1.f);
	// The cloud sinks with the blood, but only as far as there is water under the kill (a shallow
	// kill would otherwise push it into the riverbed).
	const float Drift = -FMath::Clamp(-PlumeBaseZ * 1.2f, 40.f, 150.f) * T;

	// Built in the camera's plane: a flat disc lying in the water is nearly edge-on from the observer
	// camera (pitch -15) and would be invisible, so each puff faces the viewer. Camera only, read once
	// per frame; with no camera (a headless frame) the world axes are used.
	FVector Right = FVector(0.f, 1.f, 0.f), Up = FVector(0.f, 0.f, 1.f);
	if (const UWorld* W = GetWorld())
	{
		if (const APlayerController* PC = W->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				const FRotator CamRot = PC->PlayerCameraManager->GetCameraRotation();
				Right = FRotationMatrix(CamRot).GetScaledAxis(EAxis::Y);
				Up = FRotationMatrix(CamRot).GetScaledAxis(EAxis::Z);
			}
		}
	}

	SWProc::FMeshData M;
	M.Reserve(KillPlumePuffs * (KillPlumeSegments + 2), KillPlumePuffs * KillPlumeSegments * 3);
	for (int32 Puff = 0; Puff < KillPlumePuffs; ++Puff)
	{
		// Fixed directions from the private stream: the cloud opens outward and downward, the same way
		// every time this kill is replayed.
		const float Ang = 2.f * PI * Puff / KillPlumePuffs + Wobble[Puff % 8];
		const float W = Wobble[(Puff + 3) % 8];
		const float Out = Puff == 0 ? 0.f : Spread * (0.35f + 0.65f * W);
		const FVector Centre(FMath::Cos(Ang) * Out, FMath::Sin(Ang) * Out,
		                     PlumeBaseZ + Drift * (0.4f + 0.6f * W) - 30.f * Puff * T);
		const float R = PuffR * (Puff == 0 ? 1.15f : 0.55f + 0.45f * W);
		const float A = Alpha * (Puff == 0 ? 1.f : 0.55f + 0.35f * W);
		// Vertex colour G is the ribbon material's opacity: opaque core, transparent rim = a soft blob.
		const int32 Hub = M.AddVert(Centre, FLinearColor(1.f, A, 0.f, 1.f), FVector2D(0.5f, 0.5f));
		for (int32 i = 0; i <= KillPlumeSegments; ++i)
		{
			const float A2 = 2.f * PI * i / KillPlumeSegments;
			const FVector P = Centre + (Right * FMath::Cos(A2) + Up * FMath::Sin(A2)) * R * (0.85f + 0.3f * Wobble[i % 8]);
			M.AddVert(P, FLinearColor(1.f, 0.f, 0.f, 1.f), FVector2D(0.5f, 1.f));
		}
		for (int32 i = 0; i < KillPlumeSegments; ++i)
		{
			M.AddTri(Hub, Hub + 1 + i, Hub + 2 + i);
		}
	}
	SWProc::ComputeNormals(M);
	if (!bPlumeBuilt)
	{
		bPlumeBuilt = true;
		Plume->CreateMeshSection_LinearColor(0, M.Verts, M.Tris, M.Normals, M.UV0, M.Colors, TArray<FProcMeshTangent>(), false);
	}
	else
	{
		// Same vertex count every frame, so the cheap update path applies.
		Plume->UpdateMeshSection_LinearColor(0, M.Verts, M.Normals, M.UV0, M.Colors, TArray<FProcMeshTangent>());
	}
}

void ASWKillMark::UpdateBody()
{
	USceneComponent* Visual = AuthoredCopy && AuthoredCopy->IsVisible() ? static_cast<USceneComponent*>(AuthoredCopy)
	                                                                   : static_cast<USceneComponent*>(BodyCopy);
	if (!Visual) return;
	const float T = FMath::Clamp(Age / BodySeconds, 0.f, 1.f);
	if (T >= 1.f)
	{
		Visual->SetVisibility(false);
		return;
	}
	// Tumble and sink first, and only then shrink away (the creature material is opaque, so the
	// stand-in leaves by going small): the descent is over at BodyFallEnd, the shrink starts after it,
	// so nothing ever pops out of existence in mid-air.
	const float Fall = FMath::Clamp(T / BodyFallEnd, 0.f, 1.f);
	Visual->SetRelativeLocation(FVector(0.f, 0.f, -SinkDepth * Fall * Fall));
	Visual->SetRelativeRotation(BodyStartRotation + BodyTumble * Fall);
	const float Shrink = T < BodyShrinkStart ? 1.f : FMath::Max(1.f - (T - BodyShrinkStart) / (1.f - BodyShrinkStart), 0.f);
	Visual->SetRelativeScale3D(FVector(BodyScale * Shrink));
	if (BodyMID)
	{
		// Tinted toward red over the fall: the authored material multiplies its textures by BodyTint /
		// GlowTint, the procedural one takes a flat BodyColor.
		const float Tint = FMath::Min(1.f, Fall * 1.6f);
		if (bAuthoredTint)
		{
			BodyMID->SetVectorParameterValue(TEXT("BodyTint"), FMath::Lerp(BodyBaseColor, FLinearColor(1.5f, 0.10f, 0.06f), Tint));
			BodyMID->SetVectorParameterValue(TEXT("GlowTint"), FMath::Lerp(FLinearColor::White, FLinearColor(1.2f, 0.06f, 0.04f), Tint));
		}
		else
		{
			BodyMID->SetVectorParameterValue(TEXT("BodyColor"), FMath::Lerp(BodyBaseColor, KillPlumeColor, Tint));
		}
	}
}

void ASWKillMark::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Wall seconds: the kill effects are a rendered-frame layer, like the caption and the avatars.
	Age += DeltaSeconds;
	UpdatePlume();
	UpdateBody();
	if (Age >= FMath::Max(PlumeSeconds, BodySeconds))
	{
		Destroy();
	}
}
