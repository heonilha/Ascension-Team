# 기본색 텍스처에서 노멀·러프니스 맵을 만든다 (Blender 내장 numpy 사용)
#   blender -b --factory-startup --python Scripts/MakeSurfaceMaps.py -- <BaseColor.png> <Normal.png> <Roughness.png> [강도=2.0]
import bpy, sys, numpy as np
a = sys.argv[sys.argv.index('--') + 1:]
strength = float(a[3]) if len(a) > 3 else 2.0
img = bpy.data.images.load(a[0]); w, h = img.size
px = np.empty(w * h * 4, dtype=np.float32); img.pixels.foreach_get(px); px = px.reshape(h, w, 4)
rgb = px[..., :3]                       # sRGB 값 그대로 사용 (밝기 인상 기준)
lum = rgb @ np.array([0.299, 0.587, 0.114], dtype=np.float32)
def blur(x, r):
    k = np.exp(-0.5 * (np.arange(-3 * r, 3 * r + 1) / r) ** 2).astype(np.float32); k /= k.sum()
    x = np.apply_along_axis(lambda v: np.convolve(np.pad(v, len(k) // 2, mode='wrap'), k, mode='valid'), 0, x)
    return np.apply_along_axis(lambda v: np.convolve(np.pad(v, len(k) // 2, mode='wrap'), k, mode='valid'), 1, x)
fine = blur(lum, 1.0); broad = blur(lum, 12.0)
height = fine - broad                   # 넓은 음영(구워진 조명)은 빼고 잔무늬만 높이로 쓴다
gx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * 0.5
gy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * 0.5
scale = strength * 8.0
n = np.dstack([-gx * scale, -gy * scale, np.ones_like(height)]); n /= np.linalg.norm(n, axis=2, keepdims=True)
out = np.ones((h, w, 4), dtype=np.float32); out[..., :3] = n * 0.5 + 0.5
def save(arr, path, name):
    im = bpy.data.images.new(name, w, h, alpha=False, is_data=True)
    im.pixels.foreach_set(arr.astype(np.float32).ravel()); im.filepath_raw = path; im.file_format = 'PNG'; im.save()
save(out, a[1], 'Normal')
# 러프니스: 밝고 채도 낮은 금속 장식은 매끈하게, 어두운 천은 거칠게
sat = rgb.max(2) - rgb.min(2)
rough = np.clip(0.82 - 0.9 * np.clip(height, -0.15, 0.15) - 0.35 * np.clip(lum - 0.45, 0, 1) * (1 - np.clip(sat * 4, 0, 1)), 0.35, 0.95)
r4 = np.ones((h, w, 4), dtype=np.float32); r4[..., :3] = blur(rough, 1.0)[..., None]
save(r4, a[2], 'Roughness')
print('MAPS done', w, h, 'rough range', float(rough.min()), float(rough.max()), 'mean', float(rough.mean()))
