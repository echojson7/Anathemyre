#!/usr/bin/env python3
# Люди из MakeHuman (CC0) -> .mesh игры. Офлайн, в игре не нужен.
# Морфы пола и сложения, руки вниз и ноги прямо (поза привязки игры), пальцы в хвате, рост 1.80 м,
# раскраска по граням (кожа, губы, брови), волосы над кожей головы, глаза.
# Пять LOD по LOD_TRIS, упрощение своё, лицо дороже остального. Веса до 4 костей на вершину.
# Нужен только numpy.
#   python3 -I tools/humanconv/humanconv.py <папка MakeHuman> <targets.npz> <data/meshes>
# Папка MakeHuman: base.obj, default.mhskel, default_weights.mhw (makehuman/data из system assets)
import json, os, struct, sys
import numpy as np

# Кости игры, порядок как в src/sim/body.h
(ROOT, PELVIS, SPINE, CHEST, NECK, HEAD, CLAV_L, UA_L, FA_L, HAND_L, CLAV_R, UA_R, FA_R, HAND_R,
 TH_L, SH_L, FT_L, TH_R, SH_R, FT_R) = range(20)
MAT_RUST, MAT_CLOTH, MAT_LEATHER, MAT_SKIN, MAT_METAL = 1, 3, 4, 7, 8  # как Material в src/render/mesh.h
HEIGHT = 1.80
# Треугольников тела на LOD0..4 (без оболочки волос). LOD0 — для крупных планов (редактор, портреты, вплотную)
LOD_TRIS = (16000, 8500, 1800, 500, 320)
NL = len(LOD_TRIS)

# Сложение и цвета как у нашей тройки (src/main.c human_shape), чтобы сравнивать честно
# Пропорции (переработка 2026-10-29, замеры при росте 1.80 м): голова около 7.7 высоты тела у мужчин и 7.65 у женщин
# (у MakeHuman 7.1 — голова крупная), талия в профиль 21 см у мужчины и 18 см у женщины (была тоньше, чем видно спереди)
# Руки до нормы: локоть на 0.635 роста, запястье на 0.49, размах около роста (было 0.67 и 0.52, размах 1.67 м)
# Затылок короче: у MakeHuman он раздут за ухом, лысая голова выглядела обросшей лишним
HEAD_SMALLER = {'head/head-scale-vert-decr': 0.5, 'head/head-scale-horiz-decr': 0.3, 'head/head-scale-depth-decr': 0.4,
                'head/head-back-scale-depth-decr': 0.5}
ARMS = {'measure/measure-upperarm-length-incr': 0.6, 'measure/measure-lowerarm-length-decr': 0.4}
PEOPLE = [
    dict(name='mh_male', gender=1.0, muscle=0.8, weight=0.5, beard=False,
         skin=(0.52, 0.37, 0.28), hcol=(0.10, 0.08, 0.06),
         mods=dict(HEAD_SMALLER, **{'torso/torso-muscle-pectoral-incr': 0.5, 'measure/measure-waist-circ-incr': 0.3, **ARMS})),
    # Женщина: талия уже бёдер, но без «галифе»: бёдра 35 см (были 40), ляжки на 8% тоньше, колени и икры суше;
    # плечи 45 см по дельтам (были как у мужчины, 49); грудь ближе друг к другу, лобок ниже
    dict(name='mh_female', gender=0.0, muscle=0.5, weight=0.55, beard=False,
         skin=(0.60, 0.45, 0.36), hcol=(0.20, 0.13, 0.08),
         cup=0.65, firm=0.7,
         mods={'head/head-scale-vert-decr': 0.3, 'head/head-scale-horiz-decr': 0.35, 'head/head-scale-depth-decr': 0.3,
               'head/head-back-scale-depth-decr': 0.5,
               'bodyshapes/bodyshapes-elvs-fem-neat-hourglass': 0.5, 'hip/hip-scale-horiz-decr': 0.4, 'measure/measure-hips-circ-decr': 0.15,
               'measure/measure-thigh-circ-decr': 0.15, 'armslegs/l-upperleg-fat-decr': 0.2, 'armslegs/r-upperleg-fat-decr': 0.2,
               'measure/measure-knee-circ-decr': 0.4, 'measure/measure-calf-circ-decr': 0.3,
               'buttocks/buttocks-volume-decr': 1.0, 'measure/measure-shoulder-dist-decr': 0.6,
               'measure/measure-upperarm-length-incr': 0.5, 'measure/measure-lowerarm-length-incr': 0.2,
               'breast/breast-dist-decr': 0.15, 'pelvis/bulge-decr': 1.0}),
    dict(name='mh_thin', gender=1.0, muscle=0.45, weight=0.15, beard=True,
         skin=(0.38, 0.26, 0.19), hcol=(0.07, 0.06, 0.05),
         mods=dict(HEAD_SMALLER, **{'torso/torso-muscle-pectoral-incr': 0.4, 'measure/measure-waist-circ-incr': 0.2, **ARMS})),
]
# Ступни у всех чуть меньше: в позах сидя и на корточках родные ступни MakeHuman слишком бросаются в глаза
for _p in PEOPLE:
    _p['mods'].update({'armslegs/%s-foot-scale-%s-decr' % (s, a): w for s in 'lr' for a, w in (('depth', 0.38), ('horiz', 0.3))})


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
    age = {'young': 1 - p.get('age', 0.0), 'old': p.get('age', 0.0)}
    race = p.get('race', {'african': 1 / 3, 'asian': 1 / 3, 'caucasian': 1 / 3})
    for gn, gw in gen.items():
        for an, aw in age.items():
            for mn, mw in three(p['muscle']).items():
                for wn, ww in three(p['weight']).items():
                    add('macrodetails/universal-%s-%s-%smuscle-%sweight' % (gn, an, mn, wn), gw * aw * mw * ww)
            for rn, rw in race.items():
                add('macrodetails/%s-%s-%s' % (rn, gn, an), gw * aw * rw)
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


# Морфы внешности: у каждого свой поток сдвигов вершин, игра смешивает их весами экземпляра
# (порядок как LOOK_* в src/game/look.h)
# Лицо — только местные морфы: морфы расы MakeHuman меняли и рост, и плечи, и голову
_eyes = lambda d: {'eyes/%s-%s' % (s, k): w for s in 'lr' for k, w in d.items()}
MORPHS = (
    dict(weight=1.0),                  # полнее
    dict(weight=0.0),                  # худее
    dict(muscle=1.0),                  # сильнее
    dict(age=1.0),                     # старше: 1 — глубокая старость
    dict(mods={'nose/nose-volume-incr': 0.5, 'nose/nose-scale-horiz-incr': 0.5, 'nose/nose-scale-vert-incr': 0.3,
               'nose/nose-scale-depth-incr': 0.4}),  # нос крупнее; минус — мельче
    dict(mods=_eyes({'eye-height2-decr': 0.7, 'eye-height1-decr': 0.3, 'eye-corner2-up': 0.6,
                     'eye-epicanthus-in': 0.5})),  # разрез уже, внешний угол выше; минус — круглее
    dict(mods={'chin/chin-width-incr': 0.7, 'chin/chin-bones-incr': 0.6, 'head/head-square': 0.6}),  # тяжёлая челюсть
    dict(mods={'mouth/mouth-upperlip-volume-incr': 0.8, 'mouth/mouth-lowerlip-volume-incr': 0.8,
               'mouth/mouth-scale-horiz-incr': 0.25}),  # губы полнее; минус — тоньше
)
# Класс оттенка вершины: игра перекрашивает кожу и волосы под экземпляр
TINT_NONE, TINT_SKIN, TINT_HAIR = 0, 1, 2
MORPH_POS, MORPH_NRM = 0.25, 2.0  # диапазоны сдвига позиции (м) и нормали в файле

JOINT_SRC = (('PELVIS', 'spine05'), ('SPINE', 'spine04'), ('CHEST', 'spine02'), ('NECK', 'neck01'), ('HEAD', 'head'),
             ('CLAVICLE_L', 'clavicle.L'), ('UPPER_ARM_L', 'upperarm01.L'), ('FOREARM_L', 'lowerarm01.L'), ('HAND_L', 'wrist.L'),
             ('THIGH_L', 'upperleg01.L'), ('SHIN_L', 'lowerleg01.L'), ('FOOT_L', 'foot.L'))


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


def rot_axis(k, a):
    k = k / np.linalg.norm(k)
    K = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    return np.eye(3) + np.sin(a) * K + (1 - np.cos(a)) * K @ K


# Свободный хват: сгиб фаланг, градусы (большой палец, затем указательный..мизинец)
CURL = ((10, 20, 20), (40, 50, 30), (45, 55, 30), (50, 55, 30), (55, 55, 30))


def fingers(J, sd, R_fa, sg):
    # Пальцы сгибаются к ладони; ладонь после позы смотрит к бедру (к середине тела).
    # Возвращает имя кости MH -> (R, t) в исходной позе, до поворота кисти
    j = lambda n, e='head': J['%s.%s____%s' % (n, sd, e)]
    lat = j('finger5-1') - j('finger2-1')
    out = {}
    for f, ang in enumerate(CURL, 1):
        d = j('finger%d-3' % f, 'tail') - j('finger%d-1' % f)
        d = d / np.linalg.norm(d)
        nrm = np.cross(d, lat)
        nrm /= np.linalg.norm(nrm)
        if (R_fa @ nrm)[0] * sg > 0:
            nrm = -nrm
        if f == 1:
            # Большой палец сгибается поперёк ладони, к указательному
            nrm = j('finger2-1') - j('finger1-2')
            nrm = nrm - d * (nrm @ d)
            nrm /= np.linalg.norm(nrm)
        k = np.cross(d, nrm)
        R, t = np.eye(3), np.zeros(3)
        for s, a in enumerate(ang, 1):
            # Поворот вокруг сустава в уже согнутой цепочке: x -> R x + t
            Rs = rot_axis(k, np.radians(a))
            h = R @ j('finger%d-%d' % (f, s)) + t
            R, t = Rs @ R, Rs @ (t - h) + h
            out['finger%d-%d' % (f, s)] = (R, t)
    return out


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
        hand = (R_fa, W1 - R_fa @ W0)
        curl = fingers(J, sd, R_fa, sg)
        for n in names:
            if not n.endswith('.' + sd):
                continue
            b = n.split('.')[0]
            if b.startswith('shoulder'): xf[n] = (half(R_ua), S0, S0)
            elif b.startswith('upperarm'): xf[n] = (R_ua, S0, S0)
            elif b.startswith('lowerarm'): xf[n] = (R_fa, E0, E1)
            elif b in curl:
                R, t = curl[b]
                xf[n] = (hand[0] @ R, np.zeros(3), hand[0] @ t + hand[1])
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


def posed(p, base, faces, Z, skel, W, fit=None):
    # Морфы, поза привязки и масштаб (по умолчанию — рост HEIGHT). fit — (низ, масштаб) другого тела
    v = morph(base, Z, p)
    J = {k: v[ix].mean(0) for k, ix in skel['joints'].items()}
    v = repose(v, J, W, list(W.keys()))
    J = {k: v[ix].mean(0) for k, ix in skel['joints'].items()}
    if fit is None:
        used = np.unique(faces['body'])
        y0 = v[used, 1].min()
        fit = (y0, HEIGHT / (v[used, 1].max() - y0))
    o = np.array([0.0, fit[0], 0.0])
    v, J = (v - o) * fit[1], {k: (x - o) * fit[1] for k, x in J.items()}
    return v, J, fit


