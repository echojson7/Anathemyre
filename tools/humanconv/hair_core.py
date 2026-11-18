"""Ядро волос (docs/hair.md): причёска, борода и брови — вещь над телом (wear_core.Garment) из трёх частей.

- Шапка — кусок Part по коже головы внутри линии роста: тонкий слой волос с бороздами вдоль пряди. У края слой
  сходит на нет и смешивается с кожей (второй материал вершины — human_skin): линия роста рваная, а не каска.
- Пряди — сплюснутые сужающиеся жгуты вдоль направляющих. Направляющие растут от корней шагами: жёсткость,
  тяжесть, тяга к месту завязки, упор в тело (сглаженное тело стенда + запас на шарф, капюшон и куртку).
- Свободная вершина пряди держится за ближнюю вершину тела: смещение — в осях этой вершины (нормаль тела и
  направление на соседнюю вершину). Поэтому морфы тела (голова, шея, полнота) двигают пряди вместе с кожей под
  ними, а веса костей — от этой же вершины (у головы — голова, на спине — спина), сглаженные вдоль пряди.

Случайность — свой splitmix64 от имени вещи: байты не зависят от версии numpy.
"""
import numpy as np

import humanconv as H
import wear_core as W
from wear_core import smoothstep as ss

TAU = 2 * np.pi
# Атлас прядей (tools/hairtex/strands.py): столбцы поперёк, корень наверху; отступы от краёв столбца и низа
CARD_COLS = 8
CARD_PAD = 0.02
CARD_V0, CARD_V1 = 0.004, 0.985
HEAD_C = np.array([0.0, 1.715, 0.05])  # середина черепа стенда (оси игры, рост 1.80)
# Кости, за которыми идут пряди (оба скелета)
FOLLOW = ("head", "neck_01", "spine_03", "spine_02", "clavicle_l", "clavicle_r",
          "mixamorig:Head", "mixamorig:Neck", "mixamorig:Spine2", "mixamorig:Spine1", "mixamorig:LeftShoulder",
          "mixamorig:RightShoulder")


