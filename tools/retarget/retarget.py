# Ретаргет захвата движения на скелеты человека (docs/anim.md). Blender как модуль Python (pip install bpy)
# читает BVH (CMU) и FBX (Mixamo) и считает мир костей источника по кадрам; дальше — свой счёт в numpy:
# повороты костей в мире переносятся на кости цели с поправкой «покой источника ↔ покой цели».
#   python3 tools/retarget/retarget.py [клип…]          все клипы из clips.txt или только названные
#   python3 tools/retarget/retarget.py --probe файл [с0 с1]   скорости руки и высота таза по 0.1 с (нарезка)
# Сырые файлы — в downloads/anims (не в git, fetch.sh), выход — data/anims/<клип>.<скелет>.anim.
# Байты выхода одни при повторном запуске (тот же bpy): всё в f64, на выходе — целые
import os
import sys
import numpy as np
import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from animlib import Skel, Anim, KEYS_PER_S, LOOP, m_to_q, rot_between, yaw_m, read_labels  # noqa: E402
import clean  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RAW = os.path.join(ROOT, 'downloads', 'anims')
OUT = os.path.join(ROOT, 'data', 'anims')
BODY = 'human_m'
SKELS = ['game_engine', 'mixamo']

# Общие имена костей — как у Mixamo без приставки. Порядок — родитель раньше детей
CANON = ['Hips', 'Spine', 'Spine1', 'Spine2', 'Neck', 'Head']
for s in ('Left', 'Right'):
    CANON += [s + 'Shoulder', s + 'Arm', s + 'ForeArm', s + 'Hand', s + 'UpLeg', s + 'Leg', s + 'Foot', s + 'ToeBase']

# Источник: общее имя → кость файла. CMU (BVH Б. Хане): позвоночник на одну кость сдвинут
SRC_CMU = {c: c for c in CANON}
SRC_CMU.update({'Spine': 'LowerBack', 'Spine1': 'Spine', 'Spine2': 'Spine1'})
# Конец кости для направления (только конечности): общее имя → кость, чья голова — конец
TIP = {}
for s in ('Left', 'Right'):
    TIP.update({s + 'Shoulder': s + 'Arm', s + 'Arm': s + 'ForeArm', s + 'ForeArm': s + 'Hand',
                s + 'UpLeg': s + 'Leg', s + 'Leg': s + 'Foot', s + 'Foot': s + 'ToeBase'})
SRC_HAND_TIP = {'cmu': '{}HandIndex1', 'mixamo': '{}HandMiddle1'}

# Цель: общее имя → кость скелета
TGT = {
    'game_engine': {'Hips': 'pelvis', 'Spine': 'spine_01', 'Spine1': 'spine_02', 'Spine2': 'spine_03',
                    'Neck': 'neck_01', 'Head': 'head'},
    'mixamo': {c: 'mixamorig:' + c for c in CANON},
}
for s, k in (('Left', 'l'), ('Right', 'r')):
    TGT['game_engine'].update({s + 'Shoulder': 'clavicle_' + k, s + 'Arm': 'upperarm_' + k, s + 'ForeArm': 'lowerarm_' + k,
                               s + 'Hand': 'hand_' + k, s + 'UpLeg': 'thigh_' + k, s + 'Leg': 'calf_' + k,
                               s + 'Foot': 'foot_' + k, s + 'ToeBase': 'ball_' + k})
TGT_HAND_TIP = {'game_engine': 'middle_01_{}', 'mixamo': 'mixamorig:{}HandMiddle1'}


def ortho(m):
    """Ближайшая ортонормированная (у FBX в матрице масштаб)."""
    u, _, vt = np.linalg.svd(m)
    r = u @ vt
    return r if np.linalg.det(r) > 0 else u @ np.diag([1, 1, -1]) @ vt


