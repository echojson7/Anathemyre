"""Позы «Вежи» для листа просмотра (docs/vezha.md): та же геометрия ног, что считает игра (src/view/mech.cpp).

Нога решается в своей плоскости как две длины: плюсна параллельна бедру (пантограф), поэтому от оси качания до
оси лапы — звено L1 + L3 и звено L2. Лапа держится подошвой вниз, поршень смотрит концами на свои шарниры.
"""
import math

import numpy as np

from kit import rot_y, rot_z
from vezha import BELLY, LEGS
from vezha_leg import A1, A2, FOOT_H, HIP_Y, L1, L2, L3, THIGH_MAX, THIGH_MIN, U_PITCH

LV = L1 + L3
PACK_YAW = math.radians(-135.0)   # пачка лежит вдоль стороны рамы, поршнем наружу

# Позы: высота осей бёдер; на ногу — поворот бедра от диагонали и вынос оси лапы от оси поворота
# (None — нога сложена пачкой)
POSES = {
    "bind": (HIP_Y, 0.0, U_PITCH + (LV - L2) * math.cos(A1)),
    "stand": (0.90, 0.0, U_PITCH + 0.40),
    "high": (1.22, 0.0, U_PITCH + 0.22),
    "low": (0.44, 0.0, U_PITCH + 0.62),
    "sit": (BELLY, 0.0, U_PITCH + 0.56),
    "pack": (BELLY, PACK_YAW, None),
}


def leg_angles(x, y):
    """Углы бедра и голени к горизонту для оси лапы в точке (x, y) от оси качания. Плюсна — как бедро."""
    d = min(max(math.hypot(x, y), LV - L2 + 1e-4), LV + L2 - 1e-4)
    base = math.atan2(y, x)
    a = base + math.acos(max(-1.0, min(1.0, (LV * LV + d * d - L2 * L2) / (2 * LV * d))))
    a = min(max(a, THIGH_MIN), THIGH_MAX)
    s = math.atan2(y - LV * math.sin(a), x - LV * math.cos(a))
    return a, s


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
    h, yaw, reach = POSES[name]
    rot = {"hull": np.asarray(rot_y(hull)), "gun": np.asarray(rot_x_(-gun)), "eye": np.asarray(rot_y(eye))}
    trans = {"chassis": (0.0, h, 0.0)}
    if reach is None:
        a, s, foot = 0.0, -math.pi + 0.002, 0.0
    else:
        a, s = leg_angles(reach - U_PITCH, FOOT_H - h)
        foot = 0.0
    for leg, deg in LEGS:
        M = np.asarray(rot_y(math.radians(deg)))
        loc = lambda ang: M @ np.asarray(rot_z(ang)) @ M.T
        rot["hip_" + leg] = np.asarray(rot_y(yaw))
        rot["thigh_" + leg] = loc(a - A1)
        rot["shin_" + leg] = loc((s - A2) - (a - A1))
        rot["tarsus_" + leg] = loc((a - A1) - (s - A2))   # плюсна возвращается к углу бедра
        rot["foot_" + leg] = loc(foot - (a - A1))
    # поршень: конец смотрит на свой шарнир
    mats = R.pose(rig, rot, trans)
    ix = {b[0]: i for i, b in enumerate(rig.bones)}
    for leg, _deg in LEGS:
        i, j = ix["pa_" + leg], ix["pb_" + leg]
        d0 = np.asarray(rig.bones[j][2]) - np.asarray(rig.bones[i][2])
        d0 /= np.linalg.norm(d0)
        d = mats[j][1] - mats[i][1]
        d /= np.linalg.norm(d)
        for bone, sign in ((i, 1.0), (j, -1.0)):
            Rp = mats[bone][0]  # поворот родителя (свой местный ещё единичный)
            rot[rig.bones[bone][0]] = _between(sign * d0, Rp.T @ (sign * d))
    return rot, trans
