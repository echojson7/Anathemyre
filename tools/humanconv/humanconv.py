#!/usr/bin/env python3
# Люди из MakeHuman (CC0) -> .mesh игры. Офлайн, в игре не нужен.
# Морфы пола и сложения, руки вниз и ноги прямо (поза привязки игры), рост 1.80 м,
# раскраска вершин (кожа, бельё, губы, брови), волосы из оболочки helper-hair, глаза.
#   python3 -I tools/humanconv/humanconv.py <папка MakeHuman> <targets.npz> <data/meshes>
# Папка MakeHuman: base.obj, default.mhskel, default_weights.mhw (makehuman/data из system assets)
import json, struct, sys
import numpy as np

# Кости игры, порядок как в src/sim/body.h
(ROOT, PELVIS, SPINE, CHEST, NECK, HEAD, CLAV_L, UA_L, FA_L, HAND_L, CLAV_R, UA_R, FA_R, HAND_R,
 TH_L, SH_L, FT_L, TH_R, SH_R, FT_R) = range(20)
MAT_CLOTH, MAT_SKIN = 3, 7
HEIGHT = 1.80

# Сложение и цвета как у нашей тройки (src/main.c human_shape), чтобы сравнивать честно
PEOPLE = [
    dict(name='mh_male', gender=1.0, muscle=0.8, weight=0.5, hair='short', beard=False,
         skin=(0.52, 0.37, 0.28), hcol=(0.10, 0.08, 0.06), under=(0.30, 0.29, 0.27),
         mods={'torso/torso-muscle-pectoral-incr': 0.5,
               'measure/measure-upperarm-length-decr': 0.4, 'measure/measure-lowerarm-length-decr': 0.4}),
    # Песочные часы: талия узкая, бёдра и плечи шире, грудь больше и ближе друг к другу, лобок ниже
    dict(name='mh_female', gender=0.0, muscle=0.5, weight=0.55, hair='long', beard=False,
         skin=(0.60, 0.45, 0.36), hcol=(0.20, 0.13, 0.08), under=(0.30, 0.29, 0.27),
         cup=0.65, firm=0.7,
         mods={'measure/measure-waist-circ-decr': 1.0, 'bodyshapes/bodyshapes-elvs-fem-neat-hourglass': 0.8,
               'measure/measure-hips-circ-incr': 0.1, 'buttocks/buttocks-volume-decr': 1.0, 'measure/measure-shoulder-dist-incr': 0.35,
               'breast/breast-dist-decr': 0.15, 'pelvis/bulge-decr': 1.0}),
    dict(name='mh_thin', gender=1.0, muscle=0.45, weight=0.15, hair='bald', beard=True,
         skin=(0.38, 0.26, 0.19), hcol=(0.07, 0.06, 0.05), under=(0.30, 0.29, 0.27),
         mods={'torso/torso-muscle-pectoral-incr': 0.4,
               'measure/measure-upperarm-length-decr': 0.4, 'measure/measure-lowerarm-length-decr': 0.4}),
]


def load_obj(path):
    v, faces, g = [], {}, None
    for l in open(path):
        if l.startswith('v '):
            v.append([float(x) for x in l.split()[1:4]])
        elif l.startswith('g '):
            g = l.split()[1]
        elif l.startswith('f '):
            faces.setdefault(g, []).append([int(t.split('/')[0]) - 1 for t in l.split()[1:]])
    return np.array(v), {k: np.array(f) for k, f in faces.items()}


def tri(q):
    return np.concatenate([q[:, [0, 1, 2]], q[:, [0, 2, 3]]])


# Макро-морфы MakeHuman: вес соседних категорий по линейной интерполяции
def three(x):
    lo, hi = max(0.0, 1 - 2 * x), max(0.0, 2 * x - 1)
    return {'min': lo, 'average': 1 - lo - hi, 'max': hi}


