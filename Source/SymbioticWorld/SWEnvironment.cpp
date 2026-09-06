#include "SWEnvironment.h"
#include "SWWorldManager.h"
#include "SWProcMesh.h"
#include "SymbioticWorld.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Engine/Texture.h"
#include "Kismet/KismetSystemLibrary.h"

ASWEnvironment::ASWEnvironment()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Terrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Terrain"));
	Terrain->SetupAttachment(Root);
	Terrain->bUseAsyncCooking = true;
	Terrain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Terrain->SetCastShadow(true);

	Features = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Features"));
	Features->SetupAttachment(Root);
	Features->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Features->SetCastShadow(true);

	Water = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Water"));
	Water->SetupAttachment(Root);
	Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Water->SetCastShadow(false);
}

UMaterialInterface* ASWEnvironment::LoadMat(const TCHAR* Path) const
{
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Path);
	if (!M)
	{
		UE_LOG(LogSymbioticWorld, Warning, TEXT("Material %s missing (run Tools/make_materials.py); using BasicShapeMaterial"), Path);
		M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	return M;
}

void ASWEnvironment::Build(ASWWorldManager* InManager)
{
	Manager = InManager;
	const FSWLookSettings& L = Manager->GetLook();
	const double T0 = FPlatformTime::Seconds();
	LoadRoles(L);
	BuildTerrain(L);
	BuildWater(L);
	const bool bImported = L.bUseImportedAssets && BuildImported(L);
	BuildFeatures(L);
	if (bImported) BuildImportedFeatures(L);
	BuildWaterfalls(L);
	BuildMoon(L);
	BuildTraceOverlay(L);
	BuildLighting(L);
	UE_LOG(LogSymbioticWorld, Log, TEXT("Imported dressing: %s [%s] (cliffs %d, wall pieces %d, pinnacles %d, arch rocks %d, boulders %d, river stones %d, groundcover %d, shrubs %d, trees %d, mist %d)"),
		bImported ? TEXT("yes") : TEXT("no, procedural only"), Roles.bFromManifest ? TEXT("manifest") : TEXT("built-in"),
		ImportedCliffs, ImportedWallPieces, ImportedPinnacles, ImportedArchRocks, ImportedBoulders, ImportedRiverStones, ImportedGroundcover, ImportedShrubs, ImportedTrees, MistSystems.Num());
	UE_LOG(LogSymbioticWorld, Log, TEXT("Cliff walls: %d pieces (%d rows x 2 sides + far end %d), pinnacles %d, arch ring rocks %d"),
		ImportedWallPieces, L.CliffWallRows, ImportedWallFarEnd, ImportedPinnacles, ImportedArchRingRocks);
	UE_LOG(LogSymbioticWorld, Log, TEXT("Massif arches: %d (sandstone material %s), foot cliffs %d, crown plants %d; scan material bound on %d slots of %d meshes"),
		ArchInfos.Num(), SandstoneMID ? TEXT("M_SW_Sandstone") : TEXT("M_SW_Rock fallback"), ImportedArchCliffs, ImportedCrownPlants, ScanBoundSlots, ScanBoundMeshes.Num());
	ApplyDrought(L, 0.f);
	UE_LOG(LogSymbioticWorld, Log, TEXT("Environment built in %.0f ms (grid %d, arches %d, rocks %d)"),
		(FPlatformTime::Seconds() - T0) * 1000.0, L.TerrainGrid, L.ArchCount, L.RockCount);
	RunConsoleCommands(L);
}

void ASWEnvironment::BuildTerrain(const FSWLookSettings& L)
{
	SWProc::FMeshData M;
	SWProc::BuildTerrain(L, M);
	Terrain->CreateMeshSection_LinearColor(0, M.Verts, M.Tris, M.Normals, M.UV0, M.Colors, TArray<FProcMeshTangent>(), false);
	UMaterialInterface* Base = nullptr;
	if (L.bUseImportedGroundMaterial && !Roles.GroundMaterial.IsEmpty())
	{
		Base = LoadObject<UMaterialInterface>(nullptr, *Roles.GroundMaterial);
		if (!Base) UE_LOG(LogSymbioticWorld, Warning, TEXT("Ground material %s not found; using M_SW_Terrain"), *Roles.GroundMaterial);
	}
	if (!Base && L.bUseImportedGroundMaterial)
	{
		// Built by Tools/make_materials.py from the migrated Megascans texture sets (moss grass / rocky ground / swamp mud / dried grass).
		Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SW_TerrainED.M_SW_TerrainED"));
	}
	if (!Base) Base = LoadMat(TEXT("/Game/Materials/M_SW_Terrain.M_SW_Terrain"));
	if (Base)
	{
		TerrainMID = UMaterialInstanceDynamic::Create(Base, this);
		TerrainMID->SetVectorParameterValue(TEXT("RockColor"), L.RockColor);
		TerrainMID->SetVectorParameterValue(TEXT("MossColor"), L.MossColor);
		TerrainMID->SetVectorParameterValue(TEXT("Color"), L.RockColor);   // BasicShapeMaterial fallback
		Terrain->SetMaterial(0, TerrainMID);
	}
}

void ASWEnvironment::BuildWater(const FSWLookSettings& L)
{
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (Plane) Water->SetStaticMesh(Plane);
	Water->SetRelativeLocation(FVector(0.f, 0.f, L.WaterLevel));
	Water->SetRelativeScale3D(FVector(L.TerrainHalfSize * 2.f / 100.f, L.TerrainHalfSize * 2.f / 100.f, 1.f));
	UMaterialInterface* Base = nullptr;
	bool bImportedWater = false;
	if (L.bUseImportedWaterMaterial && !Roles.WaterMaterial.IsEmpty())
	{
		Base = LoadObject<UMaterialInterface>(nullptr, *Roles.WaterMaterial);
		bImportedWater = Base != nullptr;
		if (!Base) UE_LOG(LogSymbioticWorld, Warning, TEXT("Water material %s not found; using M_SW_Water"), *Roles.WaterMaterial);
	}
	if (!Base) Base = LoadMat(TEXT("/Game/Materials/M_SW_Water.M_SW_Water"));
	if (Base)
	{
		WaterMID = UMaterialInstanceDynamic::Create(Base, this);
		WaterMID->SetVectorParameterValue(TEXT("WaterColor"), L.WaterColor);
		WaterMID->SetVectorParameterValue(TEXT("Color"), L.WaterColor);
		WaterMID->SetScalarParameterValue(TEXT("Brightness"), L.WaterBrightness);   // M_SW_Water only; the imported master ignores it
		Water->SetMaterial(0, WaterMID);
	}
	// Log the material the plane actually renders with (toggling bUseImportedWaterMaterial looked like a no-op in tests).
	UE_LOG(LogSymbioticWorld, Log, TEXT("Water plane: %s (%s; Look.bUseImportedWaterMaterial=%d, role '%s'); WaterColor (%.3f %.3f %.3f), WaterBrightness %.2f"),
		Base ? *Base->GetPathName() : TEXT("no material"), bImportedWater ? TEXT("imported") : TEXT("project M_SW_Water"), L.bUseImportedWaterMaterial ? 1 : 0, *Roles.WaterMaterial,
		L.WaterColor.R, L.WaterColor.G, L.WaterColor.B, L.WaterBrightness);
}

void ASWEnvironment::BuildFeatures(const FSWLookSettings& L)
{
	FRandomStream Rng(L.LookSeed);
	UMaterialInterface* Base = LoadMat(TEXT("/Game/Materials/M_SW_Rock.M_SW_Rock"));
	RockMID = Base ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
	if (RockMID)
	{
		RockMID->SetVectorParameterValue(TEXT("RockColor"), L.RockColor * 1.05f);
		RockMID->SetVectorParameterValue(TEXT("MossColor"), L.MossColor);
		RockMID->SetVectorParameterValue(TEXT("Color"), L.RockColor);
	}
	int32 Section = 0;
	auto AddSection = [&](const SWProc::FMeshData& M, UMaterialInterface* Mat)
	{
		Features->CreateMeshSection_LinearColor(Section, M.Verts, M.Tris, M.Normals, M.UV0, M.Colors, TArray<FProcMeshTangent>(), false);
		if (Mat) Features->SetMaterial(Section, Mat);
		Section++;
	};

	const float Half = L.TerrainHalfSize;
	const float Arena = Manager ? Manager->GetSettings().WorldHalfSize : 4500.f;

	// Massif arches (plate 1): flat-topped natural sandstone bridges with thick tapering legs, lofted by
	// SWProc::BuildMassifArch. Three placements, all ahead of the start camera (+X):
	//   hero  - spans the river, opening axis along world X so the camera looks through it;
	//   twin  - two openings sharing a middle pier on the +Y side, the low sun between them;
	//   far   - a small arch far down the valley on the -Y side.
	// Each arch is its own section with M_SW_Sandstone (triplanar rock, strata, moss on top); M_SW_Rock if missing.
	UMaterialInterface* SandBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SW_Sandstone.M_SW_Sandstone"));
	if (SandBase)
	{
		SandstoneMID = UMaterialInstanceDynamic::Create(SandBase, this);
	}
	else
	{
		UE_LOG(LogSymbioticWorld, Warning, TEXT("M_SW_Sandstone missing (run Tools/make_materials.py); massif arches use M_SW_Rock"));
	}
	UMaterialInterface* ArchMat = SandstoneMID ? static_cast<UMaterialInterface*>(SandstoneMID) : static_cast<UMaterialInterface*>(RockMID);

	struct FMassifSpec { float OpenW, OpenH, TotalH, DepthCrown, DepthFoot, LegW, NoiseAmp; };
	ArchBases.Reset();
	ArchInfos.Reset();
	// One massif at (X, Y) with the given yaw; base Z = the lower of the two feet (the legs extend
	// 0.25 * TotalH below z = 0 so the higher foot buries into the slope instead of floating).
	auto PlaceMassif = [&](const FMassifSpec& S, float X, float Y, float Yaw)
	{
		TArray<FVector> Crown;
		SWProc::FMeshData Arch;
		SWProc::BuildMassifArch(Rng, S.OpenW, S.OpenH, S.TotalH, S.DepthCrown, S.DepthFoot, S.LegW, S.NoiseAmp, Arch, &Crown);
		const float FootX = 0.5f * S.OpenW + 0.5f * S.LegW;   // leg centreline, local X
		const FVector Along = FVector(1.f, 0.f, 0.f).RotateAngleAxis(Yaw, FVector::UpVector);
		const FVector FootA = FVector(X, Y, 0.f) + Along * FootX;
		const FVector FootB = FVector(X, Y, 0.f) - Along * FootX;
		const float Z = FMath::Min(SWProc::TerrainHeight(L, FootA.X, FootA.Y), SWProc::TerrainHeight(L, FootB.X, FootB.Y));
		const FTransform Xf(FRotator(0.f, Yaw, 0.f), FVector(X, Y, Z));
		SWProc::FMeshData Placed;
		Placed.Append(Arch, Xf);
		AddSection(Placed, ArchMat);
		ArchBases.Add(FootA);
		ArchBases.Add(FootB);
		FSWArchInfo Info;
		Info.Center = FVector(X, Y, Z); Info.MajorR = FootX; Info.MinorR = 0.5f * S.LegW; Info.Yaw = Yaw;
		Info.OpenW = S.OpenW; Info.OpenH = S.OpenH; Info.TotalH = S.TotalH; Info.LegW = S.LegW;
		Info.DepthCrown = S.DepthCrown; Info.DepthFoot = S.DepthFoot;
		Info.CrownPoints.Reserve(Crown.Num());
		for (const FVector& P : Crown) Info.CrownPoints.Add(Xf.TransformPosition(P));
		ArchInfos.Add(MoveTemp(Info));
	};
	const FMassifSpec Hero { 2600.f, 1800.f, 2700.f, 900.f, 1500.f, 900.f, 240.f };
	const FMassifSpec Twin { 1700.f, 1300.f, 2100.f, 800.f, 1200.f, 750.f, 190.f };
	const FMassifSpec Far  { 1400.f, 1000.f, 1600.f, 650.f, 1000.f, 600.f, 160.f };
	if (L.ArchCount >= 1)
	{
		const float X = Arena + 2300.f;
		PlaceMassif(Hero, X, SWProc::RiverCenterY(L, X), 90.f);
	}
	if (L.ArchCount >= 2)
	{
		// Twin: the pair spans local X (after yaw 100 mostly world Y); adjacent legs overlap by 0.6 * LegW,
		// so the leg centres sit 0.4 * LegW apart and each massif is offset by OpenW/2 + 0.7 * LegW.
		const float Yaw = 118.f;   // seen from the start camera the pair sits clear of the hero span, to its right and nearer
		const FVector Along = FVector(1.f, 0.f, 0.f).RotateAngleAxis(Yaw, FVector::UpVector);
		const float C = 0.5f * Twin.OpenW + 0.7f * Twin.LegW;
		const FVector Ctr(Arena + 600.f, 4500.f, 0.f);
		PlaceMassif(Twin, Ctr.X - Along.X * C, Ctr.Y - Along.Y * C, Yaw);
		PlaceMassif(Twin, Ctr.X + Along.X * C, Ctr.Y + Along.Y * C, Yaw);
	}
	if (L.ArchCount >= 3)
	{
		PlaceMassif(Far, Arena + 5200.f, -3000.f, 70.f);
	}

	// Boulders: large on the slopes, small along the arena rim and river banks.
	// Skipped when imported boulders dress the valley instead.
	SWProc::FMeshData Rocks;
	const int32 ProcRocks = ImportedBoulders > 0 ? 0 : L.RockCount;
	for (int32 i = 0; i < ProcRocks; ++i)
	{
		const bool bLarge = i < ProcRocks * 0.4f;
		const float R = bLarge ? Rng.FRandRange(260.f, 700.f) : Rng.FRandRange(50.f, 170.f);
		float X, Y;
		if (bLarge)
		{
			X = Rng.FRandRange(-0.7f * Half, 0.7f * Half);
			Y = (Rng.FRand() < 0.5f ? -1.f : 1.f) * Rng.FRandRange(0.65f, 1.05f) * L.ValleyHalfWidth;
		}
		else
		{
			// Ring just outside the arena, or on a river bank inside it.
			if (Rng.FRand() < 0.6f)
			{
				const float Ang = Rng.FRandRange(0.f, 2.f * PI);
				const float D = Arena + Rng.FRandRange(150.f, 1400.f);
				X = FMath::Cos(Ang) * D; Y = FMath::Sin(Ang) * D;
			}
			else
			{
				X = Rng.FRandRange(-Arena, Arena);
				Y = SWProc::RiverCenterY(L, X) + (Rng.FRand() < 0.5f ? -1.f : 1.f) * Rng.FRandRange(L.RiverWidth * 1.3f, L.RiverWidth * 2.2f);
			}
		}
		const float Z = SWProc::TerrainHeight(L, X, Y) - 0.25f * R;
		SWProc::FMeshData Rock;
		SWProc::BuildRock(Rng, R, 0.22f, Rock);
		Rocks.Append(Rock, FTransform(FRotator(Rng.FRandRange(-12.f, 12.f), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(-12.f, 12.f)), FVector(X, Y, Z)));
	}
	if (Rocks.Verts.Num() > 0) AddSection(Rocks, RockMID);
}

