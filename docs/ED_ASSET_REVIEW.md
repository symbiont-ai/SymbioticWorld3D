# Electric Dreams asset review — what came into Symbiotic World and how to use it

Written 2026-09-05 (evening) after the migration pass. Sources: the registry
dump of the sample (`AssetSources/ed_registry.csv`, 23,531 rows), the six
category reviews (rocks, foliage, materials/decals, water/fog/FX, structure,
performance), the migration manifest (`AssetSources/ed_manifest.json`), the
migration log (`AssetSources/ed_migrate_log.txt`), the post-migration registry
of our project (`AssetSources/sw_registry.csv`, 409 assets), a disk check of
`Content/`, and the 17:10 `-game` run log (`Saved/Logs/SymbioticWorld.log`).
Concept plates: `docs/plates/` is still empty; the four Appendix-B plates were
viewed from the session scratchpad (`plates/p18_img0…3_1672x941.jpeg`).

Read with `DESIGN.md` §4 (environment mechanisms), `docs/VISUAL_PLAN.md` §1,
§5, §7 and `docs/ASSET_DOWNLOADS.md`. Nothing in this document changes the
simulation contract (`SWTypes.h` + `DESIGN.md`); every item here is dressing.

---

## 0. Summary

- **Added: 274 packages, 3,634 MB (3.55 GB) under `Content/`**, all copied
  file-for-file from the launcher vault at the same `/Game/…` path. Budget was
  6 GB; 2.45 GB left unspent on purpose (C: had 25.3 GB free, now ~20 GB).
- **Every manifest role resolves.** The 17:10 run logged
  `Roles resolved: cliffs 6/6, boulders 6/6, groundcover 7/7`, then
  `Imported dressing: yes [manifest] (cliffs 48, arch rocks 50, boulders 110,
  river stones 320, groundcover 5000, shrubs 260, trees 28, mist 0)` and
  `Waterfalls: 6 components, 3 lips, 0 drip systems, 14 mist cards, 27 mud patches`.
  Two things did not work yet: the drip Niagara system (loaded but 0 spawned)
  and the perf caps (counts are still the pre-migration defaults).
- **Substrate is not required and must stay off.** 0 of the 33 migrated
  material instances / 13 materials carry a Substrate tag; every master that
  renders is legacy-converted.
- **Role → asset** (exact asset names, unique in our tree):
  cliff = SM_HugeSandstoneCliff_01/_03/_06, SM_MassiveSandstoneCliff_02/_05,
  SM_CanyonSandstoneRidge_03 · arch_rock = SM_SandstoneBoulder_03/_04,
  SM_MassiveTundraRockFormation_02, SM_ForestRockFormation_05 · boulder =
  SM_MossyForestBoulder_04/_06, SM_LichenedForestBoulder_01, SM_NordicBoulder_02,
  SM_SandstoneBoulder_01/_03 · river_stone = SM_SmallStonesPack_08/_10/_12/_05 ·
  mud_patch = SM_DryRiverBed_02 · groundcover = SM_KikuyuGrass_05/_01,
  SM_Fern_05/_01, SM_CloverVarieties_01, SM_WhiteWindflower_02, SM_DeadLeaves_01 ·
  shrub = SM_Elderberry_04/_06, SM_Crownbeard_03 · tree = SM_ForestDeadTree_01,
  SM_Elderberry_08 · fog cards = SM_MistCard_v2 + MI_MistCard_RGBTiling ·
  niagara.mist = NS_WaterDrips_Falling_Placeable · niagara.dust =
  NS_Leaves_Falling_Frustum_01 · ground = M_SW_TerrainED (ours, built from
  MossyGrass / MossyRockyGround / SwampWater / DriedGrassOnDampSoil) ·
  water = M_SW_Water (ours) + T_RippleNormals · decals = MI_TileableLeakage_01,
  MI_AridDebris_01, MI_TileableMossPatches_01 (need a re-parent before use).

---

## 1. What the sample contains

**Source.** `C:/ProgramData/Epic/EpicGamesLauncher/VaultCache/ElectricDreamsSample_5.7/data/`
(`ElectricDreamsSample.uproject`, EngineAssociation 5.7, Category Samples,
one C++ runtime module, prebuilt DLLs with BuildId 47537391 = our engine
5.7.3). Content: 46.95 GB, 23,383 files, 23,531 registry rows. The sample is
Epic's "Electric Dreams Env" showcase: one World-Partition level
(`Levels/ElectricDreams_Env`, 15,235 external actors) plus PCG demo levels,
built for Lumen + hardware ray tracing + Substrate + Nanite on a 4 km map.

**Folder tree (GB on disk, from the registry).**

| Folder | GB | What it is |
|---|---|---|
| Megascans/3D_Assets | 22.15 | 148 Nanite scans in ~60 folders (0.4–1.7 GB each: cliffs, boulders, roots, stumps, riverbeds), 311 textures, nearly all 4k |
| Custom | 6.84 | RootsTest 3.46 (3 × 0.8 GB root scans), IvyTest 1.96, BGSlopes 0.80 (one background slope), RS_Assets 0.38 (RealityScan rock/dead tree/pebbles/log), OffroadSigns 0.11, CustomMaterials 0.07 (the real material masters), Lighting 0.06 |
| SmartAssets | 5.43 | SM_Cliff_01..19 hero cliffs (0.35–0.72 GB each) + 0.85 GB unique textures + 0.24 GB tileables + a material-layer stack |
| __ExternalActors__ | 3.92 | 20,727 one-actor packages of the levels (not migratable piecemeal) |
| Meshes/_GENERATED | 3.42 | kitbashed rock formations (SM_RockFormation_03 = 926 MB), merged cliff copies, SM_Log_01 |
| Megascans/3D_Plants | 2.89 | 41 species (EuropeanHornbeam alone 1.02 GB; most species 15–60 MB) |
| Megascans/Surfaces | 0.98 | 13 tileables ~35–115 MB each (MossyGrass, MossyRockyGround, NordicMoss, SwampWater, DriedGrassOnDampSoil, JungleGround, BeachCliff, RockCliff, …) |
| Megascans/Decals | 0.69 | 15 decal families (moss patches, leakage, arid debris, sticks, jungle debris) |
| Audio | 0.23 | Soundscape / MetaSound / AudioModulation stack, 221 SoundWaves |
| PCG | 0.23 | 18 graphs, 46 PCGBlueprintSettings, 49 assembly umaps, editor utilities |
| Substrate | 0.06 | shader-ball opal showcase |
| Effects, FX, Asmbly, MSPresets, Levels, Blueprints, Input, Textures, Landscape, PhysicalMaterials | < 0.05 each | Niagara (3 systems), mist-card kit, 92 packed-level assemblies, Bridge material masters, levels, drone/game framework |

