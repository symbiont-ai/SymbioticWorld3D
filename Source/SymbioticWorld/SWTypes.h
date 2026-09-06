#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "SWTypes.generated.h"

// ---------------------------------------------------------------------------
// Symbiotic World — shared types
//
// Terminology is deliberately exact. See DESIGN.md for what each mechanism is
// and is not. In short:
//   * Lifetime learning  = tabular contextual bandit (context = energy bin),
//                          epsilon-greedy, constant step-size update, with an
//                          action feasibility mask. Not Q-learning (no bootstrap,
//                          gamma = 0), not deep RL.
//   * Evolution          = asexual reproduction with Gaussian mutation of the
//                          inherited parameters (alpha, epsilon, social, env-effect e).
//                          Learned action values are NOT inherited in P0.
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class ESWSpecies : uint8
{
	Lumen  UMETA(DisplayName = "Lumen"),
	Tecton UMETA(DisplayName = "Tecton")
};

UENUM(BlueprintType)
enum class ESWAction : uint8
{
	Forage  UMETA(DisplayName = "Forage"),
	Explore UMETA(DisplayName = "Explore"),
	Follow  UMETA(DisplayName = "Follow"),
	Avoid   UMETA(DisplayName = "Avoid"),
	Signal  UMETA(DisplayName = "Signal"),
	Rest    UMETA(DisplayName = "Rest"),
	Modify  UMETA(DisplayName = "Modify"),
	COUNT   UMETA(Hidden)
};

constexpr int32 SW_NUM_ACTIONS = static_cast<int32>(ESWAction::COUNT);

// Context for the bandit: coarse energy state. Three bins is the smallest
// context that lets an agent represent "forage when low, rest when high".
constexpr int32 SW_NUM_ENERGY_BINS = 3;

UENUM(BlueprintType)
enum class ESWLearningMode : uint8
{
	// Mode A: alpha forced to 0. Agents act on their random initial action values
	// for life. Evolution still runs but cannot affect behaviour through alpha.
	LearningOff       UMETA(DisplayName = "A - Learning OFF"),

	// Mode B: alpha > 0 from a fixed, population-wide genome. Children copy the
	// parent genome exactly (no mutation). Shows within-lifetime adaptation only.
	LearningOn        UMETA(DisplayName = "B - Learning ON, genome fixed"),

	// Mode C: alpha > 0, genome inherited with Gaussian mutation, selection via
	// energy-gated reproduction and starvation. The full story.
	LearningEvolution UMETA(DisplayName = "C - Learning + evolution"),

	// Mode N (control): identical to C EXCEPT reproduction is decoupled from
	// fitness — a uniformly random living agent reproduces whenever the
	// population is below target, and starvation death is disabled (age death
	// only). Genome dynamics under N are pure drift. If mode C genome trajectories
	// are not distinguishable from mode N across seeds, you have NOT shown
	// selection on learning parameters.
	NeutralControl    UMETA(DisplayName = "N - Neutral drift control")
};

inline const TCHAR* SWActionName(ESWAction A)
{
	switch (A)
	{
	case ESWAction::Forage:  return TEXT("forage");
	case ESWAction::Explore: return TEXT("explore");
	case ESWAction::Follow:  return TEXT("follow");
	case ESWAction::Avoid:   return TEXT("avoid");
	case ESWAction::Signal:  return TEXT("signal");
	case ESWAction::Rest:    return TEXT("rest");
	case ESWAction::Modify:  return TEXT("modify");
	default:                 return TEXT("?");
	}
}

inline const TCHAR* SWSpeciesName(ESWSpecies S)
{
	return S == ESWSpecies::Lumen ? TEXT("Lumen") : TEXT("Tecton");
}

inline const TCHAR* SWModeName(ESWLearningMode M)
{
	switch (M)
	{
	case ESWLearningMode::LearningOff:       return TEXT("A_learning_off");
	case ESWLearningMode::LearningOn:        return TEXT("B_learning_on");
	case ESWLearningMode::LearningEvolution: return TEXT("C_learning_evolution");
	case ESWLearningMode::NeutralControl:    return TEXT("N_neutral_control");
	default:                                 return TEXT("?");
	}
}