class Rng:
    """splitmix64: одинаковые числа на любой машине и версии numpy"""

    def __init__(self, name):
        s = 0x9E3779B97F4A7C15
        for ch in name.encode():
            s = ((s ^ ch) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
        self.s = s

    def u(self):
        self.s = (self.s + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
        z = self.s
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
        z ^= z >> 31
        return (z >> 11) / float(1 << 53)

    def a(self, n, lo=0.0, hi=1.0):
        return np.array([lo + (hi - lo) * self.u() for _ in range(n)])

    def bell(self, n):
        return self.a(n) + self.a(n) - 1.0


class Head:
    """Голова стенда для волос: опорные точки, поля линии роста, упор для прядей, оси вершин тела"""

    def __init__(self, b):
        self.b = b
        V, S, Ns = b.V, b.S, b.Ns
        self.C = HEAD_C
        d = V - HEAD_C
        self.az = np.arctan2(d[:, 0], d[:, 2])   # 0 — вперёд, ±π — затылок, + — к x > 0
        self.r = np.linalg.norm(d, axis=1)
        dom = np.array(b.order["game_engine"])[np.argmax(b.W["game_engine"], 1)]
        self.dom = dom
        self.is_head = np.isin(dom, ("head",))
        used = np.zeros(b.nv, bool)
        used[b.used] = True
        # Упор: голова, шея, верх туловища и плечи (кисти и ноги прядям не мешают)
        arm_far = ((b.limb == W.ARM_L) | (b.limb == W.ARM_R)) & (b.along > b.elbow * 0.6)
        self.cand = np.where(used & (V[:, 1] > 1.05) & ~arm_far & ((b.limb == W.TORSO) | (b.limb <= W.ARM_R)))[0]
        # Сосед вершины для осей (первое ребро по номеру — одинаково в любом теле)
        nb = np.full(b.nv, -1, np.int64)
        E = b.E
        for a_, c_ in ((E[:, 0], E[:, 1]), (E[:, 1], E[:, 0])):
            o = np.lexsort((c_, a_))
            first = np.ones(len(o), bool)
            first[1:] = a_[o][1:] != a_[o][:-1]
            sel = o[first]
            put = nb[a_[sel]] < 0
            nb[a_[sel][put]] = c_[sel][put]
        self.nb = nb
        # Ориентиры лица (по геометрии стенда)
        eyes = np.unique(np.concatenate([b.groups["helper-l-eye"], b.groups["helper-r-eye"]]))
        vo = H.morph(b.base, b.Z, H.BASE)
        lo = vo[b.used, 1].min()
        E3 = b.place(vo, (lo, H.HEIGHT / (vo[b.used, 1].max() - lo)))[eyes]
        self.eye_y = float(E3[:, 1].mean())
        self.eye_top = float(E3[:, 1].max())
        self.eye_x = float(np.abs(E3[:, 0]).mean())
        self.eye_z = float(E3[:, 2].max())
        hv = np.where(self.is_head & used)[0]
        self.top = float(V[hv, 1].max())
        self.nose = V[hv[np.argmax(V[hv, 2])]]
        self.S, self.Ns = S, Ns

    # ---- поля на вершинах тела (> 0 — внутри)
    def scalp(self, P=None, head=None, front=0.0, temple=0.0, nape=0.0, burns=0.0, ear=0.034):
        """Линия роста волос по углу вокруг головы: y линии на угле |az|; P — точки (по умолчанию вершины тела),
        head — маска головы. front — поднять лоб (залысины), temple — залысины на висках, nape — опустить затылок,
        burns — бакенбарды вниз, ear — отступ вокруг уха. Поле в метрах по высоте: > 0 — волосы"""
        if P is None:
            P, head = self.b.V, self.is_head | (self.b.V[:, 1] > 1.58)
        d = P - HEAD_C
        a = np.degrees(np.abs(np.arctan2(d[:, 0], d[:, 2])))
        k = [0, 18, 35, 50, 62, 72, 80, 92, 104, 118, 135, 160, 180]
        y = [1.772, 1.770, 1.762, 1.748, 1.728, 1.712, 1.700, 1.722, 1.708, 1.670, 1.632, 1.612, 1.606]
        yl = np.interp(a, k, y)
        yl += front * (1 - ss(20, 70, a))
        yl += temple * np.exp(-((a - 42) / 14) ** 2)
        yl -= nape * ss(110, 150, a)
        yl -= burns * np.exp(-((a - 80) / 6) ** 2)
        f = P[:, 1] - yl
        for sx in (-1.0, 1.0):  # ухо: шар вокруг середины уха
            de = np.linalg.norm(P - np.array([sx * 0.079, 1.690, 0.030]), axis=1)
            f = np.minimum(f, de - ear)
        return np.where(head, f, -1.0)

    def scalp_v(self, v, **kw):
        # То же поле на вершинах куска (словарь параметров Part)
        P = np.stack([v["x"], v["y"], v["z"]], 1)
        return self.scalp(P, self.is_head[v["dom"]] | (v["y"] > 1.58), **kw)

    # ---- оси вершины тела: начало, нормаль, касательная (на соседа)
    def frame(self, shape, a):
        V, N, S, Ns = shape[:4]
        o = S[a]
        z = Ns[a] / np.linalg.norm(Ns[a], axis=1, keepdims=True)
        t = S[self.nb[a]] - o
        x = t - z * (t * z).sum(1, keepdims=True)
        x /= np.linalg.norm(x, axis=1, keepdims=True)
        y = np.cross(z, x)
        return o, x, y, z

    def nearest(self, P, cand=None):
        # Ближняя вершина сглаженного тела стенда и высота над ним по её нормали
        cand = self.cand if cand is None else cand
        best = np.zeros(len(P), np.int64)
        for s in range(0, len(P), 256):
            p = P[s:s + 256]
            d = ((p[:, None, :] - self.S[cand][None, :, :]) ** 2).sum(2)
            best[s:s + 256] = cand[np.argmin(d, 1)]
        h = ((P - self.S[best]) * self.Ns[best]).sum(1)
        return best, h

    def clearance(self, idx, head=0.006, body=0.03):
        """Запас над телом: у головы — head (над шапкой волос), ниже — body (одежда: рубаха, куртка). Капюшон
        с шарфом длинным волосам не по пути: при капюшоне коса и хвост не выбираются (src/view/wear.cpp)"""
        return np.where(self.is_head[idx], head, body)


def strand_mesh(G, width, thick, out, sides=6, ridge=0.14, twist=0.0, root_cap=True):
    """Жгут вдоль направляющей G (n × 3): сечение — эллипс width × thick (плоской стороной к out), рёбра ridge —
    волнистый край сечения (борозды вдоль пряди), конец — остриё. Возвращает вершины, грани, номер кольца вершины
    (у острия — последнее), долю длины и сторону (sin угла: + — наружу, − — к телу)"""
    n = len(G)
    t = np.zeros_like(G)
    t[1:-1] = G[2:] - G[:-2]
    t[0] = G[1] - G[0]
    t[-1] = G[-1] - G[-2]
    t /= np.maximum(np.linalg.norm(t, axis=1, keepdims=True), 1e-12)
    o = out - t * (out * t).sum(1, keepdims=True)
    o /= np.maximum(np.linalg.norm(o, axis=1, keepdims=True), 1e-12)
    s = np.cross(t, o)
    seg = np.linalg.norm(G[1:] - G[:-1], axis=1)
    u = np.concatenate([[0.0], np.cumsum(seg)])
    u = u / max(u[-1], 1e-9)
    ang = TAU * np.arange(sides) / sides
    V, ring, along, side = [], [], [], []
    for i in range(n - 1):
        a = ang + twist * u[i]
        r = 1.0 + ridge * np.cos(np.arange(sides) * np.pi)
        P = G[i] + (s[i] * (0.5 * width[i] * np.cos(a) * r)[:, None] + o[i] * (0.5 * thick[i] * np.sin(a) * r)[:, None])
        V.append(P)
        ring += [i] * sides
        along += [u[i]] * sides
        side += list(np.sin(a))
    V.append(G[-1:])
    ring.append(n - 2)
    along.append(1.0)
    side.append(0.0)
    V = np.concatenate(V)
    T = []
    for i in range(n - 2):
        for k in range(sides):
            a0, a1 = i * sides + k, i * sides + (k + 1) % sides
            b0, b1 = a0 + sides, a1 + sides
            T += [[a0, b0, b1], [a0, b1, a1]]
    tip = len(V) - 1
    last = (n - 2) * sides
    for k in range(sides):
        T.append([last + k, tip, last + (k + 1) % sides])
    if root_cap:
        c = len(V)
        V = np.concatenate([V, G[:1]])
        ring.append(0)
        along.append(0.0)
        side.append(0.0)
        for k in range(sides):
            T.append([c, k, (k + 1) % sides])
    T = np.array(T, np.int64)
    # Обход наружу: середина грани дальше от оси, чем её точка на оси
    fn = np.cross(V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]])
    rr = np.array(ring)
    axis_p = G[np.clip(rr[T].min(1), 0, n - 1)]
    flip = ((V[T].mean(1) - axis_p) * fn).sum(1) < 0
    T[flip] = T[flip][:, ::-1]
    return V, T, np.array(ring), np.array(along), np.array(side)


