// Ячейки света на поверхностях (docs/lcache.md, vk_lcache.cpp): отражённый свет в маленьких мировых ячейках прямо
// на полу и стенах. Ячейка — кубик мира уровня k (сторона LC_S0·2^k) и одна из 6 сторон нормали; уровень — по
// расстоянию до камеры (ячейка видна под углом ступени), не по взгляду: поворот камеры уровни не меняет.
// Таблица — корзины по 8 ключей; ключ — уровень, сторона и номер кубика по модулю 256 (окно уровня шире, чем
// ячейки уровня отходят от камеры); мировой номер — в месте, для проверки. Заявки ставит разбор плиток
// (light_tiles.comp), счёт — lcache_trace.comp, свет читает показанное (гармоники L1, половинками)
#ifndef LCACHE_GLSL
#define LCACHE_GLSL
#include "common.glsl"

const uint LC_CAP = 65536u, LC_BUCKETS = 8192u;  // мест таблицы и корзин по 8
const int LC_LEVELS = 5;
const float LC_S0 = 0.125;          // сторона ячейки уровня 0, м
const uint LC_K = 4u;               // поворотов лучей в круге (как у зондов)
const uint LC_AGE = 240u;           // кадров без взгляда — место освобождается
const float LC_W_OFF = 0.37;        // сдвиг решётки поперёк поверхности, доли стороны: круглые высоты полов не на
                                    // границе кубиков
const float LC_AXIS2 = 0.35;        // вторая сторона нормали заявляется, если её доля больше
const float LC_BAND = 0.25;         // полоса уровня (в логарифме), где заявляется и следующий
const uint LC_VALID = 0x80000000u;  // ключ: занято; 0 — пусто
const uint LC_GROUND = 0x40000000u; // ключ: ячейка земли (точка — на сетке высот лучей, а не по полю)
// Состояние места: поворот следующего счёта (биты 0–1), счётов с нового (2–4), флаги
const uint LC_INIT = 0x100u;   // заявлена заново: счёт с нуля
const uint LC_EMPTY = 0x200u;  // поверхности нет или ячейка в заслонке: свет её не читает
const uint LC_LIT = 0x400u;    // посчитана хоть раз

float lc_size(int k) { return LC_S0 * float(1 << k); }

// Уровень у точки на расстоянии d от камеры — дробно: log2(d·tg угла / сторона уровня 0)
float lc_level_f(float d) { return log2(max(d * pc.frame.lc_angle / LC_S0, 1e-6)); }

// Сторона нормали: 0, 1 — ±x, 2, 3 — ±y, 4, 5 — ±z
int lc_axis_of(vec3 n, out int second, out float w2) {
    vec3 a = abs(n);
    int i = a.x >= a.y && a.x >= a.z ? 0 : a.y >= a.z ? 1 : 2;
    vec3 b = a;
    b[i] = -1.0;
    int j = b.x >= b.y && b.x >= b.z ? 0 : b.y >= b.z ? 1 : 2;
    second = 2 * j + (n[j] < 0.0 ? 1 : 0);
    w2 = a[j];
    return 2 * i + (n[i] < 0.0 ? 1 : 0);
}
vec3 lc_axis_n(int a) {
    vec3 n = vec3(0.0);
    n[a >> 1] = (a & 1) == 0 ? 1.0 : -1.0;
    return n;
}

// Номер кубика уровня k у точки pa (м от якоря поля) для стороны a: поперёк поверхности решётка сдвинута на LC_W_OFF
ivec3 lc_home(vec3 pa, int k, int a) {
    float s = lc_size(k);
    vec3 u = pa / s;
    u[a >> 1] -= LC_W_OFF;
    return ivec3(floor(u));
}
// Центр кубика (м от якоря)
vec3 lc_center(ivec3 c, int k, int a) {
    vec3 u = vec3(c) + 0.5;
    u[a >> 1] += LC_W_OFF;
    return u * lc_size(k);
}
uint lc_key(int k, int a, ivec3 c, bool ground) {
    return LC_VALID | (ground ? LC_GROUND : 0u) | uint(k) | uint(a) << 3 | uint(c.x & 255) << 6 |
           uint(c.y & 255) << 14 | uint(c.z & 255) << 22;
}
int lc_key_level(uint key) { return int(key & 7u); }
int lc_key_axis(uint key) { return int((key >> 3) & 7u); }