def morph(base, Z, p):
    v = base.copy()

    def add(name, w):
        if w <= 1e-6:
            return
        i = Z['targets/' + name + '.index']
        if len(i):
            v[i] += w * Z['targets/' + name + '.vector'].astype(np.float64) * 1e-3
    gen = {'female': 1 - p['gender'], 'male': p['gender']}
    for gn, gw in gen.items():
        for mn, mw in three(p['muscle']).items():
            for wn, ww in three(p['weight']).items():
                add('macrodetails/universal-%s-young-%smuscle-%sweight' % (gn, mn, wn), gw * mw * ww)
        for race in ('african', 'asian', 'caucasian'):
            add('macrodetails/%s-%s-young' % (race, gn), gw / 3)
    # Размер и упругость груди (у MakeHuman только для женщин)
    gw = 1 - p['gender']
    for mn, mw in three(p['muscle']).items():
        for wn, ww in three(p['weight']).items():
            for cn, cw in three(p.get('cup', 0.5)).items():
                for fn, fw in three(p.get('firm', 0.5)).items():
                    if cn != 'average' or fn != 'average':
                        add('breast/female-young-%smuscle-%sweight-%scup-%sfirmness' % (mn, wn, cn, fn), gw * mw * ww * cw * fw)
    for name, w in p.get('mods', {}).items():
        add(name, w)
    return v


def our_bone(n):
    r = n.endswith('.R')
    b = n.split('.')[0]
    side = lambda l, rr: rr if r else l
    if b.startswith(('finger', 'metacarpal', 'wrist')): return side(HAND_L, HAND_R)
    if b.startswith('lowerarm'): return side(FA_L, FA_R)
    if b.startswith('upperarm'): return side(UA_L, UA_R)
    if b.startswith(('clavicle', 'shoulder')): return side(CLAV_L, CLAV_R)
    if b.startswith('upperleg'): return side(TH_L, TH_R)
    if b.startswith('lowerleg'): return side(SH_L, SH_R)
    if b.startswith(('foot', 'toe')): return side(FT_L, FT_R)
    if b in ('root', 'pelvis', 'spine05'): return PELVIS
    if b in ('spine04', 'spine03'): return SPINE
    if b in ('spine02', 'spine01', 'breast'): return CHEST
    if b.startswith('neck'): return NECK
    return HEAD


def rot_to(a, b):
    a = a / np.linalg.norm(a); b = b / np.linalg.norm(b)
    c = np.cross(a, b); s = np.linalg.norm(c); d = float(np.dot(a, b))
    if s < 1e-9:
        return np.eye(3)
    k = c / s
    K = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    return np.eye(3) + s * K + (1 - d) * K @ K


def half(R):
    # Половина поворота (для дельтовидной кости между ключицей и плечом)
    ang = np.arccos(np.clip((np.trace(R) - 1) / 2, -1, 1))
    if ang < 1e-6:
        return np.eye(3)
    ax = np.array([R[2, 1] - R[1, 2], R[0, 2] - R[2, 0], R[1, 0] - R[0, 1]]) / (2 * np.sin(ang))
    K = np.array([[0, -ax[2], ax[1]], [ax[2], 0, -ax[0]], [-ax[1], ax[0], 0]])
    h = ang / 2
    return np.eye(3) + np.sin(h) * K + (1 - np.cos(h)) * K @ K


def repose(v, J, W, names):
    # Руки по направлению костей игры (body.c), ноги прямые, стопы и кисти жёстко
    xf = {}  # имя кости MH -> (R, точка до, точка после)
    for sd, sg in (('L', 1), ('R', -1)):
        S0, E0, W0 = J['upperarm01.%s____head' % sd], J['lowerarm01.%s____head' % sd], J['wrist.%s____head' % sd]
        R_ua = rot_to(E0 - S0, np.array([0.03 * sg, -0.28, 0.0]))
        E1 = S0 + R_ua @ (E0 - S0)
        R_fa = rot_to(R_ua @ (W0 - E0), np.array([0.01 * sg, -0.21, 0.01])) @ R_ua
        W1 = E1 + R_fa @ (W0 - E0)
        H0, K0, A0 = J['upperleg01.%s____head' % sd], J['lowerleg01.%s____head' % sd], J['foot.%s____head' % sd]
        down = np.array([0.0, -1.0, 0.0])
        # Ноги сводятся наполовину: если поставить их отвесно, бёдра у паха входят друг в друга
        R_th = half(rot_to(K0 - H0, down))
        K1 = H0 + R_th @ (K0 - H0)
        R_sh = rot_to(R_th @ (A0 - K0), R_th @ (K0 - H0)) @ R_th
        A1 = K1 + R_sh @ (A0 - K0)
        for n in names:
            if not n.endswith('.' + sd):
                continue
            b = n.split('.')[0]
            if b.startswith('shoulder'): xf[n] = (half(R_ua), S0, S0)
            elif b.startswith('upperarm'): xf[n] = (R_ua, S0, S0)
            elif b.startswith('lowerarm'): xf[n] = (R_fa, E0, E1)
            elif b.startswith(('wrist', 'finger', 'metacarpal')): xf[n] = (R_fa, W0, W1)
            elif b.startswith('upperleg'): xf[n] = (R_th, H0, H0)
            elif b.startswith('lowerleg'): xf[n] = (R_sh, K0, K1)
            elif b.startswith(('foot', 'toe')): xf[n] = (np.eye(3), A0, A1)
    out = np.zeros_like(v)
    wsum = np.zeros(len(v))
    for n, lst in W.items():
        a = np.array(lst)
        i, w = a[:, 0].astype(int), a[:, 1]
        R, p0, p1 = xf.get(n, (np.eye(3), np.zeros(3), np.zeros(3)))
        out[i] += w[:, None] * ((v[i] - p0) @ R.T + p1)
        wsum[i] += w
    free = wsum < 1e-6
    out[~free] /= wsum[~free, None]
    out[free] = v[free]
    return out


