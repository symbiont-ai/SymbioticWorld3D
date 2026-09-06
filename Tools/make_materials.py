"""Generate the project's materials as real .uasset files from code.

Run (editor loads, builds the material graphs, saves, exits):
  "C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" ^
     "<project>/SymbioticWorld.uproject" -run=pythonscript -script="<project>/Tools/make_materials.py"

Materials (all under /Game/Materials, all parametrised so C++ can tint them
through dynamic material instances):
  M_SW_Terrain   rock/moss blend by surface normal, wetness darkening from vertex colour B
  M_SW_Rock      same family, rockier, for arches and boulders
  M_SW_Water     translucent, reflective, animated normals from world-space noise
  M_SW_Creature  body colour + pulsing emissive markings masked by vertex colour R
  M_SW_Glow      resource nodes: emissive colour * strength, slight base colour
  M_SW_Scan      replacement master for the Electric Dreams scan meshes: texture params Albedo / Normal / DR
                 (C++ copies each instance's VT textures into a dynamic instance of this)
  M_SW_Sandstone massif arches: world-aligned triplanar rock, horizontal strata, moss on top (vertex colour R)
Re-running overwrites nothing: existing assets are left alone unless --force.
"""
import sys
import unreal

FORCE = "--force" in sys.argv
FOLDER = "/Game/Materials"
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
atools = unreal.AssetToolsHelpers.get_asset_tools()

MP = unreal.MaterialProperty


class G:
    """Tiny graph builder around MaterialEditingLibrary."""

    def __init__(self, mat):
        self.mat = mat
        self.x = -1600
        self.y = -600

    def _pos(self):
        self.y += 140
        if self.y > 900:
            self.y = -600
            self.x += 320
        return self.x, self.y

    def node(self, cls, **props):
        x, y = self._pos()
        n = mel.create_material_expression(self.mat, cls, x, y)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def vparam(self, name, r, g, b, a=1.0):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name,
                         default_value=unreal.LinearColor(r, g, b, a))

    def sparam(self, name, v):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=v)

    def const(self, v):
        return self.node(unreal.MaterialExpressionConstant, r=v)

    def const3(self, r, g, b):
        return self.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(r, g, b, 1))

    def link(self, a, aout, b, bin):
        ok = mel.connect_material_expressions(a, aout, b, bin)
        if not ok:
            unreal.log_warning(f"link failed: {a.get_name()}.{aout} -> {b.get_name()}.{bin}")
        return b

    def out(self, a, aout, prop):
        ok = mel.connect_material_property(a, aout, prop)
        if not ok:
            unreal.log_warning(f"output failed: {a.get_name()}.{aout} -> {prop}")

    # --- small combinators -------------------------------------------------
    def mul(self, a, aout, b, bout):
        n = self.node(unreal.MaterialExpressionMultiply)
        self.link(a, aout, n, "A"); self.link(b, bout, n, "B")
        return n

    def add(self, a, aout, b, bout):
        n = self.node(unreal.MaterialExpressionAdd)
        self.link(a, aout, n, "A"); self.link(b, bout, n, "B")
        return n

    def sub(self, a, aout, b, bout):
        n = self.node(unreal.MaterialExpressionSubtract)
        self.link(a, aout, n, "A"); self.link(b, bout, n, "B")
        return n

    def lerp(self, a, aout, b, bout, t, tout):
        n = self.node(unreal.MaterialExpressionLinearInterpolate)
        self.link(a, aout, n, "A"); self.link(b, bout, n, "B"); self.link(t, tout, n, "Alpha")
        return n

    def saturate(self, a, aout):
        n = self.node(unreal.MaterialExpressionSaturate)
        self.link(a, aout, n, "")
        return n

    def mask(self, a, aout, r=False, g=False, b=False, a_=False):
        n = self.node(unreal.MaterialExpressionComponentMask, r=r, g=g, b=b, a=a_)
        self.link(a, aout, n, "")
        return n

    def noise(self, pos, posout, scale, levels=3, lo=0.0, hi=1.0, turb=False, func=None):
        # Default stays SIMPLEX_TEX (creature cracks, water, glow, trails keep their look).
        # SIMPLEX_TEX shows a checker/grid artefact at large scales, so terrain
        # low-frequency masks pass func=NOISEFUNCTION_GRADIENT_TEX.
        if func is None:
            func = unreal.NoiseFunction.NOISEFUNCTION_SIMPLEX_TEX
        n = self.node(unreal.MaterialExpressionNoise, scale=scale, levels=levels,
                      output_min=lo, output_max=hi, turbulence=turb, quality=1,
                      noise_function=func)
        # Noise's position input is index 0; the helper matches an empty name to input 0.
        self.link(pos, posout, n, "")
        return n

    def append(self, a, aout, b, bout):
        n = self.node(unreal.MaterialExpressionAppendVector)
        self.link(a, aout, n, "A"); self.link(b, bout, n, "B")
        return n


