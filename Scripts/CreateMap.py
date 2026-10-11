import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if unreal.EditorAssetLibrary.does_asset_exist('/Game/Maps/CombatLab'):
    unreal.log('ASCENSION_MAP_EXISTS')
else:
    if not levels.new_level('/Game/Maps/CombatLab'):
        raise RuntimeError('Cannot create CombatLab')
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')

    def block(name, position, scale):
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*position))
        actor.set_actor_label(name)
        actor.static_mesh_component.set_static_mesh(cube)
        actor.static_mesh_component.set_collision_profile_name('BlockAll')
        actor.set_actor_scale3d(unreal.Vector(*scale))

    block('Floor', (0, 0, -25), (30, 26, .5))
    block('North', (0, 1320, 100), (30.8, .4, 2))
    block('South', (0, -1320, 100), (30.8, .4, 2))
    block('East', (1520, 0, 100), (.4, 26, 2))
    block('West', (-1520, 0, 100), (.4, 26, 2))
    block('Pillar_A', (100, 650, 120), (1.2, 1.2, 2.4))
    block('Pillar_B', (100, -650, 120), (1.2, 1.2, 2.4))
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 900), unreal.Rotator(-60, -30, 0))
    sun.light_component.set_editor_property('intensity', 5.0)
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500))
    sky.light_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky.light_component.set_editor_property('intensity', 1.5)
    ambient = unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap')
    if ambient:
        sky.light_component.set_editor_property('cubemap', ambient)
    actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-750, 0, 100))
    if not levels.save_current_level():
        raise RuntimeError('Map save failed')
    unreal.log('ASCENSION_MAP_CREATED')
