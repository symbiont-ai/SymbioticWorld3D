# Symbiotic World — visual track plan for hack day 2026-09-06

Synthesised 2026-09-05 evening from three candidate plans (engine-only,
Fab-first, creatures-and-light), three judge verdicts, the engine/download/
Blender/plates inventories, and a read of the current tree (`Source/`,
`Tools/`, `Config/`, `Saved/Screenshots`, `Saved/Logs`). Everything below is
text-authored (C++, editor Python, `.ini`, bpy). Paths are absolute where an
agent will type them. Nothing in this plan touches project files today; the
first block starts at 09:00 tomorrow.

Plates: `docs/plates/` is empty. The four Appendix-B plates are in the session
scratchpad as
`<scratch>/plates/p18_img0_1672x941.jpeg`
(B1 world overview), `p18_img1` (B2 Lumen), `p18_img2` (B3 Tecton),
`p18_img3` (B4 drought), plus `crop_*.png` details. First agent action tomorrow:
copy those four into `docs/plates/plate1_world_overview.jpeg … plate4_drought.jpeg`.

---

## 1. Where we are vs the plates

1. **Environment (~3.5/10 vs B1).** The tree already renders a procedural valley with a river plane, four torus arches, Poly Haven scanned cliffs/boulders/groundcover placed by seeded HISM (`ASWEnvironment::BuildImported`, defaults now 48/110/5000), five Python-generated materials and volumetric fog. Against B1 it fails on: no visible low warm sun, no light shafts, flat grey haze (`FogColor 0.30/0.46/0.50`, default cloud coverage), four equal arches in a row that read as an aqueduct (a spec non-goal), a scanned cliff slab standing vertically mid-valley like a dead tree (`Place()` scales by longest extent, sinks 18–28 %), no shoreline, no strata.
2. **Creatures (~2/10 vs B2/B3).** Lumen is a blue-spotted blob on stick legs (dorsal stripe is a solid band, no dots, no sleekness). Tecton is an amber-blotched **mushroom cap on stilts** — the seam mask `1-|N|/0.12` covers most of the back, the raised noise lumps read as cap spots, and legs are 30 uu stalks under a 300 uu dome. "No fungi on Tecton" is a locked non-goal, so this is the one thing that must change.
3. **Information grammar (0/10).** No signal arcs, no trails, no motes; resource nodes are green/yellow glow blobs. The HUD is structurally right (dark panels, cyan/amber, truthful inherited-vs-learned Q table, seed and mode shown) but text-only.
4. **Drought (~2/10 vs B4).** A colour shift, fog density change and a 55 uu water drop; no dust, cracks, straw vegetation, retreated shoreline or warmer white balance.
5. **Performance: no headroom.** `Saved/Screenshots/WindowsEditor/SW_20260905-104854_seed1_C_learning_evolution_t0200.png` (stat unit) shows Frame 17.14 ms, Game 6.92, Draw 17.12, RHIT 10.10, GPU 16.37 ms at 50x with 119 agents, RenderRes 83.4 % (1336x751), 745 draws, 1.1 M prims — and that was with the *smaller* 26/70/1400 dressing. Lumen GI + VSM + TSR + volumetric fog + clouds are all on. Anything added must be paid for first.

---

## 2. Decision

**Plan A (engine-only, fully scripted) wins**, with blocks grafted from C and B.
Judge totals (fidelity + feasibility + automation + perf-safety, /40):

| Plan | Judge 1 | Judge 2 | Judge 3 | Sum |
|---|---|---|---|---|
| A engine-only | 30 | 31 | 30 | **91** |
| B Fab-first | 19 | 18 | 20 | 57 |
| C creatures + cinematography | 26.5 | 25 | 27 | 78.5 |

Why A: every API it names exists in the 5.7.3 headers on this machine (checked
again tonight: `SetAerialPespectiveViewDistanceScale`,
`SetDirectionalInscatteringStartDistance`, `SetSkyAtmosphereAmbientContributionColorScale`,
`SetEnableLightShaftBloom/SetBloomScale/SetBloomTint/SetTemperature`,
`SetLowerHemisphereColor`, cloud `SetMaterial/SetViewSampleCountScale`,
`BatchUpdateInstancesTransforms`, `SetNumCustomDataFloats`,
`SpawnDecalAtLocation`, `FadeStartDelay/FadeDuration`); its only authoring
gate (`Tools/make_materials.py` under `-run=pythonscript`) is the route that
already built the five `M_SW_*` assets; it needs zero downloads and zero
editor clicks; every fallback is a `-SWSet` toggle. It also puts the
atmosphere pass first, which the plates analysis ranks as the highest
impact per hour and which B and C both leave to the end or skip.

**Grafted from Plan C:** deterministic camera rail (`-SWDemo=1`, key C, 8 keys
+ timed beats) and the agent-produced backup video via
`-benchmark -fps=30 -dumpmovie` + ffmpeg (flags verified at
`LaunchEngineLoop.cpp:2412/4240/4662`, frames typed `MovieFrame` in
`UnrealClient.cpp:274`); `stat unit` baked into perf screenshots; Tecton
furrow decals behind a single `ASWEnvironment::AddFurrow` entry point (if
time remains); the Blender FBX creature fallback with a vertex-colour bake so
`M_SW_Creature` works unchanged; the "3 iterations then ship" cap.

**Grafted from Plan B:** git worktree lane isolation for the research lane;
the already-imported Poly Haven grass/fern as the reed layer (instead of A's
13k engine cones stacked on top of 5000 groundcover cards); engine fog sheets
(`S_EV_FogSheet_01` + `MI_Fogsheet_HideClose`) as wetland mist that survives
the perf cut; `r.Streaming.PoolSize` in the ini; recording winning `-SWSet`
strings in `PROGRESS.md` before baking them; `Look.ArchCount=2` as the
zero-cost anti-aqueduct interim from 09:05.

### Every fatal flaw the judges found, and how this plan avoids it

