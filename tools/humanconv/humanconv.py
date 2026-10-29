#!/usr/bin/env python3
# Люди из MakeHuman (CC0) -> .mesh игры. Офлайн, в игре не нужен.
# Морфы пола и сложения, руки вниз и ноги прямо (поза привязки игры), рост 1.80 м,
# раскраска по граням (кожа, бельё, губы, брови), волосы над кожей головы, глаза.
# Три LOD по LOD_TRIS, упрощение своё, лицо дороже остального.
# Нужен только numpy.
#   python3 -I tools/humanconv/humanconv.py <папка MakeHuman> <targets.npz> <data/meshes>
# Папка MakeHuman: base.obj, default.mhskel, default_weights.mhw (makehuman/data из system assets)
import json, struct, sys
import numpy as np

# Кости игры, порядок как в src/sim/body.h
(ROOT, PELVIS, SPINE, CHEST, NECK, HEAD, CLAV_L, UA_L, FA_L, HAND_L, CLAV_R, UA_R, FA_R, HAND_R,
 TH_L, SH_L, FT_L, TH_R, SH_R, FT_R) = range(20)
MAT_CLOTH, MAT_SKIN = 3, 7
HEIGHT = 1.80
# Треугольников тела на LOD0..2 (без оболочки волос)
LOD_TRIS = (8500, 1800, 500)

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

    # Сглаживание тела для окраски: веса новых вершин — среднее исходных
    vc, nv0 = v.copy(), len(v)
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
    brows = np.zeros(nv, bool)
    for sd, sg in (('L', 1), ('R', -1)):
        e = J['eye.%s____head' % sd]
        dx = (x - e[0]) * sg
        arch = 0.004 * (1 - ((dx - 0.004) / 0.022) ** 2)
        brow = (bone == HEAD) & (dx > -0.016) & (dx < 0.026) & (y > e[1] + 0.012 + arch) & (y < e[1] + 0.021 + arch) \
            & (z > e[2] - 0.005) & (n[:, 2] > 0.3)
        col[brow] = skin * 0.25 + hcol * 0.75
        brows |= brow
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
    else:
        # Под оболочкой волос кожа того же цвета: край оболочки не виден, линия роста — шов окраски
        col[hair_t] = hcol; mat[hair_t] = MAT_CLOTH

    # Окраска по граням: грань берёт самую частую окраску своих вершин.
    # Сглаженная сетка нужна только для точной окраски; LOD0 — исходная сетка MakeHuman,
    # её четырёхугольник берёт самую частую окраску своих четырёх дочерних граней
    eyes = []
    for sd in ('l', 'r'):
        # Глаза: белок и тёмная радужка по направлению взгляда
        f = faces['helper-%s-eye' % sd]
        ix = np.unique(f)
        d = v[ix] - (v[ix].min(0) + v[ix].max(0)) / 2
        n[ix] = d / np.linalg.norm(d, axis=1, keepdims=True)
        col[ix] = (0.46, 0.43, 0.39)
        col[ix[n[ix, 2] > 0.9]] = (0.07, 0.05, 0.04)
        mat[ix] = MAT_SKIN
        eyes.append(f)
    eyes = np.concatenate(eyes)
    pal, pid = np.unique(np.concatenate([np.round(col, 4), mat[:, None]], 1), axis=0, return_inverse=True)
    pid = pid.ravel()
    vote = lambda q: q[np.arange(len(q)), (q[:, :, None] == q[:, None, :]).sum(2).argmax(1)]
    nf0 = len(F0)
    # LOD0 — исходная сетка MakeHuman. Четырёхугольник, где окраска дочерних граней разная,
    # делится на четыре (линейно, без сглаживания); соседи с поделённым ребром — веером из центра.
    # Номера вершин как у сглаженной сетки: точки рёбер nv0 + e, центры граней nv0 + len(E) + f
    vl = np.concatenate([vc, vc[E].mean(1), vc[F0].mean(1)])
    fe_ = np.sort(np.stack([F0, np.roll(F0, -1, 1)], 2).reshape(-1, 2), 1)
    qe = np.unique(fe_, axis=0, return_inverse=True)[1].reshape(nf0, 4)

    def refine(c):
        mixed = (c != c[:, :1]).any(1)
        cut = np.zeros(len(E), bool); cut[qe[mixed].ravel()] = True
        m = np.where(mixed)[0]
        ts = [tri(body[k * nf0 + m]) for k in range(4)]
        ps = [np.tile(c[m, k], 2) for k in range(4)]
        side = cut[qe].any(1)
        plain = ~mixed & ~side
        ts.append(tri(F0[plain])); ps.append(np.tile(c[plain, 0], 2))
        fan = np.where(~mixed & side)[0]
        ctr = nv0 + len(E) + fan
        for k in range(4):
            a, b, e = F0[fan, k], F0[fan, (k + 1) % 4], qe[fan, k]
            h = cut[e]
            ts += [np.stack([ctr[~h], a[~h], b[~h]], 1), np.stack([ctr[h], a[h], nv0 + e[h]], 1), np.stack([ctr[h], nv0 + e[h], b[h]], 1)]
            ps += [c[fan[~h], 0], c[fan[h], 0], c[fan[h], 0]]
        return np.concatenate(ts), np.concatenate(ps)

    epid = vote(pid[eyes])
    T0, P0 = refine(vote(pid[body]).reshape(4, nf0).T)
    T0 = np.concatenate([T0, tri(eyes)]); P0 = np.concatenate([P0, epid, epid])
    n0 = normals(vl, T0)
    n0[np.unique(eyes)] = n[np.unique(eyes)]

    def ponytail(rings, segs):
        back = hd & (y > top_y - 0.12) & (y < top_y - 0.08)
        zb = z[back].min()
        return tail(np.array([0.0, top_y - 0.095, zb + 0.004]), np.array([0.0, top_y - 0.33, zb - 0.012]), 0.026, 0.011, rings, segs)

    # Лицо и голова при упрощении дороже: их вершины уходят последними
    wv = np.where(bone == HEAD, 40.0, np.where(bone == NECK, 8.0, 1.0))
    lods = []
    # LOD0 тоже упрощается, но с полной окраской. Волосы — копия граней кожи головы выше линии роста,
    # приподнятая над ней; к линии сходит на нет
    (T0, P0), = decimate(vl, T0, P0, [LOD_TRIS[0]], wv)
    n0 = normals(vl, T0)
    n0[np.unique(eyes)] = n[np.unique(eyes)]
    parts = [split(vl, n0, T0, P0, pal, bone)]
    if p['hair'] != 'bald':
        f = T0[hair_t[T0].all(1)]
        ix = np.unique(f)
        remap = np.full(len(vl), -1); remap[ix] = np.arange(len(ix))
        # Пряди: толщина и оттенок немного гуляют
        rng = np.random.default_rng(7)
        off = 0.0015 + ((0.006 if p['hair'] == 'short' else 0.004) + rng.uniform(-0.0015, 0.0015, len(ix))) * hair_k[ix]
        hv = vl[ix] + n0[ix] * off[:, None]
        hb = np.where(hv[:, 1] < J['neck03____head'][1], NECK, HEAD)
        hc = hcol * rng.uniform(0.85, 1.15, (len(ix), 1))
        parts.append(((hv, n0[ix], hc, np.full(len(ix), MAT_CLOTH), hb), remap[f]))
        if p['hair'] == 'long':
            # Собранные в хвост: узел на затылке и сужающийся жгут
            pv, pn, pt = ponytail(7, 8)
            parts.append(((pv, pn, np.tile(hcol, (len(pv), 1)), np.full(len(pv), MAT_CLOTH), np.full(len(pv), HEAD)), pt))
    lods.append(merge(parts))

    # Дальние LOD: своё упрощение с учётом швов окраски (decimate). Окраска проще:
    # кожа, бельё без резинки, губы, брови, борода, волосы — окраска кожи головы.
    # Глаза упрощаются отдельно и не меньше чем до 24 треугольников: иначе уходят под веки
    fcol = np.where(under_m[:, None], under, skin)
    fcol[lips] = col[lips]; fcol[brows] = col[brows]
    fmat = np.where(under_m, MAT_CLOTH, MAT_SKIN)
    if p['beard']:
        fcol[beard] = skin * 0.15 + hcol * 0.85
    if p['hair'] != 'bald':
        fcol[hair_t] = hcol; fmat[hair_t] = MAT_CLOTH
    ie = np.unique(eyes)
    fcol[ie] = col[ie]; fmat[ie] = MAT_SKIN
    fpal, fpid = np.unique(np.concatenate([np.round(fcol, 4), fmat[:, None]], 1), axis=0, return_inverse=True)
    fpid = fpid.ravel()
    fe = vote(fpid[eyes])
    Tf, Pf = refine(vote(fpid[body]).reshape(4, nf0).T)
    rs = ((5, 6), (4, 4))
    eye_n = (64, 40)
    tail_n = [(r - 1) * s * 2 if p['hair'] == 'long' else 0 for r, s in rs]
    body_l = decimate(vl, Tf, Pf, [LOD_TRIS[l + 1] - tail_n[l] - eye_n[l] for l in range(2)], wv)
    eye_l = decimate(vl, tri(eyes), np.concatenate([fe, fe]), list(eye_n), wv)
    for (rings, segs), (tb, pb), (te, pe) in zip(rs, body_l, eye_l):
        t, fp = np.concatenate([tb, te]), np.concatenate([pb, pe])
        parts = [split(vl, normals(vl, t), t, fp, fpal, bone)]
        if p['hair'] == 'long':
            pv, pn, pt = ponytail(rings, segs)
            parts.append(((pv, pn, np.tile(hcol, (len(pv), 1)), np.full(len(pv), MAT_CLOTH), np.full(len(pv), HEAD)), pt))
        lods.append(merge(parts))
    return lods, J


