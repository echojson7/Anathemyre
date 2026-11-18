// Огни (6е, docs/lamps.md): колбы и трубки ламп освещают поверхность — раз на ячейку, из списка клетки froxels.
// Единицы — как у солнца: сила света в канделах, освещённость в люксах. Трубка — отрезок: рассеянное — освещённость
// от отрезка (Karis 2013), отражение — ближайшая к лучу отражения точка отрезка, затем шара тела (там же) с
// поправкой энергии. Дальность — окно (1 − (d/R)⁴)², без резкого края. Направленная лампа (face) светит назад
// долей back: лампа на стене не освещает стену за собой. Тень — конус по полю расстояний мира (docs/sdf.md) от
// точки к телу света: мягкая по его размеру, одна и та же в каждом кадре, без карт и без мерцания
#ifndef LAMPS_GLSL
#define LAMPS_GLSL
#include "sdf.glsl"
#include "froxel.glsl"
#include "lamp_map.glsl"
// Карты огней (docs/lamp_shadows.md): кубы глубины — со сравнением (2×2 билинейно) и как есть (поиск заслонок)
layout(set = 0, binding = 14) uniform samplerCubeArrayShadow u_lsm;
layout(set = 0, binding = 15) uniform samplerCubeArray u_lsm_d;
uint g_lsm_taps = 0u;  // выборки карт огней этого потока (STAT_LSM_TAPS)

const float LAMP_AWAY = 0.25;     // м: цель тени отходит от стены по face (иначе конус трётся о стену лампы)

// Порог счёта огня (docs/lamps.md, «Дальность»): белая стена под ним на экране — LAMP_SKIP_D до кривой (вчетверо
// ниже порога дальности lamps.cpp, ~1/1000 белого после кривой). Слабее — без тени и без счёта; от порога до
// двух порогов свет плавно уходит в ноль, без ступеньки. От экспозиции кадра: в зале и ночью — сотые доли люкса
// (было 0.25 лк постоянно: ночью это четверть всего света, и край пятна лампы был виден)
const float LAMP_SKIP_D = 0.0005;
// Экспозиция кадра для порогов огней (множитель HDR, sky_out [4].x); 0 — её ещё нет (первый кадр, буфер пуст)
float lamp_ex() {
    float ex = pc.frame.sky_out.v[SKY_OUT_EYE].x;
    return ex > 1e-7 && ex < 1e4 ? ex : 0.0;
}
// Порог счёта, лк; без глаза — 0.02 лк, как у глаза в зале станции
float lamp_e_skip() {
    float ex = lamp_ex();
    return ex > 0.0 ? LAMP_SKIP_D * 3.14159265 / ex : 0.02;
}
// Множитель у порога: 0 — ниже t, 1 — от 2t (em — освещённость в самом сильном канале)
float lamp_skip_k(float em, float t) { return smoothstep(t, 2.0 * t, em); }

// Тень слабого огня дешевле (docs/lamps.md, «Цена дальних ламп»): белая стена под огнём на экране (до кривой) от
// 0.02 — все шаги и узкий конус; к 0.004 (~1/250 после кривой) — вдвое меньше шагов и конус не уже 0.08 рад
// (мягче, крупнее шаг): тени такого огня глазу не видно, а дальние лампы зала иначе удваивали цену света.
// em — освещённость в самом сильном канале, лк; soft — наименьшая ширина конуса
uint lamp_lod_steps(float em, uint steps, out float soft) {
    float ex = lamp_ex();
    float k = ex > 0.0 ? clamp(log2(max(em * ex / 3.14159265, 1e-12) / 0.004) / 2.3219, 0.0, 1.0) : 1.0;
    soft = 0.08 * (1.0 - k);
    return uint(mix(float(max(steps / 2u, 6u)), float(steps), k) + 0.5);
}

// Доля силы света лампы в сторону dir (от лампы к точке, единичный)
float lamp_face(Light l, vec3 dir) {
    if (dot(l.face, l.face) == 0.0) return 1.0;
    return mix(l.back, 1.0, smoothstep(-0.2, 0.4, dot(l.face, dir)));
}

// Окно дальности (Karis 2013): 1 у лампы, 0 на r, плавно
float lamp_window(float d, float r) {
    float x = d / r;
    x *= x;
    float w = clamp(1.0 - x * x, 0.0, 1.0);
    return w * w;
}

// Ближайшая к p точка отрезка трубки (у колбы — середина)
vec3 lamp_closest(Light l, vec3 p) {
    float aa = dot(l.axis, l.axis);
    if (aa < 1e-8) return l.c;
    return l.c + l.axis * clamp(dot(p - l.c, l.axis) / aa, -1.0, 1.0);
}