// ---------------------------------------------------------------------------
// Inherited learning parameters ("genome" G in the spec).
// Only parameters that actually enter an equation are included. The spec's
// "memory persistence" m is redundant with alpha for a constant-step-size
// update and was dropped; "environmental effect" e scales Trace X/Y deposits.
// ---------------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FSWGenome
{
	GENERATED_BODY()

	// Step size of the action-value update. 0 => no lifetime learning.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Alpha = 0.12f;
	// Probability of choosing a uniformly random feasible action.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Epsilon = 0.20f;
	// Scales how far this agent listens for signals / neighbours (Follow range),
	// and the probability of accepting a received signal.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Social = 0.50f;
	// Environmental-effect strength e (spec §6): scales the Trace X / Trace Y
	// deposit of a modify decision (deposit = base * e / 0.5) AND its energy
	// cost (ModifyBurn * e / 0.5), so a stronger effect is not free.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float EnvEffect = 0.50f;

	static constexpr float AlphaMin = 0.0f,  AlphaMax = 0.50f;
	static constexpr float EpsMin   = 0.01f, EpsMax   = 0.50f;
	static constexpr float SocMin   = 0.0f,  SocMax   = 1.0f;
	static constexpr float EnvMin   = 0.05f, EnvMax   = 1.0f;

	void Clamp()
	{
		Alpha     = FMath::Clamp(Alpha,     AlphaMin, AlphaMax);
		Epsilon   = FMath::Clamp(Epsilon,   EpsMin,   EpsMax);
		Social    = FMath::Clamp(Social,    SocMin,   SocMax);
		EnvEffect = FMath::Clamp(EnvEffect, EnvMin,   EnvMax);
	}
	// Multiplier applied to deposits and modify cost; 1.0 at the default e = 0.5.
	float EnvScale() const { return EnvEffect / 0.5f; }

	// G_child = clamp(G_parent + Normal(0, sigma)), independently per parameter.
	FSWGenome Mutated(FRandomStream& Rng, float Sigma) const;
};

// Per-species fixed parameters (NOT evolved).
USTRUCT(BlueprintType)
struct FSWSpeciesParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) float MoveSpeed = 350.f;          // uu per logical second
	UPROPERTY(EditAnywhere) float MaxEnergy = 100.f;
	UPROPERTY(EditAnywhere) float StartEnergy = 60.f;
	UPROPERTY(EditAnywhere) float BasalBurn = 0.9f;           // energy / logical s while resting
	UPROPERTY(EditAnywhere) float MoveBurn = 1.2f;            // extra energy / logical s while moving
	UPROPERTY(EditAnywhere) float SignalBurn = 1.0f;          // extra energy / logical s while signalling
	UPROPERTY(EditAnywhere) float ForageRate = 7.0f;          // max energy / logical s taken from a patch
	UPROPERTY(EditAnywhere) float ForageRadius = 220.f;       // uu
	UPROPERTY(EditAnywhere) float SenseRange = 1600.f;        // uu, for resources
	UPROPERTY(EditAnywhere) float NeighbourRange = 900.f;     // uu, base range for Follow/Avoid/Signal (scaled by Social)
	UPROPERTY(EditAnywhere) float CrowdRadius = 250.f;        // uu, Avoid feasible if a neighbour is closer than this
	UPROPERTY(EditAnywhere) float MaxAge = 150.f;             // logical s
	UPROPERTY(EditAnywhere) float MinReproAge = 20.f;         // logical s
	UPROPERTY(EditAnywhere) float ReproThreshold = 78.f;      // energy needed to reproduce
	UPROPERTY(EditAnywhere) float ReproCost = 40.f;           // energy transferred to child
	UPROPERTY(EditAnywhere) float MeshScale = 1.0f;           // multiplier on the procedural body
	UPROPERTY(EditAnywhere) float GroundOffset = 0.f;         // uu above GroundZ the body origin sits (feet on the ground = 0)
	UPROPERTY(EditAnywhere) float PickRadius = 70.f;          // uu, invisible sphere used for cursor picking
	UPROPERTY(EditAnywhere) float GaitFrequency = 3.0f;       // bobs per logical second while moving
	UPROPERTY(EditAnywhere) float GaitAmplitude = 4.f;        // uu
	UPROPERTY(EditAnywhere) FLinearColor Color = FLinearColor(0.1f, 0.9f, 1.0f);
	UPROPERTY(EditAnywhere) int32 PreferredResourceType = 0;  // 0 = Resource A, 1 = Resource B
};

// ---------------------------------------------------------------------------
// Look settings: everything about how the valley is rendered. Purely visual;
// nothing here feeds back into the simulation except GroundZ (which is a
// function of the terrain parameters) and the drought visual blend. All
// fields are reachable from the command line via -SWSet="Look.Field=value",
// so the look can be tuned by re-running, not recompiling.
// ---------------------------------------------------------------------------
USTRUCT(BlueprintType)
struct FSWLookSettings
{
	GENERATED_BODY()

	// ---- Terrain field (also used by SWProc::GroundZ) ----
	UPROPERTY(EditAnywhere) int32 LookSeed = 7;
	UPROPERTY(EditAnywhere) float TerrainHalfSize = 14000.f;
	UPROPERTY(EditAnywhere) int32 TerrainGrid = 200;
	UPROPERTY(EditAnywhere) float ValleyHalfWidth = 7000.f;
	UPROPERTY(EditAnywhere) float ValleyDepth = 3200.f;
	UPROPERTY(EditAnywhere) float RidgeNoiseAmp = 1400.f;
	UPROPERTY(EditAnywhere) float FloorNoiseAmp = 70.f;
	UPROPERTY(EditAnywhere) float RiverAmp = 1500.f;
	UPROPERTY(EditAnywhere) float RiverWavelength = 12000.f;
	UPROPERTY(EditAnywhere) float RiverWidth = 650.f;
	UPROPERTY(EditAnywhere) float RiverDepth = 150.f;
	UPROPERTY(EditAnywhere) float WaterLevel = -42.f;
	UPROPERTY(EditAnywhere) float WetlandBand = 110.f;
	UPROPERTY(EditAnywhere) int32 ArchCount = 3;                // massif arches: 1 = hero over the river, 2 = + twin arch on the +Y side, 3 = + far small arch
	UPROPERTY(EditAnywhere) int32 RockCount = 70;