def card_mesh(G, width, out, curl=0.22, across=4):
    """Карточка вдоль направляющей G (n × 3): полоса шириной width (n) поперёк, выгнута наружу (out) на curl доли
    ширины посередине — объём пряди. Вершины рядами по across поперёк; u — доля поперёк (0..1), v — доля длины
    (0 — корень, 1 — кончик). Возвращает вершины, грани, номер точки направляющей вершины, u, v"""
    n = len(G)
    t = np.zeros_like(G)
    t[1:-1] = G[2:] - G[:-2]
    t[0] = G[1] - G[0]
    t[-1] = G[-1] - G[-2]
    t /= np.maximum(np.linalg.norm(t, axis=1, keepdims=True), 1e-12)
    o = out - t * (out * t).sum(1, keepdims=True)
    o /= np.maximum(np.linalg.norm(o, axis=1, keepdims=True), 1e-12)
    s = np.cross(t, o)
    seg = np.linalg.norm(G[1:] - G[:-1], axis=1)
    v = np.concatenate([[0.0], np.cumsum(seg)])
    v = v / max(v[-1], 1e-9)
    x = np.linspace(-1.0, 1.0, across)
    V = (G[:, None, :] + s[:, None, :] * (0.5 * width[:, None, None] * x[None, :, None])
         + o[:, None, :] * (curl * width[:, None, None] * (1.0 - x[None, :, None] ** 2) - 0.5 * curl * width[:, None, None]))
    V = V.reshape(-1, 3)
    ring = np.repeat(np.arange(n), across)
    uu = np.tile((x + 1.0) * 0.5, n)
    vv = np.repeat(v, across)
    T = []
    for i in range(n - 1):
        for k in range(across - 1):
            a0, a1 = i * across + k, i * across + k + 1
            b0, b1 = a0 + across, a1 + across
            T += [[a0, b0, b1], [a0, b1, a1]]
    T = np.array(T, np.int64)
    # Обход: нормаль грани — наружу (out)
    fn = np.cross(V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]])
    flip = (fn * o[ring[T[:, 0]]]).sum(1) < 0
    T[flip] = T[flip][:, ::-1]
    return V, T, ring, uu, vv


