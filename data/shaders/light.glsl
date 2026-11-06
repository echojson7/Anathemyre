// Свет — пользователя (docs/invariants.md, шов 6; docs/roadmap.md, этап 6).
// ВРЕМЕННЫЙ свет этапов 1–2, чтобы было видно сетки и рельеф: солнце по Ламберту с тенью рельефа (луч по сетке
// высот, terrain.glsl), небо одним цветом и дымка к цвету неба по расстоянию. Теней от объектов нет.
// Пользователь пишет свой алгоритм на месте этой функции; каркас зовёт только light_surface()
#include "terrain.glsl"

vec3 light_surface(Surface s) {
    FrameData f = pc.frame;
    vec3 l = normalize(f.sun_dir.xyz);
    float ndl = max(dot(s.normal, l), 0.0);
    float shadow = ndl > 0.0 ? t_shadow(s.pos, l) : 0.0;
    vec3 sky = f.sky_color.rgb * (0.6 + 0.4 * s.normal.y);
    vec3 c = s.albedo * (f.sun_color.rgb * ndl * shadow + sky * s.ao) + s.emissive;
    float fog = 1.0 - exp(-length(s.pos) * f.fog.w);
    return mix(c, f.fog.rgb, fog);
}
