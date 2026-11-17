"""Позы «Тати» для листа просмотра (docs/tat.md): та же геометрия ног, что считает игра (src/view/mech.cpp).

Нога решается в своей плоскости двумя длинами (tat_leg.solve): плюсна «par» параллельна бедру, «vert» — отвесна.
Лапа держится подошвой вниз, поршень смотрит концами на свои шарниры.
"""
import math

import numpy as np

from kit import rot_y, rot_z
from tat import BELLY
from tat_leg import LEGS, solve
from vezha_pose import _between, rot_x_

# Позы: высота осей бёдер; на ногу — поворот бедра от бинда (град) и вынос оси лапы от оси качания; мачта (0 —
# поднята, 1 — сложена), сошки (0 — вдоль ствола, 1 — вниз)
STAND = {"fl": (0.0, 0.50), "fr": (0.0, 0.42), "rl": (0.0, 0.40), "rr": (0.0, 0.40)}
POSES = {
    "stand": (0.62, STAND, 0.0, 0.0),
    "high": (0.80, {"fl": (0.0, 0.36), "fr": (0.0, 0.30), "rl": (0.0, 0.28), "rr": (0.0, 0.30)}, 0.0, 0.0),
    "low": (0.44, {"fl": (0.0, 0.62), "fr": (0.0, 0.50), "rl": (0.0, 0.48), "rr": (0.0, 0.50)}, 0.0, 0.0),
    "prone": (0.19, {"fl": (-30.0, 0.66), "fr": (-28.0, 0.77), "rl": (12.0, 0.52), "rr": (18.0, 0.91)}, 0.0, 1.0),
    "pack": (BELLY, {"fl": (62.0, 0.30), "fr": (-58.0, 0.22), "rl": (-55.0, 0.30), "rr": (52.0, 0.24)}, 1.0, 0.0),
}
MAST_FOLD = 1.30   # рад: мачта ложится вперёд на палубу, прибор — лицом вниз
BIPOD_OPEN = 1.35  # рад: сошки вниз


def pose(rig, name, hull=0.0, gun=0.0, eye=0.0):
    import rig as R
    h, legs, mast, bip = POSES[name]
    rot = {"turret": np.asarray(rot_y(hull)), "gun": np.asarray(rot_x_(-gun)), "eye": np.asarray(rot_y(eye)),
           "mast": np.asarray(rot_x_(MAST_FOLD * mast)), "bipod": np.asarray(rot_x_(BIPOD_OPEN * bip))}
    trans = {"chassis": (0.0, h, 0.0)}
    for leg in LEGS:
        yaw, reach = legs[leg.name]
        a, s, t = solve(leg, reach, leg.foot_h - h)
        M = np.asarray(rot_y(math.radians(leg.deg)))
        loc = lambda ang: M @ np.asarray(rot_z(ang)) @ M.T
        n = leg.name
        rot["hip_" + n] = np.asarray(rot_y(math.radians(yaw)))
        rot["thigh_" + n] = loc(a - leg.a1)
        rot["shin_" + n] = loc((s - leg.a2) - (a - leg.a1))
        rot["tarsus_" + n] = loc((t - leg.a3) - (s - leg.a2))
        rot["foot_" + n] = loc(-(t - leg.a3))
    mats = R.pose(rig, rot, trans)
    ix = {b[0]: i for i, b in enumerate(rig.bones)}
    for leg in LEGS:
        if "pa_" + leg.name not in ix:
            continue
        i, j = ix["pa_" + leg.name], ix["pb_" + leg.name]
        d0 = np.asarray(rig.bones[j][2]) - np.asarray(rig.bones[i][2])
        d0 /= np.linalg.norm(d0)
        d = mats[j][1] - mats[i][1]
        d /= np.linalg.norm(d)
        for bone, sign in ((i, 1.0), (j, -1.0)):
            Rp = mats[bone][0]
            rot[rig.bones[bone][0]] = _between(sign * d0, Rp.T @ (sign * d))
    return rot, trans
