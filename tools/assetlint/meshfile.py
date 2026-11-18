"""Чтение .mesh для линта (docs/mesh.md, docs/assetlint.md): положения, нормали, материалы, индексы, LOD."""
import struct

import numpy as np


def _oct_decode(ox, oz):
    """Октаэдр с осью y (как oct_decode в data/shaders/common.glsl) → единичные нормали."""
    px = np.maximum(ox.astype(np.float64) / 32767.0, -1.0)
    pz = np.maximum(oz.astype(np.float64) / 32767.0, -1.0)
    py = 1.0 - np.abs(px) - np.abs(pz)
    neg = py < 0
    qx = np.where(neg, (1.0 - np.abs(pz)) * np.where(px >= 0, 1.0, -1.0), px)
    qz = np.where(neg, (1.0 - np.abs(px)) * np.where(pz >= 0, 1.0, -1.0), pz)
    n = np.stack([qx, py, qz], axis=1)
    return n / np.linalg.norm(n, axis=1, keepdims=True)


class MeshFile:
    """Сетка из файла: pos (N×3 f64), nrm (N×3), mat0 (N), idx (M×3, все LOD подряд), lods [(первый индекс,
    число индексов, ошибка м)], mats [имена], skinned, boxes (коробок коллайдера)."""

    def __init__(self, path):
        d = open(path, "rb").read()
        if d[:8] != b"ANAMESH1":
            raise ValueError("%s: не .mesh" % path)
        self.path = path
        self.mats = []
        self.lods = []
        self.skinned = False
        self.boxes = 0
        i = 8
        while i < len(d):
            tag = d[i:i + 4]
            ln = struct.unpack_from("<Q", d, i + 8)[0]
            body = d[i + 16:i + 16 + ln]
            if tag == b"MATS":
                n = struct.unpack_from("<I", body)[0]
                k = 4
                for _ in range(n):
                    s = body[k]
                    self.mats.append(body[k + 1:k + 1 + s].decode())
                    k += 1 + s
            elif tag == b"VERT":
                v = np.frombuffer(body, dtype=np.dtype([("p", "<f4", 3), ("o", "<i2", 2), ("m", "u1", 4)]))
                self.pos = v["p"].astype(np.float64)
                self.nrm = _oct_decode(v["o"][:, 0], v["o"][:, 1])
                self.mat0 = v["m"][:, 0].astype(np.int64)
            elif tag == b"INDX":
                self.idx = np.frombuffer(body, dtype="<u4").astype(np.int64).reshape(-1, 3)
            elif tag == b"LODS":
                n = struct.unpack_from("<I", body)[0]
                for k in range(n):
                    _, _, fi, ni, e = struct.unpack_from("<IIIIf", body, 4 + 20 * k)
                    self.lods.append((fi, ni, e))
            elif tag == b"SKIN":
                self.skinned = True
            elif tag == b"COLL":
                self.boxes = struct.unpack_from("<I", body)[0]
            i += 16 + ln
        if not self.lods:
            self.lods = [(0, len(self.idx) * 3, 0.0)]

    def lod(self, k):
        """Треугольники LOD k: M×3 номеров вершин."""
        fi, ni, _ = self.lods[k]
        return self.idx[fi // 3:(fi + ni) // 3]