def grow(hd, roots, d0, length, step=0.012, gravity=0.35, stiff=0.85, pull=None, flat=0.0, head=0.006,
         body=0.03, curl=None):
    """Направляющие от корней: шаг за шагом направление = жёсткость × прежнее + тяжесть + тяга pull(P) (к месту
    завязки) + curl(P, i) (завиток); упор — запас над телом (Head.clearance). flat — прижим к телу (у зачёсанных назад
    волос пряди лежат по голове). Длина у каждой своя. Возвращает список направляющих (n_i × 3)"""
    nL = len(roots)
    nmax = int(np.ceil(length.max() / step)) + 1
    P = roots.copy()
    D = d0 / np.linalg.norm(d0, axis=1, keepdims=True)
    out = [P.copy()]
    g = np.array([0.0, -1.0, 0.0])
    for i in range(1, nmax):
        f = D * stiff + g * gravity
        if pull is not None:
            f = f + pull(P)
        if curl is not None:
            f = f + curl(P, i)
        f /= np.linalg.norm(f, axis=1, keepdims=True)
        Pn = P + f * step
        idx, h = hd.nearest(Pn)
        c = hd.clearance(idx, head, body)
        N = hd.Ns[idx]
        Pn = Pn + N * np.maximum(0.0, c - h)[:, None]
        if flat:
            Pn = Pn - N * (flat * np.maximum(0.0, h - c))[:, None]
        D = Pn - P
        D /= np.maximum(np.linalg.norm(D, axis=1, keepdims=True), 1e-12)
        P = Pn
        out.append(P.copy())
    A = np.stack(out, 1)  # [прядь][шаг]
    res = []
    for k in range(nL):
        n = max(3, int(np.ceil(length[k] / step)) + 1)
        res.append(A[k, :n])
    return res