uint lc_hash(uint v) {
    v ^= v >> 16;
    v *= 0x7FEB352Du;
    v ^= v >> 15;
    v *= 0x846CA68Bu;
    return v ^ (v >> 16);
}
uint lc_bucket(uint key) { return (lc_hash(key) & (LC_BUCKETS - 1u)) * 8u; }

// Место ключа v среди четырёх ключей a (первое место — base) или −1. Без циклов: разбор плиток идёт под GPU-AV
// lavapipe, а он падает на доступе к буферу в цикле шейдера с barrier() (docs/shadow.md)
int lc_pick(uvec4 a, uint v, int base) {
    return a.x == v ? base : a.y == v ? base + 1 : a.z == v ? base + 2 : a.w == v ? base + 3 : -1;
}
// Место ключа в двух соседних корзинах или −1; fe — первое пустое там же (или −1)
int lc_find(uint key, out int fe) {
    FrameData f = pc.frame;
    uint b0 = lc_bucket(key), b1 = (b0 + 8u) & (LC_CAP - 1u);
    uvec4 k0 = f.lc_keys4.v[b0 >> 2], k1 = f.lc_keys4.v[(b0 >> 2) + 1u];
    uvec4 k2 = f.lc_keys4.v[b1 >> 2], k3 = f.lc_keys4.v[(b1 >> 2) + 1u];
    int e = lc_pick(k0, key, int(b0));
    if (e < 0) e = lc_pick(k1, key, int(b0) + 4);
    if (e < 0) e = lc_pick(k2, key, int(b1));
    if (e < 0) e = lc_pick(k3, key, int(b1) + 4);
    fe = lc_pick(k0, 0u, int(b0));
    if (fe < 0) fe = lc_pick(k1, 0u, int(b0) + 4);
    if (fe < 0) fe = lc_pick(k2, 0u, int(b1));
    if (fe < 0) fe = lc_pick(k3, 0u, int(b1) + 4);
    return e;
}

#ifdef LC_CLAIM
// Ячейка нужна кадру: отметка взгляда; новая (место заняли, место чужое или освободилось) — счёт с нуля. Первый
// за кадр, кто отметил место (обмен кадра), проверяет, его ли оно; все, кто пишет новое место в одном кадре,
// пишут одно и то же (в одном кадре у ключа один мировой номер: окно шире ячеек)
void lc_touch(int e, uint key, ivec3 home) {
    FrameData f = pc.frame;
    uint fr = f.lc_frame;
    if (f.lc_meta.v[e].seen == fr) return;
    if (atomicExchange(f.lc_meta.v[e].seen, fr) == fr) return;
    LcMeta m = f.lc_meta.v[e];
    if (m.key == key && m.home == home) return;
    f.lc_meta.v[e].home = home;
    f.lc_meta.v[e].key = key;
    f.lc_meta.v[e].state = LC_INIT;
    atomicAdd(f.stats.v[STAT_LC_NEW], 1u);
}
// Заявка ячейки: есть — отметка; нет — первое пустое место двух корзин (сравнение с обменом, вторая попытка, если
// место перехватил другой ключ). Мест нет — без ячейки этот кадр (свет — от зондов), счётчик LC_OVER
void lc_claim(uint key, ivec3 home) {
    FrameData f = pc.frame;
    int fe;
    int e = lc_find(key, fe);
    if (e < 0 && fe >= 0) {
        uint prev = atomicCompSwap(f.lc_keys.v[fe], 0u, key);
        if (prev == 0u || prev == key) {
            e = fe;
        } else {
            e = lc_find(key, fe);
            if (e < 0 && fe >= 0) {
                prev = atomicCompSwap(f.lc_keys.v[fe], 0u, key);
                if (prev == 0u || prev == key) e = fe;
            }
        }
    }
    if (e < 0) {
        atomicAdd(f.stats.v[STAT_LC_OVER], 1u);
        return;
    }
    lc_touch(e, key, home);
}
// Ячейки, которые прочтёт свет точки rel (м от камеры) с нормалью n: одна-две стороны нормали, один-два уровня
// (у края полосы уровня — и следующий). Свет смешивает соседние ячейки; их заявляют пиксели, которые в них лежат.
// first — у соседа по плитке та же главная ячейка: заявки ставит только первый (key0 — главная ячейка пикселя)
uint lc_primary(vec3 rel, vec3 n, bool ground) {
    FrameData f = pc.frame;
    float d = length(rel);
    if (f.lc_on == 0u || d > f.lc_far) return 0u;
    float x = lc_level_f(d);
    int k = clamp(int(ceil(x)), 0, LC_LEVELS - 1);
    if (x > float(LC_LEVELS - 1)) return 0u;
    int a2;
    float w2;
    int a = lc_axis_of(n, a2, w2);
    return lc_key(k, a, lc_home(rel + f.sdf_cam.xyz, k, a), ground);
}
void lc_request(vec3 rel, vec3 n, bool ground) {
    FrameData f = pc.frame;
    float d = length(rel);
    float x = lc_level_f(d);
    int k = clamp(int(ceil(x)), 0, LC_LEVELS - 1);
    vec3 pa = rel + f.sdf_cam.xyz;
    int a2;
    float w2;
    int a = lc_axis_of(n, a2, w2);
    bool up = x > float(k) - LC_BAND && k + 1 < LC_LEVELS;
    bool two = w2 > LC_AXIS2;
    ivec3 h = lc_home(pa, k, a);
    lc_claim(lc_key(k, a, h, ground), h);
    if (two) {
        h = lc_home(pa, k, a2);
        lc_claim(lc_key(k, a2, h, ground), h);
    }
    if (up) {
        h = lc_home(pa, k + 1, a);
        lc_claim(lc_key(k + 1, a, h, ground), h);
        if (two) {
            h = lc_home(pa, k + 1, a2);
            lc_claim(lc_key(k + 1, a2, h, ground), h);
        }
    }
}
#endif