// ---------------------------------------------------------------------------
// Imported-asset dressing
// ---------------------------------------------------------------------------

float ASWEnvironment::LongestExtent(const UStaticMesh* Mesh)
{
	if (!Mesh) return 100.f;
	const FVector Size = Mesh->GetBoundingBox().GetSize();
	return FMath::Max3(Size.X, Size.Y, Size.Z);
}

TArray<UStaticMesh*> ASWEnvironment::FindMeshes(const FString& AssetPath, const TArray<FString>& Stems) const
{
	TArray<UStaticMesh*> Out;
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	// In -game runs the registry scan is still asynchronous at BeginPlay; force this path now.
	Registry.ScanPathsSynchronous({ AssetPath }, /*bForceRescan*/ false);
	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(*AssetPath), Assets, /*bRecursive*/ true);
	UE_LOG(LogSymbioticWorld, Log, TEXT("Asset registry: %d assets under %s"), Assets.Num(), *AssetPath);
	for (const FString& Stem : Stems)
	{
		const FAssetData* Best = nullptr;
		for (const FAssetData& A : Assets)
		{
			if (A.AssetClassPath.GetAssetName() != TEXT("StaticMesh")) continue;
			if (A.PackagePath.ToString().Contains(TEXT("/_GENERATED/"))) continue;   // ED stub variants share names with the real scans
			const FString Name = A.AssetName.ToString();
			if (Name.Equals(Stem, ESearchCase::IgnoreCase)) { Best = &A; break; }       // exact name wins
			if (!Best && Name.StartsWith(Stem, ESearchCase::IgnoreCase)) Best = &A;     // else first prefix match
		}
		if (Best)
		{
			if (UStaticMesh* M = Cast<UStaticMesh>(Best->GetAsset())) Out.Add(M);
		}
		else
		{
			UE_LOG(LogSymbioticWorld, Warning, TEXT("Role mesh '%s' not found under %s"), *Stem, *AssetPath);
		}
	}
	return Out;
}

UHierarchicalInstancedStaticMeshComponent* ASWEnvironment::MakeInstanced(UStaticMesh* Mesh, bool bProjectMaterial)
{
	UHierarchicalInstancedStaticMeshComponent* C = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	C->SetupAttachment(Root);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(true);
	C->SetMobility(EComponentMobility::Movable);   // Root is Movable; a Static child cannot attach ("AttachTo: ... is not static" x36)
	if (bProjectMaterial && RockMID)
	{
		for (int32 i = 0; i < Mesh->GetStaticMaterials().Num(); ++i) C->SetMaterial(i, RockMID);
	}
	else
	{
		BindScanMaterials(C, Mesh);
	}
	C->RegisterComponent();
	Instanced.Add(C);
	return C;
}

// The Electric Dreams scan meshes ship with instances of M_MS_Default_Material_VT_DynamicLayering, whose
// master depends on sample-only inputs and renders black + glossy here. Each instance still carries the
// texture parameters "Albedo", "Normal" and "DR" (packed displacement R, roughness G; some are DpRF with
// the same first two channels), all virtual textures. For every opaque one-sided slot whose Albedo is a VT
// we create a dynamic instance of /Game/Materials/M_SW_Scan (Tools/make_materials.py), copy the three
// textures in and bind it. Poly Haven meshes (non-VT textures), masked foliage cards and two-sided
// materials are left untouched; if M_SW_Scan is missing nothing changes.
void ASWEnvironment::BindScanMaterials(UHierarchicalInstancedStaticMeshComponent* C, UStaticMesh* Mesh)
{
	if (!C || !Mesh) return;
	if (!bScanBaseSearched)
	{
		bScanBaseSearched = true;
		ScanBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SW_Scan.M_SW_Scan"));
		if (!ScanBase) UE_LOG(LogSymbioticWorld, Warning, TEXT("M_SW_Scan missing (run Tools/make_materials.py); scan meshes keep their own materials"));
	}
	if (!ScanBase) return;
	const TArray<FStaticMaterial>& Slots = Mesh->GetStaticMaterials();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		UMaterialInstance* MI = Cast<UMaterialInstance>(Slots[i].MaterialInterface);
		if (!MI) continue;
		if (MI->GetBlendMode() != BLEND_Opaque || MI->IsTwoSided()) continue;   // M_SW_Scan is opaque, one-sided
		UTexture* Albedo = nullptr; UTexture* Normal = nullptr; UTexture* DR = nullptr;
		MI->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Albedo")), Albedo);
		MI->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Normal")), Normal);
		MI->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("DR")), DR);
		if (!Albedo || !Albedo->VirtualTextureStreaming) continue;
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(ScanBase, this);
		MID->SetTextureParameterValue(TEXT("Albedo"), Albedo);
		if (Normal) MID->SetTextureParameterValue(TEXT("Normal"), Normal);
		if (DR) MID->SetTextureParameterValue(TEXT("DR"), DR);
		C->SetMaterial(i, MID);
		ScanBoundSlots++;
		if (!ScanBoundMeshes.Contains(Mesh))
		{
			ScanBoundMeshes.Add(Mesh);
			UE_LOG(LogSymbioticWorld, Log, TEXT("Scan material bound: %s (albedo %s)"), *Mesh->GetName(), *Albedo->GetName());
		}
	}
}

