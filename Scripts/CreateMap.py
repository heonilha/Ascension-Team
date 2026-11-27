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
    'Path': (.32, .30, .24), 'Moss': (.07, .13, .075), 'Gold': (.55, .30, .065),
    'Leaf': (.05, .20, .045), 'LeafDark': (.035, .13, .035), 'Camp': (.16, .11, .07),
    'Rift': (.20, .06, .30), 'Team1': (.55, .09, .07), 'Team2': (.08, .22, .60),
    'Team3': (.10, .45, .12), 'Team4': (.62, .50, .08)
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
    actor.set_actor_rotation(unreal.Rotator(pitch=pitch, yaw=yaw, roll=0), False)
    mesh = actor.static_mesh_component
    mesh.set_static_mesh(meshes[shape])
    mesh.set_material(0, materials[material])
    mesh.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    if not collision:
        mesh.set_editor_property('cast_shadow', False)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    actor.set_actor_hidden_in_game(hidden)
    return actor


# Battle royale field (Docs/11.26기획안.md "필드"): playable 100 x 100 m inside invisible limits.
# The old 56 x 48 m ruin garden stays in the middle; team bases, camps and bushes surround it.
# North of the field, outside the limits, is the sealed rift arena (reached only by the server teleport).
# Layout markers named Open_Area_<kind>_<n> (bush, camp_outer, camp_ruin, camp_elite, rift_gate, rift_arena) are
# exported by ExportServerCollision.py as "area" lines; the server reads camps, bushes and rift places from them.
FIELD = 5000
ARENA_CENTER, ARENA_RADIUS = (0, 6500), 1000
prop('HorizonGround', (0, 0, -90), (300, 300, 1), material='Earth', collision=False)
prop('SkyDome', (0, 0, 0), (1600,1600,1600), shape='Sphere', material='Sky', collision=False)
prop('Ground', (0, 0, -25), (104, 104, .5), material='Ground')
prop('CentralGate', (0,0,2), (4.6,4.6,.04), shape='Cylinder', material='DarkStone', collision=False)
for name, position, scale in [
    ('NorthLimit', (0, FIELD+20, 100), (100.8, .4, 2)),
    ('SouthLimit', (0, -FIELD-20, 100), (100.8, .4, 2)),
    ('EastLimit', (FIELD+20, 0, 100), (.4, 100, 2)),
    ('WestLimit', (-FIELD-20, 0, 100), (.4, 100, 2))
]:
    prop(name, position, scale, hidden=True)


def area(kind, index, x, y, radius, material, hidden=False):
    """Flat, walk-through marker disc the exporter turns into an "area" line."""
    return prop('Area_%s_%d' % (kind, index), (x, y, 1.5), (radius/50, radius/50, .02), shape='Cylinder',
                material=material, collision=False, hidden=hidden)


# Team bases on the diagonals (server room_layout.rs TEAM_BASE_RADIUS = 4000): a coloured disc and two banners.
for team in range(4):
    angle = math.radians(45 + 90*team)
    x, y = math.cos(angle)*4000, math.sin(angle)*4000
    prop('Base_%d' % (team+1), (x, y, 1.2), (7, 7, .02), shape='Cylinder', material='Team%d' % (team+1), collision=False)
    for side in (-1, 1):
        bx, by = x + side*420*-math.sin(angle), y + side*420*math.cos(angle)
        prop('BaseBanner_%d_%d' % (team+1, side+1), (bx, by, 160), (.35, .35, 3.2), shape='Cylinder', material='DarkStone')
        prop('BaseFlag_%d_%d' % (team+1, side+1), (bx, by, 300), (.08, .9, .6), material='Team%d' % (team+1),
             yaw=math.degrees(angle), collision=False)

# Monster camps: a trampled earth disc and two low braziers at the edge.
camps = [('camp_outer', (4200, 0)), ('camp_outer', (0, 4200)), ('camp_outer', (-4200, 0)), ('camp_outer', (0, -4200)),
         ('camp_ruin', (-1550, -1350)), ('camp_ruin', (1500, -1500)), ('camp_ruin', (-1750, 1700)), ('camp_ruin', (1600, 1700)),
         ('camp_elite', (0, 2750)), ('camp_elite', (0, -2750))]
counts = {}
for kind, (x, y) in camps:
    index = counts.get(kind, 0)
    counts[kind] = index + 1
    radius = 480 if kind == 'camp_elite' else 420
    area(kind, index, x, y, radius, 'Camp')
    for side in (-1, 1):
        prop('CampFire_%s_%d_%d' % (kind, index, side+1), (x + side*(radius+40), y, 20), (.6, .6, .4),
             shape='Cylinder', material='Gold' if kind == 'camp_elite' else 'DarkStone')

