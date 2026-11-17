"""Позы «Вежи» для листа просмотра (docs/vezha.md): та же геометрия ног, что считает игра (src/view/mech.cpp).

Задний рычаг (vezha_leg.py) решается в своей плоскости как две длины: плюсна параллельна бедру (пантограф), поэтому
от оси качания до оси лапы — звено L1 + L3 и звено L2, колено вверх. Передняя стойка (vezha_strut.py) — две длины
S1 и S2, локоть назад (колено вниз), шток идёт по оси голени. Лапа держится подошвой вниз, поршень смотрит концами на
свои шарниры. Оси сгиба — поперёк ноги (направление ноги × вверх): так и у зеркальных ног угол тот же.
"""
import math

import numpy as np

from kit import rot_y
from vezha import BELLY, LEGS
from vezha_leg import FOOT_H, L1, L2, L3, THIGH_MAX, THIGH_MIN, U_PITCH
from vezha_strut import S1, S2, U_S

LV = L1 + L3

# Позы: высота осей бёдер и на вид ноги — вынос оси лапы от оси качания (None — сложена)
POSES = {
    "stand": (0.86, {"lever": 0.40, "strut": 0.34}),
    "high": (1.16, {"lever": 0.24, "strut": 0.26}),
    "low": (0.50, {"lever": 0.56, "strut": 0.36}),
    "pack": (BELLY, {"lever": None, "strut": 0.25}),
}


def two_link(a_len, b_len, x, y, bend):
    """Углы первого и второго звена к горизонту для конца в (x, y); bend 1 — колено вверх, −1 — вниз."""
    d = min(max(math.hypot(x, y), abs(a_len - b_len) + 1e-4), a_len + b_len - 1e-4)
    a = math.atan2(y, x) + bend * math.acos(max(-1.0, min(1.0, (a_len * a_len + d * d - b_len * b_len) / (2 * a_len * d))))
    return a, math.atan2(y - a_len * math.sin(a), x - a_len * math.cos(a))


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


def pack_yaw(d, kind):
    """Поворот бедра в пачку: рычаг ложится прямо назад, стойка смотрит прямо вперёд (плечо — вдоль борта)."""
    t = (0.0, 0.0, -1.0) if kind == "lever" else (0.0, 0.0, 1.0)
    a = math.atan2(-t[2], t[0]) - math.atan2(-d[2], d[0])
    return (a + math.pi) % (2 * math.pi) - math.pi


def pose(rig, name, hull=0.0, gun=0.0, eye=0.0):
    import rig as R
    h, reach = POSES[name]
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
        if kind == "lever":
            a1 = math.atan2(*reversed(flat(K - P)))
            a2 = math.atan2(*reversed(flat(A - K)))
            if reach[kind] is None:
                yaw, a, s = pack_yaw(d, kind), 0.0, -math.pi + 0.002
            else:
                yaw = 0.0
                a, s = two_link(LV, L2, reach[kind], FOOT_H - h, 1.0)
                a = min(max(a, THIGH_MIN), THIGH_MAX)
                s = math.atan2(FOOT_H - h - LV * math.sin(a), reach[kind] - LV * math.cos(a))
            rot["thigh_" + leg] = _axis(z, a - a1)
            rot["shin_" + leg] = _axis(z, (s - a2) - (a - a1))
            rot["tarsus_" + leg] = _axis(z, (a - a1) - (s - a2))   # плюсна возвращается к углу бедра
            rot["foot_" + leg] = _axis(z, -(a - a1))
        else:
            a1 = math.atan2(*reversed(flat(K - P)))
            a2 = math.atan2(*reversed(flat(F - K)))
            yaw = pack_yaw(d, kind) if name == "pack" else 0.0
            a, s = two_link(S1, S2, reach[kind], FOOT_H - h, -1.0)
            rot["thigh_" + leg] = _axis(z, a - a1)
            rot["shin_" + leg] = _axis(z, (s - a2) - (a - a1))
            rot["foot_" + leg] = _axis(z, -(s - a2))
        rot["hip_" + leg] = np.asarray(rot_y(yaw))
    # поршень: конец смотрит на свой шарнир
    mats = R.pose(rig, rot, trans)
    ix = {b[0]: i for i, b in enumerate(rig.bones)}
    for leg, kind, *_ in LEGS:
        if "pa_" + leg not in ix:
            continue
        i, j = ix["pa_" + leg], ix["pb_" + leg]
        d0 = np.asarray(rig.bones[j][2]) - np.asarray(rig.bones[i][2])
        d0 /= np.linalg.norm(d0)
        dd = mats[j][1] - mats[i][1]
        dd /= np.linalg.norm(dd)
        for bone, sign in ((i, 1.0), (j, -1.0)):
            Rp = mats[bone][0]  # поворот родителя (свой местный ещё единичный)
            rot[rig.bones[bone][0]] = _between(sign * d0, Rp.T @ (sign * dd))
    return rot, trans