def make(name, builder, **mat_props):
    path = f"{FOLDER}/{name}"
    if eal.does_asset_exist(path):
        if not FORCE:
            unreal.log(f"{path} exists, skipping (use --force)")
            return eal.load_asset(path)
        eal.delete_asset(path)
    mat = atools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    for k, v in mat_props.items():
        mat.set_editor_property(k, v)
    builder(G(mat))
    mel.recompile_material(mat)
    eal.save_asset(mat.get_path_name())
    unreal.log(f"built {path}")
    return mat


# ---------------------------------------------------------------------------
def terrain(g, rocky=False):
    rock = g.vparam("RockColor", 0.20, 0.18, 0.16)
    moss = g.vparam("MossColor", 0.10, 0.22, 0.09)
    wet = g.vparam("WetColor", 0.05, 0.07, 0.07)
    dry = g.sparam("Dryness", 0.0)          # 0 lush .. 1 drought (moss -> ochre)
    ochre = g.const3(0.30, 0.22, 0.10)
    mossdry = g.lerp(moss, "", ochre, "", dry, "")

    wp = g.node(unreal.MaterialExpressionWorldPosition)
    n1 = g.noise(wp, "", 0.004, levels=4, lo=0.55, hi=1.15,
                 func=unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_TEX)     # large-scale tone breakup
    n2 = g.noise(wp, "", 0.03, levels=3, lo=0.75, hi=1.05)      # fine grain
    tone = g.mul(n1, "", n2, "")
    rock_t = g.mul(rock, "", tone, "")

    nrm = g.node(unreal.MaterialExpressionVertexNormalWS)
    up = g.mask(nrm, "", b=True)                                  # normal.z
    thr = g.const(0.86 if rocky else 0.74)
    k = g.const(6.0 if rocky else 5.0)
    moss_t = g.saturate(g.mul(g.sub(up, "", thr, ""), "", k, ""), "")
    moss_n = g.mul(moss_t, "", g.noise(wp, "", 0.006, levels=3, lo=0.2, hi=1.2,
                                       func=unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_TEX), "")
    moss_a = g.saturate(moss_n, "")
    base = g.lerp(rock_t, "", mossdry, "", moss_a, "")

    vc = g.node(unreal.MaterialExpressionVertexColor)
    wetness = g.mask(vc, "", b=True)
    base2 = g.lerp(base, "", wet, "", wetness, "")
    g.out(base2, "", MP.MP_BASE_COLOR)

    r_dry = g.const(0.92 if rocky else 0.85)
    r_wet = g.const(0.35)
    rough = g.lerp(r_dry, "", r_wet, "", wetness, "")
    g.out(rough, "", MP.MP_ROUGHNESS)
    g.out(g.const(0.0), "", MP.MP_METALLIC)
    g.out(g.const(0.3), "", MP.MP_SPECULAR)


def water(g):
    # Dark, reflective plane that mirrors the warm sky: base = lerp(WaterColor, DeepColor) * Brightness (Look.WaterBrightness),
    # roughness 0.05, specular 1.0, opacity Fresnel-weighted OpacityMin..OpacityMax (grazing views go opaque so the
    # reflection wins over the see-through floor).
    col = g.vparam("WaterColor", 0.010, 0.045, 0.06)
    deep = g.vparam("DeepColor", 0.005, 0.02, 0.03)
    bright = g.sparam("Brightness", 0.6)
    g.out(g.mul(g.lerp(col, "", deep, "", g.const(0.4), ""), "", bright, ""), "", MP.MP_BASE_COLOR)
    fres = g.node(unreal.MaterialExpressionFresnel, exponent=5.0, base_reflect_fraction=0.04)
    op_min = g.sparam("OpacityMin", 0.75)
    op_max = g.sparam("OpacityMax", 0.95)
    g.out(g.lerp(op_min, "", op_max, "", fres, ""), "", MP.MP_OPACITY)
    g.out(g.const(0.12), "", MP.MP_ROUGHNESS)   # 0.05 turned the sun path into red speckles
    g.out(g.const(0.0), "", MP.MP_METALLIC)
    g.out(g.const(1.0), "", MP.MP_SPECULAR)

    wp = g.node(unreal.MaterialExpressionWorldPosition)
    t = g.node(unreal.MaterialExpressionTime)
    spd = g.sparam("RippleSpeed", 40.0)
    drift = g.mul(t, "", spd, "")
    off = g.append(g.append(drift, "", drift, ""), "", g.const(0.0), "")
    p1 = g.add(wp, "", off, "")
    p2 = g.sub(wp, "", off, "")
    amp = g.sparam("RippleAmount", 0.25)
    nx = g.mul(g.noise(p1, "", 0.02, levels=2, lo=-1, hi=1), "", amp, "")
    ny = g.mul(g.noise(p2, "", 0.025, levels=2, lo=-1, hi=1), "", amp, "")
    nrm = g.append(g.append(nx, "", ny, ""), "", g.const(1.0), "")
    g.out(nrm, "", MP.MP_NORMAL)

    glow = g.vparam("GlowColor", 0.0, 0.015, 0.02)   # was 0.08/0.10: a cyan self-glow that fought the sky reflection
    g.out(glow, "", MP.MP_EMISSIVE_COLOR)