**Class counts (rows / GB).** StaticMesh 600 / 29.29 · Texture2D 730 / 13.37
(516 of them 4096², 12.34 GB; 120 at 2048²; 38 at 1024²) · PCGWorldActor 4 /
1.62 · LandscapeStreamingProxy 516 / 1.31 · PCGPartitionActor 37 / 0.50 ·
SoundWave 221 / 0.22 · Blueprint actor instances 3,729 / 0.21 ·
StaticMeshActor 15,381 / 0.07 · World 163 · MaterialInstanceConstant 299 ·
MaterialFunction 68 · Material 42 · FoliageType_InstancedStaticMesh 62 ·
DecalActor 674 · NiagaraSystem 3 · NiagaraScript 8 · SkeletalMesh 1 (a kite) ·
LevelSequence 10 · LandscapeGrassType 0. No characters, vehicles or robots as
meshes; the only man-made things are five road signs, a traffic-sign stand,
the kite and a colour checker.

**Renderer facts that matter for us.**

- *Substrate is ON in the sample* (`DefaultEngine.ini` lines 69–71:
  `r.Substrate.AccurateSRGB=1`, `r.Substrate=True`,
  `r.Substrate.BytesPerPixel = 170;`), plus `r.GBufferFormat=3`, DX12 + SM6
  only, `r.DynamicGlobalIlluminationMethod=1`, `r.Lumen.HardwareRayTracing=True`,
  `r.RayTracing=True`, `r.Nanite.MaxVisibleClusters=10485760` and friends,
  `r.VirtualTextures=True`, TSR with 32 history samples, no
  `r.Streaming.PoolSize` anywhere. *Our* `Config/DefaultEngine.ini` has no
  `r.Substrate` line (engine default 0), Lumen off, SSR, VSM on, TSR,
  `r.Streaming.PoolSize=2000`, DX12.
- The sample's materials fall into two families. (a) *Legacy-converted*
  masters that still carry BaseColor/Normal/Roughness pins plus an
  auto-inserted `StrataLegacyConversion` node (all Megascans masters,
  `M_MS_Default_Material_VT_DynamicLayering`, the foliage overrides,
  `M_RealityScan_0x`, `M_MS_Surface_Material_VT`, `M_MS_Decal_Material_VT`,
  `M_MistCard`, `M_Leaves_Base_01`): these compile unchanged with Substrate
  off, because the translator only walks the FrontMaterial pin when Substrate
  is enabled. (b) *Substrate-native* materials with no legacy pins
  (`M_PuddleWater`, `M_MudPuddle_01_PCG`, `M_MS_CustomDecal_CR/_CNR`,
  `M_SunLightFunction_00`, `M_BGLandscape_Auto`, `M_Skybox`): these render
  grey/black without Substrate and were avoided (two of them came along only
  as the saved parents of decal instances, see §5).
- *Nanite:* 590 of 600 StaticMeshes are Nanite with a single LOD; the
  registry `triangles` value is the Nanite **fallback** count, not the source
  density (disk size is the better proxy, ~15 B per source triangle). Our ini
  has no `r.Nanite.ProjectEnabled` line, so Nanite is on (5.7 default) and the
  migrated meshes render as Nanite inside HISM components.
- *Virtual textures:* every Megascans texture is `VirtualTextureStreaming=true`
  and the masters use `SAMPLERTYPE_Virtual*` samplers. `r.VirtualTextures`
  defaults to 1, so nothing to add to the ini; VT pools are separate from
  `r.Streaming.PoolSize`. (Our registry dump does not record the VT flag, so
  `sw_registry.csv` shows 0 VT rows; the flag was read from the asset
  name tables.)
- *Licence line* (put in README and the title card):
  "Environment assets from the Electric Dreams Environment sample (Epic Games;
  Quixel Megascans; RealityScan), used under the Unreal Engine EULA / Epic
  Content Licence — Unreal Engine projects only." Never redistribute the
  `.uasset` files; keep the migrated folders out of git (see §6).

---

## 2. What was migrated

**Method.** `Tools/migrate_ed.py`, run inside the full editor on the sample
(`UnrealEditor.exe <sample> /Engine/Maps/Entry -ExecutePythonScript=… -unattended`),
with `AssetTools.UseNewPackageMigration 0` so the classic `MigratePackages`
branch performs a byte-for-byte file copy at the same `/Game` path (no load,
no resave). The script computed the recursive dependency closure itself from
the registry (hard + soft) and passed `ignore_dependencies=True` so that the
two soft-referenced `/Game/Meshes/_GENERATED` stubs (`SM_DryRiverBed_02`,
`SM_ForestRockShelf_01`, which would collide by name with `FindMeshes`) were
not copied. Result: 274/274 packages, 3,633.7 MB, 0 failed, 0 size
mismatches; the closure also touched 242 `/Engine` and `/Niagara` packages
that exist identically in our 5.7.3 install and are never copied. The sample
was not modified (only editor housekeeping in its `Saved/`, `Intermediate/`,
`DerivedDataCache/`). Wall time ≈ 8 min (editor start with the sample's
shader warm-up) + 4.1 s for the copy.

**What landed under `Content/`** (disk check 2026-09-05 evening):

| Folder | MB | files | Classes |
|---|---|---|---|
| Megascans | 3,248.5 | 168 | 3D_Assets (rocks, mud patch), 3D_Plants, Surfaces, Decals |
| Custom | 144.5 | 22 | material masters, decal masters, RealityScan dead tree, cloud noise, bloom kernel, film grain |
| Effects | 28.2 | 51 | the two Niagara systems and their 8 scripts, enums, curves, leaf/drip materials, T_RippleNormals |
| SmartAssets | 25.9 | 1 | T_Rock_01_Normal (default of M_RealityScan_03) |
| MSPresets | 9.3 | 19 | Bridge surface/decal masters, foliage MFs, T_WindNoise, MPC_GlobalFoliageActor, VT placeholders |
| FX | 8.8 | 7 | mist-card kit |
| PCG | 0.02 | 1 | MI_DryGrassDampSoil_02 (a plain material instance, no PCG logic) |
| PhysicalMaterials, Textures | 0.1 | 5 | PhysMat_Grass/GravelOxidized/LooseRock/SolidRock, T_SoftSquare_01_M |
| **Migrated total** | **3,633.7** | **274** | Texture2D 136 (2,201 MB; 82 are 4k = 1,962 MB), StaticMesh 34 (1,423 MB; 32 Nanite), MaterialFunction 33, MaterialInstanceConstant 33, Material 13, NiagaraScript 8, UserDefinedEnum 6, PhysicalMaterial 4, NiagaraSystem 2, curves 2, MPC 1, NPC 1, NiagaraEffectType 1 |

`Content/` now totals 3.98 GB: the above plus the pre-existing Poly Haven set
(`Content/Assets/PolyHaven`, 124 assets, 644 MB, still on disk but no longer
placed) and our ten Python materials.

**Cost by manifest group (MB, measured closure, shared masters counted once
in the first group that pulls them):** cliff 1,274 · arch_rock 646 · boulder
549 · river_stone 67 · mud_patch 159 · groundcover 122 · shrub 107 · tree 162 ·
ground 295 · water 72 · decals 156 · fx 26.

