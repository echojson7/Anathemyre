// Луч непрямого света по полю расстояний и карте высот земли (docs/gi.md): общий для зондов (gi_trace.comp) и
// ячеек света (cells_trace.comp, docs/cells.md). Попал — яркость точки: цвет × (солнце с тенью по полю и карте
// горизонта + свет зондов прошлого смешения в точке: так копятся отскоки) + огни ламп с тенью по полю; не попал —
// небо
#ifndef GI_RAY_GLSL
#define GI_RAY_GLSL
#include "gi.glsl"
#include "horizon.glsl"
#include "atmosphere.glsl"
#include "lamps.glsl"

uint g_lamp_steps;  // шагов конуса тени огней (меньше, чем у кадра: точек много, свет зонда — среднее)

// Видимость светила из точки попадания: карта горизонта земли и узкий конус по полю к светилу
float gi_sun_vis(vec3 pa, vec3 n, vec3 l, float vox) {
    FrameData f = pc.frame;
    float v = h_sun_shadow(pa - f.sdf_cam.xyz, l);
    if (v <= 0.0) return 0.0;
    v *= sdf_cone(pa + n * vox, l, 0.03, vox, GI_TMAX, 24u).x;
    return v;
}

// Яркость луча из o по d: rgb, w — расстояние до попадания (−1 — небо). back — попал в обратную сторону грани
vec4 gi_ray(vec3 o, vec3 d, out bool back) {
    FrameData f = pc.frame;
    back = false;
    float v0 = f.sdf_cam.w;
    float t = 0.5 * v0;
    bool hit = false, gnd = false;
    bool soil = gi_under(o);  // луч из-под земли: земля — пласт (gi_ground_dist)
    int L = 0;
    for (uint i = 0u; i < GI_STEPS; ++i) {
        vec3 p = o + d * t;
        float r = max(t * GI_SPREAD, 0.5 * v0);
        L = clamp(int(ceil(log2(r / (SDF_BAND * v0)))), 0, SDF_LEVELS - 1);
        float ds = sdf_dist(p, L);
        float dg = gi_ground_dist(p, soil);
        float dd = min(ds, dg);
        if (dd < 0.25 * r) {
            hit = true;
            gnd = dg <= ds;
            break;
        }
        t += max(dd, 0.5 * r);
        if (t > GI_TMAX) break;
        if (i + 1u == GI_STEPS) {
            hit = true;  // шаги кончились у заслонок (луч вдоль стены): тёмное, а не небо
            gnd = dg <= ds;
        }
    }
    vec3 p = o + d * t;
    if (!hit && soil && gi_under(p)) {
        back = true;  // ушёл в землю под пластом: зонд в земле, а не в постройке
        return vec4(0.0, 0.0, 0.0, t);
    }
    if (!hit) return vec4(atmo_sky(vec3(d.x, max(d.y, 0.0), d.z)), -1.0);
    float vox = v0 * float(1 << L);
    vec3 n;
    vec3 alb;
    if (gnd) {
        float e = 2.0;
        float hx = gi_ground_h(p + vec3(e, 0, 0)) - gi_ground_h(p - vec3(e, 0, 0));
        float hz = gi_ground_h(p + vec3(0, 0, e)) - gi_ground_h(p - vec3(0, 0, e));
        n = normalize(vec3(-hx, 2.0 * e, -hz));
        alb = gi_ground_albedo(p);
    } else {
        vec3 gr = gi_grad(p, L, soil);
        float gl = length(gr);
        n = gl > 1e-6 ? gr / gl : -d;
        alb = GI_WALL_ALBEDO;
    }
    if (dot(n, d) > 0.0) {
        back = true;
        return vec4(0.0, 0.0, 0.0, t);
    }
    vec3 l = normalize(f.sun_dir.xyz);
    float nl = dot(n, l);
    vec3 e_sun = vec3(0.0);
    if (nl > 0.0 && dot(f.sun_color.rgb, vec3(1.0)) > 0.0) e_sun = f.sun_color.rgb * (nl * gi_sun_vis(p, n, l, vox));
    // Свет зондов в точке (прошлое смешение): второй и следующие отскоки. Вне окон зондов — свет неба без
    // заслонки. Внутри окон, где зондов ещё нет (первый проход снимка, сброс) или все они в стене, — темно, а не
    // небо: иначе первый проход заливал закрытые залы светом открытого неба, и он гас десятки секунд
    vec3 ph = p - f.sdf_cam.xyz;
    GiSample g = gi_sample(ph, n, n, false);
    vec3 amb = g.irr * g.cov;
    if (g.cov < 1.0) amb += atmo_ambient(n) * ((1.0 - g.cov) * (1.0 - gi_cover(GI_LEVELS - 1, ph)) * (1.0 - gi_buried(p)));
    // Огни: освещённость с тенью по полю. Все огни кадра по порядку (дальние отсекает дальность). Список огней
    // зонда в общей памяти lavapipe не держит (падает на лучах), а огней кадра у камеры — десятки
    vec3 e_lamp = vec3(0.0);
    for (uint k = 0u; k < f.light_count; ++k) e_lamp += lamp_e(f.lights.v[k], p - f.sdf_cam.xyz, n, g_lamp_steps);
    return vec4(alb * ((e_sun + e_lamp) / A_PI + amb), t);
}

#endif