# ---- источник через bpy
class Source:
    def __init__(self, path):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        sc = bpy.context.scene
        self.kind = 'cmu' if path.endswith('.bvh') else 'mixamo'
        if self.kind == 'cmu':
            bpy.ops.import_anim.bvh(filepath=path, global_scale=1.0, frame_start=1, use_fps_scale=False,
                                    update_scene_fps=True, update_scene_duration=True, rotate_mode='NATIVE')
            self.first = 1  # кадр файла 0 (у CMU — T-поза Хане) в сцене — 1
        else:
            sc.render.fps = 30
            bpy.ops.import_scene.fbx(filepath=path, use_anim=True, anim_offset=1.0, ignore_leaf_bones=False,
                                     automatic_bone_orientation=False)
            self.first = 1
        self.ob = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
        self.fps = sc.render.fps / sc.render.fps_base
        act = self.ob.animation_data.action if self.ob.animation_data else None
        self.length = (act.frame_range[1] - self.first) / self.fps if act else 0.0
        self.names = {b.name.split(':')[-1]: b.name for b in self.ob.pose.bones}
        sc.frame_set(self.first)

    def bone(self, canon):
        nm = SRC_CMU[canon] if self.kind == 'cmu' else canon
        return self.names[nm]

    def world(self, names):
        mw = np.array(self.ob.matrix_world)
        out = {}
        for n in names:
            m = mw @ np.array(self.ob.pose.bones[n].matrix)
            out[n] = (ortho(m[:3, :3]), m[:3, 3].copy())
        return out

    def rest(self, names):
        """Покой: у CMU — кадр 0 файла (T-поза), у Mixamo — покой арматуры (T-поза)."""
        if self.kind == 'cmu':
            bpy.context.scene.frame_set(self.first)
            return self.world(names)
        self.ob.data.pose_position = 'REST'
        bpy.context.view_layer.update()
        r = self.world(names)
        self.ob.data.pose_position = 'POSE'
        bpy.context.view_layer.update()
        return r

    def sample(self, names, t):
        f = self.first + t * self.fps
        i = int(np.floor(f))
        bpy.context.scene.frame_set(i, subframe=float(f - i))
        return self.world(names)


def tip_name(src_or_tgt, canon, kind):
    if canon.endswith('Hand'):
        side = canon[:-4]
        if src_or_tgt == 'src':
            return SRC_HAND_TIP[kind].format(side)
        return TGT_HAND_TIP[kind].format('l' if side == 'Left' else 'r') if kind == 'game_engine' else TGT_HAND_TIP[kind].format(side)
    return None


def read_source(path, t0, t1, smooth=0.0):
    """Мир костей источника в осях игры: покой и ключи 30/с на [t0, t1]. Оси: лево источника → −x, верх → +y,
    лицо в покое → +z (зеркало правой тройки Blender в левую тройку игры)."""
    src = Source(path)
    if t1 <= 0 or t1 > src.length:
        t1 = src.length
    names = [src.bone(c) for c in CANON]
    tips = {}
    for c in CANON:
        tn = tip_name('src', c, src.kind)
        if tn:
            tips[c] = src.names[tn]
        elif c in TIP:
            tips[c] = src.bone(TIP[c])
    allnames = sorted(set(names) | set(tips.values()))
    rest = src.rest(allnames)
    L = rest[src.bone('LeftUpLeg')][1] - rest[src.bone('RightUpLeg')][1]
    U = np.array([0.0, 0.0, 1.0])
    L = L - U * np.dot(L, U)
    L /= np.linalg.norm(L)
    Fw = np.cross(L, U)
    M = np.array([-L, U, Fw])  # строки: оси игры x, y, z в осях Blender; det −1

    def conv(d):
        return {n: (M @ r @ M.T, M @ p) for n, (r, p) in d.items()}
    rest = conv(rest)
    nkeys = int(np.floor((t1 - t0) * KEYS_PER_S + 1e-6)) + 1
    if smooth > 0:
        # Очистка (clean.py): кадры источника как есть, сглаживание шума маркеров (sigma, с; у носка — втрое, он
        # у CMU дрожит сильнее всего) и только потом прореживание в 30/с — иначе шум складывается в рывки ключей
        pad = int(np.ceil(4 * smooth * 3 * src.fps)) + 2
        f0 = max(0, int(np.floor(t0 * src.fps)) - pad)
        f1 = min(int(np.floor(src.length * src.fps)), int(np.ceil(t1 * src.fps)) + pad)
        frames = [conv(src.sample(allnames, f / src.fps)) for f in range(f0, f1 + 1)]
        data = {}
        for c in CANON:
            n = src.bone(c)
            r = np.array([fr[n][0] for fr in frames])
            p = np.array([fr[n][1] for fr in frames])
            sg = smooth * src.fps * (3.0 if c.endswith('ToeBase') else 1.0)
            r = clean.smooth_rot(r, sg)
            p = clean.gauss(p, sg)
            r, p = clean.resample(r, p, src.fps, t0 * src.fps - f0, nkeys)
            data[c] = {'rest_r': rest[n][0], 'rest_p': rest[n][1], 'r': r, 'p': p}
    else:
        keys = [conv(src.sample(allnames, t0 + k / KEYS_PER_S)) for k in range(nkeys)]
        data = {}
        for c in CANON:
            n = src.bone(c)
            data[c] = {'rest_r': rest[n][0], 'rest_p': rest[n][1],
                       'r': np.array([k[n][0] for k in keys]), 'p': np.array([k[n][1] for k in keys])}
    for c in CANON:
        n = src.bone(c)
        if c in tips:
            data[c]['rest_tip'] = rest[tips[c]][1]
    if src.kind == 'cmu':
        # У CMU на кисти один маркер: её поворот шумит и выворачивает запястье. Кисть идёт за предплечьем, как в бинде
        for c in ('LeftHand', 'RightHand'):
            data[c]['skip'] = True
    return data, nkeys, src.kind


