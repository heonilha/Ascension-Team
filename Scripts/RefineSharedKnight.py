# Meshy 캐릭터 FBX 정제 (Blender 5.x, 화면 없이 실행)
#   blender -b --factory-startup --python Scripts/RefineSharedKnight.py -- <입력.fbx> <출력.fbx> [세분화 단계=1]
# 형태·UV·본 가중치를 보존하면서 UV 경계로 갈라진 정점을 합치고 Catmull-Clark 세분화로 전체를 쿼드로 만든다.
# 삼각형을 먼저 쿼드로 묶으면 면 수는 줄지만 텍스처 무늬가 밀려서 묶지 않는다(2026-11-14 시험).
# 본 이름·계층·기준 자세와 UV 섬 배치는 바꾸지 않으므로 기존 텍스처와 애니메이션을 그대로 쓴다.
import bpy, bmesh, sys, math
from collections import Counter
a = sys.argv[sys.argv.index('--') + 1:]
src, dst = a[0], a[1]
levels = int(a[2]) if len(a) > 2 else 1
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=src)
ob = [o for o in bpy.data.objects if o.type == 'MESH'][0]
arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
me = ob.data
def activate(o):
    bpy.context.view_layer.objects.active = o
    for x in bpy.data.objects: x.select_set(x == o)
def report(tag):
    bm = bmesh.new(); bm.from_mesh(me)
    print('REFINE', tag, 'verts', len(bm.verts), 'faces', dict(Counter(len(f.verts) for f in bm.faces)), 'tris', sum(len(f.verts) - 2 for f in bm.faces), 'boundary', sum(e.is_boundary for e in bm.edges))
    bm.free()
activate(ob); report('input')
# 1. 가져온 분할 노멀을 버리고 UV 경계에서 갈라진 정점을 합친다(UV는 면 모서리 단위라 유지된다).
if me.has_custom_normals: bpy.ops.mesh.customdata_custom_splitnormals_clear()
for c in list(me.color_attributes): me.color_attributes.remove(c)
bm = bmesh.new(); bm.from_mesh(me)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
bm.to_mesh(me); bm.free(); report('welded')
# 2. 세분화: 모든 면이 쿼드가 되고 각진 실루엣이 부드러워진다. UV는 원래 삼각형 안에서 선형 보간해 무늬 위치를 유지한다.
if levels > 0:
    md = ob.modifiers.new('Subdiv', 'SUBSURF'); md.levels = levels; md.subdivision_type = 'CATMULL_CLARK'
    md.uv_smooth = 'NONE'; md.boundary_smooth = 'PRESERVE_CORNERS'
    while ob.modifiers[0].name != 'Subdiv': bpy.ops.object.modifier_move_up(modifier='Subdiv')
    bpy.ops.object.modifier_apply(modifier='Subdiv')
for p in me.polygons: p.use_smooth = True
# 3. 가중치: 정점당 최대 4개 본, 합계 1로 정규화.
bpy.ops.object.vertex_group_limit_total(group_select_mode='ALL', limit=4)
bpy.ops.object.vertex_group_normalize_all(group_select_mode='ALL', lock_active=False)
report('final')
print('REFINE influences', sorted(Counter(len([g for g in v.groups if g.weight > 1e-4]) for v in me.vertices).items()))
bb = [ob.matrix_world @ v.co for v in me.vertices]
print('REFINE bbox', [round(min(p[i] for p in bb), 3) for i in range(3)], [round(max(p[i] for p in bb), 3) for i in range(3)])
# 4. 내보내기: 언리얼이 만든 최상위 빈 노드를 없애고 뼈대 이름을 Armature로 두어 루트 본이 추가되지 않게 한다.
root = arm.parent
if root:
    for o in (arm,):
        mw = o.matrix_world.copy(); o.parent = None; o.matrix_world = mw
    bpy.data.objects.remove(root)
arm.name = 'Armature'; ob.name = 'SharedKnightRig'; me.name = 'SharedKnightRig'
for o in bpy.data.objects: o.select_set(o in (arm, ob))
bpy.context.view_layer.objects.active = arm
bpy.ops.export_scene.fbx(filepath=dst, use_selection=True, object_types={'ARMATURE', 'MESH'}, add_leaf_bones=False, bake_anim=False,
                         mesh_smooth_type='FACE', use_armature_deform_only=False, apply_scale_options='FBX_SCALE_NONE',
                         primary_bone_axis='Y', secondary_bone_axis='X', use_tspace=True, use_triangles=False)
# 기존 스켈레톤은 'Armature'를 루트 본으로 쓴다(25본). 언리얼은 Blender가 만든 FBX에서만 이 노드를 지우므로
# 제작 도구 표기를 같은 길이의 다른 글자로 바꿔 루트 본이 유지되게 한다. 형상 데이터는 건드리지 않는다.
raw = open(dst, 'rb').read(); n = raw.count(b'Blender')
open(dst, 'wb').write(raw.replace(b'Blender', b'B1ender'))
print('REFINE exported', dst, 'creator tags patched', n)