class Hair(W.Garment):
    """Вещь волос: куски над телом (шапка, брови, борода — Part), жёсткие детали (завязки) и свободные вершины
    прядей, привязанные к ближним вершинам тела."""

    def __init__(self, name, materials, head):
        super().__init__(name, materials)
        self.hd = head
        self.free = []  # пряди: вершины, грани, материалы, затенение, привязка

    def add_strands(self, items):
        for it in items:
            self.free.append(it)

    def strand(self, G, width, thick, mat, out=None, sides=6, ridge=0.14, twist=0.0, ao=(0.55, 1.0), m1=None,
               blend=0.0):
        hd = self.hd
        if out is None:
            idx, _ = hd.nearest(G)
            out = hd.Ns[idx]
        V, T, ring, along, side = strand_mesh(G, width, thick, out, sides, ridge, twist)
        # Затенение: сторона к телу темнее, корень темнее, глубже к телу — темнее
        idx, h = hd.nearest(V)
        depth = np.clip(h / 0.03, 0.0, 1.0)
        a = ao[0] + (ao[1] - ao[0]) * (0.5 + 0.5 * side) * (0.6 + 0.4 * depth)
        a *= 0.75 + 0.25 * ss(0.0, 0.25, along)
        self.free.append(dict(V=V, T=T, ring=ring, mat=np.full(len(T), mat, np.int64),
                              m1=np.full(len(V), mat if m1 is None else m1, np.int64),
                              blend=np.zeros(len(V)) + blend, ao=a, G=G))

    def card(self, G, width, mat, col, out=None, curl=0.22, ao=(0.7, 1.0), across=4):
        """Карточка с атласом прядей (docs/hair.md, «Карточки»): полоса по направляющей G, развёртка — столбец col
        атласа (CARD_COLS столбцов, корень наверху); затенение вершины — глубже к телу темнее, у корня темнее"""
        hd = self.hd
        if out is None:
            idx, _ = hd.nearest(G)
            out = hd.Ns[idx]
        V, T, ring, uu, vv = card_mesh(G, width, out, curl, across)
        idx, h = hd.nearest(V)
        depth = np.clip(h / 0.03, 0.0, 1.0)
        a = (ao[0] + (ao[1] - ao[0]) * (0.4 + 0.6 * depth)) * (0.8 + 0.2 * ss(0.0, 0.2, vv))
        u0 = (col + CARD_PAD) / CARD_COLS
        u1 = (col + 1 - CARD_PAD) / CARD_COLS
        uv = np.stack([u0 + (u1 - u0) * uu, CARD_V0 + (CARD_V1 - CARD_V0) * vv], 1)
        self.free.append(dict(V=V, T=T, ring=ring, mat=np.full(len(T), mat, np.int64),
                              m1=np.full(len(V), mat, np.int64), blend=np.zeros(len(V)), ao=a, G=G, uv=uv))

    def rigid_free(self, V, T, mat, ao=1.0, anchor=None):
        # Жёсткая деталь на прядях (завязка, бусина): вершины свободные, держатся за ближнюю вершину тела
        self.free.append(dict(V=V, T=T, ring=np.zeros(len(V), np.int64), mat=np.full(len(T), mat, np.int64),
                              m1=np.full(len(V), mat, np.int64), blend=np.zeros(len(V)),
                              ao=np.full(len(V), ao), G=V.mean(0, keepdims=True) if anchor is None else anchor))

    # --- сборка: после частей Garment — свободные вершины
    def topology(self):
        super().topology()
        hd = self.hd
        base = base0 = len(self.VM)
        FA, FL, VM, TT, TM, WB, UV = [], [], [], [], [], [], []
        shape = self.body.shape(H.BASE, rest=False)  # пряди заданы в A-позе, их оси — там же
        for it in self.free:
            V, G, ring = it["V"], it["G"], it["ring"]
            # Привязка: кольцо держится за вершину тела, ближнюю к точке направляющей
            gi, _ = hd.nearest(G)
            a = gi[np.clip(ring, 0, len(G) - 1)]
            o, x, y, z = hd.frame(shape, a)
            d = V - o
            FA.append(a)
            FL.append(np.stack([(d * x).sum(1), (d * y).sum(1), (d * z).sum(1)], 1))
            m = np.zeros((len(V), 4), np.int64)
            m[:, 0] = it["mat"][0]
            m[:, 1] = it["m1"]
            m[:, 2] = np.rint(np.clip(it["blend"], 0, 1) * 255)
            m[:, 3] = np.rint(np.clip(it["ao"], 0, 1) * 255)
            VM.append(m)
            TT.append(it["T"] + base)
            TM.append(it["mat"])
            WB.append((a, gi, ring))
            UV.append(it.get("uv", np.zeros((len(V), 2))))
            base += len(V)
        if not self.free:
            self.FA = np.zeros(0, np.int64)
            self.WB = []
            return
        self.FA = np.concatenate(FA)
        self.FL = np.concatenate(FL)
        self.WB = WB
        self.VM = np.concatenate([self.VM] + VM)
        self.Tc = np.concatenate([self.Tc] + TT)
        self.Tm = np.concatenate([self.Tm] + TM)
        # Развёртка — только у вещей с карточками; у шапки и жгутов нули (их материал карт не читает)
        if any("uv" in it for it in self.free):
            self.UV = np.concatenate([np.zeros((base0, 2))] + UV)

    def positions(self, shape):
        p = super().positions(shape)
        if not len(self.FA):
            return p
        o, x, y, z = self.hd.frame(shape, self.FA)
        L = self.FL
        q = o + x * L[:, 0:1] + y * L[:, 1:2] + z * L[:, 2:3]
        return np.concatenate([p, q])

    def more_weights(self, s):
        # Веса прядей: у вершины тела кольца, только кости FOLLOW; вдоль пряди сглажены (переход с головы на
        # спину — плавный, без излома при повороте головы)
        W_ = self.body.W[s]
        order = self.body.order[s]
        keep = np.array([n in FOLLOW for n in order])
        out = []
        for a, gi, ring in self.WB:
            wr = W_[gi] * keep[None, :]
            sm = wr.sum(1, keepdims=True)
            wr = np.where(sm > 1e-9, wr / np.maximum(sm, 1e-12), W_[gi])
            for _ in range(6):
                wr[1:-1] = 0.25 * wr[:-2] + 0.5 * wr[1:-1] + 0.25 * wr[2:]
            out.append(wr[np.clip(ring, 0, len(gi) - 1)])
        return out