// Освещённость площадки (p, n) на единицу силы света, 1/м². Колба — cos/d²; трубка — отрезок из колб с общей
// силой (Karis 2013): 2·sat(n·L0/2|L0| + n·L1/2|L1|) / (|L0||L1| + L0·L1). Радиус тела не даёт делить на ноль
float lamp_irr(Light l, vec3 p, vec3 n) {
    float s2 = l.size * l.size;
    vec3 L0 = l.c - l.axis - p;
    if (dot(l.axis, l.axis) < 1e-8) {
        float d2 = dot(L0, L0);
        return max(dot(n, L0), 0.0) * inversesqrt(d2) / (d2 + s2);
    }
    vec3 L1 = l.c + l.axis - p;
    float d0 = length(L0), d1 = length(L1);
    float nl = clamp(0.5 * (dot(n, L0) / d0 + dot(n, L1) / d1), 0.0, 1.0);
    return 2.0 * nl / (d0 * d1 + dot(L0, L1) + 2.0 * s2);
}

// Тень огня: конус по полю от точки pos (м от камеры, нормаль n) к телу света у точки tgt; ширина конуса — размер
// тела на расстоянии до него (не уже soft), конец — не доходя до тела (его самого и его стены в поле нет смысла видеть)
float lamp_shadow(vec3 pos, vec3 n, Light l, vec3 tgt, uint steps, float soft) {
    FrameData f = pc.frame;
    if (f.sdf_on == 0u || (l.flags & LIGHT_SHADOW) == 0u) return 1.0;
    // Старт — над поверхностью на полвокселя уровня у точки (как sdf_cones)
    vec3 dc = abs(pos);
    float vs = f.sdf_cam.w * max(1.0, max(dc.x, max(dc.y, dc.z)) / f.sdf_cam.w / SDF_FADE0);
    vec3 o = pos + n * (0.5 * vs);
    vec3 to = tgt + l.face * LAMP_AWAY - o;
    float d = length(to);
    float tmax = d - (l.size + 0.15);
    if (tmax <= vs) return 1.0;
    float th = clamp(max((l.size + 0.3 * length(l.axis)) / d, soft), 0.02, 0.5);
    return sdf_cone(o + f.sdf_cam.xyz, to / d, th, vs, tmax, steps).x;
}

// Шагов конуса тени на ступени качества
uint lamp_steps(uint q) { return q == LIGHT_Q_LOW ? 8u : q == LIGHT_Q_MEDIUM ? 12u : q == LIGHT_Q_HIGH ? 16u : 24u; }

// --- Тень картой (docs/lamp_shadows.md) ---
// Место карты огня + 1 (биты 8..15 flags, ставит vk_lamp_shadow.cpp); 0 — карты нет, тень по полю
uint lamp_map_slot(Light l) { return pc.frame.lsm_on != 0u ? (l.flags >> 8) & 0xFFu : 0u; }
// Куб слоя подвижных огня + 1 (биты 16..23): копия куба места кадра и поверх неё люди, машины и оружие у огня
// (vk_lamp_shadow.cpp). Свет кадра читает его вместо места; зонды, воздух и глаз — место (lamp_e). 0 — его нет
uint lamp_map_dyn(Light l) { return pc.frame.lsm_on != 0u ? (l.flags >> 16) & 0xFFu : 0u; }

// Точка i из n диска Фогеля радиуса 1: узор один во всех кадрах и точках (правило 2 docs/light.md — без поворота
// от кадра или пикселя, иначе край зернит); базис — от направления к точке, то есть тоже от мира
vec2 lsm_vogel(int i, int n) {
    float a = float(i) * 2.39996323;
    return vec2(cos(a), sin(a)) * sqrt((float(i) + 0.5) / float(n));
}

// Расстояние до приёмника по лучу w из огня (не единичный): пересечение с плоскостью приёмника (точка v от огня,
// нормаль n, vn = v·n). У каждой выборки фильтра — своя глубина плоскости: широкий фильтр на наклонной
// поверхности не затеняет её саму. Плоскость ребром или спиной к огню — расстояние точки d. Снизу не обрезается:
// у стены, вдоль которой светит лампа, выборка к стене пересекает плоскость в разы ближе d, и обрезка (было — не
// ближе d/2) делала саму стену заслонкой — стена между лампами темнела. Сверху — не дальше 2d (меньше тени, не больше)
float lsm_recv(vec3 w, float vn, vec3 n, float d) {
    float wn = dot(w, n);
    if (vn >= 0.0 || wn >= -1e-4) return d;
    return min(vn / wn * length(w), 2.0 * d);
}

// Ступень выборки по яркости огня в точке (em — самый сильный канал, лк) и ступени теней: 0 — одна выборка 2×2,
// 1 — PCSS 4 + 6, 2 — PCSS 8 + 12. Слабый огонь (белая стена под ним на экране до кривой тусклее 0.004) — 0
uint lamp_map_q(float em) {
    float ex = lamp_ex();
    float disp = ex > 0.0 ? em * ex / 3.14159265 : 1.0;
    uint q = disp < 0.004 ? 0u : disp < 0.02 ? 1u : 2u;
    uint qmax = pc.frame.shadow_q == LIGHT_Q_LOW ? 0u : pc.frame.shadow_q == LIGHT_Q_MEDIUM ? 1u : 2u;
    return min(q, qmax);
}

