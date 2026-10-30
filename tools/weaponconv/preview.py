# Проверка выгрузки: читает .mesh оружия и ставит их на один лист, как их увидит игра (оси игры: острие +Y, лезвие +Z)
# blender -b -P preview.py -- out.png папка id1 id2 ...
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlib import *

a = sys.argv[sys.argv.index('--') + 1:]
OUT, DIR, IDS = a[0], a[1], a[2:]
reset()
mats = {}
def mat_for(code, rgb):
    k = (code, rgb)
    if k not in mats:
        mats[k] = (mat_steel('m%d' % len(mats), tuple(c / 255 * 0.5 for c in rgb), 0.45, 0.4) if code in (8, 15, 1)
                   else mat_plain('m%d' % len(mats), tuple((c / 255) ** 1.6 for c in rgb), 0.7))
    return mats[k]
z = 0.0
for wid in IDS:
    data = open(os.path.join(DIR, wid + '.mesh'), 'rb').read()
    _, nv, ni = struct.unpack_from('<3I', data, 4)
    V = [struct.unpack_from('<6f8B', data, 16 + 32 * i) for i in range(nv)]
    I = struct.unpack_from('<%dI' % ni, data, 16 + 32 * nv)
    me = bpy.data.meshes.new(wid)
    # обратно в оси листа: x = y игры, z = -z игры, y = x игры
    me.from_pydata([(v[1], v[0], -v[2]) for v in V], [], [I[i:i + 3] for i in range(0, ni, 3)])
    keys = []
    for p in me.polygons:
        v = V[I[p.index * 3]]
        k = (v[10], (v[6], v[7], v[8]))
        if k not in keys: keys.append(k); me.materials.append(mat_for(*k))
        p.material_index = keys.index(k)
        p.use_smooth = True
    me.normals_split_custom_set_from_vertices([(v[4], v[3], -v[5]) for v in V]) if hasattr(me, 'normals_split_custom_set_from_vertices') else None
    if hasattr(me, 'use_auto_smooth'): me.use_auto_smooth = True
    ob = bpy.data.objects.new(wid, me); bpy.context.scene.collection.objects.link(ob)
    ob.location = (-1.0, 0, z)
    z -= 0.3
stage(0.2, z / 2 + 0.15, 3.0, (1500, int(1500 * (-z + 0.3) / 3.0)))
render(OUT, 32)