bool ASWEnvironment::BuildImported(const FSWLookSettings& L)
{
	// 'mountainside' is a thin slab that reads as a spire from most angles; left out.
	const TArray<UStaticMesh*> Cliffs = FindMeshes(Roles.Root, Roles.Cliff);
	const TArray<UStaticMesh*> Boulders = FindMeshes(Roles.Root, Roles.Boulder);
	const TArray<UStaticMesh*> Cover = FindMeshes(Roles.Root, Roles.Groundcover);
	UE_LOG(LogSymbioticWorld, Log, TEXT("Roles resolved: cliffs %d/%d, boulders %d/%d, groundcover %d/%d"),
		Cliffs.Num(), Roles.Cliff.Num(), Boulders.Num(), Roles.Boulder.Num(), Cover.Num(), Roles.Groundcover.Num());
	if (Cliffs.Num() + Boulders.Num() + Cover.Num() == 0) return false;

	// Rock material must exist before MakeInstanced can override with it.
	if (!RockMID)
	{
		if (UMaterialInterface* Base = LoadMat(TEXT("/Game/Materials/M_SW_Rock.M_SW_Rock")))
		{
			RockMID = UMaterialInstanceDynamic::Create(Base, this);
			RockMID->SetVectorParameterValue(TEXT("RockColor"), L.RockColor * 1.05f);
			RockMID->SetVectorParameterValue(TEXT("MossColor"), L.MossColor);
		}
	}

	FRandomStream Rng(L.LookSeed * 7 + 3);
	const float Half = L.TerrainHalfSize;
	const float Arena = Manager ? Manager->GetSettings().WorldHalfSize : 4500.f;

	auto Place = [&](UHierarchicalInstancedStaticMeshComponent* C, float X, float Y, float Size, float Sink, float PitchJitter)
	{
		const float Extent = LongestExtent(C->GetStaticMesh());
		const float S = Size / FMath::Max(Extent, 1.f);
		const float Z = SWProc::TerrainHeight(L, X, Y) - Sink * Size;
		const FRotator R(Rng.FRandRange(-PitchJitter, PitchJitter), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(-PitchJitter, PitchJitter));
		C->AddInstance(FTransform(R, FVector(X, Y, Z), FVector(S)), /*bWorldSpace*/ true);
	};

	// Cliffs: a light random scatter on the slopes and the far end, each piece at a uniform 0.6..1.1 of its
	// native size (the scans are already metric; CliffSizeMin/Max are no longer read), sunk 30% of its height.
	CliffMeshes = Cliffs;
	if (Cliffs.Num() > 0)
	{
		TArray<UHierarchicalInstancedStaticMeshComponent*> Comps;
		for (UStaticMesh* M : Cliffs) Comps.Add(MakeInstanced(M, L.bImportedRocksUseProjectMaterial));
		for (int32 i = 0; i < L.CliffCount; ++i)
		{
			float X, Y;
			if (i % 4 == 3)
			{
				X = Rng.FRandRange(0.62f * Half, 0.85f * Half);     // far end
				Y = Rng.FRandRange(-0.9f * L.ValleyHalfWidth, 0.9f * L.ValleyHalfWidth);
			}
			else
			{
				X = Rng.FRandRange(-0.75f * Half, 0.75f * Half);
				Y = (Rng.FRand() < 0.5f ? -1.f : 1.f) * Rng.FRandRange(0.58f, 0.98f) * L.ValleyHalfWidth;
			}
			// Keep the start-camera corridor (behind the arena, -X side) clear.
			if (X < -(Arena + 800.f) && FMath::Abs(Y) < 4000.f) continue;
			UHierarchicalInstancedStaticMeshComponent* C = Comps[i % Comps.Num()];
			const FBox Box = C->GetStaticMesh()->GetBoundingBox();
			const float S = Rng.FRandRange(0.6f, 1.1f);
			const float Z = SWProc::TerrainHeight(L, X, Y) - 0.30f * Box.GetSize().Z * S - Box.Min.Z * S;
			const FRotator R(Rng.FRandRange(-5.f, 5.f), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(-5.f, 5.f));
			C->AddInstance(FTransform(R, FVector(X, Y, Z), FVector(S)), /*bWorldSpace*/ true);
			ImportedCliffs++;
		}
	}
	BuildCliffWalls(L);

	// Boulders: ring around the arena and the river banks, a few on the floor.
	if (Boulders.Num() > 0)
	{
		TArray<UHierarchicalInstancedStaticMeshComponent*> Comps;
		for (UStaticMesh* M : Boulders) Comps.Add(MakeInstanced(M, L.bImportedRocksUseProjectMaterial));
		for (int32 i = 0; i < L.ImportedBoulderCount; ++i)
		{
			float X, Y;
			const float U = Rng.FRand();
			if (U < 0.5f)
			{
				const float Ang = Rng.FRandRange(0.f, 2.f * PI);
				const float D = Arena + Rng.FRandRange(100.f, 1600.f);
				X = FMath::Cos(Ang) * D; Y = FMath::Sin(Ang) * D;
			}
			else if (U < 0.8f)
			{
				X = Rng.FRandRange(-Arena, Arena);
				Y = SWProc::RiverCenterY(L, X) + (Rng.FRand() < 0.5f ? -1.f : 1.f) * Rng.FRandRange(L.RiverWidth * 1.4f, L.RiverWidth * 2.4f);
			}
			else
			{
				X = Rng.FRandRange(-Arena, Arena); Y = Rng.FRandRange(-Arena, Arena);
			}
			Place(Comps[i % Comps.Num()], X, Y, Rng.FRandRange(L.BoulderSizeMin, L.BoulderSizeMax), 0.22f, 15.f);
			ImportedBoulders++;
		}
	}

	// Groundcover: denser in the wetland band, none in the channel, thinning with distance from water.
	if (Cover.Num() > 0)
	{
		TArray<UHierarchicalInstancedStaticMeshComponent*> Comps;
		for (UStaticMesh* M : Cover)
		{
			UHierarchicalInstancedStaticMeshComponent* C = MakeInstanced(M, false);
			C->SetCastShadow(false);   // hundreds of small cards: shadows are not worth the cost
			Comps.Add(C);
		}
		GroundcoverComps = Comps;   // reused for the arch crowns in BuildImportedFeatures
		const float R = Arena + 1800.f;
		int32 Tries = 0;
		while (ImportedGroundcover < L.GroundcoverCount && Tries++ < L.GroundcoverCount * 6)
		{
			const float X = Rng.FRandRange(-R, R), Y = Rng.FRandRange(-R, R);
			const float H = SWProc::TerrainHeight(L, X, Y);
			if (H < L.WaterLevel + 6.f) continue;                                // under water
			const float AboveWater = H - L.WaterLevel;
			const float Wet = FMath::Clamp(1.f - AboveWater / (L.WetlandBand * 3.f), 0.f, 1.f);
			const float Accept = 0.12f + 0.88f * Wet;                             // lush near water, sparse on dry floor
			if (Rng.FRand() > Accept) continue;
			const int32 Idx = Wet > 0.5f ? Rng.RandRange(0, Comps.Num() - 1) : Rng.RandRange(0, FMath::Max(Comps.Num() / 2, 1) - 1);
			Place(Comps[Idx], X, Y, Rng.FRandRange(L.GroundcoverSizeMin, L.GroundcoverSizeMax) * (0.8f + 0.5f * Wet), 0.02f, 4.f);
			ImportedGroundcover++;
		}
	}
	return true;
}

// Stratified sandstone walls lining both rims (plate 1), a far-end wall with a gap on the river for the
// low sun, and tall pinnacles. Wall pieces use uniform multipliers on the mesh's native size so the hero
// piece (SM_MassiveSandstoneCliff_05, ~42 m long) becomes an 85-110 m cliff; the mesh's local X is its
// long axis and is laid along the rim direction. Own seeded stream so the scatter above is unchanged.
void ASWEnvironment::BuildCliffWalls(const FSWLookSettings& L)
{
	if (CliffMeshes.Num() == 0) return;
	FRandomStream Rng(L.LookSeed * 17 + 11);
	const float Half = L.TerrainHalfSize;
	const float VHW = L.ValleyHalfWidth;
	const float Arena = Manager ? Manager->GetSettings().WorldHalfSize : 4500.f;

	// Which mesh plays which part, by name; bounds decide when a name is absent.
	auto ByStem = [&](const TCHAR* Stem) -> UStaticMesh*
	{
		for (UStaticMesh* M : CliffMeshes) if (M && M->GetName().Contains(Stem, ESearchCase::IgnoreCase)) return M;
		return nullptr;
	};
	auto Largest = [&](bool bTallest) -> UStaticMesh*
	{
		UStaticMesh* Best = nullptr; float BestV = -1.f;
		for (UStaticMesh* M : CliffMeshes)
		{
			if (!M) continue;
			const FVector Sz = M->GetBoundingBox().GetSize();
			const float V = bTallest ? Sz.Z : FMath::Max3(Sz.X, Sz.Y, Sz.Z);
			if (V > BestV) { BestV = V; Best = M; }
		}
		return Best;
	};
	UStaticMesh* Hero = ByStem(TEXT("MassiveSandstoneCliff_05")); if (!Hero) Hero = Largest(false);
	UStaticMesh* Second = ByStem(TEXT("MassiveSandstoneCliff_02")); if (!Second) Second = Hero;
	TArray<UStaticMesh*> Small;
	if (UStaticMesh* M = ByStem(TEXT("HugeSandstoneCliff_01"))) Small.Add(M);
	if (UStaticMesh* M = ByStem(TEXT("HugeSandstoneCliff_06"))) Small.Add(M);
	if (Small.Num() == 0) Small.Add(Second);
	if (!Hero) return;

	TMap<UStaticMesh*, UHierarchicalInstancedStaticMeshComponent*> Comps;
	auto CompFor = [&](UStaticMesh* M) -> UHierarchicalInstancedStaticMeshComponent*
	{
		if (UHierarchicalInstancedStaticMeshComponent** Found = Comps.Find(M)) return *Found;
		UHierarchicalInstancedStaticMeshComponent* C = MakeInstanced(M, L.bImportedRocksUseProjectMaterial);
		Comps.Add(M, C);
		return C;
	};
	auto PickWallMesh = [&]() -> UStaticMesh*
	{
		const float U = Rng.FRand();
		if (U < 0.5f) return Hero;
		if (U < 0.8f) return Second;
		return Small[Rng.RandRange(0, Small.Num() - 1)];
	};
	// One piece: uniform scale S on the native size, base sunk Sink * scaled height below the terrain.
	auto PlaceWall = [&](UStaticMesh* M, float X, float Y, float BaseYaw, float S)
	{
		const FBox Box = M->GetBoundingBox();
		const float Z = SWProc::TerrainHeight(L, X, Y) - L.CliffWallSink * Box.GetSize().Z * S - Box.Min.Z * S;
		const FRotator R(Rng.FRandRange(-L.CliffWallPitchJitter, L.CliffWallPitchJitter),
			BaseYaw + Rng.FRandRange(-L.CliffWallYawJitter, L.CliffWallYawJitter),
			Rng.FRandRange(-0.5f * L.CliffWallPitchJitter, 0.5f * L.CliffWallPitchJitter));
		CompFor(M)->AddInstance(FTransform(R, FVector(X, Y, Z), FVector(S)), /*bWorldSpace*/ true);
	};
	auto LengthOf = [](const UStaticMesh* M, float S) { return M->GetBoundingBox().GetSize().X * S; };

	if (L.bCliffWalls)
	{
		// Rim rows: march along X on each side; row r sits further out and larger; odd rows start half a spacing later.
		const float RefLen = LengthOf(Hero, 0.5f * (L.CliffWallScaleMin + L.CliffWallScaleMax));
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const float s = (Side == 0) ? -1.f : 1.f;
			for (int32 r = 0; r < L.CliffWallRows; ++r)
			{
				const float RowScale = 1.f + 0.25f * r;
				const float YFrac = L.CliffWallYFrac + r * L.CliffWallRowStep;
				const float XEnd = L.CliffWallXMax * Half;
				float X = L.CliffWallXMin * Half + ((r % 2 == 1) ? 0.5f * L.CliffWallOverlap * RefLen * RowScale : 0.f);
				int32 Guard = 0;
				while (X < XEnd && Guard++ < 400)
				{
					UStaticMesh* M = PickWallMesh();
					const float S = Rng.FRandRange(L.CliffWallScaleMin, L.CliffWallScaleMax) * RowScale;
					const float Y = s * YFrac * VHW + Rng.FRandRange(-0.04f, 0.04f) * VHW;
					// Keep the start-camera corridor (behind the arena, -X side) clear.
					if (!(X < -(Arena + 800.f) && FMath::Abs(Y) < 4000.f))
					{
						// Long axis along X; local +Y faces the valley centre (yaw 180 on the +Y side).
						PlaceWall(M, X, Y, s > 0.f ? 180.f : 0.f, S);
						ImportedWallPieces++;
					}
					X += L.CliffWallOverlap * LengthOf(M, S);
				}
			}
		}
	}

	// Far-end wall: across the valley at +X, grown outward from both edges of the sun gap so the gap
	// around the river stays exactly 2 * CliffWallFarGap wide (bounds) behind the hero arch.
	if (L.bCliffWalls && L.bCliffWallFarEnd)
	{
		const float X0 = L.CliffWallXMax * Half + 1500.f;
		const float RiverY = SWProc::RiverCenterY(L, X0);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const float d = (Side == 0) ? -1.f : 1.f;
			float Edge = RiverY + d * L.CliffWallFarGap;   // near end of the next piece's bounds
			int32 Guard = 0;
			while (FMath::Abs(Edge) < VHW && Guard++ < 200)
			{
				UStaticMesh* M = PickWallMesh();
				const float S = Rng.FRandRange(L.CliffWallScaleMin, L.CliffWallScaleMax) * 1.15f;
				const float Len = LengthOf(M, S);
				const float Y = Edge + d * 0.5f * Len;
				const float X = X0 + Rng.FRandRange(-0.04f, 0.04f) * VHW;
				PlaceWall(M, X, Y, 90.f, S);
				ImportedWallPieces++;
				ImportedWallFarEnd++;
				Edge += d * L.CliffWallOverlap * Len;
			}
		}
	}

	// Pinnacles: the tall slab at a uniform 1.0..1.5 of its native size (the earlier X,Y x0.75 squeeze read
	// as planks). The last two flank the far sun gap; the rest sit on the mid slopes ahead of the arena.
	if (L.PinnacleCount > 0)
	{
		UStaticMesh* Spire = ByStem(TEXT("HugeSandstoneCliff_03")); if (!Spire) Spire = Largest(true);
		if (Spire)
		{
			UHierarchicalInstancedStaticMeshComponent* C = MakeInstanced(Spire, L.bImportedRocksUseProjectMaterial);
			const FBox Box = Spire->GetBoundingBox();
			const int32 NumFlank = FMath::Min(2, L.PinnacleCount);
			const int32 NumMid = L.PinnacleCount - NumFlank;
			for (int32 i = 0; i < L.PinnacleCount; ++i)
			{
				float X, Y;
				if (i < NumMid)
				{
					const float s = (i % 2 == 0) ? 1.f : -1.f;
					const float F = NumMid > 1 ? i / (float)(NumMid - 1) : 0.5f;
					X = FMath::Lerp(Arena + 300.f, 0.55f * Half - 300.f, F) + Rng.FRandRange(-250.f, 250.f);
					Y = s * Rng.FRandRange(0.48f, 0.60f) * VHW;
				}
				else
				{
					const float s = ((i - NumMid) % 2 == 0) ? -1.f : 1.f;
					X = L.CliffWallXMax * Half + 300.f;
					Y = SWProc::RiverCenterY(L, X) + s * (L.CliffWallFarGap + 900.f);
				}
				const float S = Rng.FRandRange(L.PinnacleScaleMin, L.PinnacleScaleMax);
				const float Z = SWProc::TerrainHeight(L, X, Y) - 0.08f * Box.GetSize().Z * S - Box.Min.Z * S;
				const FRotator R(Rng.FRandRange(-2.f, 2.f), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(-2.f, 2.f));
				C->AddInstance(FTransform(R, FVector(X, Y, Z), FVector(S)), true);
				ImportedPinnacles++;
			}
		}
	}
}