def decimate(v, T, fp, targets, wv):
    # Схлопывание рёбер по квадрикам (Garland–Heckbert), вершина уходит в соседа (half-edge).
    # Швы окраски и края сетки: вершина шва идёт только вдоль шва, угол шва неподвижен,
    # поэтому окраска не рвётся на треугольники. Для каждой цели — треугольники и их окраска
    import heapq
    from collections import defaultdict
    nv = len(v)
    vh = np.concatenate([v, np.ones((nv, 1))], 1)
    alive = np.ones(len(T), bool)
    vf = [set() for _ in range(nv)]
    for f, t in enumerate(T):
        for a in t:
            vf[a].add(f)
    Q = np.zeros((nv, 4, 4))
    fn = np.cross(v[T[:, 1]] - v[T[:, 0]], v[T[:, 2]] - v[T[:, 0]])
    area = np.linalg.norm(fn, axis=1)
    nn = fn / np.maximum(area, 1e-20)[:, None]
    pl = np.concatenate([nn, -(nn * v[T[:, 0]]).sum(1, keepdims=True)], 1)
    fq = area[:, None, None] * pl[:, :, None] * pl[:, None, :]
    for k in range(3):
        np.add.at(Q, T[:, k], fq)
    ef = defaultdict(list)
    for f, t in enumerate(T):
        for i in range(3):
            a, b = int(t[i]), int(t[(i + 1) % 3])
            ef[(min(a, b), max(a, b))].append(f)
    seam = set()
    for e, fs_ in ef.items():
        if len(fs_) != 2 or fp[fs_[0]] != fp[fs_[1]]:
            seam.add(e)
            # Плоскость вдоль шва поперёк грани держит шов на месте
            for f in fs_:
                d = v[e[1]] - v[e[0]]
                m = np.cross(d, nn[f]); m /= max(np.linalg.norm(m), 1e-20)
                q = np.append(m, -m @ v[e[0]])
                Q[list(e)] += 20.0 * (d @ d) * np.outer(q, q)
    Q *= wv[:, None, None]
    sn = defaultdict(set)
    for a, b in seam:
        sn[a].add(b); sn[b].add(a)

    def nbr(a):
        s = set()
        for f in vf[a]:
            s.update(T[f].tolist())
        s.discard(a)
        return s

    def allowed(a):
        # Куда можно увести вершину a
        if a in sn:
            return sn[a] if len(sn[a]) == 2 and len({fp[f] for f in vf[a]}) <= 2 else set()
        return nbr(a)

    def valid(a, b):
        shared = [f for f in vf[a] if b in T[f]]
        opp = {int(x) for f in shared for x in T[f] if x != a and x != b}
        if nbr(a) & nbr(b) != opp:
            return False
        for f in vf[a]:
            if b in T[f]:
                continue
            t = T[f].copy(); t[t == a] = b
            n1 = np.cross(v[t[1]] - v[t[0]], v[t[2]] - v[t[0]])
            l1 = np.linalg.norm(n1)
            if l1 < 1e-12 or n1 @ nn[f] < 0.3 * l1:
                return False
        return True

    ver = np.zeros(nv, int)
    heap = []

    def push(a):
        c = sorted((vh[b] @ (Q[a] + Q[b]) @ vh[b], b) for b in allowed(a))
        for cost, b in c[:4]:
            if valid(a, b):
                heapq.heappush(heap, (cost, a, b, ver[a]))
                return

    for a in range(nv):
        if vf[a]:
            push(a)
    count = int(alive.sum())
    out = []
    for target in targets:
        while count > target and heap:
            _, a, b, va = heapq.heappop(heap)
            if va != ver[a] or not vf[a] or b not in allowed(a) or not valid(a, b):
                continue
            for f in list(vf[a]):
                if b in T[f]:
                    alive[f] = False
                    for x in T[f]:
                        vf[x].discard(f)
                    count -= 1
                else:
                    T[f][T[f] == a] = b
                    vf[b].add(f)
            vf[a] = set()
            Q[b] += Q[a]
            if a in sn:
                for c in sn.pop(a):
                    sn[c].discard(a)
                    if c != b:
                        sn[c].add(b); sn[b].add(c)
                sn[b].discard(b)
            for c in nbr(b) | {b}:
                ver[c] += 1
                push(c)
        out.append((T[alive].copy(), fp[alive]))
    return out


