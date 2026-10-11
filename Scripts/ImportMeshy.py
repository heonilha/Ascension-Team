import os
import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.FBX 0')
root = os.path.abspath(os.path.join(unreal.Paths.project_dir(), 'Assets', 'Meshy', 'SharedKnight'))
tools = unreal.AssetToolsHelpers.get_asset_tools()

def fbx(name, skeletal=False, skeleton=None):
    path = os.path.join(root, name + '.fbx')
    if not os.path.isfile(path):
        unreal.log('ASCENSION_IMPORT_SKIPPED ' + name)
        return None
    target = '/Game/Models/' + name
    if name == 'SharedKnightActions' and unreal.EditorAssetLibrary.does_asset_exist('/Game/Models/SharedKnightActions_Idle'):
        return unreal.load_asset('/Game/Models/SharedKnightActions_Idle')
    if unreal.EditorAssetLibrary.does_asset_exist(target):
        existing = unreal.load_asset(target)
        if existing and existing.get_editor_property('skeleton'):
            return existing
    options = unreal.FbxImportUI()
    options.set_editor_property('automated_import_should_detect_type', False)
    options.set_editor_property('import_mesh', skeletal)
    options.set_editor_property('import_as_skeletal', skeletal)
    options.set_editor_property('import_animations', not skeletal)
    options.set_editor_property('import_materials', False)
    options.set_editor_property('import_textures', False)
    options.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else unreal.FBXImportType.FBXIT_ANIMATION)
    if skeleton:
        options.set_editor_property('skeleton', skeleton)
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', path)
    task.set_editor_property('destination_path', '/Game/Models')
    task.set_editor_property('destination_name', name)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('replace_existing_settings', True)
    task.set_editor_property('save', True)
    task.set_editor_property('options', options)
    tools.import_asset_tasks([task])
    paths = task.get_editor_property('imported_object_paths')
    unreal.log('ASCENSION_IMPORTED ' + name + ' ' + str(paths))
    return unreal.load_asset(paths[0]) if paths else None

mesh = fbx('SharedKnightRig', True)
if not mesh or not isinstance(mesh, unreal.SkeletalMesh):
    raise RuntimeError('Skeletal mesh import failed')
skeleton = mesh.get_editor_property('skeleton')
if not skeleton:
    raise RuntimeError('Imported character has no skeleton')
unreal.EditorAssetLibrary.save_directory('/Game/Models', only_if_is_dirty=False, recursive=True)
fbx('SharedKnightRun', skeleton=skeleton)
fbx('SharedKnightActions', skeleton=skeleton)

color_path = os.path.join(root, 'BaseColor.png')
if os.path.isfile(color_path) and not unreal.EditorAssetLibrary.does_asset_exist('/Game/Models/BaseColor'):
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', color_path)
    task.set_editor_property('destination_path', '/Game/Models')
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    tools.import_asset_tasks([task])

material_path = '/Game/Models/M_SharedKnight'
if not unreal.EditorAssetLibrary.does_asset_exist(material_path):
    material = tools.create_asset('M_SharedKnight', '/Game/Models', unreal.Material, unreal.MaterialFactoryNew())
    color = unreal.load_asset('/Game/Models/BaseColor')
    if color:
        sample = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionTextureSample)
        sample.set_editor_property('texture', color)
        unreal.MaterialEditingLibrary.connect_material_property(sample, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant)
    roughness.set_editor_property('r', 0.7)
    unreal.MaterialEditingLibrary.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(material_path)
material = unreal.load_asset(material_path)
material.set_editor_property('used_with_skeletal_mesh', True)
unreal.MaterialEditingLibrary.recompile_material(material)
unreal.EditorAssetLibrary.save_asset(material_path)
for path in unreal.EditorAssetLibrary.list_assets('/Game/Models', recursive=True):
    asset = unreal.load_asset(path)
    if isinstance(asset, unreal.AnimSequence):
        asset.set_editor_property('force_root_lock', True)
        asset.set_editor_property('enable_root_motion', False)
unreal.EditorAssetLibrary.save_directory('/Game/Models', only_if_is_dirty=False, recursive=True)
unreal.log('ASCENSION_MODELS ' + str(unreal.EditorAssetLibrary.list_assets('/Game/Models', recursive=True)))
unreal.log('ASCENSION_IMPORT_COMPLETE')
