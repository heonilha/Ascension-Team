import os
import unreal
task = unreal.AssetImportTask()
task.set_editor_property('filename', os.path.join(unreal.Paths.project_dir(), 'Assets', 'Audio', 'SwordImpact.wav'))
task.set_editor_property('destination_path', '/Game/Audio')
task.set_editor_property('destination_name', 'SwordImpact')
task.set_editor_property('automated', True)
task.set_editor_property('replace_existing', True)
task.set_editor_property('save', True)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
if not unreal.load_asset('/Game/Audio/SwordImpact'):
    raise RuntimeError('Impact audio import failed')
unreal.EditorAssetLibrary.save_directory('/Game/Audio', only_if_is_dirty=False)