def retarget(data, nkeys, sk, skname, face_hands, head_fix=None, inplace=False):
    """Повороты костей цели после бинда (ключ × кость), голова кости переноса (мм), корень."""
    tmap = TGT[skname]
    tidx = {c: sk.index[tmap[c]] for c in CANON}
    canon_of = {i: c for c, i in tidx.items() if not data[c].get('skip')}
    # Покой цели под покой источника: конечности бинда (A-поза) доворачиваются наименьшим поворотом, чтобы
    # кость смотрела туда же, куда в покое источника (T-поза); туловище, голова и ключицы — как в бинде.
    # Ключицы не доворачиваются: у CMU «ключица» идёт от верха позвоночника вверх к плечу (+14°), у MakeHuman — от
    # грудины вниз (−10°), и доворот поднимал плечи на 6–9 см во всех клипах (у CMU она к тому же не движется)
    W = np.empty((sk.n, 3, 3))
    P = np.empty((sk.n, 3))
    for i in range(sk.n):
        p = sk.parent[i]
        W[i] = sk.local[i] if p < 0 else W[p] @ sk.local[i]
        P[i] = sk.lpos[i] if p < 0 else P[p] + W[p] @ sk.lpos[i]
        c = canon_of.get(i)
        if c and 'rest_tip' in data[c] and not c.endswith('Shoulder'):
            tn = tip_name('tgt', c, skname)
            j = sk.index[tn] if tn else tidx[TIP[c]]
            dloc = sk.bind[i].T @ (sk.pos[j] - sk.pos[i])
            want = data[c]['rest_tip'] - data[c]['rest_p']
            W[i] = rot_between(W[i] @ dloc, want) @ W[i]
    off = {c: data[c]['rest_r'].T @ W[tidx[c]] for c in CANON}
    if head_fix is not None:
        # Поправка головы актёра (строка level в clips.txt): поворот в осях самой кости головы. Словарь — поправки
        # нескольких костей (headself: шея и голова)
        for c, m in (head_fix.items() if isinstance(head_fix, dict) else (('Head', head_fix),)):
            off[c] = off[c] @ m

    # Масштаб — по длине ног (покой у части файлов CMU стоит не на земле, высота таза в нём ненадёжна).
    # Перенос — середина голов бёдер источника в масштабе; таз цели — от середины своих бёдер, как в бинде
    hb = tidx['Hips']
    lt, rt = tidx['LeftUpLeg'], tidx['RightUpLeg']

    def leg(get, a, b, c):
        return np.linalg.norm(get(a) - get(b)) + np.linalg.norm(get(b) - get(c))
    ls = sum(leg(lambda c: data[c]['rest_p'], s + 'UpLeg', s + 'Leg', s + 'Foot') for s in ('Left', 'Right'))
    lg = sum(leg(lambda c: P[tidx[c]], s + 'UpLeg', s + 'Leg', s + 'Foot') for s in ('Left', 'Right'))
    scale = lg / ls
    dloc = W[hb].T @ (P[hb] - (P[lt] + P[rt]) * 0.5)
    mid = (data['LeftUpLeg']['p'] + data['RightUpLeg']['p']) * 0.5 * scale
    hp = mid + np.einsum('kij,j->ki', data['Hips']['r'] @ off['Hips'], dloc)
    # Курс таза: куда смотрит его «лицо» покоя (+z), сглажено ±0.4 с; корень — сглаженная проекция таза
    hr = data['Hips']['r'] @ data['Hips']['rest_r'].T
    fwd = hr[:, :, 2]
    yaw = np.unwrap(np.arctan2(fwd[:, 0], fwd[:, 2]))
    w = int(0.4 * KEYS_PER_S)
    idx = np.clip(np.arange(nkeys)[:, None] + np.arange(-w, w + 1)[None, :], 0, nkeys - 1)
    ryaw = yaw[idx].mean(axis=1)
    if face_hands:
        # Клип с оружием: «лицо» корня — куда в среднем вынесены руки от таза (стойка с мечом стоит боком)
        hands = (data['LeftHand']['p'] + data['RightHand']['p']) * 0.5 * scale - hp
        loc = np.array([yaw_m(ryaw[k]).T @ hands[k] for k in range(nkeys)]).mean(axis=0)
        ryaw = ryaw + np.radians(np.round(np.degrees(np.arctan2(loc[0], loc[2])) / 5) * 5)
    rpos = hp[idx].mean(axis=1)
    rpos[:, 1] = 0
    if inplace:
        # Цикл шага, бега, покоя (clean.py): корень — прямая с постоянной скоростью (или точка), курс — по ходу.
        # Тогда стопа на опоре стоит в мире клипа и при той же скорости в игре, а подгонка шага остаётся растяжкой
        K = nkeys - 1
        D = (hp[K] - hp[0]) * np.array([1.0, 0.0, 1.0])
        t = np.arange(nkeys)[:, None] / K
        if np.linalg.norm(D) < 0.2:
            D = np.zeros(3)
            ryaw = np.full(nkeys, np.arctan2(np.mean(np.sin(yaw)), np.mean(np.cos(yaw))))
        else:
            ryaw = np.full(nkeys, np.arctan2(D[0], D[2]))
        base = (hp - D[None] * t).mean(axis=0)
        rpos = base[None] + D[None] * t
        rpos[:, 1] = 0
    # Начало клипа: корень в нуле, лицом на +z
    y0, p0 = ryaw[0], rpos[0].copy()
    Y0 = yaw_m(y0).T
    ryaw = ryaw - y0
    rpos = (rpos - p0) @ Y0.T

    rot = np.empty((nkeys, sk.n, 4))
    trans = np.empty((nkeys, 3), dtype=np.int64)
    root = np.empty((nkeys, 3), dtype=np.int64)
    for k in range(nkeys):
        Yk = yaw_m(ryaw[k]).T @ Y0  # мир источника → оси корня ключа
        Wk = np.empty((sk.n, 3, 3))
        for i in range(sk.n):
            p = sk.parent[i]
            c = canon_of.get(i)
            if c:
                Wk[i] = Yk @ data[c]['r'][k] @ off[c]
            else:
                Wk[i] = sk.local[i] if p < 0 else Wk[p] @ sk.local[i]
            base = sk.local[i] if p < 0 else Wk[p] @ sk.local[i]
            rot[k, i] = m_to_q(base.T @ Wk[i])
        t = yaw_m(ryaw[k]).T @ (Y0 @ (hp[k] - p0) - rpos[k])  # таз в осях корня; p0 и rpos на земле (y = 0)
        trans[k] = np.floor(t * 1000 + 0.5)
        root[k, 0] = int(np.floor(rpos[k][0] * 1000 + 0.5))
        root[k, 1] = int(np.floor(rpos[k][2] * 1000 + 0.5))
        root[k, 2] = int(np.floor(ryaw[k] / (2 * np.pi) * 4294967296.0 + 0.5)) & 0xffffffff
    return rot, trans, root, hb


