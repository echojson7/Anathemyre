"""Упрощение сетки тела, как у беты (stage0/test-room, tools/humanconv/humanconv.py: decimate) — LOD 0 людей.

Схлопывание рёбер по квадрикам (Гарланд — Хекберт), вершина уходит в соседа (half-edge): положения вершин не
меняются, уходят лишние. Швы (смена ключа грани — материал, область скрытия под одеждой) и края: вершина шва идёт
только вдоль шва, угол шва неподвижен, поэтому край белья и губ не рвётся. Вес вершины (лицо дороже) множит её
квадрику: лицо уходит последним. У беты так выглядели лица: мелкие складки сетки MakeHuman сглажены, нормали — от
упрощённых граней.

Всё на числах Python (не numpy): порядок схлопываний зависит от сравнения цен, а цены — от сумм; суммы numpy
разных версий могут разойтись в последнем бите, Python — нет. Поэтому байты сетки не зависят от версии numpy.
"""
import heapq


def _plane_q(n, d, w):
    # Квадрика плоскости n·x + d = 0 с весом w: верхний треугольник 4×4 (10 чисел)
    a, b, c = n
    return [w * a * a, w * a * b, w * a * c, w * a * d, w * b * b, w * b * c, w * b * d, w * c * c, w * c * d, w * d * d]


def _cost(q, p):
    x, y, z = p
    return (q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x + q[4] * y * y + 2 * q[5] * y * z
            + 2 * q[6] * y + q[7] * z * z + 2 * q[8] * z + q[9])


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def decimate(V, T, key, targets, wv):
    """V — вершины (список троек), T — треугольники (списки), key — ключ грани (шов там, где он меняется),
    targets — сколько треугольников оставить (по убыванию, на каждую цель — снимок), wv — вес вершины.
    Возвращает на цель (номера оставшихся треугольников исходного T, их вершины, ошибка в метрах). Номера вершин —
    те же, что у V. Ошибка — наибольшее по схлопываниям среднеквадратичное (по площади) расстояние новой вершины
    до плоскостей исходных граней её области, копится (не меньше прошлой цели)."""
    V = [tuple(float(c) for c in p) for p in V]
    T = [list(map(int, t)) for t in T]
    key = [int(k) for k in key]
    nv, nt = len(V), len(T)
    alive = [True] * nt
    vf = [set() for _ in range(nv)]
    for f, t in enumerate(T):
        for a in t:
            vf[a].add(f)
    Q = [[0.0] * 10 for _ in range(nv)]
    Qa = [[0.0] * 10 for _ in range(nv)]  # без швов и весов: для ошибки в метрах
    A = [0.0] * nv
    nn = []
    for f, (i, j, k) in enumerate(T):
        n = _cross(_sub(V[j], V[i]), _sub(V[k], V[i]))
        area = _dot(n, n) ** 0.5
        u = (n[0] / area, n[1] / area, n[2] / area) if area > 1e-20 else (0.0, 0.0, 0.0)
        nn.append(u)
        q = _plane_q(u, -_dot(u, V[i]), area)
        for a in (i, j, k):
            qa, qe = Q[a], Qa[a]
            for c in range(10):
                qa[c] += q[c]
                qe[c] += q[c]
            A[a] += area
    ef = {}
    for f, t in enumerate(T):
        for s in range(3):
            a, b = t[s], t[(s + 1) % 3]
            ef.setdefault((min(a, b), max(a, b)), []).append(f)
    seam = set()
    for e in sorted(ef):
        fs = ef[e]
        if len(fs) != 2 or key[fs[0]] != key[fs[1]]:
            seam.add(e)
            # Плоскость вдоль шва поперёк грани держит шов на месте
            d = _sub(V[e[1]], V[e[0]])
            dd = _dot(d, d)
            for f in fs:
                m = _cross(d, nn[f])
                lm = max(_dot(m, m) ** 0.5, 1e-20)
                m = (m[0] / lm, m[1] / lm, m[2] / lm)
                q = _plane_q(m, -_dot(m, V[e[0]]), 20.0 * dd)
                for a in e:
                    qa = Q[a]
                    for c in range(10):
                        qa[c] += q[c]
    for a in range(nv):
        Q[a] = [c * wv[a] for c in Q[a]]
    sn = {}
    for a, b in sorted(seam):
        sn.setdefault(a, set()).add(b)
        sn.setdefault(b, set()).add(a)

    def nbr(a):
        s = set()
        for f in vf[a]:
            s.update(T[f])
        s.discard(a)
        return s

    def allowed(a):
        # Куда можно увести вершину a
        if a in sn:
            return sn[a] if len(sn[a]) == 2 and len({key[f] for f in vf[a]}) <= 2 else set()
        return nbr(a)

    def valid(a, b):
        shared = [f for f in vf[a] if b in T[f]]
        opp = {x for f in shared for x in T[f] if x != a and x != b}
        if nbr(a) & nbr(b) != opp:
            return False
        for f in vf[a]:
            t = T[f]
            if b in t:
                continue
            t = [b if x == a else x for x in t]
            n1 = _cross(_sub(V[t[1]], V[t[0]]), _sub(V[t[2]], V[t[0]]))
            l1 = _dot(n1, n1) ** 0.5
            if l1 < 1e-12 or _dot(n1, nn[f]) < 0.3 * l1:
                return False
        return True

    ver = [0] * nv
    heap = []

    def push(a):
        qa = Q[a]
        c = sorted((_cost([qa[k] + Q[b][k] for k in range(10)], V[b]), b) for b in allowed(a))
        for cost, b in c[:4]:
            if valid(a, b):
                heapq.heappush(heap, (cost, a, b, ver[a]))
                return

    for a in range(nv):
        if vf[a]:
            push(a)
    count = nt
    err = 0.0
    out = []
    for target in targets:
        while count > target and heap:
            _, a, b, va = heapq.heappop(heap)
            if va != ver[a] or not vf[a] or b not in allowed(a) or not valid(a, b):
                continue
            for f in sorted(vf[a]):
                if b in T[f]:
                    alive[f] = False
                    for x in T[f]:
                        vf[x].discard(f)
                    count -= 1
                else:
                    T[f] = [b if x == a else x for x in T[f]]
                    vf[b].add(f)
            vf[a] = set()
            Q[b] = [Q[b][k] + Q[a][k] for k in range(10)]
            Qa[b] = [Qa[b][k] + Qa[a][k] for k in range(10)]
            A[b] += A[a]
            e = _cost(Qa[b], V[b]) / max(A[b], 1e-20)
            err = max(err, e ** 0.5 if e > 0.0 else 0.0)
            if a in sn:
                for c in sorted(sn.pop(a)):
                    sn[c].discard(a)
                    if c != b:
                        sn[c].add(b)
                        sn.setdefault(b, set()).add(c)
                if b in sn:
                    sn[b].discard(b)
            for c in sorted(nbr(b) | {b}):
                ver[c] += 1
                push(c)
        keep = [f for f in range(nt) if alive[f]]
        out.append((keep, [list(T[f]) for f in keep], err))
    return out