	// ---- Sun, sky, fog ----
	// Rig: sun low down the +X valley axis (clears the terrain along +X, disc hides behind the hero arch),
	// narrow warm inscatter only around the sun, strong cool sky fill, plus a shadowless fill light from the camera side.
	UPROPERTY(EditAnywhere) float SunPitch = -5.f;          // plate look 2026-09-05: disc ~4 deg above the hero crown from the start camera (pitch -15, 78 deg FOV)
	UPROPERTY(EditAnywhere) float SunYaw = 176.f;           // light direction; 180 = sun straight down +X in front of the start camera
	UPROPERTY(EditAnywhere) float SunIntensity = 12.f;
	UPROPERTY(EditAnywhere) FLinearColor SunColor = FLinearColor(1.0f, 0.93f, 0.85f);   // warmth comes from SunTemperature
	UPROPERTY(EditAnywhere) float SunVolumetricScattering = 2.0f;
	UPROPERTY(EditAnywhere) float SkyLightIntensity = 2.5f;
	UPROPERTY(EditAnywhere) bool bClouds = true;                           // plate look 2026-09-05: thin clouds (CloudCoverage <= 0.25) texture the sky without hiding the sun
	UPROPERTY(EditAnywhere) float FogDensity = 0.002f;
	UPROPERTY(EditAnywhere) float FogHeightFalloff = 0.3f;
	UPROPERTY(EditAnywhere) FLinearColor FogColor = FLinearColor(0.30f, 0.38f, 0.48f);
	UPROPERTY(EditAnywhere) float FogSecondDensity = 0.03f;
	UPROPERTY(EditAnywhere) float FogSecondHeightOffset = -20.f;
	UPROPERTY(EditAnywhere) float FogSecondFalloff = 6.0f;
	UPROPERTY(EditAnywhere) float VolumetricFogExtinction = 1.6f;
	UPROPERTY(EditAnywhere) float FogStartDistance = 200.f;
	UPROPERTY(EditAnywhere) float FogMaxOpacity = 0.7f;      // < 1 lets the sky gradient and the sun disc show through the horizon haze (0.92 hid the disc)

	// ---- Post process ----
	UPROPERTY(EditAnywhere) float BloomIntensity = 0.4f;
	UPROPERTY(EditAnywhere) float BloomThreshold = 0.8f;
	UPROPERTY(EditAnywhere) float ExposureBias = 0.0f;
	UPROPERTY(EditAnywhere) FLinearColor ShadowTint = FLinearColor(0.80f, 0.92f, 1.15f);
	UPROPERTY(EditAnywhere) FLinearColor HighlightTint = FLinearColor(1.06f, 1.0f, 0.94f);
	UPROPERTY(EditAnywhere) float Saturation = 1.0f;
	UPROPERTY(EditAnywhere) float Contrast = 1.12f;
	UPROPERTY(EditAnywhere) float Vignette = 0.35f;

	// ---- Palette ----
	UPROPERTY(EditAnywhere) FLinearColor RockColor = FLinearColor(0.15f, 0.14f, 0.13f);
	UPROPERTY(EditAnywhere) FLinearColor MossColor = FLinearColor(0.07f, 0.19f, 0.10f);
	UPROPERTY(EditAnywhere) FLinearColor WaterColor = FLinearColor(0.010f, 0.045f, 0.06f);   // dark so the plane mirrors the warm sky (M_SW_Water)
	UPROPERTY(EditAnywhere) float WaterBrightness = 0.6f;                                   // base-colour multiplier on M_SW_Water ("Brightness")
	UPROPERTY(EditAnywhere) FLinearColor LumenBody = FLinearColor(0.05f, 0.07f, 0.10f);
	UPROPERTY(EditAnywhere) FLinearColor LumenGlow = FLinearColor(0.40f, 0.85f, 1.0f);
	UPROPERTY(EditAnywhere) FLinearColor TectonBody = FLinearColor(0.05f, 0.045f, 0.045f);
	UPROPERTY(EditAnywhere) FLinearColor TectonGlow = FLinearColor(1.0f, 0.42f, 0.07f);
	UPROPERTY(EditAnywhere) FLinearColor LeviathanBody = FLinearColor(0.028f, 0.036f, 0.048f);
	UPROPERTY(EditAnywhere) FLinearColor LeviathanGlow = FLinearColor(0.22f, 0.95f, 0.88f);
	UPROPERTY(EditAnywhere) float LeviathanScale = 0.55f;        // multiplier on the procedural body: ~900 uu long, 2.5x a Tecton
	UPROPERTY(EditAnywhere) float LeviathanGlowScale = 0.8f;     // x CreatureGlow while cruising (x2.2 while surfacing)
	UPROPERTY(EditAnywhere) FLinearColor ResourceAGlow = FLinearColor(0.25f, 1.0f, 0.35f);
	UPROPERTY(EditAnywhere) FLinearColor ResourceBGlow = FLinearColor(0.95f, 0.80f, 0.25f);
	UPROPERTY(EditAnywhere) float CreatureGlow = 5.0f;
	UPROPERTY(EditAnywhere) float SignalGlowBoost = 3.f;     // multiplier while an organism signals
	UPROPERTY(EditAnywhere) float ResourceGlow = 12.f;