# ---------------------------------------------------------------- помощники рецептов
def roots(hd, field, spacing, rng, margin=0.004):
    """Корни прядей: вершины тела внутри поля (field > margin), не ближе spacing друг к другу (бросок дротиков
    по порядку из rng). Возвращает номера вершин тела"""
    cand = np.where(field > margin)[0]
    cand = cand[np.isin(cand, hd.b.used)]
    order = np.argsort(np.array([rng.u() for _ in range(len(cand))]), kind="stable")
    P = hd.S[cand[order]]
    took = []
    for k in range(len(P)):
        if took and (np.linalg.norm(P[took] - P[k], axis=1) < spacing).any():
            continue
        took.append(k)
    return cand[order[took]]


def lift(hd, idx, h):
    # Точка над сглаженным телом стенда на высоте h
    return hd.S[idx] + hd.Ns[idx] * np.asarray(h)[..., None] if np.ndim(h) else hd.S[idx] + hd.Ns[idx] * h


def tangent_to(hd, idx, target):
    # Направление по коже от вершины к точке target
    d = target - hd.S[idx]
    n = hd.Ns[idx]
    d = d - n * (d * n).sum(1, keepdims=True)
    return d / np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-12)


def cut_at(G, target, r):
    # Направляющая до первой точки ближе r к target (не короче трёх точек)
    d = np.linalg.norm(G - target, axis=1)
    hit = np.where(d < r)[0]
    n = max(3, hit[0] + 1) if len(hit) else len(G)
    return G[:n]