def build(p, base, faces, Z, skel, W):
    v, J, fit = posed(p, base, faces, Z, skel, W)
    body = faces['body']

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
    sk = skin_weights(acc, bone)
    get = lambda *ns: sum((wt[n] for n in ns if n in wt), np.zeros(nv))

    skin = np.array(p['skin']); hcol = np.array(p['hcol'])
    col = np.tile(skin, (nv, 1)); mat = np.full(nv, MAT_SKIN)
    n = normals(v, tri(body))
    x, y, z = v[:, 0], v[:, 1], v[:, 2]
    ax = np.abs(x)

    isbody = np.zeros(nv, bool); isbody[used] = True

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
    # Размеры на голове ниже — для головы MakeHuman без уменьшения (макушка на 11.25 см над глазом): hk — во сколько раз голова меньше
    hd = (bone == HEAD) & isbody
    top_y = y[hd].max()
    hk = (top_y - J['eye.L____head'][1]) / 0.1125
    brows = np.zeros(nv, bool)
    for sd, sg in (('L', 1), ('R', -1)):
        e = J['eye.%s____head' % sd]
        dx = (x - e[0]) * sg / hk
        arch = 0.004 * (1 - ((dx - 0.004) / 0.022) ** 2)
        dy = (y - e[1]) / hk
        brow = (bone == HEAD) & (dx > -0.016) & (dx < 0.026) & (dy > 0.012 + arch) & (dy < 0.021 + arch) \
            & (z > e[2] - 0.005) & (n[:, 2] > 0.3)
        brows |= brow
    if p['beard']:
        # Борода и усы: щетина объёмом 3 мм по нижней части лица
        eye_y = J['eye.L____head'][1]
        mouth = J['oris01____head'] if 'oris01____head' in J else J['jaw____tail']
        jaw = (bone == HEAD) | (bone == NECK)
        neck_y = J['neck02____head'][1]
        beard = jaw & (y < eye_y - 0.045 * hk) & (y > neck_y - 0.01 * hk + 0.06 * hk * np.clip(-(z - mouth[2]) / (0.07 * hk), 0, 1)) \
            & (z > J['head____head'][2] - 0.005) & ~lips
        v[beard] += n[beard] * 0.003
        col[beard] = skin * 0.15 + hcol * 0.85
    # Линия роста волос: высоко надо лбом, ниже у висков, у затылка до шеи; уши открыты
    crown = hd & (y > top_y - 0.12 * hk)
    zc = (z[crown].min() + z[crown].max()) / 2
    side = hd & (y > top_y - 0.19 * hk) & (y < top_y - 0.10 * hk)
    ear = side & (ax > 0.93 * ax[side].max())
    ear_c = np.array([ax[ear].mean(), y[ear].mean(), z[ear].mean()])
    tz = np.clip((z - zc + 0.05 * hk) / (0.13 * hk), 0, 1)
    tz = tz * tz * (3 - 2 * tz)
    line = top_y + hk * (-0.20 + 0.118 * tz - 0.012 * np.clip(ax / (0.07 * hk), 0, 1) * tz)
    ear_d = ((ax - ear_c[0]) / (0.03 * hk)) ** 2 + ((y - ear_c[1]) / (0.04 * hk)) ** 2 + ((z - ear_c[2]) / (0.032 * hk)) ** 2
    # Кожа головы без щетины (решение 2026-10-29): тёмная шапка с краем по линии волос читалась как лишняя
    # кожа поверх черепа. Линия и уши нужны причёскам и вещам (garments, hair/)

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
    ie = np.unique(eyes)
    tcl = np.where(mat == MAT_SKIN, TINT_SKIN, TINT_NONE)
    if p['beard']:
        tcl[beard] = TINT_HAIR
    tcl[ie] = TINT_NONE
    pal, pid = np.unique(np.concatenate([np.round(col, 4), mat[:, None], tcl[:, None]], 1), axis=0, return_inverse=True)
    pid = pid.ravel()
    vote = lambda q: q[np.arange(len(q)), (q[:, :, None] == q[:, None, :]).sum(2).argmax(1)]
    nf0 = len(F0)
    # LOD0 — исходная сетка MakeHuman. Четырёхугольник, где окраска дочерних граней разная,
    # делится на четыре (линейно, без сглаживания); соседи с поделённым ребром — веером из центра.
    # Номера вершин как у сглаженной сетки: точки рёбер nv0 + e, центры граней nv0 + len(E) + f
    vl = np.concatenate([vc, vc[E].mean(1), vc[F0].mean(1)])
    # Морфы внешности в номерах вершин vl: то же тело с другими ползунками
    VK = []
    for m in MORPHS:
        q = dict(p, **{k: x for k, x in m.items() if k != 'mods'})
        q['mods'] = dict(p.get('mods', {}))
        for k, x in m.get('mods', {}).items():
            q['mods'][k] = q['mods'].get(k, 0.0) + x
        vk = posed(q, base, faces, Z, skel, W, fit)[0]
        VK.append(np.concatenate([vk, vk[E].mean(1), vk[F0].mean(1)]))

    def mdelta(t, n):
        # Сдвиги (вершина vl, морф, 6): позиция и нормаль; у глаз нормали свои — не трогаются
        d = np.zeros((len(vl), len(VK), 6))
        for k, vk in enumerate(VK):
            d[:, k, :3] = vk - vl
            d[:, k, 3:] = normals(vk, t) - n
        d[ie, :, 3:] = 0
        return d

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

    # Лицо и голова при упрощении дороже: их вершины уходят последними
    wv = np.where(bone == HEAD, 40.0, np.where(bone == NECK, 8.0, 1.0))
    lods = []
    # LOD0 тоже упрощается, но с полной окраской
    src = []
    for T0, P0 in decimate(vl, T0, P0, list(LOD_TRIS[:2]), wv):
        n0 = normals(vl, T0)
        n0[np.unique(eyes)] = n[np.unique(eyes)]
        md = mdelta(T0, n0)
        src.append((T0, n0, md))
        lods.append(merge([split(vl, n0, T0, P0, pal, sk, md)]))

    # Дальние LOD: своё упрощение с учётом швов окраски (decimate). Окраска проще:
    # кожа, губы, брови, борода, волосы — окраска кожи головы.
    # Глаза упрощаются отдельно и не меньше чем до 24 треугольников: иначе уходят под веки
    fcol = np.tile(skin, (nv, 1))
    fcol[lips] = col[lips]; fcol[brows] = skin * 0.25 + hcol * 0.75
    fmat = np.full(nv, MAT_SKIN)
    if p['beard']:
        fcol[beard] = skin * 0.15 + hcol * 0.85
    fcol[ie] = col[ie]; fmat[ie] = MAT_SKIN
    ftcl = tcl.copy()
    ftcl[brows] = TINT_HAIR
    fpal, fpid = np.unique(np.concatenate([np.round(fcol, 4), fmat[:, None], ftcl[:, None]], 1), axis=0, return_inverse=True)
    fpid = fpid.ravel()
    fe = vote(fpid[eyes])
    Tf, Pf = refine(vote(fpid[body]).reshape(4, nf0).T)
    eye_n = (64, 40, 24)
    body_l = decimate(vl, Tf, Pf, [LOD_TRIS[l + 2] - eye_n[l] for l in range(NL - 2)], wv)
    eye_l = decimate(vl, tri(eyes), np.concatenate([fe, fe]), list(eye_n), wv)
    for (tb, pb), (te, pe) in zip(body_l, eye_l):
        t, fp = np.concatenate([tb, te]), np.concatenate([pb, pe])
        nt = normals(vl, t)
        md = mdelta(t, nt)
        src.append((tb, nt, md))
        lods.append(merge([split(vl, nt, t, fp, fpal, sk, md)]))
    # Брови на LOD0–1 — лентой поверх кожи, а не окраской её граней: край не зависит от сетки лица.
    # Форма как у живой брови: у переносья толще и ниже, подъём к 2/3 длины, хвост тонкий и опускается.
    # Середина ленты темнее краёв. На дальних LOD — окраска кожи (brows выше)
    brow_parts = []
    th_ = src[0][0][(bone[src[0][0]] == HEAD).all(1)]
    ta, tb, tc = vl[th_[:, 0]], vl[th_[:, 1]], vl[th_[:, 2]]
    n0f = normals(vl, src[0][0])
    def hit(o):
        # Лучи вдоль −Z на кожу лица: точка и нормаль кожи (средняя по вершинам грани)
        d = np.array([0.0, 0.0, -1.0])
        e1, e2 = tb - ta, tc - ta
        pv = np.cross(d, e2)
        det = (e1 * pv).sum(1)
        ok_ = np.abs(det) > 1e-12
        det = np.where(ok_, det, 1.0)
        out_p, out_n, out_v = [], [], []
        for q in o:
            tv = q - ta
            u = (tv * pv).sum(1) / det
            qv = np.cross(tv, e1)
            w_ = (qv @ d) / det
            t = (e2 * qv).sum(1) / det
            t = np.where(ok_ & (u >= 0) & (w_ >= 0) & (u + w_ <= 1) & (t > 0), t, np.inf)
            j = t.argmin()
            out_p.append(q + d * t[j])
            nn = n0f[th_[j]].mean(0)
            out_n.append(nn / np.linalg.norm(nn))
            out_v.append(th_[j][np.argmax([1 - u[j] - w_[j], u[j], w_[j]])])
        return np.array(out_p), np.array(out_n), np.array(out_v)
    for l in range(2):
        m_ = (14, 9)[l]
        bp, bt_, bcl = [], [], []
        for sd, sg in (('L', 1), ('R', -1)):
            e = J['eye.%s____head' % sd]
            t = np.linspace(0, 1, m_)
            dxs = (-0.017 + 0.044 * t) * hk
            rise = np.where(t < 0.65, 1 - ((t - 0.65) / 0.65) ** 2, 1 - ((t - 0.65) / 0.35) ** 2 * 1.25)
            # У мужчин бровь гуще и прямее
            fem = p['gender'] < 0.5
            cy = e[1] + (0.0145 + (0.0055 if fem else 0.0035) * rise) * hk
            th = (0.0105 - 0.004 * t - 0.0045 * t ** 3) * hk * (0.5 + 0.5 * np.sqrt(np.clip(t / 0.15, 0, 1))) * (1.0 if fem else 1.08)
            rows = (-0.5, -0.28, 0.32, 0.5)
            o = np.array([[e[0] + sg * dx, y_ + r * h_, 1.0] for dx, y_, h_ in zip(dxs, cy, th) for r in rows])
            pp, pn, pv_ = hit(o)
            pp += pn * 0.0007
            base_i = sum(len(x[0]) for x in bp)
            bp.append((pp, pn, pv_))
            for i in range(m_ - 1):
                for r in range(3):
                    a = base_i + i * 4 + r
                    q = [a, a + 4, a + 5, a + 1] if sg > 0 else [a, a + 1, a + 5, a + 4]
                    bt_ += [[q[0], q[1], q[2]], [q[0], q[2], q[3]]]
                    bcl += [r == 1, r == 1]
        P, N, V = (np.concatenate([b[i] for b in bp]) for i in range(3))
        T = np.array(bt_)
        # Грань наружу: по нормали кожи
        fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
        flip = (fn * N[T].mean(1)).sum(1) < 0
        T[flip] = T[flip][:, ::-1]
        # Окраска по граням: вершины делятся, у каждой грани свой цвет
        core = np.array(bcl)
        P2, N2, V2 = P[T.ravel()], N[T.ravel()], V[T.ravel()]
        C2 = np.repeat(np.where(core[:, None], skin * 0.1 + hcol * 0.9, skin * 0.35 + hcol * 0.65), 3, 0)
        T2 = np.arange(len(P2)).reshape(-1, 3)
        k_ = len(P2)
        d_ = src[0][2][V2].copy()
        d_[:, :, 3:] = 0
        brow_parts.append(((P2, N2, C2, np.full(k_, MAT_SKIN), np.full(k_, TINT_HAIR), sk[V2], d_), T2))
    gear, hair, under = garments(dict(vl=vl, sk=sk, bone=bone, J=J, src=src, ear_d=ear_d, hd=hd, top_y=top_y, hk=hk, line=line,
                                      hcol=hcol, female=p['gender'] < 0.5))
    # Бельё — часть сетки тела: лишнего вызова отрисовки нет
    lods = [merge([(tuple(m[:-1]), m[-1]), (tuple(u[:-1]), u[-1])] + brow_parts[l:l + 1]) for l, (m, u) in enumerate(zip(lods, under))]
    return lods, J, gear, hair


# Одежда и броня: имя файла -> вещь игры (src/game/look.h, GEAR_*)
GEAR = ('rags', 'quilt', 'jacket', 'plate', 'bucket', 'greaves', 'wrap', 'boots', 'mitts', 'pack', 'belt', 'pants')
GEAR_SEGS = (20, 16, 10, 6, 5)  # сегментов по кругу у жёстких вещей на LOD0..4
# Причёски: имя файла -> вид у тела этого пола (src/game/look.c, g_hair_file)
HAIR_FEMALE = ('tail', 'bun', 'braid', 'crop', 'bob', 'loose', 'side')
HAIR_MALE = ('crop', 'shag', 'tied', 'buzz', 'slick', 'long')
LOCKS = (1.0, 0.5, 0.15, 0.0, 0.0)  # доля прядей на LOD0..4


def tube(path, up, w, h, sides):
    # Прядь или жгут вдоль ломаной path: сечение — ромб (4), треугольник (3) или гребень без низа (2,
    # лежит на объёме) шириной w и толщиной h в плоскости (вбок, наружу по up); w, h на каждую точку
    k = len(path)
    t = np.gradient(path, axis=0)
    t /= np.maximum(np.linalg.norm(t, axis=1, keepdims=True), 1e-9)
    sd = np.cross(t, up)
    sd /= np.maximum(np.linalg.norm(sd, axis=1, keepdims=True), 1e-9)
    o = np.cross(sd, t)
    ridge = sides == 2
    if ridge:
        sides = 3
        a = np.array([0, np.pi / 2, np.pi])
    else:
        a = 2 * np.pi * np.arange(sides) / sides
    hh = h if ridge else h / 2
    pv = (path[:, None] + np.cos(a)[None, :, None] * sd[:, None] * (w / 2)[:, None, None]
          + np.round(np.sin(a), 6)[None, :, None] * o[:, None] * hh[:, None, None]).reshape(-1, 3)
    pt = []
    for i in range(k - 1):
        for j in range(sides - 1 if ridge else sides):
            p0, p1 = i * sides + j, i * sides + (j + 1) % sides
            pt += [[p0, p1, p1 + sides], [p0, p1 + sides, p0 + sides]]
    pt = np.array(pt)
    # Порядок вершин по нормали грани: наружу от оси ломаной
    fn = np.cross(pv[pt[:, 1]] - pv[pt[:, 0]], pv[pt[:, 2]] - pv[pt[:, 0]])
    ring = pt[:, 0] // sides
    flip = (fn * (pv[pt].mean(1) - path[ring])).sum(1) < 0
    pt[flip] = pt[flip][:, ::-1]
    return pv, pt


def orient(pv, pt):
    # Грани выпуклой детали наружу от её центра
    c = pv.mean(0)
    fn = np.cross(pv[pt[:, 1]] - pv[pt[:, 0]], pv[pt[:, 2]] - pv[pt[:, 0]])
    flip = (fn * (pv[pt].mean(1) - c)).sum(1) < 0
    pt = pt.copy()
    pt[flip] = pt[flip][:, ::-1]
    return pt


def lathe(c, prof, segs, ex=1.0, ez=1.0, ph=0.0):
    # Тело вращения вокруг вертикали через c; prof — (сдвиг по y, радиус) снизу вверх; ph — поворот граней
    pv = [c + np.array([np.sin(2 * np.pi * j / segs + ph) * r * ex, dy, np.cos(2 * np.pi * j / segs + ph) * r * ez])
          for dy, r in prof for j in range(segs)]
    pt = []
    for i in range(len(prof) - 1):
        for j in range(segs):
            a, b = i * segs + j, i * segs + (j + 1) % segs
            pt += [[a, b, b + segs], [a, b + segs, a + segs]]
    pv, pt = np.array(pv), np.array(pt)
    return pv, orient(pv, pt)


def box(c, size, yaw):
    # Ящик с плоскими гранями: 24 вершины
    R = np.array([[np.cos(yaw), 0, np.sin(yaw)], [0, 1, 0], [-np.sin(yaw), 0, np.cos(yaw)]])
    pv, pt = [], []
    for ax in range(3):
        for sg in (-1, 1):
            u, w = [k for k in range(3) if k != ax]
            q = len(pv)
            for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                d = np.zeros(3); d[ax] = sg; d[u] = a; d[w] = b
                pv.append(c + R @ (d * np.asarray(size) / 2))
            pt += [[q, q + 1, q + 2], [q, q + 2, q + 3]]
    pv, pt = np.array(pv), np.array(pt)
    return pv, orient(pv, pt)


