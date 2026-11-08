# Общее для ретаргета и запекания (docs/anim.md): чтение .skel, запись и чтение .anim, кватернионы, FK.
# Только numpy, без bpy: запекание (bake.py) идёт в CI на тех же функциях. Всё в f64, на выходе — целые
import struct
import numpy as np

KEYS_PER_S = 30
TICKS_PER_S = 60
LOOP = 1


# ---- куски «тег, версия, длина» (как src/core/ser.h)
def chunk(tag, ver, body):
    return tag.encode() + struct.pack('<IQ', ver, len(body)) + body


def chunks(data, magic):
    assert data[:8] == magic, 'не тот файл'
    o, out = 8, {}
    while o < len(data):
        tag = data[o:o + 4].decode()
        ver, n = struct.unpack_from('<IQ', data, o + 4)
        out[tag] = (ver, data[o + 16:o + 16 + n])
        o += 16 + n
    return out


def put_name(s):
    b = s.encode()
    return struct.pack('<B', len(b)) + b


def get_name(b, o):
    n = b[o]
    return b[o + 1:o + 1 + n].decode(), o + 1 + n


# ---- кватернионы (x, y, z, w) и матрицы 3×3
def q_to_m(q):
    x, y, z, w = q[..., 0], q[..., 1], q[..., 2], q[..., 3]
    m = np.empty(q.shape[:-1] + (3, 3))
    m[..., 0, 0] = 1 - 2 * (y * y + z * z); m[..., 0, 1] = 2 * (x * y - z * w); m[..., 0, 2] = 2 * (x * z + y * w)
    m[..., 1, 0] = 2 * (x * y + z * w); m[..., 1, 1] = 1 - 2 * (x * x + z * z); m[..., 1, 2] = 2 * (y * z - x * w)
    m[..., 2, 0] = 2 * (x * z - y * w); m[..., 2, 1] = 2 * (y * z + x * w); m[..., 2, 2] = 1 - 2 * (x * x + y * y)
    return m


def m_to_q(m):
    """Матрица поворота → кватернион с w ≥ 0 (по одному, без ветвлений numpy по массиву)."""
    m = np.asarray(m, dtype=np.float64)
    t = m[0, 0] + m[1, 1] + m[2, 2]
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s]
    q = np.array(q)
    q /= np.linalg.norm(q)
    return -q if q[3] < 0 else q


def rot_between(a, b):
    """Наименьший поворот, переводящий направление a в b (матрица)."""
    a = a / np.linalg.norm(a)
    b = b / np.linalg.norm(b)
    v = np.cross(a, b)
    c = float(np.dot(a, b))
    if c < -0.999999:
        p = np.cross(a, [1.0, 0, 0])
        if np.linalg.norm(p) < 1e-6:
            p = np.cross(a, [0, 1.0, 0])
        p /= np.linalg.norm(p)
        return 2 * np.outer(p, p) - np.eye(3)
    k = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + k + k @ k / (1 + c)


def yaw_m(yaw):
    """Поворот вокруг +y на yaw радиан в осях игры: +z (лицо) уходит к +x."""
    c, s = np.cos(yaw), np.sin(yaw)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


# ---- .skel (src/asset/skel.cpp)
class Skel:
    def __init__(self, path):
        ch = chunks(open(path, 'rb').read(), b'ANASKEL1')
        b = ch['BONE'][1]
        n = struct.unpack_from('<I', b, 0)[0]
        o = 4
        self.names, self.parent, self.length = [], [], []
        self.pos, self.rot = np.zeros((n, 3)), np.zeros((n, 4))
        for i in range(n):
            nm, o = get_name(b, o)
            par, ln = struct.unpack_from('<if', b, o); o += 8
            self.pos[i] = struct.unpack_from('<3f', b, o); o += 12
            self.rot[i] = struct.unpack_from('<4f', b, o); o += 16
            o += 48
            self.names.append(nm); self.parent.append(par); self.length.append(ln)
        self.n = n
        self.index = {nm: i for i, nm in enumerate(self.names)}
        self.bind = q_to_m(self.rot)               # мир кости в бинд-позе (ось Y — к хвосту)
        self.local = np.empty((n, 3, 3))           # местный поворот бинда: родитель → кость
        self.lpos = np.empty((n, 3))               # голова в осях родителя
        for i in range(n):
            p = self.parent[i]
            if p < 0:
                self.local[i], self.lpos[i] = self.bind[i], self.pos[i]
            else:
                self.local[i] = self.bind[p].T @ self.bind[i]
                self.lpos[i] = self.bind[p].T @ (self.pos[i] - self.pos[p])

    def fk(self, rot, trans_bone=-1, trans=None):
        """Поза как pose_palette (src/anim/pose.cpp): rot — на кость кватернион после бинда (F, n, 4);
        trans (F, 3) — голова кости trans_bone. Возвращает миры костей (F, n, 3, 3) и головы (F, n, 3)."""
        F = rot.shape[0]
        R = q_to_m(rot)
        W = np.empty((F, self.n, 3, 3))
        P = np.empty((F, self.n, 3))
        for i in range(self.n):
            p = self.parent[i]
            if p < 0:
                W[:, i] = self.local[i] @ R[:, i]
                P[:, i] = self.lpos[i]
            else:
                W[:, i] = W[:, p] @ self.local[i] @ R[:, i]
                P[:, i] = P[:, p] + np.einsum('fij,j->fi', W[:, p], self.lpos[i])
            if i == trans_bone:
                P[:, i] = trans
        return W, P