def ring_mesh(c, axis, r_in, r_out, h, sides=10):
    """Кольцо-завязка: труба вокруг оси axis в точке c, радиусы r_in..r_out, высота h. Вершины на грань"""
    axis = axis / np.linalg.norm(axis)
    ref = np.array([1.0, 0.0, 0.0]) if abs(axis[0]) < 0.9 else np.array([0.0, 0.0, 1.0])
    s = np.cross(axis, ref)
    s /= np.linalg.norm(s)
    t = np.cross(axis, s)
    ang = TAU * np.arange(sides) / sides
    circ = np.cos(ang)[:, None] * s + np.sin(ang)[:, None] * t
    rings = [c - axis * h / 2 + circ * r_in, c - axis * h / 2 + circ * r_out, c + axis * h / 2 + circ * r_out,
             c + axis * h / 2 + circ * r_in]
    V = np.concatenate(rings)
    T = []
    for a in range(4):
        b_ = (a + 1) % 4
        for k in range(sides):
            k1 = (k + 1) % sides
            T += [[a * sides + k, b_ * sides + k, b_ * sides + k1], [a * sides + k, b_ * sides + k1, a * sides + k1]]
    T = np.array(T, np.int64)
    fn = np.cross(V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]])
    mid = V[T].mean(1)
    radial = mid - c - axis * ((mid - c) @ axis)[:, None]
    # Наружу у внешней стенки, внутрь у внутренней, вдоль оси у торцов
    out = np.where((np.linalg.norm(radial, axis=1) > (r_in + r_out) / 2)[:, None], radial, -radial)
    flat = np.abs((mid - c) @ axis) > h * 0.49
    out[flat] = axis * np.sign((mid[flat] - c) @ axis)[:, None]
    flip = (fn * out).sum(1) < 0
    T[flip] = T[flip][:, ::-1]
    return V, T


def frames(G, hd):
    # Оси вдоль направляющей: касательная, «наружу» (от тела), вбок
    t = np.zeros_like(G)
    t[1:-1] = G[2:] - G[:-2]
    t[0] = G[1] - G[0]
    t[-1] = G[-1] - G[-2]
    t /= np.linalg.norm(t, axis=1, keepdims=True)
    idx, _ = hd.nearest(G)
    o = hd.Ns[idx] - t * (hd.Ns[idx] * t).sum(1, keepdims=True)
    # Сгладить «наружу» вдоль пути: без рывков на стыке головы и спины
    for _ in range(8):
        o[1:-1] = 0.25 * o[:-2] + 0.5 * o[1:-1] + 0.25 * o[2:]
        o -= t * (o * t).sum(1, keepdims=True)
    o /= np.linalg.norm(o, axis=1, keepdims=True)
    return t, o, np.cross(t, o)


def along(hd, pts, step=0.008, head=0.009, body=0.03):
    """Направляющая по опорным точкам (сплайн Кэтмелла — Рома) ровным шагом, с запасом над телом (Head.clearance):
    для прядей, чей путь задан рисунком причёски (чёлка набок, пряди у лица), а не ростом"""
    Q = np.asarray(pts, np.float64)
    Q = np.concatenate([Q[:1] * 2 - Q[1:2], Q, Q[-1:] * 2 - Q[-2:-1]])
    out = []
    for i in range(1, len(Q) - 2):
        p0, p1, p2, p3 = Q[i - 1], Q[i], Q[i + 1], Q[i + 2]
        for t in np.linspace(0.0, 1.0, 16, endpoint=False):
            out.append(0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t
                              + (3 * p1 - p0 - 3 * p2 + p3) * t ** 3))
    out.append(Q[-2])
    G = resample(np.array(out), step)
    for _ in range(3):
        idx, h = hd.nearest(G)
        c = hd.clearance(idx, head, body)
        # Не дальше 1.5 см за шаг: рисунок задан у кожи, а ближняя вершина у макушки может дать ложную глубину
        G = G + hd.Ns[idx] * np.clip(c - h, 0.0, 0.015)[:, None]
    return G


def resample(G, step):
    # Ровный шаг вдоль ломаной
    seg = np.linalg.norm(G[1:] - G[:-1], axis=1)
    u = np.concatenate([[0.0], np.cumsum(seg)])
    n = max(3, int(np.ceil(u[-1] / step)) + 1)
    s = np.linspace(0.0, u[-1], n)
    return np.stack([np.interp(s, u, G[:, k]) for k in range(3)], 1)
