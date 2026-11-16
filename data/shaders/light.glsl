// Свет (docs/light.md; шов 6 docs/invariants.md): материал отдаёт Surface, здесь она освещается — раз на ячейку
// правила пиксельности (light_cells.comp). Единицы физические: освещённость светил в люксах, яркость — кд/м²;
// экспозицию и тонмаппинг делает стиль (post.glsl), HDR кадра уже умножен на экспозицию (light_cells.comp).
// Этап 6а: отражение GGX с поправкой многократного рассеяния, металл, рассеянное Ламберта; главный свет
// (солнце, ночью луна) с тенью земли по карте горизонта (sun_visibility, дальше — карта теней 6б); свет неба
// по нормали и отражение неба по шероховатости — из зондов непрямого света (6г, gi.glsl: небо и отражённое
// стенами и землёй) с ближним затенением по полю (sdf_ao); вне окон зондов — небо (atmo_ambient, atmo_sky) с
// видимостью по полю расстояний мира (6в, sdf_sky); огни ламп с тенью по полю (6е, lamps.glsl, docs/lamps.md);
// дымка atmo_fog() (atmosphere.glsl)
#include "horizon.glsl"
#include "shadow.glsl"
#include "gi.glsl"
#include "lamps.glsl"
#include "atmosphere.glsl"
#include "lcache.glsl"

// 1 — освещается поверхность самой земли (ставит light_cells.comp до light_surface): под грунтом она не бывает,
// какой бы ни была грубая высота земли у зондов (gi_buried)
float g_ground = 0.0;

// Свет места для местной экспозиции (docs/light.md, «Местная экспозиция»; light_cells.comp): освещённость, лк,
// какую намерил бы экспонометр глаза, встань он в этой точке, — непрямой свет по нормали, светило в доле
// открытости небу и огни без тени. Тени светила и огней в него не входят: поле гладкое, поэтому свет и тень
// одного места сдвигаются вместе и контраст между ними цел. Копит light_surface() своими частями, читать после неё
float g_e_local = 0.0;
// Свет места у пикселя неба и у открытой земли — один и тот же: небо и светило на открытую землю.
// Один на всё небо: градиент неба, облака и диск светила между собой не сжимаются
float local_sky_e() { return pc.frame.sky_out.v[SKY_OUT_EYE].y; }

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

// Огни клетки на поверхность s: рассеянное kd/π и отражение GGX (как у солнца в light.glsl). v — к глазу,
// a, a2 — α и α², f0, comp — Френель и поправка многократного рассеяния
vec3 light_lamps(Surface s, vec3 n, vec3 v, float nv, float a, float a2, vec3 kd, vec3 f0, vec3 comp) {
    uint fi = froxel_at(s.pos);
    uint cnt = froxel_light_count(fi);
    if (cnt == 0u) return vec3(0.0);
    FrameData f = pc.frame;
    uint steps = lamp_steps(f.light_q);
    vec3 r = reflect(-v, n);
    vec3 c = vec3(0.0);
    for (uint k = 0u; k < cnt; ++k) {
        Light l = f.lights.v[froxel_light(fi, k)];
        vec3 q = lamp_closest(l, s.pos);
        vec3 dl = q - s.pos;
        float d = length(dl);
        if (d >= l.radius || d < 1e-4) continue;
        float w = lamp_window(d, l.radius) * lamp_face(l, -dl / d);
        float e = lamp_irr(l, s.pos, n) * w;
        if (e * max(l.color.r, max(l.color.g, l.color.b)) < LAMP_E_SKIP) continue;
        // Отражение: точка отрезка, ближайшая к лучу отражения, потом точка шара тела у луча (Karis 2013)
        vec3 L0 = l.c - l.axis - s.pos, Ld = 2.0 * l.axis;
        vec3 lc = L0;
        float ll = dot(Ld, Ld);
        if (ll > 1e-8) {
            float rl = dot(r, Ld);
            float t = (dot(r, L0) * rl - dot(L0, Ld)) / max(ll - rl * rl, 1e-6);
            lc = L0 + Ld * clamp(t, 0.0, 1.0);
        }
        vec3 cr = dot(lc, r) * r - lc;
        float crl = length(cr);
        vec3 ls = lc + cr * clamp(l.size / max(crl, 1e-6), 0.0, 1.0);
        float ds = length(ls);
        vec3 lv = ls / ds;
        float nls = dot(n, lv);
        vec3 spec = vec3(0.0);
        if (nls > 0.0) {
            vec3 h = normalize(lv + v);
            float nh = max(dot(n, h), 0.0), vh = max(dot(v, h), 0.0);
            // Поправка энергии: шар (α/α')², отрезок α/α'
            float as = clamp(a + l.size / (2.0 * ds), 0.0, 1.0);
            float al = clamp(a + sqrt(ll) / (4.0 * ds), 0.0, 1.0);
            float norm = (a / as) * (a / as) * (ll > 1e-8 ? a / al : 1.0);
            // Освещённость в сторону представительной точки: вся сила трубки, растянутая нормой по лепестку
            float es = nls / (ds * ds + l.size * l.size) * w;
            spec = brdf_d(nh, a2) * brdf_v(nv, nls, a2) * brdf_f(vh, f0) * comp * (norm * es);
        }
        float sh = lamp_shadow(s.pos, n, l, q, steps);
        c += (kd * (e / A_PI) + spec) * l.color * sh;
        g_e_local += e * dot(l.color, vec3(0.2126, 0.7152, 0.0722));
    }
    return c;
}
// Непрямой свет (6г, docs/gi.md): рассеянное и отражение от неба и от того, что отразили стены и земля.
// irr, env — небо без заслонки по нормали и в сторону отражения; kd — доля рассеянного, fss_ess, fms_ems —
// отражение за один отскок и добавка многократного (Fdez-Agüera 2019).
// Зонды: E/π по нормали — уже с заслонкой и отскоками, ближнее затенение (sdf_ao) — углы мельче шага зондов.
// Отражение у шероховатого — свет зондов в его сторону, у гладкого — небо в долю, в какой зонды видят его туда,
// и отражённое стенами за остальное. Вне окон зондов (и при выключенных) — запасной путь 6в: небо × видимость
// неба конусами по полю (sdf_sky), без отражённого. Оба затеняются ao материала и по Lagarde для отражения
// lit — освещённость воздуха у точки против открытого неба (рассеянный свет к небу по нормали): дымке отрезка
vec3 light_indirect(Surface s, vec3 n, float nv, float a, vec3 rd, float blur, vec3 irr, vec3 env, vec3 kd,
                    vec3 fss_ess, vec3 fms_ems, out float lit) {
    GiSample g = gi_sample(s.pos, n, rd, true);
    vec3 c = vec3(0.0);
    const vec3 Y = vec3(0.2126, 0.7152, 0.0722);
    float sky_y = max(dot(irr, Y), 1e-6);
    lit = 0.0;
    if (g.cov < 1.0) {
        // Под грунтом неба нет, что бы ни видели конусы (gi_buried)
        float vis = sdf_sky(s.pos, n).x;
        if (g_ground == 0.0) vis *= 1.0 - gi_buried(s.pos + pc.frame.sdf_cam.xyz);
        lit = vis * (1.0 - g.cov);
        g_e_local += A_PI * sky_y * lit;
        float ao = s.ao * vis;
        float so = spec_occlusion(nv, ao, a);
        c = (kd * irr * ao + (fss_ess * env + fms_ems * irr) * so) * (1.0 - g.cov);
    }
    if (g.cov > 0.0) {
        lit += clamp(dot(g.irr, Y) / sky_y, 0.0, 1.0) * g.cov;
        g_e_local += A_PI * max(dot(g.irr, Y), 0.0) * g.cov;
        float ao = s.ao * sdf_ao(s.pos, n);
        float so = spec_occlusion(nv, ao, a);
        vec3 bounce = max(g.refl.rgb - atmo_ambient(rd) * g.refl.a, vec3(0.0));
        vec3 env_g = mix(env * g.refl.a + bounce, g.refl.rgb, blur);
        c += (kd * g.irr * ao + (fss_ess * env_g + fms_ems * g.irr) * so) * g.cov;
    }
    return c;
}

