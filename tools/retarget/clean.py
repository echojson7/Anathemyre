# Очистка захвата движения для ретаргета (docs/anim.md, «Очистка»): сглаживание шума маркеров до прореживания в
# 30 ключей/с, поиск цикла петли и разнос остатка шва по циклу, корень с постоянной скоростью для шага и бега,
# стопы на опоре без скольжения (IK бедро — голень). Только numpy; всё в f64, байты одни при повторном запуске
import numpy as np
from animlib import KEYS_PER_S, q_to_m, m_to_q, rot_between


# ---- кватернионы (x, y, z, w) массивом
def q_mul(a, b):
    ax, ay, az, aw = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    bx, by, bz, bw = b[..., 0], b[..., 1], b[..., 2], b[..., 3]
    return np.stack([aw * bx + ax * bw + ay * bz - az * by,
                     aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw,
                     aw * bw - ax * bx - ay * by - az * bz], axis=-1)


def q_conj(q):
    return q * np.array([-1.0, -1.0, -1.0, 1.0])


def q_pow(q, t):
    """Доля t поворота q (короткая дуга)."""
    q = np.where(q[..., 3:4] < 0, -q, q)
    v = q[..., :3]
    s = np.linalg.norm(v, axis=-1)
    a = np.arctan2(s, q[..., 3])
    t = np.asarray(t, dtype=np.float64)
    k = np.where(s > 1e-12, np.sin(a * t) / np.maximum(s, 1e-12), t)
    return np.concatenate([v * k[..., None], np.cos(a * t)[..., None]], axis=-1)


def q_continuous(q):
    """Знак кватернионов по времени (ось 0) без скачков: соседние — в одном полушарии."""
    q = q.copy()
    for k in range(1, len(q)):
        d = np.sum(q[k] * q[k - 1], axis=-1)
        q[k] = np.where((d < 0)[..., None], -q[k], q[k])
    return q


def q_norm(q):
    return q / np.linalg.norm(q, axis=-1, keepdims=True)


def mats_to_q(m):
    sh = m.shape[:-2]
    flat = m.reshape(-1, 3, 3)
    return np.array([m_to_q(x) for x in flat]).reshape(sh + (4,))


# ---- сглаживание
def gauss(x, sigma, wrap=False):
    """Гаусс по оси 0 (sigma — в отсчётах). Края — отражением, у петли — по кругу."""
    if sigma <= 0:
        return x
    r = int(np.ceil(3 * sigma))
    w = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    w /= w.sum()
    n = len(x)
    if wrap:
        idx = (np.arange(n)[:, None] + np.arange(-r, r + 1)[None, :]) % n
    else:
        idx = np.arange(n)[:, None] + np.arange(-r, r + 1)[None, :]
        idx = np.where(idx < 0, -idx, idx)
        idx = np.where(idx >= n, 2 * (n - 1) - idx, idx)
        idx = np.clip(idx, 0, n - 1)
    return np.einsum('k,nk...->n...', w, x[idx])


def smooth_rot(r, sigma):
    """Повороты-матрицы (кадр × 3 × 3): сглаживание кватернионов (знак непрерывен) и снова матрицы."""
    q = q_continuous(mats_to_q(r))
    return q_to_m(q_norm(gauss(q, sigma)))


def resample(r, p, fps, t0_frame, nkeys):
    """С кадров источника (fps) на ключи 30/с: ключ k — момент t0_frame + k·fps/30 (в кадрах), между кадрами —
    nlerp и линейно."""
    f = t0_frame + np.arange(nkeys) * fps / KEYS_PER_S
    i = np.clip(np.floor(f).astype(np.int64), 0, len(r) - 2)
    t = (f - i)[:, None]
    q = q_continuous(mats_to_q(r))
    qa, qb = q[i], q[i + 1]
    qb = np.where(np.sum(qa * qb, axis=-1, keepdims=True) < 0, -qb, qb)
    qr = q_norm(qa * (1 - t) + qb * t)
    pr = p[i] * (1 - t) + p[i + 1] * t
    return q_to_m(qr), pr


