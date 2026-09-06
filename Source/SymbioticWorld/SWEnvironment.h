#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWTypes.h"
#include "SWEnvironment.generated.h"

class UProceduralMeshComponent;
class UStaticMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class ADirectionalLight;
class ASkyLight;
class ASkyAtmosphere;
class AVolumetricCloud;
class AExponentialHeightFog;
class APostProcessVolume;
class ASWWorldManager;
class UNiagaraComponent;

// Which imported meshes/materials play which part in the valley. Filled from
// the asset manifest when present, otherwise from the built-in Poly Haven set.
struct FSWAssetRoles
{
	FString Root = TEXT("/Game");
	TArray<FString> Cliff, ArchRock, Boulder, RiverStone, Groundcover, Shrub, Tree;
	FString GroundMaterial, GroundMaterialWet, WaterMaterial;
	FString NiagaraMist, NiagaraMotes, NiagaraDust;   // "mist" = drips at waterfall lips / arch undersides
	TArray<FString> MudPatch;                          // cracked-mud meshes for the shoreline (drought floor)
	FString FogCardMesh, FogCardMaterial;              // mist cards along the channel
	bool bFromManifest = false;
};

// One massif arch as placed by BuildFeatures (SWProc::BuildMassifArch): the arch spans local X rotated
// by Yaw about Z, opening axis is local Y. MajorR = foot half-distance, MinorR = LegW / 2 (kept for the
// optional arch rock ring). CrownPoints = extrados centreline samples near the top, world space.
struct FSWArchInfo
{
	FVector Center;
	float MajorR;
	float MinorR;
	float Yaw;
	float OpenW = 0.f, OpenH = 0.f, TotalH = 0.f, LegW = 0.f, DepthCrown = 0.f, DepthFoot = 0.f;
	TArray<FVector> CrownPoints;
};

// The rendered valley: procedural terrain, water plane, rock arches and
// boulders, sun/sky/fog/post-process, and the drought colour shift. Spawned by
// ASWGameMode after the world manager so it can read FSWLookSettings. Has no
// effect on the simulation.
UCLASS()
class SYMBIOTICWORLD_API ASWEnvironment : public AActor
{
	GENERATED_BODY()

public:
	ASWEnvironment();

