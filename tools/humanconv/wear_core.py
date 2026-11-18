"""Ядро одежды (docs/wear.md): вещь — поверхность над телом MakeHuman, привязанная к нему.

Вершина вещи — смесь до 8 вершин тела (EI — номера, EW — доли) и высота h над «телом для ткани»: тело, сглаженное
так, что ткань перекрывает впадины (подмышки, пах, ложбину спины), но не уходит внутрь кожи. Положение в любом
теле (морфе) — смесь сглаженных вершин этого тела + сглаженная нормаль × h, поэтому вещь сама садится на рост,
пол и полноту; морф вещи — та же сумма тел, что у морфа тела (humanconv MORPHS). Веса костей — смесь весов вершин
тела. Края — по нулю полей на вершинах тела (cut из humanconv): ровные, а не лесенкой по граням.

Высота считается на угол треугольника (вершина + сам треугольник): у соседних треугольников по разные стороны
линии высоты разные — на этой линии ступень (слой обмотки, край запаха, заплата), её закрывает стенка. Свободный
край вещи — кромка толщины ткани вниз к телу или к подкладке (у широких вещей — двусторонних).
Только numpy, байты не зависят от версии numpy: всё во float64, перед записью — сетка grid (как у тела).
"""
import json
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np

import humanconv as H

DIRT = 0.55  # доля пыли от заданной вещью: пятна порогом шума, при 0.5 — уже половина поверхности
SMOOTH_IT = 14            # итераций Таубина для тела под ткань
SMOOTH_LOOSE = 45         # для свободных вещей (штаны, полы): ткань не входит в ложбины совсем
SMOOTH_LAM, SMOOTH_MU = 0.55, -0.58
ARM_BONES = ("upperarm", "lowerarm", "hand", "index", "middle", "ring", "pinky", "thumb")
LEG_BONES = ("thigh", "calf", "foot", "ball")
TORSO, ARM_L, ARM_R, LEG_L, LEG_R = range(5)

# Области тела для скрытия под одеждой (номер — в байте доли смеси вершины тела, docs/mesh.md; маска — бит номера).
# Границы — с запасом 5–8 см внутрь от отверстий вещей: у края вещи кожа под ней видна, в рукав не видно насквозь
REGIONS = ("none", "foot", "ankle", "shin", "knee", "thigh", "hips", "waist", "chest", "shoulder",
           "uparm", "elbow", "forearm", "wrist")
R = {n: i for i, n in enumerate(REGIONS)}


def mask(*names):
    m = 0
    for n in names:
        m |= 1 << R[n]
    return m