# ---- петля
def find_cycle(data, canon, lo, hi):
    """Лучший цикл в окне: пара ключей (a, b), b − a в [lo, hi], где поза и скорость поз ближе всего. Поза —
    повороты костей в осях таза и высота таза; скорость — разность соседних ключей."""
    hips = data['Hips']['r']
    n = len(hips)
    feat = np.concatenate([np.einsum('kji,kjl->kil', hips, data[c]['r']).reshape(n, -1) for c in canon[1:]] +
                          [data['Hips']['p'][:, 1:2] * 4.0], axis=1)
    vel = np.zeros_like(feat)
    vel[1:-1] = (feat[2:] - feat[:-2]) * 0.5
    best, pair = None, None
    for a in range(1, n - 1):
        for b in range(a + lo, min(a + hi, n - 2) + 1):
            d = np.sum((feat[a] - feat[b]) ** 2) + 4.0 * np.sum((vel[a] - vel[b]) ** 2)
            if best is None or d < best:
                best, pair = d, (a, b)
    return pair


def loop_fix(rot, trans):
    """Остаток шва петли (последний ключ против первого) разнесён по всему циклу: поворот кости на ключе k
    домножен на долю k/K поворота «последний → первый», таз — сдвинут на долю разности."""
    K = len(rot) - 1
    d = q_mul(q_conj(rot[K]), rot[0])
    t = np.arange(K + 1) / K
    out = q_norm(q_mul(rot, q_pow(d[None], t[:, None])))
    tr = trans + (trans[0] - trans[K])[None, :] * t[:, None]
    return out, tr


# ---- стопы на опоре
def contacts(foot_w, toe_w, thr=0.30):
    """Касание по ключам: пятка или носок почти стоят в мире (м/с). Позиции — с ключом до и после (у петли —
    через шов со сдвигом цикла), то есть n + 2 строки на n ключей."""
    hit = np.zeros(len(foot_w) - 2, dtype=bool)
    for p in (foot_w, toe_w):
        v = np.linalg.norm(p[2:] - p[:-2], axis=1) * KEYS_PER_S / 2
        hit |= v < thr
    return hit


def spans(hit, loop):
    """Отрезки True: списки номеров ключей без свёртки (у петли отрезок через шов идёт дальше n)."""
    n = len(hit)
    if hit.all():
        return [list(range(n))]
    start = int(np.argmin(hit)) if loop else 0  # с ключа без касания: отрезок через шов не рвётся
    out, cur = [], []
    for j in range(n if loop else n - start):
        k = start + j
        if hit[k % n]:
            cur.append(k)
        elif cur:
            out.append(cur)
            cur = []
    if cur:
        out.append(cur)
    return [s for s in out if len(s) >= 3]


def two_bone(H, Kn, A, T):
    """Новые миры бедра и голени (как поворот к старым): колено в плоскости бедро — цель — старое колено."""
    l1 = np.linalg.norm(Kn - H)
    l2 = np.linalg.norm(A - Kn)
    d = np.linalg.norm(T - H)
    d = min(max(d, abs(l1 - l2) + 1e-4), l1 + l2 - 1e-4)
    u = (T - H) / np.linalg.norm(T - H)
    v = (Kn - H) - u * np.dot(Kn - H, u)
    if np.linalg.norm(v) < 1e-9:
        v = np.array([0.0, 0.0, 1.0]) - u * u[2]
    v /= np.linalg.norm(v)
    a = (l1 * l1 - l2 * l2 + d * d) / (2 * d)
    h = np.sqrt(max(l1 * l1 - a * a, 0.0))
    K2 = H + u * a + v * h
    T2 = H + u * d
    R1 = rot_between(Kn - H, K2 - H)
    R2 = rot_between(R1 @ (A - Kn), T2 - K2)
    return R1, R2


def root_world(root):
    """Корень ключей (x, z мм, курс u32) → поворот (ключ × 3 × 3) и сдвиг (м) из осей корня в мир клипа."""
    yaw = root[:, 2].astype(np.float64) / 4294967296.0 * 2 * np.pi
    c, s = np.cos(yaw), np.sin(yaw)
    M = np.zeros((len(root), 3, 3))
    M[:, 0, 0], M[:, 0, 2], M[:, 1, 1], M[:, 2, 0], M[:, 2, 2] = c, s, 1, -s, c
    off = np.stack([root[:, 0] / 1000.0, np.zeros(len(root)), root[:, 1] / 1000.0], axis=1)
    return M, off


