"""Import the authored Lumen/Tecton creature package in the full UE editor (not a commandlet).

SW_CREATURE_SOURCE points to the package's exports directory (SK_<Species>.fbx, LOD1/LOD2,
A_<Species>_Idle/Walk.fbx, textures/). By default it is <repo>/AssetSources/SymbioticCreatures/exports
(AssetSources is git-ignored). Output: /Game/Characters/Symbiotic/<Species>/ with the skeletal mesh,
three LODs, skeleton, the two clips, the four textures and M_<Species>_Authored; a report goes to
Saved/CreatureIntegration/import_report.json. Never run while a C++ build is running (module DLL lock).

  set SW_CREATURE_SOURCE=<package>\exports
  "C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" ^
      "<repo>/SymbioticWorld.uproject" /Engine/Maps/Entry ^
      -ExecutePythonScript="<repo>/Tools/import_symbiotic_creatures.py" -unattended -RenderOffScreen

The script imports, saves and exits the editor. The C++ side (ASWAgent::BuildAuthoredBody) loads
what it produced; see docs/CREATURE_RENDERING.md.
"""
import json
import os
from pathlib import Path
import traceback
import unreal as ue

ROOT = Path(ue.Paths.project_dir()).resolve()
SOURCE = Path(os.environ.get('SW_CREATURE_SOURCE', str(ROOT/'AssetSources/SymbioticCreatures/exports')))
DEST = '/Game/Characters/Symbiotic'
REPORT = ROOT/'Saved/CreatureIntegration/import_report.json'
REPORT.parent.mkdir(parents=True, exist_ok=True)
TOOLS = ue.AssetToolsHelpers.get_asset_tools()
SM = ue.get_editor_subsystem(ue.SkeletalMeshEditorSubsystem)
REPORT_DATA = {'source':str(SOURCE), 'species':{}, 'ok':False}


def task(filename, destination, options=None, factory=None):
    t=ue.AssetImportTask()
    t.filename=str(filename);t.destination_path=destination;t.destination_name=Path(filename).stem
    t.automated=True;t.replace_existing=True;t.save=True
    if options:t.options=options
    if factory:t.factory=factory
    TOOLS.import_asset_tasks([t])
    paths=list(t.imported_object_paths)
    ue.log('SW_CREATURE_IMPORT '+str(paths))
    return [ue.load_asset(p) for p in paths]


def fbx_options(animation=False, skeleton=None):
    ui=ue.FbxImportUI()
    ui.automated_import_should_detect_type=False
    ui.import_as_skeletal=True;ui.import_mesh=not animation
    ui.import_animations=animation;ui.import_materials=False;ui.import_textures=False
    ui.create_physics_asset=False
    ui.mesh_type_to_import=ue.FBXImportType.FBXIT_ANIMATION if animation else ue.FBXImportType.FBXIT_SKELETAL_MESH
    ui.original_import_type=ue.FBXImportType.FBXIT_SKELETAL_MESH
    if skeleton:ui.skeleton=skeleton
    data=ui.anim_sequence_import_data if animation else ui.skeletal_mesh_import_data
    data.convert_scene=True;data.convert_scene_unit=True
    data.force_front_x_axis=False
    if not animation:
        data.normal_import_method=ue.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
    return ui