**Shared chains included in those numbers (paid once).**
`/Game/Custom/CustomMaterials/Materials/Master_Materials/M_MS_Default_Material_VT_DynamicLayering`
+ its default textures (NordicMoss set 96 MB, T_NoiseMask_RGB 42, MossyForestBoulder_04 BC/DpRF 49,
IcelandicQuarryRock 35, MF_WaterEffect) ≈ 225 MB — every Megascans rock MIC
parents to it. Foliage chain (`Custom_MS_Master/M_MS_Foliage_Overwrite/M_MS_CustomFoliage`
+ NoWPO + NoOpacity, MF_SimpleWind, MF_WindGust, MPC_GlobalFoliageActor,
T_WindNoise, AlexandraPalm default 4k set) ≈ 47 MB. `M_RealityScan_03`
defaults (T_Rock_01_Normal 27 MB) for the dead tree. Decal masters
`M_MS_CustomDecal_CR/_CNR` + their default masks ≈ 25 MB (saved parents only).

### 2.1 Role → asset, confirmed present (file = `Content/` + path + `.uasset`)

All StaticMesh rows below are `nanite=True` except `SM_MistCard_v2`;
`tris` is the Nanite fallback count. Sizes are the package on disk; the
material/texture closure per mesh is in `ed_manifest.json`.

**cliff** (`Roles.Cliff`, 1,274 MB with textures)

| Asset | Path | MB | tris | Notes |
|---|---|---|---|---|
| SM_HugeSandstoneCliff_01 | /Game/Megascans/3D_Assets/HugeSandstoneCliff/ | 134.1 | 18,063 | ~20–30 m sandstone wall, strata, plane-cut base |
| SM_HugeSandstoneCliff_03 | same | 132.8 | 20,700 | second silhouette (_04 is its twin, not taken) |
| SM_HugeSandstoneCliff_06 | same | 137.0 | 21,219 | third wall / rim line |
| SM_MassiveSandstoneCliff_02 | /Game/Megascans/3D_Assets/MassiveSandstoneCliff/ | 132.1 | 36,468 | mid-scale outcrop; densest fallback, survives Nanite-off |
| SM_MassiveSandstoneCliff_05 | same | 1.0 | 5,178 | LOD-only import (5 LODs), background only |
| SM_CanyonSandstoneRidge_03 | /Game/Megascans/3D_Assets/CanyonSandstoneRidge/ | 77.0 | 11,163 | long low ridge for the skyline shoulders |

**arch_rock** (`Roles.ArchRock`, 646 MB)

| Asset | Path | MB | tris | Notes |
|---|---|---|---|---|
| SM_SandstoneBoulder_03 | /Game/Megascans/3D_Assets/SandstoneBoulder/ | 71.4 | 5,964 | fallen-arch rubble, same ochre as the cliffs |
| SM_SandstoneBoulder_04 | same | 71.2 | 11,762 | angular variant |
| SM_MassiveTundraRockFormation_02 | /Game/Megascans/3D_Assets/MassiveTundraRockFormation/ | 132.5 | 16,151 | the 'root' an arch grows from; use 1–2 |
| SM_ForestRockFormation_05 | /Game/Megascans/3D_Assets/ForestRockFormation/ | 71.9 | 3,290 | mossy, for the arch nearest the wetland |

**boulder** (`Roles.Boulder`, 549 MB)

| Asset | Path | MB | tris | Notes |
|---|---|---|---|---|
| SM_MossyForestBoulder_04 | /Game/Megascans/3D_Assets/MossyForestBoulder/ | 71.1 | 11,441 | 5 LODs + Nanite; its BC/DpRF are already in the shared master set |
| SM_MossyForestBoulder_06 | same | 75.5 | 8,418 | the sample's most-used boulder (369 instances) |
| SM_LichenedForestBoulder_01 | /Game/Megascans/3D_Assets/LichenedForestBoulder/ | 38.2 | 10,456 | 2k textures, cheapest textured rock |
| SM_NordicBoulder_02 | /Game/Megascans/3D_Assets/NordicBoulder/ | 0.9 | 3,100 | LOD-only import, rim distance |
| SM_SandstoneBoulder_01 | /Game/Megascans/3D_Assets/SandstoneBoulder/ | 74.7 | 7,910 | dry-side rim boulder |
| SM_SandstoneBoulder_03 | (shared with arch_rock) | — | — | zero extra disk |

**river_stone** (`Roles.RiverStone`, 67 MB, one shared 2k texture set)

| Asset | Path | MB | tris | Notes |
|---|---|---|---|---|
| SM_SmallStonesPack_08 / _10 / _12 | /Game/Megascans/3D_Assets/SmallStonesPack/ | 0.1 each | 708 / 950 / 716 | LOD-only stones for the 320 no-shadow instances |
| SM_SmallStonesPack_05 | same | 46.1 | 9,332 | Nanite hero stone for the drinking-spot close-up |

**mud_patch** (`Roles.MudPatch`, 159 MB): SM_DryRiverBed_02,
`/Game/Megascans/3D_Assets/DryRiverBed/`, 71.8 MB, 28,511 tris — the only
cracked-mud imagery in the sample (plate B4 floor). Its albedo is
`T_DryRiverBed_01_Fix`; the manifest's `ground_textures.cracked_mud_unique`
still names `T_DryRiverBed_01_BC`, which was **not** migrated (fix the key or
copy the 29.8 MB texture later).

**groundcover** (`Roles.Groundcover`, 122 MB, all `/Game/Megascans/3D_Plants/…`)

| Asset | Folder | MB | tris | Master | Notes |
|---|---|---|---|---|---|
| SM_KikuyuGrass_05, _01 | KikuyuGrass/ | 1.1, 0.8 | 3,028, 1,989 | M_MS_CustomFoliage (WPO wind) | grass clumps = reed stand-in when Z-scaled |
| SM_Fern_05, _01 | Fern/ | 0.1, 0.1 | 401, 306 | CustomFoliage (wind) | wet band |
| SM_CloverVarieties_01 | CloverVarieties/ | 0.3 | 1,716 | CustomFoliageNoWPO | low mat |
| SM_WhiteWindflower_02 | WhiteWindflower/ | 0.4 | 1,538 | CustomFoliage (wind) | 'new growth' accent near resource A nodes |
| SM_DeadLeaves_01 | DeadLeaves/ | 0.3 | 943 | CustomFoliageNoWPO | litter; scale count with DroughtFactor |

**shrub** (`Roles.Shrub`, 107 MB): SM_Elderberry_04 (2.4 MB, 6,160) and
SM_Elderberry_06 (6.0 MB, 7,591) in `/Game/Megascans/3D_Plants/Elderberry/`
(NoOpacity master, cheapest under VSM); SM_Crownbeard_03 (1.4 MB, 5,164) in
`…/Crownbeard/` (tallest herb, reads as reeds at distance).