def tail(a, b, r0, r1, rings=9, segs=10):
    # Сужающаяся трубка a->b, на конце сведена в точку
    ax = (b - a) / np.linalg.norm(b - a)
    u = np.cross(ax, [1.0, 0, 0]); u /= np.linalg.norm(u)
    w = np.cross(ax, u)
    pv, pn = [], []
    for i in range(rings):
        k = i / (rings - 1)
        r = (r0 + (r1 - r0) * k) * (1.0 if i < rings - 1 else 0.3) * (1.25 if i == 1 else 1.0)
        c = a + (b - a) * k
        for j in range(segs):
            ang = 2 * np.pi * j / segs
            d = np.cos(ang) * u + np.sin(ang) * w
            pv.append(c + r * d); pn.append(d)
    pt = []
    for i in range(rings - 1):
        for j in range(segs):
            p0, p1 = i * segs + j, i * segs + (j + 1) % segs
            pt += [[p0, p1, p1 + segs], [p0, p1 + segs, p0 + segs]]
    return np.array(pv), np.array(pn), np.array(pt)


def subdivide(v, F):
    # Один шаг Catmull-Clark для четырёхугольников; вершины вне F не меняются.
    # Новые вершины: точки рёбер (nv + e), затем точки граней (nv + E + f)
    nv, nf = len(v), len(F)
    ef = np.stack([F, np.roll(F, -1, 1)], 2).reshape(-1, 2)
    E, einv, ecnt = np.unique(np.sort(ef, 1), axis=0, return_inverse=True, return_counts=True)
    einv = einv.reshape(nf, 4)
    fp = v[F].mean(1)
    mid = v[E].mean(1)
    fsum = np.zeros((len(E), 3)); np.add.at(fsum, einv.ravel(), np.repeat(fp, 4, 0))
    bnd = ecnt == 1
    ep = np.where(bnd[:, None], mid, (2 * mid + fsum) / (2 + ecnt)[:, None])
    Q = np.zeros((nv, 3)); nq = np.zeros(nv)
    np.add.at(Q, F.ravel(), np.repeat(fp, 4, 0)); np.add.at(nq, F.ravel(), 1)
    R = np.zeros((nv, 3)); nr = np.zeros(nv)
    np.add.at(R, E[:, 0], mid); np.add.at(R, E[:, 1], mid); np.add.at(nr, E.ravel(), 1)
    vn = v.copy()
    inn = nq > 0
    k = nr[inn]
    vn[inn] = (Q[inn] / nq[inn, None] + 2 * R[inn] / k[:, None] + (k - 3)[:, None] * v[inn]) / k[:, None]
    # Край (вырезы глаз, рта): 3/4 вершины + 1/8 соседей по краю
    be = E[bnd]
    bs = np.zeros((nv, 3)); bc = np.zeros(nv)
    np.add.at(bs, be[:, 0], v[be[:, 1]]); np.add.at(bs, be[:, 1], v[be[:, 0]]); np.add.at(bc, be.ravel(), 1)
    onb = bc == 2
    vn[onb] = 0.75 * v[onb] + 0.125 * bs[onb]
    ei, fi = nv + einv, nv + len(E) + np.arange(nf)
    nF = np.concatenate([np.stack([F[:, c], ei[:, c], fi, ei[:, c - 1]], 1) for c in range(4)])
    return np.concatenate([vn, ep, fp]), nF, E, F


def normals(v, t):
    n = np.zeros_like(v)
    fn = np.cross(v[t[:, 1]] - v[t[:, 0]], v[t[:, 2]] - v[t[:, 0]])
    for k in range(3):
        np.add.at(n, t[:, k], fn)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)