void ASWEnvironment::BuildLighting(const FSWLookSettings& L)
{
	UWorld* World = GetWorld();
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.Owner = this;

	// Sun: low, straight down the +X valley axis (yaw ~180 => light travels toward -X). At a low SunPitch it streams
	// through the far-end wall gap and its disc sits behind the hero arch the start camera faces; the warm inscatter is
	// kept narrow around it (FogDirectionalExponent) so the rest of the frame is lit by the cool sky and the fill light below.
	Sun = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FVector(0.f, 0.f, 3000.f), FRotator(L.SunPitch, L.SunYaw, 0.f), SP);
	if (Sun)
	{
		if (UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			C->SetMobility(EComponentMobility::Movable);
			C->SetIntensity(L.SunIntensity);
			C->SetUseTemperature(true);
			C->SetTemperature(L.SunTemperature);
			C->SetLightColor(L.SunColor);
			C->SetAtmosphereSunLight(true);
			C->SetAtmosphereSunDiskColorScale(FLinearColor::White);   // the sky atmosphere draws the disc at LightSourceAngle; nothing scales it down
			C->SetLightSourceAngle(L.SunSourceAngle);
			C->SetVolumetricScatteringIntensity(L.SunVolumetricScattering);
			C->SetCastVolumetricShadow(true);
			C->SetEnableLightShaftBloom(true);
			C->SetBloomScale(L.SunBloomScale);
			C->SetBloomThreshold(L.SunBloomThreshold);
			C->SetBloomTint(FColor(255, 215, 170));
			C->bCastCloudShadows = true;
			C->CloudShadowStrength = L.CloudShadowStrength;
			C->SetDynamicShadowDistanceMovableLight(L.SunShadowDistance);
			C->SetCastShadows(true);
			C->ForwardShadingPriority = 1;   // the sun wins forward/translucent/volumetric lighting over the fill
			C->MarkRenderStateDirty();
		}
	}

	// Fill: shadowless cool directional light from the camera side (the cinematic cheat) so the valley floor
	// reads even where the low sun is blocked by terrain. Not an atmosphere sun, no volumetrics, no GI.
	if (L.bFillLight)
	{
		Fill = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FVector(0.f, 0.f, 3000.f), FRotator(L.FillPitch, L.FillYaw, 0.f), SP);
		if (Fill)
		{
			if (UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(Fill->GetLightComponent()))
			{
				C->SetMobility(EComponentMobility::Movable);
				C->SetIntensity(L.FillIntensity);
				C->SetUseTemperature(true);
				C->SetTemperature(L.FillTemperature);
				C->SetLightColor(L.FillColor);
				C->SetAtmosphereSunLight(false);
				C->SetCastShadows(false);
				C->SetCastVolumetricShadow(false);
				C->SetVolumetricScatteringIntensity(0.f);
				C->SetEnableLightShaftBloom(false);
				C->bCastCloudShadows = false;
				C->SetAffectGlobalIllumination(false);
				C->ForwardShadingPriority = 0;   // below the sun: silences the "competing directional lights" warning
				C->MarkRenderStateDirty();
			}
		}
	}

	// Atmosphere: teal-shifted Rayleigh (teal fill in shadow), warm Mie haze around the low sun, aerial perspective scaled up for a small valley.
	Atmosphere = World->SpawnActor<ASkyAtmosphere>(ASkyAtmosphere::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);
	if (Atmosphere)
	{
		if (USkyAtmosphereComponent* A = Atmosphere->GetComponent())
		{
			A->SetRayleighScattering(L.RayleighColor);
			A->SetRayleighScatteringScale(L.RayleighScale);
			A->SetMieScatteringScale(L.MieScale);
			A->SetMieAbsorptionScale(0.0006f);
			A->SetMieAnisotropy(0.85f);
			A->SetGroundAlbedo(FColor(60, 90, 90));
			A->SetAerialPespectiveViewDistanceScale(L.AerialPerspectiveScale);   // engine spelling
			A->SetHeightFogContribution(1.f);
		}
	}

	if (L.bClouds)
	{
		Clouds = World->SpawnActor<AVolumetricCloud>(AVolumetricCloud::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);
		if (Clouds)
		{
			if (UVolumetricCloudComponent* CC = Clouds->FindComponentByClass<UVolumetricCloudComponent>())
			{
				CC->SetLayerBottomAltitude(1.5f);
				CC->SetLayerHeight(4.f);
				CC->SetViewSampleCountScale(L.CloudSampleScale);
				CC->SetShadowViewSampleCountScale(0.5f);
				CC->SetSkyLightCloudBottomOcclusion(0.5f);
				// Parameter names read from the asset's name table: Cloud_GlobalCoverage, Cloud_GlobalDensity, Cloud_AlbedoColor.
				if (UMaterialInterface* CM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst")))
				{
					CloudMID = UMaterialInstanceDynamic::Create(CM, this);
					CloudMID->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), L.CloudCoverage);
					CloudMID->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), L.CloudDensity);
					CC->SetMaterial(CloudMID);
				}
			}
		}
	}

	// Sky light: real-time capture of the atmosphere, plus a teal lower hemisphere so shadowed ground is not black.
	Sky = World->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FVector(0.f, 0.f, 800.f), FRotator::ZeroRotator, SP);
	if (Sky && Sky->GetLightComponent())
	{
		USkyLightComponent* SL = Sky->GetLightComponent();
		SL->SetMobility(EComponentMobility::Movable);
		SL->SetRealTimeCaptureEnabled(true);
		SL->SetIntensity(L.SkyLightIntensity);
		SL->bLowerHemisphereIsBlack = false;
		SL->SetLowerHemisphereColor(L.SkyLowerHemisphere);
		SL->SetVolumetricScatteringIntensity(1.f);
		SL->MarkRenderStateDirty();
	}

	// Fog: actor sits at water level so the second layer pools in the wetland; directional inscattering starts at 0 so the sun colours the haze.
	Fog = World->SpawnActor<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), FVector(0.f, 0.f, L.WaterLevel), FRotator::ZeroRotator, SP);
	if (Fog && Fog->GetComponent())
	{
		UExponentialHeightFogComponent* F = Fog->GetComponent();
		F->SetFogDensity(L.FogDensity);
		F->SetFogHeightFalloff(L.FogHeightFalloff);
		F->SetFogInscatteringColor(L.FogColor);
		F->SetSkyAtmosphereAmbientContributionColorScale(L.FogAmbientScale);
		F->SetDirectionalInscatteringColor(L.FogDirectionalColor);
		F->SetDirectionalInscatteringExponent(L.FogDirectionalExponent);
		F->SetDirectionalInscatteringStartDistance(L.FogDirectionalStartDistance);
		F->SetStartDistance(L.FogStartDistance);
		F->SetFogMaxOpacity(L.FogMaxOpacity);
		FExponentialHeightFogData Second;
		Second.FogDensity = L.FogSecondDensity;
		Second.FogHeightFalloff = L.FogSecondFalloff;
		Second.FogHeightOffset = L.FogSecondHeightOffset;
		F->SetSecondFogData(Second);
		F->SetVolumetricFog(true);
		F->SetVolumetricFogScatteringDistribution(0.35f);
		F->SetVolumetricFogAlbedo(L.VolumetricFogAlbedo.ToFColor(false));
		F->SetVolumetricFogExtinctionScale(L.VolumetricFogExtinction);
		F->SetVolumetricFogDistance(20000.f);
		F->SetVolumetricFogStartDistance(0.f);
	}

	// Post: fixed exposure (auto exposure is off project-wide), teal shadows / amber highlights, warm white point, grain, flare.
	PostProcess = World->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);
	if (PostProcess)
	{
		PostProcess->bUnbound = true;
		FPostProcessSettings& S = PostProcess->Settings;
		S.bOverride_BloomIntensity = true;      S.BloomIntensity = L.BloomIntensity;
		S.bOverride_BloomThreshold = true;      S.BloomThreshold = L.BloomThreshold;
		S.bOverride_AutoExposureBias = true;    S.AutoExposureBias = L.ExposureBias;
		S.bOverride_ColorSaturation = true;     S.ColorSaturation = FVector4(L.Saturation, L.Saturation, L.Saturation, 1.f);
		S.bOverride_ColorContrast = true;       S.ColorContrast = FVector4(L.Contrast, L.Contrast, L.Contrast, 1.f);
		S.bOverride_ColorGainShadows = true;    S.ColorGainShadows = FVector4(L.ShadowTint.R, L.ShadowTint.G, L.ShadowTint.B, 1.f);
		S.bOverride_ColorGainHighlights = true; S.ColorGainHighlights = FVector4(L.HighlightTint.R, L.HighlightTint.G, L.HighlightTint.B, 1.f);
		S.bOverride_VignetteIntensity = true;   S.VignetteIntensity = L.Vignette;
		S.bOverride_WhiteTemp = true;           S.WhiteTemp = L.WhiteTemp;
		S.bOverride_FilmGrainIntensity = true;  S.FilmGrainIntensity = L.FilmGrain;
		S.bOverride_LensFlareIntensity = true;  S.LensFlareIntensity = L.LensFlare;
		S.bOverride_AmbientOcclusionIntensity = true; S.AmbientOcclusionIntensity = 0.6f;
		if (L.LutIntensity > 0.f)
		{
			if (UTexture* Lut = LoadObject<UTexture>(nullptr, TEXT("/Engine/MapTemplates/lut/LUT_Morning.LUT_Morning")))
			{
				S.bOverride_ColorGradingLUT = true;       S.ColorGradingLUT = Lut;
				S.bOverride_ColorGradingIntensity = true; S.ColorGradingIntensity = L.LutIntensity;
			}
		}
	}
}

