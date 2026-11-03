# Blender 5.2: blender -b -P Scripts/Blender/CreateProps.py -- <output_dir>
# Builds low-poly dark-fantasy props that replace the engine primitive placeholders.
# Units: 1 Blender metre = 100 UE cm. +X forward (UE +X), Z up, origin at the floor/grip.
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
OUT = os.path.abspath(argv[0] if argv else os.path.join(os.getcwd(), 'Assets', 'Blender'))
FBX = os.path.join(OUT, 'FBX')
os.makedirs(FBX, exist_ok=True)
rng = random.Random(20261103)

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'

# name: (base color, roughness, metallic, emission strength)
MATERIALS = {
    'Stone':     ((.30, .285, .245), .92, 0, 0),
    'DarkStone': ((.12, .135, .125), .95, 0, 0),
    'Moss':      ((.07, .14, .075), .98, 0, 0),
    'Gold':      ((.62, .36, .08), .38, 1, 0),
    'Steel':     ((.62, .64, .68), .28, 1, 0),
    'Leather':   ((.16, .085, .045), .75, 0, 0),
    'Cloth':     ((.30, .22, .13), .9, 0, 0),
    'RuneRed':   ((1.0, .18, .06), .5, 0, 6),
    'PortalBlue': ((.25, .6, 1.0), .5, 0, 5),
    'ExpCyan':   ((.2, .95, 1.0), .2, 0, 4),
    'AltarGold': ((1.0, .7, .2), .4, 0, 3),
}


def material(name):
    mat = bpy.data.materials.get(name)
    if mat:
        return mat
    color, rough, metal, emit = MATERIALS[name]
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = (*color, 1)
    bsdf.inputs['Roughness'].default_value = rough
    bsdf.inputs['Metallic'].default_value = metal
    if emit:
        bsdf.inputs['Emission Color'].default_value = (*color, 1)
        bsdf.inputs['Emission Strength'].default_value = emit
    mat.diffuse_color = (*color, 1)
    return mat


def activate(obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def finish(obj, mat, loc=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1), bevel=0.0, jitter=0.0, flat=True):
    obj.location, obj.rotation_euler, obj.scale = loc, [math.radians(a) for a in rot], scale
    activate(obj)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    if jitter:
        for v in obj.data.vertices:
            v.co += Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))) * jitter
    if bevel:
        mod = obj.modifiers.new('Bevel', 'BEVEL')
        mod.width, mod.segments, mod.limit_method = bevel, 1, 'ANGLE'
        bpy.ops.object.modifier_apply(modifier=mod.name)
    obj.data.materials.clear()
    obj.data.materials.append(material(mat))
    for poly in obj.data.polygons:
        poly.use_smooth = not flat
    return obj


def cube(mat, loc, size, rot=(0, 0, 0), bevel=.02, jitter=0.0):
    bpy.ops.mesh.primitive_cube_add(size=1)
    return finish(bpy.context.object, mat, loc, rot, size, bevel, jitter)


def cyl(mat, loc, radius, depth, verts=16, rot=(0, 0, 0), bevel=.015, jitter=0.0, flat=True):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=radius, depth=depth)
    return finish(bpy.context.object, mat, loc, rot, (1, 1, 1), bevel, jitter, flat)


def cone(mat, loc, r1, r2, depth, verts=16, rot=(0, 0, 0), bevel=0.0):
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r1, radius2=r2, depth=depth)
    return finish(bpy.context.object, mat, loc, rot, (1, 1, 1), bevel)


def torus(mat, loc, major, minor, rot=(0, 0, 0), major_seg=32, minor_seg=6, scale=(1, 1, 1)):
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor,
                                     major_segments=major_seg, minor_segments=minor_seg)
    return finish(bpy.context.object, mat, loc, rot, scale)