**tree** (`Roles.Tree`, 162 MB): SM_ForestDeadTree_01
(`/Game/Custom/RS_Assets/ForestDeadTree_01/`, 23.1 MB, 9,198; opaque
RealityScan snag on `M_RealityScan_03`, no wind, also the drought spire) and
SM_Elderberry_08 (`…/Elderberry/`, 47.4 MB, 16,897; the only cheap live
canopy, shares MI_Elderberry_01).

**ground** (295 MB): `/Game/PCG/Assets/Materials/MI_DryGrassDampSoil_02`
(straw-on-damp-soil instance, parent `/Game/MSPresets/M_MS_Surface_Material_VT/M_MS_Surface_Material_VT`)
and the texture sets MossyGrass (BC 39.2, N 53.5, AoRDp 10.7 MB),
MossyRockyGround (32.0 / 39.1 / 26.4), SwampWater (28.1 / 6.9 / 14.7),
DriedGrassOnDampSoil (BC/N/AoRDp), NordicMoss (BC/N/AoRDp), all under
`/Game/Megascans/Surfaces/<Name>/T_<Name>_01_*`.

**water** (72 MB): `/Game/Effects/Environments/PuddleMaterial/T_RippleNormals`
(18.1 MB, 4k BC5), the SwampWater set above, and
`/Game/Custom/CustomMaterials/Materials/Master_Materials/Custom_Decal/T_Water_01_M`
(4.6 MB puddle mask).

**decals** (156 MB): masters
`/Game/MSPresets/M_MS_Decal_Material_VT/M_MS_Decal_Material_VT` and
`…/M_MS_Decal_Material_VT_NoNormalAlbedo1`; instances
`/Game/Megascans/Decals/TileableLeakage/MI_TileableLeakage_01`,
`/Game/Megascans/Decals/AridDebris/MI_AridDebris_01`,
`/Game/Megascans/Decals/TileableMossPatches/MI_TileableMossPatches_01`
(each with its BC / N / DpRM 4k textures).

**fx** (26 MB): `/Game/FX/MistCards/Materials/M_MistCard`,
`MI_MistCard_RGBTiling`, `MI_MistCard_RGB`;
`/Game/FX/MistCards/StaticMesh/SM_MistCard_v2` (246 tris, non-Nanite);
`/Game/Effects/Environments/WaterDrips/NS_WaterDrips_Falling_Placeable`
(1.7 MB) and `/Game/Effects/Environments/Leaves/NS_Leaves_Falling_Frustum_01`
(2.6 MB) with their 8 NiagaraScripts, NPC_Wind, NET_Placeable_WaterDrips;
`/Game/Custom/Lighting/Clouds/T_Cloud2dNoise7` (1.8 MB);
`/Game/Custom/CustomMaterials/Materials/PostProcess/FilmGrain/T_FilmGrain`
(2.5 MB); `/Game/Custom/Lighting/T_DefaultBloomKernel_01` (0.4 MB).

Name uniqueness: `sw_registry.csv` has no duplicate StaticMesh names, so
`FindMeshes` (exact name first, else first prefix match) resolves every stem
deterministically.

---

## 3. What was deliberately left out, and why

| Left out | Size | Why |
|---|---|---|
| `__ExternalActors__`, `__ExternalObjects__`, `Levels/*`, `Asmbly/*`, `PCG/*` (graphs, settings, assemblies, utilities) | ~4.2 GB | Level scaffolding of a World-Partition map and PCG logic; not migratable piecemeal, and SymbioticWorld places every mesh itself with seeded HISM (`ASWEnvironment::BuildImported`). PCG plugin is not enabled in our uproject. |
| `Blueprints/*`, `Input/*`, `Custom/Sequences/*`, `BP_FogCard`, `BP_FrustumBasedSystem`, `BP_GlobalFoliageActor_UE5`, `BP_LensFlareSpawner`, the SP_* plugins and the sample C++ module | small | CLAUDE.md: no Blueprint logic; drone/game-framework/camera classes will not load; every behaviour we need (camera-facing cards, MPC wind values) is two lines of C++. |
| `Audio/*` | 229 MB | Needs Soundscape/AudioModulation/AudioGameplay plugins; out of scope. |
| `Custom/OffroadSigns`, `Megascans/3D_Assets/TrafficSignStand`, `Meshes/Hero_Kite`, `SM_ColorCalibrator`, hover drone | ~230 MB | Civilisation / machines / props; the valley spec forbids anything that reads as built. |
| `SmartAssets/AssetMeshes/SM_Cliff_01..19` + their layer stack | 4.3 GB meshes + 1.1 GB textures | 0.35–0.72 GB per mesh, grey-green nordic tone that fights the sandstone plates, material-layer stack on 4k tileables; SM_Cliff_11..19 ship with no material bound. |
| `Custom/RootsTest`, `Custom/IvyTest`, `Custom/BGSlopes/SM_Slope_01`, `Meshes/_GENERATED/RockFormation/SM_RockFormation_02/_03`, `SM_RiverRocks__PlaneCut`, `SM_SandstoneCliff_01..03` | 0.3–0.9 GB each | Wrong subject (roots, vines) or level-specific merged formations; each one is 5–15 % of the budget and a whole Nanite streaming pool by itself. |
| `Megascans/3D_Assets/QuarryCliff` | 0.93 GB | Quarry faces are man-cut and read as worked stone (spec: never ruins). |
| `Megascans/3D_Assets/MossyRocks`, `MossyRockFace`, `HugeSandstoneCliff_02/_05`, `MassiveCanyonSandstoneMesa`, `GiganticSandstoneTerrain`, `MassiveSandstoneCliff_06` (256-tri proxy), `HugeMossyForestCliff` (placeholder), `MossyForestBoulder_07` (no material) | 0.1–0.4 GB each | 2–3× the disk of an equivalent pick, terrain-scale slabs that `Place()` would crush, or placeholder assets. |
| Duplicates: `SandstoneBoulder_02` (= _01), `HugeSandstoneCliff_04` (= _03), `_08` (= _07), `ErodedRockyGround_03` (= _02) | — | Identical size and fallback counts in the registry. |
| `Megascans/3D_Plants/EuropeanHornbeam/**` (28 trees, PivotPainter variant) | 1.02 GB | 3–4 material slots, 13–34k fallback tris, a 272 MB all-4k texture chain, 313 MB resident — the one Nanite foliage class that does not fit next to 220 creature PMCs on 8 GB. |
| Tropical/houseplant species (palms, Taro, JungleGinger, Croton, Dracaena, MoneyPlant, …), Ivy/EnglishIvy, `SilverLadyFern`, heavy scan variants (`Elderberry_09`, `BeechFern_05/09-12`, `Clover_04/05`, `WhiteWindflower_01/05`) | ~1.5 GB | Wrong biome, climbers, or 15–57 MB scans of species already covered. |
| All `FTI_*` FoliageTypes, `MS_Foliage_Material_LATEST/GlobalFoliageActor` | small | Foliage-tool settings the C++ placer never reads. |
| Substrate-native materials: `M_PuddleWater`, `M_MudPuddle_01_PCG` + MIs, `M_SunLightFunction_00` + MI, `M_BGLandscape_Auto`, `M_Skybox`/`MI_Skybox_01`, `Substrate/*`, `M_MS_Default_Material_VT_WaterTest`, `MF_WaterEffect`'s 222 MB experiment set | ~0.4 GB | No legacy pins: with `r.Substrate=0` they render grey/black (light function would black out the sun). Their textures were taken instead (T_RippleNormals, SwampWater, T_Cloud2dNoise7). |
| `Landscape/M_BGLandscape_Auto` | 406 MB closure | Landscape/auto material on Substrate; cannot drive our `UProceduralMeshComponent`. |
| `Megascans/Decals/Leakage/MI_Leakage_02..13`, GreenVine, JungleDebris, DriedBamboo, ScatteredBark, PlantsPerennials | 0.5 GB | Wall stains, wrong biome, or redundant with the three decal picks. |
| `Custom/Lighting/Clouds` profile clouds, `MI_MistCard_RGBTiling` from `/Game/PCG` (second 8.7 MB atlas), `MI_Leakage_01` | 9–58 MB | Cheap optional adds; clouds are off by decision (`bClouds=false`). |
| Sample cvars: `r.Substrate=True`, `r.Nanite.Max*` buffers, `r.RayTracing`, `r.Lumen.HardwareRayTracing`, `r.TSR.History.SampleCount=32`, `r.MSAACount=4`, global `r.Streaming.MipBias=1` | — | Sized for a 4 km map on a desktop GPU; Substrate would recompile every shader (20–40 min, several GB of DDC) and quadruple the GBuffer. |