def creature(g):
    body = g.vparam("BodyColor", 0.06, 0.08, 0.10)
    em = g.vparam("EmissiveColor", 0.1, 0.9, 1.0)
    strength = g.sparam("EmissiveStrength", 8.0)
    pulse_speed = g.sparam("PulseSpeed", 2.0)
    pulse_amt = g.sparam("PulseAmount", 0.35)
    # Per-pixel crack network in the body's LOCAL space (stable while the
    # organism moves). CrackMode 0 = markings come from vertex colour R only
    # (Lumen); 1 = vertex colour R gates a crack pattern (Tecton armour seams).
    crack_mode = g.sparam("CrackMode", 0.0)
    crack_scale = g.sparam("CrackScale", 0.045)
    crack_width = g.sparam("CrackWidth", 0.10)

    wp = g.node(unreal.MaterialExpressionWorldPosition)
    grain = g.noise(wp, "", 0.08, levels=2, lo=0.8, hi=1.1)
    g.out(g.mul(body, "", grain, ""), "", MP.MP_BASE_COLOR)
    g.out(g.sparam("Roughness", 0.45), "", MP.MP_ROUGHNESS)
    g.out(g.const(0.0), "", MP.MP_METALLIC)

    vc = g.node(unreal.MaterialExpressionVertexColor)
    region = g.mask(vc, "", r=True)               # marking / armour-region mask painted per-vertex by C++
    phase = g.mask(vc, "", g=True)                # 0..1 along the body for travelling pulse
    # cracks = 1 - smoothstep(0, width, |noise(localPos * scale)|)
    wp2 = g.node(unreal.MaterialExpressionWorldPosition)
    to_local = g.node(unreal.MaterialExpressionTransformPosition,
                      transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
                      transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    g.link(wp2, "", to_local, "")
    lp = g.mul(to_local, "", crack_scale, "")
    n = g.noise(lp, "", 1.0, levels=2, lo=-1.0, hi=1.0)
    n_abs = g.node(unreal.MaterialExpressionAbs)
    g.link(n, "", n_abs, "")
    ratio = g.node(unreal.MaterialExpressionDivide)
    g.link(n_abs, "", ratio, "A"); g.link(crack_width, "", ratio, "B")
    crack = g.saturate(g.sub(g.const(1.0), "", ratio, ""), "")
    gated = g.mul(region, "", crack, "")
    mask = g.lerp(region, "", gated, "", crack_mode, "")
    t = g.node(unreal.MaterialExpressionTime)
    tp = g.mul(t, "", pulse_speed, "")
    ph = g.add(tp, "", g.mul(phase, "", g.const(6.283), ""), "")
    s = g.node(unreal.MaterialExpressionSine, period=6.283)
    g.link(ph, "", s, "")
    s01 = g.add(g.mul(s, "", g.const(0.5), ""), "", g.const(0.5), "")
    pulse = g.add(g.const(1.0), "", g.mul(g.sub(s01, "", g.const(0.5), ""), "", pulse_amt, ""), "")
    e = g.mul(g.mul(g.mul(em, "", strength, ""), "", mask, ""), "", pulse, "")
    g.out(e, "", MP.MP_EMISSIVE_COLOR)


def glow(g):
    col = g.vparam("GlowColor", 0.2, 1.0, 0.3)
    strength = g.sparam("GlowStrength", 6.0)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    core = g.mask(vc, "", r=True)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    t = g.node(unreal.MaterialExpressionTime)
    flick = g.noise(g.add(wp, "", g.append(g.append(g.mul(t, "", g.const(30.0), ""), "", g.const(0.0), ""), "", g.const(0.0), ""), ""),
                    "", 0.02, levels=2, lo=0.7, hi=1.0)
    g.out(g.mul(col, "", g.const(0.15), ""), "", MP.MP_BASE_COLOR)
    g.out(g.mul(g.mul(g.mul(col, "", strength, ""), "", g.add(core, "", g.const(0.15), ""), ""), "", flick, ""), "", MP.MP_EMISSIVE_COLOR)
    g.out(g.const(0.6), "", MP.MP_ROUGHNESS)


def trail(g):
    # Unlit, additive-looking translucent ribbon: colour * glow * fade (vertex G) * soft edge (UV.x).
    col = g.vparam("TrailColor", 0.1, 0.8, 1.0)
    glow = g.sparam("TrailGlow", 6.0)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    fade = g.mask(vc, "", g=True)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    u = g.mask(uv, "", r=True)
    # edge = sin(pi * u)
    edge = g.node(unreal.MaterialExpressionSine, period=2.0)
    g.link(u, "", edge, "")
    edge_pos = g.node(unreal.MaterialExpressionAbs); g.link(edge, "", edge_pos, "")
    t = g.node(unreal.MaterialExpressionTime)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    drift = g.append(g.append(g.mul(t, "", g.const(120.0), ""), "", g.const(0.0), ""), "", g.const(0.0), "")
    sparkle = g.noise(g.add(wp, "", drift, ""), "", 0.04, levels=2, lo=0.5, hi=1.0)
    strength = g.mul(g.mul(g.mul(fade, "", fade, ""), "", edge_pos, ""), "", sparkle, "")
    g.out(g.mul(g.mul(col, "", glow, ""), "", strength, ""), "", MP.MP_EMISSIVE_COLOR)
    g.out(g.mul(strength, "", g.const(0.9), ""), "", MP.MP_OPACITY)


def waterfall(g):
    # Unlit translucent sheet: scrolling streaks (noise moving down in world Z), bright core, soft edges,
    # fading in at the lip (vertex G near 0) and dissolving into spray at the foot.
    glow = g.sparam("Glow", 1.6)
    seed = g.sparam("Seed", 0.0)
    col = g.vparam("WaterColor", 0.80, 0.90, 1.0)
    t = g.node(unreal.MaterialExpressionTime)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    fall = g.append(g.append(seed, "", g.const(0.0), ""), "", g.mul(t, "", g.const(900.0), ""), "")   # streaks fall at 9 m/s
    p = g.add(wp, "", fall, "")
    streaks = g.noise(p, "", 0.012, levels=2, lo=0.0, hi=1.0)
    fine = g.noise(g.mul(p, "", g.const(3.0), ""), "", 0.02, levels=1, lo=0.6, hi=1.0)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    u = g.mask(uv, "", r=True)
    edge = g.node(unreal.MaterialExpressionSine, period=2.0); g.link(u, "", edge, "")
    edge_pos = g.node(unreal.MaterialExpressionAbs); g.link(edge, "", edge_pos, "")
    vc = g.node(unreal.MaterialExpressionVertexColor)
    f = g.mask(vc, "", g=True)                       # 0 lip .. 1 foot
    lip = g.saturate(g.mul(f, "", g.const(6.0), ""), "")
    body = g.mul(g.mul(g.mul(streaks, "", fine, ""), "", edge_pos, ""), "", lip, "")
    g.out(g.mul(g.mul(col, "", glow, ""), "", g.add(body, "", g.mul(edge_pos, "", g.const(0.25), ""), ""), ""), "", MP.MP_EMISSIVE_COLOR)
    g.out(g.saturate(g.mul(body, "", g.const(1.4), ""), ""), "", MP.MP_OPACITY)


def moon(g):
    glow = g.sparam("Glow", 4.0)
    col = g.vparam("MoonColor", 0.85, 0.88, 0.95)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    maria = g.noise(wp, "", 0.0025, levels=3, lo=0.55, hi=1.0)
    g.out(g.mul(g.mul(col, "", glow, ""), "", maria, ""), "", MP.MP_EMISSIVE_COLOR)


def trace_overlay(g):
    # Unlit translucent ground stain: R = Trace X (cyan-white), G = Trace Y (soft amber); opacity follows intensity.
    # Intensity (Look.TraceOverlayIntensity, default 0.35) multiplies BOTH emissive and opacity: at 1.0 it is the old
    # saturated pools, at 0.35 a faint luminous stain.
    cx = g.vparam("TraceXColor", 0.55, 0.9, 1.0)
    cy = g.vparam("TraceYColor", 1.0, 0.75, 0.4)
    gain = g.sparam("Gain", 2.5)
    inten = g.sparam("Intensity", 0.35)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    r = g.mask(vc, "", r=True)
    gg = g.mask(vc, "", g=True)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    grain = g.noise(wp, "", 0.02, levels=2, lo=0.6, hi=1.0)
    e = g.mul(g.add(g.mul(cx, "", r, ""), "", g.mul(cy, "", gg, ""), ""), "", g.mul(gain, "", grain, ""), "")
    g.out(g.mul(e, "", inten, ""), "", MP.MP_EMISSIVE_COLOR)
    g.out(g.saturate(g.mul(g.mul(g.add(r, "", gg, ""), "", g.const(0.85), ""), "", inten, ""), ""), "", MP.MP_OPACITY)


def terrain_ed(g):
    """Procedural-terrain material from the migrated Megascans texture sets.
    World-aligned UVs (no mesh UV dependence): moss grass on flats, rocky
    ground on slopes, swamp mud in the wet band (vertex B), dried grass as
    the drought state (Dryness). Normals from the matching _N maps."""
    def tex(path):
        return eal.load_asset(path) if eal.does_asset_exist(path) else None
    sets = {
        "moss": ("/Game/Megascans/Surfaces/MossyGrass/T_MossyGrass_01_BC", "/Game/Megascans/Surfaces/MossyGrass/T_MossyGrass_01_N"),
        "rock": ("/Game/Megascans/Surfaces/MossyRockyGround/T_MossyRockyGround_01_BC", "/Game/Megascans/Surfaces/MossyRockyGround/T_MossyRockyGround_01_N"),
        "mud": ("/Game/Megascans/Surfaces/SwampWater/T_SwampWater_01_BC", "/Game/Megascans/Surfaces/SwampWater/T_SwampWater_01_N"),
        "dry": ("/Game/Megascans/Surfaces/DriedGrassOnDampSoil/T_DriedGrassOnDampSoil_01_BC", "/Game/Megascans/Surfaces/DriedGrassOnDampSoil/T_DriedGrassOnDampSoil_01_N"),
    }
    loaded = {k: (tex(bc), tex(n)) for k, (bc, n) in sets.items()}
    if not loaded["moss"][0] or not loaded["rock"][0]:
        raise RuntimeError("Megascans ground textures not migrated yet")
    tile = g.sparam("Tile", 400.0)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    xy = g.mask(wp, "", r=True, g=True)
    uv = g.node(unreal.MaterialExpressionDivide); g.link(xy, "", uv, "A"); g.link(tile, "", uv, "B")
    # second tiling scale breaks repetition
    uv2 = g.mul(uv, "", g.const(0.37), "")

    def sample(t, coords, normal=False):
        n = g.node(unreal.MaterialExpressionTextureSample)
        n.set_editor_property("texture", t)
        if normal:
            # The migrated Megascans maps are virtual textures: a plain Normal sampler on a
            # VT fails the whole material compile (UE then draws WorldGridMaterial's checker).
            vt = False
            try:
                vt = bool(t.get_editor_property("virtual_texture_streaming"))
            except Exception:
                pass
            st = unreal.MaterialSamplerType
            n.set_editor_property("sampler_type", st.SAMPLERTYPE_VIRTUAL_NORMAL if vt else st.SAMPLERTYPE_NORMAL)
        g.link(coords, "", n, "UVs")
        return n

    def layer(key, macro=False):
        bc, n = loaded[key]
        col = sample(bc, uv) if bc else None
        if col is not None and macro:
            # sample the same texture a second time at uv2 and blend 50/50 so the
            # 400 uu tile does not repeat visibly; normals stay at uv only
            col2 = sample(bc, uv2)
            col = g.lerp(col, "RGB", col2, "RGB", g.const(0.5), "")
        nrm = sample(n, uv, normal=True) if n else None
        return col, nrm

    moss_c, moss_n = layer("moss", macro=True)
    rock_c, rock_n = layer("rock", macro=True)
    mud_c, mud_n = layer("mud") if loaded["mud"][0] else (moss_c, moss_n)
    dry_c, dry_n = layer("dry", macro=True) if loaded["dry"][0] else (rock_c, rock_n)

    # slope mask from the vertex normal, wetness from vertex colour B, dryness parameter
    nrm_ws = g.node(unreal.MaterialExpressionVertexNormalWS)
    up = g.mask(nrm_ws, "", b=True)
    slope = g.saturate(g.mul(g.sub(g.const(0.80), "", up, ""), "", g.const(6.0), ""), "")     # 1 on slopes
    vc = g.node(unreal.MaterialExpressionVertexColor)
    wet = g.mask(vc, "", b=True)
    dry = g.sparam("Dryness", 0.0)
    breakup = g.noise(wp, "", 0.0015, levels=3, lo=0.0, hi=1.0,
                      func=unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_TEX)
    # far-distance tone breakup (very low frequency, multiplied into the base colour)
    macro = g.noise(wp, "", 0.00025, levels=2, lo=0.85, hi=1.15,
                    func=unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_TEX)
    slope_b = g.saturate(g.add(slope, "", g.mul(g.sub(breakup, "", g.const(0.5), ""), "", g.const(0.6), ""), ""), "")

    flat_c = g.lerp(moss_c, "", dry_c, "", dry, "")
    flat_n = g.lerp(moss_n, "RGB", dry_n, "RGB", dry, "") if (moss_n and dry_n) else None
    base = g.lerp(flat_c, "", rock_c, "", slope_b, "")
    mud_out = "RGB" if mud_c is not moss_c else ""
    base = g.lerp(base, "", mud_c, mud_out, g.saturate(g.mul(wet, "", g.sub(g.const(1.0), "", dry, ""), ""), ""), "")
    base = g.mul(base, "", macro, "")
    tint = g.vparam("Tint", 0.95, 1.0, 0.95)
    g.out(g.mul(base, "", tint, ""), "", MP.MP_BASE_COLOR)
    if flat_n and rock_n:
        nrm = g.lerp(flat_n, "", rock_n, "RGB", slope_b, "")
        if mud_n:
            nrm = g.lerp(nrm, "", mud_n, "RGB", wet, "")
        g.out(nrm, "", MP.MP_NORMAL)
    rough = g.lerp(g.const(0.85), "", g.const(0.35), "", wet, "")
    g.out(rough, "", MP.MP_ROUGHNESS)
    g.out(g.const(0.0), "", MP.MP_METALLIC)


def _tex(path):
    return eal.load_asset(path) if eal.does_asset_exist(path) else None


def _vt_sample(g, t, coords=None, normal=False, linear=False):
    """TextureSample with a VT-aware sampler type (same pattern as terrain_ed.sample): the migrated
    Megascans maps are virtual textures and a non-VT sampler on one fails the whole material compile."""
    n = g.node(unreal.MaterialExpressionTextureSample)
    n.set_editor_property("texture", t)
    vt = False
    try:
        vt = bool(t.get_editor_property("virtual_texture_streaming"))
    except Exception:
        pass
    st = unreal.MaterialSamplerType
    if normal:
        n.set_editor_property("sampler_type", st.SAMPLERTYPE_VIRTUAL_NORMAL if vt else st.SAMPLERTYPE_NORMAL)
    elif linear:
        n.set_editor_property("sampler_type", st.SAMPLERTYPE_VIRTUAL_LINEAR_COLOR if vt else st.SAMPLERTYPE_LINEAR_COLOR)
    else:
        n.set_editor_property("sampler_type", st.SAMPLERTYPE_VIRTUAL_COLOR if vt else st.SAMPLERTYPE_COLOR)
    if coords is not None:
        g.link(coords, "", n, "UVs")
    return n


def scan(g):
    """Replacement master for the Electric Dreams scan meshes (rocks, cliffs, boulders, river stones, mud
    patches). Their MI_* instances carry "Albedo", "Normal" and "DR" (packed displacement R, roughness G;
    some DpRF) virtual textures, but their own master depends on sample-only inputs and renders black.
    ASWEnvironment::BindScanMaterials makes a dynamic instance of this per slot and copies the three
    textures in. Mesh UVs (no world projection). Default textures: the HugeSandstoneCliff_01 set."""
    def find_any(suffix):
        for p in eal.list_assets("/Game/Megascans/3D_Assets", recursive=True, include_folder=False):
            name = p.split("/")[-1].split(".")[0]
            if name.startswith("T_") and name.endswith(suffix):
                return eal.load_asset(p)
        return None
    base = "/Game/Megascans/3D_Assets/HugeSandstoneCliff/T_HugeSandstoneCliff_01"
    albedo = _tex(base + "_BC") or find_any("_BC")
    normal = _tex(base + "_N") or find_any("_N")
    dr = _tex(base + "_DpR") or find_any("_DpR") or find_any("_DpRF")
    if not albedo:
        raise RuntimeError("no Megascans 3D asset textures migrated (need a T_*_BC under /Game/Megascans/3D_Assets)")
    st = unreal.MaterialSamplerType

    def tparam(name, tex, stype):
        n = g.node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name=name)
        n.set_editor_property("texture", tex)
        n.set_editor_property("sampler_type", stype)   # after the texture: setting the texture auto-picks a type
        return n

    a = tparam("Albedo", albedo, st.SAMPLERTYPE_VIRTUAL_COLOR)
    tint = g.vparam("Tint", 1.0, 1.0, 1.0)
    g.out(g.mul(a, "RGB", tint, ""), "", MP.MP_BASE_COLOR)
    if normal:
        nm = tparam("Normal", normal, st.SAMPLERTYPE_VIRTUAL_NORMAL)
        g.out(nm, "RGB", MP.MP_NORMAL)
    rs = g.sparam("RoughnessScale", 1.0)
    if dr:
        d = tparam("DR", dr, st.SAMPLERTYPE_VIRTUAL_MASKS)   # packed DpR masks: UE refuses Linear Color here
        g.out(g.mul(g.mask(d, "", g=True), "", rs, ""), "", MP.MP_ROUGHNESS)
    else:
        g.out(g.mul(g.const(0.8), "", rs, ""), "", MP.MP_ROUGHNESS)
    g.out(g.const(0.0), "", MP.MP_METALLIC)


