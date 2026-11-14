# 정제한 SharedKnight 메시·표면 맵을 기존 에셋 자리에 다시 가져온다.
#   UnrealEditor-Cmd <프로젝트> -run=pythonscript -script=Scripts/ImportRefinedKnight.py -unattended -nullrhi
# 입력: Assets/SharedKnightRefined/SharedKnightRig_Refined.fbx, SharedKnight_Normal.png, SharedKnight_Roughness.png
# 기존 스켈레톤을 유지하므로 애니메이션 5종은 다시 가져오지 않는다. 기준 자세가 달라지면 실패로 끝낸다.
import os
import unreal

root = os.path.abspath(os.path.join(unreal.Paths.project_dir(), 'Assets', 'SharedKnightRefined'))
tools = unreal.AssetToolsHelpers.get_asset_tools()
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.FBX 0')
MESH = '/Game/Models/SharedKnightRig'
ANIMS = ['SharedKnightActions_Idle', 'SharedKnightRun', 'SharedKnightActions_Right_Hand_Sword_Slash', 'SharedKnightActions_Charged_Upward_Slash', 'SharedKnightActions_Roll_Dodge']

def ref_pose(mesh):
    mod = unreal.SkeletonModifier()
    mod.set_skeletal_mesh(mesh)
    out = {}
    for name in mod.get_all_bone_names():
        t = mod.get_bone_transform(name, False)
        out[str(name)] = (str(mod.get_parent_name(name)), t.translation, t.rotation, t.scale3d)
    return out

def stats(mesh, tag):
    lib = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    n = lib.get_lod_count(mesh)
    unreal.log('ASC_REFINE %s lods=%d verts=%s bounds=%s' % (tag, n, [lib.get_num_verts(mesh, i) for i in range(n)], mesh.get_bounds().box_extent))

mesh = unreal.load_asset(MESH)
skeleton = mesh.get_editor_property('skeleton')
before = ref_pose(mesh); stats(mesh, 'before')

options = unreal.FbxImportUI()
options.set_editor_property('automated_import_should_detect_type', False)
options.set_editor_property('import_mesh', True)
options.set_editor_property('import_as_skeletal', True)
options.set_editor_property('import_animations', False)
options.set_editor_property('import_materials', False)
options.set_editor_property('import_textures', False)
options.set_editor_property('create_physics_asset', False)
options.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_SKELETAL_MESH)
options.set_editor_property('skeleton', skeleton)
data = options.get_editor_property('skeletal_mesh_import_data')
data.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
data.set_editor_property('normal_generation_method', unreal.FBXNormalGenerationMethod.MIKK_T_SPACE)
data.set_editor_property('import_morph_targets', False)
data.set_editor_property('update_skeleton_reference_pose', False)
data.set_editor_property('use_t0_as_ref_pose', False)
data.set_editor_property('convert_scene', True)
task = unreal.AssetImportTask()
task.set_editor_property('filename', os.path.join(root, 'SharedKnightRig_Refined.fbx'))
task.set_editor_property('destination_path', '/Game/Models')
task.set_editor_property('destination_name', 'SharedKnightRig')
task.set_editor_property('automated', True)
task.set_editor_property('replace_existing', True)
task.set_editor_property('replace_existing_settings', True)
task.set_editor_property('save', False)
task.set_editor_property('options', options)
tools.import_asset_tasks([task])
paths = [str(p) for p in task.get_editor_property('imported_object_paths')]
unreal.log('ASC_REFINE imported %s' % paths)
mesh = unreal.load_asset(MESH)
if not paths or not isinstance(mesh, unreal.SkeletalMesh):
    raise RuntimeError('Refined mesh import failed')
if mesh.get_editor_property('skeleton') != skeleton:
    raise RuntimeError('Skeleton changed: %s' % mesh.get_editor_property('skeleton'))

after = ref_pose(mesh)
if list(before.keys()) != list(after.keys()):
    raise RuntimeError('Bone list changed: %s -> %s' % (list(before.keys()), list(after.keys())))
