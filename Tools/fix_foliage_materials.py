"""Give imported foliage scans a proper masked material.

Poly Haven foliage FBX files reference diffuse/normal/roughness only; the cutout
mask ships as a separate <stem>_alpha_<res>.png that Interchange never sees, so
the grass/fern cards import opaque. This script, per foliage stem:
  1. imports the alpha PNG as a texture (if not already there),
  2. builds M_PH_<stem>: masked, two-sided, diffuse -> base colour,
     alpha.R -> opacity mask, normal map -> normal, roughness.R -> roughness,
  3. assigns it to every material slot of the imported static mesh and saves.

Run inside the editor process (same reason as import_assets.py):
  set SW_FOLIAGE_SRC=<repo>/AssetSources/polyhaven
  set SW_FOLIAGE_DEST=/Game/Assets/PolyHaven
  UnrealEditor.exe SymbioticWorld.uproject -ExecutePythonScript="<abs>/Tools/fix_foliage_materials.py" -unattended -nosplash -log
"""
import glob
import os
import unreal

_SW_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.environ.get("SW_FOLIAGE_SRC", os.path.join(_SW_ROOT, "AssetSources", "polyhaven"))
DEST = os.environ.get("SW_FOLIAGE_DEST", "/Game/Assets/PolyHaven")
STEMS = [s for s in os.environ.get("SW_FOLIAGE_STEMS", "fern_02,grass_medium_01,grass_medium_02,shrub_01,shrub_02,shrub_03").split(",") if s]

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
atools = unreal.AssetToolsHelpers.get_asset_tools()
registry = unreal.AssetRegistryHelpers.get_asset_registry()
MP = unreal.MaterialProperty


def find_asset(name_prefix, cls_name):
    for a in registry.get_assets_by_path(DEST, recursive=True):
        if str(a.asset_class_path.asset_name) == cls_name and str(a.asset_name).startswith(name_prefix):
            return a.get_asset()
    return None


def import_texture(path, dest):
    name = os.path.splitext(os.path.basename(path))[0]
    existing = eal.load_asset(f"{dest}/{name}")
    if existing:
        return existing
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", path)
    t.set_editor_property("destination_path", dest)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    atools.import_asset_tasks([t])
    paths = t.get_editor_property("imported_object_paths")
    return eal.load_asset(paths[0]) if paths else None


def tex_node(mat, tex, x, y, is_normal=False):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, x, y)
    n.set_editor_property("texture", tex)
    if is_normal:
        n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    elif tex and tex.get_editor_property("srgb") is False:
        n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    return n


def build_material(stem, diff, alpha, nor, rough):
    folder = f"{DEST}/Masked"
    name = f"M_PH_{stem}"
    path = f"{folder}/{name}"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    mat = atools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("opacity_mask_clip_value", 0.35)
    if diff:
        d = tex_node(mat, diff, -600, -200)
        mel.connect_material_property(d, "RGB", MP.MP_BASE_COLOR)
    if alpha:
        alpha.set_editor_property("srgb", False)
        a = tex_node(mat, alpha, -600, 100)
        mel.connect_material_property(a, "R", MP.MP_OPACITY_MASK)
    if nor:
        nor.set_editor_property("srgb", False)
        try:
            nor.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        except Exception:
            pass
        n = tex_node(mat, nor, -600, 400, is_normal=True)
        mel.connect_material_property(n, "RGB", MP.MP_NORMAL)
    if rough:
        rough.set_editor_property("srgb", False)
        r = tex_node(mat, rough, -600, 700)
        mel.connect_material_property(r, "R", MP.MP_ROUGHNESS)
    mel.recompile_material(mat)
    eal.save_asset(mat.get_path_name())
    return mat


fixed = 0
for stem in STEMS:
    mesh = find_asset(stem, "StaticMesh")
    if not mesh:
        unreal.log_warning(f"[fix_foliage] no static mesh for {stem}")
        continue
    # Interchange imports the mesh but (for these foliage FBX files) none of the
    # textures, so pull every map straight from the source folder.
    tex_dir = os.path.join(SRC, stem, "textures")
    def grab(pattern):
        files = sorted(glob.glob(os.path.join(tex_dir, pattern)))
        return import_texture(files[0], DEST) if files else None
    alpha = grab(f"{stem}_alpha_*.png")
    diff = grab(f"{stem}_diff_*.jpg") or grab(f"{stem}_diff_*.png")
    nor = grab(f"{stem}_nor_gl_*.exr") or grab(f"{stem}_nor_gl_*.png")
    rough = grab(f"{stem}_rough_*.exr") or grab(f"{stem}_rough_*.png")
    mat = build_material(stem, diff, alpha, nor, rough)
    n_slots = len(mesh.get_editor_property("static_materials"))
    for i in range(max(n_slots, 1)):
        mesh.set_material(i, mat)
    eal.save_asset(mesh.get_path_name())
    unreal.log(f"[fix_foliage] {stem}: alpha={'yes' if alpha else 'NO'} diff={'yes' if diff else 'NO'} nor={'yes' if nor else 'NO'} slots={n_slots} -> {mat.get_path_name()}")
    fixed += 1
unreal.log(f"[fix_foliage] done, {fixed} meshes")