def fabric(ctx):
    # Общее для вещей и причёсок: поле области по вершинам тела и оболочка над кожей по нему
    vl, sk, src = ctx['vl'], ctx['sk'], ctx['src']
    nvl = len(vl)

    # Рёбра всех LOD тела: по ним размывается граница области
    E = np.unique(np.sort(np.concatenate([t[:, [a, b]] for t, _, _ in src for a, b in ((0, 1), (1, 2), (2, 0))]), 1), axis=0)
    deg = np.bincount(E.ravel(), minlength=nvl)

    def nsum(q, e, nv):
        # Сумма значений соседей по рёбрам e
        return np.bincount(e[:, 0], q[e[:, 1]], nv) + np.bincount(e[:, 1], q[e[:, 0]], nv)

    def soft(mask, it=4):
        # Область вершин -> поле: ноль на сглаженной границе, внутри больше нуля
        m = mask.astype(float)
        for _ in range(it):
            m = np.where(deg > 0, 0.5 * m + 0.5 * nsum(m, E, nvl) / np.maximum(deg, 1), m)
        return m - 0.5

    lin = lambda d: d * 25.0  # метры -> единицы поля soft (переход ~4 см)
    only = lambda sel, d: np.where(sel, d, 1e3)
    field = lambda *fs: np.minimum.reduce(fs)

    def cuts(t, f):
        # Грани t по полю f на вершинах: внутрь (f > 0) и наружу. Новые вершины — точки рёбер,
        # общие для обеих сторон: ребро (a, b), доля w пути от a
        f = np.where(np.abs(f) < 1e-6, 1e-6, f)
        ins = f > 0
        k = ins[t].sum(1)
        nv = len(f)
        cut, ea, eb, ew, fin, fout = {}, [], [], [], [], []

        def pt(a, b):
            key = (min(a, b), max(a, b))
            if key not in cut:
                cut[key] = nv + len(ea)
                ea.append(key[0]); eb.append(key[1]); ew.append(f[key[0]] / (f[key[0]] - f[key[1]]))
            return cut[key]

        mixed = (k == 1) | (k == 2)
        for tri, kk in zip(t[mixed].tolist(), k[mixed].tolist()):
            # Одинокая вершина по свою сторону: у неё треугольник, у другой стороны четырёхугольник
            r = [ins[x] for x in tri].index(kk == 1)
            a, b, c = tri[r], tri[(r + 1) % 3], tri[(r + 2) % 3]
            p1, p2 = pt(a, b), pt(a, c)
            lone, quad = [(a, p1, p2)], [(p1, b, c), (p1, c, p2)]
            (fin if kk == 1 else fout).extend(lone)
            (fout if kk == 1 else fin).extend(quad)
        I = np.concatenate([t[k == 3], np.array(fin, int).reshape(-1, 3)])
        O = np.concatenate([t[k == 0], np.array(fout, int).reshape(-1, 3)])
        return I, O, np.array(ea, int), np.array(eb, int), np.array(ew)

    def grow(A, ea, eb, w):
        # Атрибуты новых вершин: скиннинг от ближнего конца ребра, остальное по доле пути
        for key, q in A.items():
            ww = w.reshape((-1,) + (1,) * (q.ndim - 1))
            A[key] = np.concatenate([q, np.where(ww < 0.5, q[ea], q[eb]) if key == 'sk' else q[ea] + (q[eb] - q[ea]) * ww])

    def shell(l, s, off, base, layers=(), rim=0.6, relax=0, tint=TINT_NONE, flat=None):
        # Оболочка LOD l по полю s. base — (цвет, материал); layers — окраска полями
        # (поле, цвет, материал, до какого LOD резать): грани режутся и по ним, край краски ровный.
        # relax — пар шагов сглаживания: ткань не повторяет мелочей тела
        t, n, md = src[l]
        A = dict(vb=vl, nb=n, ob=off if np.ndim(off) else np.full(nvl, off), md=md, sk=sk, s=s)
        for i, (g, *_) in enumerate(layers):
            A['g%d' % i] = np.zeros(nvl) if type(g) is tuple else g
        F, _, ea, eb, w = cuts(t, s)
        grow(A, ea, eb, w)
        vb, nb, ob = A['vb'], A['nb'] / np.maximum(np.linalg.norm(A['nb'], axis=1, keepdims=True), 1e-12), A['ob']
        nv = len(vb)
        p = vb + nb * ob[:, None]
        e = np.concatenate([F[:, [0, 1]], F[:, [1, 2]], F[:, [2, 0]]])
        uk, inv, cnt = np.unique(np.sort(e, 1), axis=0, return_inverse=True, return_counts=True)
        # Ткань натянута поверх: сглаживание с упором в тело (не ближе 0.7 толщины) затягивает
        # впадины и мелочи, выпуклое остаётся. Край на месте
        dg = np.bincount(uk.ravel(), minlength=nv)
        free = (dg > 0)[:, None].copy()
        free[e[cnt[inv.ravel()] == 1].ravel()] = False
        for i in range(relax * 3 >> max(l - 1, 0)):
            lap = np.stack([nsum(p[:, c], uk, nv) for c in range(3)], 1) / np.maximum(dg, 1)[:, None] - p
            p = np.where(free, p + 0.5 * lap, p)
            p += nb * np.maximum(0.0, 0.7 * ob - ((p - vb) * nb).sum(1))[:, None]
        if flat:
            p = flat(p, nb)
        A['p'] = p
        A['nb'] = nb
        for i, (g, *_) in enumerate(layers):
            if type(g) is tuple:
                A['g%d' % i] = g[0](p)  # полосы — по месту ткани после сглаживания, иначе идут волной
        for i, (g, _, _, top) in enumerate(layers):
            # Полосы уже ребра: режутся по обоим краям каждой полосы, а не по полю
            for c in (g[1](A['g%d' % i]) if type(g) is tuple else [0.0]) if l <= top + 1 else ():
                I, O, ea, eb, w = cuts(F, A['g%d' % i] - c)
                grow(A, ea, eb, w)
                F = np.concatenate([I, O])
        p, vb, nb = A['p'], A['vb'], A['nb']
        nv = len(p)
        # Окраска граней: последний слой, в чьё поле грань попала
        fc = np.tile(np.append(base[0], base[1]).astype(float), (len(F), 1))
        for i, (g, col, m, _) in enumerate(layers):
            q = A['g%d' % i][F].mean(1)
            fc[(g[2](q) if type(g) is tuple else q) > 0] = np.append(col, m)
        e = np.concatenate([F[:, [0, 1]], F[:, [1, 2]], F[:, [2, 0]]])
        _, inv, cnt = np.unique(np.sort(e, 1), axis=0, return_inverse=True, return_counts=True)
        be = np.where(cnt[inv.ravel()] == 1)[0]
        b = e[be]
        # Кромка: каждое ребро края — две грани до кожи, цвет соседней грани темнее
        r = np.concatenate([np.stack([b[:, 0], b[:, 1] + nv, b[:, 1]], 1), np.stack([b[:, 0], b[:, 0] + nv, b[:, 1] + nv], 1)])
        rc = fc[be % len(F)] * np.array([rim, rim, rim, 1.0])
        allc = np.concatenate([fc, rc, rc])
        pal, fp = np.unique(np.concatenate([np.round(allc[:, :3], 4), allc[:, 3:], np.full((len(allc), 1), tint)], 1), axis=0, return_inverse=True)
        ext = lambda a: np.concatenate([a, a])
        return split(np.concatenate([p, vb + nb * 0.0008]), np.concatenate([normals(p, F), nb]),
                     np.concatenate([F, r]), fp.ravel(), pal, ext(A['sk']), ext(A['md']))

    return soft, lin, only, field, shell