void ASWEnvironment::RunConsoleCommands(const FSWLookSettings& L)
{
	if (L.ConsoleCommands.IsEmpty()) return;
	TArray<FString> Cmds;
	L.ConsoleCommands.ParseIntoArray(Cmds, TEXT("|"), true);
	for (FString Cmd : Cmds)
	{
		Cmd.ReplaceInline(TEXT("="), TEXT(" "));
		Cmd.TrimStartAndEndInline();
		if (Cmd.IsEmpty()) continue;
		UE_LOG(LogSymbioticWorld, Log, TEXT("Console: %s"), *Cmd);
		UKismetSystemLibrary::ExecuteConsoleCommand(GetWorld(), Cmd, nullptr);
	}
}

void ASWEnvironment::ApplyDrought(const FSWLookSettings& L, float F)
{
	if (Sun)
	{
		if (UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			C->SetLightColor(FMath::Lerp(L.SunColor, L.DroughtSunColor, F));
			C->SetTemperature(FMath::Lerp(L.SunTemperature, L.DroughtSunTemperature, F));
		}
		Sun->SetActorRotation(FRotator(L.SunPitch + L.DroughtSunPitchDrop * F, L.SunYaw, 0.f));
	}
	if (Atmosphere)
	{
		if (USkyAtmosphereComponent* A = Atmosphere->GetComponent())
		{
			A->SetMieScatteringScale(FMath::Lerp(L.MieScale, L.DroughtMieScale, F));
			A->SetGroundAlbedo(FColor(FMath::RoundToInt(FMath::Lerp(60.f, 120.f, F)), 90, FMath::RoundToInt(FMath::Lerp(90.f, 60.f, F))));
		}
	}
	if (Fog && Fog->GetComponent())
	{
		Fog->GetComponent()->SetFogInscatteringColor(FMath::Lerp(L.FogColor, L.DroughtFogColor, F));
		Fog->GetComponent()->SetFogDensity(L.FogDensity * FMath::Lerp(1.f, L.DroughtFogDensityScale, F));
		Fog->GetComponent()->SetVolumetricFogAlbedo(FMath::Lerp(L.VolumetricFogAlbedo, L.DroughtVolumetricFogAlbedo, F).ToFColor(false));
		Fog->GetComponent()->SetDirectionalInscatteringColor(FMath::Lerp(L.FogDirectionalColor, FLinearColor(1.8f, 1.1f, 0.55f), F));
	}
	if (Water)
	{
		Water->SetRelativeLocation(FVector(0.f, 0.f, L.WaterLevel - L.DroughtWaterDrop * F));
	}
	// Waterfalls (rim + arch sheets and their spray) thin out with the water; mist cards and drips go with them.
	for (int32 i = 0; i < Waterfalls.Num(); ++i)
	{
		if (!Waterfalls[i]) continue;
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(Waterfalls[i]->GetMaterial(0)))
		{
			const float G0 = WaterfallBaseGlow.IsValidIndex(i) ? WaterfallBaseGlow[i] : L.WaterfallGlow;
			M->SetScalarParameterValue(TEXT("Glow"), G0 * (1.f - 0.85f * F));
		}
		Waterfalls[i]->SetVisibility(F < 0.97f);
	}
	for (UStaticMeshComponent* C : MistCards) if (C) C->SetVisibility(F < 0.75f);
	for (UNiagaraComponent* N : MistSystems) if (N) N->SetVisibility(F < 0.75f, true);
	if (TerrainMID) TerrainMID->SetScalarParameterValue(TEXT("Dryness"), F);
	if (RockMID) RockMID->SetScalarParameterValue(TEXT("Dryness"), F * 0.6f);
	if (PostProcess)
	{
		FPostProcessSettings& S = PostProcess->Settings;
		S.WhiteTemp = FMath::Lerp(L.WhiteTemp, L.DroughtWhiteTemp, F);
		const FLinearColor Hi = FMath::Lerp(L.HighlightTint, FLinearColor(1.18f, 0.96f, 0.78f), F);
		S.ColorGainHighlights = FVector4(Hi.R, Hi.G, Hi.B, 1.f);
		S.ColorSaturation = FVector4(L.Saturation, L.Saturation, L.Saturation, 1.f) * FMath::Lerp(1.f, 0.9f, F);
	}
}

void ASWEnvironment::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Manager) return;
	const FSWLookSettings& L = Manager->GetLook();
	const float Target = (Manager->IsDrought() || L.bDroughtPreview) ? 1.f : 0.f;
	const float Rate = 1.f / FMath::Max(L.DroughtBlendSeconds, 0.1f);
	DroughtFactor = FMath::FInterpConstantTo(DroughtFactor, Target, DeltaSeconds, Rate);
	if (FMath::Abs(DroughtFactor - LastAppliedDrought) > 0.004f)
	{
		LastAppliedDrought = DroughtFactor;
		ApplyDrought(L, DroughtFactor);
	}
	TraceRefreshTimer += DeltaSeconds;
	if (TraceRefreshTimer >= 0.1f)   // 10 Hz is plenty for a ground glow
	{
		TraceRefreshTimer = 0.f;
		UpdateTraceOverlay();
	}
}

void ASWEnvironment::EndPlay(const EEndPlayReason::Type Reason)
{
	Super::EndPlay(Reason);
}

// ---------------------------------------------------------------------------
// Roles: manifest or built-in
// ---------------------------------------------------------------------------

void ASWEnvironment::LoadRoles(const FSWLookSettings& L)
{
	Roles = FSWAssetRoles();
	// Built-in Poly Haven roles (what Tools/fetch_polyhaven.py + import_assets.py produce).
	Roles.Root = L.AssetRoot;
	Roles.Cliff = { TEXT("rock_face_01"), TEXT("rock_face_02"), TEXT("coastal_cliff_01"), TEXT("coastal_cliff_02"), TEXT("namaqualand_cliff_01"), TEXT("namaqualand_cliff_02") };
	Roles.Boulder = { TEXT("namaqualand_boulder_02"), TEXT("namaqualand_boulder_03"), TEXT("namaqualand_boulder_04"), TEXT("namaqualand_boulder_05"), TEXT("boulder_01"), TEXT("rock_07"), TEXT("rock_09"), TEXT("rock_moss_set_01"), TEXT("rock_moss_set_02"), TEXT("moss_01") };
	Roles.ArchRock = { TEXT("namaqualand_boulder_02"), TEXT("boulder_01"), TEXT("rock_face_02") };
	Roles.RiverStone = { TEXT("rock_07"), TEXT("rock_09"), TEXT("namaqualand_boulder_05") };
	Roles.Groundcover = { TEXT("fern_02"), TEXT("grass_medium_01"), TEXT("grass_medium_02"), TEXT("moss_01") };
	Roles.Shrub = { TEXT("shrub_01"), TEXT("shrub_02"), TEXT("shrub_03") };

	if (L.AssetManifest.IsEmpty()) return;
	FString Path = L.AssetManifest;
	if (FPaths::IsRelative(Path)) Path = FPaths::Combine(FPaths::ProjectDir(), Path);
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogSymbioticWorld, Log, TEXT("No asset manifest at %s; using built-in roles"), *Path);
		return;
	}
	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		UE_LOG(LogSymbioticWorld, Warning, TEXT("Asset manifest %s is not valid JSON; using built-in roles"), *Path);
		return;
	}
	const TSharedPtr<FJsonObject>* RolesObj = nullptr;
	if (!Json->TryGetObjectField(TEXT("roles"), RolesObj) || !RolesObj || !RolesObj->IsValid()) return;
	auto Names = [&](const TCHAR* Key, TArray<FString>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if ((*RolesObj)->TryGetArrayField(Key, Arr) && Arr)
		{
			TArray<FString> Tmp;
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				FString S; if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) Tmp.Add(S);
			}
			if (Tmp.Num() > 0) Out = Tmp;   // manifest overrides the built-in list only when non-empty
		}
	};
	auto Str = [&](const TCHAR* Key, FString& Out)
	{
		FString S; if ((*RolesObj)->TryGetStringField(Key, S) && !S.IsEmpty() && S != TEXT("null")) Out = S;
	};
	Names(TEXT("cliff"), Roles.Cliff);
	Names(TEXT("arch_rock"), Roles.ArchRock);
	Names(TEXT("boulder"), Roles.Boulder);
	Names(TEXT("river_stone"), Roles.RiverStone);
	Names(TEXT("groundcover"), Roles.Groundcover);
	Names(TEXT("shrub"), Roles.Shrub);
	Names(TEXT("tree"), Roles.Tree);
	Names(TEXT("mud_patch"), Roles.MudPatch);
	Str(TEXT("fog_card_mesh"), Roles.FogCardMesh);
	Str(TEXT("fog_card_material"), Roles.FogCardMaterial);
	Str(TEXT("ground_material"), Roles.GroundMaterial);
	Str(TEXT("ground_material_wet"), Roles.GroundMaterialWet);
	Str(TEXT("water_material"), Roles.WaterMaterial);
	const TSharedPtr<FJsonObject>* Nia = nullptr;
	if ((*RolesObj)->TryGetObjectField(TEXT("niagara"), Nia) && Nia && Nia->IsValid())
	{
		FString S;
		if ((*Nia)->TryGetStringField(TEXT("mist"), S) && S != TEXT("null")) Roles.NiagaraMist = S;
		if ((*Nia)->TryGetStringField(TEXT("motes"), S) && S != TEXT("null")) Roles.NiagaraMotes = S;
		if ((*Nia)->TryGetStringField(TEXT("dust"), S) && S != TEXT("null")) Roles.NiagaraDust = S;
	}
	Roles.Root = TEXT("/Game");   // manifest names may live anywhere in the project
	Roles.bFromManifest = true;
	UE_LOG(LogSymbioticWorld, Log, TEXT("Asset manifest loaded: %s (cliff %d, arch_rock %d, boulder %d, river_stone %d, groundcover %d, shrub %d, tree %d, ground '%s', water '%s', mist '%s')"),
		*Path, Roles.Cliff.Num(), Roles.ArchRock.Num(), Roles.Boulder.Num(), Roles.RiverStone.Num(), Roles.Groundcover.Num(), Roles.Shrub.Num(), Roles.Tree.Num(),
		*Roles.GroundMaterial, *Roles.WaterMaterial, *Roles.NiagaraMist);
}