class Body:
    """Тело стенда и всё, что о нём знает одежда: параметры вершин (часть тела, длина вдоль руки или ноги, угол
    вокруг оси), плотные веса костей, сглаженные тела морфов."""

    def __init__(self, base, groups, Z, rigs, weights, mh):
        self.base, self.groups, self.Z, self.rigs, self.mh = base, groups, Z, rigs, mh
        self.quads = groups["body"][:, ::-1]
        self.T = H.tri(self.quads).astype(np.int64)
        self.used = np.unique(groups["body"])
        nv = self.nv = len(base)
        q = self.quads
        e = np.concatenate([q[:, [0, 1]], q[:, [1, 2]], q[:, [2, 3]], q[:, [3, 0]]])
        self.E = np.unique(np.sort(e, 1), axis=0)
        self.deg = np.bincount(self.E.ravel(), minlength=nv).astype(np.float64)
        self.cache = {}
        vo = H.morph(base, Z, H.RIG)  # скелет — от тела RIG, как у human_m (humanconv)
        y0 = vo[self.used, 1].min()
        fit = (y0, H.HEIGHT / (vo[self.used, 1].max() - y0))
        to_game = lambda x: self.place(x[None, :], fit)[0]
        self.skels = {s: H.skeleton(vo, groups, rigs[s], to_game) for s in H.SKELETONS}
        self.order = {s: [b["name"] for b in self.skels[s]] for s in H.SKELETONS}
        self.J = {b["name"]: b["pos"] for b in self.skels["game_engine"]}
        self.Jlen = {b["name"]: b["length"] for b in self.skels["game_engine"]}
        self.W = {}
        for s in H.SKELETONS:
            acc = np.zeros((nv, len(self.order[s])))
            for k, n in enumerate(self.order[s]):
                lst = weights[s].get(n, [])
                if lst:
                    a = np.array(lst)
                    np.add.at(acc[:, k], a[:, 0].astype(int), a[:, 1])
            self.W[s] = acc
        # Разметка вещей — в A-позе (по ней заданы числа частей и суставы скелета); вершины сеток — в стойке беты
        self.V, self.N, self.S, self.Ns = self.shape(H.BASE, rest=False)[:4]
        self._params()

    @staticmethod
    def place(vo, fit):
        g = vo * H.OBJ_TO_GAME
        return (g - np.array([0.0, fit[0], 0.0])) * fit[1]

    def shape(self, q, rest=True):
        # Тело с параметрами q (как shape в humanconv.build): вершины, нормали, тело для ткани и его нормали.
        # rest — в стойке беты (поза привязки сеток, humanconv.rest), иначе в A-позе MakeHuman (разметка)
        key = json.dumps([q, rest], sort_keys=True)
        if key in self.cache:
            return self.cache[key]
        vo = H.morph(self.base, self.Z, q)
        lo = vo[self.used, 1].min()
        V = self.place(H.rest(vo, self.mh) if rest else vo, (lo, H.HEIGHT / (vo[self.used, 1].max() - lo)))
        N = H.normals(V, self.T)
        S = self.smooth(V, N, SMOOTH_IT)
        S2 = self.smooth(V, N, SMOOTH_LOOSE)
        self.cache[key] = (V, N, S, H.normals(S, self.T), S2, H.normals(S2, self.T))
        return self.cache[key]

    def smooth(self, V, N, iters):
        # Таубин (λ, μ — без усадки) по рёбрам тела, после каждого шага — не внутрь кожи
        S = V.copy()
        a, b = self.E[:, 0], self.E[:, 1]
        d = np.maximum(self.deg, 1.0)[:, None]
        for it in range(iters):
            for k in (SMOOTH_LAM, SMOOTH_MU):
                acc = np.zeros_like(S)
                np.add.at(acc, a, S[b])
                np.add.at(acc, b, S[a])
                S = S + k * (acc / d - S) * (self.deg[:, None] > 0)
            s = ((S - V) * N).sum(1)
            S = S + N * np.maximum(0.0, -s)[:, None]
        return S

    def _params(self):
        # Параметры вершины тела (стенд): часть, длина вдоль руки или ноги от плеча или бедра, угол вокруг оси
        # (0 — вперёд; у ног + — наружу, у рук + — вверх, у туловища + — вправо, x > 0), расстояние до оси
        V, J, nv = self.V, self.J, self.nv
        dom = np.array(self.order["game_engine"])[np.argmax(self.W["game_engine"], 1)]
        limb = np.full(nv, TORSO)
        for i, n in enumerate(dom):
            side = n.endswith("_l")
            if n.startswith(ARM_BONES) and n[-2:] in ("_l", "_r"):
                limb[i] = ARM_L if side else ARM_R
            elif n.startswith(LEG_BONES) and n[-2:] in ("_l", "_r"):
                limb[i] = LEG_L if side else LEG_R
        self.limb = limb
        along = np.full(nv, -1.0)
        th = np.arctan2(V[:, 0], V[:, 2] + 0.01)
        rad = np.hypot(V[:, 0], V[:, 2] + 0.01)
        self.chain = {}
        for lb, s in ((ARM_L, "l"), (ARM_R, "r"), (LEG_L, "l"), (LEG_R, "r")):
            if lb in (ARM_L, ARM_R):
                names = ["upperarm_", "lowerarm_", "hand_", "middle_01_", "middle_03_"]
            else:
                names = ["thigh_", "calf_", "foot_", "ball_"]
            pts = [J[n + s] for n in names]
            last = names[-1] + s
            d = pts[-1] - pts[-2]
            pts.append(pts[-1] + d / np.linalg.norm(d) * max(self.Jlen[last], 0.03))
            pts = np.array(pts)
            self.chain[lb] = pts
            idx = np.where(limb == lb)[0]
            a, t_, r_ = self.project(V[idx], pts, lb)
            along[idx], th[idx], rad[idx] = a, t_, r_
        self.along, self.th, self.rad = along, th, rad
        self.y, self.x, self.z = V[:, 1].copy(), V[:, 0].copy(), V[:, 2].copy()
        # Опорные длины и высоты
        self.knee = self.Jlen["thigh_l"]
        self.ankle = self.knee + self.Jlen["calf_l"]
        self.elbow = self.Jlen["upperarm_l"]
        self.wrist = self.elbow + self.Jlen["lowerarm_l"]
        self.y_pelvis = J["pelvis"][1]
        self.y_hip = (J["thigh_l"][1] + J["thigh_r"][1]) * 0.5
        self.y_chest = J["spine_03"][1]
        self.y_neck = J["neck_01"][1]
        self.y_head = J["head"][1]

    def project(self, P, pts, lb):
        # Ближняя точка ломаной оси: длина вдоль, угол, расстояние. До начала и после конца — продолжение
        seg = pts[1:] - pts[:-1]
        L = np.linalg.norm(seg, axis=1)
        cum = np.concatenate([[0.0], np.cumsum(L)])
        best = np.full(len(P), np.inf)
        a = np.zeros(len(P))
        foot = np.zeros((len(P), 3))
        dirs = np.zeros((len(P), 3))
        for k in range(len(seg)):
            u = seg[k] / L[k]
            t = ((P - pts[k]) @ u)
            lo = -np.inf if k == 0 else 0.0
            hi = np.inf if k == len(seg) - 1 else L[k]
            t = np.clip(t, lo, hi)
            c = pts[k] + t[:, None] * u
            dd = np.linalg.norm(P - c, axis=1)
            m = dd < best - 1e-12
            best[m], a[m], foot[m], dirs[m] = dd[m], cum[k] + t[m], c[m], u
        r = P - foot
        if lb in (LEG_L, LEG_R):
            ref = np.array([0.0, 0.0, 1.0])
            lat = np.array([-1.0 if lb == LEG_L else 1.0, 0.0, 0.0])
        else:
            ref = np.array([0.0, 0.0, 1.0])
            lat = np.array([0.0, 1.0, 0.0])
        f_ = ref - dirs * (dirs @ ref)[:, None]
        f_ /= np.linalg.norm(f_, axis=1, keepdims=True)
        l_ = lat - dirs * (dirs @ lat)[:, None] - f_ * (f_ @ lat)[:, None]
        l_ /= np.maximum(np.linalg.norm(l_, axis=1, keepdims=True), 1e-9)
        th = np.arctan2((r * l_).sum(1), (r * f_).sum(1))
        return a, th, np.linalg.norm(r, axis=1)

    def near(self, p, limb=None):
        # Вершина тела стенда, ближняя к точке p (оси игры); limb — только этой части
        idx = self.used if limb is None else self.used[np.isin(self.limb[self.used], np.atleast_1d(limb))]
        d = np.linalg.norm(self.V[idx] - np.asarray(p, np.float64), axis=1)
        return int(idx[np.argmin(d)])

    def pick(self, limb, al=None, th=None, y=None, x=None, z=None):
        # Вершина тела части limb, ближняя по заданным параметрам (длина вдоль, угол, высота, x, z)
        idx = self.used[np.isin(self.limb[self.used], np.atleast_1d(limb))]
        d = np.zeros(len(idx))
        for val, arr, k in ((al, self.along, 1.0), (y, self.y, 1.0), (x, self.x, 1.0), (z, self.z, 1.0)):
            if val is not None:
                d += k * np.abs(arr[idx] - val)
        if th is not None:
            d += 0.05 * np.abs(np.angle(np.exp(1j * (self.th[idx] - th))))
        return int(idx[np.argmin(d)])

    def regions(self):
        # Область вершины тела для скрытия под одеждой (REGIONS), по параметрам стенда
        reg = np.zeros(self.nv, np.int64)
        al, y = self.along, self.y
        leg = (self.limb == LEG_L) | (self.limb == LEG_R)
        arm = (self.limb == ARM_L) | (self.limb == ARM_R)
        tor = self.limb == TORSO
        k, a = self.knee, self.ankle
        reg[leg & (al > a + 0.02)] = R["foot"]
        reg[leg & (al <= a + 0.02) & (al > a - 0.07)] = R["ankle"]
        reg[leg & (al <= a - 0.07) & (al > k + 0.10)] = R["shin"]
        reg[leg & (al <= k + 0.10) & (al > k - 0.12)] = R["knee"]
        reg[leg & (al <= k - 0.12)] = R["thigh"]
        yw = self.y_pelvis + 0.06
        reg[tor & (y < yw)] = R["hips"]
        reg[tor & (y >= yw) & (y < self.y_chest)] = R["waist"]
        reg[tor & (y >= self.y_chest) & (y < self.y_neck - 0.10)] = R["chest"]
        reg[tor & (y >= self.y_neck - 0.10) & (y < self.y_neck + 0.01)] = R["shoulder"]
        e, w = self.elbow, self.wrist
        reg[arm & (al < e - 0.07)] = R["uparm"]
        reg[arm & (al >= e - 0.07) & (al < e + 0.07)] = R["elbow"]
        reg[arm & (al >= e + 0.07) & (al < w - 0.07)] = R["forearm"]
        reg[arm & (al >= w - 0.07) & (al < w + 0.02)] = R["wrist"]
        return reg

    def vparams(self, EI, EW):
        # Параметры вершин-смесей: непрерывные — смесью, угол — через cos/sin, часть — у главной вершины
        mix = lambda X: H.ext_eval(X, EI, EW)
        dom = EI[np.arange(len(EI)), np.argmax(EW, 1)]
        th = np.arctan2(mix(np.sin(self.th)), mix(np.cos(self.th)))
        n = mix(self.N)
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
        return dict(limb=self.limb[dom], al=mix(self.along), th=th, rad=mix(self.rad), y=mix(self.y),
                    x=mix(self.x), z=mix(self.z), dom=dom, nx=n[:, 0], ny=n[:, 1], nz=n[:, 2])


