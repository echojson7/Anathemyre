"""Цельное тело булевыми операциями Blender (docs/parts.md): глыба минус пустоты плюс лестница минус сколы.

Нужен Blender как модуль Python (`pip install bpy`, как у ретаргета — docs/anim.md). Решатель — MANIFOLD: он даёт
одни и те же координаты от запуска к запуску (EXACT — нет, замер 2026-11-15), порядок вершин выравнивает export.py.
Результат всегда замкнут: щель между стеной и лестницей невозможна, потому что это одна поверхность.
"""
import math

import bpy

from kit import Body

_ready = False


def _object(name, body):
    """Тело → объект Blender (вершины сварены по положению)."""
    index = {}
    verts = []
    faces = []
    for pts, _, _ in body.faces:
        f = []
        for p in pts:
            k = index.get(p)
            if k is None:
                k = index[p] = len(verts)
                verts.append(p)
            f.append(k)
        faces.append(f)
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    return bpy.data.objects.new(name, me)


def solid(ops, bevel=0.012, bevel_angle=50.0, segments=1):
    """ops — [("add" | "cut", Body), …] по порядку; первое — "add". Подряд идущие одинаковые операции идут одной
    коллекцией. bevel — фаска на всех рёбрах круче bevel_angle градусов (0 — без фаски), segments — сегментов
    фаски (больше одного — скругление дугой).
    Возвращает Body из треугольников (материал None — его ставит вызывающий по месту и нормали грани)."""
    global _ready
    if not _ready:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        _ready = True
    assert ops and ops[0][0] == "add"
    base = _object("solid", ops[0][1])
    bpy.context.scene.collection.objects.link(base)
    k = 1
    g = 0
    while k < len(ops):
        op = ops[k][0]
        coll = bpy.data.collections.new("g%d" % g)
        while k < len(ops) and ops[k][0] == op:
            coll.objects.link(_object("o%d" % k, ops[k][1]))
            k += 1
        m = base.modifiers.new("b%d" % g, 'BOOLEAN')
        m.operand_type = 'COLLECTION'
        m.collection = coll
        m.operation = 'UNION' if op == "add" else 'DIFFERENCE'
        m.solver = 'MANIFOLD'
        g += 1
    if bevel > 0:
        bv = base.modifiers.new("bevel", 'BEVEL')
        bv.width = bevel
        bv.segments = segments
        bv.limit_method = 'ANGLE'
        bv.angle_limit = math.radians(bevel_angle)
    me = bpy.data.meshes.new_from_object(base.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    me.calc_loop_triangles()
    out = Body()
    vs = [tuple(v.co) for v in me.vertices]
    for t in me.loop_triangles:
        a, b, c = t.vertices
        out.faces.append(((vs[a], vs[b], vs[c]), None, True))
    # Убрать за собой: следующий solid() начинает с пустой сцены
    bpy.ops.wm.read_factory_settings(use_empty=True)
    return out