def import_material(species, dest):
    textures={}
    for suffix in ['BaseColor','Normal','Roughness','Emission']:
        objects=task(SOURCE/species/'textures'/f'{species}_{suffix}.png', dest+'/Textures', factory=ue.TextureFactory())
        tex=next(o for o in objects if isinstance(o,ue.Texture2D))
        tex.set_editor_property('srgb',suffix in ['BaseColor','Emission'])
        tex.set_editor_property('virtual_texture_streaming',False)
        if suffix=='Normal':
            tex.set_editor_property('compression_settings',ue.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property('flip_green_channel',True)
        elif suffix=='Roughness':
            tex.set_editor_property('compression_settings',ue.TextureCompressionSettings.TC_DEFAULT)
        ue.EditorAssetLibrary.save_loaded_asset(tex)
        textures[suffix]=tex
    name=f'M_{species}_Authored'
    material=ue.load_asset(dest+'/'+name) if ue.EditorAssetLibrary.does_asset_exist(dest+'/'+name) else TOOLS.create_asset(name,dest,ue.Material,ue.MaterialFactoryNew())
    ML=ue.MaterialEditingLibrary
    ML.delete_all_material_expressions(material)
    material.set_editor_property('used_with_skeletal_mesh',True)
    material.set_editor_property('blend_mode',ue.BlendMode.BLEND_OPAQUE)
    MP=ue.MaterialProperty
    row=[0]
    def node(cls):
        row[0]+=1
        return ML.create_material_expression(material,cls,-800+(row[0]%5)*240,(row[0]//5)*200)
    def scalar(name,value):
        n=node(ue.MaterialExpressionScalarParameter);n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',value);return n
    def vector(name,value):
        n=node(ue.MaterialExpressionVectorParameter);n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',ue.LinearColor(*value,1));return n
    def connect(a,b,pin,out=''):
        assert ML.connect_material_expressions(a,out,b,pin),(a.get_name(),b.get_name(),pin,out)
    def mul(a,b):
        n=node(ue.MaterialExpressionMultiply);connect(a,n,'A');connect(b,n,'B');return n
    samples={}
    for suffix,tex in textures.items():
        n=node(ue.MaterialExpressionTextureSample);n.texture=tex
        st=ue.MaterialSamplerType
        n.sampler_type=st.SAMPLERTYPE_NORMAL if suffix=='Normal' else st.SAMPLERTYPE_LINEAR_COLOR if suffix=='Roughness' else st.SAMPLERTYPE_COLOR
        samples[suffix]=n
    is_lumen=species=='Lumen'
    # Lift body midtones independently of the emissive mask; retain atlas variation.
    body=mul(samples['BaseColor'],scalar('BodyBrightness',3.0 if is_lumen else 5.0))
    body=mul(body,vector('BodyTint',(.72,1.05,1.15) if is_lumen else (.90,1.0,1.12)))
    saturate=node(ue.MaterialExpressionSaturate);connect(body,saturate,'')
    ML.connect_material_property(saturate,'',MP.MP_BASE_COLOR)
    rough=node(ue.MaterialExpressionMax)
    connect(samples['Roughness'],rough,'A','R');connect(scalar('MinimumRoughness',.52 if is_lumen else .72),rough,'B')
    ML.connect_material_property(rough,'',MP.MP_ROUGHNESS)
    ML.connect_material_property(scalar('Specular',.28 if is_lumen else .32),'',MP.MP_SPECULAR)
    normal=node(ue.MaterialExpressionLinearInterpolate)
    connect(vector('NeutralNormal',(0,0,1)),normal,'A');connect(samples['Normal'],normal,'B','RGB')
    connect(scalar('NormalStrength',.65 if is_lumen else .8),normal,'Alpha')
    unit=node(ue.MaterialExpressionNormalize);connect(normal,unit,'')
    ML.connect_material_property(unit,'',MP.MP_NORMAL)
    glow=mul(samples['Emission'],vector('GlowTint',(.65,1.1,1.2) if is_lumen else (1.0,2.0,.65)))
    glow=mul(glow,scalar('EmissiveStrength',5.0))
    # A restrained body edge keeps Lumen legible from the valley camera.
    if is_lumen:
        rim=node(ue.MaterialExpressionFresnel);rim.set_editor_property('exponent',3.5);rim.set_editor_property('base_reflect_fraction',.02)
        rim=mul(mul(rim,body),scalar('BodyRimStrength',.6))
        add=node(ue.MaterialExpressionAdd);connect(glow,add,'A');connect(rim,add,'B');glow=add
    ML.connect_material_property(glow,'',MP.MP_EMISSIVE_COLOR)
    ML.recompile_material(material)
    ue.EditorAssetLibrary.save_loaded_asset(material)
    return material


def main():
    for species in ['Lumen','Tecton']:
        dest=DEST+'/'+species
        objects=task(SOURCE/species/f'SK_{species}.fbx',dest,fbx_options(),ue.FbxFactory())
        mesh=next(o for o in objects if isinstance(o,ue.SkeletalMesh))
        expected_path=dest+f'/SK_{species}'
        if mesh.get_path_name().split('.')[0]!=expected_path:
            assert ue.EditorAssetLibrary.rename_asset(mesh.get_path_name(),expected_path)
        skeleton=mesh.get_editor_property('skeleton')
        material=import_material(species,dest)
        slots=list(mesh.get_editor_property('materials'))
        for slot in slots:slot.set_editor_property('material_interface',material)
        mesh.set_editor_property('materials',slots)
        for lod in [1,2]:
            result=SM.import_lod(mesh,lod,str(SOURCE/species/f'SK_{species}_LOD{lod}.fbx'))
            assert result==lod,(species,lod,result)
        # UE 5.7 no longer exposes the old lod_info array to Python. The importer
        # initializes screen thresholds for the custom LODs through AddLODInfo.
        # LOD import may restore FBX material assignments: bind the atlas again.
        slots=list(mesh.get_editor_property('materials'))
        for slot in slots:slot.set_editor_property('material_interface',material)
        mesh.set_editor_property('materials',slots)
        ue.EditorAssetLibrary.save_loaded_asset(mesh)
        clips={}
        for clip in ['Idle','Walk']:
            ui=fbx_options(True,skeleton);ui.override_animation_name=f'A_{species}_{clip}'
            objects=task(SOURCE/species/f'A_{species}_{clip}.fbx',dest,ui,ue.FbxFactory())
            anim=next(o for o in objects if isinstance(o,ue.AnimSequence))
            name=dest+f'/A_{species}_{clip}'
            if anim.get_path_name().split('.')[0]!=name:
                assert ue.EditorAssetLibrary.rename_asset(anim.get_path_name(),name)
            duration=anim.get_editor_property('sequence_length')
            assert duration>0
            assert anim.get_editor_property('skeleton')==skeleton
            clips[clip]={'asset':anim.get_path_name(),'seconds':duration}
        bounds=mesh.get_bounds()
        # Record mesh bounds; the source is authored nose-forward along +X.
        record={'mesh':mesh.get_path_name(),'skeleton':skeleton.get_path_name(),
                'lod_count':SM.get_lod_count(mesh),'lod_vertices':[SM.get_num_verts(mesh,i) for i in range(3)],
                'material':material.get_path_name(),'clips':clips,
                'bounds_extent_cm':[bounds.box_extent.x,bounds.box_extent.y,bounds.box_extent.z],
                'source_forward':' +X', 'source_up':' +Z'}
        assert record['lod_count']==3
        assert 100<max(record['bounds_extent_cm'])<500,record
        REPORT_DATA['species'][species]=record
        ue.log('SW_CREATURE_VERIFIED '+json.dumps(record))
        ue.EditorAssetLibrary.save_directory(dest,only_if_is_dirty=True,recursive=True)
    REPORT_DATA['ok']=True


try:
    main()
except Exception:
    REPORT_DATA['error']=traceback.format_exc()
    ue.log_error(REPORT_DATA['error'])
finally:
    REPORT.write_text(json.dumps(REPORT_DATA,indent=2))
    ue.log('SW_CREATURE_IMPORT_DONE '+str(REPORT_DATA['ok']))
    ue.SystemLibrary.quit_editor()