// Видимость огня l картой (место slot + 1) из точки p (м от камеры) с нормалью n: 1 — свет, 0 — тень.
// Сдвиг по нормали — тексель грани на расстоянии точки, при скользящем свете до трёх; смещение глубины — полтекселя.
// PCSS: заслонки ищутся в теле, увеличенном в 1.5 / расстояние (тангенс от огня), полутень — тело ×
// (приёмник − заслонка) / (заслонка · приёмник). Тело в плоскости выборок: у колбы — круг радиуса тела, у трубки —
// эллипс: вдоль её оси (в проекции) — полуось + радиус, поперёк — радиус. Тень от трубки мягкая вдоль неё и резкая
// поперёк, как в жизни; круглое тело размера трубки заходило в стену за ней (трубки зала — в 9.5 см от стены),
// и стена у лампы и предметы на стене получали ложную тень
float lamp_map_vis(Light l, uint slot, vec3 p, vec3 n, uint q) {
    float layer = float(slot - 1u);
    vec3 v0 = p - l.c;
    float d0 = length(v0);
    if (d0 >= LSM_RANGE || d0 < 1e-4) return 1.0;
    float texel = 2.0 * d0 / LSM_N;
    float nl = clamp(-dot(n, v0) / d0, 0.0, 1.0);
    vec3 v = v0 + n * (texel * (1.0 + 2.0 * (1.0 - nl)));
    float d = length(v);
    vec3 dir = v / d;
    float bias = 0.5 * texel;
    if (q == 0u) {
        g_lsm_taps++;
        return texture(u_lsm, vec4(dir, layer), lsm_depth(d - bias));
    }
    float vn = dot(v, n);
    float sg = dir.z >= 0.0 ? 1.0 : -1.0;
    float ka = -1.0 / (sg + dir.z), kb = dir.x * dir.y * ka;
    vec3 t1 = vec3(1.0 + sg * dir.x * dir.x * ka, sg * kb, -sg * dir.x);
    vec3 t2 = vec3(kb, sg + dir.y * dir.y * ka, -dir.y);
    // Полуоси тела в плоскости выборок (м, в базисе t1 t2): eu — вдоль трубки, ev — поперёк
    vec2 ax = vec2(dot(l.axis, t1), dot(l.axis, t2));
    float al = length(ax);
    vec2 eu = al > 1e-5 ? ax / al : vec2(1.0, 0.0);
    vec2 ev = vec2(-eu.y, eu.x) * l.size;
    float amax = l.size + al;
    eu *= amax;
    int nb = q >= 2u ? 8 : 4, nf = q >= 2u ? 12 : 6;
    // Масштаб тела в тангенсах: поиск — 1.5 / d, но не меньше двух текселей и не больше 0.25 по длинной полуоси
    float ks = clamp(1.5 / d, 2.0 / (LSM_N * amax), 0.25 / amax);
    float sum = 0.0;
    int cnt = 0;
    for (int i = 0; i < nb; ++i) {
        vec2 g = lsm_vogel(i, nb) * ks;
        vec2 o = eu * g.x + ev * g.y;
        vec3 w = dir + t1 * o.x + t2 * o.y;
        float dr = lsm_recv(w, vn, n, d) - bias;
        float db = (1.0 - textureLod(u_lsm_d, vec4(w, layer), 0.0).r) * LSM_RANGE;
        if (db < dr) {
            sum += db;
            cnt++;
        }
    }
    g_lsm_taps += uint(nb);
    if (cnt == 0) return 1.0;
    float db = max(sum / float(cnt), 0.02);
    float kp = clamp((d - db) / (db * d), 1.0 / (LSM_N * amax), ks);
    // Заслонки по всему телу поиска, а полутень меньше его половины — точка в тени целиком
    if (cnt == nb && kp < 0.5 * ks) return 0.0;
    float vis = 0.0;
    for (int i = 0; i < nf; ++i) {
        vec2 g = lsm_vogel(i, nf) * kp;
        vec2 o = eu * g.x + ev * g.y;
        vec3 w = dir + t1 * o.x + t2 * o.y;
        vis += texture(u_lsm, vec4(w, layer), lsm_depth(lsm_recv(w, vn, n, d) - bias));
    }
    g_lsm_taps += uint(nf);
    return vis / float(nf);
}

// Освещённость точки (p, n) огнём l с тенью, лк (rgb): для зондов и глаза — без отражения; skip — порог счёта, лк
vec3 lamp_e(Light l, vec3 p, vec3 n, uint steps, float skip) {
    vec3 q = lamp_closest(l, p);
    vec3 dl = q - p;
    float d = length(dl);
    if (d >= l.radius || d < 1e-4) return vec3(0.0);
    float w = lamp_window(d, l.radius) * lamp_face(l, -dl / d);
    float e = lamp_irr(l, p, n) * w;
    vec3 c = l.color * e;
    float em = max(c.r, max(c.g, c.b));
    float k = lamp_skip_k(em, skip);
    if (k <= 0.0) return vec3(0.0);
    uint ms = lamp_map_slot(l);
    if (ms != 0u) return c * (k * lamp_map_vis(l, ms, p, n, 0u));
    float soft;
    uint st = lamp_lod_steps(em, steps, soft);
    return c * (k * lamp_shadow(p, n, l, q, st, soft));
}

#endif
