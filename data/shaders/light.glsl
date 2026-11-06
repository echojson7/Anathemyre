// Свет — пользователя (docs/invariants.md, шов 6; docs/roadmap.md, этап 6).
// ВРЕМЕННЫЙ свет этапов 1–2, чтобы было видно сетки и рельеф: солнце по Ламберту с тенью рельефа (луч по сетке
// высот, terrain.glsl), небо полусферой (сверху цвет неба, снизу тёплый отсвет земли), блик и отражение неба
// по Френелю для гладкого (вода), воздушная перспектива: дымка гуще в низинах, к солнцу теплее.
// Теней от объектов нет. Пользователь пишет свой алгоритм на месте этой функции; каркас зовёт только light_surface()
#include "terrain.glsl"

vec3 light_surface(Surface s) {
    FrameData f = pc.frame;
    vec3 l = normalize(f.sun_dir.xyz);
    float dist = length(s.pos);
    vec3 v = dist > 1e-4 ? -s.pos / dist : vec3(0.0, 1.0, 0.0);
    vec3 nrm = s.normal;
    float ndl = max(dot(nrm, l), 0.0);
    float shadow = ndl > 0.0 ? t_shadow(s.pos, l) : 0.0;
    vec3 sky = f.sky_color.rgb;
    vec3 bounce = sky * vec3(1.0, 0.85, 0.65) * 0.35;
    vec3 amb = mix(bounce, sky, 0.5 + 0.5 * nrm.y) * s.ao;
    vec3 c = s.albedo * (f.sun_color.rgb * ndl * shadow + amb);
    // Блик и отражение неба: заметны только у гладкого
    float nv = max(dot(nrm, v), 1e-3);
    float fres = 0.02 + 0.98 * pow(1.0 - nv, 5.0);
    float gloss = 1.0 - smoothstep(0.1, 0.6, s.rough);
    if (gloss > 0.0) {
        vec3 r = reflect(-v, nrm);
        vec3 horizon = f.fog.rgb;
        vec3 refl = mix(horizon, sky * 1.6, smoothstep(0.0, 0.5, r.y));
        float a = max(s.rough * s.rough, 0.002);
        vec3 hv = normalize(l + v);
        float nh = max(dot(nrm, hv), 0.0);
        float dd = nh * nh * (a * a - 1.0) + 1.0;
        float spec = a * a / (3.14159 * dd * dd) * 0.25;
        c = c * (1.0 - fres * gloss) + (refl * fres + f.sun_color.rgb * spec * fres * ndl * shadow) * gloss;
    }
    // Дымка: плотнее у дна котловины, к солнцу светлее и теплее
    float y = s.pos.y + f.cam_off.y;
    float dens = f.fog.w * (0.75 + 0.6 * exp(-max(y - 300.0, 0.0) / 300.0));
    float fog = 1.0 - exp(-dist * dens);
    float sunward = pow(max(dot(-v, l), 0.0), 6.0);
    vec3 fogc = mix(f.fog.rgb, f.fog.rgb * vec3(1.15, 1.02, 0.85), sunward);
    return mix(c, fogc, fog);
}