def build(p, base, faces, Z, skel, W):
    v = morph(base, Z, p)
    J = {k: v[ix].mean(0) for k, ix in skel['joints'].items()}
    v = repose(v, J, W, list(W.keys()))
    J = {k: v[ix].mean(0) for k, ix in skel['joints'].items()}

    body = faces['body']
    used = np.unique(body)
    lo = v[used].min(0)
    s = HEIGHT / (v[used, 1].max() - lo[1])
    v = (v - np.array([0.0, lo[1], 0.0])) * s
    J = {k: (x - np.array([0.0, lo[1], 0.0])) * s for k, x in J.items()}

    # Кость игры и веса нужных групп MH на вершину
    nv = len(v)
    acc = np.zeros((nv, 20))
    wt = {}
    for n, lst in W.items():
        a = np.array(lst)
        i, w = a[:, 0].astype(int), a[:, 1]
        np.add.at(acc[:, our_bone(n)], i, w)
        wt[n] = np.zeros(nv)
        wt[n][i] = w

    # Сглаживание тела: веса новых вершин — среднее исходных
    v, body, E, F0 = subdivide(v, body)
    ext = lambda a: np.concatenate([a, a[E].mean(1), a[F0].mean(1)])
    acc = ext(acc)
    wt = {k: ext(a) for k, a in wt.items()}
    used = np.unique(body)
    nv = len(v)
    bone = acc.argmax(1)
    bone[acc.max(1) < 1e-6] = HEAD
    get = lambda *ns: sum((wt[n] for n in ns if n in wt), np.zeros(nv))

    skin = np.array(p['skin']); hcol = np.array(p['hcol']); under = np.array(p['under'])
    col = np.tile(skin, (nv, 1)); mat = np.full(nv, MAT_SKIN)
    n = normals(v, tri(body))
    x, y, z = v[:, 0], v[:, 1], v[:, 2]
    ax = np.abs(x)

    # Бельё: трусы по линии паха и бёдер, у женщины топ по чашкам груди
    hip = J['upperleg01.L____head'][1]
    trunk = np.isin(bone, [PELVIS, SPINE, TH_L, TH_R])
    isbody = np.zeros(nv, bool); isbody[used] = True
    mid = trunk & isbody & (ax < 0.012) & (y > 0.6) & (y < hip)
    crotch = y[mid].min()
    # У мужчин боксеры: штанина ровно чуть ниже паха; у женщин трусы с вырезом по бедру
    if p['gender'] > 0.5:
        top = hip + 0.065
        leg = crotch - 0.05 + 0.08 * np.maximum(0.0, ax - 0.06)
    else:
        top = hip + 0.08
        leg = crotch + 0.012 + 0.75 * np.maximum(0.0, ax - 0.025)
    briefs = trunk & (y < top) & (y > leg)
    under_m = briefs
    # Резинка по поясу и подгиб штанин темнее
    hem = briefs & ((y > top - 0.016) | (y < leg + 0.009))
    if p['gender'] < 0.5:
        # Топ без лямок: полоса вокруг груди, как бельё в Kenshi
        torso = np.isin(bone, [SPINE, CHEST, CLAV_L, CLAV_R])
        nip = J['breast.L____tail']
        rise = 0.02 * np.clip((z - nip[2] + 0.12) / 0.08, 0, 1)  # спереди край выше, чем на спине
        lo, hi = nip[1] - 0.045, nip[1] + 0.05 + rise
        top_m = torso & (y > lo) & (y < hi)
        under_m = under_m | top_m
        hem = hem | (top_m & ((y < lo + 0.012) | (y > hi - 0.007) | ((ax < 0.008) & (z > 0))))
    col[under_m] = under; mat[under_m] = MAT_CLOTH
    col[hem] = under * 0.72
    # Ткань на 1.2 мм над кожей: по краю виден срез
    v[under_m] += n[under_m] * 0.0012

    if p['gender'] > 0.5:
        # Соски: ореол вокруг самой выступающей точки груди
        for sd in ('L', 'R'):
            nj = J['breast.%s____tail' % sd]
            near = isbody & (np.abs(x - nj[0]) < 0.012) & (np.abs(y - nj[1]) < 0.012)
            c = v[np.where(near)[0][z[near].argmax()]]
            d = np.linalg.norm(v - c, axis=1)
            col[isbody & (d < 0.011)] = skin * np.array([0.80, 0.68, 0.64])
            tip = isbody & (d < 0.004)
            col[tip] = skin * np.array([0.70, 0.56, 0.52])
            v[tip] += n[tip] * 0.0015

    # Губы по весам круговой мышцы рта, брови над глазами
    lips = (get('oris01', 'oris03.L', 'oris03.R', 'oris05', 'oris07.L', 'oris07.R') > 0.8) & (n[:, 2] > 0.2)
    col[lips] = skin * np.array([0.93, 0.84, 0.82])
    for sd, sg in (('L', 1), ('R', -1)):
        e = J['eye.%s____head' % sd]
        dx = (x - e[0]) * sg
        arch = 0.004 * (1 - ((dx - 0.004) / 0.022) ** 2)
        brow = (bone == HEAD) & (dx > -0.016) & (dx < 0.026) & (y > e[1] + 0.012 + arch) & (y < e[1] + 0.021 + arch) \
            & (z > e[2] - 0.005) & (n[:, 2] > 0.3)
        col[brow] = skin * 0.25 + hcol * 0.75
    if p['beard']:
        # Борода и усы: щетина объёмом 3 мм по нижней части лица
        eye_y = J['eye.L____head'][1]
        mouth = J['oris01____head'] if 'oris01____head' in J else J['jaw____tail']
        jaw = (bone == HEAD) | (bone == NECK)
        neck_y = J['neck02____head'][1]
        beard = jaw & (y < eye_y - 0.045) & (y > neck_y - 0.01 + 0.06 * np.clip(-(z - mouth[2]) / 0.07, 0, 1)) \
            & (z > J['head____head'][2] - 0.005) & ~lips
        v[beard] += n[beard] * 0.003
        col[beard] = skin * 0.15 + hcol * 0.85
    # Линия роста волос: высоко надо лбом, ниже у висков, у затылка до шеи; уши открыты
    hd = (bone == HEAD) & isbody
    top_y = y[hd].max()
    crown = hd & (y > top_y - 0.12)
    zc = (z[crown].min() + z[crown].max()) / 2
    side = hd & (y > top_y - 0.19) & (y < top_y - 0.10)
    ear = side & (ax > 0.93 * ax[side].max())
    ear_c = np.array([ax[ear].mean(), y[ear].mean(), z[ear].mean()])
    tz = np.clip((z - zc + 0.05) / 0.13, 0, 1)
    tz = tz * tz * (3 - 2 * tz)
    line = top_y - 0.20 + 0.118 * tz - 0.012 * np.clip(ax / 0.07, 0, 1) * tz
    ear_d = ((ax - ear_c[0]) / 0.03) ** 2 + ((y - ear_c[1]) / 0.04) ** 2 + ((z - ear_c[2]) / 0.032) ** 2
    hair_t = hd & (ear_d > 1) & (y > line)
    hair_k = np.where(hair_t, np.clip((y - line) / 0.015, 0, 1), 0.0)
    if p['hair'] == 'bald':
        col = col * (1 - 0.35 * hair_k[:, None]) + hcol * 0.35 * hair_k[:, None]

    # Цвет и материал по граням: край белья и губ чёткий, без размытия по треугольнику.
    # Грань берёт самую частую окраску своих вершин, вершины делятся по окраске граней
    _, pid = np.unique(np.concatenate([np.round(col, 4), mat[:, None]], 1), axis=0, return_inverse=True)
    pid = pid.ravel()
    q = pid[body]
    fpid = q[np.arange(len(q)), (q[:, :, None] == q[:, None, :]).sum(2).argmax(1)]
    key = body.astype(np.int64) * (pid.max() + 1) + fpid[:, None]
    uk, kinv = np.unique(key.ravel(), return_inverse=True)
    src = uk // (pid.max() + 1)
    pal = np.zeros((pid.max() + 1, 4)); pal[pid] = np.concatenate([col, mat[:, None]], 1)
    kp = pal[uk % (pid.max() + 1)]
    bq = kinv.reshape(-1, 4)
    verts = [(v[src], n[src], kp[:, :3], kp[:, 3].astype(int), bone[src])]
    tris = [tri(bq)]
    count = [len(src)]

    def add_part(pv, pn, pc, pm, pb, f, ix):
        # f — четырёхугольники в номерах исходной сетки (ix — их вершины) или готовые треугольники при ix=None
        if ix is None:
            t = f + count[0]
        else:
            remap = np.full(nv, -1); remap[ix] = np.arange(len(ix)) + count[0]
            t = remap[tri(f)]
        verts.append((pv, pn, pc, np.broadcast_to(pm, len(pv)), np.broadcast_to(pb, len(pv))))
        tris.append(t)
        count[0] += len(pv)

    # Глаза: белок и тёмная радужка по направлению взгляда
    for sd in ('l', 'r'):
        f = faces['helper-%s-eye' % sd]
        ix = np.unique(f)
        ev = v[ix]
        d = ev - (ev.min(0) + ev.max(0)) / 2
        en = d / np.linalg.norm(d, axis=1, keepdims=True)
        ec = np.tile(np.array([0.46, 0.43, 0.39]), (len(ix), 1))
        ec[en[:, 2] > 0.9] = (0.07, 0.05, 0.04)
        add_part(ev, en, ec, MAT_SKIN, HEAD, f, ix)

    # Волосы: копия кожи головы выше линии роста, приподнятая над ней; к линии сходит на нет
    if p['hair'] != 'bald':
        f = body[(hair_t[body]).all(1)]
        ix = np.unique(f)
        # Пряди: толщина и оттенок немного гуляют
        rng = np.random.default_rng(7)
        off = 0.0015 + ((0.006 if p['hair'] == 'short' else 0.004) + rng.uniform(-0.0015, 0.0015, len(ix))) * hair_k[ix]
        hv = v[ix] + n[ix] * off[:, None]
        hb = np.where(hv[:, 1] < J['neck03____head'][1], NECK, HEAD)
        hc = hcol * rng.uniform(0.85, 1.15, (len(ix), 1))
        add_part(hv, n[ix], hc, MAT_CLOTH, hb, f, ix)
        if p['hair'] == 'long':
            # Собранные в хвост: узел на затылке и сужающийся жгут
            back = hd & (y > top_y - 0.12) & (y < top_y - 0.08)
            zb = z[back].min()
            pv, pn, pt = tail(np.array([0.0, top_y - 0.095, zb + 0.004]), np.array([0.0, top_y - 0.33, zb - 0.012]), 0.026, 0.011)
            add_part(pv, pn, np.tile(hcol, (len(pv), 1)), MAT_CLOTH, HEAD, pt, None)

    # Только вершины, на которые ссылаются треугольники
    V = np.concatenate([a[0] for a in verts]); N = np.concatenate([a[1] for a in verts])
    C = np.concatenate([a[2] for a in verts]); M = np.concatenate([a[3] for a in verts])
    B = np.concatenate([a[4] for a in verts]); T = np.concatenate(tris)
    live = np.unique(T)
    remap = np.full(len(V), -1); remap[live] = np.arange(len(live))
    return V[live], N[live], C[live], M[live], B[live], remap[T], J


