"""Позы «Гриди» для листа просмотра (docs/gryd.md): та же геометрия, что считает игра (src/view/mech.cpp).

Нога решается в своей плоскости двумя длинами до голеностопа (gryd_leg.solve), плюсна держит угол бинда, ступня
подошвой по земле. Руки: запястья — там, где их держит винтовка (кисть повёрнута как винтовка), локоть — решение
двумя длинами к своему полюсу; цилиндры наводятся друг на друга.
"""
import math

import numpy as np

from kit import rot_x, rot_y, rot_z
from gryd_leg import HIP_Y, LEGS, U, solve
from vezha_pose import _between

STOW = (-0.45, -0.8)    # винтовка по-походному: поворот внутрь и вниз, рад (GRYD_STOW_* в src/view/gryd_move.cpp)

# Позы: высота осей бёдер, наклон таза вперёд (рад), ступни (вперёд от оси качания, подъём) правой и левой,
# поворот винтовки (внутрь, вверх), поворот корпуса
POSES = {
    "stand": (1.16, 0.0, ((0.07, 0.0), (0.07, 0.0)), (0.0, 0.0), 0.0),
    "aim": (0.90, 0.10, ((-0.46, 0.0), (0.30, 0.0)), (0.0, 0.03), 0.18),
    "walk": (1.10, 0.04, ((0.26, 0.0), (-0.18, 0.12)), (-0.15, -0.3), 0.0),
    "rest": (0.84, 0.05, ((0.05, 0.0), (0.05, 0.0)), STOW, 0.0),
    "low": (1.16, 0.0, ((0.07, 0.0), (0.07, 0.0)), STOW, 0.0),
}


def gun_rot(yaw, pitch):
    """Местный поворот винтовки на корпусе: внутрь (вокруг y), потом возвышение (вокруг x)."""
    return np.asarray(rot_y(yaw)) @ np.asarray(rot_x(-pitch))


def pole0(S0, E0, W0):
    """Полюс локтя в бинде, как в игре (mech_bind_skel): на продолжении локтя от прямой плечо — запястье."""
    x = (W0 - S0) / np.linalg.norm(W0 - S0)
    off = (E0 - S0) - x * np.dot(E0 - S0, x)
    return E0 + off / np.linalg.norm(off) * 0.3


def _frame(x, n):
    x = x / np.linalg.norm(x)
    n = n / np.linalg.norm(n)
    y = np.cross(n, x)
    return np.stack([x, y, n], axis=1)


def arms(rig, rot, trans, out=None):
    """Дописать в rot повороты костей рук: кисти — как винтовка, локоть — решение двумя длинами к полюсу."""
    import rig as R
    import gryd
    import gryd_arm as arm
    A = gryd.arm_points()
    mats = R.pose(rig, rot, trans)
    ix = {b[0]: i for i, b in enumerate(rig.bones)}
    Rh, _ = mats[ix["hull"]]
    Rg, pg = mats[ix["gun"]]
    for n, a in A.items():
        S0, E0, W0 = (np.asarray(a[k]) for k in ("S", "E", "W"))
        _, S = mats[ix["ua_" + n]]
        W = pg + Rg @ (W0 - np.asarray(gryd.GUN))
        pole = S + Rh @ (pole0(S0, E0, W0) - S0)
        E = np.asarray(arm.arm_ik(S, W, pole))
        n0 = np.cross(E0 - S0, W0 - E0)
        n1 = np.cross(E - S, W - E)
        Ru = _frame(E - S, n1) @ _frame(E0 - S0, n0).T
        Rf = _frame(W - E, n1) @ _frame(W0 - E0, n0).T
        rot["ua_" + n] = Rh.T @ Ru
        rot["fa_" + n] = Ru.T @ Rf
        rot["hand_" + n] = Rf.T @ Rg
        if out is not None:
            out[n] = (S, E, W)
    return rot


def cylinders(rig, rot, trans, pairs):
    import rig as R
    mats = R.pose(rig, rot, trans)
    ix = {b[0]: i for i, b in enumerate(rig.bones)}
    for pa, pb in pairs:
        i, j = ix[pa], ix[pb]
        d0 = np.asarray(rig.bones[j][2]) - np.asarray(rig.bones[i][2])
        d0 /= np.linalg.norm(d0)
        dd = mats[j][1] - mats[i][1]
        dd /= np.linalg.norm(dd)
        for bone, sign in ((i, 1.0), (j, -1.0)):
            Rp = mats[bone][0]
            rot[rig.bones[bone][0]] = _between(sign * d0, Rp.T @ (sign * dd))


