# Запечённые дорожки для шага мира и касания стоп (docs/anim.md). Только numpy: CI запускает его и сверяет байты
# data/anims/*.trk и строки foot в data/anims/*.txt с git.
#   python3 tools/retarget/bake.py            все клипы data/anims/*.anim
# Вход — .anim и .skel (то, что видит игра), выход — <клип>.<скелет>.trk: на тик 60 Гц корень, 11 капсул тела
# и отрезок оружия целыми мм в осях корня
import glob
import os
import struct
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from animlib import Skel, Anim, KEYS_PER_S, TICKS_PER_S, LOOP, chunk, put_name, read_labels, yaw_m  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BODY = 'human_m'
TRK_WEAPON = 2

# Кости частей у скелета: общее имя → имя кости
BONES = {
    'game_engine': {'Hips': 'pelvis', 'Spine2': 'spine_03', 'Neck': 'neck_01', 'Head': 'head',
                    'Arm_l': 'upperarm_l', 'ForeArm_l': 'lowerarm_l', 'Tip_l': 'middle_01_l',
                    'Arm_r': 'upperarm_r', 'ForeArm_r': 'lowerarm_r', 'Tip_r': 'middle_01_r',
                    'UpLeg_l': 'thigh_l', 'Leg_l': 'calf_l', 'Foot_l': 'foot_l', 'Toe_l': 'ball_l',
                    'UpLeg_r': 'thigh_r', 'Leg_r': 'calf_r', 'Foot_r': 'foot_r', 'Toe_r': 'ball_r',
                    'Hand_r': 'hand_r', 'Hand_l': 'hand_l', 'Index_r': 'index_01_r', 'Pinky_r': 'pinky_01_r'},
    'mixamo': {},
}
for k, v in {'Hips': 'Hips', 'Spine2': 'Spine2', 'Neck': 'Neck', 'Head': 'Head', 'Arm_l': 'LeftArm',
             'ForeArm_l': 'LeftForeArm', 'Tip_l': 'LeftHandMiddle1', 'Arm_r': 'RightArm', 'ForeArm_r': 'RightForeArm',
             'Tip_r': 'RightHandMiddle1', 'UpLeg_l': 'LeftUpLeg', 'Leg_l': 'LeftLeg', 'Foot_l': 'LeftFoot',
             'Toe_l': 'LeftToeBase', 'UpLeg_r': 'RightUpLeg', 'Leg_r': 'RightLeg', 'Foot_r': 'RightFoot',
             'Toe_r': 'RightToeBase', 'Hand_r': 'RightHand', 'Hand_l': 'LeftHand', 'Index_r': 'RightHandIndex1',
             'Pinky_r': 'RightHandPinky1'}.items():
    BONES['mixamo'][k] = 'mixamorig:' + v

# 11 частей: имя, начало, конец (голова кости или точка вдоль её оси Y, м), радиус мм. Порядок — как у ragdoll
PARTS = [
    ('head', ('Head', 0.05), ('Head', 0.14), 100),
    ('chest', ('Spine2', None), ('Neck', -0.08), 140),
    ('pelvis', ('Hips', None), ('Spine2', None), 150),
    ('upperarm_l', ('Arm_l', None), ('ForeArm_l', None), 50),
    ('upperarm_r', ('Arm_r', None), ('ForeArm_r', None), 50),
    ('forearm_l', ('ForeArm_l', None), ('Tip_l', None), 45),
    ('forearm_r', ('ForeArm_r', None), ('Tip_r', None), 45),
    ('thigh_l', ('UpLeg_l', None), ('Leg_l', None), 80),
    ('thigh_r', ('UpLeg_r', None), ('Leg_r', None), 80),
    ('shin_l', ('Leg_l', None), ('Foot_l', None), 60),
    ('shin_r', ('Leg_r', None), ('Foot_r', None), 60),
]
SWORD = (0.10, 1.00)  # клинок от и до, м от хвата по оси хвата


def sample_keys(a, ticks):
    """Ключи 30/с → тики 60/с: чётный тик — ключ, нечётный — середина (нормированная сумма кватернионов)."""
    F, n = a.rot.shape[:2]
    rot = np.empty((ticks, n, 4))
    trans = np.empty((ticks, 3))
    root = np.empty((ticks, 3))
    for t in range(ticks):
        k, h = divmod(t, 2)
        if h == 0:
            rot[t], trans[t], root[t] = a.rot[k], a.trans[k], a.root[k]
            continue
        q0, q1 = a.rot[k], a.rot[k + 1]
        s = np.where(np.sum(q0 * q1, axis=1, keepdims=True) < 0, -1.0, 1.0)
        q = q0 + q1 * s
        rot[t] = q / np.linalg.norm(q, axis=1, keepdims=True)
        trans[t] = (a.trans[k] + a.trans[k + 1]) * 0.5
        dy = ((a.root[k + 1, 2] - a.root[k, 2] + 2**31) % 2**32) - 2**31  # курс — кратчайшим путём
        root[t] = [(a.root[k, 0] + a.root[k + 1, 0]) * 0.5, (a.root[k, 1] + a.root[k + 1, 1]) * 0.5, a.root[k, 2] + dy * 0.5]
    return rot, trans, root