def sandstone(g):
    """Massif arches (plate 1). World-aligned TRIPLANAR of the MossyRockyGround set at Tile uu, weights
    |VertexNormalWS|^4 normalised; SandTint drifting to RustTint per band (noise on world Z only); irregular
    STRATA from world Z (band = frac(z/150 + 0.55*noiseA(0.0012) + 0.2*noiseB(0.009)), ledge smoothstep(0.42, 0.58),
    x0.86..1.07) so the courses wander and break instead of reading as brick rows; slope darkening; NordicMoss on
    top-facing surfaces gated by vertex colour R (soft mask).
    Normals: the arch is a procedural mesh without tangents, so the material outputs a WORLD-space normal
    (tangent_space_normal=False): each plane's tangent-space rock normal sample is applied as a perturbation
    of the vertex normal and the three planes are blended with the same triplanar weights."""
    rock_bc = _tex("/Game/Megascans/Surfaces/MossyRockyGround/T_MossyRockyGround_01_BC")
    rock_n = _tex("/Game/Megascans/Surfaces/MossyRockyGround/T_MossyRockyGround_01_N")
    moss_bc = _tex("/Game/Megascans/Surfaces/NordicMoss/T_NordicMoss_01_BC")
    if not rock_bc:
        raise RuntimeError("MossyRockyGround textures not migrated yet")
    tile = g.sparam("Tile", 650.0)
    moss_tile = g.sparam("MossTile", 200.0)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    nrm = g.node(unreal.MaterialExpressionVertexNormalWS)
    nz = g.mask(nrm, "", b=True)

    # triplanar weights: |n|^4 / sum
    an = g.node(unreal.MaterialExpressionAbs); g.link(nrm, "", an, "")
    w4 = g.node(unreal.MaterialExpressionPower, const_exponent=4.0); g.link(an, "", w4, "Base")
    wsum = g.node(unreal.MaterialExpressionDotProduct); g.link(w4, "", wsum, "A"); g.link(g.const3(1, 1, 1), "", wsum, "B")
    w = g.node(unreal.MaterialExpressionDivide); g.link(w4, "", w, "A"); g.link(wsum, "", w, "B")
    wx = g.mask(w, "", r=True); wy = g.mask(w, "", g=True); wz = g.mask(w, "", b=True)

    def plane_uv(a, b, scale):
        m = g.node(unreal.MaterialExpressionComponentMask, r=a == "x" or b == "x", g=a == "y" or b == "y", b=a == "z" or b == "z")
        g.link(wp, "", m, "")
        d = g.node(unreal.MaterialExpressionDivide); g.link(m, "", d, "A"); g.link(scale, "", d, "B")
        return d
    uv_x = plane_uv("y", "z", tile)   # plane facing X samples with (Y, Z)
    uv_y = plane_uv("x", "z", tile)
    uv_z = plane_uv("x", "y", tile)

    def triblend(sx, sxo, sy, syo, sz, szo):
        return g.add(g.add(g.mul(sx, sxo, wx, ""), "", g.mul(sy, syo, wy, ""), ""), "", g.mul(sz, szo, wz, ""), "")

    cx = _vt_sample(g, rock_bc, uv_x); cy = _vt_sample(g, rock_bc, uv_y); cz = _vt_sample(g, rock_bc, uv_z)
    rock = triblend(cx, "RGB", cy, "RGB", cz, "RGB")

    # irregular strata: band = frac(z / 150 + 0.55 * noiseA + 0.2 * noiseB) with noiseA a large-scale (0.0012) and
    # noiseB a fine (0.009) gradient noise at world position, so the courses wander, pinch and break;
    # strata_mul = lerp(0.86, 1.07, smoothstep(0.42, 0.58, band)) keeps the ledge narrow and soft.
    zw = g.mask(wp, "", b=True)
    GRAD = unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_TEX
    nA = g.noise(wp, "", 0.0012, levels=2, lo=0.0, hi=1.0, func=GRAD)
    nB = g.noise(wp, "", 0.009, levels=2, lo=0.0, hi=1.0, func=GRAD)
    freqmod = g.noise(wp, "", 0.0006, levels=2, lo=0.7, hi=1.35, func=unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_TEX)   # beds widen and narrow
    band_in = g.add(g.add(g.mul(zw, "", g.mul(g.const(1.0 / 150.0), "", freqmod, ""), ""), "", g.mul(nA, "", g.const(0.55), ""), ""),
                    "", g.mul(nB, "", g.const(0.2), ""), "")
    band = g.node(unreal.MaterialExpressionFrac); g.link(band_in, "", band, "")
    ss = g.node(unreal.MaterialExpressionSmoothStep, const_min=0.42, const_max=0.58); g.link(band, "", ss, "Value")
    strata_mul = g.lerp(g.const(0.86), "", g.const(1.07), "", ss, "")
    # slope darkening
    slope = g.lerp(g.const(0.75), "", g.const(1.0), "", g.saturate(g.add(g.mul(nz, "", g.const(0.5), ""), "", g.const(0.5), ""), ""), "")
    # per-band colour drift: tint = lerp(SandTint, RustTint, saturate(noise(0.0007, world Z only))) so bands differ in hue
    sand = g.vparam("SandTint", 0.70, 0.56, 0.42)
    rust = g.vparam("RustTint", 0.58, 0.40, 0.28)
    z_only = g.append(g.append(g.const(0.0), "", g.const(0.0), ""), "", zw, "")
    hue_n = g.noise(z_only, "", 0.0007, levels=2, lo=0.0, hi=1.0, func=GRAD)
    tint = g.lerp(sand, "", rust, "", g.saturate(hue_n, ""), "")
    rock_col = g.mul(g.mul(g.mul(rock, "", tint, ""), "", strata_mul, ""), "", slope, "")

    # moss on top: saturate((n.z - 0.55) * 4) * VertexColor.R * noise, softened
    vc = g.node(unreal.MaterialExpressionVertexColor)
    top = g.saturate(g.mul(g.sub(nz, "", g.const(0.55), ""), "", g.const(4.0), ""), "")
    mn = g.noise(wp, "", 0.006, levels=3, lo=0.3, hi=1.3, func=unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_TEX)
    m_raw = g.mul(g.mul(top, "", g.mask(vc, "", r=True), ""), "", mn, "")
    moss_a = g.node(unreal.MaterialExpressionSmoothStep, const_min=0.3, const_max=0.7); g.link(m_raw, "", moss_a, "Value")
    if moss_bc:
        moss_c = _vt_sample(g, moss_bc, plane_uv("x", "y", moss_tile))
        base = g.lerp(rock_col, "", moss_c, "RGB", moss_a, "")
    else:
        base = g.lerp(rock_col, "", g.const3(0.10, 0.22, 0.09), "", moss_a, "")
    g.out(base, "", MP.MP_BASE_COLOR)
    g.out(g.lerp(g.const(0.85), "", g.const(0.7), "", moss_a, ""), "", MP.MP_ROUGHNESS)
    g.out(g.const(0.0), "", MP.MP_METALLIC)

    if rock_n:
        strength = g.sparam("NormalStrength", 1.0)
        nx = _vt_sample(g, rock_n, uv_x, normal=True)
        ny = _vt_sample(g, rock_n, uv_y, normal=True)
        nzs = _vt_sample(g, rock_n, uv_z, normal=True)
        zero = g.const(0.0)
        # per-plane perturbation of the vertex normal: (0, u, v) for the X plane, (u, 0, v) Y, (u, v, 0) Z
        px = g.append(zero, "", g.mask(nx, "", r=True, g=True), "")
        py = g.append(g.append(g.mask(ny, "", r=True), "", zero, ""), "", g.mask(ny, "", g=True), "")
        pz = g.append(g.mask(nzs, "", r=True, g=True), "", zero, "")
        pert = triblend(px, "", py, "", pz, "")
        n_ws = g.node(unreal.MaterialExpressionNormalize)
        g.link(g.add(nrm, "", g.mul(pert, "", strength, ""), ""), "", n_ws, "")
        g.out(n_ws, "", MP.MP_NORMAL)


