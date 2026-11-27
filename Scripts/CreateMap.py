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


# Battle royale field (Docs/11.26기획안.md "필드"): playable 500 x 500 m inside invisible limits.
# The old 56 x 48 m ruin garden stays in the middle; roads, team bases, camps, bushes, trees and rocks surround it.
# North of the field, outside the limits, is the sealed rift arena (reached only by the server teleport).
# Layout markers named Open_Area_<kind>_<n> (bush, camp_outer, camp_ruin, camp_elite, rift_gate, rift_arena) are
# exported by ExportServerCollision.py as "area" lines; the server reads camps, bushes and rift places from them.
FIELD = 25000
BASE_RADIUS = 16000            # server room_layout.rs TEAM_BASE_RADIUS
ARENA_CENTER, ARENA_RADIUS = (0, 27000), 1000
prop('HorizonGround', (0, 0, -90), (1500, 1500, 1), material='Earth', collision=False)
prop('SkyDome', (0, 0, 0), (2400, 2400, 2400), shape='Sphere', material='Sky', collision=False)
prop('Ground', (0, 0, -25), (504, 504, .5), material='Ground')
prop('CentralGate', (0,0,2), (4.6,4.6,.04), shape='Cylinder', material='DarkStone', collision=False)
for name, position, scale in [
    ('NorthLimit', (0, FIELD+20, 100), (500.8, .4, 2)),
    ('SouthLimit', (0, -FIELD-20, 100), (500.8, .4, 2)),
    ('EastLimit', (FIELD+20, 0, 100), (.4, 500, 2)),
    ('WestLimit', (-FIELD-20, 0, 100), (.4, 500, 2))
]:
    prop(name, position, scale, hidden=True)

