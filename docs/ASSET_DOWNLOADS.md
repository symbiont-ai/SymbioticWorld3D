# Asset downloads — what to click, where it lands, what happens next

Goal: bring the procedural valley up to the concept plates (`docs/plates/`)
with free, licence-clean assets. Total human time about 15 minutes. Everything
after your clicks is scripted.

Findings that shape this list (from the research pass on 2026-09-05):

- **Fab cannot be automated.** No REST API, no CLI, and the Fab plugin only
  works from the editor's embedded browser. Every Fab item costs a one-time
  Epic login plus 2 to 4 clicks. The editor must be open for the in-editor
  route, and while it is open I cannot build C++, so do all Fab clicks in
  **one block** and close the editor afterwards.
- **Poly Haven is fully scriptable** (CC0, public API). I fetch it myself.
- **Content Examples 5.7 is already on disk** in the launcher vault (Megascans
  rock shelf, rocks, trees, groundcover, Niagara mist/waterfall). Copying it
  is zero clicks; say the word and I do it.
- No free natural **rock arch** mesh exists anywhere. Arches stay procedural
  (mine) and get dressed with downloaded cliff/tower meshes.
- Bandwidth measured at ~7 MB/s: 500 MB ≈ 1.5 min, 2 GB ≈ 5 min. Disk free ≈ 54 GB.

## Where files go

One staging folder, gitignored:

```
<repo>\AssetSources\
    polyhaven\        <- I fill this (running now)
    creatures\        <- your zips from step 1
    quixel\           <- your Quixel zips from step 1
```

