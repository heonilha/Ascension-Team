import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
path = '/Game/Maps/CombatLab'
existing = unreal.EditorAssetLibrary.does_asset_exist(path)
if not (levels.load_level(path) if existing else levels.new_level(path)):
    raise RuntimeError('Cannot open CombatLab')
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
by_label = {actor.get_actor_label(): actor for actor in actors.get_all_level_actors()}


def block(name, position, scale):
    actor = by_label.get(name)
    if actor is None:
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*position))
        actor.set_actor_label(name)
    actor.set_actor_location(unreal.Vector(*position), False, True)
    actor.static_mesh_component.set_static_mesh(cube)
    actor.static_mesh_component.set_collision_profile_name('BlockAll')
    actor.set_actor_scale3d(unreal.Vector(*scale))


# 36 x 32 metres; wall inner faces match AAscensionMode arena bounds.
block('Floor', (0, 0, -25), (36, 32, .5))
block('North', (0, 1620, 100), (36.8, .4, 2))
block('South', (0, -1620, 100), (36.8, .4, 2))
block('East', (1820, 0, 100), (.4, 32, 2))
block('West', (-1820, 0, 100), (.4, 32, 2))
block('Pillar_A', (100, 650, 120), (1.2, 1.2, 2.4))
block('Pillar_B', (100, -650, 120), (1.2, 1.2, 2.4))

if not existing:
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
unreal.log('ASCENSION_MAP_READY width=3600 depth=3200 existing=' + str(existing))