# ---- сжатый кватернион: smallest-three, 48 бит (docs/anim.md)
Q_RANGE = 1.0 / np.sqrt(2.0)
Q_STEPS = 32767


def q_pack(q):
    q = np.asarray(q, dtype=np.float64)
    q = q / np.linalg.norm(q)
    k = int(np.argmax(np.abs(q)))
    if q[k] < 0:
        q = -q
    v = k
    sh = 2
    for i in range(4):
        if i == k:
            continue
        c = min(max(q[i], -Q_RANGE), Q_RANGE)
        u = int(np.floor((c / Q_RANGE * 0.5 + 0.5) * Q_STEPS + 0.5))
        v |= u << sh
        sh += 15
    return struct.pack('<Q', v)[:6]


def q_unpack(b):
    v = struct.unpack('<Q', b + b'\0\0')[0]
    k = v & 3
    q = np.zeros(4)
    sh, s = 2, 0.0
    for i in range(4):
        if i == k:
            continue
        u = (v >> sh) & 0x7fff
        sh += 15
        q[i] = (u / Q_STEPS * 2 - 1) * Q_RANGE
        s += q[i] * q[i]
    q[k] = np.sqrt(max(0.0, 1 - s))
    return q


# ---- .anim
class Anim:
    """Клип одного скелета: ключи 30/с; на ключ корень (x, z мм, курс — доля оборота u32), голова кости
    переноса в осях корня (мм) и повороты всех костей после бинда."""
    def __init__(self):
        self.skel = ''
        self.flags = 0
        self.bones = []
        self.trans_bone = 0
        self.root = None    # (F, 3) i64: x мм, z мм, курс u32
        self.trans = None   # (F, 3) i64 мм
        self.rot = None     # (F, n, 4) f64 — уже после упаковки, если читали файл

    def write(self, path):
        F, n = self.rot.shape[:2]
        head = put_name(self.skel) + struct.pack('<IIII', KEYS_PER_S, F, n, self.flags) + put_name(self.bones[self.trans_bone])
        bone = b''.join(put_name(s) for s in self.bones)
        root = b''.join(struct.pack('<iiI', int(r[0]), int(r[1]), int(r[2]) & 0xffffffff) for r in self.root)
        trans = b''.join(struct.pack('<hhh', *(int(v) for v in t)) for t in self.trans)
        rots = b''.join(q_pack(self.rot[f, i]) for f in range(F) for i in range(n))
        data = b'ANAANIM1' + chunk('HEAD', 1, head) + chunk('BONE', 1, bone) + chunk('ROOT', 1, root) + \
            chunk('TRNS', 1, trans) + chunk('ROTS', 1, rots)
        open(path, 'wb').write(data)

    @staticmethod
    def read(path):
        a = Anim()
        ch = chunks(open(path, 'rb').read(), b'ANAANIM1')
        h = ch['HEAD'][1]
        a.skel, o = get_name(h, 0)
        kps, F, n, a.flags = struct.unpack_from('<IIII', h, o); o += 16
        tb, o = get_name(h, o)
        assert kps == KEYS_PER_S
        b = ch['BONE'][1]
        o = 0
        for _ in range(n):
            s, o = get_name(b, o)
            a.bones.append(s)
        a.trans_bone = a.bones.index(tb)
        a.root = np.array([struct.unpack_from('<iiI', ch['ROOT'][1], 12 * f) for f in range(F)], dtype=np.int64)
        a.trans = np.array([struct.unpack_from('<hhh', ch['TRNS'][1], 6 * f) for f in range(F)], dtype=np.int64)
        r = ch['ROTS'][1]
        a.rot = np.array([[q_unpack(r[6 * (f * n + i):6 * (f * n + i) + 6]) for i in range(n)] for f in range(F)])
        return a

    def frames(self):
        return self.rot.shape[0]


# ---- метки клипа (data/anims/<клип>.txt, docs/anim.md): строки «слово числа…», # — комментарий
def read_labels(path):
    out = []
    try:
        for line in open(path, encoding='utf-8'):
            w = line.split('#', 1)[0].split()
            if w:
                out.append(w)
    except FileNotFoundError:
        pass
    return out