def best_loop(data, k0, k1, span=10):
    """Конец петли около k1, где поза ближе всего к началу (повороты конечностей в осях таза)."""
    def pose(k):
        hr = data['Hips']['r'][k]
        return np.concatenate([(hr.T @ data[c]['r'][k]).ravel() for c in CANON[1:]])
    a = pose(k0)
    best, bk = None, k1
    for k in range(max(k0 + 10, k1 - span), min(len(data['Hips']['r']) - 1, k1 + span) + 1):
        d = np.sum((pose(k) - a) ** 2)
        if best is None or d < best:
            best, bk = d, k
    return bk


def head_bias(sk, skname, rot, trans, hb, canon='Head'):
    """Обратный среднему повороту кости (голова, шея) от бинда (оси корня), в осях самой кости: после него кость
    в среднем стоит как в бинде."""
    W, _ = sk.fk(rot, hb, trans / 1000.0)
    i = sk.index[TGT[skname][canon]]
    mean = ortho(np.mean(W[:, i] @ sk.bind[i].T, axis=0))
    return sk.bind[i].T @ mean.T @ sk.bind[i]


_level_cache = {}


def head_level(level, sources, sk, skname):
    """Поправка головы для источника: средний поворот головы на отрезке эталона (актёр смотрит прямо) — ноль."""
    ref, t0, t1 = level
    key = (ref, t0, t1, skname)
    if key not in _level_cache:
        data, nkeys, _ = read_source(os.path.join(RAW, sources[ref]), t0, t1)
        rot, trans, _, hb = retarget(data, nkeys, sk, skname, False)
        _level_cache[key] = head_bias(sk, skname, rot, trans, hb)
    return _level_cache[key]


