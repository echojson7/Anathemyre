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
    float soft;
    uint st = lamp_lod_steps(em, steps, soft);
    return c * (k * lamp_shadow(p, n, l, q, st, soft));
}

#endif
