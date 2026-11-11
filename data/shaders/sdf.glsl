// Поле расстояний мира для света (docs/sdf.md, vk_sdf.cpp): выборка колец и видимость неба конусами.
// Видимость неба — мягкое число (правило 5 docs/light.md): конусы из точки по полусфере вокруг нормали идут по
// полю, у каждого — наименьшее «расстояние до заслонки / ширина конуса». Узор конусов один во всех кадрах,
// зависит только от нормали; уровень выборки — от ширины конуса (не от камеры), а камера только выбирает, какое
// окно точку накрывает, с плавным переходом у края окна. Стоящая камера — значение ячейки не меняется вовсе
#ifndef SDF_GLSL
#define SDF_GLSL
#include "common.glsl"
layout(set = 0, binding = 2) uniform sampler3D u_sdf;
uint g_sdf_taps = 0u;  // выборки поля этого потока (STAT_SDF_TAPS)

const float SDF_FREE = 1e4;          // точка вне колец: заслонок нет
const float SDF_FADE0 = 40.0, SDF_FADE1 = 48.0;  // переход на следующий уровень: вокселей от камеры
const float SDF_FAR0 = 120.0, SDF_FAR1 = 150.0;  // видимость неба гаснет к 1 дальше от камеры, м

// Расстояние в точке pa (м от якоря) по уровню L: тороидальное место, слой уровня по z
float sdf_level(vec3 pa, int L) {
    float v = pc.frame.sdf_cam.w * float(1 << L);
    vec3 u = pa / v;
    float z = float(L * SDF_SLAB + 1) + mod(u.z, float(SDF_N));
    g_sdf_taps++;
    return textureLod(u_sdf, vec3(u.xy / float(SDF_N), z / float(SDF_LEVELS * SDF_SLAB)), 0.0).r;
}

// Расстояние в точке не мельче уровня L. Окно уровня Lc накрывает точку с запасом, пока она ближе SDF_FADE1
// вокселей к камере; от SDF_FADE0 значение плавно переходит к уровню Lc + 1 (камера едет — значение течёт)
float sdf_dist(vec3 pa, int L) {
    FrameData f = pc.frame;
    vec3 dc = abs(pa - f.sdf_cam.xyz);
    float q = max(dc.x, max(dc.y, dc.z)) / f.sdf_cam.w;
    int lc = 0;
    while (lc < SDF_LEVELS && q > SDF_FADE1 * float(1 << lc)) ++lc;
    if (lc >= SDF_LEVELS) return SDF_FREE;
    int l0 = max(L, lc);
    float d = sdf_level(pa, l0);
    float w = l0 == lc && lc + 1 < SDF_LEVELS ? smoothstep(SDF_FADE0, SDF_FADE1, q / float(1 << lc)) : 0.0;
    if (w > 0.0) d = mix(d, sdf_level(pa, lc + 1), w);
    return d;
}

// Видимость по конусу из o по dir (тангенс полуугла th) до tmax: наименьшее d / ширина конуса, шаг — не меньше
// половины ширины. Уровень выборки — тот, чья полоса не уже конуса (у широкого конуса — грубее, как мип)
float sdf_cone(vec3 o, vec3 dir, float th, float t0, float tmax, uint steps) {
    float v0 = pc.frame.sdf_cam.w;
    float vis = 1.0, t = t0;
    for (uint i = 0u; i < steps; ++i) {
        float r = max(t * th, 0.5 * v0);
        int L = clamp(int(ceil(log2(r / (SDF_BAND * v0)))), 0, SDF_LEVELS - 1);
        float d = sdf_dist(o + dir * t, L);
        vis = min(vis, clamp(d / r, 0.0, 1.0));
        t += max(d, 0.5 * r);
        if (vis < 0.02 || t > tmax) break;
    }
    return vis * vis * (3.0 - 2.0 * vis);
}

// Видимость неба точки pos (м от камеры) с нормалью n, 0..1. Конусы: один по нормали и кольцо вокруг под углом
// SDF_RING к нормали, вес — косинус к нормали. Ступени «Качества света» — число конусов, их ширина, шаги, дальность
float sdf_sky(vec3 pos, vec3 n) {
    FrameData f = pc.frame;
    if (f.sdf_on == 0u) return 1.0;
    float far = smoothstep(SDF_FAR0, SDF_FAR1, length(pos));
    if (far >= 1.0) return 1.0;
    vec3 pa = pos + f.sdf_cam.xyz;
    // Старт — над поверхностью на полвокселя того уровня, что у точки (непрерывно по расстоянию до камеры)
    vec3 dc = abs(pa - f.sdf_cam.xyz);
    float vs = f.sdf_cam.w * max(1.0, max(dc.x, max(dc.y, dc.z)) / f.sdf_cam.w / SDF_FADE0);
    vec3 o = pa + n * (0.5 * vs);
    uint q = f.light_q;
    int ring = q == LIGHT_Q_LOW ? 3 : q == LIGHT_Q_MEDIUM ? 4 : q == LIGHT_Q_HIGH ? 5 : 8;
    float th = q == LIGHT_Q_LOW ? 0.70 : q == LIGHT_Q_MEDIUM ? 0.62 : q == LIGHT_Q_HIGH ? 0.58 : 0.45;
    uint steps = q == LIGHT_Q_LOW ? 8u : q == LIGHT_Q_MEDIUM ? 10u : q == LIGHT_Q_HIGH ? 12u : 14u;
    float tmax = q == LIGHT_Q_LOW ? 16.0 : q == LIGHT_Q_MEDIUM ? 20.0 : q == LIGHT_Q_HIGH ? 24.0 : 32.0;
    float ca = q == LIGHT_Q_ULTRA ? 0.616 : 0.574;  // косинус угла кольца к нормали: 52° / 55°
    float sa = sqrt(1.0 - ca * ca);
    // Базис у нормали без ветвлений (Duff и др. 2017): зависит только от n
    float sg = n.z >= 0.0 ? 1.0 : -1.0;
    float ka = -1.0 / (sg + n.z), kb = n.x * n.y * ka;
    vec3 t1 = vec3(1.0 + sg * n.x * n.x * ka, sg * kb, -sg * n.x);
    vec3 t2 = vec3(kb, sg + n.y * n.y * ka, -n.y);
    float sum = sdf_cone(o, n, th, vs, tmax, steps);
    float wsum = 1.0;
    for (int k = 0; k < ring; ++k) {
        float ph = 6.2831853 * float(k) / float(ring);
        vec3 d = ca * n + sa * (cos(ph) * t1 + sin(ph) * t2);
        sum += ca * sdf_cone(o, d, th, vs, tmax, steps);
        wsum += ca;
    }
    return mix(sum / wsum, 1.0, far);
}

// F3 «поле»: расстояние у точки (на полвокселя над поверхностью) изолиниями через 25 см, синее — у заслонки,
// оранжевое — дальше полосы; яркость — как серая карта под небом
vec3 sdf_field_view(vec3 pos, vec3 n, float lux) {
    vec3 pa = pos + pc.frame.sdf_cam.xyz + n * (0.5 * pc.frame.sdf_cam.w);
    float d = sdf_dist(pa, 0);
    vec3 c = mix(vec3(0.1, 0.35, 1.0), vec3(1.0, 0.55, 0.1), clamp(d, 0.0, 1.0));
    if (d < 0.0) c = vec3(1.0, 0.1, 0.1);
    c *= 0.65 + 0.35 * step(0.5, fract(d * 4.0));
    return c * (0.5 / 3.14159265 * lux);
}
#endif