void ASWEnvironment::BuildImportedFeatures(const FSWLookSettings& L)
{
	FRandomStream Rng(L.LookSeed * 11 + 5);
	const float Arena = Manager ? Manager->GetSettings().WorldHalfSize : 4500.f;

	auto Place = [&](UHierarchicalInstancedStaticMeshComponent* C, float X, float Y, float Size, float Sink, float PitchJitter)
	{
		const float Extent = LongestExtent(C->GetStaticMesh());
		const float S = Size / FMath::Max(Extent, 1.f);
		const float Z = SWProc::TerrainHeight(L, X, Y) - Sink * Size;
		const FRotator R(Rng.FRandRange(-PitchJitter, PitchJitter), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(-PitchJitter, PitchJitter));
		C->AddInstance(FTransform(R, FVector(X, Y, Z), FVector(S)), true);
	};

	// Arch feet: clusters of mid-size rocks so the arches grow out of rubble.
	const TArray<UStaticMesh*> ArchRocks = FindMeshes(Roles.Root, Roles.ArchRock);
	if (ArchRocks.Num() > 0 && ArchBases.Num() > 0)
	{
		TArray<UHierarchicalInstancedStaticMeshComponent*> Comps;
		for (UStaticMesh* M : ArchRocks) Comps.Add(MakeInstanced(M, L.bImportedRocksUseProjectMaterial));
		for (const FVector& B : ArchBases)
		{
			for (int32 k = 0; k < L.ArchRockCount; ++k)
			{
				const float Ang = Rng.FRandRange(0.f, 2.f * PI);
				const float D = Rng.FRandRange(60.f, 420.f);
				Place(Comps[Rng.RandRange(0, Comps.Num() - 1)], B.X + FMath::Cos(Ang) * D, B.Y + FMath::Sin(Ang) * D,
					Rng.FRandRange(220.f, 620.f), 0.3f, 12.f);
				ImportedArchRocks++;
			}
		}

		// Rings: rocks embedded along each torus (local XZ plane rotated by Yaw) so the arches read as cut from
		// cliff mass. Boulders and the small formation only; the flat tundra slab is left to the feet.
		if (L.ArchRockRing > 0)
		{
			TArray<int32> RingComps;
			for (int32 c = 0; c < Comps.Num(); ++c)
			{
				const FString N = ArchRocks[c]->GetName();
				if (N.Contains(TEXT("SandstoneBoulder_04")) || N.Contains(TEXT("SandstoneBoulder_03")) || N.Contains(TEXT("ForestRockFormation_05"))) RingComps.Add(c);
			}
			if (RingComps.Num() == 0) for (int32 c = 0; c < Comps.Num(); ++c) RingComps.Add(c);
			const float Denom = (float)FMath::Max(L.ArchRockRing - 1, 1);
			for (const FSWArchInfo& A : ArchInfos)
			{
				for (int32 k = 0; k < L.ArchRockRing; ++k)
				{
					const float Theta = FMath::Lerp(0.10f, 0.90f, k / Denom) * PI;
					const float R = A.MajorR - 0.35f * A.MinorR;   // on the tube centreline, pushed toward the arch centre
					const FVector Local(R * FMath::Cos(Theta), 0.f, R * FMath::Sin(Theta));
					const FVector P = A.Center + Local.RotateAngleAxis(A.Yaw, FVector::UpVector);
					UHierarchicalInstancedStaticMeshComponent* C = Comps[RingComps[Rng.RandRange(0, RingComps.Num() - 1)]];
					const float Size = L.ArchRockRingScale * A.MinorR * Rng.FRandRange(0.7f, 1.3f);
					const float S = Size / FMath::Max(LongestExtent(C->GetStaticMesh()), 1.f);
					const FRotator Rot(Rng.FRandRange(0.f, 360.f), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(0.f, 360.f));
					C->AddInstance(FTransform(Rot, P, FVector(S)), true);
					ImportedArchRingRocks++;
				}
			}
		}
	}

	// Massif feet: two cliff pieces pressed against each leg (HugeSandstoneCliff_01 / _06 at a uniform
	// 0.45..0.7 of native size, sunk 30%), one in front of and one behind the leg, nudged to its outer side.
	if (ArchInfos.Num() > 0 && CliffMeshes.Num() > 0)
	{
		TArray<UStaticMesh*> FootMeshes;
		for (UStaticMesh* M : CliffMeshes)
		{
			if (M && (M->GetName().Contains(TEXT("HugeSandstoneCliff_01")) || M->GetName().Contains(TEXT("HugeSandstoneCliff_06")))) FootMeshes.Add(M);
		}
		if (FootMeshes.Num() == 0)
		{
			// No named pieces: the smallest cliff mesh stands in.
			UStaticMesh* Smallest = nullptr;
			for (UStaticMesh* M : CliffMeshes) if (M && (!Smallest || LongestExtent(M) < LongestExtent(Smallest))) Smallest = M;
			if (Smallest) FootMeshes.Add(Smallest);
		}
		TArray<UHierarchicalInstancedStaticMeshComponent*> Comps;
		for (UStaticMesh* M : FootMeshes) Comps.Add(MakeInstanced(M, L.bImportedRocksUseProjectMaterial));
		for (const FSWArchInfo& A : ArchInfos)
		{
			if (Comps.Num() == 0) break;
			const FVector Along = FVector(1.f, 0.f, 0.f).RotateAngleAxis(A.Yaw, FVector::UpVector);    // span direction
			const FVector Across = FVector(0.f, 1.f, 0.f).RotateAngleAxis(A.Yaw, FVector::UpVector);   // opening axis
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				const FVector Foot = FVector(A.Center.X, A.Center.Y, 0.f) + Along * (Side * A.MajorR);
				for (int32 k = 0; k < 2; ++k)
				{
					const float Front = (k == 0) ? -1.f : 1.f;
					UHierarchicalInstancedStaticMeshComponent* C = Comps[Rng.RandRange(0, Comps.Num() - 1)];
					const FBox Box = C->GetStaticMesh()->GetBoundingBox();
					const float S = Rng.FRandRange(0.45f, 0.7f);
					const FVector P = Foot + Along * (Side * (0.35f * A.LegW + Rng.FRandRange(0.f, 0.15f * A.LegW)))
					                       + Across * (Front * (0.5f * A.DepthFoot + Rng.FRandRange(0.f, 200.f)));
					const float Z = SWProc::TerrainHeight(L, P.X, P.Y) - 0.30f * Box.GetSize().Z * S - Box.Min.Z * S;
					const FRotator R(Rng.FRandRange(-4.f, 4.f), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(-4.f, 4.f));
					C->AddInstance(FTransform(R, FVector(P.X, P.Y, Z), FVector(S)), true);
					ImportedArchCliffs++;
				}
			}
		}
	}

	// River stones: small, along both banks and in the shallows.
	const TArray<UStaticMesh*> Stones = FindMeshes(Roles.Root, Roles.RiverStone);
	if (Stones.Num() > 0)
	{
		TArray<UHierarchicalInstancedStaticMeshComponent*> Comps;
		for (UStaticMesh* M : Stones) { UHierarchicalInstancedStaticMeshComponent* C = MakeInstanced(M, L.bImportedRocksUseProjectMaterial); C->SetCastShadow(false); Comps.Add(C); }
		for (int32 i = 0; i < L.RiverStoneCount; ++i)
		{
			const float X = Rng.FRandRange(-Arena - 1500.f, Arena + 1500.f);
			const float Off = (Rng.FRand() < 0.5f ? -1.f : 1.f) * Rng.FRandRange(L.RiverWidth * 0.6f, L.RiverWidth * 1.9f);
			const float Y = SWProc::RiverCenterY(L, X) + Off;
			Place(Comps[i % Comps.Num()], X, Y, Rng.FRandRange(25.f, 110.f), 0.3f, 25.f);
			ImportedRiverStones++;
		}
	}

	// Shrubs: the bank band just above the wetland, thinning outward.
	const TArray<UStaticMesh*> Shrubs = FindMeshes(Roles.Root, Roles.Shrub);
	if (Shrubs.Num() > 0)
	{
		TArray<UHierarchicalInstancedStaticMeshComponent*> Comps;
		for (UStaticMesh* M : Shrubs) { UHierarchicalInstancedStaticMeshComponent* C = MakeInstanced(M, false); C->SetCastShadow(false); Comps.Add(C); }
		int32 Tries = 0;
		while (ImportedShrubs < L.ShrubCount && Tries++ < L.ShrubCount * 8)
		{
			const float X = Rng.FRandRange(-Arena - 1800.f, Arena + 1800.f);
			const float Y = Rng.FRandRange(-Arena - 1800.f, Arena + 1800.f);
			const float H = SWProc::TerrainHeight(L, X, Y);
			const float Above = H - L.WaterLevel;
			if (Above < 10.f || Above > L.WetlandBand * 4.f) continue;
			if (Rng.FRand() > 0.35f + 0.65f * (1.f - Above / (L.WetlandBand * 4.f))) continue;
			Place(Comps[Rng.RandRange(0, Comps.Num() - 1)], X, Y, Rng.FRandRange(160.f, 360.f), 0.04f, 4.f);
			ImportedShrubs++;
		}
		ShrubComps = Comps;
	}

	// Crown vegetation (plate 1: green on the arch tops): 40 groundcover + 8 shrub instances per massif along
	// its CrownPoints polyline, seeded offsets within +-0.35 * DepthCrown across the crown, Z from the polyline.
	if (ArchInfos.Num() > 0 && (GroundcoverComps.Num() > 0 || ShrubComps.Num() > 0))
	{
		for (const FSWArchInfo& A : ArchInfos)
		{
			if (A.CrownPoints.Num() < 2) continue;
			const FVector Across = FVector(0.f, 1.f, 0.f).RotateAngleAxis(A.Yaw, FVector::UpVector);   // opening axis
			auto OnCrown = [&](const TArray<UHierarchicalInstancedStaticMeshComponent*>& Comps, float SizeMin, float SizeMax, float Sink)
			{
				const float F = Rng.FRandRange(0.f, (float)(A.CrownPoints.Num() - 1));
				const int32 I0 = FMath::Clamp((int32)F, 0, A.CrownPoints.Num() - 2);
				FVector P = FMath::Lerp(A.CrownPoints[I0], A.CrownPoints[I0 + 1], FMath::Clamp(F - I0, 0.f, 1.f));
				P += Across * (Rng.FRandRange(-0.35f, 0.35f) * A.DepthCrown);
				UHierarchicalInstancedStaticMeshComponent* C = Comps[Rng.RandRange(0, Comps.Num() - 1)];
				const float Size = Rng.FRandRange(SizeMin, SizeMax);
				const float S = Size / FMath::Max(LongestExtent(C->GetStaticMesh()), 1.f);
				const FRotator R(Rng.FRandRange(-4.f, 4.f), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(-4.f, 4.f));
				C->AddInstance(FTransform(R, P - FVector(0.f, 0.f, Sink * Size), FVector(S)), true);
				ImportedCrownPlants++;
			};
			if (GroundcoverComps.Num() > 0) for (int32 k = 0; k < 40; ++k) OnCrown(GroundcoverComps, L.GroundcoverSizeMin, L.GroundcoverSizeMax, 0.02f);
			if (ShrubComps.Num() > 0) for (int32 k = 0; k < 3; ++k) OnCrown(ShrubComps, 160.f, 320.f, 0.04f);
		}
	}

	// Trees / snags: the rim and the far end, never inside the arena.
	const TArray<UStaticMesh*> Trees = FindMeshes(Roles.Root, Roles.Tree);
	if (Trees.Num() > 0)
	{
		TArray<UHierarchicalInstancedStaticMeshComponent*> Comps;
		for (UStaticMesh* M : Trees) Comps.Add(MakeInstanced(M, false));
		for (int32 i = 0; i < L.TreeCount; ++i)
		{
			const float Side = (Rng.FRand() < 0.5f ? -1.f : 1.f);
			const float X = Rng.FRandRange(-0.6f * L.TerrainHalfSize, 0.6f * L.TerrainHalfSize);
			const float Y = Side * Rng.FRandRange(Arena + 900.f, 0.72f * L.ValleyHalfWidth);
			Place(Comps[i % Comps.Num()], X, Y, Rng.FRandRange(900.f, 1900.f), 0.02f, 3.f);
			ImportedTrees++;
		}
	}

	// Mist cards pooled along the channel: the sample's mist card if migrated, else the engine fog sheet.
	{
		UStaticMesh* CardMesh = Roles.FogCardMesh.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *Roles.FogCardMesh);
		UMaterialInterface* CardMat = Roles.FogCardMaterial.IsEmpty() ? nullptr : LoadObject<UMaterialInterface>(nullptr, *Roles.FogCardMaterial);
		if (!CardMesh)
		{
			CardMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/EngineVolumetrics/Fogsheet/Mesh/S_EV_FogSheet_01.S_EV_FogSheet_01"));
			CardMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineVolumetrics/Fogsheet/Materials/MI_Fogsheet_HideClose.MI_Fogsheet_HideClose"));
		}
		if (CardMesh)
		{
			const float Extent = LongestExtent(CardMesh);
			for (int32 i = 0; i < L.MistCount; ++i)
			{
				const float X = -Arena + (i + 0.5f) * (2.f * Arena / FMath::Max(L.MistCount, 1));
				const float Y = SWProc::RiverCenterY(L, X) + Rng.FRandRange(-L.RiverWidth * 0.8f, L.RiverWidth * 0.8f);
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
				C->SetupAttachment(Root);
				C->SetStaticMesh(CardMesh);
				if (CardMat) C->SetMaterial(0, CardMat);
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				C->SetCastShadow(false);
				const float Size = Rng.FRandRange(1400.f, 2600.f);
				C->SetWorldTransform(FTransform(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), FVector(X, Y, L.WaterLevel + 60.f), FVector(Size / FMath::Max(Extent, 1.f))));
				C->RegisterComponent();
				MistCards.Add(C);
			}
		}
	}

	// Cracked-mud patches along the shoreline band (the drought floor of plate B4, visible even when wet).
	const TArray<UStaticMesh*> Mud = FindMeshes(Roles.Root, Roles.MudPatch);
	if (Mud.Num() > 0)
	{
		TArray<UHierarchicalInstancedStaticMeshComponent*> Comps;
		for (UStaticMesh* M : Mud) { UHierarchicalInstancedStaticMeshComponent* C = MakeInstanced(M, false); C->SetCastShadow(false); Comps.Add(C); }
		for (int32 i = 0; i < 70; ++i)
		{
			const float X = Rng.FRandRange(-Arena, Arena);
			const float Off = (Rng.FRand() < 0.5f ? -1.f : 1.f) * Rng.FRandRange(L.RiverWidth * 1.1f, L.RiverWidth * 2.6f);
			const float Y = SWProc::RiverCenterY(L, X) + Off;
			if (SWProc::TerrainHeight(L, X, Y) < L.WaterLevel + 4.f) continue;
			Place(Comps[i % Comps.Num()], X, Y, Rng.FRandRange(500.f, 900.f), 0.12f, 2.f);
			ImportedMudPatches++;
		}
	}
}

