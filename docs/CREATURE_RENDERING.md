# Authored creature rendering

Lumen and Tecton are drawn with imported skeletal meshes (original Blender models made
for this project): three LODs each, an authored material, an idle and a walk clip, blended
transitions and terrain foot placement. The simulation is untouched: movement, sensing,
learning, reproduction and every seeded draw stay in `SWAgent` / `SWWorldManager`.
`USWCreatureMeshComponent` only adjusts the evaluated visual pose before it reaches the
renderer. Without the content, or with `Look.bAuthoredCreatures=false`, the organisms
render with the procedural `SWProc` bodies, and the run is byte-identical either way.

## Content

```
Content/Characters/Symbiotic/<Species>/
  SK_<Species>            skeletal mesh, 3 LODs (Lumen 28k/16k/7k verts, Tecton 56k/34k/15k)
  SK_<Species>_Skeleton
  A_<Species>_Idle        2.4 s loop
  A_<Species>_Walk        Lumen 1.0 s, Tecton 1.6 s loop
  M_<Species>_Authored    opaque, default lit (parameters below)
  Textures/<Species>_BaseColor / _Normal / _Roughness / _Emission
```

Authoring units: metres, ground-rooted at Z = 0, nose along +X (the FBX importer keeps
that axis, so the mesh needs no extra rotation). Runtime world scale = the species'
`MeshScale` x `Look.AuthoredLumenScale` (1.105) or `Look.AuthoredTectonScale` (3.733333):
Lumen 1.326 (about 5.0 m nose to tail tip, 3.4 m to the filament tips), Tecton 2.8
(about 11.6 m long, 7 m tall; the river predator is about 18 m at `Look.LeviathanScale` 1.1). A hidden box around the mesh bounds (85 % of them, riding on
the visual mesh) is the click target, so a click anywhere on the body selects the organism
without an inflated root sphere blocking clicks on neighbours.

## Install on another machine

The Unreal content is local: `Content/Characters/Symbiotic` (46 MB of .uasset) is untracked on
purpose, so the owner can decide between committing it (self-contained clones) and keeping it
per machine (`Content/Characters/Mannequins`, Epic's template content for the scientist
avatars, is git-ignored either way). Until it is committed, regenerate it from the
`SymbioticCreatures` source package:

1. Put the package somewhere local (default: `AssetSources/SymbioticCreatures/`, git-ignored),
   or point `SW_CREATURE_SOURCE` at its `exports` folder.
2. With no UnrealEditor process running, launch the full editor with the import script:

   ```bat
   set SW_CREATURE_SOURCE=C:\path\to\SymbioticCreatures\exports
   "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" "%CD%\SymbioticWorld.uproject" /Engine/Maps/Entry -ExecutePythonScript="%CD%\Tools\import_symbiotic_creatures.py" -unattended -RenderOffScreen
   ```

3. Check `Saved/CreatureIntegration/import_report.json` for `"ok": true` (it records the
   LOD vertex counts, clip lengths and bounds), then `Tools/build.bat` as usual.

The runtime logs one line per species at the first spawn of every run:
`Authored creature Lumen: mesh=SK_Lumen bones=38 LODs=3 (screen sizes 1.000/0.500/0.250) scale=1.326 pick box=318x81x220uu idle=2.40s walk=1.00s`,
or `Authored creature Lumen: no content under /Game/Characters/Symbiotic/Lumen/, procedural body (docs/CREATURE_RENDERING.md)`;
a headless process logs `Authored creatures: not loaded (no rendering in this process)` once.

## Rendering and animation

- Material parameters (`M_<Species>_Authored`): `BodyBrightness`, `BodyTint`,
  `MinimumRoughness`, `Specular`, `NormalStrength`, `NeutralNormal`, `GlowTint`,
  `EmissiveStrength`; Lumen also has `BodyRimStrength`. The runtime keeps driving `EmissiveStrength` from energy,
  signalling and selection through `Look.CreatureGlow` / `SignalGlowBoost`, as before.
- Clip choice: walk while the organism moved this substep, idle otherwise; the switch blends
  local bone transforms over 0.2 logical seconds. Play rate = distance moved per logical step
  over the authored stride (125 cm Lumen, 100 cm Tecton, times world scale), so feet do not
  slide at any time scale. The procedural gait bob is not applied to the authored body.
- Position: the visible mesh is interpolated between the last two substep positions on the
  rendered frame (`ASWWorldManager::Tick` passes the accumulator, also while paused, with the
  alpha frozen); the actor itself stays on the fixed logical clock and stands at
  `GroundZ + GroundOffset` like the procedural body. Poses are refreshed at up to 60 Hz within
  40 m of the camera, 30 Hz to 80 m, 15 Hz beyond, de-phased per organism, and not at all for a
  body no view has drawn for half a second; the transform update runs every frame so a culled
  body can come back. No pose is ever evaluated inside a substep (births included).
- Grounding: after animation, four two-bone chains (`fore_L/R`, `hind_L/R`: `_upper`,
  `_lower`, `_foot`) put each ankle at terrain height plus the animated lift, with reach
  clamped to the leg length instead of stretching. Steps beyond a leg's reach are a
  locomotion limitation; the solver never moves simulation actors.
- LOD: the component's tick is off, so its LOD status is refreshed with each pose sample
  (`UpdateLODStatus`) from the renderer's screen-size verdict; the imported thresholds are
  1.0 / 0.5 / 0.25, and from the demo camera at 50x most organisms sit at LOD 2 (audit line
  below). Measured 2026-09-11, seed 1, 1600x900 offscreen, 50x, `stat unit`: 179 organisms
  at t = 590 s render in 20.2 ms (GPU 18.7) against 16.2 ms with the procedural bodies and
  25.8 ms when every organism stayed at LOD 0; 136 organisms at t = 300 s: 17.6 ms (GPU 17.0).
  The pose update itself costs 1.3 / 2.9 ms of game thread at those counts.
- Headless runs (`-nullrhi`) skip the authored path entirely (`FApp::CanEverRender()` gate):
  no content is loaded and the procedural body, also unrendered, stands in. The CSV logs are
  identical either way.

## Inspection

- `-SWFollowSpecies=Leviathan` follows the river predator (with `Settings.bLeviathan=1`).
- `-SWFollowSpecies=Lumen` or `=Tecton` (after `--` with `Tools/run_sim.py`): chase camera
  on the selected organism of that species (it selects one if none is, so the inspector and
  the HUD marker describe what is on screen; with `-SWAutoSelect=1` it follows the youngest
  Lumen), re-acquired when it dies, framed for its size and kept above the terrain. Any camera
  key or the F key releases it. A creature walking between the camera and its target still
  occludes the view: the chase camera has no collision.
- `-SWCreatureAudit`: at each `--shot` time logs
  `Creature visual audit: agents=N update_ms=... within_1cm=N max_reach_error_cm=... lod0=N lod1=N lod2=N lod3+=N`
  (CPU pose-update work for all organisms that frame, trails excluded, the worst foot-target
  error, and the rendered-LOD histogram).
- `Look.bAuthoredCreatures=0` (launch, or control-file `set` + `reset`) shows the procedural
  bodies for a side-by-side.

## Source assets

The `SymbioticCreatures` package holds the editable `<Species>.blend` assemblies, the
`<Species>_game.blend` files with the refined weights and planted-foot actions, the FBX/GLB
exports and textures, `validation.json` per species, and `scripts/` to rebuild, refine and
validate the exports. These are generated meshes with automatic LOD reduction, not
hand-retopologized cinematic assets.
