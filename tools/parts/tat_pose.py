"""Позы «Тати» для листа просмотра (docs/tat.md): та же геометрия ног, что считает игра (src/view/mech.cpp).

Нога решается в своей плоскости двумя длинами до голеностопа (tat_leg.solve), плюсна держит угол бинда, ступня
подошвой вниз, цилиндры колена и голеностопа наводятся друг на друга (как в vezha_pose).
"""
import math

import numpy as np

from kit import rot_y, rot_z
from tat import BELLY
from tat_leg import LEGS, solve
from vezha_pose import _between, rot_x_

# Позы: высота осей бёдер; на ногу — поворот бедра от бинда (град) и вынос оси лапы от оси качания
_all = lambda yaw_f, yaw_r, reach_f, reach_r: {"fr": (-yaw_f, reach_f), "fl": (yaw_f, reach_f), "rl": (-yaw_r, reach_r),
                                               "rr": (yaw_r, reach_r)}
POSES = {
    "stand": (0.62, _all(0.0, 0.0, 0.38, 0.38)),
    "high": (0.74, _all(0.0, 0.0, 0.36, 0.36)),
    "low": (0.50, _all(0.0, 0.0, 0.44, 0.44)),
    "prone": (0.39, _all(20.0, 17.0, 0.70, 0.66)),   # TAT_PRONE в src/view/tat_move.cpp
    "pack": (BELLY, _all(0.0, 0.0, 0.32, 0.32)),
}


def pose(rig, name, hull=0.0, gun=0.0, eye=0.0):
    import rig as R
    h, legs = POSES[name]
    rot = {"turret": np.asarray(rot_y(hull)), "gun": np.asarray(rot_x_(-gun)), "eye": np.asarray(rot_y(eye))}
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
        for pa, pb in (("pa_", "pb_"), ("ka_", "kb_")):
            i, j = ix[pa + leg.name], ix[pb + leg.name]
            d0 = np.asarray(rig.bones[j][2]) - np.asarray(rig.bones[i][2])
            d0 /= np.linalg.norm(d0)
            dd = mats[j][1] - mats[i][1]
            dd /= np.linalg.norm(dd)
            for bone, sign in ((i, 1.0), (j, -1.0)):
                Rp = mats[bone][0]
                rot[rig.bones[bone][0]] = _between(sign * d0, Rp.T @ (sign * dd))
    return rot, trans
