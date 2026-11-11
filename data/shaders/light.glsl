// Свет (docs/light.md; шов 6 docs/invariants.md): материал отдаёт Surface, здесь она освещается — раз на ячейку
// правила пиксельности (light_cells.comp). Единицы физические: освещённость светил в люксах, яркость — кд/м²;
// экспозицию и тонмаппинг делает стиль (post.glsl), HDR кадра уже умножен на экспозицию (light_cells.comp).
// Этап 6а: отражение GGX с поправкой многократного рассеяния, металл, рассеянное Ламберта; главный свет
// (солнце, ночью луна) с тенью земли по карте горизонта (sun_visibility, дальше — карта теней 6б); свет неба
// по нормали (atmo_ambient) и отражение неба (atmo_sky) по шероховатости, оба — с видимостью неба по полю
// расстояний мира (6в, sdf_sky); дымка atmo_fog() (atmosphere.glsl)
#include "horizon.glsl"
#include "shadow.glsl"
#include "sdf.glsl"
#include "atmosphere.glsl"

// Видимость главного светила: 1 — свет, 0 — тень, всегда мягко (docs/light.md, правило 5). Земля — карта
// горизонта (docs/horizon.md), сетки — карта теней страницами (docs/shadow.md, 6б-1)
float sun_visibility(Surface s, vec3 l) {
    float v = h_sun_shadow(s.pos, l);
    return v > 0.0 ? v * sm_visibility(s.pos, s.normal) : 0.0;
}

// GGX (Trowbridge–Reitz), a2 = α²
float brdf_d(float nh, float a2) {
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (A_PI * d * d);
}
// Видимость Smith с корреляцией высот (Heitz 2014): G / (4·nl·nv)
float brdf_v(float nv, float nl, float a2) {
    float gv = nl * sqrt(nv * nv * (1.0 - a2) + a2);
    float gl = nv * sqrt(nl * nl * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-6);
}
vec3 brdf_f(float vh, vec3 f0) { return f0 + (1.0 - f0) * pow(1.0 - vh, 5.0); }

// Интеграл одного рассеяния GGX по полусфере (Karis 2014, приближение Lazarov): x — при F0, y — при F90.
// x + y = Ess — доля энергии, которую отражение отдаёт за один отскок; недостающее добирает многократное
vec2 brdf_dfg(float nv, float rough) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

// Затенение отражения по ao (Lagarde 2014): в щели отражение гаснет быстрее рассеянного у гладкого
float spec_occlusion(float nv, float ao, float a) {
    return clamp(pow(nv + ao, exp2(-16.0 * a - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

vec3 light_surface(Surface s) {
    FrameData f = pc.frame;
    vec3 l = normalize(f.sun_dir.xyz);
    float dist = length(s.pos);
    vec3 v = dist > 1e-4 ? -s.pos / dist : vec3(0.0, 1.0, 0.0);
    vec3 n = s.normal;
    float nv = max(dot(n, v), 1e-3);
    float nl = max(dot(n, l), 0.0);
    float rough = clamp(s.rough, 0.02, 1.0);
    float a = rough * rough;
    float a2 = a * a;
    vec3 diff = s.albedo * (1.0 - s.metal);
    vec3 f0 = mix(vec3(0.04), s.albedo, s.metal);

    // Многократное рассеяние (Fdez-Agüera 2019): отражение за один отскок FssEss, добавка Fms·Ems; рассеянному
    // остаётся то, что отражение не забрало. Прямой свет — поправкой Filament 1 + F0·(1/Ess − 1)
    vec2 dfg = brdf_dfg(nv, rough);
    float ess = dfg.x + dfg.y;
    vec3 fss_ess = f0 * dfg.x + dfg.y;
    float ems = 1.0 - ess;
    vec3 favg = f0 + (1.0 - f0) / 21.0;
    vec3 fms = fss_ess * favg / (1.0 - ems * favg);
    vec3 kd = diff * max(1.0 - fss_ess - fms * ems, 0.0);
    vec3 comp = 1.0 + f0 * (1.0 / max(ess, 1e-3) - 1.0);

    // Главный свет: освещённость E (лк) на перпендикулярную площадку у камеры
    vec3 c = vec3(0.0);
    float vis = nl > 0.0 ? sun_visibility(s, l) : 0.0;
    if (vis > 0.0) {
        vec3 h = normalize(l + v);
        float nh = max(dot(n, h), 0.0), vh = max(dot(v, h), 0.0);
        // Диск солнца (0.53°) не точка: лепесток шире на квадрат его полурадиуса — у глади блик не иглой
        float a2s = min(a2 + f.a_sun_t.w * f.a_sun_t.w * 0.25, 1.0);
        vec3 spec = brdf_d(nh, a2s) * brdf_v(nv, nl, a2s) * brdf_f(vh, f0) * comp;
        c += (kd / A_PI + spec) * f.sun_color.rgb * (nl * vis);
    }

    // Небо: рассеянное — освещённость/π по нормали; отражение — небо по направлению отражения, у шероховатого
    // переходит в свет неба по главному направлению лепестка (Lagarde 2014). Под горизонт отражение не уходит
    vec3 irr = atmo_ambient(n);
    vec3 r = reflect(-v, n);
    vec3 rd = normalize(mix(n, r, (1.0 - a) * (sqrt(1.0 - a) + a)));
    float blur = smoothstep(0.1, 0.75, rough);
    vec3 env = atmo_ambient(rd);
    // Таблица неба — только у гладкого и со ступени «Средне» (на «Низко» отражение — из гармоник неба)
    if (blur < 1.0 && f.light_q >= LIGHT_Q_MEDIUM) env = mix(atmo_sky(vec3(rd.x, max(rd.y, 0.0), rd.z)), env, blur);
    // Видимость неба по полю расстояний мира (6в, docs/sdf.md): здание и камни закрывают небо — интерьер темнеет
    // (свет, отражённый от стен, — зонды 6г). Свет неба и его отражение гаснут вместе с ao материала
    float ao = s.ao * sdf_sky(s.pos, n);
    float so = spec_occlusion(nv, ao, a);
    c += kd * irr * ao + (fss_ess * env + fms * ems * irr) * so;
    c += s.emissive;
    return atmo_fog(c, s.pos);
}
