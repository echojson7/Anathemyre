#!/usr/bin/env python3
"""Поправка мест ношения оружия на сложение (docs/body.md, «Оружие на теле»): насколько морф отодвигает поверхность
тела относительно кости у места ремня на левом бедре (наружу, −x) и за спиной (назад, −z). Печатает таблицу для src/view/appear.cpp
(APPEAR_CARRY): на +1 и на −1 веса морфа, м. Перезапускать после правки MORPHS в humanconv.py.

    python3 tools/humanconv/carry.py
Места — как в carry_place (src/view/arms.cpp): от головы pelvis (−0.19, 0.03, 0.05), от головы spine_03 (0.02, 0.13, −0.13).
"""
import os
import sys

import numpy as np

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
sys.path.insert(0, os.path.join(here, "..", "retarget"))
import meshread  # noqa: E402
import animlib  # noqa: E402

NAMES = ["female", "full", "thin", "strong", "old", "belly", "female_full", "female_thin", "female_strong",
         "shoulders", "hips", "legs", "torso", "arms", "neck", "head", "kin_a", "kin_b",
         "nose", "nose_hump", "jaw", "chin", "lips", "eyes", "cheeks", "brow", "ears"]  # порядок AppearMorph


def main():
    root = os.path.join(here, "..", "..")
    m = meshread.read(os.path.join(root, "data/meshes/human_m.mesh"))
    sk = animlib.Skel(os.path.join(root, "data/people/human_m.game_engine.skel"))
    pel, ch = sk.pos[sk.index["pelvis"]], sk.pos[sk.index["spine_03"]]

    J = m.joints["game_engine"]
    ip, ic = sk.index["pelvis"], sk.index["spine_03"]

    def ext(w):
        p = meshread.shaped(m, w)[0]
        # Поверхность — относительно головы своей кости: место ношения ходит за костью, а кость — за морфом
        dp = sum(v * J[n][ip, :3] for n, v in w.items())
        dc = sum(v * J[n][ic, :3] for n, v in w.items())
        hip = (np.abs(p[:, 1] - (pel[1] + 0.03)) < 0.04) & (np.abs(p[:, 2] - (pel[2] + 0.05)) < 0.06) & (p[:, 0] < 0)
        back = (np.abs(p[:, 1] - (ch[1] + 0.13)) < 0.06) & (np.abs(p[:, 0] - (ch[0] + 0.02)) < 0.06) & (p[:, 2] < ch[2])
        return np.array([(-p[hip, 0]).max() + (dp[0] if w else 0), (-p[back, 2]).max() + (dc[2] if w else 0)])

    b = ext({})
    print("// бедро +1, бедро −1, спина +1, спина −1 (tools/humanconv/carry.py)")
    for n in NAMES:
        if n not in m.morph_names:
            sys.exit("нет морфа " + n)
        p, q = ext({n: 1.0}) - b, ext({n: -1.0}) - b
        print("    {%.4ff, %.4ff, %.4ff, %.4ff},  // %s" % (p[0], q[0], p[1], q[1], n))


if __name__ == "__main__":
    main()