def vuv0(pos, T, n, uv):
    """Кусок VUV0 (docs/mesh.md): uv f16 и касательная (октаэдр 15 + 15 бит, бит 31 — знак битангенса). Касательная —
    dP/du по треугольникам (Ленгьел); знак — битангенс шейдера cross(N, T)·w смотрит против dP/dv (к верху картинки)"""
    # Касательная — по развёртке, как она лежит в куске (f16): последние биты float64 у разных версий numpy разные,
    # а у почти вырожденной вершины они поворачивают касательную
    uv = np.asarray(uv, np.float64).astype(np.float16).astype(np.float64)
    t = np.zeros_like(pos)
    b = np.zeros_like(pos)
    p0, p1, p2 = pos[T[:, 0]], pos[T[:, 1]], pos[T[:, 2]]
    w0, w1, w2 = uv[T[:, 0]], uv[T[:, 1]], uv[T[:, 2]]
    e1, e2 = p1 - p0, p2 - p0
    d1, d2 = w1 - w0, w2 - w0
    det = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    r = np.where(np.abs(det) > 1e-12, 1.0 / np.where(np.abs(det) > 1e-12, det, 1.0), 0.0)[:, None]
    sd = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) * r
    td = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) * r
    for k in range(3):
        np.add.at(t, T[:, k], sd)
        np.add.at(b, T[:, k], td)
    t = t - n * (t * n).sum(1, keepdims=True)
    bad = np.linalg.norm(t, axis=1) < 1e-12
    t[bad] = np.cross(n[bad], np.array([0.0, 1.0, 0.0]) + 0.0)
    t[np.linalg.norm(t, axis=1) < 1e-12] = np.array([1.0, 0.0, 0.0])
    t /= np.linalg.norm(t, axis=1, keepdims=True)
    w_neg = (np.cross(n, t) * -b).sum(1) < 0.0     # cross(N, T) смотрит по dP/dv — знак −1
    a = np.abs(t).sum(1, keepdims=True)
    q = t[:, :2] / a
    neg = t[:, 2] < 0
    q[neg] = (1.0 - np.abs(q[neg][:, ::-1])) * np.where(q[neg] >= 0, 1.0, -1.0)
    e = np.clip(np.rint((q + 1.0) * 0.5 * 32767.0), 0, 32767).astype(np.uint32)
    tan = e[:, 0] | (e[:, 1] << 15) | (w_neg.astype(np.uint32) << 31)
    rec = np.zeros(len(pos), dtype=[("u", "<f2"), ("v", "<f2"), ("t", "<u4")])
    rec["u"], rec["v"], rec["t"] = uv[:, 0], uv[:, 1], tan
    return rec.tobytes()