def split(v, n, t, fp, pal, bone):
    # Вершины делятся по окраске граней: край белья и губ чёткий, без размытия по треугольнику
    K = len(pal)
    uk, kinv = np.unique((t.astype(np.int64) * K + fp[:, None]).ravel(), return_inverse=True)
    src, kp = uk // K, pal[uk % K]
    return (v[src], n[src], kp[:, :3], kp[:, 3].astype(int), bone[src]), kinv.reshape(-1, 3)


def merge(parts):
    # Части в одну сетку; остаются только вершины, на которые ссылаются треугольники
    base, ts = 0, []
    for (pv, *_), t in parts:
        ts.append(t + base); base += len(pv)
    V, N, C, M, B = (np.concatenate([np.asarray(pa[0][k]) for pa in parts]) for k in range(5))
    T = np.concatenate(ts)
    live = np.unique(T)
    remap = np.full(len(V), -1); remap[live] = np.arange(len(live))
    return V[live], N[live], C[live], M[live], B[live], remap[T]


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
        lods, J = build(p, base, faces, Z, skel, W)
        for l, (V, N, C, M, B, T) in enumerate(lods):
            name = p['name'] + ('_lod%d' % l if l else '')
            write_mesh('%s/%s.mesh' % (out, name), V, N, C, M, B, T)
            print('%-15s %5d вершин %6d треугольников' % (name, len(V), len(T)))
        j = lambda n: J[n + '____head']
        print('  плечо %s  локоть %s  запястье %s' % tuple(np.round(j(b + '.L'), 3) for b in ('upperarm01', 'lowerarm01', 'wrist')))
        print('  бедро %s  колено %s  лодыжка %s' % tuple(np.round(j(b + '.L'), 3) for b in ('upperleg01', 'lowerleg01', 'foot')))
        print('  шея %.3f  голова %.3f  ключица %s' % (j('neck01')[1], j('head')[1], np.round(j('clavicle.L'), 3)))


if __name__ == '__main__':
    main()