def plant(sk, rot, trans, root, hb, legs, loop, log=None, ramp=3):
    """Стопы на опоре без скольжения. Касание — пятка или носок почти стоят в мире клипа. На опоре закреплена
    точка, вокруг которой стопа катится (сначала пятка, потом носок): цель лодыжки — эта точка плюс нынешний
    вынос лодыжки от неё, поворот стопы как был. Бедро и голень — IK, колено в своей плоскости; вход и выход —
    плавно за ramp ключей. legs — (бедро, голень, стопа, носок) индексами костей на сторону. У петли последний
    ключ равен первому: считаются m = n − 1 ключей, номер k ≥ m — тот же ключ k − m на цикл дальше."""
    rot = rot.copy()
    W, P = sk.fk(rot, hb, trans / 1000.0)
    M, off = root_world(root)
    n = len(rot)
    m = n - 1 if loop else n
    D = off[n - 1] - off[0] if loop else np.zeros(3)

    def wpos(b, k):
        """Мир клипа точки b на ключе k без свёртки."""
        c, j = divmod(k, m) if loop else (0, min(max(k, 0), n - 1))
        return M[j] @ P[j, b] + off[j] + D * c

    for th, ca, fo, to in legs:
        rng = range(-1, m + 1)
        # порог касания растёт со скоростью клипа: на бегу стопа стоит 3–5 ключей и шум за ключ больше
        v = np.linalg.norm(off[n - 1] - off[0]) / max(n - 1, 1) * KEYS_PER_S
        hit = contacts(np.array([wpos(fo, k) for k in rng]), np.array([wpos(to, k) for k in rng]), 0.30 + 0.07 * v)
        sp = spans(hit, loop)
        if not sp:
            continue
        aim = {}                   # ключ без свёртки → цель лодыжки в мире
        for s in sp:
            def speed(b, k):
                return np.linalg.norm(wpos(b, k + 1) - wpos(b, k - 1))
            on_toe = speed(fo, s[0]) > speed(to, s[0])
            lock = wpos(to if on_toe else fo, s[0])
            first = (lock, on_toe)
            for k in s:
                if not on_toe and speed(to, k) < speed(fo, k) * 0.7:
                    on_toe = True        # пятка пошла вверх: стопа катится на носок, замок — носок
                    lock = wpos(to, k)
                aim[k] = lock + (wpos(fo, k) - wpos(to if on_toe else fo, k))
            last = (lock, on_toe)
            # спад: до и после опоры цель тянется к замку с убывающим весом
            for j in range(1, ramp + 1):
                e = 0.5 - 0.5 * np.cos(np.pi * (1 - j / (ramp + 1)))
                for k, (lk, toe) in ((s[0] - j, first), (s[-1] + j, last)):
                    if not loop and not 0 <= k < n:
                        continue
                    if k in aim or (loop and any((k - q) % m == 0 and q in aim for q in (k - m, k + m))):
                        continue
                    a = wpos(fo, k)
                    aim[k] = a + (lk + (a - wpos(to if toe else fo, k)) - a) * e
        moved = 0.0
        for k, a in aim.items():
            j, c = (k % m, k // m) if loop else (k, 0)
            T = M[j].T @ (a - D * c - off[j])  # в осях корня ключа
            moved = max(moved, float(np.linalg.norm(T - P[j, fo])))
            R1, R2 = two_bone(P[j, th], P[j, ca], P[j, fo], T)
            pt = sk.parent[th]
            Wt = R1 @ W[j, th]
            Wc = R2 @ R1 @ W[j, ca]
            rot[j, th] = m_to_q((W[j, pt] @ sk.local[th]).T @ Wt)
            rot[j, ca] = m_to_q((Wt @ sk.local[ca]).T @ Wc)
            rot[j, fo] = m_to_q((Wc @ sk.local[fo]).T @ W[j, fo])
        if log is not None:
            log.append(f'{sk.names[fo]}: опор {len(sp)}, сдвиг лодыжки до {moved * 100:.1f} см')
    if loop:
        rot[n - 1] = rot[0]
    return rot