def box(w, h, d, bevel=0.0, taper=0.0):
    """Коробка w × h × d (x, y, z) с фаской по рёбрам и сужением верха (taper — доля): вершины на грань (жёсткие
    рёбра), z от 0 (у тела) до d. Для пряжек, подсумков, бляшек"""
    b = min(bevel, w * 0.3, h * 0.3, d * 0.3)
    xs, ys = np.array([-w / 2, w / 2]), np.array([-h / 2, h / 2])
    # Восьмиугольный профиль в плоскости xz по высоте не строим: фаска — срез рёбер наружной грани
    P = []
    for zi, z in enumerate((0.0, d - b, d)):
        k = 1.0 - taper * (z / d)
        ins = b if zi == 2 else 0.0
        P.append([(xs[0] * k + ins, ys[0] * k + ins, z), (xs[1] * k - ins, ys[0] * k + ins, z),
                  (xs[1] * k - ins, ys[1] * k - ins, z), (xs[0] * k + ins, ys[1] * k - ins, z)])
    P = np.array(P, np.float64)  # [слой][угол]
    quads = []
    for l in range(2):
        for c in range(4):
            a0, a1 = P[l][c], P[l][(c + 1) % 4]
            b0, b1 = P[l + 1][c], P[l + 1][(c + 1) % 4]
            quads.append((a0, a1, b1, b0))
    quads.append(tuple(P[2][::-1][[3, 2, 1, 0]]))  # верх (наружу +z)
    V, T = [], []
    for q in quads:
        q = np.array(q)
        n = len(V)
        V += list(q)
        T += [[n, n + 1, n + 2], [n, n + 2, n + 3]]
    V, T = np.array(V), np.array(T, np.int64)
    # Обход: наружу от середины
    c = V.mean(0) * np.array([1, 1, 0]) + np.array([0, 0, d * 0.5])
    fn = np.cross(V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]])
    out = ((V[T].mean(1) - c) * fn).sum(1) < 0
    T[out] = T[out][:, ::-1]
    return V, T


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