make("M_SW_Terrain", lambda g: terrain(g, rocky=False))
make("M_SW_Rock", lambda g: terrain(g, rocky=True), used_with_instanced_static_meshes=True)
make("M_SW_Water", water,
     blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT,
     translucency_lighting_mode=unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING,
     screen_space_reflections=True,
     two_sided=False)
make("M_SW_Creature", creature)
make("M_SW_Glow", glow, used_with_instanced_static_meshes=True)
make("M_SW_Trail", trail,
     blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT,
     shading_model=unreal.MaterialShadingModel.MSM_UNLIT,
     two_sided=True)
make("M_SW_Waterfall", waterfall,
     blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT,
     shading_model=unreal.MaterialShadingModel.MSM_UNLIT,
     two_sided=True)
make("M_SW_Moon", moon, shading_model=unreal.MaterialShadingModel.MSM_UNLIT)
try:
    make("M_SW_TerrainED", terrain_ed)
except Exception as ex:  # textures not migrated yet: keep the procedural terrain material
    unreal.log_warning(f"M_SW_TerrainED skipped: {ex}")
make("M_SW_TraceOverlay", trace_overlay,
     blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT,
     shading_model=unreal.MaterialShadingModel.MSM_UNLIT,
     two_sided=True)
try:
    make("M_SW_Scan", scan, used_with_instanced_static_meshes=True)
except Exception as ex:  # scan textures not migrated: C++ leaves the scan meshes' own materials in place
    unreal.log_warning(f"M_SW_Scan skipped: {ex}")
try:
    make("M_SW_Sandstone", sandstone, tangent_space_normal=False)
except Exception as ex:  # surface textures not migrated: the massif arches fall back to M_SW_Rock
    unreal.log_warning(f"M_SW_Sandstone skipped: {ex}")
unreal.log("make_materials done")