// Место ячейки (key, home), посчитанной и не пустой, или −1
int lc_lookup(uint key, ivec3 home) {
    int fe;
    int e = lc_find(key, fe);
    if (e < 0) return -1;
    LcMeta m = pc.frame.lc_meta.v[e];
    if (m.key != key || m.home != home || (m.state & (LC_LIT | LC_EMPTY)) != LC_LIT) return -1;
    return e;
}

// Гармоники места половинками → a0, a1 по осям (E/π = a0 + a1·n)
void lc_unpack(LcSh s, out vec3 a0, out vec3 ax, out vec3 ay, out vec3 az) {
    vec2 p0 = unpackHalf2x16(s.a.x), p1 = unpackHalf2x16(s.a.y), p2 = unpackHalf2x16(s.a.z);
    vec2 p3 = unpackHalf2x16(s.a.w), p4 = unpackHalf2x16(s.b.x), p5 = unpackHalf2x16(s.b.y);
    a0 = vec3(p0, p1.x);
    ax = vec3(p1.y, p2);
    ay = vec3(p3, p4.x);
    az = vec3(p4.y, p5);
}
// Сглаженный свет места (его читает свет): E/π по нормали n — a0 + a1·n (гармоники L1 со свёрткой косинусом),
// не меньше нуля
vec3 lc_irr(int e, vec3 n) {
    vec3 a0, ax, ay, az;
    lc_unpack(pc.frame.lc_filt.v[e], a0, ax, ay, az);
    return max(a0 + ax * n.x + ay * n.y + az * n.z, vec3(0.0));
}
LcSh lc_pack(vec3 a0, vec3 ax, vec3 ay, vec3 az) {
    LcSh s;
    s.a = uvec4(packHalf2x16(a0.xy), packHalf2x16(vec2(a0.z, ax.x)), packHalf2x16(ax.yz), packHalf2x16(ay.xy));
    s.b = uvec2(packHalf2x16(vec2(ay.z, az.x)), packHalf2x16(az.yz));
    return s;
}

// Свет одной стороны и уровня у точки: билинейно по 4 ячейкам в плоскости (центры ячеек вдоль поверхности — на
// середине кубиков), слой поперёк — тот же, что у точки. Ячейка чужой поверхности (точка лучей дальше от
// плоскости точки, чем четверть стороны с подъёмом: ступенька, край плиты) и ненайденные — без веса; x — сумма
// весов, найденных из 1
vec4 lc_gather_one(vec3 pa, vec3 n, int k, int a, bool ground) {
    float s = lc_size(k);
    int ax = a >> 1, i1 = (ax + 1) % 3, i2 = (ax + 2) % 3;
    ivec3 h = lc_home(pa, k, a);
    vec2 f = vec2(pa[i1], pa[i2]) / s - 0.5;
    vec2 b = floor(f), fr = f - b;
    vec3 N = lc_axis_n(a);
    float lift = 0.05 + 0.05 * s;
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    for (int j = 0; j < 4; ++j) {
        ivec2 o = ivec2(j & 1, j >> 1);
        ivec3 c = h;
        c[i1] = int(b.x) + o.x;
        c[i2] = int(b.y) + o.y;
        float w = (o.x == 0 ? 1.0 - fr.x : fr.x) * (o.y == 0 ? 1.0 - fr.y : fr.y);
        int e = lc_lookup(lc_key(k, a, c, ground), c);
        if (e < 0 || w <= 0.0) continue;
        float dz = abs(dot(pc.frame.lc_meta.v[e].org - pa, N) - lift);
        w *= clamp(1.0 - (dz - 0.25 * s) / (0.5 * s), 0.0, 1.0);
        sum += lc_irr(e, n) * w;
        wsum += w;
    }
    return vec4(wsum > 0.0 ? sum / wsum : vec3(0.0), wsum);
}