	// ---- Imported assets (Tools/fetch_polyhaven.py + Tools/import_assets.py) ----
	// When the meshes exist under AssetRoot they dress the valley: cliffs on the
	// slopes, boulders around the arena, groundcover in the wetland band. When
	// they are missing the procedural rocks are used, so the world always renders.
	UPROPERTY(EditAnywhere) bool bUseImportedAssets = true;
	UPROPERTY(EditAnywhere) FString AssetRoot = TEXT("/Game/Assets/PolyHaven");
	// Optional role manifest (project-relative or absolute path to a JSON written by the
	// asset review: {"roles": {"cliff": [names], "arch_rock": [...], "boulder": [...],
	// "river_stone": [...], "groundcover": [...], "shrub": [...], "tree": [...],
	// "ground_material": "/Game/..", "water_material": "/Game/..", "niagara": {"mist": "/Game/.."}}}).
	// Empty or missing => the built-in Poly Haven roles under AssetRoot.
	UPROPERTY(EditAnywhere) FString AssetManifest = TEXT("AssetSources/ed_manifest.json");
	UPROPERTY(EditAnywhere) bool bUseImportedGroundMaterial = true;
	UPROPERTY(EditAnywhere) bool bUseImportedWaterMaterial = false;   // off since 2026-09-05: M_SW_Water (dark, Fresnel-weighted, reflective) instead of the Electric Dreams water
	UPROPERTY(EditAnywhere) int32 ArchRockCount = 5;            // dressing rocks per arch
	UPROPERTY(EditAnywhere) int32 RiverStoneCount = 320;
	UPROPERTY(EditAnywhere) int32 ShrubCount = 200;
	UPROPERTY(EditAnywhere) int32 TreeCount = 18;
	UPROPERTY(EditAnywhere) int32 MistCount = 8;                // mist cards along the river
	UPROPERTY(EditAnywhere) float TerrainUVTile = 450.f;        // uu per texture repeat on the terrain
	UPROPERTY(EditAnywhere) bool bImportedRocksUseProjectMaterial = false;   // true = tint cliffs/boulders with M_SW_Rock instead of their scan textures
	UPROPERTY(EditAnywhere) int32 CliffCount = 10;                 // random scatter of cliff pieces at native scale (uniform 0.6..1.1) on the slopes
	UPROPERTY(EditAnywhere) int32 ImportedBoulderCount = 110;
	UPROPERTY(EditAnywhere) int32 GroundcoverCount = 2500;
	UPROPERTY(EditAnywhere) float CliffSizeMin = 900.f;      // unused since 2026-09-05: the cliff scatter uses the mesh's native size (see CliffCount)
	UPROPERTY(EditAnywhere) float CliffSizeMax = 3000.f;     // unused (kept so --set overrides and saved settings still parse)
	UPROPERTY(EditAnywhere) float BoulderSizeMin = 120.f;
	UPROPERTY(EditAnywhere) float BoulderSizeMax = 520.f;
	UPROPERTY(EditAnywhere) float GroundcoverSizeMin = 140.f;
	UPROPERTY(EditAnywhere) float GroundcoverSizeMax = 320.f;

	// ---- Cliff walls, pinnacles, arch rock rings (plate 1: stratified sandstone cliffs on both rims) ----
	// Wall pieces are the 'cliff' role meshes scaled by their native size (not LongestExtent), marched along X
	// on each rim with overlap so no gaps show; the far-end wall leaves a gap around the river for the sun.
	UPROPERTY(EditAnywhere) bool bCliffWalls = false;              // off since 2026-09-05 ("too much wall"): the massif arches are the centrepiece
	UPROPERTY(EditAnywhere) int32 CliffWallRows = 2;
	UPROPERTY(EditAnywhere) float CliffWallYFrac = 0.72f;          // row 1 centreline at |Y| = frac * ValleyHalfWidth
	UPROPERTY(EditAnywhere) float CliffWallRowStep = 0.13f;        // row r at frac + r * step; row 2 scale x1.25
	UPROPERTY(EditAnywhere) float CliffWallScaleMin = 2.0f;        // uniform multipliers on the mesh's native size
	UPROPERTY(EditAnywhere) float CliffWallScaleMax = 2.7f;
	UPROPERTY(EditAnywhere) float CliffWallOverlap = 0.78f;        // advance along X by overlap * scaled piece length
	UPROPERTY(EditAnywhere) float CliffWallXMin = -0.55f;          // fractions of TerrainHalfSize
	UPROPERTY(EditAnywhere) float CliffWallXMax = 0.80f;
	UPROPERTY(EditAnywhere) float CliffWallFarGap = 2800.f;        // uu; far-end wall skips |Y - RiverCenterY| below this (sun gap)
	UPROPERTY(EditAnywhere) bool bCliffWallFarEnd = true;
	UPROPERTY(EditAnywhere) float CliffWallSink = 0.10f;           // fraction of scaled height sunk below terrain
	UPROPERTY(EditAnywhere) float CliffWallYawJitter = 12.f;
	UPROPERTY(EditAnywhere) float CliffWallPitchJitter = 4.f;
	UPROPERTY(EditAnywhere) int32 PinnacleCount = 4;               // last 2 flank the far sun gap, the rest sit on the mid slopes; uniform native scale
	UPROPERTY(EditAnywhere) float PinnacleScaleMin = 1.0f;
	UPROPERTY(EditAnywhere) float PinnacleScaleMax = 1.5f;
	UPROPERTY(EditAnywhere) int32 ArchRockRing = 0;                // rocks embedded along each arch (0 = off; the massif arches carry their own strata)
	UPROPERTY(EditAnywhere) float ArchRockRingScale = 2.3f;        // rock longest axis = scale * MinorR * 0.7..1.3