class Part:
    """Кусок вещи над телом: треугольники тела в области (все поля > 0), разрезанные по полям. corner(v, t) — высота
    угла: v — параметры вершин угла, t — параметры треугольника (середина); оба словари массивов по углам."""

    def __init__(self, body, keep, cuts=(), corner=None, mat=None, dirt=None, thick=0.004, double=False,
                 rim=True, smooth_w=0, ao=None, pieces=None, loose=False):
        self.body, self.thick, self.double, self.rim, self.smooth_w = body, thick, double, rim, smooth_w
        self.loose = loose
        self.corner, self.mat, self.dirt, self.ao, self.pieces = corner, mat, dirt, ao, pieces
        B = body
        T = B.T.copy()
        EI = np.zeros((B.nv, 8), np.int64)
        EI[:, 0] = np.arange(B.nv)
        EW = np.zeros((B.nv, 8))
        EW[:, 0] = 1.0
        # Только треугольники, где все поля области ≥ запаса, — до резки (резка — по всему телу дорого)
        F = [f for f in keep]
        pre = np.ones(len(T), bool)
        for f in F:
            pre &= f[T].max(1) > -1e-9
        T = T[pre]
        for f in list(F) + list(cuts):
            T, _, EI, EW = H.cut(T, H.ext_eval(f, EI, EW), EI, EW)
        ok = np.ones(len(T), bool)
        for f in F:
            ok &= H.ext_eval(f, EI, EW)[T].mean(1) > 0
        T = T[ok]
        u, inv = np.unique(T.ravel(), return_inverse=True)
        self.EI, self.EW, self.T = EI[u], EW[u], inv.reshape(-1, 3)
        self.vp = B.vparams(self.EI, self.EW)