// ---------------------------------------------------------------------------
// Waterfalls and moon (plate elements no asset pack supplies)
// ---------------------------------------------------------------------------

void ASWEnvironment::BuildWaterfalls(const FSWLookSettings& L)
{
	FRandomStream Rng(L.LookSeed * 13 + 9);
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SW_Waterfall.M_SW_Waterfall"));

	// Shared sheet builder: TWO crossed ribbons of quads (AcrossA / AcrossB, 90 deg apart) swept along a centreline so
	// the sheet reads from any camera angle. Width lerps WidthTop -> WidthFoot; vertex G = 0 at the lip .. 1 at the foot
	// (M_SW_Waterfall fades in at the lip and dissolves at the foot). Registered on Waterfalls for the drought lerp.
	auto AddSheet = [&](const TArray<FVector>& Path, const FVector& AcrossA, const FVector& AcrossB, float WidthTop, float WidthFoot, float Glow) -> UProceduralMeshComponent*
	{
		if (Path.Num() < 2) return nullptr;
		TArray<float> Cum; Cum.Add(0.f);
		for (int32 k = 1; k < Path.Num(); ++k) Cum.Add(Cum.Last() + FVector::Dist(Path[k - 1], Path[k]));
		const float Total = FMath::Max(Cum.Last(), 1.f);
		TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FVector2D> UV; TArray<FLinearColor> C;
		const FVector AcrossDirs[2] = { AcrossA, AcrossB };
		for (int32 Sheet = 0; Sheet < 2; ++Sheet)
		{
			const FVector Across = AcrossDirs[Sheet];
			const int32 Base0 = V.Num();
			for (int32 k = 0; k < Path.Num(); ++k)
			{
				const float F = Cum[k] / Total;
				const float W = FMath::Lerp(WidthTop, WidthFoot, F);
				V.Add(Path[k] + Across * W * 0.5f); V.Add(Path[k] - Across * W * 0.5f);
				N.Add(FVector::UpVector); N.Add(FVector::UpVector);
				UV.Add(FVector2D(0.f, F)); UV.Add(FVector2D(1.f, F));
				C.Add(FLinearColor(1.f, F, 0.f, 1.f)); C.Add(FLinearColor(1.f, F, 0.f, 1.f));
			}
			for (int32 k = 0; k + 1 < Path.Num(); ++k)
			{
				const int32 A = Base0 + k * 2, B = A + 1, Cc = A + 2, D = A + 3;
				T.Add(A); T.Add(Cc); T.Add(B);
				T.Add(B); T.Add(Cc); T.Add(D);
			}
		}
		UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(this);
		PMC->SetupAttachment(Root);
		PMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PMC->SetCastShadow(false);
		PMC->RegisterComponent();
		PMC->CreateMeshSection_LinearColor(0, V, T, N, UV, C, TArray<FProcMeshTangent>(), false);
		if (Base)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
			MID->SetScalarParameterValue(TEXT("Glow"), Glow);
			MID->SetScalarParameterValue(TEXT("Seed"), Rng.FRandRange(0.f, 100.f));
			PMC->SetMaterial(0, MID);
		}
		Waterfalls.Add(PMC);
		WaterfallBaseGlow.Add(Glow);
		return PMC;
	};
	// Spray at the foot: a translucent glow cluster in the same material stands in for the splash.
	auto AddSpray = [&](const FVector& At, float Rx, float Ry, float Glow)
	{
		if (!Base) return;
		SWProc::FMeshData Spray;
		SWProc::AppendEllipsoid(Spray, FTransform(At), FVector(Rx, Ry, 90.f), 14, 8,
			[](const FVector& U, const FVector&) { return FLinearColor(1.f, 0.15f, 0.f, 1.f); },
			[&](const FVector& U) { return 0.15f * FMath::Sin(U.X * 5.f + U.Y * 7.f); });
		UProceduralMeshComponent* S2 = NewObject<UProceduralMeshComponent>(this);
		S2->SetupAttachment(Root);
		S2->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		S2->SetCastShadow(false);
		S2->RegisterComponent();
		S2->CreateMeshSection_LinearColor(0, Spray.Verts, Spray.Tris, Spray.Normals, Spray.UV0, Spray.Colors, TArray<FProcMeshTangent>(), false);
		UMaterialInstanceDynamic* MID2 = UMaterialInstanceDynamic::Create(Base, this);
		MID2->SetScalarParameterValue(TEXT("Glow"), Glow);
		MID2->SetScalarParameterValue(TEXT("Seed"), Rng.FRandRange(0.f, 100.f));
		S2->SetMaterial(0, MID2);
		Waterfalls.Add(S2);
		WaterfallBaseGlow.Add(Glow);
	};
	// Mist cards: the same fog-sheet role mesh/material BuildImportedFeatures uses along the river (engine fallback).
	UStaticMesh* CardMesh = Roles.FogCardMesh.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *Roles.FogCardMesh);
	UMaterialInterface* CardMat = Roles.FogCardMaterial.IsEmpty() ? nullptr : LoadObject<UMaterialInterface>(nullptr, *Roles.FogCardMaterial);
	if (!CardMesh)
	{
		CardMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/EngineVolumetrics/Fogsheet/Mesh/S_EV_FogSheet_01.S_EV_FogSheet_01"));
		CardMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineVolumetrics/Fogsheet/Materials/MI_Fogsheet_HideClose.MI_Fogsheet_HideClose"));
	}
	const float CardExtent = CardMesh ? FMath::Max(LongestExtent(CardMesh), 1.f) : 1.f;
	auto AddMist = [&](const FVector& At, float Size)
	{
		if (!CardMesh) return;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(Root);
		C->SetStaticMesh(CardMesh);
		if (CardMat) C->SetMaterial(0, CardMat);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetWorldTransform(FTransform(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), At, FVector(Size / CardExtent)));
		C->RegisterComponent();
		MistCards.Add(C);
	};

	// ---- Rim falls: sheets from the valley walls ahead of the start camera, alternating sides ----
	for (int32 i = 0; i < L.WaterfallCount; ++i)
	{
		const float Side = (i % 2 == 0) ? 1.f : -1.f;
		const float X = -1500.f + i * 2600.f + Rng.FRandRange(-500.f, 500.f);
		const float YTop = Side * L.ValleyHalfWidth * Rng.FRandRange(0.80f, 0.92f);
		const float YFoot = Side * L.ValleyHalfWidth * Rng.FRandRange(0.58f, 0.66f);
		const float ZTop = SWProc::TerrainHeight(L, X, YTop) - 40.f;
		const float ZFoot = SWProc::TerrainHeight(L, X, YFoot) + 10.f;
		if (ZTop - ZFoot < 400.f) continue;
		WaterfallLips.Add(FVector(X, YTop, ZTop));
		// Centreline: a slight outward bow (free fall) and a little acceleration toward the pool.
		const int32 Segs = 14;
		const FVector Top(X, YTop, ZTop), Foot(X, YFoot, ZFoot);
		TArray<FVector> Path;
		for (int32 k = 0; k <= Segs; ++k)
		{
			const float F = k / (float)Segs;
			FVector P = FMath::Lerp(Top, Foot, F);
			P.Y += -Side * 35.f * FMath::Sin(F * PI);
			P.Z = FMath::Lerp(Top.Z, Foot.Z, F * F * 0.15f + F * 0.85f);
			Path.Add(P);
		}
		AddSheet(Path, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), L.WaterfallWidth * 0.6f, L.WaterfallWidth * 1.2f, L.WaterfallGlow);
		AddSpray(FVector(Foot.X, Foot.Y - Side * 60.f, Foot.Z + 40.f), L.WaterfallWidth * 0.9f, L.WaterfallWidth * 0.7f, L.WaterfallGlow * 0.5f);
	}

	// ---- Arch falls (plate 1): a sheet per massif arch pouring off the crown, over the shoulder and down the OUTER
	// face of the nearer leg to the water/ground. Lip = crown polyline point ~30% along from a seeded end; the top run
	// follows the real crown vertices, the shoulder and jamb follow the section curves of SWProc::BuildMassifArch.
	int32 ArchFallCount = 0;
	if (L.bArchFalls && L.ArchFallsPerArch > 0)
	{
		for (const FSWArchInfo& A : ArchInfos)
		{
			const int32 NC = A.CrownPoints.Num();
			if (NC < 4 || A.OpenW <= 0.f || A.TotalH <= 0.f) continue;
			const FVector Along = FVector(1.f, 0.f, 0.f).RotateAngleAxis(A.Yaw, FVector::UpVector);   // local X: left foot (index 0) -> right foot
			const FVector Open = FVector(0.f, 1.f, 0.f).RotateAngleAxis(A.Yaw, FVector::UpVector);    // local Y: the opening axis
			const FVector Ctr2D(A.Center.X, A.Center.Y, 0.f);
			const float R = 0.5f * A.OpenW;
			const float ZShoulder = A.Center.Z + 0.55f * A.TotalH, ZBase = A.Center.Z - 0.25f * A.TotalH;
			const float Sx = 0.28f * (A.OpenW + 2.f * A.LegW), Sz = 0.45f * A.TotalH, XOut = R + A.LegW;
			const float End0 = Rng.FRand() < 0.5f ? -1.f : 1.f;
			for (int32 f = 0; f < L.ArchFallsPerArch; ++f)
			{
				const float End = (f % 2 == 0) ? End0 : -End0;
				const float Frac = 0.30f + (f >= 2 ? Rng.FRandRange(-0.1f, 0.1f) : 0.f);
				const int32 Idx = FMath::Clamp(FMath::RoundToInt((End < 0.f ? Frac : 1.f - Frac) * (NC - 1)), 0, NC - 1);
				const int32 Dir = End < 0.f ? -1 : 1;
				const float LegSign = FVector::DotProduct(A.CrownPoints[Idx] - A.Center, Along) < 0.f ? -1.f : 1.f;
				const FVector Out = Along * LegSign;                                        // outward from the arch, away from the opening
				const float Depth0 = FVector::DotProduct(A.CrownPoints[Idx] - A.Center, Open);
				const float Off = 0.2f * A.LegW;                                             // stand-off from the nominal flank (mesh noise)
				TArray<FVector> Path;
				// 1. Along the crown from the lip to the edge on that side (real displaced vertices, lifted 25 uu).
				for (int32 k = Idx; k >= 0 && k < NC; k += Dir) Path.Add(A.CrownPoints[k] + FVector(0.f, 0.f, 25.f));
				const FVector Edge = Path.Last();
				WaterfallLips.Add(Edge);                                                     // drips where the water goes over the edge
				// 2. Over the rounded shoulder (quarter ellipse Sx x Sz about (XOut - Sx, ZShoulder)) from the edge angle down to 0.
				const float AEdge = FMath::Asin(FMath::Clamp((Edge.Z - ZShoulder) / FMath::Max(Sz, 1.f), 0.f, 1.f));
				for (int32 k = 1; k <= 4; ++k)
				{
					const float Ang = AEdge * (1.f - k / 4.f);
					const float X = XOut - Sx * (1.f - FMath::Cos(Ang)) + Off * (k / 4.f);
					const float Z = ZShoulder + Sz * FMath::Sin(Ang) + 25.f * FMath::Sin(Ang);
					Path.Add(Ctr2D + Out * X + Open * Depth0 + FVector(0.f, 0.f, Z));
				}
				// 3. Down the jamb: the flank flares from LegW at the shoulder to 1.9 LegW at ZBase. Bottom = water or ground.
				auto FlankX = [&](float Z) { const float t = FMath::Clamp((Z - ZBase) / FMath::Max(ZShoulder - ZBase, 1.f), 0.f, 1.f); return R + FMath::Lerp(1.9f * A.LegW, A.LegW, t) + Off; };
				FVector Foot = Ctr2D + Out * FlankX(A.Center.Z) + Open * Depth0;
				float BottomZ = FMath::Max(SWProc::TerrainHeight(L, Foot.X, Foot.Y), L.WaterLevel) + 10.f;
				Foot = Ctr2D + Out * FlankX(BottomZ) + Open * Depth0;
				BottomZ = FMath::Max(SWProc::TerrainHeight(L, Foot.X, Foot.Y), L.WaterLevel) + 10.f;
				Foot.Z = BottomZ;
				const int32 Segs = 10;
				for (int32 k = 1; k <= Segs; ++k)
				{
					const float F = k / (float)Segs;
					const float Z = FMath::Lerp(ZShoulder, BottomZ, F * F * 0.15f + F * 0.85f);
					Path.Add(Ctr2D + Out * (FlankX(Z) + 40.f * FMath::Sin(F * PI)) + Open * Depth0 + FVector(0.f, 0.f, Z));
				}
				AddSheet(Path, Open, Along, L.ArchFallWidth * 0.7f, L.ArchFallWidth * 1.3f, L.ArchFallGlow);
				AddSpray(Foot + Out * 60.f + FVector(0.f, 0.f, 30.f), L.ArchFallWidth * 0.9f, L.ArchFallWidth * 0.7f, L.ArchFallGlow * 0.5f);
				for (int32 m = 0; m < 2; ++m)
				{
					const FVector At = Foot + Out * Rng.FRandRange(150.f, 380.f) + Open * (m == 0 ? -1.f : 1.f) * Rng.FRandRange(120.f, 260.f) + FVector(0.f, 0.f, 40.f);
					AddMist(At, Rng.FRandRange(900.f, 1400.f));
				}
				ArchFallCount++;
			}
		}
	}

	// Drips / spray Niagara at every lip (rim lips + arch edges) if the manifest supplied a system.
	if (!Roles.NiagaraMist.IsEmpty())
	{
		if (UNiagaraSystem* Sys = LoadObject<UNiagaraSystem>(nullptr, *Roles.NiagaraMist))
		{
			for (const FVector& Lip : WaterfallLips)
			{
				if (UNiagaraComponent* C = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), Sys, Lip + FVector(0.f, 0.f, 30.f), FRotator::ZeroRotator, FVector(3.f), false, true, ENCPoolMethod::None, true))
				{
					MistSystems.Add(C);
				}
			}
		}
		else UE_LOG(LogSymbioticWorld, Warning, TEXT("Drips Niagara system %s not found"), *Roles.NiagaraMist);
	}
	UE_LOG(LogSymbioticWorld, Log, TEXT("Waterfalls: %d components (%d arch falls on %d arches), %d lips, %d drip systems, %d mist cards, %d mud patches"), Waterfalls.Num(), ArchFallCount, ArchInfos.Num(), WaterfallLips.Num(), MistSystems.Num(), MistCards.Num(), ImportedMudPatches);
}

