import math
import random
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
path = '/Game/Maps/CombatLab'
existing = unreal.EditorAssetLibrary.does_asset_exist(path)
if not (levels.load_level(path) if existing else levels.new_level(path)):
    raise RuntimeError('Cannot open CombatLab')

# Rebuild only this script's scenery. Lighting, start and gameplay assets survive.
for actor in actors.get_all_level_actors():
    label = actor.get_actor_label()
    if label in ('Floor', 'North', 'South', 'East', 'West', 'Pillar_A', 'Pillar_B') or label.startswith('Open_'):
        actors.destroy_actor(actor)

meshes = {name: unreal.load_asset('/Engine/BasicShapes/' + name) for name in ('Cube', 'Cylinder', 'Sphere')}
tools = unreal.AssetToolsHelpers.get_asset_tools()
materials = {}
for name, color in {
    'Ground': (.10, .155, .115), 'Earth': (.075, .105, .085),
    'Stone': (.26, .245, .205), 'DarkStone': (.12, .135, .125),
    'Path': (.32, .30, .24), 'Moss': (.07, .13, .075), 'Gold': (.55, .30, .065)
}.items():
    asset_path = '/Game/Environment/M_' + name
    mat = unreal.load_asset(asset_path)
    if not mat:
        mat = tools.create_asset('M_' + name, '/Game/Environment', unreal.Material, unreal.MaterialFactoryNew())
        rgb = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
        rgb.set_editor_property('constant', unreal.LinearColor(*color, 1.0))
        unreal.MaterialEditingLibrary.connect_material_property(rgb, '', unreal.MaterialProperty.MP_BASE_COLOR)
        rough = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant)
        rough.set_editor_property('r', .9)
        unreal.MaterialEditingLibrary.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
        unreal.MaterialEditingLibrary.recompile_material(mat)
        unreal.EditorAssetLibrary.save_asset(asset_path)
    materials[name] = mat

sky_path = '/Game/Environment/M_RuinsSky'
sky_material = unreal.load_asset(sky_path)
if not sky_material:
    sky_material = tools.create_asset('M_RuinsSky', '/Game/Environment', unreal.Material, unreal.MaterialFactoryNew())
    sky_material.set_editor_property('two_sided', True)
    sky_material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    color = unreal.MaterialEditingLibrary.create_material_expression(sky_material, unreal.MaterialExpressionConstant3Vector)
    color.set_editor_property('constant', unreal.LinearColor(.15, .27, .40, 1))
    unreal.MaterialEditingLibrary.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(sky_material)
    unreal.EditorAssetLibrary.save_asset(sky_path)
materials['Sky'] = sky_material


def prop(name, position, scale, shape='Cube', material='Stone', yaw=0, collision=True, hidden=False, pitch=0):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*position))
    actor.set_actor_label('Open_' + name)
    actor.set_actor_rotation(unreal.Rotator(pitch, yaw, 0), False)
    mesh = actor.static_mesh_component
    mesh.set_static_mesh(meshes[shape])
    mesh.set_material(0, materials[material])
    mesh.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    if not collision:
        mesh.set_editor_property('cast_shadow', False)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    actor.set_actor_hidden_in_game(hidden)
    return actor


# Playable 56 x 48 m; scenery extends beyond the invisible safety boundary.
prop('HorizonGround', (0, 0, -90), (240, 240, 1), material='Earth', collision=False)
prop('SkyDome', (0, 0, 0), (1600,1600,1600), shape='Sphere', material='Sky', collision=False)
prop('Ground', (0, 0, -25), (64, 56, .5), material='Ground')
for name, position, scale in [
    ('NorthLimit', (0, 2420, 100), (56.8, .4, 2)),
    ('SouthLimit', (0, -2420, 100), (56.8, .4, 2)),
    ('EastLimit', (2820, 0, 100), (.4, 48, 2)),
    ('WestLimit', (-2820, 0, 100), (.4, 48, 2))
]:
    prop(name, position, scale, hidden=True)

rng = random.Random(20261011)
# Broken paving offers readable routes without blocking pursuit or rolling.
for index in range(36):
    angle = index * math.tau / 36
    radius = 380 + rng.uniform(-35, 35)
    prop('Plaza_%02d' % index, (math.cos(angle)*radius, math.sin(angle)*radius, 1),
         (1.05, 1.65, .02), material='Path', yaw=math.degrees(angle), collision=False)