def mm(v):
    return np.floor(np.asarray(v) * 1000 + 0.5).astype(np.int64)


def bake_one(path_anim, labels):
    a = Anim.read(path_anim)
    sk = Skel(os.path.join(ROOT, 'data', 'people', f'{BODY}.{a.skel}.skel'))
    assert a.bones == sk.names, f'{path_anim}: кости не как в .skel'
    B = {k: sk.index[v] for k, v in BONES[a.skel].items()}
    F = a.frames()
    ticks = 2 * (F - 1) + 1
    rot, trans, root = sample_keys(a, ticks)
    W, P = sk.fk(rot, a.trans_bone, trans / 1000.0)

    def point(spec):
        b, along = spec
        p = P[:, B[b]]
        return p if along is None else p + W[:, B[b], :, 1] * along
    weapon = any(w[0] == 'weapon' for w in labels)
    two = any(w[0] == 'weapon' and w[1:2] == ['sword2'] for w in labels)
    flags = (a.flags & LOOP) | (TRK_WEAPON if weapon else 0)
    head = put_name(a.skel) + struct.pack('<IIII', TICKS_PER_S, ticks, len(PARTS), flags)
    part = b''.join(put_name(n) + struct.pack('<H', r) for n, _, _, r in PARTS)
    rootb = b''.join(struct.pack('<iiI', int(np.floor(r[0] + 0.5)), int(np.floor(r[1] + 0.5)),
                                 int(np.floor(r[2] + 0.5)) & 0xffffffff) for r in root)
    caps = np.empty((ticks, len(PARTS), 6), dtype=np.int64)
    for i, (_, sa, sb, _) in enumerate(PARTS):
        caps[:, i, :3] = mm(point(sa))
        caps[:, i, 3:] = mm(point(sb))
    data = b'ANATRAK1' + chunk('HEAD', 1, head) + chunk('PART', 1, part) + chunk('ROOT', 1, rootb) + \
        chunk('CAPS', 1, caps.astype('<i2').tobytes())
    if weapon:
        # Хват — середина кисти и основания среднего пальца; ось — от мизинца к указательному (клинок выходит
        # со стороны большого пальца)
        g = (P[:, B['Hand_r']] + P[:, B['Tip_r']]) * 0.5
        d = P[:, B['Index_r']] - P[:, B['Pinky_r']]
        if two:  # двуручный хват (кисти CMU не перенесены): клинок продолжает линию кистей от навершия к гарде;
            # у актёра CMU 02 у гарды левая кисть, поэтому от правой к левой
            g = P[:, B['Hand_l']]
            d = P[:, B['Hand_l']] - P[:, B['Hand_r']]
        d /= np.linalg.norm(d, axis=1, keepdims=True)
        seg = np.concatenate([mm(g + d * SWORD[0]), mm(g + d * SWORD[1])], axis=1)
        data += chunk('WEAP', 1, seg.astype('<i2').tobytes())
    out = path_anim[:-5] + '.trk'
    open(out, 'wb').write(data)
    return a, sk, W, P, B


def feet(a, sk, P, B):
    """Касания стоп по ключам: пятка (голова стопы) или носок почти не движутся в мире."""
    F = a.frames()
    keys = np.arange(0, 2 * (F - 1) + 1, 2)
    out = []
    for side in ('l', 'r'):
        hit = np.zeros(F, dtype=bool)
        for b in ('Foot_' + side, 'Toe_' + side):
            p = P[keys, B[b]].copy()
            # в мир: корень ключа
            for k in range(F):
                p[k] = yaw_m(a.root[k, 2] / 4294967296.0 * 2 * np.pi) @ p[k] + [a.root[k, 0] / 1000, 0, a.root[k, 1] / 1000]
            v = np.zeros(F)
            v[1:-1] = np.linalg.norm(p[2:] - p[:-2], axis=1) * KEYS_PER_S / 2
            v[0], v[-1] = v[1], v[-2]
            hit |= v < 0.45  # почти стоит в мире (по высоте не судим: лестница)
        k = 0
        while k < F:
            if hit[k]:
                e = k
                while e + 1 < F and hit[e + 1]:
                    e += 1
                if e - k >= 2:
                    out.append(f'foot {side} {k} {e}')
                k = e + 1
            else:
                k += 1
    return out


def write_feet(path, lines):
    if not os.path.exists(path):
        return
    old = open(path, encoding='utf-8').read().splitlines()
    keep = [s for s in old if not s.startswith('foot ')]
    open(path, 'w', encoding='utf-8').write('\n'.join(keep + lines) + '\n')


def main():
    for path in sorted(glob.glob(os.path.join(ROOT, 'data', 'anims', '*.anim'))):
        clip = os.path.basename(path).split('.')[0]
        txt = os.path.join(ROOT, 'data', 'anims', clip + '.txt')
        a, sk, W, P, B = bake_one(path, read_labels(txt))
        if a.skel == 'game_engine':  # касания — по одному скелету (выбор скелета — этап 8)
            write_feet(txt, feet(a, sk, P, B))
    print('bake: готово')


if __name__ == '__main__':
    main()