void ASWEnvironment::BuildMoon(const FSWLookSettings& L)
{
	if (!L.bMoon) return;
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Sphere) return;
	Moon = NewObject<UStaticMeshComponent>(this);
	Moon->SetupAttachment(Root);
	Moon->SetStaticMesh(Sphere);
	Moon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Moon->SetCastShadow(false);
	const float El = FMath::DegreesToRadians(L.MoonElevation), Az = FMath::DegreesToRadians(L.MoonAzimuth);
	const FVector Dir(FMath::Cos(El) * FMath::Cos(Az), FMath::Cos(El) * FMath::Sin(Az), FMath::Sin(El));
	Moon->SetWorldLocation(Dir * L.MoonDistance);
	Moon->SetWorldScale3D(FVector(L.MoonRadius / 50.f));
	Moon->RegisterComponent();
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SW_Moon.M_SW_Moon")))
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
		MID->SetScalarParameterValue(TEXT("Glow"), L.MoonGlow);
		Moon->SetMaterial(0, MID);
	}
}

// ---------------------------------------------------------------------------
// Trace field overlay: a translucent grid draped just above the terrain whose
// vertex colours are the two fields (R = Trace X cyan, G = Trace Y amber).
// ---------------------------------------------------------------------------

void ASWEnvironment::BuildTraceOverlay(const FSWLookSettings& L)
{
	if (!Manager || !Manager->GetSettings().bTraceFields) return;
	const FSWTraceField& F = Manager->GetTraceX();
	const int32 N = F.Cells();
	if (N < 2) return;
	const float Half = F.HalfSize();
	const float Cell = F.CellSize();
	TraceVerts.Reset(); TraceTris.Reset(); TraceNormals.Reset(); TraceUV.Reset(); TraceColors.Reset();
	for (int32 j = 0; j <= N; ++j)
	{
		for (int32 i = 0; i <= N; ++i)
		{
			const float X = -Half + i * Cell, Y = -Half + j * Cell;
			const float Z = FMath::Max(SWProc::TerrainHeight(L, X, Y), L.WaterLevel) + 6.f;
			TraceVerts.Add(FVector(X, Y, Z));
			TraceNormals.Add(FVector::UpVector);
			TraceUV.Add(FVector2D(i / (float)N, j / (float)N));
			TraceColors.Add(FLinearColor(0.f, 0.f, 0.f, 1.f));
		}
	}
	for (int32 j = 0; j < N; ++j)
	{
		for (int32 i = 0; i < N; ++i)
		{
			const int32 A = j * (N + 1) + i, B = A + 1, C = A + (N + 1), D = C + 1;
			TraceTris.Add(A); TraceTris.Add(C); TraceTris.Add(B);
			TraceTris.Add(B); TraceTris.Add(C); TraceTris.Add(D);
		}
	}
	TraceOverlay = NewObject<UProceduralMeshComponent>(this);
	TraceOverlay->SetupAttachment(Root);
	TraceOverlay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TraceOverlay->SetCastShadow(false);
	TraceOverlay->RegisterComponent();
	TraceOverlay->CreateMeshSection_LinearColor(0, TraceVerts, TraceTris, TraceNormals, TraceUV, TraceColors, TArray<FProcMeshTangent>(), false);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SW_TraceOverlay.M_SW_TraceOverlay")))
	{
		TraceMID = UMaterialInstanceDynamic::Create(Base, this);
		TraceMID->SetVectorParameterValue(TEXT("TraceXColor"), L.TraceOverlayXColor);   // cyan-white / soft amber stains, not the creature glow colours
		TraceMID->SetVectorParameterValue(TEXT("TraceYColor"), L.TraceOverlayYColor);
		TraceMID->SetScalarParameterValue(TEXT("Gain"), 1.1f);
		TraceMID->SetScalarParameterValue(TEXT("Intensity"), L.TraceOverlayIntensity);   // multiplies emissive and opacity (M_SW_TraceOverlay)
		TraceOverlay->SetMaterial(0, TraceMID);
	}
}

void ASWEnvironment::UpdateTraceOverlay()
{
	if (!TraceOverlay || !Manager) return;
	const FSWTraceField& FX = Manager->GetTraceX();
	const FSWTraceField& FY = Manager->GetTraceY();
	const int32 N = FX.Cells();
	if ((N + 1) * (N + 1) != TraceVerts.Num()) return;
	// Vertex (i, j) shows the average of the up-to-4 cells that touch it.
	for (int32 j = 0; j <= N; ++j)
	{
		for (int32 i = 0; i <= N; ++i)
		{
			float SX = 0.f, SY = 0.f; int32 C = 0;
			for (int32 dj = -1; dj <= 0; ++dj) for (int32 di = -1; di <= 0; ++di)
			{
				const int32 II = i + di, JJ = j + dj;
				if (II < 0 || JJ < 0 || II >= N || JJ >= N) continue;
				SX += FX.At(II, JJ); SY += FY.At(II, JJ); C++;
			}
			const float Inv = C > 0 ? 1.f / C : 0.f;
			TraceColors[j * (N + 1) + i] = FLinearColor(FMath::Clamp(SX * Inv, 0.f, 1.f), FMath::Clamp(SY * Inv, 0.f, 1.f), 0.f, 1.f);
		}
	}
	TraceOverlay->UpdateMeshSection_LinearColor(0, TraceVerts, TraceNormals, TraceUV, TraceColors, TArray<FProcMeshTangent>());
}
