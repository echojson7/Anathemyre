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
// Очередь молодых (lcache_list.comp): корзины «ни разу» и «не прошли круг» по уровням, крупные раньше; мест в корзине
const uint LC_QB = 2u * uint(LC_LEVELS), LC_QCAP = LC_CAP / 16u;

float lc_size(int k) { return LC_S0 * float(1 << k); }

// Место работы i кадра (лучи, сглаживание): молодые — по корзинам очереди (начала — bs, lcache_args.comp), дальше —
// обновление
uint lc_job(uint i) {
    FrameData f = pc.frame;
    uint nn = f.lc_jobs.cmd.w;
    if (i >= nn) return f.lc_jobs.v[LC_CAP + i - nn];
    uint b = 0u;
    while (b + 1u < LC_QB && i >= f.lc_jobs.bs[b + 1u]) ++b;
    return f.lc_jobs.v[b * LC_QCAP + i - f.lc_jobs.bs[b]];
}

// Доверие к свету ячейки по числу счётов (1…4 из круга): молодая отдаёт остальное свету предков (lc_level) и
// соседям в сглаживании — новая ячейка входит в кадр за круг, а не пятном первой оценки
float lc_conf(uint state) { return float(min((state >> 2) & 7u, LC_K)) / float(LC_K); }