	// ---- Drought visuals (blended in over DroughtBlendSeconds of real time) ----
	UPROPERTY(EditAnywhere) FLinearColor DroughtSunColor = FLinearColor(1.0f, 0.72f, 0.50f);
	UPROPERTY(EditAnywhere) FLinearColor DroughtFogColor = FLinearColor(0.55f, 0.38f, 0.22f);
	UPROPERTY(EditAnywhere) float DroughtFogDensityScale = 0.6f;
	UPROPERTY(EditAnywhere) float DroughtWaterDrop = 55.f;
	UPROPERTY(EditAnywhere) float DroughtBlendSeconds = 6.f;

	// ---- B1 atmosphere recipe (plate B1: low warm sun, teal shadows, pooled fog) ----
	// Console commands executed once after the environment is built. '|' separates
	// commands and '=' becomes a space, so the value survives -SWSet without quoting:
	//   --set "Look.ConsoleCommands=r.vsync=0|stat=unit"
	UPROPERTY(EditAnywhere) FString ConsoleCommands;
	UPROPERTY(EditAnywhere) bool bCreatureShadows = true;
	UPROPERTY(EditAnywhere) bool bDroughtPreview = false;            // render the drought look without touching the sim
	UPROPERTY(EditAnywhere) float SunTemperature = 4300.f;          // golden hour (tournament 2026-09-05 19:15: the accidental drought frame was the closest to the plate)
	UPROPERTY(EditAnywhere) float DroughtSunTemperature = 3400.f;
	UPROPERTY(EditAnywhere) float DroughtSunPitchDrop = 3.f;
	UPROPERTY(EditAnywhere) float SunBloomScale = 0.45f;             // light-shaft bloom on the sun; >= 0.4 so the 3 deg disc blooms
	UPROPERTY(EditAnywhere) float SunBloomThreshold = 0.5f;
	UPROPERTY(EditAnywhere) FLinearColor FogDirectionalColor = FLinearColor(0.6f, 0.4f, 0.25f);   // warm inscatter band; 1.4/0.9/0.5 @ exp 12 drowned the sun disc
	UPROPERTY(EditAnywhere) float FogDirectionalExponent = 16.f;
	UPROPERTY(EditAnywhere) float FogDirectionalStartDistance = 0.f; // engine default 10000 uu hides it entirely in a 280 m valley
	UPROPERTY(EditAnywhere) FLinearColor FogAmbientScale = FLinearColor(0.6f, 0.75f, 0.8f);
	UPROPERTY(EditAnywhere) FLinearColor VolumetricFogAlbedo = FLinearColor(0.95f, 0.85f, 0.70f);
	UPROPERTY(EditAnywhere) FLinearColor DroughtVolumetricFogAlbedo = FLinearColor(1.0f, 0.82f, 0.63f);
	UPROPERTY(EditAnywhere) FLinearColor RayleighColor = FLinearColor(0.22f, 0.55f, 1.0f);
	UPROPERTY(EditAnywhere) float RayleighScale = 0.035f;          // lower = the low sun stays golden instead of deep red
	UPROPERTY(EditAnywhere) float MieScale = 0.008f;
	UPROPERTY(EditAnywhere) float DroughtMieScale = 0.03f;
	UPROPERTY(EditAnywhere) float AerialPerspectiveScale = 1.5f;      // the valley is ~280 m across; without this aerial perspective is invisible
	UPROPERTY(EditAnywhere) float CloudCoverage = 0.22f;
	UPROPERTY(EditAnywhere) float CloudDensity = 0.5f;
	UPROPERTY(EditAnywhere) float CloudSampleScale = 0.5f;
	UPROPERTY(EditAnywhere) float CloudShadowStrength = 0.25f;        // sun CloudShadowStrength; 0.6 darkened the whole valley
	UPROPERTY(EditAnywhere) FLinearColor SkyLowerHemisphere = FLinearColor(0.04f, 0.10f, 0.11f);
	UPROPERTY(EditAnywhere) float WhiteTemp = 5000.f;
	UPROPERTY(EditAnywhere) float DroughtWhiteTemp = 4300.f;
	UPROPERTY(EditAnywhere) float FilmGrain = 0.06f;
	UPROPERTY(EditAnywhere) float LensFlare = 0.12f;
	UPROPERTY(EditAnywhere) float LutIntensity = 0.f;                // 0 = off; 0.4 blends /Engine/MapTemplates/lut/LUT_Morning