# Bushes: hide players from enemies outside (server rule). Walk-through leaf clumps over a dark ground patch.
bushes = []
for team in range(4):
    for offset in (-13, 13):
        angle = math.radians(45 + 90*team + offset)
        bushes.append((math.cos(angle)*3050, math.sin(angle)*3050, 300))
for x, y in ((3000, 1300), (-3000, -1300), (-1300, 3000), (1300, -3000), (2950, -1200), (-2950, 1200),
             (1200, 3000), (-1200, -3000), (1150, -550), (-1150, 550)):
    bushes.append((x, y, 260))
leaf_rng = random.Random(20261128)
for index, (x, y, radius) in enumerate(bushes):
    area('bush', index, x, y, radius, 'LeafDark')
    for clump in range(9):
        angle = clump * math.tau / 9 + leaf_rng.uniform(-.2, .2)
        reach = radius * (.25 if clump == 0 else leaf_rng.uniform(.45, .8))
        size = leaf_rng.uniform(1.6, 2.3)
        prop('Bush_%02d_%d' % (index, clump), (x + math.cos(angle)*reach, y + math.sin(angle)*reach, 55),
             (size, size, leaf_rng.uniform(1.3, 1.7)), shape='Sphere', material='Leaf' if clump % 2 else 'LeafDark',
             collision=False)

# Rift gates: invisible markers; the client draws them only while the server says the rift is open.
area('rift_gate', 0, -800, 0, 170, 'Rift', hidden=True)
area('rift_gate', 1, 800, 0, 170, 'Rift', hidden=True)

# Rift arena: sealed stone ring outside the field.
ax, ay = ARENA_CENTER
prop('RiftFloor', (ax, ay, -25), (24, 24, .5), material='DarkStone')
area('rift_arena', 0, ax, ay, ARENA_RADIUS, 'Rift')
for index in range(32):
    angle = index * math.tau / 32
    wall_radius = ARENA_RADIUS + 60
    prop('RiftWall_%02d' % index, (ax + math.cos(angle)*wall_radius, ay + math.sin(angle)*wall_radius, 150),
         (.4, 2.15, 3), material='Rift', yaw=math.degrees(angle))
for index in range(6):
    angle = index * math.tau / 6 + .26
    prop('RiftPillar_%d' % index, (ax + math.cos(angle)*560, ay + math.sin(angle)*560, 130), (.8, .8, 2.6),
         shape='Cylinder', material='Rift')

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
for index in range(48):
    angle = index * math.tau / 48
    x = math.cos(angle)*(5700+rng.uniform(-120,220))
    y = math.sin(angle)*(5700+rng.uniform(-120,220))
    height = rng.uniform(150,420)
    if y > 0 and abs(x) < 2200:
        continue  # keep the view of the rift arena clear
    prop('OuterRock_%02d' % index, (x,y,height*.25),
         (rng.uniform(3,6),rng.uniform(2.8,5),height/65), shape='Cube', material='DarkStone',
         yaw=rng.uniform(0,360), pitch=rng.uniform(-20,20), collision=False)
for index,(x,y) in enumerate(((-2200,-1300),(2100,100),(2100,-1700),(-2200,500),(1000,1850),(-600,-1850))):
    prop('CoverRock_%d' % index, (x,y,70), (2.2,2.7,1.9), shape='Sphere', material='DarkStone', yaw=index*47)
    prop('Moss_%d' % index, (x+65,y-40,2), (3.7,3.3,.035), shape='Cylinder', material='Moss', collision=False)

current = actors.get_all_level_actors()
suns = [a for a in current if isinstance(a, unreal.DirectionalLight)]
sun = suns[0] if suns else actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,0,900))
sun.set_actor_rotation(unreal.Rotator(pitch=-48, yaw=-35, roll=0), False)
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
for actor in actors.get_all_level_actors():
    if actor.get_actor_label().startswith(('Open_Route_', 'Open_Plaza_')):
        rotation = actor.get_actor_rotation()
        if abs(rotation.pitch) > .01 or abs(rotation.roll) > .01:
            raise RuntimeError('Paving tile is tilted: ' + actor.get_actor_label())
unreal.log('ASCENSION_PAVING_FLAT PASS')
unreal.log('ASCENSION_OPEN_RUINS_READY playable_width=10000 playable_depth=10000 props=' + str(len(actors.get_all_level_actors())))