Cheap optional adds if wanted later, each self-contained: TundraMossyBoulder_01
(+205 MB), NordicBeachRocks_01 (+160 MB), SM_Log_01 (+84 MB), BeechFern_04
(+51 MB), MI_Leakage_01 (+37 MB), T_DryRiverBed_01_BC (+30 MB).

---

## 4. How `ASWEnvironment` should consume each role

**How it works today** (`Source/SymbioticWorld/SWEnvironment.cpp`).
`LoadRoles()` reads `AssetSources/ed_manifest.json` (default of
`FSWLookSettings::AssetManifest`), overrides each built-in Poly Haven list
when the manifest list is non-empty, sets `Roles.Root = /Game`, and reads
`mud_patch`, `fog_card_mesh`, `fog_card_material`, `ground_material`,
`ground_material_wet`, `water_material` and `niagara.{mist,motes,dust}`.
`FindMeshes()` scans the registry under `/Game` (409 assets now) and resolves
stems by asset name. `MakeInstanced()` creates one
`UHierarchicalInstancedStaticMeshComponent` per mesh (NoCollision, Static,
shadows on unless turned off per role, `RockMID` override when
`bImportedRocksUseProjectMaterial`). `Place(C, X, Y, Size, Sink, PitchJitter)`
scales the mesh so its **longest extent = Size** (uu), puts it at
`TerrainHeight − Sink·Size`, random yaw, ±PitchJitter pitch/roll, all from the
seeded `FRandomStream(LookSeed·7+3)` / `(LookSeed·11+5)`. Because scale is by
longest extent, a 25 m cliff scan at 900–3000 uu keeps roughly 1:1 texel
density, a 1.5 m boulder stretched to 520 uu loses ~3×, and a 20 cm stone at
110 uu gains.

**Per-role table.** "Now" = the literal/field in the code today; "Target" =
what the perf review (RTX 4060 Laptop 8 GB, 16.4 ms GPU before this content)
and the plates ask for. Counts marked ★ must move before the demo.

| Role | Manifest key / `FSWLookSettings` fields | Where placed | Size (uu, longest extent) | Sink | Pitch jitter | Shadows | Count now → target |
|---|---|---|---|---|---|---|---|
| cliff | `cliff` → `Roles.Cliff`; `CliffCount`, `CliffSizeMin/Max`, `bImportedRocksUseProjectMaterial` | valley walls (\|y\| = 0.58–0.98 ValleyHalfWidth) and far end (every 4th), start corridor kept clear | 900–3000 (keep; Huge cliffs are 20–30 m scans, ~1:1 texels at the top of the range) | 0.36 (keep: Megascans cliffs are plane-cut, the sink hides the seam) | 5° | on | 48 → **26 ★** (`Look.CliffCount=26`, the ladder value; ≤ 6 distinct meshes, which we have) |
| arch_rock | `arch_rock` → `Roles.ArchRock`; `ArchRockCount` (per arch base; 5 arches × 2 bases × 5 = 50) | ring of radius 60–420 uu around each `ArchBases` entry | 220–620 (literal; consider an `ArchRockSizeMin/Max` pair; MassiveTundraRockFormation_02 wants the top of the range) | 0.3 | 12° | on | 50 → 50 (fine; drop to 3 per base if the frame needs it) |
| boulder | `boulder` → `Roles.Boulder`; `ImportedBoulderCount`, `BoulderSizeMin/Max` | 50 % ring around the arena (Arena+100..1600), 30 % river banks (1.4–2.4 RiverWidth), 20 % floor | 120–520 (keep; mossy family on the wet side, sandstone on the rim) | 0.22 | 15° | on | 110 → 110 (ladder 70); ≤ 6 distinct meshes ✓ |
| river_stone | `river_stone` → `Roles.RiverStone`; `RiverStoneCount` | both banks, 0.6–1.9 RiverWidth from `RiverCenterY` | 25–110 (literal; keep) | 0.3 | 25° | off | 320 → 320 (all four share one 2k set) |
| mud_patch | `mud_patch` → `Roles.MudPatch`; **no field yet** (literal 70 attempts, 27 placed) | shoreline 1.1–2.6 RiverWidth, skipped under water | 500–900 (literal; the scan is a ~3–4 m slab, so 1.2–2.5× — acceptable) | 0.12 | 2° | off | add `MudPatchCount` (wet 20–30, drought 60–120) and scale the visible count or a per-instance custom-data fade with `DroughtFactor`; use pitch 0 and sink 0.05–0.08 if patches float or vanish on the first screenshot |
| groundcover | `groundcover` → `Roles.Groundcover`; `GroundcoverCount`, `GroundcoverSizeMin/Max` | anywhere within Arena+1800, accept = 0.12 + 0.88·Wet, none under water | 140–320 × (0.8 + 0.5·Wet) (keep; Kikuyu at 320 is ~1 m, right for a clump) | 0.02 | 4° | off | 5000 → **2500 ★** (ladder 1400); cull distance `SetCullDistances(0, 14000)` |
| shrub | `shrub` → `Roles.Shrub`; `ShrubCount` | bank band 10 uu..4·WetlandBand above water, thinning outward | 160–360 (literal) | 0.04 | 4° | off | 260 → **200 ★** |
| tree | `tree` → `Roles.Tree`; `TreeCount` | rim, \|y\| from Arena+900 to 0.72 ValleyHalfWidth, x within ±0.6 TerrainHalfSize | 900–1900 (literal; right for the snag, 3–6× stretch for Elderberry_08 — add `TreeSizeMin/Max` or per-stem scale, e.g. 700–1300 for Elderberry) | 0.02 | 3° | **on** (only tree HISMs cast) | 28 → **16–20 ★**, half snag / half live |
| fog cards | `fog_card_mesh` / `fog_card_material` → `Roles.FogCardMesh/Material`; `MistCount` | evenly along the channel, ±0.8 RiverWidth, Z = WaterLevel + 60, random yaw | 1400–2600 (literal) | n/a | 0 | off | 14 → **6–8 ★** (translucent overdraw); material is assigned directly — switch to a MID so `ApplyDrought` can lerp it (§5) |
| niagara.mist (drips) | `niagara.mist` → `Roles.NiagaraMist` | `SpawnSystemAtLocation` at each `WaterfallLips` + 30 uu, scale 3 | — | — | — | — | 3 lips → 0 spawned in the 17:10 run (§6); cap ≤ 10, add `ArchBases` undersides once it spawns; never time-dilate at 50× |
| niagara.dust (leaves) | `niagara.dust` → `Roles.NiagaraDust` | **not consumed yet** | — | — | — | — | attach to the camera pawn, activate only while `DroughtFactor > 0`, straw tint, capped `User.SpawnCount` |
| decals | `decals` (manifest array) | **not read by `LoadRoles` yet** | 200–600 uu decal boxes | — | — | — | ≤ 200 via `UGameplayStatics::SpawnDecalAtLocation`, fade with `DroughtFactor` |