	// ---- Plate elements the asset packs cannot supply (all procedural) ----
	UPROPERTY(EditAnywhere) bool bLumenTrails = true;                // glowing ribbons behind moving Lumen (= Trace X made visible)
	UPROPERTY(EditAnywhere) int32 TrailSamples = 22;                  // ribbon control points
	UPROPERTY(EditAnywhere) float TrailSampleInterval = 0.3f;        // logical seconds between samples (trail length is sim-distance, not real time)
	UPROPERTY(EditAnywhere) float TrailWidth = 24.f;                  // uu at the head
	UPROPERTY(EditAnywhere) float TrailGlow = 14.f;
	UPROPERTY(EditAnywhere) int32 WaterfallCount = 3;                 // sheets falling from the valley rim into the wetland
	UPROPERTY(EditAnywhere) float WaterfallWidth = 300.f;
	UPROPERTY(EditAnywhere) float WaterfallGlow = 1.5f;
	UPROPERTY(EditAnywhere) bool bMoon = true;
	// Elevation/azimuth are measured from the world origin; the moon sits MoonDistance away, so from the start camera
	// (86 m behind the origin, pitch -15, ~49 deg vertical FOV => frame top ~+9.5 deg, HUD stat boxes cover the top
	// ~4 deg) elevation 8 reads as ~3.5 deg: a 4 deg disc just above the hero crown (-1 deg) and 9 deg right of the
	// sun (+3 deg), under the HUD bar. 26/20 (and 22/8) were above the frame.
	UPROPERTY(EditAnywhere) float MoonElevation = 20.f;                // degrees above the horizon, from the origin
	UPROPERTY(EditAnywhere) float MoonAzimuth = 12.f;                  // degrees, 0 = straight down the valley (+X)
	UPROPERTY(EditAnywhere) float MoonDistance = 45000.f;             // uu (inside the fog range on purpose: reads as a hazy disc)
	UPROPERTY(EditAnywhere) float MoonRadius = 1200.f;
	UPROPERTY(EditAnywhere) float MoonGlow = 1.0f;

	// ---- Fill light + sun shaping (relight 2026-09-05) ----
	UPROPERTY(EditAnywhere) bool bFillLight = true;                 // shadowless cool fill from the camera side
	UPROPERTY(EditAnywhere) float FillIntensity = 0.9f;             // lux (1.5 flattened the cliffs)
	UPROPERTY(EditAnywhere) float FillTemperature = 7000.f;
	UPROPERTY(EditAnywhere) float FillPitch = -35.f;
	UPROPERTY(EditAnywhere) float FillYaw = 20.f;                   // world yaw; start camera looks down +X at yaw ~-8, so ~20 lights the near faces
	UPROPERTY(EditAnywhere) FLinearColor FillColor = FLinearColor(0.80f, 0.90f, 1.0f);
	UPROPERTY(EditAnywhere) float SunSourceAngle = 1.5f;            // degrees; sun disc size (plate: big soft golden disc; 0.6 was a pinprick lost in the band)
	UPROPERTY(EditAnywhere) float SunShadowDistance = 30000.f;      // uu; DynamicShadowDistanceMovableLight on the sun

	// ---- Arch falls (plate 1: sheets pouring off the massif crowns), trace overlay (relook 2026-09-05) ----
	UPROPERTY(EditAnywhere) bool bArchFalls = true;                 // one sheet per massif arch: crown point ~30% from one end, over the shoulder, down the outer face of that leg
	UPROPERTY(EditAnywhere) int32 ArchFallsPerArch = 1;
	UPROPERTY(EditAnywhere) float ArchFallWidth = 180.f;            // uu at the shoulder; flares x1.3 toward the foot
	UPROPERTY(EditAnywhere) float ArchFallGlow = 0.4f;
	UPROPERTY(EditAnywhere) float TraceOverlayIntensity = 0.55f;    // multiplies the ground overlay emissive and opacity (1.0 = the old saturated pools)
	UPROPERTY(EditAnywhere) FLinearColor TraceOverlayXColor = FLinearColor(0.55f, 0.9f, 1.0f);   // Trace X stain: cyan-white
	UPROPERTY(EditAnywhere) FLinearColor TraceOverlayYColor = FLinearColor(1.0f, 0.75f, 0.4f);   // Trace Y stain: soft amber
};

