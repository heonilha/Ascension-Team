# 믹사모 동작을 가져와 주인공(Fab 'Man', UE4 마네킹 스켈레톤)용으로 리타깃한다.
#
# 실행(에디터를 닫고):
#   UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=<repo>\Scripts\ImportMixamo.py -unattended -nullrhi -nosplash
#
# 입력: Assets/Mixamo/
#   MixamoSkin.fbx  믹사모 스켈레톤을 만들 스킨 있는 FBX(아무 동작이나 With Skin). 용량이 커서 저장소에는 넣지 않는다.
#   Run.fbx Roll.fbx Slash.fbx Walk.fbx  Without Skin 동작
# 결과: /Game/Mixamo/Source (믹사모 스켈레톤·동작·IK 리그·리타기터), /Game/Mixamo/Animation/A_<이름> (게임에서 쓰는 동작)
import os
import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.FBX 0')
root = os.path.abspath(os.path.join(unreal.Paths.project_dir(), 'Assets', 'Mixamo'))
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
SOURCE, OUTPUT = '/Game/Mixamo/Source', '/Game/Mixamo/Animation'
CLIPS = ['Run', 'Roll', 'Slash', 'Walk']
TARGET_MESH = '/Game/Man/Mesh/Full/SK_Man_Full_03'


def fbx(file_name, asset_name, skeleton=None):
    path = os.path.join(root, file_name)
    if not os.path.isfile(path):
        raise RuntimeError('Missing ' + path)
    skeletal = skeleton is None
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
        # 믹사모 FBX 일부(구르기·베기)는 초당 1,400~1,900프레임으로 나와 동작 하나가 20MB를 넘는다. 30fps로 다시 샘플링한다
        anim_data = options.get_editor_property('anim_sequence_import_data')
        anim_data.set_editor_property('use_default_sample_rate', False)
        anim_data.set_editor_property('custom_sample_rate', 30)
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', path)
    task.set_editor_property('destination_path', SOURCE)
    task.set_editor_property('destination_name', asset_name)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('replace_existing_settings', True)
    task.set_editor_property('save', True)
    task.set_editor_property('options', options)
    tools.import_asset_tasks([task])
    paths = list(task.get_editor_property('imported_object_paths'))
    unreal.log('ASCENSION_MIXAMO_IMPORTED %s %s' % (asset_name, paths))
    return [unreal.load_asset(p) for p in paths]


def asset(name, path, cls, factory):
    # 다시 실행할 때는 있던 것을 불러와 비워 쓴다(지우고 같은 이름으로 다시 만들면 메모리에 남은 것과 부딪힌다)
    full = path + '/' + name
    if lib.does_asset_exist(full):
        return unreal.load_asset(full)
    return tools.create_asset(name, path, cls, factory)


def clear_rig(c):
    for chain in list(c.get_retarget_chains()):
        c.remove_retarget_chain(chain.get_editor_property('chain_name'))
    while c.get_num_solvers():
        c.remove_solver(0)
    for goal in list(c.get_all_goals()):
        c.remove_goal(goal.get_editor_property('goal_name'))

# 1) 믹사모 스켈레톤(스킨 FBX)과 동작
imported = fbx('MixamoSkin.fbx', 'SK_Mixamo')
source_mesh = next(o for o in imported if isinstance(o, unreal.SkeletalMesh))
skeleton = source_mesh.get_editor_property('skeleton')
for o in imported:  # 스킨 FBX에 같이 들어 있는 동작은 쓰지 않는다
    if isinstance(o, unreal.AnimSequence):
        lib.delete_asset(o.get_path_name())
sources = {}
for clip in CLIPS:
    anims = [o for o in fbx(clip + '.fbx', 'MX_' + clip, skeleton) if isinstance(o, unreal.AnimSequence)]
    sources[clip] = anims[0]

# 2) IK 리그(양쪽 모두 알려진 템플릿이라 자동 생성)
target_mesh = unreal.load_asset(TARGET_MESH)
rigs = {}
for name, mesh in (('IK_Mixamo', source_mesh), ('IK_Man', target_mesh)):
    rig = asset(name, SOURCE, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(rig)
    clear_rig(c)
    c.set_skeletal_mesh(mesh)
    ok = c.apply_auto_generated_retarget_definition()
    c.apply_auto_fbik()
    # 골반 이동·회전을 골반 본에 싣는다(기본값이면 마네킹 'root'가 골반 움직임을 떠맡아 대기 동작과 섞을 때 몸이 접힌다)
    for bone in (('pelvis',) if name == 'IK_Man' else ('Hips', 'mixamorig_Hips', 'mixamorig:Hips')):
        if c.set_retarget_root(bone) and str(c.get_retarget_root()) == bone:
            break
    unreal.log('ASCENSION_MIXAMO_RIG %s template=%s chains=%d retarget_root=%s' % (name, ok, len(c.get_retarget_chains()), c.get_retarget_root()))
    lib.save_asset(rig.get_path_name())
    rigs[name] = rig

# 3) 리타기터: 믹사모 T자세와 마네킹 A자세 차이는 목표 쪽 체인 정렬로 맞춘다
rtg = asset('RTG_MixamoToMan', SOURCE, unreal.IKRetargeter, unreal.IKRetargetFactory())
rc = unreal.IKRetargeterController.get_controller(rtg)
rc.remove_all_ops()
rc.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, rigs['IK_Mixamo'])
rc.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, rigs['IK_Man'])
rc.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, source_mesh)
rc.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, target_mesh)
rc.add_default_ops()
rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, rigs['IK_Mixamo'])
rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, rigs['IK_Man'])
rc.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
rc.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
# 루트 모션은 만들지 않는다(이동은 게임이 하고, 클립 안 골반 이동은 HeroMotion이 지운다)
for i in range(rc.get_num_retarget_ops()):
    op = str(rc.get_op_name(i))
    if 'Root' in op and 'Motion' in op:
        rc.set_retarget_op_enabled(i, False)
    unreal.log('ASCENSION_MIXAMO_OP %d %s enabled=%s' % (i, op, rc.get_retarget_op_enabled(i)))
lib.save_asset(rtg.get_path_name())

# 4) 동작 리타깃 → /Game/Mixamo/Animation/A_<이름>
for clip, anim in sources.items():
    final = OUTPUT + '/A_' + clip
    if lib.does_asset_exist(final):
        lib.delete_asset(final)
    data = unreal.EditorAssetLibrary.find_asset_data(anim.get_path_name())
    made = unreal.IKRetargetBatchOperation.duplicate_and_retarget([data], source_mesh, target_mesh, rtg, '', '', '', '_Man', OUTPUT, False, False, True)
    for d in made:
        p = str(d.package_name)
        if p.startswith(OUTPUT) and unreal.load_asset(p).get_class().get_name() == 'AnimSequence':
            lib.rename_asset(p, final)
    lib.save_asset(final)
    a = unreal.load_asset(final)
    unreal.log('ASCENSION_MIXAMO_RETARGETED %s length=%.3f frames=%d skeleton=%s' % (final, a.get_play_length(), unreal.AnimationLibrary.get_num_frames(a), a.get_editor_property('skeleton').get_name()))

# 스킨 FBX를 가져올 때 같이 생긴 스켈레톤·물리 에셋까지 모두 저장한다
lib.save_directory(SOURCE, only_if_is_dirty=False, recursive=True)
lib.save_directory(OUTPUT, only_if_is_dirty=False, recursive=True)
unreal.log('ASCENSION_MIXAMO_SAVED %s' % sorted(str(p) for p in lib.list_assets(SOURCE, recursive=True)))