def rock(mat, loc, size, subdiv=2, noise=.22, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=subdiv, radius=.5)
    obj = bpy.context.object
    for v in obj.data.vertices:
        v.co *= 1 + rng.uniform(-noise, noise)
    # Flatten the underside so rocks sit on the ground.
    obj = finish(obj, mat, (0, 0, 0), rot, size)
    for v in obj.data.vertices:
        v.co.z = max(v.co.z, -size[2] * .32)
    obj.location = loc
    activate(obj)
    bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)
    return obj


def prism(mat, points, z0, z1, loc=(0, 0, 0), rot=(0, 0, 0)):
    """Extrude a 2D outline (XY or as given) between two heights."""
    mesh = bpy.data.meshes.new('prism')
    n = len(points)
    verts = [(x, y, z0) for x, y in points] + [(x, y, z1) for x, y in points]
    faces = [list(reversed(range(n))), list(range(n, 2 * n))]
    faces += [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new('prism', mesh)
    scene.collection.objects.link(obj)
    return finish(obj, mat, loc, rot)


def fluted_shaft(mat, loc, radius, depth, flutes=12):
    bpy.ops.mesh.primitive_cylinder_add(vertices=flutes * 2, radius=radius, depth=depth)
    obj = bpy.context.object
    for v in obj.data.vertices:
        index = round((math.atan2(v.co.y, v.co.x) % math.tau) / math.tau * flutes * 2)
        if index % 2:
            v.co.x *= .9
            v.co.y *= .9
    return finish(obj, mat, loc, bevel=.01)


ASSETS = []


def asset(name, parts):
    """Join parts into one mesh object whose origin is the world origin."""
    bpy.ops.object.select_all(action='DESELECT')
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    if len(parts) > 1:
        bpy.ops.object.join()
    obj = bpy.context.object
    obj.name = obj.data.name = name
    bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    ASSETS.append(obj)
    return obj


# ---------------------------------------------------------------- weapons / items
def build_sword():
    # Grip centred on the origin, blade along +X, flat of the blade in the X-Z plane.
    parts = [cyl('Leather', (0, 0, 0), .019, .2, 10, rot=(0, 90, 0), bevel=0)]
    for x in (-.07, -.025, .02, .065):
        parts.append(torus('Leather', (x, 0, 0), .021, .006, rot=(0, 90, 0), major_seg=10, minor_seg=4))
    parts.append(cube('Gold', (-.12, 0, 0), (.05, .045, .045), rot=(45, 0, 0), bevel=.008))
    parts.append(cube('Gold', (.115, 0, 0), (.035, .045, .26), bevel=.008))
    parts.append(cube('Gold', (.115, 0, .14), (.04, .05, .04), rot=(0, 45, 0), bevel=.006))
    parts.append(cube('Gold', (.115, 0, -.14), (.04, .05, .04), rot=(0, 45, 0), bevel=.006))
    # Diamond-section blade with tapering tip.
    mesh = bpy.data.meshes.new('blade')
    x0, x1, tip = .13, .86, .98
    w0, w1, t = .036, .028, .007
    verts = [(x0, 0, w0), (x0, t, 0), (x0, 0, -w0), (x0, -t, 0),
             (x1, 0, w1), (x1, t * .8, 0), (x1, 0, -w1), (x1, -t * .8, 0), (tip, 0, 0)]
    faces = [(0, 3, 2, 1)] + [(i, (i + 1) % 4, 4 + (i + 1) % 4, 4 + i) for i in range(4)]
    faces += [(4 + i, 4 + (i + 1) % 4, 8) for i in range(4)]
    mesh.from_pydata(verts, [], faces)
    blade = bpy.data.objects.new('blade', mesh)
    scene.collection.objects.link(blade)
    parts.append(finish(blade, 'Steel'))
    parts.append(cube('Gold', (.2, 0, 0), (.1, .016, .012), bevel=0))  # fuller inlay
    return asset('SM_Sword', parts)


def build_loot_bag():
    bpy.ops.mesh.primitive_uv_sphere_add(segments=14, ring_count=8, radius=.22)
    body = bpy.context.object
    for v in body.data.vertices:
        v.co.z *= .85 if v.co.z > 0 else .7
        v.co *= 1 + rng.uniform(-.06, .06)
    body = finish(body, 'Cloth', (0, 0, .17), flat=False)
    parts = [body,
             cone('Cloth', (0, 0, .39), .07, .11, .1, 10),
             torus('Leather', (0, 0, .35), .07, .018, major_seg=14, minor_seg=5),
             cube('Gold', (.21, 0, .16), (.02, .12, .1), bevel=.006),  # buckle plate
             cube('Steel', (.0, .0, .45), (.04, .04, .04), rot=(30, 20, 0), bevel=.005)]
    return asset('SM_LootBag', parts)


def build_exp_crystal():
    mesh = bpy.data.meshes.new('gem')
    r, h = .09, .16
    ring = [(r * math.cos(a * math.tau / 6), r * math.sin(a * math.tau / 6), 0) for a in range(6)]
    verts = ring + [(0, 0, h), (0, 0, -h * .8)]
    faces = [(i, (i + 1) % 6, 6) for i in range(6)] + [((i + 1) % 6, i, 7) for i in range(6)]
    mesh.from_pydata(verts, [], faces)
    gem = bpy.data.objects.new('gem', mesh)
    scene.collection.objects.link(gem)
    return asset('SM_ExpCrystal', [finish(gem, 'ExpCyan')])


def build_spin_blade():
    parts = [cyl('Gold', (0, 0, 0), .07, .04, 12, bevel=.008), cyl('Steel', (0, 0, 0), .035, .06, 8, bevel=0)]
    outline = [(.06, -.03), (.2, -.07), (.34, -.04), (.42, .04), (.3, .0), (.17, .04), (.06, .03)]
    for i in range(3):
        parts.append(prism('Steel', outline, -.008, .008, rot=(0, 0, i * 120)))
    return asset('SM_SpinBlade', parts)


# ---------------------------------------------------------------- ruins
def build_column():
    parts = [cube('Stone', (0, 0, .2), (1.5, 1.5, .4), bevel=.04, jitter=.02),
             cyl('Stone', (0, 0, .47), .62, .14, 20, bevel=.02),
             fluted_shaft('Stone', (0, 0, 1.5), .5, 1.95),
             cyl('Stone', (0, 0, 2.53), .6, .12, 20, bevel=.02),
             cube('Stone', (0, 0, 2.68), (1.3, 1.3, .24), bevel=.03, jitter=.015)]
    # Moss band around the foot.
    parts.append(cyl('Moss', (0, 0, .42), .78, .04, 14, jitter=.03))
    return asset('SM_RuinColumn', parts)


def build_column_broken():
    parts = [cube('DarkStone', (0, 0, .2), (1.5, 1.5, .4), bevel=.04, jitter=.02),
             fluted_shaft('DarkStone', (0, 0, 1.0), .52, 1.3)]
    top = cyl('DarkStone', (0, 0, 1.72), .52, .3, 24, bevel=0)
    for v in top.data.vertices:  # jagged break
        if v.co.z > 1.72:
            v.co.z += rng.uniform(-.25, .2)
            v.co.x *= rng.uniform(.75, 1)
            v.co.y *= rng.uniform(.75, 1)
    parts.append(top)
    for i in range(3):
        a = rng.uniform(0, math.tau)
        parts.append(rock('DarkStone', (math.cos(a) * 1.0, math.sin(a) * 1.0, .1), (.4, .35, .3), 1, .25))
    return asset('SM_RuinColumnBroken', parts)


def build_lintel():
    parts = [cube('Stone', (0, 0, 0), (4.4, 1.3, .55), bevel=.05, jitter=.025),
             cube('Stone', (0, 0, .34), (4.6, 1.4, .14), bevel=.03, jitter=.015)]
    for x in (-1.5, -.5, .5, 1.5):  # carved panels
        parts.append(cube('DarkStone', (x, -.655, 0), (.7, .02, .3), bevel=0))
    parts.append(cube('Moss', (1.4, 0, .43), (1.1, 1.0, .03), jitter=.03))
    return asset('SM_RuinLintel', parts)


def build_broken_wall():
    parts = []
    rows, brick_h, length = 5, .26, 2.7
    for row in range(rows):
        offset = .2 if row % 2 else 0
        count = 6 - (row > 2) - (row > 3) * 2
        x = -length / 2 + offset
        for _ in range(count):
            w = rng.uniform(.38, .5)
            if x + w > length / 2:
                break
            parts.append(cube('Stone' if rng.random() > .25 else 'DarkStone',
                              (x + w / 2, rng.uniform(-.02, .02), row * brick_h + brick_h / 2),
                              (w - .03, .7, brick_h - .025), bevel=.03, jitter=.012))
            x += w
    for _ in range(4):
        parts.append(rock('DarkStone', (rng.uniform(.4, 1.4), rng.uniform(.5, .8), .08), (.3, .28, .2), 1, .3))
    return asset('SM_BrokenWall', parts)


def build_rubble():
    parts = []
    for i in range(9):
        a, d = rng.uniform(0, math.tau), rng.uniform(0, .55)
        s = rng.uniform(.25, .55)
        if rng.random() < .5:
            parts.append(cube('Stone', (math.cos(a) * d, math.sin(a) * d, s * .3), (s, s * .8, s * .55),
                              rot=(rng.uniform(-15, 15), rng.uniform(-15, 15), rng.uniform(0, 90)), bevel=.03, jitter=.02))
        else:
            parts.append(rock('DarkStone', (math.cos(a) * d, math.sin(a) * d, s * .2), (s, s, s * .7), 1, .3))
    return asset('SM_Rubble', parts)


def build_cover_rock():
    parts = [rock('DarkStone', (0, 0, .75), (2.2, 2.7, 1.9), 2, .2),
             rock('DarkStone', (.9, -.9, .25), (.8, .7, .6), 1, .3),
             rock('Moss', (-.7, .9, .12), (.9, .8, .3), 1, .2)]
    return asset('SM_CoverRock', parts)


def build_cliff_rock():
    parts = []
    for i in range(4):
        h = rng.uniform(2.4, 4.2)
        parts.append(rock('DarkStone', (rng.uniform(-1, 1), rng.uniform(-.8, .8), h * .3),
                          (rng.uniform(1.4, 2.2), rng.uniform(1.3, 2), h), 2, .18,
                          rot=(rng.uniform(-8, 8), rng.uniform(-8, 8), rng.uniform(0, 90))))
    return asset('SM_CliffRock', parts)


def build_paving_tile():
    parts = []
    for i, (x, y, w, d) in enumerate(((-.27, -.42, .5, .8), (.27, -.4, .5, .78), (-.26, .42, .52, .78), (.26, .43, .5, .76))):
        parts.append(cube('Stone' if i % 3 else 'DarkStone', (x + rng.uniform(-.01, .01), y, .025),
                          (w - .03, d - .03, .05), rot=(0, 0, rng.uniform(-3, 3)), bevel=.012, jitter=.006))
    return asset('SM_PavingTile', parts)


# ---------------------------------------------------------------- landmarks
def build_altar_pillar():
    parts = [cube('DarkStone', (0, 0, .15), (1.2, 1.2, .3), bevel=.04),
             cyl('Stone', (0, 0, 1.35), .42, 2.2, 8, bevel=.02),
             cube('Stone', (0, 0, 2.6), (1.35, 1.35, .3), bevel=.04),
             cone('Stone', (0, 0, 2.9), .55, .0, .3, 4, rot=(0, 0, 45))]
    for z in (.7, 1.5, 2.25):
        parts.append(cyl('Gold', (0, 0, z), .45, .08, 8, bevel=0))
    for a in range(4):  # glowing rune slits
        ang = a * math.pi / 2
        parts.append(cube('AltarGold', (math.cos(ang) * .4, math.sin(ang) * .4, 1.1), (.06, .06, .35),
                          rot=(0, 0, a * 90), bevel=0))
    return asset('SM_AltarPillar', parts)


def build_altar_dais():
    parts = [cyl('DarkStone', (0, 0, .06), 2.65, .12, 32, bevel=.03),
             cyl('Stone', (0, 0, .15), 2.1, .08, 32, bevel=.02),
             torus('Gold', (0, 0, .2), 1.25, .05, major_seg=40, minor_seg=4, scale=(1, 1, .5)),
             cyl('AltarGold', (0, 0, .2), .45, .04, 6, bevel=0)]
    for i in range(12):
        a = i * math.tau / 12
        parts.append(cube('AltarGold', (math.cos(a) * 1.65, math.sin(a) * 1.65, .2), (.28, .08, .03),
                          rot=(0, 0, math.degrees(a) + 90), bevel=0))
    return asset('SM_AltarDais', parts)


def build_summon_circle():
    parts = [cyl('DarkStone', (0, 0, .04), 2.3, .08, 40, bevel=.02),
             torus('RuneRed', (0, 0, .085), 2.0, .035, major_seg=48, minor_seg=4, scale=(1, 1, .4)),
             torus('RuneRed', (0, 0, .085), 1.2, .03, major_seg=40, minor_seg=4, scale=(1, 1, .4))]
    # Six-point star between the rings.
    for i in range(3):
        parts.append(cube('RuneRed', (0, 0, .085), (3.4, .05, .012), rot=(0, 0, i * 60), bevel=0))
    for i in range(16):
        a = i * math.tau / 16
        parts.append(cube('RuneRed', (math.cos(a) * 1.6, math.sin(a) * 1.6, .085),
                          (.16, .05 + (i % 3) * .03, .012), rot=(0, 0, math.degrees(a) + (i % 2) * 45), bevel=0))
    for i in range(6):  # broken standing stones around the edge
        a = i * math.tau / 6 + .26
        parts.append(cube('Stone', (math.cos(a) * 2.45, math.sin(a) * 2.45, .3), (.32, .32, rng.uniform(.5, .8)),
                          rot=(rng.uniform(-6, 6), 0, math.degrees(a)), bevel=.03, jitter=.03))
    return asset('SM_SummonCircle', parts)


def build_exit_portal():
    parts = [cyl('DarkStone', (0, 0, .06), 1.75, .12, 32, bevel=.03)]
    for side in (-1, 1):
        parts.append(cube('Stone', (0, side * 1.05, 1.1), (.45, .45, 2.2), bevel=.04, jitter=.015))
    stones = 9
    for i in range(stones):  # arch voussoirs
        a = math.pi * (i + .5) / stones
        parts.append(cube('Stone', (0, math.cos(a) * 1.05, 2.2 + math.sin(a) * 1.05), (.45, .42, .34),
                          rot=(math.degrees(a) - 90, 0, 0), bevel=.03))
    parts.append(cube('Gold', (0, 0, 3.27), (.5, .28, .3), bevel=.03))  # keystone
    # Glowing membrane: a filled arch outline in the Y-Z plane.
    outline = [(-.82, 0), (.82, 0)] + [(math.cos(math.pi * i / 12) * .82, 2.2 + math.sin(math.pi * i / 12) * .82)
                                        for i in range(13)]
    parts.append(prism('PortalBlue', outline, -.03, .03, rot=(90, 0, 90)))
    return asset('SM_ExitPortal', parts)


for build in (build_sword, build_loot_bag, build_exp_crystal, build_spin_blade,
              build_column, build_column_broken, build_lintel, build_broken_wall, build_rubble,
              build_cover_rock, build_cliff_rock, build_paving_tile,
              build_altar_pillar, build_altar_dais, build_summon_circle, build_exit_portal):
    build()

# ---------------------------------------------------------------- export
for obj in ASSETS:
    bpy.ops.object.mode_set(mode='OBJECT') if bpy.context.object and bpy.context.object.mode != 'OBJECT' else None
    activate(obj)
    bpy.ops.object.shade_flat()
    bpy.ops.export_scene.fbx(filepath=os.path.join(FBX, obj.name + '.fbx'), use_selection=True,
                             object_types={'MESH'}, apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE',
                             mesh_smooth_type='FACE', use_mesh_modifiers=True, bake_space_transform=False,
                             add_leaf_bones=False, path_mode='STRIP')
    dims = obj.dimensions
    print('ASCENSION_PROP %s tris=%d size_cm=%.0fx%.0fx%.0f' % (
        obj.name, sum(len(p.vertices) - 2 for p in obj.data.polygons), dims.x * 100, dims.y * 100, dims.z * 100))


# ---------------------------------------------------------------- preview renders
def preview(names, path, cam_loc, cam_target, ortho):
    for obj in ASSETS:
        obj.hide_render = obj.name not in names
    cam = bpy.data.objects.get('PreviewCam')
    if not cam:
        cam = bpy.data.objects.new('PreviewCam', bpy.data.cameras.new('PreviewCam'))
        scene.collection.objects.link(cam)
        scene.camera = cam
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = ortho
    cam.location = cam_loc
    cam.rotation_euler = (Vector(cam_target) - Vector(cam_loc)).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


layout_big = ['SM_RuinColumn', 'SM_RuinColumnBroken', 'SM_RuinLintel', 'SM_BrokenWall', 'SM_Rubble',
              'SM_CoverRock', 'SM_CliffRock', 'SM_PavingTile', 'SM_AltarPillar', 'SM_AltarDais',
              'SM_SummonCircle', 'SM_ExitPortal']
layout_small = ['SM_Sword', 'SM_LootBag', 'SM_ExpCrystal', 'SM_SpinBlade']
for i, name in enumerate(layout_big):
    bpy.data.objects[name].location = ((i % 4) * 6.0 - 9, (i // 4) * -6.0 + 6, 0)
for i, name in enumerate(layout_small):
    bpy.data.objects[name].location = (i * 1.3 - 1.95, 60, .3 if name != 'SM_LootBag' else 0)

bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 20, -.01))
finish(bpy.context.object, 'Moss')
sun = bpy.data.objects.new('Sun', bpy.data.lights.new('Sun', 'SUN'))
sun.data.energy = 4
sun.rotation_euler = (math.radians(50), 0, math.radians(-35))
scene.collection.objects.link(sun)
world = bpy.data.worlds.new('World')
world.use_nodes = True
world.node_tree.nodes['Background'].inputs['Color'].default_value = (.15, .2, .28, 1)
world.node_tree.nodes['Background'].inputs['Strength'].default_value = .8
scene.world = world
for engine in ('BLENDER_EEVEE', 'BLENDER_EEVEE_NEXT'):
    try:
        scene.render.engine = engine
        break
    except TypeError:
        continue
scene.render.resolution_x, scene.render.resolution_y = 1600, 1000
scene.view_settings.view_transform = 'AgX' if 'AgX' in [i.identifier for i in scene.view_settings.bl_rna.properties['view_transform'].enum_items] else 'Filmic'

preview(layout_big, os.path.join(OUT, 'Preview_Environment.png'), (14, -26, 22), (0, 0, 1), 30)
preview(layout_small, os.path.join(OUT, 'Preview_Items.png'), (2.5, 55.5, 3.5), (0, 60, .3), 5.2)
for obj in ASSETS:
    obj.hide_render = False
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, 'AscensionProps.blend'))
print('ASCENSION_BLENDER_DONE %d assets -> %s' % (len(ASSETS), OUT))
