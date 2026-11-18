// Воздух (шаг 5б, docs/air.md): пыль у земли и в залах. Клетки — плитки AIR_TILE пикселей × AIR_NZ слоёв по
// расстоянию вдоль луча до AIR_FAR; слой z (доля 0..1) — на t = AIR_A·((1 + AIR_FAR/AIR_A)^z − 1): у камеры слои
// тоньше. Центр текселя k картинок воздуха — на t((k + 0.5)/AIR_NZ), так что выборка по z = air_z(t) берёт
// значение на расстоянии t. Здесь — общее для клеток, суммы, лучей солнца и сложения с кадром: слои, луч клетки,
// плотность пыли, фаза. Картинки 16–19 — только compute (их объявляет каждый шейдер сам)
#ifndef AIR_GLSL
#define AIR_GLSL
#include "atmosphere.glsl"
#include "shadow.glsl"
#include "terrain.glsl"

const uint AIR_TILE = 16u, AIR_NZ = 64u;     // как в vk_state.h
const float AIR_FAR = 160.0, AIR_A = 1.0;    // м
// Пикселей кадра на тексель страницы теней у лучей солнца: уровень вдвое-вчетверо крупнее, чем у поверхности
// (там — пиксель на тексель). Луч в пыли — сумма по десяткам метров, тексель мельче не виден, а страниц впятеро
// меньше
const float AIR_SM_FOOT = 4.0;

// Расстояние вдоль луча по доле слоёв z и обратно
float air_t(float z) { return AIR_A * (exp(z * pc.frame.air_lnk) - 1.0); }
float air_z(float t) { return log(1.0 + max(t, 0.0) / AIR_A) / pc.frame.air_lnk; }

// Луч через точку кадра q (пиксели, дробные) без дрожания TAA: единичный, от камеры. Клетки и лучи солнца стоят
// на месте, пока стоит камера: дрожание кадра их не трясёт
vec3 air_ray(vec2 q) {
    FrameData f = pc.frame;
    vec2 ndc = q * f.ndc_per_pixel - 1.0;
    return normalize(f.forward.xyz + f.right.xyz * (ndc.x / f.proj_x) - f.up.xyz * (ndc.y / f.proj_y));
}

// Пыль у земли, 1/м: air_dust у самой земли, вверх — экспонентой с высотой air_dust_h. Высота — над сеткой 8 м
// (без деталей): пыль лежит в низинах и на плато одинаково. Под землёй (Исподь) — как у земли
float air_ground(vec3 rel) {
    FrameData f = pc.frame;
    if (f.air_dust <= 0.0) return 0.0;
    vec3 p = f.cam_off + rel;
    float h = p.y;
    if (f.t_on != 0u) h -= t_base_height(p.xz + vec2((f.cam_chunk - ivec2(f.t_chunk0)) * 256));
    return f.air_dust * exp(-max(h, 0.0) / f.air_dust_h);
}

// Пыль коробок заготовок (air в data/prefabs, docs/text.md): плотность внутри, мягкий край — внутрь коробки (за
// её гранью пыли нет: коробка стоит в стенах зала, и свет в пыли не выходит сквозь стену). Коробки одного зала
// могут заходить друг в друга (зал, тоннель, лестница): берётся самая густая, а не сумма
float air_boxes(vec3 rel) {
    FrameData f = pc.frame;
    float s = 0.0;
    for (uint i = 0u; i < f.air_boxes_n; ++i) {
        AirBox b = f.air_boxes.v[i];
        vec3 d = rel - b.c;
        vec3 q = abs(vec3(d.x * b.u.x + d.z * b.u.y, d.y, d.z * b.u.x - d.x * b.u.y)) - b.h;
        float e = -max(q.x, max(q.y, q.z));  // до ближней грани изнутри, м
        if (e > 0.0) s = max(s, b.dust * smoothstep(0.0, b.soft, e));
    }
    return s;
}

float air_density(vec3 rel) { return air_ground(rel) + air_boxes(rel); }

// Фаза: Корнетт–Шенкс с вытянутостью air_g и доля air_iso равномерно во все стороны (пыль крупная: вперёд светит
// сильно, но и вбок заметно). c — косинус между направлением к свету и направлением луча взгляда
float air_phase(float c) {
    FrameData f = pc.frame;
    return (1.0 - f.air_iso) * phase_mie(c, f.air_g) + f.air_iso * (0.25 / A_PI);
}

// Уровень страниц теней для точки воздуха: как sm_level, но тексель — AIR_SM_FOOT пикселей на этой глубине
int sm_level_air(float depth, float far) {
    FrameData f = pc.frame;
    float t0 = f.sm_t0;
    float foot = max(depth, 0.0) * f.pixel_angle * AIR_SM_FOOT;
    int kd = int(ceil(log2(max(foot / t0, 1.0))));
    int kw = int(ceil(log2(max(far / (SM_REACH * t0), 1.0))));
    return min(max(kd, kw), SM_LEVELS);
}

// Свет в пыли кадра (rgb, × экспозиция) — половинками в uvec2
uvec2 air_pack(vec3 c) { return uvec2(packHalf2x16(c.rg), packHalf2x16(vec2(c.b, 0.0))); }
vec3 air_unpack(uvec2 v) { return vec3(unpackHalf2x16(v.x), unpackHalf2x16(v.y).x); }
// Вес соседа при размытии и сложении: по расстоянию вдоль луча (до AIR_FAR — дальше всё одно)
float air_depth_w(float d, float dc) {
    float a = min(d, AIR_FAR), b = min(dc, AIR_FAR);
    float k = (a - b) / (0.04 * b + 0.05);
    return exp(-k * k);
}
#endif