#ifdef SDF_GLSL
// Ближнее затенение по полю (light.glsl, lcache_trace.comp): конусы до tmax; у ячеек — на полторы стороны ячейки у
// точки (lc_near_t, не меньше 25 см: старт конусов — полвокселя поля), дальше ячейка затенена своими лучами
float lc_near_ao(vec3 pos, vec3 n, float tmax) {
    uint q = pc.frame.gi_q;
    uint steps = q == LIGHT_Q_LOW ? 4u : q == LIGHT_Q_MEDIUM ? 5u : q == LIGHT_Q_HIGH ? 6u : 7u;
    return sdf_cones(pos, n, q, tmax, steps).x;
}
float lc_near_t(vec3 pos) { return clamp(length(pos) * pc.frame.lc_angle * 1.5, 0.25, 1.0); }
// То же у точки ячейки (pos — м от камеры, на поверхности под точкой лучей)
float lc_cell_ao(vec3 pos, vec3 n) { return lc_near_ao(pos, n, lc_near_t(pos)); }
#endif

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
// Номер кубика ячейки: у ячеек самой земли сверху (сторона +y) слой по высоте один — 0. Их точка лучей — на сетке
// высот в середине столбца (lcache_trace.comp) и от слоя не зависит, а со слоем соседний столбец на склоне лежал в
// другом слое: сбор его не находил, и свет на склоне шёл ступенями вдоль горизонталей (разбор 18.11, шаг 6б)
ivec3 lc_home_of(vec3 pa, int k, int a, bool ground) {
    ivec3 h = lc_home(pa, k, a);
    if (ground && a == 2) h.y = 0;
    return h;
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
int lc_claim(uint key, ivec3 home) {
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
        return -1;
    }
    lc_touch(e, key, home);
    return e;
}
// Ячейка ещё не прошла круг (заявлена заново или молода): пиксели берут свет её родителя
bool lc_young(int e) {
    uint st = pc.frame.lc_meta.v[e].state;
    return (st & LC_INIT) != 0u || ((st >> 2) & 7u) < LC_K;
}
// Ячейки, которые прочтёт свет точки rel (м от камеры) с нормалью n: одна-две стороны нормали, один-два уровня
// (у края полосы уровня — и следующий; пока главная молода — тоже: её свет пока — от родителя, и в новом месте
// родитель считается первым). Свет смешивает соседние ячейки; их заявляют пиксели, которые в них лежат.
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
    return lc_key(k, a, lc_home_of(rel + f.sdf_cam.xyz, k, a, ground), ground);
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
    bool two = w2 > LC_AXIS2;
    ivec3 h = lc_home_of(pa, k, a, ground);
    int e = lc_claim(lc_key(k, a, h, ground), h);
    bool up = k + 1 < LC_LEVELS && (x > float(k) - LC_BAND || (e >= 0 && lc_young(e)));
    if (two) {
        h = lc_home_of(pa, k, a2, ground);
        lc_claim(lc_key(k, a2, h, ground), h);
    }
    if (up) {
        h = lc_home_of(pa, k + 1, a, ground);
        lc_claim(lc_key(k + 1, a, h, ground), h);
        if (two) {
            h = lc_home_of(pa, k + 1, a2, ground);
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

// Ячейка e годится точке pa: её точка лучей в плоскости точки (дальше четверти стороны с подъёмом — чужая
// поверхность: ступенька, край плиты), 0..1. Земле сверху не нужно (lc_gather_one): точки её ячеек — на той же
// сетке высот, а сама точка — на земле с деталями (рябь, гребни до ~1 м над сеткой 8 м), и подъём между ними
// отбрасывал на склонах и гребнях соседей и даже свою ячейку
float lc_plane_w(int e, vec3 pa, vec3 N, float s) {
    float dz = abs(dot(pc.frame.lc_meta.v[e].org - pa, N) - (0.05 + 0.05 * s));
    return clamp(1.0 - (dz - 0.25 * s) / (0.5 * s), 0.0, 1.0);
}

// Свет уровня у точки (lc_gather_one): среднее с доверием, доля с доверием, найденная доля (из 1), затенение у точек
struct LcG { vec3 irr; float c, f, ao; };
bool g_lc_young = false;  // сбор задел молодые или ждущие счёта ячейки (кеш земли досчитает страницу в следующем кадре)

// Место ячейки (key, home) и доверие к ней, как lc_lookup. Молодая или заявленная, но ещё не посчитанная (ждёт в
// очереди) — отметка g_lc_young: её свет вот-вот изменится
int lc_lookup_c(uint key, ivec3 home, out float conf) {
    int fe;
    conf = 0.0;
    int e = lc_find(key, fe);
    if (e < 0) return -1;
    LcMeta m = pc.frame.lc_meta.v[e];
    if (m.key != key || m.home != home) return -1;
    if ((m.state & (LC_LIT | LC_EMPTY)) != LC_LIT) {
        if ((m.state & LC_INIT) != 0u) g_lc_young = true;
        return -1;
    }
    conf = lc_conf(m.state);
    if (conf < 1.0) g_lc_young = true;
    return e;
}

// Свет одной стороны и уровня у точки: билинейно по 4 ячейкам в плоскости (центры ячеек вдоль поверхности — на
// середине кубиков), слой поперёк — тот же, что у точки. Соседа нет или он чужой поверхности — его долю берёт
// своя ячейка (в которой точка), если она есть: у стены и колонны соседа за гранью нет вовсе (его кубик в
// заслонке), и без замены доля ячеек у каждой стены проседала — свет у основания уходил на зонды с их затенением
// на 1 м, тёмное кольцо с резким краем (разбор 17.11). Своей нет (молодое место) — без веса. Вес ячейки — ещё и
// доверие к ней (lc_conf): недоверенное возьмут предки (lc_level). ao — сглаженное ближнее затенение у точек ячеек
// теми же весами
LcG lc_gather_one(vec3 pa, vec3 n, int k, int a, bool ground) {
    LcG r;
    r.irr = vec3(0.0);
    r.c = r.f = 0.0;
    r.ao = 1.0;
    if (k >= LC_LEVELS) return r;
    float s = lc_size(k);
    int ax = a >> 1, i1 = (ax + 1) % 3, i2 = (ax + 2) % 3;
    ivec3 h = lc_home_of(pa, k, a, ground);
    vec2 f = vec2(pa[i1], pa[i2]) / s - 0.5;
    vec2 b = floor(f), fr = f - b;
    vec3 N = lc_axis_n(a);
    bool ground_up = ground && a == 2;  // земля сверху: соседи — та же поверхность (lc_home_of, lc_plane_w)
    float ch;
    int eh = lc_lookup_c(lc_key(k, a, h, ground), h, ch);
    float wh = eh >= 0 ? (ground_up ? 1.0 : lc_plane_w(eh, pa, N, s)) : 0.0;
    vec3 ih = wh > 0.0 ? lc_irr(eh, n) : vec3(0.0);
    float aoh = wh > 0.0 ? unpackHalf2x16(pc.frame.lc_meta.v[eh].ao).y : 1.0;
    vec3 sum = vec3(0.0);
    float asum = 0.0;
    for (int j = 0; j < 4; ++j) {
        ivec2 o = ivec2(j & 1, j >> 1);
        ivec3 c = h;
        c[i1] = int(b.x) + o.x;
        c[i2] = int(b.y) + o.y;
        float w = (o.x == 0 ? 1.0 - fr.x : fr.x) * (o.y == 0 ? 1.0 - fr.y : fr.y);
        if (w <= 0.0) continue;
        float ce = ch;
        int e = c == h ? eh : lc_lookup_c(lc_key(k, a, c, ground), c, ce);
        float we = e >= 0 ? w * (c == h ? wh : ground_up ? 1.0 : lc_plane_w(e, pa, N, s)) : 0.0;
        if (we > 0.0) {
            float wc = we * ce;
            sum += lc_irr(e, n) * wc;
            asum += unpackHalf2x16(pc.frame.lc_meta.v[e].ao).y * wc;
            r.c += wc;
            r.f += we;
        }
        float wr = wh > 0.0 ? w - we : 0.0;  // недостающее — своей ячейкой
        sum += ih * (wr * ch);
        asum += aoh * (wr * ch);
        r.c += wr * ch;
        r.f += wr;
    }
    if (r.c > 0.0) {
        r.irr = sum / r.c;
        r.ao = asum / r.c;
    }
    return r;
}

// Уровень k с подменой предками: недоверенную и недостающую долю берут уровни выше по порядку (родитель, его
// родитель…), а что не взял никто — молодые ячейки самого уровня целиком. Подлёт: мелкие ячейки новые, а предки —
// уровни, которыми это место светилось мгновения назад, — новые входят за круг без пятен (разбор 18.11)
LcG lc_level(vec3 pa, vec3 n, int k, int a, bool ground) {
    LcG x = lc_gather_one(pa, n, k, a, ground);
    vec3 sum = x.irr * x.c;
    float asum = x.ao * x.c, c = x.c;
    for (int kk = k + 1; kk < LC_LEVELS && c < 1.0 - 1e-4; ++kk) {
        LcG p = lc_gather_one(pa, n, kk, a, ground);
        float t = (1.0 - c) * p.c;
        sum += p.irr * t;
        asum += p.ao * t;
        c += t;
    }
    float tr = min(1.0 - c, max(x.f - x.c, 0.0));
    sum += x.irr * tr;
    asum += x.ao * tr;
    c += tr;
    LcG r;
    r.c = r.f = c;
    r.irr = c > 0.0 ? sum / c : vec3(0.0);
    r.ao = c > 0.0 ? asum / c : 1.0;
    return r;
}

// Отражённый свет точки из ячеек (docs/lcache.md, «Сбор»): E/π по нормали n и доля ячеек 0..1 (остальное — зонды).
// Стороны нормали смешиваются по наклону (у ребра 45° — поровну), уровни — в полосе у края уровня (там заявлены оба),
// каждый уровень — с подменой предками (lc_level); дальше последнего уровня и у предела дальности ячейки сходят на
// зонды.
// Найденная доля меньше половины — зонды
vec4 lc_gather(vec3 pos, vec3 n, bool ground, out float ao) {
    FrameData f = pc.frame;
    float d = length(pos);
    ao = 1.0;
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
    float got = 0.0, want = 0.0, asum = 0.0;
    for (int i = 0; i < 4; ++i) {
        float w = ((i >> 1) == 0 ? 1.0 - wl : wl) * ((i & 1) == 0 ? 1.0 - wa : wa);
        if (w <= 0.0) continue;
        LcG g = lc_level(pa, n, k + (i >> 1), (i & 1) == 0 ? a : a2, ground);
        sum += g.irr * (w * g.c);
        asum += g.ao * (w * g.c);
        got += w * g.c;
        want += w;
    }
    ao = got > 0.0 ? asum / got : 1.0;
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
    ivec3 h = lc_home_of(pa, k, a, ground);
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