// Run-level settings. Editable on ASWWorldManager; also overridable from the
// command line: -SWSeed=123 -SWMode=C (A|B|C|N).
USTRUCT(BlueprintType)
struct FSWRunSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) int32 Seed = 42;
	UPROPERTY(EditAnywhere) ESWLearningMode Mode = ESWLearningMode::LearningEvolution;

	UPROPERTY(EditAnywhere) int32 InitialLumen = 40;
	UPROPERTY(EditAnywhere) int32 InitialTecton = 12;
	UPROPERTY(EditAnywhere) int32 MaxPopulation = 220;       // hard cap; reproduction blocked at cap

	UPROPERTY(EditAnywhere) float WorldHalfSize = 4500.f;    // uu; square arena centred at origin
	UPROPERTY(EditAnywhere) int32 ResourcePatchesA = 22;
	UPROPERTY(EditAnywhere) int32 ResourcePatchesB = 10;
	UPROPERTY(EditAnywhere) float PatchCapacity = 120.f;
	UPROPERTY(EditAnywhere) float PatchRegenPerSec = 6.0f;   // logistic regrowth rate at low stock (1.6 collapses Lumen; 6 stable on seed 1, see DESIGN.md §6)

	UPROPERTY(EditAnywhere) float DecisionInterval = 1.0f;   // logical s between action choices
	UPROPERTY(EditAnywhere) float LogicalSubstep = 0.1f;     // fixed integration step, logical s
	UPROPERTY(EditAnywhere) int32 MaxSubstepsPerFrame = 120;

	UPROPERTY(EditAnywhere) float MutationSigma = 0.03f;     // absolute, per parameter, per birth
	UPROPERTY(EditAnywhere) FSWGenome FounderGenome;         // mean genome of the founding population
	UPROPERTY(EditAnywhere) float FounderSpread = 0.05f;     // founders drawn as Normal(Founder, spread) in modes A/C/N
	UPROPERTY(EditAnywhere) float FounderAgeSpread = 0.6f;   // founder age ~ Uniform(0, spread * MaxAge); 0 = all born at t=0

	// Reward is r = wE * dEnergy/RewardScale + wN * novelty. Defaults make it
	// pure energy change; novelty is opt-in and documented as a bias if used.
	UPROPERTY(EditAnywhere) float RewardScale = 10.f;
	UPROPERTY(EditAnywhere) float WeightEnergy = 1.0f;
	UPROPERTY(EditAnywhere) float WeightNovelty = 0.0f;

	// Initial action values: Uniform(0, QInitMax) per (bin, action). Small random
	// values break ties differently per individual; in mode A they ARE the policy.
	UPROPERTY(EditAnywhere) float QInitMax = 0.05f;

	// Drought (P1 preview): multiplies patch regen and caps patch stock.
	UPROPERTY(EditAnywhere) float DroughtRegenMultiplier = 0.30f;
	UPROPERTY(EditAnywhere) float DroughtCapacityMultiplier = 0.45f;

	// Neutral control: births attempted at this interval while below target pop.
	UPROPERTY(EditAnywhere) float NeutralBirthInterval = 2.5f;

	// ---- Leviathan: river predation (a perturbation, not a species) ----------
	// The leviathan is part of the ENVIRONMENT, like the drought: no genome, no
	// learning, no ESWSpecies entry, no entry in the action set. It patrols the
	// river channel and kills organisms that are in the water. Deliberately kept
	// out of the species machinery so ESWSpecies stays binary and the external
	// policy protocol (docs/POLICY_API.md: 7 actions, 2 species) is unchanged.
	//
	// The danger is legible through the EXISTING percept: an organism is at risk
	// exactly when percept.on_land is false, and 'avoid' is already a feasible
	// action, so a policy server can learn to stay out of the river with no
	// protocol change at all.
	// OFF by default on purpose: enabling it removes organisms, so every recorded
	// baseline for a seed would stop reproducing. Turn it on per run with
	//   --set "Settings.bLeviathan=1"
	UPROPERTY(EditAnywhere) bool bLeviathan = false;
	UPROPERTY(EditAnywhere) int32 LeviathanCount = 1;
	UPROPERTY(EditAnywhere) float LeviathanSpeed = 620.f;          // uu per logical s along the channel
	UPROPERTY(EditAnywhere) float LeviathanStrikeRadius = 420.f;   // uu, horizontal
	UPROPERTY(EditAnywhere) float LeviathanStrikeCooldown = 6.f;   // logical s between kills (one animal cannot clear a shoal)
	// Added to Look.WaterLevel when testing "in the water". 0 makes the danger zone
	// EXACTLY the set where FSWPercept::bOnLand is false; raise it to make the
	// shallows dangerous too (at the cost of that exact correspondence).
	UPROPERTY(EditAnywhere) float LeviathanWaterMargin = 0.f;      // uu
	// The river is only ~160 uu deep (RiverDepth 150, surface at WaterLevel -42), so
	// the animal cannot submerge fully; it runs bed-hugging with its back showing and
	// is lifted clear of the terrain over shallow stretches. See ASWLeviathan.
	UPROPERTY(EditAnywhere) float LeviathanSubmersion = 120.f;     // uu the spine cruises below the surface
	UPROPERTY(EditAnywhere) float LeviathanBreachRise = 130.f;     // uu the spine rises at the top of a surfacing arc (back and head clear, belly stays wet)
	UPROPERTY(EditAnywhere) float LeviathanSurfaceInterval = 14.f; // logical s between surfacing arcs
	UPROPERTY(EditAnywhere) float LeviathanSurfaceDuration = 3.5f; // logical s per arc (a kill also triggers one)

	UPROPERTY(EditAnywhere) bool bWriteLogs = true;
	UPROPERTY(EditAnywhere) float AgentLogInterval = 1.0f;   // logical s between per-agent rows

	// ---- Trace fields (spec §7): shared, self-modifying environment ----
	UPROPERTY(EditAnywhere) bool bTraceFields = true;
	UPROPERTY(EditAnywhere) int32 TraceCells = 30;              // grid cells per side over the arena
	UPROPERTY(EditAnywhere) float TraceXHalfLife = 20.f;        // logical s (spec 15-30)
	UPROPERTY(EditAnywhere) float TraceYHalfLife = 120.f;       // logical s (spec 60-180)
	UPROPERTY(EditAnywhere) float TraceXDeposit = 0.4f;         // per Lumen modify decision (x e/0.5; stays below TraceMax up to e = 1 so the e cost/effect trade-off never saturates)
	UPROPERTY(EditAnywhere) float TraceYDeposit = 0.5f;         // per Tecton modify decision
	UPROPERTY(EditAnywhere) float TraceMax = 1.0f;
	UPROPERTY(EditAnywhere) float TraceYRegenGain = 1.5f;       // patch regen *= 1 + gain * TraceY(cell)
	UPROPERTY(EditAnywhere) float TraceXFollowMin = 0.08f;      // Lumen follow climbs the Trace X gradient above this
	UPROPERTY(EditAnywhere) float ModifyBurn = 1.5f;            // extra energy / logical s while modifying
	// Reward term wI (spec §5.4 "UsefulInteraction"): a small immediate reward when a deposit lands where it
	// is useful (Lumen: a stocked resource in range; Tecton: a patch below half stock in the cell). This is a
	// documented bias that lets a gamma = 0 learner credit an action whose benefit arrives later. 0 disables it.
	UPROPERTY(EditAnywhere) float WeightInteraction = 0.10f;

	// ---- External policy servers (docs/POLICY_API.md) ----
	// "host:port=Lumen|host:port=Tecton|host:port=Both". Organisms of a served species send their
	// decision (percept, feasibility mask, own Q table as a hint) to that server over newline JSON
	// and act on the reply; the built-in bandit still receives every reward. Empty = all built-in.
	// Also settable as -SWPolicy=... ; ',' and ';' are not allowed in the value (command-line parsing).
	UPROPERTY(EditAnywhere) FString PolicyServers;
	UPROPERTY(EditAnywhere) int32 PolicyTimeoutMs = 200;    // per-substep wait for a reply; on timeout the built-in bandit decides
	UPROPERTY(EditAnywhere) float PolicyShare = 1.0f;       // fraction of a served species assigned to the server, decided per organism at birth (seeded stream)
	// Server list file, watched on the wall clock while the sim runs (docs/POLICY_API.md, "Adding servers while
	// the sim runs"): one "host:port=Species" per line, '#' comments. Its entries are added to PolicyServers
	// (same host:port: the file line wins for the species). Relative paths are under the project directory.
	// A missing file means "no file servers". Empty = do not watch. Also settable as -SWPolicyFile=path.
	UPROPERTY(EditAnywhere) FString PolicyServerFile = TEXT("Saved/policy_servers.txt");
	UPROPERTY(EditAnywhere) float PolicyFilePollSec = 3.0f;  // wall-clock seconds between stats of the file (never inside a substep)
};

