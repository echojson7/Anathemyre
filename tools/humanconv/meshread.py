"""Чтение готового .mesh (docs/mesh.md) для листа просмотра людей и проверок: вершины, индексы LOD, материалы,
веса костей, морфы и сдвиги суставов. Только numpy."""
import struct

import numpy as np

VERT = np.dtype([("p", "<f4", 3), ("nx", "<i2"), ("nz", "<i2"), ("m0", "u1"), ("m1", "u1"), ("blend", "u1"), ("ao", "u1")])


def oct_decode(nx, nz):
    # Октаэдр с осью y → единичная нормаль (как oct_decode в common.glsl)
    x, z = nx / 32767.0, nz / 32767.0
    y = 1.0 - np.abs(x) - np.abs(z)
    t = np.maximum(-y, 0.0)
    x = x + np.where(x >= 0, -t, t)
    z = z + np.where(z >= 0, -t, t)
    n = np.stack([x, y, z], 1)
    return n / np.linalg.norm(n, axis=1, keepdims=True)


class Mesh:
    pass


def _name(b, o):
    n = b[o]
    return b[o + 1:o + 1 + n].decode(), o + 1 + n


def read(path):
    data = open(path, "rb").read()
    if data[:8] != b"ANAMESH1":
        raise ValueError("%s: не .mesh" % path)
    m = Mesh()
    m.skins, m.joints, m.morph_names, m.lods, m.lodv = {}, {}, [], [], []
    o = 8
    while o < len(data):
        tag, _, size = data[o:o + 4], *struct.unpack_from("<IQ", data, o + 4)
        b = data[o + 16:o + 16 + size]
        o += 16 + size
        if tag == b"HEAD":
            m.nv, m.ni = struct.unpack_from("<II", b, 0)
        elif tag == b"MATS":
            k, q = struct.unpack_from("<I", b, 0)[0], 4
            m.materials = []
            for _ in range(k):
                s, q = _name(b, q)
                m.materials.append(s)
        elif tag == b"VERT":
            m.vert = np.frombuffer(b, VERT)
        elif tag == b"INDX":
            m.index = np.frombuffer(b, "<u4")
        elif tag == b"LODS":
            k = struct.unpack_from("<I", b, 0)[0]
            m.lods = [struct.unpack_from("<IIIIf", b, 4 + 20 * i) for i in range(k)]
        elif tag == b"LODV":
            k = struct.unpack_from("<I", b, 0)[0]
            m.lodv = list(struct.unpack_from("<%dI" % k, b, 4))
        elif tag == b"SKIN":
            name, q = _name(b, 0)
            nb = struct.unpack_from("<I", b, q)[0]
            q += 4
            bones = []
            for _ in range(nb):
                s, q = _name(b, q)
                bones.append(s)
            rec = np.frombuffer(b, np.dtype([("b", "u1", 4), ("w", "u1", 4)]), offset=q)
            m.skins[name] = (bones, rec)
        elif tag == b"MRPH":
            k, q = struct.unpack_from("<I", b, 0)[0], 4
            m.morph_step = []
            for _ in range(k):
                s, q = _name(b, q)
                m.morph_names.append(s)
                m.morph_step.append(struct.unpack_from("<f", b, q)[0])
                q += 4
            m.morph = np.frombuffer(b, np.dtype([("p", "<i2", 3), ("n", "i1", 3)]), offset=q).reshape(-1, k)
        elif tag == b"MRPJ":
            name, q = _name(b, 0)
            nb, k = struct.unpack_from("<II", b, q)
            q += 8
            out = {}
            for _ in range(k):
                s, q = _name(b, q)
                out[s] = np.frombuffer(b, "<f4", nb * 4, q).reshape(nb, 4)
                q += nb * 16
            m.joints[name] = out
    return m


def lod_triangles(m, lod=0):
    if not m.lods:
        return m.index.reshape(-1, 3)
    _, _, first, count, _ = m.lods[lod]
    return m.index[first:first + count].reshape(-1, 3)


def shaped(m, weights):
    """Вершины и нормали с морфами: weights — {имя: вес}."""
    p = m.vert["p"].astype(np.float64)
    n = oct_decode(m.vert["nx"].astype(np.float64), m.vert["nz"].astype(np.float64))
    for name, w in weights.items():
        if name not in m.morph_names:
            continue
        k = m.morph_names.index(name)
        p = p + m.morph["p"][:, k].astype(np.float64) * (w * m.morph_step[k])
        n = n + m.morph["n"][:, k].astype(np.float64) * (w / 127.0)
    return p, n / np.linalg.norm(n, axis=1, keepdims=True)