class Garment:
    """Вещь: куски над телом (Part) и жёсткие детали (пряжки, подсумки) на точках тела. Пишет .mesh."""

    def __init__(self, name, materials, hide=0, carry=(0.0, 0.0)):
        self.name, self.materials, self.hide, self.carry = name, list(materials), hide, carry
        self.parts, self.rigid = [], []

    def add(self, part):
        self.parts.append(part)
        return part

    def attach(self, body, at, verts, faces, mats, up=(0.0, 1.0, 0.0), h=0.0):
        # Жёсткая деталь: verts в осях точки тела (x — вбок, y — вверх по касательной, z — наружу по нормали),
        # at — номер вершины тела стенда (или массив номеров — средняя точка). Веса — у точки, деталь не гнётся
        at = np.atleast_1d(np.asarray(at, np.int64))
        self.rigid.append(dict(at=at, verts=np.asarray(verts, np.float64), faces=np.asarray(faces, np.int64),
                               mats=np.asarray(mats, np.int64), up=np.asarray(up, np.float64), h=h))

    # --- сборка вершин: топология одна на все тела, положение — функция тела ---
    def topology(self):
        VI, VW, VH, VM, tris, tmat = [], [], [], [], [], []
        self.walls = 0
        base = 0

        VL = []

        def push(ei, ew, hh, mm):
            VL.append(np.full(len(hh), bool(P.loose)))
            VI.append(ei)
            VW.append(ew)
            VH.append(hh)
            VM.append(mm)

        for P in self.parts:
            vp, T = P.vp, P.T
            nt = len(T)
            # Параметры треугольника — середина (угол — через cos/sin, часть — у первой вершины)
            tp = {}
            for k, X in vp.items():
                if k == "th":
                    tp[k] = np.arctan2(np.sin(X[T]).mean(1), np.cos(X[T]).mean(1))
                elif k in ("limb", "dom"):
                    tp[k] = X[T[:, 0]]
                else:
                    tp[k] = X[T].mean(1)
            cv = {k: X[T].ravel() for k, X in vp.items()}   # углы: своя вершина
            ct = {k: np.repeat(X, 3) for k, X in tp.items()}  # углы: их треугольник
            full = lambda x, n, dt: np.broadcast_to(np.asarray(x, dt), (n,)).copy()
            h = full(P.corner(cv, ct) if P.corner else 0.0, nt * 3, np.float64)
            mt = full(P.mat(tp) if P.mat else 0, nt, np.int64)
            m0c = np.repeat(mt, 3)
            if P.dirt:
                m1, bl = P.dirt(cv, ct)
                m1, bl = full(m1, nt * 3, np.int64), full(bl, nt * 3, np.float64) * DIRT
            else:
                m1, bl = m0c.copy(), np.zeros(nt * 3)
            ao = full(P.ao(cv, ct) if P.ao else 1.0, nt * 3, np.float64)
            # Вершина = (вершина куска, высота, материалы, доля, ао): разные высоты у одной вершины — ступень
            vid = T.ravel()
            key = np.stack([vid, np.rint(h * 2 ** 16).astype(np.int64), m0c, m1,
                            np.rint(np.clip(bl, 0, 1) * 255).astype(np.int64),
                            np.rint(np.clip(ao, 0, 1) * 255).astype(np.int64)], 1)
            _, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
            inv = inv.ravel()
            nv = len(first)
            src = vid[first]
            hv = h[first]
            mv = key[first][:, 2:].copy()
            Tn = inv.reshape(-1, 3)
            push(P.EI[src], P.EW[src], hv, mv)
            tris.append(Tn + base)
            tmat.append(mt)
            # Рёбра куска: у края один треугольник, внутри — два
            ed = {}
            for t in range(nt):
                for k in range(3):
                    a_, b_ = T[t, k], T[t, (k + 1) % 3]
                    ed.setdefault((min(a_, b_), max(a_, b_)), []).append((t, k))
            rim_e = []
            for lst in ed.values():
                if len(lst) == 1:
                    t, k = lst[0]
                    rim_e.append((Tn[t, k], Tn[t, (k + 1) % 3], mt[t]))
                    continue
                if len(lst) != 2:
                    continue
                (t1, k1), (t2, k2) = lst
                a1, b1 = Tn[t1, k1], Tn[t1, (k1 + 1) % 3]   # a1 → b1 — по обходу t1
                a2, b2 = Tn[t2, (k2 + 1) % 3], Tn[t2, k2]   # те же точки тела у t2
                if a1 == a2 and b1 == b2:
                    continue
                if max(abs(hv[a1] - hv[a2]), abs(hv[b1] - hv[b2])) < 5e-4:
                    continue
                # Стенка ступени смотрит в сторону нижнего треугольника
                if hv[a1] + hv[b1] < hv[a2] + hv[b2]:
                    q, m_ = (b1, a1, a2, b2), mt[t2]
                else:
                    q, m_ = (a2, b2, b1, a1), mt[t1]
                tris.append(np.array([[q[0], q[1], q[2]], [q[0], q[2], q[3]]]) + base)
                tmat.append(np.array([m_, m_]))
                self.walls += 1
            n_all = nv
            if P.rim and rim_e:
                if P.double:
                    # Подкладка: по вершине тела одна, на толщину ниже самой низкой высоты; обход обратный
                    uv, rows, vinv = np.unique(src, return_index=True, return_inverse=True)
                    vinv = vinv.ravel()
                    hmin = np.full(len(uv), np.inf)
                    np.minimum.at(hmin, vinv, hv)
                    lm = mv[rows].copy()
                    lm[:, 3] = np.minimum(lm[:, 3], 110)
                    lm[:, 1] = lm[:, 0]
                    lm[:, 2] = 0
                    push(P.EI[uv], P.EW[uv], hmin - P.thick, lm)
                    low = vinv + n_all
                    tris.append(low[Tn][:, ::-1] + base)
                    tmat.append(mt)
                    n_all += len(uv)
                else:
                    bv = np.unique(np.array([[a_, b_] for a_, b_, _ in rim_e]).ravel())
                    lm = mv[bv].copy()
                    lm[:, 3] = np.minimum(lm[:, 3], 150)
                    push(P.EI[src[bv]], P.EW[src[bv]], hv[bv] - P.thick, lm)
                    low = np.full(nv, -1, np.int64)
                    low[bv] = n_all + np.arange(len(bv))
                    n_all += len(bv)
                # Кромка: край a → b по обходу (кусок слева), лицом наружу
                rq, rm = [], []
                for a_, b_, m_ in rim_e:
                    rq += [[a_, low[a_], low[b_]], [a_, low[b_], b_]]
                    rm += [m_, m_]
                tris.append(np.array(rq, np.int64) + base)
                tmat.append(np.array(rm, np.int64))
            base += n_all
        self.ncloth = base
        # Жёсткие детали: свои вершины на грань (жёсткие рёбра), веса и место — у точки тела
        self.RA, self.RL, self.RU, self.RH = [], [], [], []
        for r in self.rigid:
            nb = base
            nv = len(r["verts"])
            self.RA.append(r["at"])
            self.RL.append(r["verts"])
            self.RU.append(r["up"])
            self.RH.append(r["h"])
            m = np.zeros((nv, 4), np.int64)
            fm = r["mats"]
            m[r["faces"].ravel(), 0] = np.repeat(fm, 3)
            m[:, 1] = m[:, 0]
            m[:, 3] = 255
            VM.append(m)
            VL.append(np.zeros(nv, bool))
            tris.append(r["faces"] + nb)
            tmat.append(fm)
            base += nv
        self.VI = np.concatenate(VI) if VI else np.zeros((0, 8), np.int64)
        self.VW = np.concatenate(VW) if VW else np.zeros((0, 8))
        self.VH = np.concatenate(VH) if VH else np.zeros(0)
        self.VL = np.concatenate(VL)[: len(self.VH)] if VL else np.zeros(0, bool)
        self.VM = np.concatenate(VM).astype(np.int64)
        self.Tc = np.concatenate(tris).astype(np.int64)
        self.Tm = np.concatenate(tmat).astype(np.int64)

    def positions(self, shape):
        # Положения всех вершин вещи в теле shape (V, N, S, Ns)
        V, N, S, Ns, S2, Ns2 = shape
        L = self.VL[:, None]
        p = np.where(L, H.ext_eval(S2, self.VI, self.VW), H.ext_eval(S, self.VI, self.VW))
        n = np.where(L, H.ext_eval(Ns2, self.VI, self.VW), H.ext_eval(Ns, self.VI, self.VW))
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
        out = [p + n * self.VH[:, None]]
        for at, L, up, h in zip(self.RA, self.RL, self.RU, self.RH):
            o = S[at].mean(0)
            z = Ns[at].mean(0)
            z /= np.linalg.norm(z)
            o = o + z * h
            y = up - z * (up @ z)
            y /= np.linalg.norm(y)
            x = np.cross(y, z)
            out.append(o + L[:, 0:1] * x + L[:, 1:2] * y + L[:, 2:3] * z)
        return np.concatenate(out)

    def weights(self, s):
        W = self.body.W[s]
        acc = (W[self.VI] * self.VW[:, :, None]).sum(1)
        rig = [np.repeat(W[at].mean(0)[None, :], len(L), 0) for at, L in zip(self.RA, self.RL)]
        rig += self.more_weights(s)
        acc = np.concatenate([acc] + rig) if rig else acc
        # Сглаживание весов по рёбрам ткани (широкие вещи: полы, хакама — без изломов на сгибах)
        it = max([p.smooth_w for p in self.parts] + [0])
        if it:
            cl = self.Tc[(self.Tc < self.ncloth).all(1)]
            e = np.unique(np.sort(np.concatenate([cl[:, [0, 1]], cl[:, [1, 2]], cl[:, [2, 0]]]), 1), axis=0)
            deg = np.bincount(e.ravel(), minlength=len(acc)).astype(np.float64)[:, None]
            for _ in range(it):
                s_ = np.zeros_like(acc)
                np.add.at(s_, e[:, 0], acc[e[:, 1]])
                np.add.at(s_, e[:, 1], acc[e[:, 0]])
                m = deg[:, 0] > 0
                acc[m] = acc[m] * 0.5 + 0.5 * s_[m] / deg[m]
        return H.skin_quant(acc)

    def more_weights(self, s):
        # Плотные веса вершин после жёстких деталей (у волос — пряди, hair_core)
        return []

    def write(self, body, data_dir, meshpack, lods):
        self.body = body
        self.topology()
        T = self.Tc
        base_shape = body.shape(H.BASE)
        pos = H.grid(self.positions(base_shape), 17)
        n = H.normals(pos, T)
        nv = len(pos)
        nx, nz = H.oct_encode(n)
        vert = np.zeros(nv, dtype=[("p", "<f4", 3), ("nx", "<u2"), ("nz", "<u2"), ("m0", "u1"), ("m1", "u1"),
                                   ("blend", "u1"), ("ao", "u1")])
        vm = self.VM
        vert["p"], vert["nx"], vert["nz"] = pos, nx, nz
        vert["m0"], vert["m1"] = vm[:, 0], vm[:, 1]
        vert["blend"], vert["ao"] = vm[:, 2], vm[:, 3]
        out = b"ANAMESH1"
        mn, mx = pos.min(0), pos.max(0)
        out += H.chunk(b"HEAD", struct.pack("<II6f", nv, T.size, *mn, *mx))
        out += H.chunk(b"MATS", struct.pack("<I", len(self.materials)) + b"".join(H.name_bytes(m) for m in self.materials))
        out += H.chunk(b"VERT", vert.tobytes())
        out += H.chunk(b"INDX", T.astype("<u4").tobytes())
        for s in H.SKELETONS:
            order = body.order[s]
            bones, wq = self.weights(s)
            b = H.name_bytes(s) + struct.pack("<I", len(order)) + b"".join(H.name_bytes(nm) for nm in order)
            rec = np.zeros(nv, dtype=[("b", "u1", 4), ("w", "u1", 4)])
            rec["b"], rec["w"] = bones, wq
            out += H.chunk(b"SKIN", b + rec.tobytes())
        # Морфы — те же суммы тел, что у тела
        head = struct.pack("<I", len(H.MORPHS))
        recs = []
        cache = {}

        def at(q):
            k = json.dumps(q, sort_keys=True)
            if k not in cache:
                pk = H.grid(self.positions(body.shape(q)), 17)
                cache[k] = (pk, H.normals(pk, T))
            return cache[k]
        for name, terms in H.MORPHS:
            dp, dn = np.zeros_like(pos), np.zeros_like(pos)
            for coef, q in terms:
                pk, nk = at(q)
                dp += coef * pk
                dn += coef * nk
            step = float(2.0 ** np.ceil(np.log2(max(float(np.abs(dp).max()), 1e-6) / 32767.0)))
            ip = np.clip(np.round(dp / step), -32767, 32767).astype("<i2")
            inn = np.clip(np.round(dn * 127), -127, 127).astype("i1")
            head += H.name_bytes(name) + struct.pack("<f", step)
            recs.append((ip, inn))
        rec = np.zeros(nv, dtype=[("m", [("p", "<i2", 3), ("n", "i1", 3)], len(H.MORPHS))])
        for k, (ip, inn) in enumerate(recs):
            rec["m"]["p"][:, k], rec["m"]["n"][:, k] = ip, inn
        out += H.chunk(b"MRPH", head + rec.tobytes())
        # Вещь: какие области тела скрывает, насколько отодвигает места ношения оружия (у бедра, за спиной)
        out += H.chunk(b"WEAR", struct.pack("<I2f", self.hide, *self.carry))
        UV = getattr(self, "UV", None)
        if UV is not None:
            out += H.chunk(b"VUV0", vuv0(pos, T, n, UV))
        with tempfile.TemporaryDirectory() as tmp:
            raw = os.path.join(tmp, self.name + ".raw.mesh")
            open(raw, "wb").write(out)
            nt = len(T)
            lv = []
            for f in lods:  # цепочка строго убывает и не мельче 60 треугольников
                n = max(60, int(nt * f))
                if not lv or n < lv[-1]:
                    lv.append(n)
            subprocess.run([meshpack, "--prune", "--lods", ",".join(str(x) for x in lv), raw,
                            os.path.join(data_dir, "meshes", self.name + ".mesh")], check=True)
        print("одежда: %-16s вершин %5d, треугольников %5d, стенок %4d" % (self.name, nv, len(T), self.walls))
        return pos, T, vm
