"""Позы «Вежи» для листа просмотра (docs/vezha.md): та же геометрия ног, что считает игра (src/view/mech.cpp).

Нога-паук (vezha_leg.py) из трёх звеньев: угол плюсны задан высотой тела (meta_at), бедро и голень решаются двумя
длинами до скакательного сустава, колено вверх; ступня — подошвой вниз. Цилиндры смотрят концами на свои проушины.
Оси сгиба — поперёк ноги (направление ноги × вверх): так и у зеркальных ног угол тот же.
"""
import math

import numpy as np

from kit import rot_y
from vezha import BELLY, LEGS
from vezha_leg import solve

# Позы: высота осей бёдер, вынос оси кардана от оси качания и поворот ног к оси машины (лёжа — к носу и корме)
POSES = {
    "stand": (0.82, 1.0, 0.0),
    "high": (1.0, 0.95, 0.0),
    "low": (0.58, 1.05, 0.0),
    "pack": (BELLY, 1.1, math.radians(20.0)),
}


def _axis(ax, ang):
    ax = np.asarray(ax, dtype=np.float64)
    ax = ax / np.linalg.norm(ax)
    k = np.array([[0, -ax[2], ax[1]], [ax[2], 0, -ax[0]], [-ax[1], ax[0], 0]])
    return np.eye(3) + math.sin(ang) * k + (1 - math.cos(ang)) * k @ k


def _between(a, b):
    """Матрица наименьшего поворота единичного a в единичный b."""
    v = np.cross(a, b)
    c = float(np.dot(a, b))
    if c < -0.9999:
        return -np.eye(3)
    vx = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + vx + vx @ vx / (1.0 + c)


def rot_x_(a):
    c, s = math.cos(a), math.sin(a)
    return ((1.0, 0.0, 0.0), (0.0, c, -s), (0.0, s, c))


def pose(rig, name, hull=0.0, gun=0.0, eye=0.0):
    import rig as R
    h, reach, fold = POSES[name]
    rot = {"hull": np.asarray(rot_y(hull)), "gun": np.asarray(rot_x_(-gun)), "eye": np.asarray(rot_y(eye))}
    trans = {"chassis": (0.0, h, 0.0)}
    head = lambda b: np.asarray(rig.head(b))
    for leg, kind, *_ in LEGS:
        d = head("thigh_" + leg) - head("hip_" + leg)
        d[1] = 0.0
        d /= np.linalg.norm(d)
        z = np.cross(d, (0.0, 1.0, 0.0))
        P, K, A, F = (head(b + "_" + leg) for b in ("thigh", "shin", "tarsus", "foot"))
        flat = lambda v: (float(np.dot(v, d)), float(v[1]))
        a1 = math.atan2(*reversed(flat(K - P)))
        a2 = math.atan2(*reversed(flat(A - K)))
        a3 = math.atan2(*reversed(flat(F - A)))
        a, s, m = solve(kind, h, reach)
        rot["thigh_" + leg] = _axis(z, a - a1)
        rot["shin_" + leg] = _axis(z, (s - a2) - (a - a1))
        rot["tarsus_" + leg] = _axis(z, (m - a3) - (s - a2))
        rot["foot_" + leg] = _axis(z, -(m - a3))   # ступня снова подошвой вниз
        # к оси машины: передние — к носу, задние — к корме (поворот вокруг y по знаку угла ноги)
        side = 1.0 if (d[0] > 0) == (d[2] > 0) else -1.0
        rot["hip_" + leg] = np.asarray(rot_y(-side * fold))
    # цилиндры: гильза и шток смотрят друг на друга
    mats = R.pose(rig, rot, trans)
    ix = {b[0]: i for i, b in enumerate(rig.bones)}
    for leg, *_ in LEGS:
        for pa, pb in (("pa_", "pb_"), ("ka_", "kb_"), ("ma_", "mb_")):
            i, j = ix[pa + leg], ix[pb + leg]
            d0 = np.asarray(rig.bones[j][2]) - np.asarray(rig.bones[i][2])
            d0 /= np.linalg.norm(d0)
            dd = mats[j][1] - mats[i][1]
            dd /= np.linalg.norm(dd)
            for bone, sign in ((i, 1.0), (j, -1.0)):
                Rp = mats[bone][0]  # поворот родителя (свой местный ещё единичный)
                rot[rig.bones[bone][0]] = _between(sign * d0, Rp.T @ (sign * dd))
    return rot, trans