// F3 «только непрямой» (docs/gi.md): серая карта 50 % под рассеянным непрямым светом, как его видит свет кадра
vec3 light_indirect_view(Surface s) {
    // F3 «ячейки» (docs/lcache.md): свет ближайшей ячейки главной стороны нормали, края ячеек темнее; где ячейки
    // нет (люди, дальше ячеек, не посчитана) — свет зондов, красноватый
    if (pc.frame.gi_view == GI_VIEW_CELLS) {
        bool ok;
        vec3 c = lc_view(s.pos, s.normal, g_ground != 0.0, ok);
        if (ok) return c;
    }
    GiSample g = gi_sample(s.pos, s.normal, s.normal, false);
    vec3 irr = atmo_ambient(s.normal);
    float bury = g_ground == 0.0 ? gi_buried(s.pos + pc.frame.sdf_cam.xyz) : 0.0;
    vec3 fb = irr * (s.ao * (g.cov < 1.0 ? sdf_sky(s.pos, s.normal).x * (1.0 - bury) : 1.0));
    vec3 gi = g.cov > 0.0 ? g.irr * (s.ao * sdf_ao(s.pos, s.normal)) : vec3(0.0);
    vec3 c = 0.5 * mix(fb, gi, g.cov);
    return pc.frame.gi_view == GI_VIEW_CELLS ? c * vec3(1.0, 0.45, 0.45) : c;
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
    g_e_local = 0.0;
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
    c += light_lamps(s, n, v, nv, a, a2, kd, f0, comp);
    float lit;
    c += light_indirect(s, n, nv, a, rd, blur, irr, env, kd, fss_ess, fms * ems, lit);
    c += s.emissive;
    // Воздух отрезка освещён как у точки вдали и как у глаза вблизи (глаз — eye.comp, sky_out [5].w)
    float lc = clamp(f.sky_out.v[SKY_OUT_EYE2].w, 0.0, 1.0);
    // Свет места: к непрямому свету и огням — светило на землю в доле открытости небу (lit: под открытым небом
    // весь, в зале — ничего). Вдали пиксель — уже не поверхность, а воздух перед ней: свет места сходит к небу
    g_e_local += f.sky_out.v[SKY_OUT_EYE2].x * smoothstep(0.25, 0.75, lit);
    float air = 1.0 - dot(exp(-atmo_depth(s.pos)), vec3(1.0 / 3.0));
    g_e_local = exp2(mix(log2(max(g_e_local, 1e-6)), log2(max(local_sky_e(), 1e-6)), air));
    return atmo_fog_lit(c, s.pos, mix(lit, lc, exp(-dist / 32.0)));
}