# Zone (자기장) materials: the client builds the wall and the next-zone ring from panels at run time.
# Opacity follows world height: dense at the ground, fading toward the top of the 24 m wall. The colour is a deep blue
# well below the sky's brightness, so the wall reads against the sky and the field behind it stays visible.
MEL = unreal.MaterialEditingLibrary
for name, color, glow, bottom, top in (('ZoneWall', (.05, .25, 1.0), 1.2, .45, .12), ('ZoneNext', (1.0, 1.0, 1.0), 1.5, .75, .75)):
    asset_path = '/Game/Environment/M_' + name
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.EditorAssetLibrary.delete_asset(asset_path)
    mat = tools.create_asset('M_' + name, '/Game/Environment', unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    rgb = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
    rgb.set_editor_property('constant', unreal.LinearColor(*color, 1.0))
    bright = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply)
    bright.set_editor_property('const_b', glow)
    MEL.connect_material_expressions(rgb, '', bright, 'A')
    MEL.connect_material_property(bright, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    world = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition)
    z = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask)
    z.set_editor_property('r', False)
    z.set_editor_property('g', False)
    z.set_editor_property('b', True)
    MEL.connect_material_expressions(world, '', z, '')
    height = MEL.create_material_expression(mat, unreal.MaterialExpressionDivide)
    height.set_editor_property('const_b', 2400.0)
    MEL.connect_material_expressions(z, '', height, 'A')
    clamp = MEL.create_material_expression(mat, unreal.MaterialExpressionSaturate)
    MEL.connect_material_expressions(height, '', clamp, '')
    fade = MEL.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate)
    fade.set_editor_property('const_a', bottom)
    fade.set_editor_property('const_b', top)
    MEL.connect_material_expressions(clamp, '', fade, 'Alpha')
    MEL.connect_material_property(fade, '', unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(asset_path)

layout_rng = random.Random(20261128)
reserved = []  # (x, y, radius): places that scattered trees and rocks keep clear of


def polar(radius, degrees):
    a = math.radians(degrees)
    return math.cos(a)*radius, math.sin(a)*radius


def near_road(x, y, margin):
    """Distance test against the four diagonal roads (centre -> team bases)."""
    for team in range(4):
        dx, dy = polar(1, 45 + 90*team)
        along = x*dx + y*dy
        if 0 < along < BASE_RADIUS + 600 and abs(-x*dy + y*dx) < margin:
            return True
    return False


def clear(x, y, radius, extra=0):
    if abs(x) > FIELD - radius - 300 or abs(y) > FIELD - radius - 300:
        return False
    return all(math.hypot(x-rx, y-ry) >= radius + rr + extra for rx, ry, rr in reserved)


def area(kind, index, x, y, radius, material, hidden=False):
    """Flat, walk-through marker disc the exporter turns into an "area" line."""
    return prop('Area_%s_%d' % (kind, index), (x, y, 1.5), (radius/50, radius/50, .02), shape='Cylinder',
                material=material, collision=False, hidden=hidden)


reserved.append((0, 0, 2800))  # the old ruin garden and the rift plaza

# Dirt roads from the plaza to each team base.
for team in range(4):
    angle = 45 + 90*team
    for step in range(8):
        cx, cy = polar(2600 + 1700*step + 850, angle)
        prop('Road_%d_%d' % (team+1, step), (cx, cy, 1), (17.4, 3.2, .02), material='Path',
             yaw=angle + layout_rng.uniform(-1.5, 1.5), collision=False)

# Team bases on the diagonals: a coloured disc and two banners.
for team in range(4):
    angle = 45 + 90*team
    x, y = polar(BASE_RADIUS, angle)
    reserved.append((x, y, 1500))
    prop('Base_%d' % (team+1), (x, y, 1.2), (7, 7, .02), shape='Cylinder', material='Team%d' % (team+1), collision=False)
    a = math.radians(angle)
    for side in (-1, 1):
        bx, by = x + side*420*-math.sin(a), y + side*420*math.cos(a)
        prop('BaseBanner_%d_%d' % (team+1, side+1), (bx, by, 160), (.35, .35, 3.2), shape='Cylinder', material='DarkStone')
        prop('BaseFlag_%d_%d' % (team+1, side+1), (bx, by, 300), (.08, .9, .6), material='Team%d' % (team+1),
             yaw=angle, collision=False)

# Monster camps (server field.rs camp_kinds): outer camps near the bases, ruin camps on the middle ring,
# elite camps close to the plaza. A trampled earth disc and two low braziers at the edge.
camps = []
for team in range(4):
    base = 45 + 90*team
    for offset in (-22, 22):
        camps.append(('camp_outer', polar(20500, base + offset)))
    for offset in (-27, 27):
        camps.append(('camp_outer', polar(13000, base + offset)))
for cardinal in range(4):
    camps.append(('camp_outer', polar(18500, 90*cardinal)))
for index in range(12):
    camps.append(('camp_ruin', polar(9200, 30*index)))
for index in range(6):
    camps.append(('camp_elite', polar(4700, 60*index)))
counts = {}
for kind, (x, y) in camps:
    index = counts.get(kind, 0)
    counts[kind] = index + 1
    radius = 480 if kind == 'camp_elite' else 420
    reserved.append((x, y, radius + 500))
    area(kind, index, x, y, radius, 'Camp')
    for side in (-1, 1):
        prop('CampFire_%s_%d_%d' % (kind, index, side+1), (x + side*(radius+40), y, 20), (.6, .6, .4),
             shape='Cylinder', material='Gold' if kind == 'camp_elite' else 'DarkStone')

# Bushes: hide players from enemies outside (server rule). Walk-through leaf clumps over a dark ground patch.
# A few sit beside every camp for ambushes; the rest are scattered with room between them.
bushes = []
for kind, (x, y) in camps:
    a = math.atan2(y, x) + layout_rng.choice((-1, 1)) * math.radians(layout_rng.uniform(55, 80))
    d = 1500 + layout_rng.uniform(0, 300)
    bx, by, br = x + math.cos(a)*d, y + math.sin(a)*d, layout_rng.uniform(280, 360)
    if clear(bx, by, br, 150) and not near_road(bx, by, br + 250):
        bushes.append((bx, by, br))
        reserved.append((bx, by, br + 150))
attempts = 0
while len(bushes) < 84 and attempts < 20000:
    attempts += 1
    x, y, r = layout_rng.uniform(-FIELD, FIELD), layout_rng.uniform(-FIELD, FIELD), layout_rng.uniform(260, 420)
    if clear(x, y, r, 1500) and not near_road(x, y, r + 250):
        bushes.append((x, y, r))
        reserved.append((x, y, r + 150))
leaf_rng = random.Random(20261128)
for index, (x, y, radius) in enumerate(bushes):
    area('bush', index, x, y, radius, 'LeafDark')
    for clump in range(7):
        angle = clump * math.tau / 7 + leaf_rng.uniform(-.2, .2)
        reach = radius * (.2 if clump == 0 else leaf_rng.uniform(.45, .8))
        size = leaf_rng.uniform(1.7, 2.4)
        prop('Bush_%02d_%d' % (index, clump), (x + math.cos(angle)*reach, y + math.sin(angle)*reach, 55),
             (size, size, leaf_rng.uniform(1.3, 1.7)), shape='Sphere', material='Leaf' if clump % 2 else 'LeafDark',
             collision=False)

# Trees (blocking trunk, walk-under canopy) and boulders break the long sight lines of the open field.
trees = 0
attempts = 0
while trees < 170 and attempts < 20000:
    attempts += 1
    x, y = layout_rng.uniform(-FIELD, FIELD), layout_rng.uniform(-FIELD, FIELD)
    if not clear(x, y, 60, 650) or near_road(x, y, 450):
        continue
    reserved.append((x, y, 60))
    height = layout_rng.uniform(2.6, 3.6)
    prop('Tree_%03d' % trees, (x, y, height*50), (.55, .55, height), shape='Cylinder', material='Camp')
    for leaf in range(2):
        size = layout_rng.uniform(2.6, 3.6) - leaf*.7
        prop('Canopy_%03d_%d' % (trees, leaf), (x + layout_rng.uniform(-40, 40), y + layout_rng.uniform(-40, 40),
             height*100 + 40 + leaf*120), (size, size, size*.8), shape='Sphere',
             material='LeafDark' if leaf == 0 else 'Leaf', collision=False)
    trees += 1
boulders = 0
attempts = 0
while boulders < 70 and attempts < 20000:
    attempts += 1
    x, y = layout_rng.uniform(-FIELD, FIELD), layout_rng.uniform(-FIELD, FIELD)
    size = layout_rng.uniform(1.6, 2.8)
    if not clear(x, y, size*50, 900) or near_road(x, y, size*50 + 400):
        continue
    reserved.append((x, y, size*50))
    prop('Boulder_%02d' % boulders, (x, y, size*35), (size*1.2, size, size*.9), shape='Sphere', material='DarkStone',
         yaw=layout_rng.uniform(0, 360))
    prop('BoulderMoss_%02d' % boulders, (x+40, y-30, 2), (size*1.6, size*1.4, .035), shape='Cylinder', material='Moss',
         collision=False)
    boulders += 1

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

# Irregular distant rocks outside the limits frame the open horizon.
index = 0
for side in range(4):
    for step in range(26):
        along = -FIELD + 1000 + step * 1900 + rng.uniform(-300, 300)
        out = FIELD + 900 + rng.uniform(0, 900)
        x, y = [(along, out), (out, along), (along, -out), (-out, along)][side]
        if side == 0 and abs(x) < 2600:
            continue  # keep the view of the rift arena clear
        height = rng.uniform(250, 700)
        prop('OuterRock_%03d' % index, (x, y, height*.25),
             (rng.uniform(5, 10), rng.uniform(4, 8), height/65), shape='Cube', material='DarkStone',
             yaw=rng.uniform(0, 360), pitch=rng.uniform(-20, 20), collision=False)
        index += 1
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
unreal.log('ASCENSION_OPEN_RUINS_READY playable_width=50000 playable_depth=50000 props=' + str(len(actors.get_all_level_actors())))