Groundcover species order matters: with `Wet ≤ 0.5` the placer picks only from
the **first half** of the list (`Comps.Num()/2` = 3 of 7), so the first three
stems are the dry-floor species. Today those are `SM_KikuyuGrass_05`,
`SM_KikuyuGrass_01`, `SM_Fern_05` (a fern on the dry floor). Reorder the
manifest to `["SM_KikuyuGrass_05","SM_DeadLeaves_01","SM_CloverVarieties_01",
"SM_KikuyuGrass_01","SM_Fern_05","SM_Fern_01","SM_WhiteWindflower_02"]` so the
dry half is grass + litter + clover and ferns/windflowers stay in the wet band.

Distinct Nanite meshes placed: 33 (20 rocks incl. the mud patch, 12 plants,
plus the non-Nanite card) against the perf review's guideline of ≤ 25. If
`stat gpu` shows Nanite raster or VSM page cost, the cheapest trims are
dropping `SM_Fern_01`, `SM_KikuyuGrass_01`, `SM_SmallStonesPack_12` and
`SM_Elderberry_06` from the manifest (0 disk change, 4 fewer HISMs).

**New `FSWLookSettings` fields worth adding** (all `UPROPERTY(EditAnywhere)`,
settable via `--set`): `MudPatchCount=24`, `DroughtMudPatchCount=90`,
`MudPatchSizeMin/Max=500/900`, `FogCardCount=8` (replace the reuse of
`MistCount` for cards), `FogCardSizeMin/Max=1400/2600`, `FogCardHeight=60`,
`FogCardIntensity=1.0`, `DroughtFogCardIntensityScale=0.6`,
`ShrubSizeMin/Max=160/360`, `TreeSizeMin/Max=900/1900`,
`ArchRockSizeMin/Max=220/620`, `RiverStoneSizeMin/Max=25/110`,
`DecalCount=120`, `bDroughtDebris=true`, `DebrisSpawnCount=60`,
`ImportedCullDistance=14000`. Defaults for the demo: `CliffCount=26`,
`GroundcoverCount=2500`, `ShrubCount=200`, `TreeCount=18`, `MistCount=8`.

A housekeeping item seen in the log: `MakeInstanced()` sets the HISM to
Static mobility and then attaches it to the movable `Root`, which prints
`AttachTo: … Root is not static, cannot attach …` once per HISM (36 lines).
Either set mobility after `RegisterComponent`, or make `Root` static; the
components still render.

---

## 5. Ground, water, decal and Niagara wiring

### 5.1 Terrain (`UProceduralMeshComponent`, `BuildTerrain`)

- Resolution order in code: `Roles.GroundMaterial` (manifest `ground_material`,
  null on purpose) → `/Game/Materials/M_SW_TerrainED` (exists,
  `Content/Materials/M_SW_TerrainED.uasset`, built by `Tools/make_materials.py::terrain_ed`)
  → `M_SW_Terrain`. `M_SW_TerrainED` samples the migrated tileables with
  world-aligned UVs: moss grass on flats, MossyRockyGround on slopes (vertex
  normal), SwampWater mud in the wet band (vertex colour B), DriedGrassOnDampSoil
  as the drought state. Parameters: `Tile` (400 uu), `Dryness` (0..1, set by
  `ApplyDrought`), `Tint` (vector). `BuildTerrain` also sets `RockColor` /
  `MossColor`, which this material does not have (harmless).
- **Sampler-type check (do first).** `terrain_ed()` uses the default colour
  sampler and `SAMPLERTYPE_NORMAL`, but the Megascans textures are virtual
  textures. UE reports "Sampler type is Color/Normal, should be Virtual Color /
  Virtual Normal" and the material falls back to the default grey. The 17:10
  log has no `LogMaterial` error, but no post-migration screenshot exists.
  Fix in `make_materials.py`: if `texture.get_editor_property("virtual_texture_streaming")`
  use `SAMPLERTYPE_VIRTUAL_COLOR` / `SAMPLERTYPE_VIRTUAL_NORMAL`. Alternative
  for the three sets no migrated MIC uses (MossyGrass, MossyRockyGround,
  SwampWater): clear `virtual_texture_streaming` in the texture pass; keep VT
  on DriedGrassOnDampSoil (shared with `MI_DryGrassDampSoil_02`).
- `MI_DryGrassDampSoil_02` (`ground_material_dry` in the manifest, not read by
  C++) is the ready-made straw ground. Parent `M_MS_Surface_Material_VT`
  parameters (from the instance name tables): textures `Albedo`, `Normal`,
  `ARD` (packed AO/Roughness/Displacement = `*_AoRDp`); scalars/vectors
  `Tiling/Offset`, `Albedo Tint`, `Normal Strength`, `Metalness`,
  `Animate Surface`, `Wind Gust Strength`, `Wind Noise Strength`. Do not name it
  as `ground_material`: `bUseImportedGroundMaterial` defaults to true, so the
  whole valley would turn straw in the wet state and lose the `Dryness` lerp.
  To make lush MICs from Python instead: `MaterialInstanceConstantFactoryNew`,
  `set_material_instance_parent(mic, M_MS_Surface_Material_VT)`,
  `set_material_instance_texture_parameter_value(mic, "Albedo"/"Normal"/"ARD", T_*)`.

