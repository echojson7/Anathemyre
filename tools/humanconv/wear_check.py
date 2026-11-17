"""Проверка одежды в движении (docs/wear.md, «Проверка»): пробои нижнего сквозь верхнее по всем клипам захвата,
крайним сложениям и парам вещей, которые носятся вместе. Без игры: скиннинг на numpy, как skin.comp.

    python3 -I tools/humanconv/wear_check.py <исходники MakeHuman> [--step 3] [--clips walk,run] [--quiet]

Пробой — вершина нижнего (тела или вещи), видимая при надетых вместе вещах, оказалась над поверхностью верхней
вещи больше чем на POKE. Пары: верхняя вещь G выше нижней по порядку надевания (wear_core.RANK), вещи совместимы
(носятся вместе), рубаха и ботинки надеты всегда. Код возврата 1, если пробои есть.
"""
import argparse
import glob
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "retarget"))
import humanconv as H  # noqa: E402
import wear  # noqa: E402
import wear_core as W  # noqa: E402
from animlib import Anim, Skel  # noqa: E402

POKE = 0.002     # насколько нижнее может выйти над верхним (м): мельче — не видно
NEAR = 0.04      # пара точек дальше — не та поверхность (край, складка)
EDGE = 0.015     # у самого края верхней вещи нижнее выходит из-под неё законно
ALWAYS = ("wear_shirt", "wear_boots")
# Вещи, которые вместе не носятся (src/view/wear.cpp, wear_make)
EXCLUSIVE = [("wear_trousers", "wear_hakama"), ("wear_jacket", "wear_coat"), ("wear_coat", "wear_vest"),
             ("wear_wraps_arm", "wear_gloves")]
CHECK_SHAPES = ("female", "full", "female_full", "thin")


def compatible(a, b):
    return (a, b) not in EXCLUSIVE and (b, a) not in EXCLUSIVE


def skin(P, bones, wts, M):
    # P (N,3) бинд, bones/wts (N,4), M (n,3,4) — матрицы костей позы: v' = Σ w · M_b · [v, 1]
    A = (M[bones] * wts[:, :, None, None]).sum(1)  # (N,3,4)
    return np.einsum("nij,nj->ni", A[:, :, :3], P) + A[:, :, 3]


def palette(sk, Wp, Pp):
    # Матрицы скиннинга кадра: мир кости в позе · бинд⁻¹
    n = sk.n
    M = np.zeros((n, 3, 4))
    R = Wp @ np.transpose(sk.bind, (0, 2, 1))
    M[:, :, :3] = R
    M[:, :, 3] = Pp - np.einsum("nij,nj->ni", R, sk.pos)
    return M