def read_clips():
    path = os.path.join(os.path.dirname(__file__), 'clips.txt')
    sources, clips, levels = {}, [], {}
    for line in open(path, encoding='utf-8'):
        w = line.split('#', 1)[0].split()
        if not w:
            continue
        if w[0] == 'source':
            sources[w[1]] = w[2]
        elif w[0] == 'level':
            for src in w[4:]:
                levels[src] = (w[1], float(w[2]), float(w[3]))
        elif w[0] == 'clip':
            clips.append({'name': w[1], 'src': w[2], 't0': float(w[3]), 't1': float(w[4]), 'flags': w[5:],
                          'note': line.split('#', 1)[1].strip() if '#' in line else ''})
    return sources, clips, levels


def write_labels(clip, src_file, kind, nkeys):
    path = os.path.join(OUT, clip['name'] + '.txt')
    if os.path.exists(path):
        return
    lic = 'CMU Graphics Lab Motion Capture Database (BVH Б. Хане), свободно' if kind == 'cmu' else \
        'Mixamo (Adobe), только наш .anim'
    with open(path, 'w', encoding='utf-8') as f:
        f.write(f"# Метки клипа {clip['name']} (docs/anim.md). Источник: {src_file} {clip['t0']}–{clip['t1']} с; {lic}\n")
        if clip['note']:
            f.write(f"# {clip['note']}\n")
        f.write('# Время — ключи 30/с от начала клипа. Строки foot ставит tools/retarget/bake.py, остальное — руками\n')
        f.write(f'keys {nkeys}\n')
        if 'loop' in clip['flags']:
            f.write('loop\n')
        if 'sword' in clip['flags']:
            f.write('weapon sword\n')
        if 'sword2' in clip['flags']:
            f.write('weapon sword2\n')


def flag(clip, name, default=None):
    """Флаг строки clip: «имя» → True, «имя=значение» → строка значения."""
    for f in clip['flags']:
        if f == name:
            return True
        if f.startswith(name + '='):
            return f[len(name) + 1:]
    return default


# Ноги цели для закрепления стоп: бедро, голень, стопа, носок
LEGS = {'game_engine': [('thigh_' + k, 'calf_' + k, 'foot_' + k, 'ball_' + k) for k in ('l', 'r')],
        'mixamo': [('mixamorig:' + s + b) for s in ('Left', 'Right') for b in ('UpLeg', 'Leg', 'Foot', 'ToeBase')]}
LEGS['mixamo'] = [tuple(LEGS['mixamo'][i:i + 4]) for i in (0, 4)]