def garments(ctx):
    # Вещи по телу: оболочка над гранями кожи нужной области (веса и морфы — как у кожи под ней),
    # по краю — кромка до кожи. Область — поле на вершинах: грани режутся по его нулю, край ровный.
    # Жёсткие вещи (кожух, ранец, подсумки) — на одной кости.
    # LOD вещи берёт грани тела того же LOD: силуэт совпадает
    vl, sk, bone, J, src = ctx['vl'], ctx['sk'], ctx['bone'], ctx['J'], ctx['src']
    nvl = len(vl)
    X, Y, Z = vl[:, 0], vl[:, 1], vl[:, 2]
    AX = np.abs(X)
    j = lambda n: J[n + '____head']
    hip, waist, chest = j('upperleg01.L')[1], j('spine04')[1], j('spine02')[1]
    clav, neck, elbow, wrist = j('clavicle.L')[1], j('neck01')[1], j('lowerarm01.L')[1], j('wrist.L')[1]
    knee, ankle, eye = j('lowerleg01.L')[1], j('foot.L')[1], j('eye.L')[1]
    trunk = np.isin(bone, [PELVIS, SPINE, CHEST, CLAV_L, CLAV_R])
    ua, fa, hand = np.isin(bone, [UA_L, UA_R]), np.isin(bone, [FA_L, FA_R]), np.isin(bone, [HAND_L, HAND_R])
    thigh, shin, foot = np.isin(bone, [TH_L, TH_R]), np.isin(bone, [SH_L, SH_R]), np.isin(bone, [FT_L, FT_R])
    arm = ua | fa | hand
    hd = ctx['hd'][:nvl]
    nrm0 = src[0][1]
    ang = np.arctan2(X, Z)

    soft, lin, only, field, shell = fabric(ctx)

    def rigid_part(pv, pt, col, mat, bn, near):
        # Жёсткая деталь на кости bn; при морфах сдвигается как кожа рядом (near)
        k = len(pv)
        d = src[0][2][near].mean(0).copy()
        d[:, 3:] = 0
        c = np.tile(col, (k, 1)) if np.ndim(col) == 1 else col
        return ((pv, normals(pv, pt), c, np.full(k, mat), np.full(k, TINT_NONE), rigid(np.full(k, bn)), np.tile(d, (k, 1, 1))), pt)

    out = {}
    def stripe(fu, step, wd):
        # Полосы шириной wd через step по координате fu(точки): (fu, края полос по u, поле от u)
        def edges(u):
            c = np.arange(np.floor(u.min() / step), np.ceil(u.max() / step) + 1) * step + step / 2
            return np.concatenate([c - wd / 2, c + wd / 2])
        return (fu, edges, lambda q: wd / 2 - np.abs(np.mod(q, step) - step / 2))

    used = np.unique(src[0][0])
    md0 = src[0][2]

    def stuck(pv, pt, col, mat, at):
        # Мелкая деталь поверх вещи: скиннинг и морфы — от ближней к точке at вершины тела
        k = len(pv)
        q = used[np.argmin(np.linalg.norm(vl[used] - at, axis=1))]
        d = md0[q].copy()
        d[:, 3:] = 0
        c = np.tile(col, (k, 1)) if np.ndim(col) == 1 else col
        return ((pv, normals(pv, pt), c, np.full(k, mat), np.full(k, TINT_NONE), np.tile(sk[q], (k, 1)), np.tile(d, (k, 1, 1))), pt)

    def surf(mesh, at, d):
        # Точка на лицевой стороне вещи mesh возле at, если смотреть по d, и нормаль там
        V, N = mesh[0][0], mesh[0][1]
        ok = N @ d > 0.5
        V2, N2 = V[ok], N[ok]
        lat = V2 - at
        lat -= np.outer(lat @ d, d)
        k = np.argsort(np.linalg.norm(lat, axis=1))[:6]
        n = N2[k].mean(0)
        return V2[k[np.argmax(V2[k] @ d)]], n / np.linalg.norm(n)

    def flip_to(pv, pt, out):
        # Грани наружу по направлению out(центр грани)
        fn = np.cross(pv[pt[:, 1]] - pv[pt[:, 0]], pv[pt[:, 2]] - pv[pt[:, 0]])
        pt = pt.copy()
        fl = (fn * out(pv[pt].mean(1))).sum(1) < 0
        pt[fl] = pt[fl][:, ::-1]
        return pt

    def glyph(rng, c, n, h, cols=1):
        # Знаки неизвестного языка по трафарету: штрихи по сетке 5x7, как glyph_ink в scene.frag
        up = np.array([0.0, 1.0, 0.0]) - n * n[1]
        up /= np.linalg.norm(up)
        rt = np.cross(up, n)
        u = h / 7
        pv, pt = [], []
        for g in range(cols):
            ox = (g - (cols - 1) / 2) * 6.5
            st = [(i * 2, y0, i * 2, min(y0 + 3 + 3 * rng.integers(0, 2), 6)) for i in range(3) if rng.random() > 0.35
                  for y0 in [3 * rng.integers(0, 2)]]
            st += [(x0, i * 3, min(x0 + 2 + 2 * rng.integers(0, 2), 4), i * 3) for i in range(3) if rng.random() > 0.45
                   for x0 in [2 * rng.integers(0, 2)]]
            if len(st) < 3:
                st += [(0, 0, 0, 6), (0, 6, 4, 6), (4, 3, 4, 6)]
            for x0, y0, x1, y1 in st:
                q = len(pv)
                for a, b in ((x0, y0), (x1 + 1, y0), (x1 + 1, y1 + 1), (x0, y1 + 1)):
                    pv.append(c + rt * (ox + a - 2.5) * u + up * (b - 3.5) * u + n * 0.0015)
                pt += [[q, q + 1, q + 2], [q, q + 2, q + 3]]
        pv, pt = np.array(pv), np.array(pt)
        return pv, flip_to(pv, pt, lambda m: n)

    def cyl(a, b, r, segs, caps=True):
        # Цилиндр a->b радиуса r с крышками
        k = 2
        pv, pt = tube(np.stack([a, b]), np.tile(np.cross(b - a, [0.3, 1.0, 0.1]), (k, 1)), np.full(k, 2 * r), np.full(k, 2 * r), segs)
        if caps:
            m = len(pv)
            pv = np.concatenate([pv, [a, b]])
            cap = [[m, i, (i + 1) % segs] for i in range(segs)] + [[m + 1, segs + (i + 1) % segs, segs + i] for i in range(segs)]
            pt = np.concatenate([pt, cap])
            ax = (b - a) / np.linalg.norm(b - a)
            mid = (a + b) / 2
            # Стенка — наружу от оси, крышки — вдоль оси
            def out(cc):
                d = cc - mid
                t = d @ ax
                rad = d - np.outer(t, ax)
                return np.where((np.abs(np.abs(t) - np.linalg.norm(b - a) / 2) < 1e-6)[:, None], np.outer(t, ax), rad)
            pt = flip_to(pv, pt, out)
        return pv, pt

    def pipe(a, b, f, r, half, rings, segs, th):
        # Половинка трубы a->b: дуга ±half вокруг направления f, стенка толщиной th
        L = np.linalg.norm(b - a)
        ax = (b - a) / L
        f = f - ax * (f @ ax)
        f /= np.linalg.norm(f)
        sd = np.cross(ax, f)
        ang = np.linspace(-half, half, segs + 1)
        W = segs + 1
        ring = lambda rr: np.array([a + ax * L * t + (f * np.cos(g) + sd * np.sin(g)) * rr for t in np.linspace(0, 1, rings) for g in ang])
        pv = np.concatenate([ring(r), ring(r - th)])
        n2 = rings * W
        pt = []
        for i in range(rings - 1):
            for j in range(segs):
                q = i * W + j
                for o in (0, n2):
                    pt += [[q + o, q + 1 + o, q + 1 + W + o], [q + o, q + 1 + W + o, q + W + o]]
        for i in (0, rings - 1):
            for j in range(segs):
                q = i * W + j
                pt += [[q, q + 1, q + 1 + n2], [q, q + 1 + n2, q + n2]]
        for j in (0, segs):
            for i in range(rings - 1):
                q = i * W + j
                pt += [[q, q + W, q + W + n2], [q, q + W + n2, q + n2]]
        pt = np.array(pt)
        rm = r - th / 2

        def out(cc):
            # От серединной поверхности стенки наружу
            d = cc - a
            t = np.clip(d @ ax, 0.002, L - 0.002)
            rad = d - np.outer(d @ ax, ax)
            g = np.clip(np.arctan2(rad @ sd, rad @ f), -half + 0.01, half - 0.01)
            m = a + np.outer(t, ax) + (np.outer(np.cos(g), f) + np.outer(np.sin(g), sd)) * rm
            return cc - m
        return pv, flip_to(pv, pt, out)

    def slab(p, nb):
        # Лист металла: спереди и сзади поверхность выпрямлена до прямой по высоте через самые выпуклые точки
        for sg in (1, -1):
            fr = nb[:, 2] * sg > 0.35
            if fr.sum() < 8:
                continue
            y, z = p[fr, 1], p[fr, 2] * sg
            ym = np.median(y)
            lo, hi = y < ym, y >= ym
            y1, z1 = y[lo][np.argmax(z[lo])], z[lo].max()
            y2, z2 = y[hi][np.argmax(z[hi])], z[hi].max()
            zz = z1 + (y - y1) * (z2 - z1) / max(y2 - y1, 1e-3)
            p[fr, 2] = np.maximum(z, zz) * sg
        return p

    inside = lambda sel, d: np.where(sel, d, -1.0)  # слой краски только в области sel
    PAINT = (0.60, 0.65, 0.53)       # бледная краска знаков
    RUBBER = (0.075, 0.072, 0.065)   # чёрная резина
    sL = np.sign(j('upperarm01.L')[0])
    side_l = np.sign(X) == sL

    # Роба: выцветший серо-зелёный брезент, рукава закатаны выше локтя, накладной карман и ворот темнее
    s = field(soft(trunk | arm), lin(Y - hip - 0.02 - 0.006 * np.sin(ang * 7)), only(arm, lin(Y - elbow - 0.06)))
    lay = [(np.sin(X * 31) * np.sin(Y * 27 + Z * 19) - 0.65, (0.46, 0.47, 0.40), MAT_CLOTH, 1),
           (inside(arm, Y - elbow - 0.035), (0.30, 0.31, 0.26), MAT_CLOTH, 1),
           (inside(trunk & (Z > 0), np.minimum(0.045 - np.abs(sL * X - 0.09), 0.04 - np.abs(Y - chest - 0.05))), (0.33, 0.34, 0.28), MAT_CLOTH, 0),
           (inside(trunk, Y - clav + 0.015), (0.29, 0.30, 0.25), MAT_CLOTH, 1)]
    out['rags'] = [[shell(l, s, 0.007, ((0.38, 0.39, 0.32), MAT_CLOTH), lay, relax=3)] for l in range(NL)]

    # Комбинезон: выцветший серо-синий от ворота до щиколоток, молния, резиновые накладки на коленях и локтях; трафарет на спине и груди
    s = field(soft(trunk | arm | thigh | shin | np.isin(bone, [NECK])), lin(Y - ankle - 0.07),
              only(fa | hand, lin(Y - wrist - 0.03)), only(np.isin(bone, [NECK, HEAD]), lin(neck + 0.01 - Y)))
    lay = [(inside(trunk & (Z > 0), 0.006 - AX), (0.15, 0.17, 0.19), MAT_CLOTH, 1),
           (inside(thigh | shin, np.minimum(0.05 - np.abs(Y - knee - 0.01), (nrm0[:, 2] - 0.3) * 0.2)), RUBBER, MAT_LEATHER, 1),
           (inside(arm, np.minimum(0.04 - np.abs(Y - elbow), (-nrm0[:, 2] - 0.25) * 0.2)), RUBBER, MAT_LEATHER, 1),
           (inside(trunk, 0.015 - np.abs(Y - hip - 0.13)), (0.18, 0.20, 0.22), MAT_CLOTH, 1)]
    out['quilt'] = []
    for l in range(NL):
        sh = shell(l, s, np.where(thigh | shin, 0.014, 0.012), ((0.17, 0.19, 0.21), MAT_CLOTH), lay, relax=6)
        parts = [sh]
        if l < 3:
            rng = np.random.default_rng(11)
            c, n = surf(sh, np.array([0.0, chest + 0.02, 0.0]), np.array([0, 0, -1.0]))
            pv, pt = glyph(rng, c, n, 0.075, cols=2)
            parts.append(stuck(pv, pt, PAINT, MAT_CLOTH, c))
            c, n = surf(sh, np.array([-sL * 0.085, chest + 0.075, 0.0]), np.array([0, 0, 1.0]))
            pv, pt = glyph(rng, c, n, 0.03, cols=3)
            parts.append(stuck(pv, pt, PAINT, MAT_CLOTH, c))
        out['quilt'].append(parts)

    # Прорезиненная куртка: чёрная, высокий ворот, кокетка на плечах, застёжки скобами
    s = field(soft(trunk | arm | thigh | np.isin(bone, [NECK])), lin(Y - hip),
              only(fa | hand, lin(Y - wrist - 0.025)), only(np.isin(bone, [NECK, HEAD]), lin(neck + 0.06 - Y)))
    lay = [(inside(trunk | arm, Y - clav + 0.035), (0.11, 0.105, 0.095), MAT_LEATHER, 1),
           (inside(fa | hand, wrist + 0.06 - Y), (0.05, 0.05, 0.045), MAT_LEATHER, 1),
           (inside(trunk & (Z > 0), 0.008 - AX), (0.05, 0.05, 0.045), MAT_LEATHER, 0)]
    out['jacket'] = []
    for l in range(NL):
        sh = shell(l, s, 0.014, (RUBBER, MAT_LEATHER), lay, relax=3)
        parts = [sh]
        for y in (np.linspace(waist + 0.04, clav - 0.06, 4) if l < 2 else ()):
            c, n = surf(sh, np.array([0.0, y, 0.0]), np.array([0, 0, 1.0]))
            pv, pt = box(c + n * 0.003, (0.028, 0.012, 0.008), 0.0)
            parts.append(stuck(pv, pt, (0.33, 0.32, 0.29), MAT_METAL, c))
        out['jacket'].append(parts)

    # Обшивка: грудь и спина — выпрямленные листы с панели машины, заклёпки, знаки; наплечник с одной стороны в полосах
    s = field(soft(trunk), lin(Y - waist - 0.05), lin(clav + 0.01 - Y), (0.75 - np.abs(nrm0[:, 0])) * 4)
    wear = np.sin(X * 23 + 1) * np.sin(Y * 19 + Z * 13) + 0.5 * np.sin(X * 41 - Y * 37) - 0.95
    lay = [(wear, (0.30, 0.17, 0.09), MAT_RUST, 1), (0.45 - s, (0.13, 0.12, 0.10), MAT_RUST, 1)]
    sp = field(soft(side_l & (trunk | np.isin(bone, [UA_L, UA_R, CLAV_L, CLAV_R]))), lin(Y - clav + 0.10), lin(sL * X - 0.10))
    lp = [(stripe(lambda P: P[:, 1] + P[:, 2] * 0.8 - sL * P[:, 0] * 0.4, 0.045, 0.016), (0.47, 0.41, 0.22), MAT_RUST, 1),
          (0.45 - sp, (0.15, 0.15, 0.13), MAT_RUST, 1)]
    out['plate'] = []
    for l in range(NL):
        sh = shell(l, s, 0.035, ((0.19, 0.21, 0.18), MAT_RUST), lay, 0.5, relax=10, flat=slab)
        parts = [sh, shell(l, sp, 0.05, ((0.19, 0.21, 0.18), MAT_RUST), lp, 0.5, relax=8)]
        if l < 3:
            rng = np.random.default_rng(7)
            c, n = surf(sh, np.array([-sL * 0.06, chest + 0.03, 0.0]), np.array([0, 0, 1.0]))
            pv, pt = glyph(rng, c, n, 0.07, cols=2)
            parts.append(stuck(pv, pt, PAINT, MAT_RUST, c))
            c, n = surf(sh, np.array([0.0, chest, 0.0]), np.array([0, 0, -1.0]))
            pv, pt = glyph(rng, c, n, 0.11)
            parts.append(stuck(pv, pt, PAINT, MAT_RUST, c))
        if l < 2:
            for sgz in (1.0, -1.0):
                for x, y in ((0.12, waist + 0.08), (-0.12, waist + 0.08), (0.10, clav - 0.03), (-0.10, clav - 0.03)):
                    c, n = surf(sh, np.array([x, y, 0.0]), np.array([0, 0, sgz]))
                    pv, pt = box(c + n * 0.002, (0.011, 0.011, 0.006), 0.0)
                    parts.append(stuck(pv, pt, (0.20, 0.19, 0.17), MAT_METAL, c))
        out['plate'].append(parts)

    # Трубные поножи: половинки труб спереди голени с фланцами по краям, примотаны кабелем в двух местах
    out['greaves'] = [[] for _ in range(NL)]
    for sd_ in ('L', 'R'):
        K, A = j('lowerleg01.' + sd_), j('foot.' + sd_)
        a, b = K + (A - K) * 0.12, A + (K - A) * 0.18
        L = np.linalg.norm(b - a)
        ax = (b - a) / L
        bn = SH_L if sd_ == 'L' else SH_R
        leg = (bone == bn) & np.isin(np.arange(nvl), used)
        f = np.array([0, 0, 1.0]) - ax * ax[2]
        f /= np.linalg.norm(f)
        sd = np.cross(ax, f)
        d = vl[leg] - a
        t = d @ ax
        rad = d - np.outer(t, ax)
        mid = (t > 0.15 * L) & (t < 0.85 * L)
        ga_all = np.arctan2(rad @ sd, rad @ f)
        rn_all = np.linalg.norm(rad, axis=1)
        # Ось трубы сдвинута вперёд: дуга обнимает голень спереди, а не стоит колоколом
        sh_ = 0.025
        c0 = a + f * sh_
        fr = mid & (np.abs(ga_all) < 1.2)
        r = np.percentile(np.linalg.norm(rad[fr] - f * sh_, axis=1), 97) + 0.03
        half = 1.35
        for l in range(NL):
            pv, pt = pipe(c0, c0 + (b - a), f, r, half, 2, (14, 10, 6, 4, 4)[l], 0.004)
            out['greaves'][l].append(rigid_part(pv, pt, (0.20, 0.13, 0.08), MAT_RUST, bn, leg))
            if l < 3:
                for u0 in (0.0, 0.93):
                    pv, pt = pipe(c0 + (b - a) * u0, c0 + (b - a) * (u0 + 0.07), f, r + 0.005, half + 0.04, 2, (14, 10, 6)[l], 0.009)
                    out['greaves'][l].append(rigid_part(pv, pt, (0.13, 0.11, 0.09), MAT_METAL, bn, leg))
            for u in (0.3, 0.7):
                # Кабель по кругу: спереди по трубе, сзади по икре
                g = np.sort(np.concatenate([np.linspace(-np.pi, np.pi, (17, 13, 7, 5, 5)[l]), [-half, half]]))
                o = a + (b - a) * u
                near = mid & (np.abs(t - u * L) < 0.04)
                ga, rn = ga_all[near], rn_all[near]
                skin = np.array([np.median(rn[np.abs(np.angle(np.exp(1j * (ga - q)))) < 0.3]) + 0.03 for q in g])
                # Радиус трубы в направлении g от оси ноги (ось трубы сдвинута на sh_ вперёд)
                cg = np.cos(g)
                pr = sh_ * cg + np.sqrt(np.maximum((r + 0.004) ** 2 - (sh_ * np.sin(g)) ** 2, 0))
                # По трубе до её краёв, дальше натянут к икре
                rr = np.where(np.abs(g) <= half + 1e-6, np.maximum(pr, skin), skin)
                path = o + (np.outer(cg, f) + np.outer(np.sin(g), sd)) * rr[:, None]
                pv, pt = tube(path, path - o, np.full(len(g), 0.008), np.full(len(g), 0.007), 4 if l < 3 else 3)
                out['greaves'][l].append(rigid_part(pv, pt, RUBBER, MAT_LEATHER, bn, leg))

    # Респиратор: резиновая полумаска от переносицы до подбородка, фильтр-банка впереди, ремень вокруг головы
    face = hd & (Z > np.median(Z[hd])) & (ctx['ear_d'][:nvl] > 1.6)
    s = field(soft(face), lin(eye - 0.022 - Y))
    sb = field(soft(hd & (ctx['ear_d'][:nvl] > 1.2)), lin(0.006 - np.abs(Y - eye + 0.035)))
    out['wrap'] = []
    for l in range(NL):
        sh = shell(l, s, 0.009, (RUBBER, MAT_LEATHER), relax=4)
        c, n = surf(sh, np.array([0.0, eye - 0.068, 0.0]), np.array([0, -0.3, 1.0]) / np.hypot(0.3, 1.0))
        pv, pt = cyl(c - n * 0.004, c + n * 0.03, 0.024, (16, 12, 8, 6, 5)[l])
        col = np.tile((0.24, 0.25, 0.19), (len(pv), 1))
        out['wrap'].append([sh, shell(l, sb, 0.004, (RUBBER, MAT_LEATHER), relax=1), rigid_part(pv, pt, col, MAT_METAL, HEAD, hd)])

    # Рабочие ботинки: голенище — оболочка выше щиколотки, стопа — колодка по срезам ступни (пальцы не
    # проступают), резиновая подошва, окованный носок поверх, светлее отворот сверху
    s = field(soft(foot | shin), lin(ankle + 0.12 - Y), lin(Y - ankle + 0.035))
    lay = [(Y - ankle - 0.10, (0.17, 0.13, 0.09), MAT_LEATHER, 1)]
    BOOT, SOLE = (0.11, 0.085, 0.06), (0.05, 0.045, 0.04)

    def last(q, n, segs, grow, t0=0.0):
        # Колодка по n срезам ступни q от пятки к носку (с доли t0): скруглённый прямоугольник в срезе,
        # ширина и высота по коже с запасом grow, сглажены, чтобы пальцы слились в носок
        P = vl[q]
        c2 = P[:, [0, 2]].mean(0)
        dz = np.linalg.svd(P[:, [0, 2]] - c2)[2][0]
        dz = dz if dz[1] > 0 else -dz
        fw = np.array([dz[0], 0, dz[1]])
        sd = np.array([dz[1], 0, -dz[0]])
        u, v = P @ fw, P @ sd
        u0, u1 = u.min(), u.max()
        bot = P[:, 1].min() - 0.008
        ts = np.linspace(0, 1, n)
        sec = []
        for tt in ts:
            m = np.abs(u - (u0 + (u1 - u0) * tt)) < (u1 - u0) / n * 1.2
            m = m if m.sum() > 2 else np.abs(u - (u0 + (u1 - u0) * tt)) < (u1 - u0) / n * 2.5
            sec.append((v[m].min(), v[m].max(), P[m, 1].max()))
        sec = np.array(sec)
        for k in (0, 2):
            sec[:, k] = np.convolve(np.pad(sec[:, k], 1, mode='edge'), [0.25, 0.5, 0.25], 'valid')
        sec[:, 1] = np.convolve(np.pad(sec[:, 1], 1, mode='edge'), [0.25, 0.5, 0.25], 'valid')
        g = 2 * np.pi * np.arange(segs) / segs
        cx, cy = np.cos(g), np.sin(g)
        sx, sy = np.sign(cx) * np.abs(cx) ** 0.55, np.sign(cy) * np.abs(cy) ** 0.55
        pv = []
        for i, tt in enumerate(ts):
            if tt < t0 - 1e-6:
                continue
            lo, hi, top = sec[i]
            # Пятка и носок скруглены: срез у концов меньше
            e = 1.0 - 0.35 * max(0.0, 1.0 - min(tt, 1.0 - tt) * n / 1.5) ** 2
            hw, hh = (hi - lo) / 2 + grow, (top - bot) / 2 + grow * 0.5
            cv, cyc = (hi + lo) / 2, (top + bot) / 2
            if tt > 0.75:
                hh *= 1.0 - 0.6 * (tt - 0.75)
                cyc = bot + hh
            uu = u0 - grow + (u1 - u0 + 2 * grow) * tt
            for a_, b_ in zip(sx, sy):
                y = cyc + b_ * hh * e
                pv.append(fw * uu + sd * (cv + a_ * hw * e) + np.array([0, max(y, bot), 0]))
        pv = np.array(pv)
        k = len(pv) // segs
        pt = [[i * segs + j, i * segs + (j + 1) % segs, (i + 1) * segs + (j + 1) % segs] for i in range(k - 1) for j in range(segs)]
        pt += [[i * segs + j, (i + 1) * segs + (j + 1) % segs, (i + 1) * segs + j] for i in range(k - 1) for j in range(segs)]
        ends = [0, (k - 1) * segs]
        cs = [pv[e_:e_ + segs].mean(0) for e_ in ends]
        m0 = len(pv)
        pv = np.concatenate([pv, cs])
        pt += [[m0, j, (j + 1) % segs] for j in range(segs)] + [[m0 + 1, ends[1] + j, ends[1] + (j + 1) % segs] for j in range(segs)]
        pt = np.array(pt)
        pt = orient(pv, pt)
        low = pv[:, 1] < bot + 0.022
        return pv, pt, low
    out['boots'] = [[] for _ in range(NL)]
    for l in range(NL):
        out['boots'][l].append(shell(l, s, 0.03, (BOOT, MAT_LEATHER), lay, relax=6))
        for bn in (FT_L, FT_R):
            q = used[bone[used] == bn]
            n_, sg_ = (16, 12, 8, 5, 4)[l], (16, 12, 8, 6, 5)[l]
            pv, pt, low = last(q, n_, sg_, 0.009)
            col = np.where(low[:, None], np.array(SOLE), np.array((0.085, 0.065, 0.045)))
            out['boots'][l].append(rigid_part(pv, pt, col, MAT_LEATHER, bn, bone == bn))
            if l < 3:
                pv, pt, _ = last(q, n_, sg_, 0.012, t0=0.72)
                out['boots'][l].append(rigid_part(pv, pt, (0.26, 0.25, 0.22), MAT_METAL, bn, bone == bn))

    # Резиновые краги: перчатки с широким раструбом до середины предплечья
    s = field(soft(hand | fa), lin(wrist + 0.11 - Y))
    off = np.where(hand, 0.004, 0.006 + 0.09 * np.clip(Y - wrist - 0.02, 0, None))
    lay = [(Y - wrist - 0.095, (0.17, 0.16, 0.13), MAT_LEATHER, 1)]
    out['mitts'] = [[shell(l, s, off, ((0.10, 0.10, 0.085), MAT_LEATHER), lay, relax=2)] for l in range(NL)]

    # Пояс: стропа с пряжкой, подсумки и шланг петлёй у бедра сзади
    belt_y = hip + 0.075
    s = field(soft(np.isin(bone, [PELVIS, SPINE])), lin(0.022 - np.abs(Y - belt_y)))
    lay = [(np.minimum(0.025 - AX, Z), (0.30, 0.30, 0.28), MAT_METAL, 1)]
    band = np.isin(bone, [PELVIS, SPINE]) & (np.abs(Y - belt_y) < 0.01)

    def rad_at(a):
        # Радиус пояса в направлении a — по ближайшей вершине ленты
        k = np.where(band)[0][np.argmin(np.abs(np.angle(np.exp(1j * (ang[band] - a)))))]
        return np.hypot(X[k], Z[k])
    out['belt'] = []
    for l in range(NL):
        parts = [shell(l, s, 0.03, ((0.22, 0.22, 0.16), MAT_CLOTH), lay, relax=1)]
        for a in (-1.1, 1.1, 2.6):
            r = rad_at(a) + 0.052
            pv, pt = box(np.array([np.sin(a) * r, belt_y - 0.03, np.cos(a) * r]), (0.075, 0.09, 0.045), a)
            parts.append(rigid_part(pv, pt, (0.20, 0.20, 0.15), MAT_CLOTH, PELVIS, band))
        n_ = (16, 12, 7, 4, 3)[l]
        tt = np.linspace(0, 1, n_)
        aa = 2.6 - 0.75 * tt
        rr = np.array([rad_at(q) for q in aa]) + 0.045
        path = np.stack([np.sin(aa) * rr, belt_y - 0.075 - 0.13 * np.sin(np.pi * tt), np.cos(aa) * rr], 1)
        pv, pt = tube(path, np.stack([np.sin(aa), 0 * aa, np.cos(aa)], 1), np.full(n_, 0.022), np.full(n_, 0.022), (8, 6, 5, 3, 3)[l])
        rib = np.repeat(np.arange(n_), len(pv) // n_) % 2
        col = np.where(rib[:, None] == 0, np.array(RUBBER), np.array((0.11, 0.105, 0.095)))
        parts.append(rigid_part(pv, pt, col, MAT_LEATHER, PELVIS, band))
        out['belt'].append(parts)

    # Кожух: гранёный купол корпуса прибора до подбородка, смотровая щель, обод с болтами, рыло респиратора с двумя фильтрами
    up = hd & (Y > eye - 0.06)
    rx, rz = AX[up].max() + 0.018, (Z[up].max() - Z[up].min()) / 2 + 0.018
    chin = Y[hd & (Z > (Z[up].max() + Z[up].min()) / 2)].min()
    hc = np.array([0.0, chin - 0.01, (Z[up].max() + Z[up].min()) / 2])
    top = ctx['top_y'] + 0.022 - hc[1]
    lo, hi = eye - 0.012 - hc[1], eye + 0.008 - hc[1]
    out['bucket'] = []
    for l in range(NL):
        sg = 8
        prof = [(0.0, 1.06), (0.012, 1.04), (lo, 1.0), (lo, 1.0), (hi, 1.0), (hi, 1.0), (hi + 0.012, 1.0), (hi + 0.012, 1.0),
                (hi + 0.022, 1.0), (hi + 0.022, 1.0), (top - 0.085, 0.94), (top - 0.045, 0.80), (top - 0.015, 0.52), (top, 0.0)]
        pv, pt = lathe(hc, prof, sg, rx * 1.08, rz * 1.08, ph=np.pi / 8)
        col = np.tile((0.25, 0.27, 0.23), (len(pv), 1))
        front = np.cos(2 * np.pi * np.arange(sg) / sg + np.pi / 8) > 0.3
        col[3 * sg:5 * sg][np.tile(front, 2)] = (0.02, 0.02, 0.02)
        col[7 * sg:9 * sg] = (0.14, 0.13, 0.11)
        parts = [rigid_part(pv, pt, col, MAT_RUST, HEAD, hd)]
        zf = hc[2] + rz * 1.08 * np.cos(np.pi / 8)
        mo = np.array([0.0, eye - 0.07, zf + 0.016])
        pv, pt = box(mo, (0.07, 0.055, 0.04), 0.0)
        parts.append(rigid_part(pv, pt, RUBBER, MAT_LEATHER, HEAD, hd))
        for sx in (-1, 1):
            a = mo + np.array([sx * 0.03, -0.01, 0.0])
            pv, pt = cyl(a, a + np.array([sx * 0.03, -0.022, 0.022]), 0.02, (12, 10, 7, 5, 4)[l])
            parts.append(rigid_part(pv, pt, (0.24, 0.25, 0.19), MAT_METAL, HEAD, hd))
        if l < 2:
            for k in range(8):
                g = 2 * np.pi * k / 8
                c = hc + np.array([np.sin(g) * rx * 1.085, hi + 0.017, np.cos(g) * rz * 1.085])
                pv, pt = box(c, (0.01, 0.01, 0.01), g)
                parts.append(rigid_part(pv, pt, (0.20, 0.19, 0.17), MAT_METAL, HEAD, hd))
        if l < 3:
            c = hc + np.array([sL * rx * 1.08 * np.cos(np.pi / 8), (hi + 0.022 + top - 0.085) / 2, 0.0])
            pv, pt = glyph(np.random.default_rng(3), c, np.array([sL, 0, 0.0]), 0.05)
            parts.append(rigid_part(pv, pt, PAINT, MAT_RUST, HEAD, hd))
        out['bucket'].append(parts)

    # Ранец: рама из трубок на спине, ящик и поверх поперёк древний баллон со знаком
    back = trunk & (np.abs(Y - 1.27) < 0.03) & (AX < 0.05) & (Z < 0)
    bz = Z[back].min()
    s = field(soft(trunk), lin(0.02 - np.abs(AX - 0.105)), lin(Y - chest))
    out['pack'] = []
    for l in range(NL):
        sg = (12, 10, 7, 5, 4)[l]
        parts = [shell(l, s, 0.03, (RUBBER, MAT_LEATHER), relax=1)]
        z0 = bz - 0.035
        for x in (-0.11, 0.11):
            pv, pt = cyl(np.array([x, 1.04, z0]), np.array([x, 1.50, z0]), 0.011, max(4, sg // 2))
            parts.append(rigid_part(pv, pt, (0.22, 0.21, 0.19), MAT_METAL, CHEST, back))
        for y in (1.08, 1.46):
            pv, pt = cyl(np.array([-0.12, y, z0]), np.array([0.12, y, z0]), 0.009, max(4, sg // 2))
            parts.append(rigid_part(pv, pt, (0.22, 0.21, 0.19), MAT_METAL, CHEST, back))
        pv, pt = box(np.array([0.0, 1.23, z0 - 0.08]), (0.27, 0.26, 0.13), 0.0)
        parts.append(rigid_part(pv, pt, (0.15, 0.15, 0.10), MAT_LEATHER, CHEST, back))
        a, b = np.array([-0.17, 1.43, z0 - 0.085]), np.array([0.17, 1.43, z0 - 0.085])
        pv, pt = cyl(a, b, 0.062, sg)
        parts.append(rigid_part(pv, pt, (0.30, 0.33, 0.28), MAT_METAL, CHEST, back))
        for x in (-0.11, 0.11):
            pv, pt = cyl(np.array([x - 0.012, 1.43, z0 - 0.085]), np.array([x + 0.012, 1.43, z0 - 0.085]), 0.066, sg)
            parts.append(rigid_part(pv, pt, (0.12, 0.12, 0.11), MAT_METAL, CHEST, back))
        if l < 3:
            pv, pt = glyph(np.random.default_rng(5), np.array([0.0, 1.43, z0 - 0.085 - 0.062]), np.array([0, 0, -1.0]), 0.05, cols=2)
            parts.append(rigid_part(pv, pt, PAINT, MAT_METAL, CHEST, back))
        out['pack'].append(parts)
    # Рабочие штаны: тёмный хаки-брезент от пояса до щиколоток, светлые заплаты на коленях, накладной карман
    # на одном бедре, пояс и подгиб темнее. На поясе тоньше робы: рубаха поверх штанов; низ уходит в ботинки
    legs = np.isin(bone, [PELVIS, SPINE, TH_L, TH_R, SH_L, SH_R])
    s = field(soft(legs), lin(hip + 0.09 - Y), lin(Y - ankle - 0.05))
    lay = [(Y - hip - 0.072, (0.10, 0.10, 0.08), MAT_CLOTH, 1),
           (inside(thigh | shin, np.minimum(0.055 - np.abs(Y - knee - 0.01), (nrm0[:, 2] - 0.25) * 0.2)), (0.24, 0.23, 0.18), MAT_CLOTH, 1),
           (inside(thigh & side_l, np.minimum(0.05 - np.abs(Y - hip + 0.20), (np.abs(nrm0[:, 0]) - 0.55) * 0.2)), (0.12, 0.125, 0.095), MAT_CLOTH, 1),
           (inside(shin, ankle + 0.075 - Y), (0.11, 0.11, 0.085), MAT_CLOTH, 1),
           (inside(np.isin(bone, [PELVIS, SPINE]) & (Z > 0), np.minimum(0.004 - AX, Y - hip + 0.06)), (0.10, 0.10, 0.08), MAT_CLOTH, 0)]
    # Штанина свободная и книзу шире, как у брезента; ботинки и поножи сидят поверх
    off = np.where((thigh | shin) | (Y < hip), 0.012 + 0.011 * np.clip((hip - Y) / (hip - ankle), 0, 1), 0.005)
    out['pants'] = [[shell(l, s, off, ((0.16, 0.165, 0.125), MAT_CLOTH), lay, relax=6)] for l in range(NL)]

    # Бельё из небелёного холста: трусы с широкой тёмной резинкой; у мужчин короткие штанины,
    # у женщин вырез по бедру и лиф на лямках, перекрещенных на спине (не полоса без лямок).
    # Резинка на 3 см выше бедренного сустава, чем была: трусы сидели на бёдрах низко (пользователь 2026-10-30)
    mid = np.isin(bone, [PELVIS, SPINE, TH_L, TH_R]) & np.isin(np.arange(nvl), used) & (AX < 0.012) & (Y > 0.6) & (Y < hip)
    crotch = Y[mid].min()
    LINEN, BAND = (0.33, 0.31, 0.26), (0.17, 0.16, 0.13)
    hips = np.isin(bone, [PELVIS, SPINE, TH_L, TH_R])
    if ctx['female']:
        top, leg = hip + 0.105, crotch + 0.012 + 0.75 * np.maximum(0.0, AX - 0.025)
    else:
        top, leg = hip + 0.095, crotch - 0.045 + 0.08 * np.maximum(0.0, AX - 0.06)
    sb = field(soft(hips), lin(top - Y), lin(Y - leg))
    lb = [(Y - top + 0.017, BAND, MAT_CLOTH, 1), (inside(Y < leg + 0.012, leg + 0.007 - Y), BAND, MAT_CLOTH, 1)]
    t0 = src[0][0]
    # Кожа под лямками: грани LOD0 плеч, груди, спины и шеи
    near_t = t0[np.isin(bone[t0], [SPINE, CHEST, CLAV_L, CLAV_R, NECK, UA_L, UA_R]).all(1)]
    tri_a, tri_b, tri_c = vl[near_t[:, 0]], vl[near_t[:, 1]], vl[near_t[:, 2]]
    tri_n = np.cross(tri_b - tri_a, tri_c - tri_a)
    tri_n /= np.maximum(np.linalg.norm(tri_n, axis=1, keepdims=True), 1e-12)

    def ray(o, d):
        # Первое пересечение лучей o + d·t с кожей (Мёллер — Трумбор)
        e1, e2 = tri_b - tri_a, tri_c - tri_a
        pv = np.cross(d[:, None], e2[None])
        det = (e1[None] * pv).sum(2)
        tv = o[:, None] - tri_a[None]
        u = (tv * pv).sum(2) / np.where(np.abs(det) < 1e-12, 1e-12, det)
        qv = np.cross(tv, e1[None])
        v_ = (d[:, None] * qv).sum(2) / np.where(np.abs(det) < 1e-12, 1e-12, det)
        t = (e2[None] * qv).sum(2) / np.where(np.abs(det) < 1e-12, 1e-12, det)
        t = np.where((u >= 0) & (v_ >= 0) & (u + v_ <= 1) & (t > 0) & (np.abs(det) > 1e-12), t, np.inf)
        return o + d * t.min(1)[:, None]

    def closest(p):
        # Ближняя точка кожи к каждой точке p и нормаль там (по ближней грани)
        best, bn, bd = p.copy(), np.zeros_like(p), np.full(len(p), np.inf)
        ab, ac = tri_b - tri_a, tri_c - tri_a
        for i in range(len(p)):
            ap = p[i] - tri_a
            d1, d2 = (ab * ap).sum(1), (ac * ap).sum(1)
            # Проекция на плоскость и зажим в треугольник через барицентрические координаты
            dn = (ab * ab).sum(1) * (ac * ac).sum(1) - (ab * ac).sum(1) ** 2
            u = ((ac * ac).sum(1) * d1 - (ab * ac).sum(1) * d2) / np.maximum(dn, 1e-18)
            v_ = ((ab * ab).sum(1) * d2 - (ab * ac).sum(1) * d1) / np.maximum(dn, 1e-18)
            u, v_ = np.clip(u, 0, 1), np.clip(v_, 0, 1)
            k = np.maximum(u + v_, 1.0)
            q = tri_a + ab * (u / k)[:, None] + ac * (v_ / k)[:, None]
            d = np.linalg.norm(q - p[i], axis=1)
            j = d.argmin()
            best[i], bn[i], bd[i] = q[j], tri_n[j], d[j]
        return best, bn

    def strap(sg, x0, y0, yb, xb, k):
        # Лямка лифа лентой по коже: спереди вверх от чашки, через плечо, на спине наискось к другому боку.
        # Опорные точки — лучи на кожу спереди, сверху и сзади; потом лента натягивается (сглаживание)
        # и выталкивается наружу на 3.5 мм от ближней кожи: над впадиной у ключицы идёт мостом, в тело не уходит
        xs = sg * (x0 + 0.012)
        top = ray(np.array([[xs, 2.5, 0.0]]), np.array([[0.0, -1.0, 0.0]]))[0]
        fy = np.linspace(y0, top[1] - 0.035, 8)
        front = ray(np.stack([sg * x0 + (xs - sg * x0) * (fy - y0) / (top[1] - 0.035 - y0), fy, np.full(8, 1.0)], 1), np.tile([0.0, 0.0, -1.0], (8, 1)))
        bt = np.linspace(0, 1, 10)
        back = ray(np.stack([xs + (xb - xs) * bt, top[1] - 0.035 + (yb - top[1] + 0.035) * bt, np.full(10, -1.0)], 1), np.tile([0.0, 0.0, 1.0], (10, 1)))
        path = np.concatenate([front, [top], back])
        path = path[np.isfinite(path).all(1)]
        # Ровный шаг 6 мм
        def resample(q, m):
            sl = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(q, axis=0), axis=1))])
            u = np.linspace(0, sl[-1], m)
            return np.stack([np.interp(u, sl, q[:, c]) for c in range(3)], 1)
        sl = np.linalg.norm(np.diff(path, axis=0), axis=1).sum()
        q = resample(path, max(int(sl / 0.006), 8))
        for _ in range(40):
            q[1:-1] = 0.25 * q[:-2] + 0.5 * q[1:-1] + 0.25 * q[2:]
            c, cn = closest(q)
            gap = ((q - c) * cn).sum(1)
            q += cn * np.maximum(0.0, 0.0035 - gap)[:, None]
        q = resample(q, k)
        c, cn = closest(q)
        q += cn * np.maximum(0.0, 0.0035 - ((q - c) * cn).sum(1))[:, None]
        # Нормали ближних граней скачут: лента без перекрутов — по сглаженной нормали
        for _ in range(4):
            cn[1:-1] = 0.25 * cn[:-2] + 0.5 * cn[1:-1] + 0.25 * cn[2:]
        cn /= np.linalg.norm(cn, axis=1, keepdims=True)
        # Скиннинг и морфы — от ближней вершины кожи
        cv = cand[np.argmin(np.linalg.norm(vl[cand][None] - q[:, None], axis=2), axis=1)]
        pv, pt = tube(q, cn, np.full(len(q), 0.014), np.full(len(q), 0.003), 4)
        ring = np.arange(len(pv)) // 4
        d = md0[cv[ring]].copy()
        d[:, :, 3:] = 0
        k_ = len(pv)
        return ((pv, normals(pv, pt), np.tile(LINEN, (k_, 1)), np.full(k_, MAT_CLOTH), np.full(k_, TINT_NONE), sk[cv[ring]], d), pt)

    cand = used[np.isin(bone[used], [SPINE, CHEST, CLAV_L, CLAV_R, NECK])]
    straps = {}
    if ctx['female']:
        nip = J['breast.L____tail']
        lo = nip[1] - 0.075
        for sg in (1.0, -1.0):
            straps[sg] = [strap(sg, abs(nip[0]), nip[1] + 0.012, lo + 0.035, -sg * 0.06, n) for n in (40, 28, 14, 8, 6)]
    under = []
    for l in range(NL):
        parts = [shell(l, sb, 0.003, (LINEN, MAT_CLOTH), lb, 0.8)]
        if ctx['female']:
            torso = np.isin(bone, [SPINE, CHEST, CLAV_L, CLAV_R])
            # Чашки спереди до соска + 2.5 см, сзади лента 4.5 см; между чашками край плавно спускается
            # к узкой перемычке на грудине: в ложбину между грудей край не заходит и не ломается
            k = np.clip((Z - nip[2] + 0.14) / 0.08, 0, 1)
            k = k * k * (3 - 2 * k)
            gore = np.clip(1 - AX / 0.055, 0, 1)
            cup = lo + 0.045 + (nip[1] + 0.025 - lo - 0.045) * k
            hi = cup + (lo + 0.05 - cup) * k * gore * gore * (3 - 2 * gore)
            sbra = field(soft(torso), lin(Y - lo), lin(hi - Y))
            lw = [(inside(Y < lo + 0.012, lo + 0.012 - Y), BAND, MAT_CLOTH, 1)]
            parts.append(shell(l, sbra, 0.003, (LINEN, MAT_CLOTH), lw, 0.8))
            for sg in (1.0, -1.0):
                parts.append(straps[sg][l])
        under.append(merge(parts))
    return ({g: [merge(parts) for parts in out[g]] for g in GEAR}, hairdos(ctx), under)


def hairdos(ctx):
    # Причёски поверх кожи головы (переделаны 2026-10-30). Из чего собраны:
    # - объём: оболочка над кожей головы выше линии роста, толщина своя у каждой причёски
    #   (макушка, бока, лоб), к линии роста сходит на нет — без обода по краю;
    # - пряди: плоские гребни внахлёст по направлению расчёса, прижаты к объёму, кончики сужаются,
    #   у каждой свой оттенок, к кончику светлее;
    # - у длинных — «занавес»: волосы падают отвесно от самого широкого места головы
    #   (огибающая кожи и объёма сверху вниз), слой толщиной 0.5–1.2 см с прядями поверх;
    # - хвост, коса, пучок — жгуты из прядей у узла на затылке.
    # Скиннинг и морфы каждой точки — от ближней кожи головы, шеи и спины: длинное следует за телом
    vl, bone, J, src, sk = ctx['vl'], ctx['bone'], ctx['J'], ctx['src'], ctx['sk']
    soft, lin, only, field, shell = fabric(ctx)
    nvl = len(vl)
    X, Y, Z = vl[:, 0], vl[:, 1], vl[:, 2]
    AX = np.abs(X)
    hd, top_y, hcol, hk, fem = ctx['hd'][:nvl], ctx['top_y'], np.asarray(ctx['hcol']), ctx['hk'], ctx['female']
    n0, md0 = src[0][1], src[0][2]
    used = np.unique(src[0][0])
    j = lambda n: J[n + '____head']
    eye_y, neck_y, clav_y = j('eye.L')[1], j('neck01')[1], j('clavicle.L')[1]
    sst = lambda u: (lambda c: c * c * (3 - 2 * c))(np.clip(u, 0, 1))

    # Линия роста: на лбу 6.8 см ниже макушки (было 8.2 — лоб выглядел обритым), у висков ниже,
    # на затылке посередине до шеи, за ушами и по бокам шеи выше; у мужчин углы лба выше (лёгкие залысины)
    crown_m = hd & (Y > top_y - 0.12 * hk)
    zc = (Z[crown_m].min() + Z[crown_m].max()) / 2
    tz = sst((Z - zc + 0.05 * hk) / (0.13 * hk))
    temple = np.exp(-((AX - 0.042 * hk) / (0.015 * hk)) ** 2) * tz ** 3
    nape = 0.05 * sst((AX - 0.035 * hk) / (0.03 * hk)) * (1 - tz)
    line = top_y + hk * (-0.20 + nape + 0.132 * tz - 0.014 * np.clip(AX / (0.07 * hk), 0, 1) * tz + (0.004 if fem else 0.012) * temple)
    scalp = hd & (ctx['ear_d'][:nvl] > 1)
    s = field(soft(scalp), lin(Y - line))
    up = np.clip((Y - line) / 0.06, 0, 1)
    edge = sst((Y - line) / 0.014)
    cr = np.clip((Y - (top_y - 0.05 * hk)) / (0.05 * hk), 0, 1)

    # Кожа для прижатия прядей и для скиннинга
    qi = used[np.isin(bone[used], [HEAD, NECK])]
    qs = used[np.isin(bone[used], [HEAD, NECK, CHEST, SPINE, CLAV_L, CLAV_R])]

    def nearest(p, pool):
        # Ближняя вершина пула к каждой точке (кусками: без больших матриц)
        out = np.empty(len(p), int)
        q = vl[pool].astype(np.float32)
        qq = (q * q).sum(1)
        for a in range(0, len(p), 1024):
            b = p[a:a + 1024].astype(np.float32)
            d = qq[None] - 2 * b @ q.T
            out[a:a + 1024] = pool[d.argmin(1)]
        return out

    def part(pv, pt, col, mat=MAT_CLOTH):
        k = len(pv)
        q = nearest(pv, qs)
        d = md0[q].copy()
        d[:, :, 3:] = 0
        c = np.tile(col, (k, 1)) if np.ndim(col) == 1 else col
        return ((pv, normals(pv, pt), c, np.full(k, mat), np.full(k, TINT_HAIR), sk[q], d), pt)

    back = hd & (Y > top_y - 0.12 * hk) & (Y < top_y - 0.08 * hk)
    tie = np.array([0.0, top_y - 0.095 * hk, Z[back].min() - 0.004])
    crown = np.array([0.0, top_y, zc - 0.02])

    def roots(rng, n, mask, mind, band=(0.3, np.inf)):
        # Корни прядей без скучивания: случайный порядок, не ближе mind друг к другу; band — пределы поля s
        cand = np.where(mask & (s > band[0]) & (s < band[1]) & np.isin(np.arange(nvl), used))[0]
        rng.shuffle(cand)
        got = []
        P = np.empty((0, 3))
        for c in cand:
            if len(got) >= n:
                break
            if len(P) and (((P - vl[c]) ** 2).sum(1) < mind * mind).any():
                continue
            got.append(c)
            P = np.vstack([P, vl[c]])
        return np.array(got, int)

    def locks(rng, r, th, segs, length, width, h, comb, lift=-0.0008, slack=0.0015, fringe=None, jitter=0.0, tip=1.06):
        # Пряди от корней r: шаг по расчёсу comb(p, nrm) вдоль кожи, высота над кожей th + lift .. + slack.
        # fringe — нижний край чёлки: за линию роста спереди прядь идёт по лбу до этой высоты; иначе стоит
        n = len(r)
        if n == 0:
            return []
        p = vl[r] + n0[r] * (th[r] * 0.6)[:, None]
        nrm = n0[r]
        L = rng.uniform(*length, n)
        wob = rng.normal(0, 1, (n, 3)) * jitter
        path, ups, alive = [p], [nrm], np.ones(n, bool)
        for i in range(segs):
            d = comb(p, nrm) + wob * (i + 1) / segs
            d = d - (d * nrm).sum(1, keepdims=True) * nrm
            d /= np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-9)
            q = p + d * (L / segs)[:, None]
            k = nearest(q, qi)
            hgt = ((q - vl[k]) * n0[k]).sum(1)
            want = th[k] + lift
            q = q + n0[k] * (np.clip(hgt, want, want + slack) - hgt)[:, None]
            front = q[:, 2] > zc + 0.03
            if fringe is None:
                stop = (s[k] < -0.1) & front
            else:
                stop = (q[:, 1] < fringe) & front
            alive &= ~stop
            q = np.where(alive[:, None], q, p)
            k = np.where(alive, k, nearest(p, qi))
            nrm = n0[k]
            p = q
            path.append(p); ups.append(nrm)
        path, ups = np.stack(path, 1), np.stack(ups, 1)
        # Лента пряди лежит на объёме: каждая её точка прижата к нему по ближней коже (края — вплотную,
        # середина — выше на h, от этого бороздки), поэтому края не висят над головой и не режут объём.
        # Слой у каждой пряди свой (до 0.6 мм), чтобы внахлёст не мерцали
        out = []
        for i in range(n):
            pa, up_ = path[i], ups[i]
            keep = np.concatenate([[True], np.linalg.norm(np.diff(pa, axis=0), axis=1) > 1e-5])
            pa, up_ = pa[keep], up_[keep]
            if len(pa) < 2:
                continue
            m = len(pa)
            t = np.linspace(0, 1, m)
            wd = rng.uniform(*width) * (1 - 0.8 * sst((t - 0.35) / 0.65))
            tg = np.gradient(pa, axis=0)
            sd = np.cross(tg, up_)
            sd /= np.maximum(np.linalg.norm(sd, axis=1, keepdims=True), 1e-9)
            lay = rng.uniform(0.0, 0.0006)
            pts = np.concatenate([pa - sd * (wd / 2)[:, None], pa, pa + sd * (wd / 2)[:, None]])
            off = np.concatenate([np.full(m, 0.0004 + lay), 0.0004 + lay + h * (1 - 0.6 * t), np.full(m, 0.0004 + lay)])
            k = nearest(pts, qi)
            pts = pts + n0[k] * ((th[k] + off) - ((pts - vl[k]) * n0[k]).sum(1))[:, None]
            pt = []
            for j in range(m - 1):
                for c0 in (0, m):
                    a0, a1, b0, b1 = c0 + j, c0 + j + 1, c0 + m + j, c0 + m + j + 1
                    pt += [[a0, b0, b1], [a0, b1, a1]]
            pt = np.array(pt)
            fn = np.cross(pts[pt[:, 1]] - pts[pt[:, 0]], pts[pt[:, 2]] - pts[pt[:, 0]])
            flip = (fn * n0[k[pt[:, 0]]]).sum(1) < 0
            pt[flip] = pt[flip][:, ::-1]
            tone = rng.uniform(0.9, 1.05)
            c = hcol * tone * (0.92 + (tip - 0.92) * np.tile(t, 3))[:, None]
            out.append(part(pts, pt, c))
        return out

    # Огибающая головы для «занавеса»: полярная сетка вокруг вертикали через середину головы
    ang = lambda p: np.arctan2(p[:, 0], -(p[:, 2] - zc))  # 0 — затылок, ±π — лицо
    def envelope(th, with_body):
        pts = vl[scalp] + n0[scalp] * th[scalp][:, None]
        sel = np.isin(bone, [HEAD, NECK] + ([CHEST, SPINE, CLAV_L, CLAV_R] if with_body else []))
        sel &= np.isin(np.arange(nvl), used) & ~((Z > zc) & (bone == HEAD) & (AX < 0.05))
        pts = np.concatenate([pts, vl[sel]])
        A, R = ang(pts), np.hypot(pts[:, 0], pts[:, 2] - zc)
        na, y0, dy = 120, pts[:, 1].min(), 0.005
        ny = int((pts[:, 1].max() - y0) / dy) + 2
        ia = ((A + np.pi) / (2 * np.pi) * na).astype(int) % na
        iy = ((pts[:, 1] - y0) / dy).astype(int)
        g = np.zeros((na, ny))
        np.maximum.at(g, (ia, iy), R)
        # Сверху вниз: волосы падают отвесно, ниже самого широкого места радиус не уменьшается
        g = np.maximum.accumulate(g[:, ::-1], axis=1)[:, ::-1]
        g = np.maximum(g, np.roll(g, 1, 0) * 0.98)
        g = np.maximum(g, np.roll(g, -1, 0) * 0.98)
        for _ in range(3):
            g = 0.25 * np.roll(g, 1, 0) + 0.5 * g + 0.25 * np.roll(g, -1, 0)
        def r_at(a, y):
            fa = (a + np.pi) / (2 * np.pi) * na - 0.5
            fy = np.clip((y - y0) / dy, 0, ny - 1.001)
            a0, y0i = np.floor(fa).astype(int), np.floor(fy).astype(int)
            wa, wy = fa - a0, fy - y0i
            a0 %= na
            a1 = (a0 + 1) % na
            v0 = g[a0, y0i] * (1 - wy) + g[a0, y0i + 1] * wy
            v1 = g[a1, y0i] * (1 - wy) + g[a1, y0i + 1] * wy
            return v0 * (1 - wa) + v1 * wa
        return r_at

    def curtain(rng, l, th, y_top, y_end, amax, thick, nstr, with_body=False, flare=0.012):
        # Слой волос от y_top вниз до y_end(угол) в секторе |угол| < amax (от затылка), отвесно по огибающей.
        # Наружная и внутренняя поверхность, нижний и боковые края; сверху пряди с неровными кончиками
        r_at = envelope(th, with_body)
        na, ny = (36, 24, 12, 8, 6)[l], (10, 7, 4, 3, 2)[l]
        A = np.linspace(-amax, amax, na)
        yb = y_end(A)
        u = np.linspace(0, 1, ny)
        Ya = y_top + (yb[None, :] - y_top) * u[:, None]
        drop = np.clip((y_top - Ya) / 0.25, 0, 1)
        Rr = r_at(np.tile(A, (ny, 1)).ravel(), Ya.ravel()).reshape(ny, na) - 0.002 + 0.003 + flare * drop
        dirs = np.stack([np.sin(A), np.zeros(na), -np.cos(A)], 1)
        tk = thick * (1 - 0.55 * u)[:, None] * np.ones((1, na))
        outer = np.array([0, 0, zc]) + dirs[None] * Rr[:, :, None] + np.stack([np.zeros_like(Ya), Ya, np.zeros_like(Ya)], 2)
        inner = np.array([0, 0, zc]) + dirs[None] * (Rr - tk)[:, :, None] + np.stack([np.zeros_like(Ya), Ya, np.zeros_like(Ya)], 2)
        pv = np.concatenate([outer.reshape(-1, 3), inner.reshape(-1, 3)])
        m = ny * na
        idx = lambda i, k: i * na + k
        # Грани и куда они смотрят: наружная — от оси, внутренняя — к оси, низ — вниз, бока — из сектора
        pt, want = [], []
        for i in range(ny - 1):
            for k in range(na - 1):
                a, b, c, d = idx(i, k), idx(i, k + 1), idx(i + 1, k + 1), idx(i + 1, k)
                pt += [[a, b, c], [a, c, d], [m + a, m + b, m + c], [m + a, m + c, m + d]]
                want += [1, 1, -1, -1]
        for k in range(na - 1):
            a, b = idx(ny - 1, k), idx(ny - 1, k + 1)
            pt += [[a, b, m + b], [a, m + b, m + a]]
            want += [2, 2]
        for i in range(ny - 1):
            for k in (0, na - 1):
                a, d = idx(i, k), idx(i + 1, k)
                pt += [[a, d, m + d], [a, m + d, m + a]]
                want += [3 if k else -3] * 2
        pt, want = np.array(pt), np.array(want)
        fn = np.cross(pv[pt[:, 1]] - pv[pt[:, 0]], pv[pt[:, 2]] - pv[pt[:, 0]])
        cen = pv[pt].mean(1)
        rad = cen - np.array([0, 0, zc]); rad[:, 1] = 0
        tng = np.stack([-rad[:, 2], np.zeros(len(rad)), rad[:, 0]], 1)  # по росту угла
        dirw = np.where((np.abs(want) == 1)[:, None], rad * np.sign(want)[:, None],
                        np.where((want == 2)[:, None], np.array([0, -1.0, 0]), tng * np.sign(want)[:, None]))
        flip = (fn * dirw).sum(1) < 0
        pt[flip] = pt[flip][:, ::-1]
        col = np.concatenate([np.tile(hcol * 0.78, (m, 1)), np.tile(hcol * 0.55, (m, 1))])
        out = [part(pv, pt, col)]
        # Пряди по наружной поверхности, от верха вниз, кончики разной длины
        if nstr:
            ka = rng.uniform(-amax * 0.97, amax * 0.97, nstr)
            seg = (8, 6, 3, 2, 2)[l]
            for a in ka:
                ye = y_end(np.array([a]))[0] + rng.uniform(-0.035, 0.012)
                ys = y_top + rng.uniform(-0.01, 0.03)
                yy = ys + (ye - ys) * np.linspace(0, 1, seg + 1)
                dd = np.clip((y_top - yy) / 0.25, 0, 1)
                rr = r_at(np.full(seg + 1, a), yy) + 0.003 + flare * dd + 0.0012
                dv = np.array([np.sin(a), 0, -np.cos(a)])
                pa = np.array([0, 0, zc]) + dv[None] * rr[:, None] + np.stack([np.zeros_like(yy), yy, np.zeros_like(yy)], 1)
                t = np.linspace(0, 1, seg + 1)
                wd = rng.uniform(0.028, 0.04) * (1 - 0.8 * t ** 2)
                pv2, pt2 = tube(pa, np.tile(dv, (seg + 1, 1)), wd, np.full(seg + 1, 0.0022), 2)
                c = hcol * rng.uniform(0.86, 1.08) * (0.92 + 0.14 * np.repeat(t, 3))[:, None]
                out.append(part(pv2, pt2, c))
        return out

    def rope(L, r0, k, d0, sag=0.04):
        # Ось жгута от узла: сначала назад-вниз, потом под тяжестью вниз; не ближе r + 5 мм к коже спины и шеи
        ts = np.linspace(0, 1, k)
        d0 = np.asarray(d0, float) / np.linalg.norm(d0)
        pa = [tie]
        ds = L / (k - 1)
        dcur = d0
        for i in range(1, k):
            dcur = dcur * 0.62 + np.array([0, -1.0, 0]) * 0.38
            dcur /= np.linalg.norm(dcur)
            pa.append(pa[-1] + dcur * ds)
        pa = np.array(pa)
        body = used[np.isin(bone[used], [HEAD, NECK, CHEST, SPINE, CLAV_L, CLAV_R])]
        for _ in range(12):
            q = nearest(pa, body)
            gap = np.linalg.norm(pa - vl[q], axis=1)
            need = r0(ts) + 0.005
            push = np.maximum(0, need - gap)
            dirn = (pa - vl[q]) / np.maximum(gap, 1e-6)[:, None]
            pa[1:] += (dirn * push[:, None])[1:]
            pa[1:-1] = 0.25 * pa[:-2] + 0.5 * pa[1:-1] + 0.25 * pa[2:]
        return pa, ts

    def frame(pa):
        tg = np.gradient(pa, axis=0)
        tg /= np.linalg.norm(tg, axis=1, keepdims=True)
        sd = np.cross(tg, [0.0, 0.0, -1.0])
        bad = np.linalg.norm(sd, axis=1) < 1e-3
        sd[bad] = np.cross(tg[bad], [1.0, 0, 0])
        sd /= np.linalg.norm(sd, axis=1, keepdims=True)
        nb = np.cross(sd, tg)
        return tg, sd, nb

    def bundle(rng, l, pa, ts, rad, nstr, core_col=0.7):
        # Жгут: ядро и пряди-гребни по его поверхности с лёгким закручиванием, кончики разной длины
        tg, sd, nb = frame(pa)
        sides = (10, 8, 6, 5, 4)[l]
        r = rad(ts)
        pv, pt = tube(pa, nb, 2 * r * 0.92, 2 * r * 0.92, sides)
        out = [part(pv, pt, np.tile(hcol * core_col, (len(pv), 1)))]
        for k in range(nstr):
            a0 = 2 * np.pi * (k + rng.uniform(-0.3, 0.3)) / nstr
            tw = rng.uniform(0.6, 1.2)
            end = rng.uniform(0.82, 1.0)
            m = ts <= end
            a = a0 + tw * ts[m]
            rad_dir = np.cos(a)[:, None] * sd[m] + np.sin(a)[:, None] * nb[m]
            p = pa[m] + rad_dir * (r[m] * 0.95)[:, None]
            t = ts[m] / end
            wd = (2 * np.pi * r[m] / nstr) * rng.uniform(1.8, 2.4) * (1 - 0.8 * t ** 2)
            pv, pt = tube(p, rad_dir, wd, np.full(len(p), 0.0022) * (1 - 0.5 * t), 2)
            c = hcol * rng.uniform(0.86, 1.08) * (0.9 + 0.16 * np.repeat(t, 3))[:, None]
            out.append(part(pv, pt, c))
        return out

    def band(pa, r):
        tg, sd, nb = frame(pa[:2])
        p = np.array([pa[0] - tg[0] * 0.006, pa[0] + tg[0] * 0.006])
        pv, pt = tube(p, np.tile(nb[0], (2, 1)), np.full(2, 2 * r + 0.004), np.full(2, 2 * r + 0.004), 8)
        return [part(pv, pt, np.tile((0.12, 0.09, 0.07), (len(pv), 1)))]

    # Направления расчёса
    def from_crown(g, fwd=0.0):
        def f(p, nrm):
            d = p - crown
            d[:, 1] = 0
            d /= np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-9)
            # У макушки «от макушки» не определено: там расчёс назад
            d += np.array([0, -g, -0.35])
            d[:, 2] += fwd * (p[:, 2] > zc + 0.02)
            return d
        return f
    to_tie = lambda p, nrm: tie - p
    def parted(px, fr):
        # Пробор по px (0 — посередине): от пробора в стороны и вниз; спереди у лба — вперёд (чёлка), если fr
        def f(p, nrm):
            sgn = np.where(p[:, 0] >= px, 1.0, -1.0)
            d = np.stack([sgn * 0.9, np.full(len(p), -1.0), np.full(len(p), -0.25)], 1)
            if fr:
                fz = sst((p[:, 2] - zc - 0.035) / 0.02)
                d = d * (1 - fz)[:, None] + np.array([0.0, -0.8, 1.0]) * fz[:, None]
            return d
        return f
    slick = lambda p, nrm: np.stack([0.15 * np.sign(p[:, 0]), np.full(len(p), -0.25), np.full(len(p), -1.0)], 1)
    def swept(p, nrm):
        # На бок: сверху к левому виску, чёлка косо на лоб
        return np.stack([np.full(len(p), -1.0), np.full(len(p), -0.35), 0.3 + 0.4 * (p[:, 2] > zc)], 1)

    styles = HAIR_FEMALE if fem else HAIR_MALE
    if 'only' in ctx:
        styles = [st for st in styles if st in ctx['only']]
    mask_all = np.ones(nvl, bool)
    fringe_y = eye_y + 0.028 * hk
    chin_y = eye_y - 0.105 * hk
    res = {}
    for st in styles:
        # Толщина объёма по месту на голове
        th = {
            'crop': 0.006 + 0.008 * up + 0.003 * cr,
            'shag': 0.009 + 0.012 * up,
            'tied': 0.003 + 0.004 * up,
            'buzz': 0.0032 + 0.0012 * up,
            'slick': 0.004 + 0.004 * up + 0.018 * up * tz ** 2,
            'long': 0.006 + 0.010 * up,
            'bob': 0.006 + 0.012 * up,
            'loose': 0.005 + 0.009 * up,
            'side': 0.003 + (0.010 + 0.010 * up) * sst((Y - (top_y - 0.085 * hk)) / (0.025 * hk)),
        }.get(st, 0.004 + 0.004 * up)
        th = np.maximum(th * edge, 0.0006)
        lods = []
        for l in range(NL):
            rng = np.random.default_rng(sum(map(ord, st)) * 7 + l)
            kk = LOCKS[l]
            segs = (6, 5, 2, 2, 2)[l]
            parts = [shell(l, s, th, (hcol * 0.72, MAT_CLOTH), [], 0.7, relax=3, tint=TINT_HAIR)]
            md = (0.009, 0.013, 0.025, 1, 1)[l]
            R = lambda n, mask=mask_all, k=1.0: roots(rng, int(n * kk * k), mask, md / max(k, 0.5))
            if st == 'crop':
                parts += locks(rng, R(340), th, segs, (0.04, 0.07), (0.016, 0.026), 0.0016, from_crown(0.3, 0.8 if fem else 0.5),
                               fringe=fringe_y + 0.012, jitter=0.12)
            elif st == 'shag':
                parts += curtain(rng, l, th, eye_y + 0.035 * hk, lambda a: np.full(len(a), eye_y - 0.045 * hk - 0.01 * np.cos(a)),
                                 np.radians(112), 0.008, int(36 * kk))
                parts += locks(rng, R(320), th, segs, (0.07, 0.11), (0.018, 0.028), 0.002, parted(0.0, True),
                               fringe=fringe_y, jitter=0.3, slack=0.003)
            elif st in ('tied', 'tail', 'braid', 'bun'):
                parts += locks(rng, R(280), th, segs, (0.06, 0.12), (0.018, 0.028), 0.0014, to_tie, slack=0.0008)
                if st == 'tied':
                    pa, ts = rope(0.075, lambda t: 0.014 * (1 - 0.7 * t ** 2), (8, 6, 4, 3, 3)[l], (0, -0.2, -1))
                    rad = lambda t: 0.014 * (0.8 + 0.4 * np.sin(np.pi * t) ** 0.6) * (1 - 0.8 * t ** 2.5)
                    parts += bundle(rng, l, pa, ts, rad, (12, 9, 5, 0, 0)[l]) + band(pa, 0.009)
                elif st == 'tail':
                    rad = lambda t: 0.022 * (0.75 + 0.5 * np.sin(np.pi * t) ** 0.6) * (1 - 0.85 * t ** 2.5)
                    pa, ts = rope(0.30, rad, (14, 10, 6, 4, 3)[l], (0, -0.7, -1))
                    parts += bundle(rng, l, pa, ts, rad, (24, 16, 8, 0, 0)[l]) + band(pa, 0.012)
                elif st == 'braid':
                    rad = lambda t: 0.016 * (1 - 0.45 * t)
                    k = (49, 37, 19, 9, 7)[l]
                    pa, ts = rope(0.30, rad, k, (0, -0.4, -1))
                    tg, sd, nb = frame(pa)
                    per = 0.042
                    w = 2 * np.pi * (0.30 / per)
                    rs = 0.0105 * (1 - 0.45 * ts)
                    for i in range(3):
                        ph = w * ts + 2 * np.pi * i / 3
                        p = pa + (np.sin(ph) * rs * 1.1)[:, None] * sd + (0.55 * np.sin(2 * ph) * rs)[:, None] * nb
                        pv, pt = tube(p, nb, 2.3 * rs, 1.7 * rs, (6, 6, 4, 4, 3)[l])
                        seg = np.repeat(np.arange(len(p)), (6, 6, 4, 4, 3)[l])
                        tone = hcol * (0.92 + 0.06 * i)
                        parts.append(part(pv, pt, tone * np.where((seg // max(1, k // 14)) % 2 == 0, 0.9, 1.04)[:, None]))
                    # Перехват и кисточка на конце
                    tail_pa = pa[-1] + np.array([[0, 0, 0], [0, -0.025, -0.004], [0, -0.05, -0.006]])
                    parts += band(np.array([pa[-1], pa[-1] - tg[-1] * 0.01]), 0.007)
                    parts += bundle(rng, l, tail_pa, np.linspace(0, 1, 3), lambda t: 0.009 * (1 + 0.4 * t) * (1 - 0.7 * t ** 3), (8, 6, 0, 0, 0)[l])
                    parts += band(np.array([tie, tie + tg[0] * 0.01]), 0.013)
                else:
                    # Пучок: жгут, закрученный спиралью в купол над узлом
                    ax_ = np.array([0, 0.35, -1.0]); ax_ /= np.linalg.norm(ax_)
                    e1 = np.array([1.0, 0, 0]); e2 = np.cross(ax_, e1)
                    k = (60, 40, 20, 10, 8)[l]
                    t = np.linspace(0, 1, k)
                    a = 2 * np.pi * 1.8 * t
                    R_ = 0.036 * (1 - 0.75 * t)
                    hgt = 0.006 + 0.028 * np.sin(np.pi / 2 * t)
                    pa = tie + ax_[None] * hgt[:, None] + R_[:, None] * (np.cos(a)[:, None] * e1 + np.sin(a)[:, None] * e2)
                    rr = 0.017 * (1 - 0.45 * t)
                    pv, pt = tube(pa, np.tile(ax_, (k, 1)), 2 * rr, 2 * rr, (8, 6, 5, 4, 4)[l])
                    seg = np.repeat(np.arange(k), (8, 6, 5, 4, 4)[l])
                    parts.append(part(pv, pt, hcol * (0.86 + 0.12 * ((seg // 3) % 2))[:, None]))
                    pv, pt = lathe(tie + ax_ * 0.004, [(0.0, 0.03), (0.012, 0.028), (0.022, 0.0)], 10, 1.0, 0.7)
                    parts.append(part(pv, pt, np.tile(hcol * 0.7, (len(pv), 1))))
            elif st == 'buzz':
                parts += locks(rng, roots(rng, int((700, 350, 90, 0, 0)[l]), mask_all, (0.005, 0.008, 0.016, 1, 1)[l]), th, 2,
                               (0.012, 0.02), (0.007, 0.011), 0.0008, from_crown(0.4, 0.3), jitter=0.2)
            elif st == 'slick':
                parts += locks(rng, R(300), th, segs, (0.12, 0.18), (0.016, 0.024), 0.0022, slick, slack=0.0008)
            elif st in ('long', 'loose', 'bob'):
                if st == 'bob':
                    y_end = lambda a: chin_y - 0.012 * np.abs(a) / np.pi
                    amax, thick, ns, wb = np.radians(132), 0.012, 56, False
                elif st == 'long':
                    # Сзади до плеч, у лица короче — до подбородка
                    y_end = lambda a: neck_y - 0.035 + (chin_y - neck_y + 0.035) * sst((np.abs(a) - np.radians(80)) / np.radians(35))
                    amax, thick, ns, wb = np.radians(118), 0.010, 48, False
                else:
                    y_long, y_sh = neck_y - 0.30, chin_y - 0.02
                    y_end = lambda a: y_long + (y_sh - y_long) * sst((np.abs(a) - np.radians(50)) / np.radians(40))
                    amax, thick, ns, wb = np.radians(118), 0.010, 64, True
                y_top = eye_y + 0.045 * hk
                parts += curtain(rng, l, th, y_top, y_end, amax, thick, int(ns * kk), with_body=wb)
                parts += locks(rng, R(300), th, segs, (0.08, 0.13), (0.018, 0.028), 0.0018, parted(0.0, st == 'bob'),
                               fringe=fringe_y if st == 'bob' else None, slack=0.002)
            elif st == 'side':
                top = sst((Y - (top_y - 0.085 * hk)) / (0.025 * hk)) > 0.5
                parts += locks(rng, roots(rng, int((450, 220, 60, 0, 0)[l]), ~top, (0.005, 0.008, 0.016, 1, 1)[l]), th, 2,
                               (0.012, 0.02), (0.007, 0.011), 0.0008, from_crown(0.6), jitter=0.2)
                parts += locks(rng, R(220, top), th, segs, (0.08, 0.13), (0.018, 0.028), 0.002, swept, fringe=fringe_y, slack=0.002)
            lods.append(merge(parts))
        res[st] = lods
    return res


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


def skin_weights(acc, bone):
    # До 4 костей на вершину: самые тяжёлые, сумма 1. Строка: 4 номера, 4 веса
    top = np.argsort(-acc, 1)[:, :4]
    w = np.take_along_axis(acc, top, 1)
    w[w < 0.02] = 0
    s = w.sum(1)
    w[s < 1e-6] = (1, 0, 0, 0)
    top[s < 1e-6, 0] = bone[s < 1e-6]
    w /= w.sum(1, keepdims=True)
    top[w == 0] = top[:, :1].repeat(4, 1)[w == 0]
    return np.concatenate([top, w], 1)


def rigid(bone):
    return np.concatenate([np.asarray(bone)[:, None].repeat(4, 1), np.tile((1.0, 0, 0, 0), (len(bone), 1))], 1)


def split(v, n, t, fp, pal, sk, md):
    # Вершины делятся по окраске граней: край белья и губ чёткий, без размытия по треугольнику.
    # Часть сетки: позиции, нормали, цвета, материалы, классы оттенка, скиннинг, сдвиги морфов
    K = len(pal)
    uk, kinv = np.unique((t.astype(np.int64) * K + fp[:, None]).ravel(), return_inverse=True)
    src, kp = uk // K, pal[uk % K]
    return (v[src], n[src], kp[:, :3], kp[:, 3].astype(int), kp[:, 4].astype(int), sk[src], md[src]), kinv.reshape(-1, 3)


def merge(parts):
    # Части в одну сетку; остаются только вершины, на которые ссылаются треугольники
    base, ts = 0, []
    for (pv, *_), t in parts:
        ts.append(t + base); base += len(pv)
    cols = [np.concatenate([np.asarray(pa[0][k]) for pa in parts]) for k in range(len(parts[0][0]))]
    T = np.concatenate(ts)
    live = np.unique(T)
    remap = np.full(len(cols[0]), -1); remap[live] = np.arange(len(live))
    return [c[live] for c in cols] + [remap[T]]


def write_mesh(path, V, N, C, M, L, S, D, T):
    # AMSH 3: после индексов поток скиннинга, на вершину u8 кости[4] и u8 веса[4] (сумма 255);
    # затем u32 число морфов и сдвиги: на вершину и морф три u32 = snorm16 пары (dx dy) (dz nx) (ny nz),
    # позиция в долях MORPH_POS, нормаль в долях MORPH_NRM
    w = np.floor(S[:, 4:] * 255).astype(int)
    w[:, 0] += 255 - w.sum(1)
    sk = np.concatenate([S[:, :4].astype(int), w], 1).astype('u1')
    dt = np.dtype([('pos', '<f4', 3), ('nrm', '<f4', 3), ('col', 'u1', 4), ('mat', 'u1'), ('bone', 'u1'), ('pad', 'u1', 2)])
    a = np.zeros(len(V), dt)
    a['pos'] = V; a['nrm'] = N
    a['col'][:, :C.shape[1]] = np.clip(np.round(C * 255), 0, 255)
    if C.shape[1] == 3:
        a['col'][:, 3] = 255
    a['mat'] = M; a['bone'] = sk[:, 0]; a['pad'][:, 0] = L
    q = D / np.array([MORPH_POS] * 3 + [MORPH_NRM] * 3)
    assert np.abs(q).max() <= 1.0, 'сдвиг морфа вне диапазона файла'
    q = np.round(q * 32767).astype('<i2')
    with open(path, 'wb') as f:
        f.write(b'AMSH' + struct.pack('<III', 3, len(V), T.size))
        f.write(a.tobytes())
        f.write(T.astype('<u4').tobytes())
        f.write(sk.tobytes())
        f.write(struct.pack('<I', D.shape[1]))
        f.write(q.tobytes())


def main():
    mh, npz, out = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(out + '/gear', exist_ok=True)
    base, faces = load_obj(mh + '/base.obj')
    Z = np.load(npz, allow_pickle=False)
    skel = json.load(open(mh + '/default.mhskel'))
    W = json.load(open(mh + '/default_weights.mhw'))['weights']
    os.makedirs(out + '/hair', exist_ok=True)
    for p in PEOPLE:
        lods, J, gear, hair = build(p, base, faces, Z, skel, W)
        for g, gl in hair.items():
            for l, mesh in enumerate(gl):
                write_mesh('%s/hair/%s_%s%s.mesh' % (out, p['name'], g, '_lod%d' % l if l else ''), *mesh)
            print('  волосы %-6s %s треугольников' % (g, ' / '.join(str(len(m[-1])) for m in gl)))
        for g, gl in gear.items():
            for l, mesh in enumerate(gl):
                write_mesh('%s/gear/%s_%s%s.mesh' % (out, p['name'], g, '_lod%d' % l if l else ''), *mesh)
            print('  %-8s %s треугольников' % (g, ' / '.join(str(len(m[-1])) for m in gl)))
        for l, mesh in enumerate(lods):
            name = p['name'] + ('_lod%d' % l if l else '')
            write_mesh('%s/%s.mesh' % (out, name), *mesh)
            V, T = mesh[0], mesh[-1]
            print('%-15s %5d вершин %6d треугольников' % (name, len(V), len(T)))
        # Суставы тела для src/game/look.c (g_body_joints, порядок BONE_*): голова первой кости MH, что ведёт кость игры;
        # мужские — и в src/sim/body.c (g_body_bones). Правые — зеркало левых
        jt = {b: J[n + '____head'] for b, n in JOINT_SRC}
        order = ['ROOT', 'PELVIS', 'SPINE', 'CHEST', 'NECK', 'HEAD'] + ['%s_%s' % (k, sd) for sd in 'LR' for k in ('CLAVICLE', 'UPPER_ARM', 'FOREARM', 'HAND')] \
            + ['%s_%s' % (k, sd) for sd in 'LR' for k in ('THIGH', 'SHIN', 'FOOT')]
        def at(b):
            if b == 'ROOT':
                return np.zeros(3)
            if b.endswith('_R'):
                return jt[b[:-2] + '_L'] * np.array([-1.0, 1.0, 1.0])
            return jt[b]
        print('  [%s] = {' % p['name'])
        for i in range(0, len(order), 4):
            print('    ' + ' '.join('{ %.3ff, %.3ff, %.3ff },' % tuple(np.round(at(b), 3) + 0.0) for b in order[i:i + 4]))
        print('  },')

if __name__ == '__main__':
    main()