def dequant(bones, wq):
    w = wq.astype(np.float64)
    return bones.astype(np.int64), w / np.maximum(w.sum(1, keepdims=True), 1e-9)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("--step", type=int, default=3, help="каждый N-й ключ клипа")
    ap.add_argument("--clips", default="")
    ap.add_argument("--shapes", default=",".join(CHECK_SHAPES))
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--rest", action="store_true", help="только бинд-поза (без клипов)")
    a = ap.parse_args()

    body = wear.load_body(a.src)
    clothes = wear.catalog(body)
    dist = W.cover_all(body, clothes)
    byname = {g.name: g for g in clothes}
    sk = Skel(os.path.join(ROOT, "data", "people", "human_m.game_engine.skel"))
    if sk.names != body.order["game_engine"]:
        sys.exit("wear_check: кости .skel не как у тела стенда")

    # Тело: вершины MakeHuman (как у сетки тела до разрезов белья), веса — как у сетки, от главной вершины
    bb, bw = dequant(*H.skin_quant(body.W["game_engine"]))
    cov_body = np.zeros(body.nv, np.int64)
    for g in clothes:
        if g.name in dist:
            cov_body |= np.where(dist[g.name] >= g.margin, g.hide, 0)
    used = body.used
    # Вещи: вершины ткани до деления по закрытости, свои веса
    gw = {}
    for g in clothes:
        bo, wq = g.weights("game_engine")
        gw[g.name] = dequant(bo[: g.ncloth], wq[: g.ncloth])

    names = [""] + [s for s in a.shapes.split(",") if s]
    qs = [H.BASE] + [terms[0][1] for nm, terms in H.MORPHS if nm in names[1:]]
    shapes = [body.shape(q) for q in qs]
    labels = ["стенд"] + [nm for nm, _ in H.MORPHS if nm in names[1:]]

    # Пары: (нижнее, верхняя вещь). Кандидаты нижнего — видимые при надетых {нижнее, G, ALWAYS}, в области G не у края
    pairs = []
    for G in clothes:
        if G.name not in dist:
            continue
        rg = W.RANK[G.name]
        worn_bits = G.hide | sum(byname[n].hide for n in ALWAYS)
        inner = [("тело", None)] + [(X.name, X) for X in clothes
                                     if X is not G and W.RANK[X.name] < rg and compatible(X.name, G.name)]
        for nm, X in inner:
            bits = worn_bits | (X.hide if X is not None else 0)
            if X is None:
                d = dist[G.name]
                idx = used[(d[used] >= EDGE) & ((cov_body[used] & bits) == 0)]
            else:
                d = H.ext_eval(dist[G.name], X.VI, X.VW)
                idx = np.where((d >= EDGE) & ((X.cover & (bits & ~X.hide)) == 0))[0]
            if len(idx):
                pairs.append((nm, X, G, idx))

    # Соответствие в покое (у каждого тела своё): ближняя вершина лица верхней вещи
    def rest(sh):
        Pb = sh[0]
        PG = {g.name: g.positions(sh)[: g.ncloth] for g in clothes}
        corr = []
        for nm, X, G, idx in pairs:
            p = Pb[idx] if X is None else PG[X.name][idx]
            face = np.where(G.VK == 0)[0]
            j = face[W.nearest(p, PG[G.name][face])]
            ok = np.linalg.norm(p - PG[G.name][j], axis=1) < NEAR
            corr.append((idx[ok], j[ok]))
        return Pb, PG, corr

    anims = sorted(glob.glob(os.path.join(ROOT, "data", "anims", "*.game_engine.anim")))
    if a.clips:
        want = set(a.clips.split(","))
        anims = [p for p in anims if os.path.basename(p).split(".")[0] in want]
    if a.rest:
        anims = [None]
    worst = {}
    frames = 0
    for si, sh in enumerate(shapes):
        Pb, PG, corr = rest(sh)
        for path in anims:
            if path is None:
                clip, keys = "бинд", [0]
                Wp, Pp = sk.bind[None], sk.pos[None]
            else:
                clip = os.path.basename(path).split(".")[0]
                an = Anim.read(path)
                if an.bones != sk.names:
                    sys.exit("wear_check: %s — кости не как в .skel" % clip)
                keys = list(range(0, an.frames(), a.step))
                Wp, Pp = sk.fk(an.rot[keys], an.trans_bone, an.trans[keys] / 1000.0)
            for f, key in enumerate(keys):
                M = palette(sk, Wp[f], Pp[f])
                frames += 1
                Sb = skin(Pb, bb, bw, M)
                SG = {g.name: skin(PG[g.name], *gw[g.name], M) for g in clothes}
                NG = {g.name: W.cloth_normals(g, np.concatenate([SG[g.name], np.zeros((len(g.VM) - g.ncloth, 3))]))
                      for g in clothes}
                for (nm, X, G, _), (ii, jj) in zip(pairs, corr):
                    if not len(ii):
                        continue
                    p = Sb[ii] if X is None else SG[X.name][ii]
                    q, n = SG[G.name][jj], NG[G.name][jj]
                    s = ((p - q) * n).sum(1)
                    bad = s > POKE
                    if not bad.any():
                        continue
                    key_ = (nm, G.name)
                    rec = worst.setdefault(key_, dict(verts=set(), max=0.0, where=""))
                    rec["verts"].update(ii[bad].tolist())
                    if s.max() > rec["max"]:
                        rec["max"] = float(s.max())
                        rec["where"] = "%s, %s, ключ %d" % (labels[si], clip, key)
    total = 0
    print("wear_check: тел %d, клипов %d, кадров %d, пар %d" % (len(shapes), len(anims), frames, len(pairs)))
    for (nm, gn), r in sorted(worst.items(), key=lambda e: -len(e[1]["verts"])):
        total += len(r["verts"])
        if not a.quiet:
            print("  пробой: %-15s сквозь %-15s вершин %5d, до %4.1f мм (%s)" % (
                nm.replace("wear_", ""), gn.replace("wear_", ""), len(r["verts"]), r["max"] * 1000, r["where"]))
    print("wear_check: пробоев — вершин %d" % total)
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