### 5.2 Water (`BuildWater`)

- `water_material` is null, so `M_SW_Water` stays (parameters `WaterColor`,
  SSR on). The sample's water materials are Substrate-native
  SingleLayerWater and render grey without Substrate — do not use them.
- Upgrade path in `make_materials.py::water()`: replace the two noise-derived
  normals with two panned samples of `T_RippleNormals` (different tiling /
  speed, `BlendAngleCorrectedNormals`), `sampler_type =
  SAMPLERTYPE_VIRTUAL_NORMAL`; cap the texture at 1024 (it tiles). The
  SwampWater set gives the amber-brown shallows on the terrain below
  `WaterLevel` (already the mud layer of `M_SW_TerrainED`). `T_Water_01_M` is
  the mask for a Python `MD_DEFERRED_DECAL` puddle whose box scale shrinks with
  `DroughtFactor`.
- `ApplyDrought` today: sun colour/temperature/pitch, Mie scale, fog colour,
  density, volumetric albedo and directional colour, water plane drop
  (`DroughtWaterDrop` 55 uu), `Dryness` on terrain (F) and rock (0.6·F),
  white balance, highlight gain, saturation. Add: mist-card MID, mud-patch
  count, decal fade, leaves activation, wind MPC `Season Strength`.

### 5.3 Decals

- The three instances are saved with Substrate-native parents
  (`M_MS_CustomDecal_CNR` for moss/debris, `_CR` for leakage) and render as
  flat grey squares as-is. One Python pass before use:
  `set_material_instance_parent(mic, M_MS_Decal_Material_VT)` (leakage →
  `M_MS_Decal_Material_VT_NoNormalAlbedo1`, exactly as the sample's own
  `/Game/PCG/Assets/Materials/MI_Leakage_01` does), then bind `T_*_DpRM` to
  both `Opacity` and `Roughness` and confirm with
  `MaterialEditingLibrary.get_texture_parameter_names`. Master parameters:
  textures `Albedo`, `Normal`, `Opacity`, `Roughness`, `Heightmap Texture`,
  `Metallic Map`; scalars `Opacity Intensity`, `Roughness Intensity`,
  `Normal Intensity`, `Overlay Intensity`, `Crop X/Y`, `Height Ratio`;
  switches `UsePOM` (set false), `Use World Coordinates`; vector `Color Overlay`.
  If the DpRM channel needs a select, author a 10-line `MD_DEFERRED_DECAL`
  master in `make_materials.py` (Albedo + DpRM.B as opacity).
- Consumption: add `Roles.Decals` (array) to `LoadRoles`, spawn with
  `UGameplayStatics::SpawnDecalAtLocation` along `RiverCenterY ± RiverWidth·(1.0–1.6)`
  with seeded rotation/scale (200–600 uu), cap 200, `FadeStartDelay/FadeDuration`.
  Usage: `MI_TileableLeakage_01` = damp ground / drying-pool rings at the water
  edge, fade out with `DroughtFactor`; `MI_AridDebris_01` = dry litter spawned
  when `DroughtFactor > 0.5`; `MI_TileableMossPatches_01` = moss on the
  wet-band banks (plate B2). Decals only affect opaque receivers, which the PMC
  terrain is.

### 5.4 Niagara (`"Niagara"` is already in `SymbioticWorld.Build.cs`)

- `NS_WaterDrips_Falling_Placeable`: GPU sprite drips with depth-buffer
  collision (lands on the PMC terrain without collision meshes). User
  parameters `User.SpawnRate`, `User.NumberOfEmissionPoints`. Effect type
  `NET_Placeable_WaterDrips` came along. Its sprite material
  `M_WaterDrips_Base` is Substrate-native with only Emissive/Normal/Roughness
  legacy pins, so drips read darker than in the sample; fix with
  `MaterialEditingLibrary.connect_material_property` (BaseColor) or accept.
  **Observed:** the 17:10 run loaded the system without a "not found" warning
  but `SpawnSystemAtLocation` returned null for all 3 lips (`0 drip systems`,
  `mist 0`). Most likely the system's scripts are not compiled for our project
  (the sample's DDC is not reused; an uncooked `-game` run does not compile
  Niagara), so `IsReadyToRun()` is false. Open the project once in the editor
  (or the `-ExecutePythonScript` route, `unreal.NiagaraSystem` request
  compile + save) so the compiled data is stored, then re-run; also check
  `LogNiagara` for "GPU simulation not supported" lines
  (`UAV Float16 is not supported` only disables compressed emitters).
- `NS_Leaves_Falling_Frustum_01`: frustum-recycled mesh leaves (dead-leaf 1k
  textures on a 162-tri grid). User parameters: `User.Color`,
  `ColorRandomTintMin/Max`, `SpawnCount`, `WindSpeed`, `WindSpeedScale`,
  `TurbulenceScale/Frequency`, `Gravity`, `RecycleHeight`, `Near/Far Distance`,
  `Width/Height Coverage`, `MeshUniformScaleMin/Max`, `FadeIn/OutSeconds`,
  `UseNiagaraParamaterCollectionWind` (sic). The FrustumRecycler expects camera
  parameters that the sample's Blueprint pushed; set them from C++ each frame
  or accept the defaults. Use as the drought debris (straw tint ≈ 1.0, 0.8,
  0.45), attached to the camera pawn, active only while `DroughtFactor > 0`.
- `niagara.motes` is null: nothing suitable in the sample; use
  `/Niagara/DefaultAssets/Templates/Systems/FountainLightweight` with a
  6-line additive sprite material from `make_materials.py`, or the planned ISM
  motes with `M_SW_Glow`.

### 5.5 Mist cards, light function, post-process

- `SM_MistCard_v2` + `MI_MistCard_RGBTiling` (legacy-compatible; translucent,
  two-sided, sky-atmosphere lit; `M_MistCard` uses `TLM_VolumetricDirectional`
  so keep the SkyAtmosphere actor). Parameters: `Fog Color`, `Intensity`,
  `Contrast`, `FadeDistance`, `Camera Fade Exponent/Low End`,
  `EdgeFadeExponent`, `PanningSpeedX/Y`, `TilingTextureX/YScale`,
  `RGB Lighting`, `SkyAtmosphereLightAmount`, `TextureTop/Bottom/BackLightColor`
  (confirm with `GetAllScalarParameterInfo`). Create one MID per card in
  `BuildImportedFeatures`, yaw the cards toward the start camera, and in
  `ApplyDrought` lerp `Fog Color` → `DroughtVolumetricFogAlbedo` and
  `Intensity × (1 − 0.4·F)`. Keep cards over the channel behind the arena,
  never between the follow camera and the Lumen.
- `T_Cloud2dNoise7` → a `MD_LIGHT_FUNCTION` material authored in
  `make_materials.py` (panned `WorldPosition.xy` × tiling, `CheapContrast`),
  assigned with `DirectionalLightComponent::SetLightFunctionMaterial`;
  drifting cloud shadow, contrast up under drought, pan < 20 uu/s under VSM.