// Snapshot of what one agent can perceive when it decides. Filled by the
// world manager; consumed by the agent's feasibility gate and movement.
struct FSWPercept
{
	float Energy = 0.f;
	float MaxEnergy = 1.f;
	bool  bResourceKnown = false;
	class ASWResourcePatch* NearestPatch = nullptr;
	FVector NearestResourceLoc = FVector::ZeroVector;
	float NearestResourceDist = TNumericLimits<float>::Max();
	float NearestResourceStock = 0.f;
	int32 SameSpeciesInRange = 0;
	int32 OtherSpeciesInRange = 0;
	FVector NeighbourCentroid = FVector::ZeroVector;   // same-species centroid within NeighbourRange
	bool  bNeighbourKnown = false;
	float NearestAnyAgentDist = TNumericLimits<float>::Max();
	bool  bSignalKnown = false;
	FVector SignalLoc = FVector::ZeroVector;
	float TraceX = 0.f;                                 // local Trace X intensity
	float TraceY = 0.f;                                 // local Trace Y intensity
	bool  bTraceXGradient = false;
	FVector TraceXGradientDir = FVector::ZeroVector;
	bool  bOnLand = true;                               // above water level
	bool  bPatchInCellNeedsSoil = false;                // Tecton: a patch in this cell below half stock

	int32 EnergyBin() const
	{
		const float F = Energy / FMath::Max(MaxEnergy, 1.f);
		return F < 0.3333f ? 0 : (F < 0.6667f ? 1 : 2);
	}
};