def run_clip(clip, sources, levels):
    rel = sources[clip['src']]
    path = os.path.join(RAW, rel)
    if not os.path.exists(path):
        print(f"retarget: {clip['name']}: нет {path} (fetch.sh или папка Mixamo)")
        return False
    smooth = float(flag(clip, 'smooth', '0'))
    data, nkeys, kind = read_source(path, clip['t0'], clip['t1'], smooth)
    loop = 'loop' in clip['flags']
    cyc = flag(clip, 'cycle')
    note = ''
    if loop and cyc:
        # Цикл петли ищется по всему окну (clean.py): длина от и до, с; остаток шва разносится после ретаргета
        lo, hi = (int(round(float(v) * KEYS_PER_S)) for v in cyc.split('-'))
        a, b = clean.find_cycle(data, CANON, lo, hi)
        for c in CANON:
            for f in ('r', 'p'):
                data[c][f] = data[c][f][a:b + 1]
        nkeys = b - a + 1
        note = f' цикл с {clip["t0"] + a / KEYS_PER_S:.2f} с'
    elif loop:
        k1 = best_loop(data, 0, nkeys - 1)
        for c in CANON:
            for f in ('r', 'p'):
                data[c][f] = data[c][f][:k1 + 1]
        nkeys = k1 + 1
    os.makedirs(OUT, exist_ok=True)
    log = []
    for skname in SKELS:
        sk = Skel(os.path.join(ROOT, 'data', 'people', f'{BODY}.{skname}.skel'))
        fix = head_level(levels[clip['src']], sources, sk, skname) if clip['src'] in levels else None
        sword = any(f.startswith('sword') for f in clip['flags'])
        inplace = bool(flag(clip, 'inplace'))
        if flag(clip, 'headself'):
            # Шея и голова по самому клипу: в шаге, беге и покое человек в среднем смотрит прямо и шею не тянет
            # (маркеры у каждого актёра CMU свои: у актёра покоя шея шла вперёд на 40°). Повороты костей переносятся
            # в мире, поэтому поправка шеи голову не трогает
            rot, trans, _, hb = retarget(data, nkeys, sk, skname, sword, None, inplace)
            fix = {c: head_bias(sk, skname, rot, trans, hb, c) for c in ('Neck', 'Head')}
        rot, trans, root, hb = retarget(data, nkeys, sk, skname, sword, fix, inplace)
        if loop and cyc:
            rot, trans = clean.loop_fix(rot, trans)
            trans = np.floor(trans + 0.5).astype(np.int64)
        if flag(clip, 'plant'):
            legs = [tuple(sk.index[n] for n in leg) for leg in LEGS[skname]]
            rot = clean.plant(sk, rot, trans, root, hb, legs, loop, log if skname == 'game_engine' else None)
        a = Anim()
        a.skel, a.flags, a.bones, a.trans_bone = skname, LOOP if loop else 0, sk.names, hb
        a.rot, a.trans, a.root = rot, trans, root
        a.write(os.path.join(OUT, f"{clip['name']}.{skname}.anim"))
    write_labels(clip, rel, kind, nkeys)
    print(f"retarget: {clip['name']}: ключей {nkeys} ({(nkeys - 1) / KEYS_PER_S:.2f} с){' петля' if loop else ''}{note}"
          f"{'; ' + '; '.join(log) if log else ''}")
    return True


def probe(path, t0=0.0, t1=0.0):
    data, nkeys, kind = read_source(path, t0, t1)
    hand = data['RightHand']['p']
    lh = data['LeftHand']['p']
    hips = data['Hips']['p']
    s = 1.0 / data['Hips']['rest_p'][1]
    fwd = (data['Hips']['r'] @ data['Hips']['rest_r'].T)[:, :, 2]
    yaw = np.degrees(np.unwrap(np.arctan2(fwd[:, 0], fwd[:, 2])))
    if '--short' in sys.argv:
        print(' | '.join(f"{t0 + k / KEYS_PER_S:.1f} {hips[k][1] * s:.2f} {yaw[k]:.0f}°" for k in range(0, nkeys, 9)))
        return
    for k in range(0, nkeys - 3, 3):
        v = np.linalg.norm(hand[k + 3] - hand[k]) * s * KEYS_PER_S / 3
        vl = np.linalg.norm(lh[k + 3] - lh[k]) * s * KEYS_PER_S / 3
        hv = np.linalg.norm((hips[k + 3] - hips[k])[[0, 2]]) * s * KEYS_PER_S / 3
        print(f"{t0 + k / KEYS_PER_S:6.2f} с  правая {v:5.2f}  левая {vl:5.2f}  таз {hips[k][1] * s:4.2f} ход {hv:4.2f}  "
              f"рука y {hand[k][1] * s:4.2f}  {'#' * int(v * 4)}")


def main():
    args = sys.argv[1:]
    if args and args[0] == '--probe':
        probe(args[1], *(float(x) for x in args[2:4] if x != '--short'))
        return
    sources, clips, levels = read_clips()
    ok = True
    for c in clips:
        if not args or c['name'] in args:
            ok &= run_clip(c, sources, levels)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