- `T_FilmGrain` → `FPostProcessSettings::FilmGrainTexture` with the existing
  `FilmGrain = 0.06`; `T_DefaultBloomKernel_01` → `BloomMethod = BM_Convolution`
  only if 1× lands ≤ 14 ms.

### 5.6 Wind, textures, VT pools

- `MF_SimpleWind` reads `MPC_GlobalFoliageActor` scalars `Wind Strength`,
  `Wind Speed`, `Wind Strength Plants`, `Wind Speed Plants`, `Wind Tiling`,
  `Wind Noise`, vector `Wind Direction`, plus `Season Strength/Brightness/Saturation`,
  `Health`. The sample's Blueprint that drives them is not migrated, so set
  them once from C++ (`UKismetMaterialLibrary::SetScalarParameterValue`) or
  accept static plants; `Season Strength` is a text-authored drought lever.
  `MI_KikuyuGrass_01`, `MI_Fern_01`, `MI_WhiteWindflower_01`, `MI_Crownbeard_01`
  carry WPO wind (Nanite programmable raster + VSM invalidation); if `stat gpu`
  shows it, `MakeInstanced` them with `M_MS_CustomFoliageNoWPO`.
- Texture pass (editor Python, one run): `max_texture_size = 2048` on every
  migrated `Texture2D` except the six cliff sets and the two arch-formation
  sets (keep 4k), `1024` on `T_RippleNormals`. That takes the fully resident
  set from ~2.5 GB to ~1.2 GB inside the 2000 MB pool.
- VT pools: copy the sample's `[/Script/Engine.VirtualTexturePoolConfig]` block
  into `Config/DefaultEngine.ini` only if `LogVirtualTexturing` pool warnings
  appear.

---

## 6. Risks and fallbacks

| Risk | How it shows | Fallback |
|---|---|---|
| A Megascans master does not compile under legacy shading (unverified until the first migrated MIC renders) | rocks in world-grid default / grey; `LogMaterial` errors | `--set "Look.bImportedRocksUseProjectMaterial=true"`: every cliff/boulder/arch-rock/river-stone HISM gets `M_SW_Rock` (`RockMID`, tinted by `RockColor`/`MossColor`, `Dryness` still lerps) and keeps the Nanite geometry. Plants have no such flag: assign the project's masked two-sided materials via `MakeInstanced` or drop the species from the manifest. |
| `M_SW_TerrainED` fails on VT sampler types | terrain grey/default | falls back automatically to `M_SW_Terrain` (procedural); fix the sampler types in `make_materials.py` (§5.1) |
| `M_WaterDrips_Base` / decal masters are Substrate-native | drips too dark; decals grey squares | connect BaseColor in Python; re-parent the decal MICs (§5.3); until then leave `decals` unconsumed |
| Drip Niagara returns null (`0 drip systems` in the 17:10 run) | no spray at the lips | the translucent glow spray cluster in `BuildWaterfalls` is still there; compile the Niagara system once in the editor (§5.4); or point `niagara.mist` at `/Niagara/DefaultAssets/Templates/Systems/FountainLightweight` |
| Frame budget (17.1 ms at 50×/119 agents before this content; 33 Nanite meshes, 82 4k textures) | `stat unit` > 16.7 ms at 1×, hitching on the rail sweep | apply the counts in §4 first; then the rebuild-free ladder from VISUAL_PLAN §5 (`Look.bClouds=false` → `r.Shadow.Virtual.Enable=0` → `r.ScreenPercentage=70` → `Look.GroundcoverCount=1400;Look.CliffCount=26;Look.ImportedBoulderCount=70` → `Look.bCreatureShadows=false`); texture pass (§5.6); `r.Nanite=0` via `Look.ConsoleCommands` draws the 3–36k fallback meshes, survivable for rocks, not with 2500 cards |
| Disk (C: ~20 GB free after the copy; first open adds shader + Nanite DDC, likely 1–3 GB) | build or DDC failures | nothing else to migrate; delete `Content/Assets/PolyHaven` (644 MB) once the ED dressing is accepted; never enable Substrate (full recompile, several GB) |
| Manifest side effect: it loads on every run and replaces the Poly Haven lists | if the ED folders are absent, `BuildImported` returns false → procedural rocks only | `--set "Look.AssetManifest=none"` restores the built-in Poly Haven roles |
| Name resolution by prefix | a stem like `SM_Fern_0` would silently pick `SM_Fern_01` | all stems are exact names; keep it that way when editing the manifest |
| Git / licence | Content folders untracked; the repo has no commits yet | add `Content/Megascans/`, `Content/MSPresets/`, `Content/Custom/`, `Content/Effects/`, `Content/FX/`, `Content/PCG/`, `Content/PhysicalMaterials/`, `Content/SmartAssets/`, `Content/Textures/` (and `Content/Assets/`) to `.gitignore` before the first commit; credit line in README |
| Manifest key mismatch | `ground_textures.cracked_mud_unique` names `T_DryRiverBed_01_BC` (not migrated) | use `T_DryRiverBed_01_Fix` (present) or copy `_BC` (+29.8 MB) |
| Foreign editor/game processes | build fails with a locked DLL | `tasklist | findstr UnrealEditor` before every build (CLAUDE.md hard rule) |

---

## 7. Ordered chores before the first ED screenshot

1. `Config`-free changes in `AssetSources/ed_manifest.json`: reorder
   `groundcover` (§4), fix `cracked_mud_unique`.
2. Editor-Python pass (one `-ExecutePythonScript` run, no C++ build): texture
   `max_texture_size`, decal re-parent, `M_WaterDrips_Base` BaseColor, Niagara
   compile + save, regenerate `M_SW_TerrainED` with virtual sampler types,
   author the light-function material.
3. C++ (when no UnrealEditor process is running): `MudPatchCount` +
   drought coupling, fog-card MIDs + drought lerp, `Roles.Decals` + spawner,
   `NiagaraDust` attach, MPC wind values, cull distances, the HISM mobility
   warning; defaults `CliffCount=26`, `GroundcoverCount=2500`, `ShrubCount=200`,
   `TreeCount=18`, `MistCount=8`.
4. Windowed self-screenshot at 1× with `Look.ConsoleCommands=r.vsync=0|stat=unit`
   and at 50×/220 founders; compare to `docs/plates/` (copy the four plates
   there first).
5. `.gitignore` the ED folders; add the credit line to README.

Files this review depends on: `AssetSources/ed_registry.csv`,
`AssetSources/ed_manifest.json`, `AssetSources/ed_migrate_log.txt`,
`AssetSources/sw_registry.csv`, `Tools/migrate_ed.py`,
`Tools/registry_dump.py`, `Tools/make_materials.py`,
`Source/SymbioticWorld/SWEnvironment.cpp/.h`, `Source/SymbioticWorld/SWTypes.h`.
