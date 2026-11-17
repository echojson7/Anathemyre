"""Позы «Тати» для листа просмотра (docs/tat.md): та же геометрия ног, что считает игра (src/view/mech.cpp).

Нога решается в своей плоскости двумя длинами до голеностопа (tat_leg.solve), плюсна отвесна, лапа подошвой вниз.
"""
import math

import numpy as np

from kit import rot_y, rot_z
from tat import BELLY
from tat_leg import LEGS, solve
from vezha_pose import rot_x_

# Позы: высота осей бёдер; на ногу — поворот бедра от бинда (град) и вынос оси лапы от оси качания
_all = lambda yaw_f, yaw_r, reach_f, reach_r: {"fr": (-yaw_f, reach_f), "fl": (yaw_f, reach_f), "rl": (-yaw_r, reach_r),
                                               "rr": (yaw_r, reach_r)}
POSES = {
    "stand": (0.62, _all(0.0, 0.0, 0.44, 0.44)),
    "high": (0.74, _all(0.0, 0.0, 0.40, 0.40)),
    "low": (0.50, _all(0.0, 0.0, 0.50, 0.50)),
    "prone": (0.39, _all(20.0, 17.0, 0.70, 0.66)),   # TAT_PRONE в src/view/tat_move.cpp
    "pack": (BELLY, _all(0.0, 0.0, 0.32, 0.32)),
}


def pose(rig, name, hull=0.0, gun=0.0, eye=0.0):
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
    return rot, trans