| # | Flaw (judge) | Avoidance |
|---|---|---|
| 1 | `UnrealEditor-Cmd -run=pythonscript` + `AssetTools.import_asset_tasks` crashes after the first asset (`Assertion failed: CurrentApplication.IsValid()`, log 14.39.11) — breaks Plan B B1 and Plan C B1 as written. | No import is on the critical path. If the Blender creature fallback is taken, the import runs through `UnrealEditor.exe … -ExecutePythonScript=…` (the route that imported the 29 Poly Haven assets 14:42–14:46, documented in `Tools/import_assets.py`). `make_materials.py` stays on `-run=pythonscript` (proven 10:17). |
| 2 | `GAverageFPS/GAverageMS` are not declared in any public 5.7 header (Plan B's first build fails). | `extern ENGINE_API float GAverageFPS; extern ENGINE_API float GAverageMS;` at file scope in `SWHUD.cpp` (definitions verified at `UnrealEngine.cpp:735-736`), plus the manager's own 5-s `SW perf` accumulator so the log has numbers even in `-nullrhi`. |
| 3 | All plans budget against guessed baselines; the measured frame is already 17.1 ms (GPU 16.4) at 50x/119 agents, `r.ScreenPercentage=85` is a no-op (already 83.4 %), 220 skinned VSM casters (B) or 1100 non-Nanite components (C) cannot fit. | Cut ladder is a **day-1 decision, not a fallback**: Lumen GI off + SSR from 09:00 in the ini (section 5); clouds decided at 09:25 by screenshot; VSM kept but `r.Shadow.Virtual.Enable=0` is one `-SWSet Look.ConsoleCommands` away; 220-agent stress run forces founders (`Settings.InitialLumen=180;Settings.InitialTecton=40`) so the number is real; vsync off (`r.vsync=0`) for every measurement; 50x beat rehearsed at 10x as the fallback. |
| 4 | Plan C B2 replaces 220 PMC bodies with up to 1100 non-Nanite static-mesh components. | Not taken. One `UProceduralMeshComponent` body per agent stays; leg motion is a material WPO (stretch) or none. The FBX fallback imports the *merged* `SM_Tecton.fbx` (one component per agent). |
| 5 | Plan B's VRAM guard resaves every >2048 texture inside the import session (tens of seconds each, can eat the block). | No Fab packs. Poly Haven scans are 2k. `r.Streaming.PoolSize=2000` in the ini instead. |
| 6 | Stale baseline: the 10:47 build defaults `bUseImportedAssets=true` and shows a vertical cliff slab; no plan owns it. | B0.7 owns it: pivot-aware `Place()` (scale by footprint, sink by bounds min Z, pitch jitter 4°), bounds logged per mesh, counts back to 26/70/1400 unless the 09:20 measurement says otherwise. If it is not fixed by 09:40, ship `Look.bUseImportedAssets=false` (procedural rocks return automatically). |
| 7 | Plan A names the cloud coverage parameter `GlobalCoverage`. | Read the name table of `m_SimpleVolumetricCloud.uasset` tonight: **`Cloud_GlobalCoverage`, `Cloud_GlobalDensity`, `Cloud_AlbedoColor`**. Those names are used; the `get_scalar_parameter_names` print stays as confirmation. |
| 8 | Worktrees need a commit; the repo has zero commits and CLAUDE.md forbids unasked commits. | Human step 1 at 08:55 (one baseline commit, `Content/Assets/` gitignored first so 615 MB stays out). Fallback: lock-file protocol (`Saved/BUILD_LOCK`) and serialized builds; the day still works, ~20 % slower. |
| 9 | Plan A's "50 x 220 = 11k transform writes per frame" is ~6x too high. | Corrected: ~8 substeps/frame at 50x/60 fps, ~1.8k writes. `ApplyVisualFrame()` still lands (it is what makes the per-frame MID gait parameter possible) but is booked as ~1 ms, not the 50x fix. `Step()` itself is the research lane's cost. |
| 10 | Plan B claims `-SWSet` is numeric/bool only. | False: `SetStructPropertyByName` handles `FString`, `FLinearColor` (`r:g:b`) and enums (SWWorldManager.cpp:155-196). The plan uses that for `Look.ConsoleCommands` and the follow offsets. |
| 11 | Plan C's `-SWCam=demo` beats fire on wall-clock while `-SWShot` fires on sim time, so rail stills land mid-transition. | B0 adds `-SWShotReal=<s:s:s>` (wall-clock seconds since BeginPlay); B6 verifies the rail with it and with the `-dumpmovie` frames. |
| 12 | Lumen GI costs 3–5 ms but `UProceduralMeshComponent` has no distance fields: terrain, arches and every creature are invisible to Lumen's software tracing. | `r.DynamicGlobalIlluminationMethod=0` + `r.ReflectionMethod=2` from 09:00. SkyLight real-time capture keeps the teal ambient; emissives never needed GI. Re-enable only if 1x lands ≤ 12 ms after B5. |
| 13 | Plan B's 09:00 Fab window is a human + network single point of failure (login/2FA, editor open = no builds). | No Fab, no browser downloads. Human steps are a commit, two glances and a key check. |
| 14 | Plan C's `SetState()` writes four MID params per logical substep (~7k/frame at 50x). | All MID writes happen in `ApplyVisualFrame()` once per rendered frame and only when dirty. |
| 15 | Plan A B0(c)'s "50x budget" run never reaches 220 agents at seed 1 (DESIGN.md §6: 52 → ~90 by 120 s). | Stress run uses founder counts (`Settings.InitialLumen=180;Settings.InitialTecton=40`), 12 s, shot at 6. |
| 16 | Plan A's B4 reeds are engine cones (read as cones) and stack 13k instances on the 5000 groundcover cards it did not know about. | Reeds = the imported `grass_medium_01/02` and `fern_02` HISMs already placed by `BuildImported`, with taller Z scale in the wet band; no cones, no extra instances. |

Plan A soft spots also fixed here: the camera rail moves from "16:00 polish" into B6; B1/B2 are trimmed to what fits; clouds are decided by screenshot at 09:25 rather than assumed.

---

## 3. Hour-by-hour plan for 2026-09-06 (visual track, 09:00–14:00)

The research lane (Trace X/Y, balance, drought tuning) runs in parallel in
its own worktree. File ownership after B0: visual lane = `SWEnvironment.*`,
`SWProcMesh.*`, `SWResourcePatch.cpp` visuals, `SWCameraPawn.*`, one HUD
line, `Tools/make_materials.py`, `Config/DefaultEngine.ini`,
`Config/DefaultInput.ini`, and fields **appended at the end** of
`FSWLookSettings`. `SWWorldManager.*` and the sim half of `SWAgent.cpp` are
touched only in B0 (hooks agreed at 09:00) and never again.

Every block ends with the determinism oracle (seed 7, mode C, 300 s headless,
run twice, `population.csv` byte-identical) and `grep -n GetRng
Source/SymbioticWorld/SWEnvironment.cpp Source/SymbioticWorld/SWProcMesh.cpp
Source/SymbioticWorld/SWCameraPawn.cpp` returning nothing.

Standard screenshot run (call it **SHOT**):
`python Tools/run_sim.py --mode C --seed 1 --duration 45 --speed 1 --windowed --shot 4,40 --auto-select --no-logs -- -SWFollow=1`
Standard perf run (call it **PERF**, vsync off, stat unit in the PNG):
`python Tools/run_sim.py --mode C --seed 1 --duration 12 --speed 50 --windowed --shot 6 --no-logs --set "Settings.InitialLumen=180;Settings.InitialTecton=40;Look.ConsoleCommands=r.vsync=0|stat=unit"`
(`Look.ConsoleCommands` is added in B0: `|`-separated, `=` becomes a space, so no shell quoting problem.)

| Block | Deliverable | How (files / APIs / assets) | Exit condition (agent-verifiable) | Fallback |
|---|---|---|---|---|
| **B0 09:00–09:40** Hooks, renderer budget, baseline, dressing fix | Perf readout in HUD + log; `-SWDroughtAt`, `-SWFollow`, `-SWShotReal`, `Look.ConsoleCommands`; agent visuals applied once per rendered frame; `ReceiveSignal` returns acceptance and the manager calls `Environment->OnSignal`; camera FOV 55 + species follow offsets + slow orbit; pivot-aware scan placement; ini renderer cut; baseline numbers in `PROGRESS.md`. | Section 6 has the full code. Files: `Config/DefaultEngine.ini`, `SWTypes.h` (append), `SWWorldManager.h/.cpp` (parse + Tick + signal loop), `SWAgent.h/.cpp` (`ApplyVisualFrame`, `ReceiveSignal` bool, `bCreatureShadows`), `SWHUD.cpp` line 110, `SWCameraPawn.h/.cpp`, `SWEnvironment.h/.cpp` (`OnSignal` stub, `RunConsoleCommands`, `Place()` fix). Build with `Build.bat … -WaitMutex -NoHotReload` after `tasklist \| findstr UnrealEditor` is empty. Runs: SHOT; SHOT with `--set "Look.bClouds=false"`; PERF; determinism pair. | Build succeeds. t=40 PNG shows `frame X ms (Y fps)` in the top-left panel and the camera behind a Lumen at FOV 55. `grep "SW perf" Saved/Logs/SymbioticWorld.log` returns lines. PERF PNG shows stat unit with 220 agents; numbers (1x/~55 agents and 50x/220) written to `PROGRESS.md`. Clouds-off vs on tells whether the sun disc is visible (decides B1). No vertical slab in the t=4 frame (or `bUseImportedAssets=false` shipped). Two seed-7 runs byte-identical. | If the hooks are not agreed by 09:10: skip `OnSignal` (B5 arcs then fire from `ReceiveSignal` inside `SWAgent.cpp`, one line) and skip `-SWDroughtAt` (use `Look.bDroughtPreview` in B6). If the slab is not fixed in 20 min: `Look.bUseImportedAssets=false`. |
| **B1 09:40–10:30** Atmosphere pass | `BuildLighting/ApplyDrought` rewritten to the plate-B1 recipe with every value in `FSWLookSettings` (tunable via `-SWSet`, no rebuild): sun 4800 K pitch −11 yaw 195, light-shaft bloom, volumetric shadow, teal Rayleigh + Mie haze + aerial perspective x3, pooled second fog layer at water level, directional inscattering start distance 0, teal lower-hemisphere skylight, WhiteTemp 5800, grain, lens flare; drought halves lerped by the existing `DroughtFactor`. | Section 6 has the full `BuildLighting` and `ApplyDrought`. Engine assets: `/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst` (MID params `Cloud_GlobalCoverage`, `Cloud_GlobalDensity`), optional `/Engine/MapTemplates/lut/LUT_Morning` at `Look.LutIntensity` (default 0). Tune with `--set "Look.ExposureBias=-0.3;Look.SunPitch=-9;Look.FogDensity=0.022"` etc. | SHOT t=40 PNG (agent reads it): warm glow on the +X horizon, at least one visible light shaft or haze band across the floor, shadows read blue-teal against warm lit ground, fog pooled low over the water and thinning uphill. Drought run `--duration 40 --shot 15,35 --no-logs -- -SWDroughtAt=18`: t=35 warmer, dustier, lower saturation than t=15. HUD frame ≤ 16.7 ms at 1x. | If > 16.7 ms: `Look.bClouds=false` → `Look.ConsoleCommands=r.Shadow.Virtual.Enable=0` → `r.ScreenPercentage=70`; each is a re-run, not a rebuild. If the sun disc is hidden by clouds even at coverage 0.3: ship clouds off. |
| **B2 10:30–11:30** Creatures | `SWProc::BuildTecton` rebuilt as Voronoi rock plates with thin amber seams (F2−F1 edge mask, so seams are thin regardless of noise amplitude), dark basalt base, legs ≥ 1/3 body height; `BuildLumen` sleeker with a dotted dorsal stripe and longer thinner legs; species scales rebalanced via `-SWSet` (`Lumen.MeshScale=1.0;Tecton.MeshScale=1.15;Tecton.PickRadius=200`) and baked at the final merge. Stretch: material WPO leg swing driven by the per-frame `GaitPhase` MID parameter. | `SWProcMesh.cpp` only (plus `make_materials.py creature()` for the stretch). Voronoi core: scatter 44–56 unit vectors from `VisRng` (drop 70 % with Z < −0.35); per vertex `F1/F2` = two smallest `1 − dot(U, cell)`, `edge = F2 − F1`; colour `R = saturate(1 − edge/0.035) * (U.Z > −0.25 ? 1 : 0.3)`; displacement `PlateH[cell] (0.07–0.16) * smoothstep(0, 0.045, edge) * (U.Z < −0.3 ? 0.35 : 1) − 0.05 + 0.03*FBm3(U*8+Seed, 2)`; body radii (155,100,78) at Z 100; head wedge (62,46,30) at Z 75 with the same colouring; legs `AppendTaperedCylinder` R0 44 → R1 34, length 84 at X ±95 / Y ±70; `Look.TectonBody` → (0.05,0.045,0.045). Lumen: `Mark = (U.Z > 0.55 && |U.Y| < 0.22 && sin(P.X*0.35+Seed) > 0.3) ? 1 : 0`, flank rings 0.6, legs R0 5 → R1 3 length 48, tail 70, body (66,19,22). Stretch WPO: vertex colour B = swing weight (0 hip, 1 foot), A = leg pair (1/0/0.5); material `swing = sin(GaitPhase) * (A*2−1) * B * GaitAmp` on the local X axis via `MaterialExpressionTransform` (Python enum spelling `unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL` / `unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD` **unconfirmed** — print `dir(unreal.MaterialVectorCoordTransform)` in the run). Material run R1: `"C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<repo>/SymbioticWorld.uproject" -run=pythonscript -script="<repo>/Tools/make_materials.py --force" -stdout -unattended -nullrhi` (~2 min; if `--force` does not reach `sys.argv`, set `FORCE = True` for the run). | SHOT t=40 crop: Tecton = dark plates separated by thin amber lines, no yellow cap, legs at least 1/3 of body height; Lumen shows a dotted cyan spine; frame ms within 1 ms of B1; determinism pair identical. A Tecton follow shot: `--set "Settings.InitialLumen=0" -- -SWFollow=1` (auto-select picks the youngest Lumen, so with none the agent adds a `-SWSelect=T` variant only if cheap; otherwise use `--cam` from `run_sim.py` aimed at a Tecton: `--cam -1200,0,220,-8,0`). | Two iterations max on the procedural plates. If it still reads as a fungus: Blender FBX route (section 8), 45 min, taken from B5/B6. |
| **B3 11:30–12:05** Geology | `BuildFeatures` places exactly three distinct, non-parallel arches (spans differing ≥ 1.4x, pairwise ≥ 2500 uu, yaw difference ≥ 20°, one thickened leg each, flat crown cap) plus two mesas on the −Y rim; `TerrainHeight` gains terraces on the rims only (`Rise` ≈ 0 inside the arena so `GroundZ` is unchanged); `M_SW_Rock`/`M_SW_Terrain` get horizontal strata banding and slope darkening (written now, flushed in run R2 during B4). | `SWProcMesh.cpp` (`BuildArch`: `LegAsymmetry` 1.0–1.9 on one leg by angle sign, buttress `Thick = 1 + 0.9*max(0,−sinA)^1.5`, low-frequency lumps `0.5*MinorR*FBm3(P*0.0015+Seed)`, ledges `0.18*MinorR*(0.5+0.5*sin(P.Z*0.012+2*FBm3(P*0.003)))`, crown cap `AppendEllipsoid` (0.9 MajorR, 2.2 MinorR, 0.6 MinorR) with Z clipped; new `BuildMesa`), `SWEnvironment.cpp BuildFeatures` (hero arch MajorR 1400 at X = Arena+1800, Y = −0.25 ValleyHalfWidth, yaw 100; mid 900 at Arena+3600, +0.35, yaw 70; far 650 at Arena+5200, −0.05, yaw 125), `SWTypes.h` (`ArchCount` 4 → 3, `MesaCount` 2, `TerraceStep` 260, `TerraceAmount` 0.55), `make_materials.py terrain(g, rocky)` (`band = frac(WorldPosition.z*0.0025 + 0.4*n1)`, `strata = lerp(0.85, 1.15, smoothstep(band))`, `rock_t *= lerp(0.7, 1.0, normal.z)`, moss threshold 0.74 → 0.80, moss albedo 40 % from `/Engine/StarterContent/Textures/T_ground_Moss_D` at `WorldPosition.xy*0.0025`). | t=40 start-camera PNG: three arches with visibly different spans and leg thickness, none parallel, at least one flat-topped mesa on the rim, no row of equal openings; Lumen feet still on the ground in the crop; `grep "Environment built in"` shows the features PMC under ~60k tris; frame ms within 1 ms of B2. | `Look.ArchCount=2` (already the interim default from 09:05) and skip mesas; strata stay as a material-only change. |
| **B4 12:05–12:45** Wetland | Bank reeds = the imported grass/fern HISM instances given Z scale 1.8–2.6 where `Wet > 0.6`; water gets depth-fade shoreline + Fresnel; 6–8 engine fog sheets over the river; drought couples to reeds (Z scale x0.55 via component scale), fog sheets (opacity x(1−0.7F)) and terrain cracks. Material run R2 flushes B3's strata + this block's water/terrain/glow edits. | `SWEnvironment.cpp BuildImported` groundcover loop (`Scale.Z *= Wet > 0.6f ? Rng.FRandRange(1.8f, 2.6f) : 1.f`), new `BuildMist(L)`: `UStaticMeshComponent` x 6–8 with `/Engine/EngineVolumetrics/Fogsheet/Mesh/S_EV_FogSheet_01.S_EV_FogSheet_01` and a MID of `/Engine/EngineVolumetrics/Fogsheet/Mesh/MI_Fogsheet_HideClose.MI_Fogsheet_HideClose`, every ~1500 uu along `SWProc::RiverCenterY`, Z = WaterLevel+120, yaw facing −X, scale 12–20 (**its tint/opacity parameter names are unverified** — print `unreal.MaterialEditingLibrary.get_vector_parameter_names(unreal.load_asset('/Engine/EngineVolumetrics/Fogsheet/Mesh/MI_Fogsheet_HideClose'))` in run R2; until then the sheets ship untinted). `make_materials.py water(g)`: `opacity = lerp(0.55, 0.92, Fresnel(4)) * DepthFade(200)`, `shoreline = (1 − DepthFade(60)) * 0.35` added to base colour (`MaterialExpressionDepthFade`, `MaterialExpressionFresnel`, both headers present). `terrain(g)`: puddles where `wetness*(1−0.8*Dryness) > 0.7` and `noise(0.01) > 0.6` → roughness 0.08, 30 % darker; cracks at `Dryness > 0.5` via `step(0.62, noise(WorldPosition, 0.06, turbulence))`. Run R2 = same command as R1 (~2 min, no builds meanwhile; the next windowed run recompiles shaders, so judge the t=40 frame, not t=4). | t=40 PNG: taller reed silhouettes line both banks and the basin, not the uplands; a pale shoreline band where water meets ground; wet ground near water darker/glossier than moss; mist sheets visible over the river in the wide shot; HUD frame ≤ 16.7 ms at 1x. Drought run (`-- -SWDroughtAt=18 --shot 15,35`): t=35 reeds shorter, puddles gone, cracks visible, water edge retreated. | If fog sheets cost > 1 ms or read as white cards: keep 3 far ones or drop them (volumetric fog remains). If run R2 breaks a material, `make_materials.py` without `--force` recreates only the missing ones; `LoadMat` already falls back to `BasicShapeMaterial`, so the world always renders. |
| **B5 12:45–13:25** Information grammar + resource nodes | Three `UInstancedStaticMeshComponent`s on `ASWEnvironment`: **Arcs** (from `OnSignal`, quadratic Bezier of 14 dots, fade in logical time, never > 0.6 real s), **Trails** (one 3 uu dot per living Lumen every 0.5 logical s, ring of 4000, age fade via `PerInstanceCustomData`), **Motes** (500 teal dots drifting over the wetland on wall-clock, dust-coloured and lowered in drought). Resource A = stalk clusters with bright bulbs (green = recovery), Resource B = amber crystal clusters, both scaled by stock as today. | `SWEnvironment.h/.cpp` (`OnSignal` body, `Tick` additions, `BuildFX(L)`), `SWProcMesh.cpp` (`BuildStalkCluster`, `BuildCrystalCluster`), `SWResourcePatch.cpp Init` (choose by `ResourceType`), `make_materials.py glow()` (age fade: `alpha = lerp(1, saturate(1 − (SimTime − PerInstanceCustomData(0))/20), UseAge)`, flushed in R2 if written in time, else R3 at 13:40). Meshes: `/Engine/BasicShapes/Sphere.Sphere`; material MIDs of `/Game/Materials/M_SW_Glow` (`GlowColor = Look.LumenGlow`, `GlowStrength` 18 arcs / 3 motes). Arc slot life: `t = max((SimTime−BirthSim)/1.5, (RealNow−BirthReal)/0.6)`; `BatchUpdateInstancesTransforms(slot*14, Transforms, true, true)`; per-signaller 0.25 s wall-clock rate limit above 5x. All FX: `SetCastShadow(false)`, no collision, `SetCullDistances(0, 12000)`. Stalks: 7–11 `AppendTaperedCylinder` R0 6 → R1 2, length 60–110, tilted 10–25°, bulb ellipsoid R 8 (colour R=1) on each tip; crystals: 5–8 cones R0 22 → R1 0, length 80–140. | SHOT t=40 PNG: at least one cyan dotted arc between two Lumen (the HUD `signal N` count > 0 confirms signalling in the window), dotted trails behind the followed Lumen fading toward its past positions, motes over the water in the wide t=20 shot, green stalk clusters and amber crystal clusters with a depleted patch visibly shorter/dimmer; HUD frame within 0.5 ms of B4; a 50x run shows no lingering arcs; determinism pair identical. | If arcs are invisible under TSR: dot scale ≥ 3 uu, `GlowStrength` ≤ 18, or `Look.ConsoleCommands=r.AntiAliasingMethod=2`. If `OnSignal` was not hooked in B0: emit from `ASWAgent::ReceiveSignal` on acceptance (one line, the arc then starts at the receiver's nearest signaller found by `Manager->GetAgents()`). |
| **B6 13:25–14:00** Camera rail, drought completion, capture dry run | `ASWCameraPawn` plays the 8-key rail (section 9) with timed beats (Tab, F, 1/2/3, P, help off) on `-SWDemo=1` or key C, Z clamped above `SWProc::TerrainHeight`, `-SWDemoQuit=1` quits at 92 s; `ApplyDrought` completes across every layer (sun temp/pitch, Mie, ground albedo, WhiteTemp, fog albedo, reeds, motes, water, post); 3-s `-dumpmovie` dry run confirms frame naming. | `SWCameraPawn.h/.cpp` (`FSWCamKey{T, Loc, Rot, FOV, Mode, Offset}`, `FSWDemoBeat{T, Kind, Value}`, keys built at start from `SWProc::TerrainHeight(Manager->GetLook(), X, Y)` and `Manager->GetSettings().WorldHalfSize`), `Config/DefaultInput.ini` (`+ActionMappings=(ActionName="CamDemo",Key=C)`), `SWPlayerController.h` (`SetHelpVisible(bool)`), `SWEnvironment.cpp ApplyDrought`. Beats call existing public API only: `Manager->CycleSelection()`, `Manager->SetTimeScale()`, `Manager->ToggleDrought()`, `Cam->SetFollowTarget()`. Capture dry run: `"C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" "<repo>/SymbioticWorld.uproject" -game -windowed -ResX=1920 -ResY=1080 -benchmark -fps=30 -dumpmovie -unattended -nosound -SWMode=C -SWSeed=1 -SWNoLogs=1 -SWDemo=1 -SWDuration=8` then `ls Saved/Screenshots/WindowsEditor/MovieFrame*` (name pattern **unconfirmed beyond the `MovieFrame` type name**). | Rail run `python Tools/run_sim.py --mode C --seed 1 --duration 3000 --speed 1 --windowed --no-logs -- -SWDemo=1 -SWDemoQuit=1 -SWShotReal=4:12:25:42:56:70:80:88`: the eight `SW_*_r00NN.png` stills match the shot list (wide establishing, low follow with inspector, elevated orbit at 50x, low shot across water with the red drought banner after 50 s, Tecton follow, high overview, arch against the sun); process exits 0 by itself; camera never below terrain (assert in Tick). Drought preview `--set "Look.bDroughtPreview=true" --shot 20`: unmistakably the B4 look. Dry run writes frames. | If the rail is late: `-SWCam=x:y:z:pitch:yaw` (already exists) gives the eight stills from fixed poses and the human drives the live camera. If `-dumpmovie` writes nothing in the dry run: human OBS recording (human step 5). |

### If time remains (16:00–17:00 polish hour, in this order)

1. Backup video: full 92-s `-benchmark -fps=30 -dumpmovie` run (~2700 PNGs, several GB) → `ffmpeg -framerate 30 -i Saved/Screenshots/WindowsEditor/MovieFrame%05d.png -c:v libx264 -pix_fmt yuv420p -crf 18 Saved/Demo/backup_seed1_modeC.mp4` (ffmpeg 8.1 on PATH); delete the PNGs after encoding. Verify three extracted frames (5 s, 55 s, 88 s) against shots 1, 5, 8.
2. Tecton furrow decals: `UGameplayStatics::SpawnDecalAtLocation(World, FurrowMat, FVector(60,55,150), Loc, FRotator(-90,Yaw,0), 95.f)` + `SetFadeOut(50.f, 40.f, false)`, cap 200, spawn every ~260 uu of Tecton travel only when `TimeScale <= 10`, Python-authored `MD_DEFERRED_DECAL` material, single `ASWEnvironment::AddFurrow(Loc, Yaw)` entry point the research lane can drive from Trace Y later. Never labelled "modification" in the HUD.
3. Niagara spark on signal (15-min timebox, `Look.bNiagaraBursts=false` default): add `"Niagara"` to `SymbioticWorld.Build.cs`, `UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, LoadObject<UNiagaraSystem>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Systems/RadialBurst.RadialBurst")), From, FRotator::ZeroRotator, FVector(0.25f), true, true)` then `SetCustomTimeDilation(TimeScale)`. White only.
4. HUD restyle (Roboto `/Engine/EngineFonts/Roboto.Roboto`, stat-card row, corner brackets, sparklines from a ring buffer) — separate lane if a fourth agent exists.
5. Second moon (unlit sphere ~10° apparent near the sun azimuth) and 2–3 far-rim waterfall cards (panning `Good64x64TilingNoiseHighFreq` streaks + a mist sheet).
6. Blender creature swap for Lumen only if the human downloaded a rig (not planned).

---

## 4. Human steps

1. **08:55 (2 min, strongly recommended).** Add `Content/Assets/` to `.gitignore` (615 MB of scans; `Content/Materials` stays in), then `git add -A && git commit -m "hack-day baseline"` and `git worktree add <repo>/../SW_research -b research`. The research lane builds and runs headless in that worktree (its own `Binaries/`), so its `-nullrhi` sweeps never lock the visual lane's DLL. Without the scans its `BuildImported` returns false and logs "procedural only" — harmless headless. Agents may not commit unasked (CLAUDE.md). If declined: both lanes use a `Saved/BUILD_LOCK` file and serialize builds.
2. **09:00 (2 min).** Ratify the B0 shared-file hooks with the research lane: (a) `bool ASWAgent::ReceiveSignal(...)` returns acceptance and the manager's signal loop calls `Environment->OnSignal(From, To)` on true; (b) `ApplyVisualFrame()` per agent + `-SWDroughtAt/-SWFollow/-SWShotReal` parsing + the 5-s `SW perf` log in `ASWWorldManager::Tick/ApplyCommandLineOverrides`. After B0 the visual lane never edits `SWWorldManager.*` or the sim half of `SWAgent.cpp`.
3. **~09:30 (1 min, optional).** Look at the clouds-on / clouds-off pair from B0 and say which sky you want; otherwise the agent decides by the sun-disc visibility rule.
4. **~13:45 (5 min).** Open the main clone in the editor, press Play, and press LMB, Tab, F, 1/2/3, Space, P, M, R, H and the new **C** (camera rail). The only checklist item that cannot be verified headless. Close the editor afterwards (DLL lock).
5. **16:00 (only if the `-dumpmovie` dry run failed).** Record the backup with Xbox Game Bar (Win+Alt+R) or OBS while `python Tools/run_sim.py --mode C --seed 1 --duration 3000 --speed 1 --windowed -- -SWDemo=1` runs.

No downloads, no Fab/Epic login, no Blender session, no editor clicks are required by any block.

---

## 5. Renderer settings to change now

`Config/DefaultEngine.ini` — edit the existing `[/Script/Engine.RendererSettings]` lines and append a `[SystemSettings]` section:

```ini
[/Script/Engine.RendererSettings]
r.DefaultFeature.AutoExposure=False
r.DefaultFeature.AutoExposure.Method=1
r.DefaultFeature.MotionBlur=False
r.DynamicGlobalIlluminationMethod=0
r.ReflectionMethod=2
r.GenerateMeshDistanceFields=True
r.Shadow.Virtual.Enable=1
r.AntiAliasingMethod=4
r.VolumetricFog=1
r.Lumen.TranslucencyReflections.FrontLayer.Enable=0
r.Lumen.TranslucencyReflections.FrontLayer.EnableForProject=0

[SystemSettings]
r.VolumetricFog.GridPixelSize=16
r.Streaming.PoolSize=2000
```

Reasons, one per line:

- `r.DynamicGlobalIlluminationMethod=0` (was 1, Lumen). The terrain, arches, resource nodes and every creature body are `UProceduralMeshComponent`s, which have no mesh distance fields, so Lumen's software tracing only ever screen-traces them; the measured GPU frame is 16.4 ms and Lumen is the largest single line item (~3–5 ms). The SkyLight keeps `SetRealTimeCaptureEnabled(true)` so the teal sky ambient survives; emissive markings never needed GI. Re-enable only if 1x lands ≤ 12 ms after B5.
- `r.ReflectionMethod=2` (SSR, was 1 Lumen reflections). `M_SW_Water` already sets `screen_space_reflections=True`; Lumen reflections without Lumen GI would keep the Lumen scene alive for nothing. The two `r.Lumen.TranslucencyReflections` lines are moot with Lumen off (set to 0 for clarity; harmless either way).
- `r.Shadow.Virtual.Enable=1` (keep). Creature contact shadows at the hero distance are the plates' look; ~2 ms. First ladder step at runtime: `--set "Look.ConsoleCommands=r.Shadow.Virtual.Enable=0"` (CSM 2048 takes over).
- `r.AntiAliasingMethod=4` (keep TSR). Do **not** add `r.ScreenPercentage=85`: the render already runs at 83.4 % (1336x751) per the stat-unit still, so it is a no-op. `r.ScreenPercentage=70` is the third ladder step, again via `Look.ConsoleCommands`.
- `r.VolumetricFog.GridPixelSize=16` (default 8): halves the volumetric fog cost (~1.2 → ~0.6 ms) with no visible loss at 1600x900 for a soft pooled fog.
- `r.Streaming.PoolSize=2000`: 29 Poly Haven scans at 2k on an 8 GB laptop GPU; keeps texture streaming from thrashing when the rail sweeps the rim.
- Bloom is not a cvar here: `BloomThreshold` 1.2 → 0.8 and `BloomIntensity` 0.7 → 0.55 move in `FSWLookSettings` (B1) so emissives halo without washing out.
- Clouds: decided by the 09:25 screenshot pair. If kept: `SetViewSampleCountScale(0.5)`; measured cost tonight 0.65 ms.
- vsync: leave the engine default for the live demo; every measurement run passes `Look.ConsoleCommands=r.vsync=0|stat=unit` so the numbers are not capped at 16.65 ms.

Cut ladder (all rebuild-free, in order, until 1x ≤ 16.7 ms with ~55 agents and 50x/220 ≤ 22 ms): `Look.bClouds=false` → `r.Shadow.Virtual.Enable=0` → `r.ScreenPercentage=70` → `Look.GroundcoverCount=1400;Look.CliffCount=26;Look.ImportedBoulderCount=70` → `Look.bCreatureShadows=false` → demo the generations beat at 10x instead of 50x.

---

## 6. First block, ready to run (09:00–10:00 = B0 + the B1 lighting rewrite)

Everything below references only files in the tree tonight and assets confirmed on disk. Build command (repo root, after `tasklist | findstr UnrealEditor` prints nothing):

```
"C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" SymbioticWorldEditor Win64 Development -Project="<repo>/SymbioticWorld.uproject" -WaitMutex -NoHotReload
```

### 6.1 `Config/DefaultEngine.ini`

Apply section 5 verbatim.

### 6.2 `Source/SymbioticWorld/SWTypes.h` — append at the END of `FSWLookSettings` (before the closing `};`)

```cpp
	// ---- Visual track 2026-09-06. Appended at the end so merges with the research lane stay trivial. ----
	// Console commands executed once after the environment is built. '|' separates commands, '=' becomes a space,
	// so the value survives -SWSet without shell quoting: --set "Look.ConsoleCommands=r.vsync=0|stat=unit"
	UPROPERTY(EditAnywhere) FString ConsoleCommands;
	UPROPERTY(EditAnywhere) float CameraFov = 55.f;
	// Follow-cam offsets per species (FLinearColor doubles as an -SWSet-able vector: "r:g:b").
	UPROPERTY(EditAnywhere) FLinearColor LumenFollowOffset = FLinearColor(-420.f, 90.f, 210.f);
	UPROPERTY(EditAnywhere) FLinearColor TectonFollowOffset = FLinearColor(-700.f, 140.f, 380.f);
	UPROPERTY(EditAnywhere) float FollowOrbitDegPerSec = 1.5f;
	UPROPERTY(EditAnywhere) bool bCreatureShadows = true;
	UPROPERTY(EditAnywhere) bool bDroughtPreview = false;            // render the drought look without touching the sim
	// B1 atmosphere recipe (plate B1: low warm sun, teal shadows, pooled fog).
	UPROPERTY(EditAnywhere) float SunTemperature = 4800.f;
	UPROPERTY(EditAnywhere) float DroughtSunTemperature = 3400.f;
	UPROPERTY(EditAnywhere) float DroughtSunPitchDrop = 3.f;
	UPROPERTY(EditAnywhere) float SunBloomScale = 0.3f;
	UPROPERTY(EditAnywhere) float SunBloomThreshold = 0.5f;
	UPROPERTY(EditAnywhere) FLinearColor FogDirectionalColor = FLinearColor(1.5f, 1.05f, 0.60f);
	UPROPERTY(EditAnywhere) float FogDirectionalExponent = 12.f;
	UPROPERTY(EditAnywhere) float FogDirectionalStartDistance = 0.f; // engine default 10000 uu hides it entirely in a 280 m valley
	UPROPERTY(EditAnywhere) FLinearColor FogAmbientScale = FLinearColor(0.6f, 0.75f, 0.8f);
	UPROPERTY(EditAnywhere) FLinearColor VolumetricFogAlbedo = FLinearColor(0.80f, 0.92f, 0.96f);
	UPROPERTY(EditAnywhere) FLinearColor DroughtVolumetricFogAlbedo = FLinearColor(1.0f, 0.82f, 0.63f);
	UPROPERTY(EditAnywhere) FLinearColor RayleighColor = FLinearColor(0.22f, 0.55f, 1.0f);
	UPROPERTY(EditAnywhere) float RayleighScale = 0.045f;
	UPROPERTY(EditAnywhere) float MieScale = 0.012f;
	UPROPERTY(EditAnywhere) float DroughtMieScale = 0.03f;
	UPROPERTY(EditAnywhere) float AerialPerspectiveScale = 3.f;      // the valley is ~280 m across; without this aerial perspective is invisible
	UPROPERTY(EditAnywhere) float CloudCoverage = 0.45f;
	UPROPERTY(EditAnywhere) float CloudDensity = 0.5f;
	UPROPERTY(EditAnywhere) float CloudSampleScale = 0.5f;
	UPROPERTY(EditAnywhere) FLinearColor SkyLowerHemisphere = FLinearColor(0.04f, 0.10f, 0.11f);
	UPROPERTY(EditAnywhere) float WhiteTemp = 5800.f;
	UPROPERTY(EditAnywhere) float DroughtWhiteTemp = 4300.f;
	UPROPERTY(EditAnywhere) float FilmGrain = 0.06f;
	UPROPERTY(EditAnywhere) float LensFlare = 0.12f;
	UPROPERTY(EditAnywhere) float LutIntensity = 0.f;                // 0 = off; 0.4 blends /Engine/MapTemplates/lut/LUT_Morning
```

And retune these existing defaults in the same struct (values, not new fields):
`SunPitch -15 → -11`, `SunYaw 228 → 195`, `SunIntensity 7 → 5`, `SunColor → (1.0, 0.93, 0.85)` (the 4800 K temperature now carries the warmth), `DroughtSunColor → (1.0, 0.72, 0.50)`, `SunVolumetricScattering 2.5 → 3`, `SkyLightIntensity 1.8 → 1.2`, `FogDensity 0.013 → 0.028`, `FogHeightFalloff 0.12 → 0.6`, `FogColor → (0.10, 0.16, 0.18)` (the flat grey wash comes from 0.30/0.46/0.50), `FogSecondDensity 0.09 → 0.20`, `FogSecondHeightOffset -60 → -20`, `FogSecondFalloff 1.2 → 3.0`, `VolumetricFogExtinction 3.0 → 2.5`, `FogStartDistance 300 → 200`, `BloomIntensity 0.7 → 0.55`, `BloomThreshold 1.2 → 0.8`, `ExposureBias -0.35 → -0.6`, `Vignette 0.5 → 0.35`, `ArchCount 4 → 2` (interim until B3), `TectonBody → (0.05, 0.045, 0.045)`, `CliffCount 48 → 26`, `ImportedBoulderCount 110 → 70`, `GroundcoverCount 5000 → 1400` (the measured 17.1 ms frame was taken with these smaller counts; raise them again only with a number).

### 6.3 `Source/SymbioticWorld/SWWorldManager.h`

```cpp
public:
	float GetAverageFrameMs() const { return AvgFrameMs; }
	bool ShouldFollowOnSelect() const { return bFollowOnSelect; }
protected:
	float DroughtAt = -1.f;              // -SWDroughtAt=<sim s>: toggle drought once when SimTime passes it
	bool  bFollowOnSelect = false;       // -SWFollow=1: the camera follows whatever is selected
	TArray<float> RealScreenshotTimes;   // -SWShotReal=5:20:40 : wall-clock seconds since BeginPlay (camera-rail stills)
	int32 NextRealShotIdx = 0;
	float RealTime = 0.f;
	float PerfAccum = 0.f; int32 PerfFrames = 0; float AvgFrameMs = 0.f;
```

### 6.4 `Source/SymbioticWorld/SWWorldManager.cpp`

In `ApplyCommandLineOverrides()`, after the `SWShot=` block:

```cpp
	FParse::Value(FCommandLine::Get(), TEXT("SWDroughtAt="), DroughtAt);
	bool bFollow = false;
	if (FParse::Bool(FCommandLine::Get(), TEXT("SWFollow="), bFollow) && bFollow) bFollowOnSelect = true;
	FString RealShotSpec;
	if (FParse::Value(FCommandLine::Get(), TEXT("SWShotReal="), RealShotSpec))
	{
		TArray<FString> Parts;
		RealShotSpec.ParseIntoArray(Parts, TEXT(":"), true);
		for (const FString& P : Parts) RealScreenshotTimes.Add(FCString::Atof(*P));
		RealScreenshotTimes.Sort();
	}
```

In `Tick()`, replace the first lines up to the pause check with:

```cpp
	Super::Tick(DeltaSeconds);

	// Wall-clock bookkeeping runs even while paused: perf readout and camera-rail stills.
	RealTime += DeltaSeconds;
	PerfAccum += DeltaSeconds; PerfFrames++;
	if (PerfAccum >= 5.f)
	{
		AvgFrameMs = 1000.f * PerfAccum / PerfFrames;
		UE_LOG(LogSymbioticWorld, Log, TEXT("SW perf: %.1f ms/frame (%.0f fps) agents %d speed %.0fx sim %.0f"),
			AvgFrameMs, 1000.f / FMath::Max(AvgFrameMs, 0.01f), Agents.Num(), TimeScale, SimTime);
		PerfAccum = 0.f; PerfFrames = 0;
	}
	while (NextRealShotIdx < RealScreenshotTimes.Num() && RealTime >= RealScreenshotTimes[NextRealShotIdx])
	{
		const FString Name = FString::Printf(TEXT("SW_%s_r%04d.png"), *RunId, FMath::RoundToInt(RealScreenshotTimes[NextRealShotIdx]));
		FScreenshotRequest::RequestScreenshot(Name, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		NextRealShotIdx++;
	}

	if (bPaused || TimeScale <= 0.f) return;
```

After the substep `while` loop (right after `if (Steps >= Settings.MaxSubstepsPerFrame) Accumulator = 0.f;`):

```cpp
	if (DroughtAt > 0.f && !bDrought && SimTime >= DroughtAt) { ToggleDrought(); DroughtAt = -1.f; }
	// Visuals are written once per rendered frame, never per logical substep.
	for (ASWAgent* A : Agents) A->ApplyVisualFrame();
```

In the signal loop (`StepWorld`, step 3), replace `B->ReceiveSignal(Pc.NearestResourceLoc, SimTime);` with:

```cpp
				const bool bAccepted = B->ReceiveSignal(Pc.NearestResourceLoc, SimTime);
				if (bAccepted && Environment) Environment->OnSignal(A->GetActorLocation(), B->GetActorLocation());
```

(`SWEnvironment.h` is already included at the top of the file.)

### 6.5 `Source/SymbioticWorld/SWAgent.h`

```cpp
	// Signal reception (called by the manager when a neighbour signals). Returns true if accepted.
	bool ReceiveSignal(const FVector& Loc, float SimTime);
	// Writes the visual state (body bob/pitch, material parameters) once per rendered frame. Called by the manager.
	void ApplyVisualFrame();
protected:
	float VisBob = 0.f, VisPitch = 0.f, VisStrength = 0.f;
	bool bVisualDirty = true;
```

### 6.6 `Source/SymbioticWorld/SWAgent.cpp`

```cpp
bool ASWAgent::ReceiveSignal(const FVector& Loc, float SimTime)
{
	// Social responsiveness gates acceptance of a received signal. (RNG consumption is unchanged.)
	if (Manager->GetRng().FRand() < Genome.Social)
	{
		bHasSignal = true;
		SignalLoc = Loc;
		SignalTime = SimTime;
		return true;
	}
	return false;
}

void ASWAgent::UpdateGait(float Dt)
{
	// Logical-time gait phase (follows 1x/10x/50x and pause). Only floats here; ApplyVisualFrame writes transforms.
	if (bMovedThisStep)
	{
		GaitPhase += Dt * Params.GaitFrequency * 2.f * PI;
		if (GaitPhase > 2.f * PI) GaitPhase -= 2.f * PI;
	}
	else
	{
		GaitPhase = FMath::FInterpTo(GaitPhase, 0.f, Dt, 4.f);
	}
	VisBob = Params.GaitAmplitude * FMath::Abs(FMath::Sin(GaitPhase));
	VisPitch = 2.5f * FMath::Sin(GaitPhase);
}

void ASWAgent::UpdateVisual()
{
	if (!Manager) return;
	const FSWLookSettings& L = Manager->GetLook();
	const float E = FMath::Clamp(Energy / FMath::Max(Params.MaxEnergy, 1.f), 0.f, 1.f);
	float Strength = L.CreatureGlow * (0.3f + 0.7f * E) * (Species == ESWSpecies::Tecton ? 0.55f : 1.f);
	if (IsSignalling()) Strength *= L.SignalGlowBoost;
	if (bSelected) Strength *= 1.6f;
	if (!FMath::IsNearlyEqual(Strength, VisStrength, 0.01f)) { VisStrength = Strength; bVisualDirty = true; }
}

void ASWAgent::ApplyVisualFrame()
{
	if (!Body) return;
	Body->SetRelativeLocation(FVector(0.f, 0.f, VisBob));
	Body->SetRelativeRotation(FRotator(VisPitch, 0.f, 0.f));
	if (!MID || !Manager) return;
	MID->SetScalarParameterValue(TEXT("GaitPhase"), GaitPhase);   // consumed by the B2 material; harmless before that
	if (bVisualDirty)
	{
		MID->SetScalarParameterValue(TEXT("EmissiveStrength"), VisStrength);
		const FSWLookSettings& L = Manager->GetLook();
		const float E = FMath::Clamp(Energy / FMath::Max(Params.MaxEnergy, 1.f), 0.f, 1.f);
		const FLinearColor Glow = Species == ESWSpecies::Lumen ? L.LumenGlow : L.TectonGlow;
		MID->SetVectorParameterValue(TEXT("Color"), Glow * (0.25f + 0.75f * E));   // BasicShapeMaterial fallback
		bVisualDirty = false;
	}
}

void ASWAgent::SetSelected(bool bInSelected)
{
	bSelected = bInSelected;
	UpdateVisual();
	ApplyVisualFrame();   // selection can change while paused
}
```

Also in `Init()` add `ApplyVisualFrame();` right after the existing `UpdateVisual();`, and in `BuildBody()` add `Body->SetCastShadow(L.bCreatureShadows);` after `Body->SetRelativeScale3D(...)`. `Step()`, `Decide()`, `ApplyAction()`, `PlaceAt()` and the RNG order are untouched.

### 6.7 `Source/SymbioticWorld/SWHUD.cpp`

At file scope after the includes:

```cpp
// Defined in Engine/Private/UnrealEngine.cpp (ENGINE_API) but not declared in any public header.
extern ENGINE_API float GAverageFPS;
extern ENGINE_API float GAverageMS;
```

In `DrawGlobalStats`: `DrawPanel(X - 10.f, Y - 6.f, 330.f, 232.f, ColPanel);` → width `400.f`, and the line

```cpp
	Y = DrawLine(X, Y, FString::Printf(TEXT("sim time %.0f s   speed %.0fx   step %.1f ms   frame %.1f ms (%.0f fps)"),
		M.GetSimTime(), M.GetTimeScale(), M.GetLastStepMs(), GAverageMS, GAverageFPS), ColText);
```

### 6.8 `Source/SymbioticWorld/SWCameraPawn.h/.cpp`

Header: add `class ASWWorldManager;` and members `UPROPERTY() ASWWorldManager* Manager = nullptr; bool bFollowSelected = false; bool bLookApplied = false; float OrbitYaw = 0.f;`.

`.cpp`: add `#include "SWWorldManager.h"`. In `BeginPlay()` after the `-SWCam=` parse:

```cpp
	bool bF = false;
	if (FParse::Bool(FCommandLine::Get(), TEXT("SWFollow="), bF) && bF) bFollowSelected = true;
```

In `Tick()`, right after `Super::Tick(DeltaSeconds);`:

```cpp
	if (!Manager) Manager = ASWWorldManager::Get(GetWorld());
	if (Manager && !bLookApplied) { Camera->SetFieldOfView(Manager->GetLook().CameraFov); bLookApplied = true; }
	if (Manager && bFollowSelected && MoveInput.IsNearlyZero())
	{
		if (ASWAgent* Sel = Manager->GetSelectedAgent()) { if (Sel != FollowTarget) FollowTarget = Sel; }
	}
```

Replace the follow branch's `Desired` computation:

```cpp
		const FSWLookSettings& L = Manager ? Manager->GetLook() : FSWLookSettings();
		const FLinearColor O = FollowTarget->GetSpecies() == ESWSpecies::Lumen ? L.LumenFollowOffset : L.TectonFollowOffset;
		OrbitYaw += L.FollowOrbitDegPerSec * DeltaSeconds;   // slow orbit instead of chasing the heading (agents re-aim every second)
		const FVector Off = FVector(O.R, O.G, O.B).RotateAngleAxis(OrbitYaw, FVector::UpVector);
		const FVector Target = FollowTarget->GetActorLocation();
		const FVector Desired = Target + Off;
```

(`FSWLookSettings` needs `#include "SWTypes.h"`, already pulled in via `SWAgent.h`.) Follow offsets are `-SWSet`-tunable: `--set "Look.LumenFollowOffset=-300:60:150"`.

### 6.9 `Source/SymbioticWorld/SWEnvironment.h/.cpp` (B0 part)

Header, public: `void OnSignal(const FVector& From, const FVector& To);` and protected: `void RunConsoleCommands(const FSWLookSettings& L);`.

`.cpp`: add `#include "GameFramework/PlayerController.h"` and `#include "Engine/Engine.h"`.

```cpp
void ASWEnvironment::OnSignal(const FVector& /*From*/, const FVector& /*To*/)
{
	// B5 draws the cyan arc here. Called by the manager only for accepted Lumen->Lumen signals.
}

void ASWEnvironment::RunConsoleCommands(const FSWLookSettings& L)
{
	if (L.ConsoleCommands.IsEmpty()) return;
	TArray<FString> Cmds;
	L.ConsoleCommands.ParseIntoArray(Cmds, TEXT("|"), true);
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	for (FString C : Cmds)
	{
		C.ReplaceInline(TEXT("="), TEXT(" "));   // "r.ScreenPercentage=70" -> "r.ScreenPercentage 70"; "stat=unit" -> "stat unit"
		if (PC) PC->ConsoleCommand(C);
		else if (GEngine) GEngine->Exec(GetWorld(), *C);
		UE_LOG(LogSymbioticWorld, Log, TEXT("Console: %s"), *C);
	}
}
```

Call `RunConsoleCommands(L);` as the last line of `Build()`. In `Tick()`, the drought target becomes `const float Target = (Manager->IsDrought() || L.bDroughtPreview) ? 1.f : 0.f;`.

`Place()` in `BuildImported` becomes pivot-aware (fixes the vertical slab):

```cpp
	auto Place = [&](UHierarchicalInstancedStaticMeshComponent* C, float X, float Y, float Size, float Sink, float PitchJitter)
	{
		const FBox Box = C->GetStaticMesh()->GetBoundingBox();
		const FVector Ext = Box.GetSize();
		const float Footprint = FMath::Max3(Ext.X, Ext.Y, 1.f);           // scale by footprint, not by the tallest axis
		const float S = Size / Footprint;
		// Bottom of the bounds sits Sink of its own height below the terrain, whatever the FBX pivot was.
		const float Z = SWProc::TerrainHeight(L, X, Y) - Box.Min.Z * S - Sink * Ext.Z * S;
		const FRotator R(Rng.FRandRange(-PitchJitter, PitchJitter), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(-PitchJitter, PitchJitter));
		C->AddInstance(FTransform(R, FVector(X, Y, Z), FVector(S)), /*bWorldSpace*/ true);
	};
```

and the cliff call uses `PitchJitter` 4 (was 8), `Sink` 0.30. Add one log line per mesh in `FindMeshes` (`UE_LOG(... "%s bounds %s"...)` with `M->GetBoundingBox().GetSize().ToString()`) so a still-standing slab can be excluded by stem name from the log without guessing.

### 6.10 Build, baseline runs, determinism gate (09:20–09:40)

```
tasklist | findstr UnrealEditor
"C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" SymbioticWorldEditor Win64 Development -Project="<repo>/SymbioticWorld.uproject" -WaitMutex -NoHotReload
python Tools/run_sim.py --mode C --seed 1 --duration 45 --speed 1 --windowed --shot 4,40 --auto-select --no-logs -- -SWFollow=1
python Tools/run_sim.py --mode C --seed 1 --duration 45 --speed 1 --windowed --shot 4,40 --auto-select --no-logs --set "Look.bClouds=false"
python Tools/run_sim.py --mode C --seed 1 --duration 12 --speed 50 --windowed --shot 6 --no-logs --set "Settings.InitialLumen=180;Settings.InitialTecton=40;Look.ConsoleCommands=r.vsync=0|stat=unit"
python Tools/run_sim.py --mode C --seed 1 --duration 45 --speed 1 --windowed --shot 40 --no-logs --set "Look.ConsoleCommands=r.vsync=0|stat=unit"
```

Read the four newest PNGs in `Saved/Screenshots/WindowsEditor/`; record "1x/55 agents: Frame/GPU ms" and "50x/220 agents: Frame/Game/GPU ms" in `PROGRESS.md`. Determinism gate (run twice, compare the two newest seed-7 runs):

```
python Tools/run_sim.py --mode C --seed 7 --duration 300
python Tools/run_sim.py --mode C --seed 7 --duration 300
python -c "import glob,os,hashlib;d=sorted(glob.glob('Saved/SymbioticWorld/*seed7*'),key=os.path.getmtime)[-2:];print([hashlib.md5(open(os.path.join(p,'population.csv'),'rb').read()).hexdigest() for p in d])"
```

Two equal hashes, and `grep -n GetRng Source/SymbioticWorld/SWEnvironment.cpp Source/SymbioticWorld/SWProcMesh.cpp Source/SymbioticWorld/SWCameraPawn.cpp` empty.

### 6.11 B1 start (09:40) — `ASWEnvironment::BuildLighting` replacement

All setters verified in `Engine/Source/Runtime/Engine/Classes/Components/{LightComponent,LightComponentBase,DirectionalLightComponent,SkyAtmosphereComponent,ExponentialHeightFogComponent,SkyLightComponent,VolumetricCloudComponent}.h` and `Engine/Scene.h`. Add `UPROPERTY() UMaterialInstanceDynamic* CloudMID = nullptr;` to the header and `#include "Engine/Texture.h"`.

```cpp
void ASWEnvironment::BuildLighting(const FSWLookSettings& L)
{
	UWorld* World = GetWorld();
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.Owner = this;

	// Sun: low and warm, aimed so it sits over the +X arch end the start camera faces (light travels toward -X at yaw ~180).
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
			C->SetLightSourceAngle(1.2f);
			C->SetVolumetricScatteringIntensity(L.SunVolumetricScattering);
			C->SetCastVolumetricShadow(true);
			C->SetEnableLightShaftBloom(true);
			C->SetBloomScale(L.SunBloomScale);
			C->SetBloomThreshold(L.SunBloomThreshold);
			C->SetBloomTint(FColor(255, 190, 120));
			C->bCastCloudShadows = true;
			C->CloudShadowStrength = 0.6f;
			C->SetDynamicShadowDistanceMovableLight(15000.f);
			C->SetCastShadows(true);
			C->MarkRenderStateDirty();
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
		F->SetFogMaxOpacity(0.92f);
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
```

`ApplyDrought(L, F)` gains, next to the existing lerps:

```cpp
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
		Fog->GetComponent()->SetVolumetricFogAlbedo(FMath::Lerp(L.VolumetricFogAlbedo, L.DroughtVolumetricFogAlbedo, F).ToFColor(false));
		Fog->GetComponent()->SetDirectionalInscatteringColor(FMath::Lerp(L.FogDirectionalColor, FLinearColor(3.0f, 1.65f, 0.75f), F));
	}
	if (PostProcess)
	{
		PostProcess->Settings.WhiteTemp = FMath::Lerp(L.WhiteTemp, L.DroughtWhiteTemp, F);
	}
```

Verify at 10:00 with SHOT, then the drought run (`--duration 40 --shot 15,35 --no-logs -- -SWDroughtAt=18`), tuning by re-running with `--set` (no rebuild) until the t=40 frame shows the sun glow, a light shaft and pooled fog with the HUD at ≤ 16.7 ms.

---

## 7. Assets

### Assets confirmed on disk (no download)

- Project materials (Python-generated 10:17): `<repo>/Content/Materials/M_SW_Terrain, M_SW_Rock, M_SW_Water, M_SW_Creature, M_SW_Glow.uasset` (builder: `Tools/make_materials.py`).
- Poly Haven CC0 scans, imported (94 uassets, 615 MB): `<repo>/Content/Assets/PolyHaven/` — meshes `rock_face_01, rock_face_02, mountainside, coastal_cliff_01, coastal_cliff_02, namaqualand_cliff_01, namaqualand_cliff_02, namaqualand_boulder_02…05, boulder_01, rock_07, rock_09, rock_moss_set_01, rock_moss_set_02, moss_01, fern_02, grass_medium_01, grass_medium_02, shrub_01…03` with `_diff/_nor_gl/_rough_2k` textures and `_alpha_2k` for foliage; masked foliage materials `Content/Assets/PolyHaven/Masked/M_PH_fern_02, M_PH_grass_medium_01, M_PH_grass_medium_02, M_PH_shrub_01…03`. Raw sources: `<repo>/AssetSources/polyhaven/` (483 MB, gitignored).
- Blender-generated creature and rock FBX + manifest + spike renders: `<scratch>/out/{SM_Lumen, SM_Lumen_Body, SM_Lumen_Leg, SM_Tecton, SM_Tecton_Body, SM_Tecton_Leg, SM_Arch_A, SM_Arch_B, SM_Cliff_A, SM_Boulder_A}.fbx`, `manifest.json`, `lumen_spike.png`, `tecton_spike.png`, `arch_spike.png`; generator `scratchpad/make_creatures.py`; importer sketch `scratchpad/ue_import_creatures.py`; C++ sketch `scratchpad/sw_agent_mesh_swap.cpp`.
- Plates: `scratchpad/plates/p18_img0…3_1672x941.jpeg` (+ crops).
- Engine content (UE 5.7.3 at `C:/Program Files/Epic Games/UE_5.7`): `/Engine/BasicShapes/{Sphere,Cone,Cylinder,Plane,Cube}`, `/Engine/EngineVolumetrics/Fogsheet/Mesh/S_EV_FogSheet_01` + `MI_Fogsheet_HideClose`, `/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst`, `/Engine/MapTemplates/lut/{LUT_Morning,LUT_Afternoon,LUT_Daytime,LUT_Night}`, `/Engine/EngineMaterials/Good64x64TilingNoiseHighFreq`, `/Engine/StarterContent/Textures/T_ground_Moss_D` (the only StarterContent asset), `/Engine/EngineFonts/Roboto`, `/Niagara/DefaultAssets/Templates/Systems/{RadialBurst,FountainLightweight,DirectionalBurst,AttributeReaderTrails,…}`.
- Zero-download extras, unused by this plan: Content Examples 5.7 in `C:/ProgramData/Epic/EpicGamesLauncher/VaultCache/ContentExamples_5.7/data/Content/ExampleContent/{Landscapes,Niagara}` (Megascans rock shelf, mist/waterfall Niagara); Kenney Nature Kit zip in the scratchpad (CC0, stylised).
- Tools: ffmpeg 8.1 (`ffmpeg` (on PATH), on PATH); Blender 5.2 (`C:/Program Files/Blender Foundation/Blender 5.2/blender.exe`); anaconda Python.

### Assets that need download

**None are required by any block.** Optional, only if a human wants them and only outside the 09:00–14:00 window (each costs clicks and, for Fab in-editor packs, an open editor = no builds):

| Optional asset | Use | URL | Human clicks |
|---|---|---|---|
| Quaternius Ultimate Animated Animals (CC0; Fox/Wolf + 12 anims) | Lumen with a real walk cycle (skeletal swap, +30–45 min) | https://poly.pizza/bundle/Animated-Animal-Pack-ILAPXeUYiS ("Download FBX") | 1 |
| PBR Stegasaurus (Animated) by Ferocious Industries (free; fbx+glTF, 15 anims) | Literal rock-armoured quadruped for Tecton | https://www.fab.com/listings/b8168028-5a5a-4775-823f-f65db06a5f9f | 2 + Fab login |
| Monument Valley-Style Desert Rock Tower 01 (UE 5.7, 4k–8k textures, 1–3 GB) | Eroded towers to kitbash arch silhouettes | https://www.fab.com/listings/51234468-75a2-4e4f-8bbf-997ec08bdfa1 | editor open, 2 |
| mediterranean Vegetation: giant Reed (UE 5.0–5.4, 30 meshes) | Wetland reeds | https://www.fab.com/listings/b4c4cc88-d228-4961-9c18-9aa9c0d1292c | editor open, 2 |
| Niagara Examples Pack (Epic, UE 5.7) | Mist cards, pings | https://www.fab.com/listings/0e188eca-4e54-4fb2-a9ed-d8b8a565e600 | editor open, 2 |

Import route for any of them: `UnrealEditor.exe … -ExecutePythonScript="…/Tools/import_assets.py"` with `SW_IMPORT_SRC/SW_IMPORT_DEST` set (never `-run=pythonscript`). Credits: CC0 needs none (credit anyway); Fab Standard Licence packs by seller name; CC-BY items would need an on-screen line.

---

## 8. Creature pipeline

**Primary route (B2): procedural, in `SWProcMesh.cpp`, zero gates.** One `UProceduralMeshComponent` per agent, vertex colours R = emissive mask, G = phase, read by the existing `/Game/Materials/M_SW_Creature`. Tecton becomes Voronoi plates with `F2−F1` seams (thin borders whatever the noise amplitude), dark basalt, legs ≥ 1/3 body height; Lumen gets a dotted spine and thinner, longer legs. Scales: `Lumen.MeshScale 1.3 → 1.0`, `Tecton.MeshScale 0.75 → 1.15`, `Tecton.PickRadius 150 → 200` (all `-SWSet` first, baked in `SWWorldManager.cpp`'s constructor at the final merge — it is the only visual value living in the research lane's file).

**Fallback route: Blender bpy → FBX → static mesh.** Spike result tonight (headless, `blender.exe --background --factory-startup --python make_creatures.py -- --out <dir> --render`, 2.0 s for all ten FBX + three EEVEE previews):

- `scratchpad/tecton_spike.png`: 13 displaced rock plates over an amber-emissive core, low wedge head with amber visor, four rock legs — reads as a rock-armoured beast with glowing seams. 5808 tris, 4.0 m long (`SM_Tecton.fbx`, slots `SW_TectonRock`, `SW_TectonGlow`).
- `scratchpad/lumen_spike.png`: fox-like quadruped with ears, snout, dorsal stripe and tail, but legs and tail are metaball bead chains (fix: leg/tail metaball threshold 0.45 or element radii x1.35, ~10 min). 4712 tris, 1.2 m (`SM_Lumen.fbx`, slots `SW_LumenBody`, `SW_LumenGlow`).
- Units: binary FBX, `FBX_SCALE_ALL` (1 m → 100 uu), +X forward, Z up, body pivot at ground; `manifest.json` carries tri counts, dims and hip offsets (`lumen_hips_m`, `tecton_hips_m`, x100 for uu).

Steps if B2's procedural Tecton fails twice (45 min, taken from B5/B6):

1. `make_creatures.py`: after `bpy.ops.object.convert(target='MESH')`, bake vertex colours so `M_SW_Creature` works unchanged — `col = me.color_attributes.new(name='Col', type='FLOAT_COLOR', domain='CORNER')`; per polygon `r = 1.0 if poly.material_index == 1 else 0.0`, per loop `g = (x − xmin)/(xmax − xmin)`; export with `colors_type='LINEAR'`. Copy the script to `Tools/make_creatures.py`, output to `Content/RawAssets/`.
2. Import with the full editor (the commandlet crashes on `import_asset_tasks`): `"C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" "<repo>/SymbioticWorld.uproject" -ExecutePythonScript="<repo>/Tools/ue_import_creatures.py" -unattended -nosplash -log`. Under Interchange (the default when `task.factory` is None) every `FbxImportUI` option in that script is silently ignored; either set `task.factory = unreal.FbxFactory()` (legacy path, keeps `vertex_color_import_option=REPLACE`, `import_materials=False`) or rely on Interchange's default `IVCIO_Replace`. Delete the `build_nanite` CHECK line if it throws. Assert `SM_Tecton_Body` half-extent X in 190–210 uu and `SM_Lumen_Body` in 55–65 uu.
3. C++ swap (from `scratchpad/sw_agent_mesh_swap.cpp`, reduced to one component): in `ASWAgent::BuildBody`, `if (UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/SW/Meshes/SM_Tecton.SM_Tecton")))` create a `UStaticMeshComponent` `StaticBody` attached to `Mesh`, `SetStaticMesh(SM)`, `SetRelativeScale3D(FVector(Params.MeshScale * 0.9f))` (400 uu FBX vs 360 uu procedural), `SetCollisionEnabled(NoCollision)`, `SetCastShadow(L.bCreatureShadows)`, `SetMaterial(0, MID); SetMaterial(1, MID)`, and skip the PMC build; `ApplyVisualFrame` moves whichever body exists. Everything else (MID contract `BodyColor/EmissiveColor/EmissiveStrength/PulseSpeed/GaitPhase`, `PlaceAt`, `SnapToGround`, the RNG) is untouched, so the determinism oracle still passes.

A downloaded rigged quadruped (Quaternius) is not planned: it needs a human download, a skeletal import with ~30 % scale/root-bone risk, and yields "a fox with a cyan tint" rather than the spec's marked creature.

---

## 9. Demo shot list (90 s, camera rail keys K1–K9, beats in brackets)

Rail time is wall-clock; the sim runs underneath at the beat speed. HUD panels: G = global stats, P = population strip, I = inspector, K = keys/help, B = drought banner.

| Time | Shot | Camera (uu, deg) | HUD | VO / beat |
|---|---|---|---|---|
| 0–8 s | 1 Establishing | K1 (−9000, 1400, g+2300) pitch −14 FOV 55 → K2 (−7000, 900, g+1900) pitch −16: slow dolly 250 uu/s with 5° yaw drift toward the arches; fog pooled over the wetland, sun through the arch gap, herd dots and glowing nodes | G only (help hidden at 0 s) | "Most agents are deployed when training ends…" |
| 8–15 s | 2 Descend to hero | ease to K3: follow mode, offset (−260, 60, 120) rotated by the slow orbit, FOV 45, ~400 uu from a Lumen at a water-side node | G + I | [8 s Tab: youngest Lumen; 8.5 s F: follow] Inspector shows INHERITED and the initial Q table |
| 15–35 s | 3 Follow at 1x | K3 held; the selected Lumen forages/signals; cyan arcs to neighbours and its fading trail visible; Q values move while the inherited block stays fixed | G + I | [hold] "the same individual learns" |
| 35–50 s | 4 Generations | K4 (sel.X−1400, sel.Y, g+1400) pitch −35 FOV 55, 20° orbit around the wetland to K5 | G + P + I (on the newest child) | [35 s speed 10x; 40 s 50x; 45 s Tab: newest child — parent id and mutated genome next to the parent's] |
| 50–62 s | 5 Drought hit | K6 (−3000, −2500, g+400) yaw 35 pitch −10 FOV 45, static, low 3/4 across the water toward the sun | G + B | [50 s speed 1x; 50.5 s P] 6-s lerp: warm grade, dust motes, pools contract, reeds straw; red PERTURBATION / DROUGHT banner |
| 62–75 s | 6 Behaviour under drought | K7: follow the nearest Tecton, offset (−500, 100, 260), FOV 40; Lumen groups clustering at the remaining pools behind | G + P | [62 s 1x; 66 s 10x] furrows behind the Tecton if the polish item landed |
| 75–85 s | 7 Population shift | K8 (0, −6500, g+2200) pitch −50 FOV 60, very slow push-in at 50x | G + P | [75 s 50x] meta-parameter strip moving; seed and mode visible |
| 85–90 s | 8 Close | K9 (2500, 0, g+300) yaw 0 pitch −5 FOV 38 toward the largest arch against the low sun, motes drifting, world still running | G fades to the title line "TRAINING WAS ONLY GENERATION ZERO." with seed and mode in small type | [85 s 1x] |

Camera never dips below `SWProc::TerrainHeight + 60`; moves stay slow (dolly 150–300 uu/s, orbit 1–2 °/s) so 50x reads as time passing, not camera motion. Verification stills: `-SWShotReal=4:12:25:42:56:70:80:88`.

---

## 10. Open questions for the user (each with the recommended default)

1. **Baseline commit + research worktree at 08:55?** Default: yes (one commit, `Content/Assets/` gitignored first). It is the difference between two lanes working and two lanes waiting on one DLL.
2. **Ship Lumen GI off (`r.DynamicGlobalIlluminationMethod=0`, SSR for the water)?** Default: yes from 09:00; re-enable only if the 1x frame lands ≤ 12 ms after B5. The procedural world cannot benefit from Lumen anyway.
3. **Clouds on or off?** Default: decided by the 09:25 screenshot pair — keep only if the sun disc/glow is visible at `CloudCoverage 0.45` and the cost stays under 1 ms; otherwise off (warm atmosphere sky).
4. **Tecton route?** Default: procedural Voronoi plates (B2, no gates), Blender FBX swap only if two iterations still read as a fungus. Both keep the existing material and determinism.
5. **Demo capture?** Default: the live run at 1600x900 on the rail (key C), with the agent-produced `-benchmark -fps=30 -dumpmovie` backup video encoded in the 16:00 hour; you record with OBS only if the 13:50 dry run writes no frames.