def pose(rig, name, hull=None, gun=None, eye=0.0):
    import rig as R
    h, pitch, feet, g, hy = POSES[name]
    if gun:   # 0 и None — как в позе
        g = (g[0], gun)
    hy = hull if hull else hy
    Rc = np.asarray(rot_x(pitch))
    rot = {"chassis": Rc, "hull": np.asarray(rot_y(hy)), "gun": gun_rot(*g), "eye": np.asarray(rot_y(eye - hy * 0.6))}
    trans = {"chassis": (0.0, h, 0.0)}
    M = np.asarray(rot_y(math.radians(-90.0)))
    for leg, (fx, lift) in zip(LEGS, feet):
        # Ступня в мире под своим бедром (вынос fx вперёд от оси качания при стоящем тазе), в оси таза
        Fw = np.array([leg.corner[0], leg.foot_h + lift, leg.corner[1] + U + fx])
        loc = Rc.T @ (Fw - np.array([0.0, h, 0.0]))
        r = M.T @ (loc - np.array([leg.corner[0], 0.0, leg.corner[1]]))
        a, s, t = solve(r[0] - U, r[1])
        lc = lambda ang: M @ np.asarray(rot_z(ang)) @ M.T
        n = leg.name
        rot["hip_" + n] = np.eye(3)
        rot["thigh_" + n] = lc(a - leg.a1)
        rot["shin_" + n] = lc((s - leg.a2) - (a - leg.a1))
        rot["tarsus_" + n] = lc((t - leg.a3) - (s - leg.a2))
        rot["foot_" + n] = lc(-(t - leg.a3))
    # подошва по земле: ступня в мире без поворота
    mats = R.pose(rig, rot, trans)
    ix = {b[0]: i for i, b in enumerate(rig.bones)}
    for leg in LEGS:
        n = leg.name
        Rt = mats[ix["tarsus_" + n]][0]
        rot["foot_" + n] = Rt.T
    arms(rig, rot, trans)
    pairs = [(p + leg.name, q + leg.name) for leg in LEGS for p, q in (("ka_", "kb_"), ("ma_", "mb_"))]
    pairs += [("ea_r", "eb_r"), ("ea_l", "eb_l")]
    cylinders(rig, rot, trans, pairs)
    return rot, trans


# Ход винтовки: возвышение −0.5 … 0.75 (с подбросом отдачи) и переход в походное (src/view/gryd_move.cpp)
ARM_RANGE = [(STOW[0] * s, p / 20.0 + (STOW[1] - p / 20.0) * s) for p in range(-10, 16) for s in (0.0, 0.25, 0.5, 0.75, 1.0)]


def elbow_range(n):
    """Расстояние между концами цилиндра локтя руки n по ходу винтовки (ARM_RANGE): (меньшее, большее)."""
    import gryd
    import gryd_arm as arm
    A = gryd.arm_points()[n]
    S0, E0, W0 = (np.asarray(A[k]) for k in ("S", "E", "W"))
    ea0, eb0 = (np.asarray(v) for v in arm.cyl_points(A["S"], A["E"], A["W"]))
    ds = []
    for yaw, pitch in ARM_RANGE:
        Rg = gun_rot(yaw, pitch)
        W = np.asarray(gryd.GUN) + Rg @ (W0 - np.asarray(gryd.GUN))
        E = np.asarray(arm.arm_ik(S0, W, pole0(S0, E0, W0)))
        n0 = np.cross(E0 - S0, W0 - E0)
        n1 = np.cross(E - S0, W - E)
        Ru = _frame(E - S0, n1) @ _frame(E0 - S0, n0).T
        Rf = _frame(W - E, n1) @ _frame(W0 - E0, n0).T
        ea = S0 + Ru @ (ea0 - S0)
        eb = E + Rf @ (eb0 - E0)
        ds.append(float(np.linalg.norm(eb - ea)))
    return min(ds), max(ds)