for route, target in enumerate(((650, 650), (-1100, 900), (1550, -1000), (-1650, -900))):
    for step in range(4, 15):
        ratio = step / 15
        x, y = target[0]*ratio, target[1]*ratio
        prop('Route_%d_%02d' % (route, step), (x+rng.uniform(-15,15), y+rng.uniform(-15,15), 1),
             (1.15, 1.55, .02), material='Path', yaw=math.degrees(math.atan2(target[1], target[0]))-90,
             collision=False)

# Small ruin islands leave several broad ways around each obstruction.
for index, (x,y) in enumerate(((-1550,-850),(1500,-1000),(-1750,1200),(1600,1200))):
    prop('RuinBase_%d' % index, (x,y,2), (4.4,3.3,.04), material='Path', collision=False)
    prop('Column_%d_A' % index, (x-160,y,140), (1.1,1.1,2.8), shape='Cylinder')
    prop('Column_%d_B' % index, (x+160,y,140), (1.1,1.1,2.8), shape='Cylinder')
    prop('Lintel_%d' % index, (x,y,300), (4.4,1.3,.7))
    prop('ColumnFoot_%d_A' % index, (x-160,y,20), (1.5,1.5,.4))
    prop('ColumnFoot_%d_B' % index, (x+160,y,20), (1.5,1.5,.4))
    prop('BrokenWall_%d' % index, (x+230,y+160,65), (2.7,.7,1.3), yaw=25)
    prop('Rubble_%d' % index, (x-230,y+200,35), (1.1,1.5,.7), shape='Sphere', material='DarkStone', yaw=50)

# Existing awakening altar now has a visible stone landmark; E interaction is unchanged.
prop('AltarGround', (650,650,1), (5.3,5.3,.02), shape='Cylinder', material='DarkStone', collision=False)
prop('AltarRing', (650,650,3), (2.5,2.5,.03), shape='Cylinder', material='Gold', collision=False)
for index,(x,y) in enumerate(((420,930),(880,930),(100,650))):
    prop('AltarColumn_%d' % index, (x,y,130), (.95,.95,2.6), shape='Cylinder')
    prop('AltarCap_%d' % index, (x,y,275), (1.35,1.35,.3))
prop('SouthBrokenColumn', (100,-650,90), (1.2,1.2,1.8), shape='Cylinder', material='DarkStone')
prop('ExitLandmark', (-1100,900,1), (3.5,3.5,.02), shape='Cylinder', material='Path', collision=False)

# Irregular distant rocks replace the enclosing walls and frame the open horizon.
for index in range(32):
    angle = index * math.tau / 32
    x = math.cos(angle)*(3100+rng.uniform(-120,180))
    y = math.sin(angle)*(2700+rng.uniform(-100,160))
    height = rng.uniform(120,330)
    prop('OuterRock_%02d' % index, (x,y,height*.25),
         (rng.uniform(3,6),rng.uniform(2.8,5),height/65), shape='Cube', material='DarkStone',
         yaw=rng.uniform(0,360), pitch=rng.uniform(-20,20), collision=False)
for index,(x,y) in enumerate(((-2200,-1300),(2100,100),(2100,-1700),(-2200,500),(1000,1850),(-600,-1850))):
    prop('CoverRock_%d' % index, (x,y,70), (2.2,2.7,1.9), shape='Sphere', material='DarkStone', yaw=index*47)
    prop('Moss_%d' % index, (x+65,y-40,2), (3.7,3.3,.035), shape='Cylinder', material='Moss', collision=False)

current = actors.get_all_level_actors()
suns = [a for a in current if isinstance(a, unreal.DirectionalLight)]
sun = suns[0] if suns else actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,0,900))
sun.set_actor_rotation(unreal.Rotator(-48,-35,0), False)
sun.light_component.set_editor_property('intensity', 5.0)
sun.light_component.set_editor_property('atmosphere_sun_light', True)
if not any(isinstance(a, unreal.SkyAtmosphere) for a in current):
    actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0,0,0)).set_actor_label('RuinsSky')
if not any(isinstance(a, unreal.SkyLight) for a in current):
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0,0,500))
    sky.light_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky.light_component.set_editor_property('cubemap', unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
    sky.light_component.set_editor_property('intensity', 1.5)
if not any(isinstance(a, unreal.PlayerStart) for a in current):
    actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-750,0,100))
if not levels.save_current_level():
    raise RuntimeError('Map save failed')
unreal.log('ASCENSION_OPEN_RUINS_READY playable_width=5600 playable_depth=4800 props=' + str(len(actors.get_all_level_actors())))