Fab **UE-format** packs added through the editor land directly in
`Content\<PackName>\` (that is how the plugin works); nothing to move.

## Step 1 — browser downloads (about 5 minutes, no login except Fab)

Save every zip into `AssetSources\creatures\` or `AssetSources\quixel\`.

| # | What | Why | Link | Click |
|---|---|---|---|---|
| 1 | **Quaternius Ultimate Animated Animals** (CC0, rigged fox/wolf/deer + 12 anims) | Lumen body with real walk/run animation | https://poly.pizza/bundle/Animated-Animal-Pack-ILAPXeUYiS | "Download FBX" |
| 2 | **Quaternius Ultimate Monsters** (CC0, incl. Goleling rock golem) | Tecton candidate | https://poly.pizza/bundle/Ultimate-Monsters-Bundle-5oyGWAmOB6 | "Download FBX" |
| 3 | **PBR Stegasaurus (Animated)** (free, 15 anims) | Best literal "large armoured quadruped" for Tecton | https://www.fab.com/listings/b8168028-5a5a-4775-823f-f65db06a5f9f | Add to My Library → Download (zip) |
| 4 | **Quixel: Nordic Forest Cliff Large** | Cliff faces on the valley walls | https://www.fab.com/listings/3a67a52f-0d3c-4f84-aa60-67b771062e0a | Add to My Library → Download → glTF, medium |
| 5 | **Quixel: Massive Tundra Rock Formation** | One hero rock mass near the arches | https://www.fab.com/listings/c2288fef-0c83-4a4f-8a63-a089c0443e00 | same, glTF medium |
| 6 | **Quixel: Wild Grass** and **Moss** surface | Wetland groundcover / terrain moss texture | https://www.fab.com/listings/7dc915c8-2fd9-4803-aedb-e37dc5a7335c and https://www.fab.com/listings/16744ea0-5ded-4f93-adcc-a74d17c9c1e9 | same, medium |
| 7 | optional **Swamp Cypress Roots** (free, 47 MB fbx) | Wetland dressing around the river | https://www.fab.com/listings/770f0ddb-2e52-4bd7-9fa8-deaf33466765 | Add → Download |

Items 1 and 2 are JavaScript buttons behind Cloudflare, which is why I cannot
fetch them. If poly.pizza misbehaves, the same packs are on
https://quaternius.com (Google Drive folder).

## Step 2 — Fab in-editor packs (about 8 minutes, editor open once)

1. Double-click `SymbioticWorld.uproject` (or open it from the Epic launcher).
2. **Window → Fab**. Log in with your Epic account (opens a browser once).
3. For each pack below: search it in the Fab window (or open the link in your
   browser and use "Add to My Library"), then in the Fab window's **My Library**
   tab press **"+"** ("Add to project"). It downloads straight into `Content\`.
4. When the last one finishes, **close the editor** and tell me.

| # | Pack | Why | Link | Est. size |
|---|---|---|---|---|
| A | **Monument Valley-Style Desert Rock Tower 01** (and 02) | Houdini-eroded rock towers: closest free stand-in for the plates' geology; I kitbash them into the arch silhouettes | https://www.fab.com/listings/51234468-75a2-4e4f-8bbf-997ec08bdfa1 and https://www.fab.com/listings/41a69de6-a543-4dce-afa1-a2824ac91d1e | 1–3 GB (8k textures) |
| B | **giant Reed** (Project Nature, 30 reed meshes) | Wetland reeds along the river; they shrink during drought | https://www.fab.com/listings/b4c4cc88-d228-4961-9c18-9aa9c0d1292c | ~0.5 GB |
| C | **Niagara Examples Pack** (Epic) | Mist cards for the wetland haze, ping/marker systems for Lumen signals, dust for Tecton digging | https://www.fab.com/listings/0e188eca-4e54-4fb2-a9ed-d8b8a565e600 | <1.5 GB |
| D | optional **[FREE] Mountain Tops** | Rocky uplands backdrop | https://www.fab.com/listings/8cdfadfb-5fa6-4b4e-99db-1a02b01295c2 | <1 GB |
| E | optional **Quadruped Fantasy Creatures** (PROTOFACTOR, UE-ready skeletons, 19–38 anims) | Higher-fidelity Lumen if Quaternius looks too toy-like | https://www.fab.com/listings/52d686b6-1180-4f26-901f-ce3c69a14767 | 0.3–0.8 GB |

Skip: Rural Australia (10 GB, wrong biome), Megaplants (heavy Nanite foliage;
maybe one species later), anything marked only for an engine newer than 5.7.
Packs marked 5.0–5.5 load fine in 5.7 and are resaved.

## Step 3 — what I do after your clicks (no human needed)

1. `Tools/import_assets.py` imports every FBX/glTF under `AssetSources\` into
   `/Game/Assets/<Set>` (Interchange, Nanite on for rocks, skeletal for
   creatures). It runs inside the editor process with `-ExecutePythonScript`
   because the headless commandlet crashes after the first asset (Content
   Browser sync needs Slate); the editor opens, imports, saves and exits on its
   own. Verified on the Poly Haven bundle 2026-09-05.
2. Cliff/tower/boulder meshes get scattered by the same seeded placement code
   that places the procedural rocks now, so the layout stays reproducible
   (`ASWEnvironment::BuildImported`, counts and sizes under `Look.*`).
   Foliage scans ship their cutout mask as a separate `_alpha` PNG that FBX
   never references, so `Tools/fix_foliage_materials.py` builds a masked
   two-sided material per foliage mesh and assigns it (editor process, like
   the importer).
3. Creatures: the imported skeletal mesh replaces the procedural body on
   `ASWAgent`; walk/idle animations play from C++ based on movement; cyan or
   amber emissive comes from a dynamic material instance, so the recolor is
   code, not art. The procedural body stays behind a `-SWSet Look.` flag as the
   fallback.
4. Reeds and grass are instanced along the wetland band from the terrain
   field; reed scale is tied to the drought factor.
5. Niagara mist cards go in the river channel; the ping system becomes the
   Lumen signal pulse.
6. Every step ends with a self-screenshot so you can see the result without
   opening the editor.

## Zero-click extras I can do on request

- Copy Content Examples `ExampleContent/Landscapes` (810 MB) and
  `ExampleContent/Niagara` (373 MB) from the launcher vault into the project.
  Adds 1.2 GB to the repo folder; I would gitignore it.
- Enable the engine **Water** plugin (river/lake bodies with proper
  reflections and caustics) instead of my flat water plane.

## Licences to credit in the demo

CC0: Poly Haven, Quaternius, Kenney (no credit required, we credit anyway).
Fab Standard License / UE EULA: Epic packs, Quixel, PROTOFACTOR, Artemus Blay,
Project Nature. CC-BY (credit required on screen or in README): Hyena_A1,
tharlevfx Water Materials, if used. Quixel assets may only be used inside
Unreal projects.