// Отражённый свет точки из ячеек (docs/lcache.md, «Сбор»): E/π по нормали n и доля ячеек 0..1 (остальное — зонды).
// Стороны нормали смешиваются по наклону (у ребра 45° — поровну), уровни — в полосе у края уровня (там заявлены оба),
// дальше последнего уровня и у предела дальности ячейки сходят на зонды. Найденная доля меньше половины — зонды
vec4 lc_gather(vec3 pos, vec3 n, bool ground) {
    FrameData f = pc.frame;
    float d = length(pos);
    if (f.lc_on == 0u || d > f.lc_far) return vec4(0.0);
    float x = lc_level_f(d);
    if (x > float(LC_LEVELS - 1)) return vec4(0.0);
    int k = clamp(int(ceil(x)), 0, LC_LEVELS - 1);
    vec3 pa = pos + f.sdf_cam.xyz;
    int a2;
    float w2;
    int a = lc_axis_of(n, a2, w2);
    float w1 = abs(n[a >> 1]);
    float wa = clamp((w2 - LC_AXIS2) / max(w1 - LC_AXIS2, 1e-4), 0.0, 1.0) * 0.5;  // доля второй стороны
    float wl = k + 1 < LC_LEVELS ? clamp((x - (float(k) - LC_BAND)) / LC_BAND, 0.0, 1.0) : 0.0;  // доля уровня k+1
    vec3 sum = vec3(0.0);
    float got = 0.0, want = 0.0;
    for (int i = 0; i < 4; ++i) {
        int kk = k + (i >> 1), aa = (i & 1) == 0 ? a : a2;
        float w = ((i >> 1) == 0 ? 1.0 - wl : wl) * ((i & 1) == 0 ? 1.0 - wa : wa);
        if (w <= 0.0) continue;
        vec4 g = lc_gather_one(pa, n, kk, aa, ground);
        sum += g.rgb * (w * g.w);
        got += w * g.w;
        want += w;
    }
    float cov = smoothstep(0.5, 0.8, got / max(want, 1e-6));
    // Последний уровень (за ним уровня нет) и последние 8 м дальности — на зонды
    if (k + 1 >= LC_LEVELS) cov *= clamp((float(LC_LEVELS - 1) - x) / LC_BAND, 0.0, 1.0);
    cov *= clamp((f.lc_far - d) / 8.0, 0.0, 1.0);
    return vec4(got > 0.0 ? sum / got : vec3(0.0), cov);
}

// F3 «ячейки» (docs/lcache.md): серая карта 50 % под светом ближайшей ячейки главной стороны, края ячеек темнее;
// без ячейки — красноватый свет зондов (out ok = false)
vec3 lc_view(vec3 pos, vec3 n, bool ground, out bool ok) {
    FrameData f = pc.frame;
    ok = false;
    float d = length(pos);
    if (f.lc_on == 0u || d > f.lc_far) return vec3(0.0);
    float x = lc_level_f(d);
    int k = clamp(int(ceil(x)), 0, LC_LEVELS - 1);
    if (x > float(LC_LEVELS - 1)) return vec3(0.0);
    int a2;
    float w2;
    int a = lc_axis_of(n, a2, w2);
    vec3 pa = pos + f.sdf_cam.xyz;
    ivec3 h = lc_home(pa, k, a);
    int e = lc_lookup(lc_key(k, a, h, ground), h);
    if (e < 0) return vec3(0.0);
    ok = true;
    vec3 fr = fract(pa / lc_size(k));
    fr[a >> 1] = 0.5;
    vec3 edge = min(fr, 1.0 - fr);
    float line = min(edge.x, min(edge.y, edge.z)) < 0.04 ? 0.7 : 1.0;
    return 0.5 * lc_irr(e, n) * line;
}
#endif