	void Build(ASWWorldManager* InManager);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	float GetDroughtFactor() const { return DroughtFactor; }

protected:
	UPROPERTY() ASWWorldManager* Manager = nullptr;
	UPROPERTY() USceneComponent* Root;
	UPROPERTY() UProceduralMeshComponent* Terrain;
	UPROPERTY() UProceduralMeshComponent* Features;     // arches + rocks, one section each
	UPROPERTY() UStaticMeshComponent* Water;
	UPROPERTY() TArray<UHierarchicalInstancedStaticMeshComponent*> Instanced;
	UPROPERTY() TArray<UNiagaraComponent*> MistSystems;
	UPROPERTY() TArray<UProceduralMeshComponent*> Waterfalls;
	TArray<FVector> WaterfallLips;
	TArray<float> WaterfallBaseGlow;   // parallel to Waterfalls: the undroughted Glow of each sheet / spray
	UPROPERTY() TArray<UStaticMeshComponent*> MistCards;
	int32 ImportedMudPatches = 0;
	UPROPERTY() UStaticMeshComponent* Moon = nullptr;
	UPROPERTY() UProceduralMeshComponent* TraceOverlay = nullptr;
	UPROPERTY() UMaterialInstanceDynamic* TraceMID = nullptr;
	TArray<FVector> TraceVerts; TArray<int32> TraceTris; TArray<FVector> TraceNormals; TArray<FVector2D> TraceUV; TArray<FLinearColor> TraceColors;
	float TraceRefreshTimer = 0.f;
	int32 ImportedCliffs = 0, ImportedBoulders = 0, ImportedGroundcover = 0;
	int32 ImportedArchRocks = 0, ImportedRiverStones = 0, ImportedShrubs = 0, ImportedTrees = 0;
	int32 ImportedWallPieces = 0, ImportedWallFarEnd = 0, ImportedPinnacles = 0, ImportedArchRingRocks = 0;
	int32 ImportedArchCliffs = 0, ImportedCrownPlants = 0;   // cliff pieces at the massif feet, groundcover + shrubs on the crowns
	FSWAssetRoles Roles;
	UPROPERTY() TArray<UStaticMesh*> CliffMeshes;   // resolved 'cliff' role meshes (BuildImported), reused by BuildCliffWalls
	TArray<UHierarchicalInstancedStaticMeshComponent*> GroundcoverComps, ShrubComps;   // owned via Instanced; reused for the arch crowns
	TArray<FVector> ArchBases;   // filled by BuildFeatures, used to dress arch feet
	TArray<FSWArchInfo> ArchInfos;   // filled by BuildFeatures, used for the foot cliffs, crown plants and the optional rock rings
	UPROPERTY() UMaterialInstanceDynamic* TerrainMID;
	UPROPERTY() UMaterialInstanceDynamic* RockMID;
	UPROPERTY() UMaterialInstanceDynamic* SandstoneMID = nullptr;   // M_SW_Sandstone for the massif arches
	// Scan material binding (M_SW_Scan): the Electric Dreams masters need sample-only inputs, so every VT
	// scan slot is rebound to our own master with the instance's Albedo / Normal / DR textures.
	UPROPERTY() UMaterialInterface* ScanBase = nullptr;
	bool bScanBaseSearched = false;
	TSet<UStaticMesh*> ScanBoundMeshes;
	int32 ScanBoundSlots = 0;
	UPROPERTY() UMaterialInstanceDynamic* WaterMID;
	UPROPERTY() UMaterialInstanceDynamic* CloudMID = nullptr;
	UPROPERTY() ADirectionalLight* Sun;
	UPROPERTY() ADirectionalLight* Fill = nullptr;      // shadowless cool fill from the camera side
	UPROPERTY() ASkyLight* Sky;
	UPROPERTY() ASkyAtmosphere* Atmosphere;
	UPROPERTY() AVolumetricCloud* Clouds;
	UPROPERTY() AExponentialHeightFog* Fog;
	UPROPERTY() APostProcessVolume* PostProcess;

	float DroughtFactor = 0.f;
	float LastAppliedDrought = -1.f;

	void BuildTerrain(const FSWLookSettings& L);
	void BuildWater(const FSWLookSettings& L);
	void BuildFeatures(const FSWLookSettings& L);
	// Imported-asset dressing. Returns false if nothing was found under AssetRoot.
	bool BuildImported(const FSWLookSettings& L);
	void BuildCliffWalls(const FSWLookSettings& L);         // stratified rim walls, far-end wall with sun gap, pinnacles
	void LoadRoles(const FSWLookSettings& L);
	void BuildImportedFeatures(const FSWLookSettings& L);   // arch rocks, river stones, shrubs, trees, mist (after arches exist)
	TArray<UStaticMesh*> FindMeshes(const FString& AssetPath, const TArray<FString>& Stems) const;
	UHierarchicalInstancedStaticMeshComponent* MakeInstanced(UStaticMesh* Mesh, bool bProjectMaterial);
	void BindScanMaterials(UHierarchicalInstancedStaticMeshComponent* C, UStaticMesh* Mesh);   // see ScanBase
	static float LongestExtent(const UStaticMesh* Mesh);
	void BuildLighting(const FSWLookSettings& L);
	void BuildWaterfalls(const FSWLookSettings& L);
	void BuildMoon(const FSWLookSettings& L);
	void BuildTraceOverlay(const FSWLookSettings& L);
	void UpdateTraceOverlay();
	void RunConsoleCommands(const FSWLookSettings& L);
	void ApplyDrought(const FSWLookSettings& L, float F);
	UMaterialInterface* LoadMat(const TCHAR* Path) const;
};