worst_t = 0.0; worst_r = 0.0
for name in before:
    b = before[name]; a = after[name]
    if b[0] != a[0]: raise RuntimeError('Parent changed for ' + name)
    worst_t = max(worst_t, (b[1] - a[1]).length())
    dot = abs(b[2].x * a[2].x + b[2].y * a[2].y + b[2].z * a[2].z + b[2].w * a[2].w)
    worst_r = max(worst_r, 1.0 - min(dot, 1.0))
    worst_t = max(worst_t, (b[3] - a[3]).length())
    if (b[1] - a[1]).length() > 0.05 or 1.0 - min(dot, 1.0) > 1e-4 or (b[3] - a[3]).length() > 0.01:
        unreal.log('ASC_REFINE bone_delta %s before=%s %s %s after=%s %s %s' % (name, b[1], b[2], b[3], a[1], a[2], a[3]))
unreal.log('ASC_REFINE refpose bones=%d max_translation_or_scale_delta=%.5f max_rotation_delta=%.7f' % (len(after), worst_t, worst_r))
if worst_t > 0.05 or worst_r > 1e-4:
    raise RuntimeError('Reference pose changed; not saving')

# LOD: 원본보다 면이 늘었으므로 거리별 단순화 단계를 만든다.
lib = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
ok = lib.regenerate_lod(mesh, 4, True, False)
unreal.log('ASC_REFINE regenerate_lod %s' % ok)
stats(mesh, 'after')

def texture(name, compression):
    path = os.path.join(root, name + '.png')
    t = unreal.AssetImportTask()
    t.set_editor_property('filename', path)
    t.set_editor_property('destination_path', '/Game/Models')
    t.set_editor_property('destination_name', name)
    t.set_editor_property('automated', True)
    t.set_editor_property('replace_existing', True)
    t.set_editor_property('save', False)
    tools.import_asset_tasks([t])
    tex = unreal.load_asset('/Game/Models/' + name)
    tex.set_editor_property('srgb', False)
    tex.set_editor_property('compression_settings', compression)
    return tex
normal = texture('SharedKnight_Normal', unreal.TextureCompressionSettings.TC_NORMALMAP)
rough = texture('SharedKnight_Roughness', unreal.TextureCompressionSettings.TC_GRAYSCALE)

mel = unreal.MaterialEditingLibrary
material = unreal.load_asset('/Game/Models/M_SharedKnight')
mel.delete_all_material_expressions(material)
color = mel.create_material_expression(material, unreal.MaterialExpressionTextureSample, -400, -200)
color.set_editor_property('texture', unreal.load_asset('/Game/Models/BaseColor'))
mel.connect_material_property(color, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
ns = mel.create_material_expression(material, unreal.MaterialExpressionTextureSample, -400, 100)
ns.set_editor_property('texture', normal)
ns.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
mel.connect_material_property(ns, 'RGB', unreal.MaterialProperty.MP_NORMAL)
rs = mel.create_material_expression(material, unreal.MaterialExpressionTextureSample, -400, 400)
rs.set_editor_property('texture', rough)
rs.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
mel.connect_material_property(rs, 'R', unreal.MaterialProperty.MP_ROUGHNESS)
material.set_editor_property('used_with_skeletal_mesh', True)
mel.recompile_material(material)

for name in ANIMS:
    anim = unreal.load_asset('/Game/Models/' + name)
    if not anim or anim.get_editor_property('skeleton') != skeleton:
        raise RuntimeError('Animation no longer matches skeleton: ' + name)
unreal.log('ASC_REFINE animations_ok %d' % len(ANIMS))
for path in (MESH, '/Game/Models/SharedKnight_Normal', '/Game/Models/SharedKnight_Roughness', '/Game/Models/M_SharedKnight'):
    if not unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False):
        raise RuntimeError('Save failed: ' + path)
unreal.log('ASC_REFINE_COMPLETE')
