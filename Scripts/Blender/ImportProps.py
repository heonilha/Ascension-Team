# UnrealEditor-Cmd <project> -run=pythonscript -script=ImportProps.py
# Imports Assets/Blender/FBX/*.fbx as static meshes under /Game/Blender and assigns
# project materials (emissive runes cannot travel through FBX).
import glob
import os
import unreal

SOURCE = os.environ.get('ASCENSION_PROPS_FBX') or os.path.join(unreal.Paths.project_dir(), 'Assets', 'Blender', 'FBX')
TARGET = '/Game/Blender'
MATERIALS = {  # name: (color, roughness, metallic, emissive strength) — mirrors CreateProps.py
    'Stone': ((.30, .285, .245), .92, 0, 0), 'DarkStone': ((.12, .135, .125), .95, 0, 0),
    'Moss': ((.07, .14, .075), .98, 0, 0), 'Gold': ((.62, .36, .08), .38, 1, 0),
    'Steel': ((.62, .64, .68), .28, 1, 0), 'Leather': ((.16, .085, .045), .75, 0, 0),
    'Cloth': ((.30, .22, .13), .9, 0, 0), 'RuneRed': ((1.0, .18, .06), .5, 0, 6),
    'PortalBlue': ((.25, .6, 1.0), .5, 0, 5), 'ExpCyan': ((.2, .95, 1.0), .2, 0, 4),
    'AltarGold': ((1.0, .7, .2), .4, 0, 3),
}
lib = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.FBX 0')


def material(name):
    path = '%s/Materials/M_Prop%s' % (TARGET, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    color, rough, metal, emit = MATERIALS[name]
    mat = tools.create_asset('M_Prop' + name, TARGET + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
    rgb = lib.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
    rgb.set_editor_property('constant', unreal.LinearColor(*color, 1))
    lib.connect_material_property(rgb, '', unreal.MaterialProperty.MP_BASE_COLOR)
    for value, prop in ((rough, unreal.MaterialProperty.MP_ROUGHNESS), (metal, unreal.MaterialProperty.MP_METALLIC)):
        const = lib.create_material_expression(mat, unreal.MaterialExpressionConstant)
        const.set_editor_property('r', value)
        lib.connect_material_property(const, '', prop)
    if emit:
        glow = lib.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
        glow.set_editor_property('constant', unreal.LinearColor(*(c * emit for c in color), 1))
        lib.connect_material_property(glow, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    return mat


files = sorted(glob.glob(os.path.join(SOURCE, 'SM_*.fbx')))
tasks = []
for path in files:
    options = unreal.FbxImportUI()
    options.set_editor_property('automated_import_should_detect_type', False)
    options.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
    options.set_editor_property('import_mesh', True)
    options.set_editor_property('import_as_skeletal', False)
    options.set_editor_property('import_materials', False)
    options.set_editor_property('import_textures', False)
    data = options.static_mesh_import_data
    data.set_editor_property('combine_meshes', True)
    data.set_editor_property('auto_generate_collision', True)
    data.set_editor_property('generate_lightmap_u_vs', True)
    task = unreal.AssetImportTask()
    for key, value in (('filename', path), ('destination_path', TARGET), ('automated', True),
                       ('replace_existing', True), ('save', True), ('options', options)):
        task.set_editor_property(key, value)
    tasks.append(task)
tools.import_asset_tasks(tasks)

for path in files:
    name = os.path.splitext(os.path.basename(path))[0]
    mesh = unreal.load_asset('%s/%s' % (TARGET, name))
    if not mesh:
        unreal.log_error('ASCENSION_PROP_IMPORT_FAILED ' + name)
        continue
    for index, slot in enumerate(mesh.get_editor_property('static_materials')):
        slot_name = str(slot.get_editor_property('material_slot_name'))
        if slot_name in MATERIALS:
            mesh.set_material(index, material(slot_name))
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    box = mesh.get_bounding_box()
    size = box.max - box.min
    unreal.log('ASCENSION_PROP_IMPORTED %s size_cm=%.0fx%.0fx%.0f x=%.0f..%.0f min_z=%.0f slots=%d' % (
        name, size.x, size.y, size.z, box.min.x, box.max.x, box.min.z, len(mesh.get_editor_property('static_materials'))))
