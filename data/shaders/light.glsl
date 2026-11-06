// Свет — пользователя (docs/invariants.md, шов 6; docs/roadmap.md, этап 6).
// ВРЕМЕННЫЙ свет этапов 1–3, чтобы было видно сетки и рельеф: главный свет (солнце, ночью луна) по Ламберту
// с тенью рельефа (луч по сетке высот, terrain.glsl), свет неба по нормали (atmo_ambient, вместе с отсветом
// земли), блик и отражение неба по Френелю для гладкого (вода), дымка atmo_fog() (atmosphere.glsl, этап 3).
// Теней от объектов нет. Пользователь пишет свой алгоритм на месте этой функции; каркас зовёт только light_surface()
#include "terrain.glsl"
#include "atmosphere.glsl"

vec3 light_surface(Surface s) {
    FrameData f = pc.frame;
    vec3 l = normalize(f.sun_dir.xyz);
    float dist = length(s.pos);
    vec3 v = dist > 1e-4 ? -s.pos / dist : vec3(0.0, 1.0, 0.0);
    vec3 nrm = s.normal;
    float ndl = max(dot(nrm, l), 0.0);
    float shadow = ndl > 0.0 ? t_shadow(s.pos, l) : 0.0;
    vec3 amb = atmo_ambient(nrm) * s.ao;
    vec3 c = s.albedo * (f.sun_color.rgb * ndl * shadow + amb);
    // Блик и отражение неба: заметны только у гладкого
    float nv = max(dot(nrm, v), 1e-3);
    float fres = 0.02 + 0.98 * pow(1.0 - nv, 5.0);
    float gloss = 1.0 - smoothstep(0.1, 0.6, s.rough);
    if (gloss > 0.0) {
        vec3 r = reflect(-v, nrm);
        vec3 refl = atmo_sky(vec3(r.x, max(r.y, 0.0), r.z));
        float a = max(s.rough * s.rough, 0.002);
        vec3 hv = normalize(l + v);
        float nh = max(dot(nrm, hv), 0.0);
        float dd = nh * nh * (a * a - 1.0) + 1.0;
        float spec = a * a / (3.14159 * dd * dd) * 0.25;
        c = c * (1.0 - fres * gloss) + (refl * fres + f.sun_color.rgb * spec * fres * ndl * shadow) * gloss;
    }
    return atmo_fog(c, s.pos);
}