def write_mesh(path, V, N, C, M, B, T):
    dt = np.dtype([('pos', '<f4', 3), ('nrm', '<f4', 3), ('col', 'u1', 4), ('mat', 'u1'), ('bone', 'u1'), ('pad', 'u1', 2)])
    a = np.zeros(len(V), dt)
    a['pos'] = V; a['nrm'] = N
    a['col'][:, :3] = np.clip(np.round(C * 255), 0, 255); a['col'][:, 3] = 255
    a['mat'] = M; a['bone'] = B
    with open(path, 'wb') as f:
        f.write(b'AMSH' + struct.pack('<III', 1, len(V), T.size))
        f.write(a.tobytes())
        f.write(T.astype('<u4').tobytes())


def main():
    mh, npz, out = sys.argv[1], sys.argv[2], sys.argv[3]
    base, faces = load_obj(mh + '/base.obj')
    Z = np.load(npz, allow_pickle=False)
    skel = json.load(open(mh + '/default.mhskel'))
    W = json.load(open(mh + '/default_weights.mhw'))['weights']
    for p in PEOPLE:
        V, N, C, M, B, T, J = build(p, base, faces, Z, skel, W)
        write_mesh('%s/%s.mesh' % (out, p['name']), V, N, C, M, B, T)
        j = lambda n: J[n + '____head']
        print('%-10s %5d вершин %5d треугольников' % (p['name'], len(V), len(T)))
        print('  плечо %s  локоть %s  запястье %s' % tuple(np.round(j(b + '.L'), 3) for b in ('upperarm01', 'lowerarm01', 'wrist')))
        print('  бедро %s  колено %s  лодыжка %s' % tuple(np.round(j(b + '.L'), 3) for b in ('upperleg01', 'lowerleg01', 'foot')))
        print('  шея %.3f  голова %.3f  ключица %s' % (j('neck01')[1], j('head')[1], np.round(j('clavicle.L'), 3)))


if __name__ == '__main__':
    main()
