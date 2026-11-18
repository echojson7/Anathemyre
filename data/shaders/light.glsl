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
#include "clouds.glsl"
#include "lcache.glsl"

// 1 — освещается поверхность самой земли (ставит light_cells.comp до light_surface): под грунтом она не бывает,
// какой бы ни была грубая высота земли у зондов (gi_buried)
float g_ground = 0.0;
// 1 — сетка движется сама (люди, меч): ячейки мира ей не подходят, отражённый свет — от зондов (ставит light_cells.comp)
float g_moving = 0.0;

// Свет места для местной экспозиции (docs/light.md, «Местная экспозиция»; light_cells.comp): освещённость, лк,
// какую намерил бы экспонометр глаза, встань он в этой точке, — непрямой свет по нормали, светило в доле
// открытости небу и огни без тени. Тени светила и огней в него не входят: поле гладкое, поэтому свет и тень
// одного места сдвигаются вместе и контраст между ними цел. Копит light_surface() своими частями, читать после неё
float g_e_local = 0.0;
// Цвет того же света (линейный sRGB, после light_surface — с Y = 1): к нему привыкает глаз (eye.comp, «Баланс белого»)
vec3 g_c_local = vec3(0.0);
// Свет места у пикселя неба и у открытой земли — один и тот же: небо и светило на открытую землю.
// Один на всё небо: градиент неба, облака и диск светила между собой не сжимаются
float local_sky_e() { return pc.frame.sky_out.v[SKY_OUT_EYE].y; }
// Цвет того же света (Y = 1): небо и светило на открытую землю (eye.comp)
vec3 local_sky_c() {
    vec3 c = pc.frame.sky_out.v[SKY_OUT_VIEW].rgb;
    return c / max(dot(c, vec3(0.2126, 0.7152, 0.0722)), 1e-12);
}

// Видимость главного светила: 1 — свет, 0 — тень, всегда мягко (docs/light.md, правило 5). Земля — карта
// горизонта (docs/horizon.md), сетки — карта теней страницами (docs/shadow.md, 6б-1), облака — их карта тени
// (docs/clouds.md)
float sun_visibility(Surface s, vec3 l) {
    float v = h_sun_shadow(s.pos, l);
    return v > 0.0 ? v * sm_visibility(s.pos, s.normal) * cloud_shadow(s.pos) : 0.0;
}

// Тень солнца своим проходом (sun_vis.comp, docs/shadow.md «Свой проход»): SUN_VIS_READ — освещение берёт число
// пикселя g_sun_px из sun_vis, а не считает его. Отладочное освещение (точка ячейки, виды F3) считает само
#ifndef SUN_VIS_READ
#define SUN_VIS_READ false
#endif
ivec2 g_sun_px = ivec2(0);
float sun_vis_of(Surface s, vec3 l) {
    if (!SUN_VIS_READ) return sun_visibility(s, l);
    uint tx = (uint(pc.frame.viewport.x) + 7u) / 8u;
    return pc.frame.sun_vis.v[(uint(g_sun_px.y >> 3) * tx + uint(g_sun_px.x >> 3)) * 64u + uint((g_sun_px.y & 7) * 8 + (g_sun_px.x & 7))];
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

// Цвет свечения стекла (docs/lamps.md, «Стекло»): светящаяся поверхность внутри тела огня (ближе size + 12 см к
// его отрезку) светит цветом этого огня — материал стекла один на все лампы, а лампы разных кельвинов и цветов.
// Ставит light_lamps: g_glow_d — расстояние до ближайшего такого огня (1e9 — нет), g_glow_c — его цвет
float g_glow_d = 1e9;
vec3 g_glow_c = vec3(1.0);
const float LAMP_GLOW_R = 0.12;  // м сверх радиуса тела
const float LAMP_PLACE_NEAR = 3.0, LAMP_PLACE_FAR = 8.0;  // м: огонь в свете места без тени → с тенью

// Огни клетки на поверхность s: рассеянное kd/π и отражение GGX (как у солнца в light.glsl). v — к глазу,
// a, a2 — α и α², f0, comp — Френель и поправка многократного рассеяния
vec3 light_lamps(Surface s, vec3 n, vec3 v, float nv, float a, float a2, vec3 kd, vec3 f0, vec3 comp) {
    g_glow_d = 1e9;
    uint fi = froxel_at(s.pos);
    uint cnt = froxel_light_count(fi);
    if (cnt == 0u) return vec3(0.0);
#ifdef LAMP_TILE_MASK
    if (s_lamp_any == 0u) return vec3(0.0);  // ни один огонь не достаёт до плитки (light_cells.comp)
#endif
    FrameData f = pc.frame;
    uint steps = lamp_steps(f.shadow_q);
    float skip = lamp_e_skip();
    vec3 r = reflect(-v, n);
    vec3 c = vec3(0.0);
    for (uint k = 0u; k < cnt; ++k) {
        uint li = froxel_light(fi, k);
#ifdef LAMP_TILE_MASK
        if ((s_lamp[li >> 5] & (1u << (li & 31u))) == 0u) continue;  // шар огня мимо точек плитки: d ≥ radius у всех
#endif
        Light l = f.lights.v[li];
        vec3 q = lamp_closest(l, s.pos);
        vec3 dl = q - s.pos;
        float d = length(dl);
        if (d < l.size + LAMP_GLOW_R && d < g_glow_d) {
            g_glow_d = d;
            g_glow_c = l.color;
        }
        if (d >= l.radius || d < 1e-4) continue;
        float w = lamp_window(d, l.radius) * lamp_face(l, -dl / d);
        float e = lamp_irr(l, s.pos, n) * w;
        float em = e * max(l.color.r, max(l.color.g, l.color.b));
        float ks = lamp_skip_k(em, skip);
        if (ks <= 0.0) continue;
        e *= ks;
        w *= ks;
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
        // Тень: картой огня, если у него есть место (docs/lamp_shadows.md), иначе конусом по полю. У огня с людьми,
        // машинами или оружием рядом — куб слоя подвижных: копия места с ними поверх, одна выборка на обе
        uint md = lamp_map_dyn(l);
        uint ms = md != 0u ? md : lamp_map_slot(l);
        float sh;
        if (ms != 0u) {
            sh = lamp_map_vis(l, ms, s.pos, n, lamp_map_q(em));
        } else {
            float soft;
            uint st = lamp_lod_steps(em, steps, soft);
            sh = lamp_shadow(s.pos, n, l, q, st, soft);
        }
        c += (kd * (e / A_PI) + spec) * l.color * sh;
        // Свет места: ближний огонь — без тени (свет и тень одного места сдвигаются вместе), дальний — с тенью:
        // его заслоняют стены, и лампы соседнего зала за стеной в свет места не идут (иначе в зале станции глаз
        // привыкал к 40 лк вместо 6 и кадр темнел втрое — после дальности ламп по глазу, разбор 18.11)
        float el = e * mix(1.0, sh, smoothstep(LAMP_PLACE_NEAR, LAMP_PLACE_FAR, d));
        g_e_local += el * dot(l.color, vec3(0.2126, 0.7152, 0.0722));
        g_c_local += el * l.color;
    }
    return c;
}
#ifndef LIGHT_GI_CACHED_ONLY
#define LIGHT_GI_CACHED_ONLY false
#endif
// Размытие отражения по шероховатости материала: 0 — гладкое (свет по лучу отражения), 1 — шероховатое (по нормали)
float light_blur(float rough) { return smoothstep(0.1, 0.75, clamp(rough, 0.02, 1.0)); }
#ifdef GCACHE_GLSL
// Отражённый свет пикселя — из кеша земли (после gc_surface_px): свет есть у всех текселей, поверхность шероховатая
bool light_gi_cached(float blur) { return g_gcl_ok && blur >= 1.0; }
#endif

// Свет берёт ячейки: не движущаяся сетка, не F3 «без ячеек» (сравнение) и не «ячейки» (там их показывает свой вид)
bool lc_use() { return g_moving == 0.0 && pc.frame.gi_view != GI_VIEW_PROBES && pc.frame.gi_view != GI_VIEW_CELLS; }

// Ближнее затенение по полю: у зондов — на 1 м (углы мельче их шага), у ячеек — на полторы стороны ячейки у точки
// и только разница с затенением у точек ячеек (ao_cell — сглаженное, как их свет): их лучи эти заслонки уже видят.
// Иначе у основания колонн и стен тень считалась дважды — тёмное кольцо (разбор 17.11). Светлее ячейки — нет:
// конусы тень переоценивают (заслонка для них чёрная), лучи ячейки видят и отражённое заслонкой
float lc_ao(vec3 pos, vec3 n, float cells, float ao_cell) {
    float v = lc_near_ao(pos, n, mix(1.0, lc_near_t(pos), cells));
    return min(v / mix(1.0, max(ao_cell, 0.05), cells), 1.0);
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
    // Земля из кеша (docs/gcache.md, «Отражённый свет в кеше»): зонды, ячейки, ближнее затенение и видимость неба —
    // смесь текселей. Только у шероховатого (blur = 1): там отражение берёт свет зондов почти по нормали, и его
    // заменяет тот же свет текселя; гладкому (вода, мокрое) нужен свет точно по лучу отражения — он на пиксель.
    // LIGHT_GI_CACHED_ONLY — у конвейера нет пути на пиксель (освещение земли в игре: такие плитки оно отдаёт
    // «земле на пиксель», docs/perf_arch.md, шаг 3)
    bool cached = false;
#ifdef GCACHE_GLSL
    cached = LIGHT_GI_CACHED_ONLY || light_gi_cached(blur);
#endif
    GiSample g;
    if (cached) {
#ifdef GCACHE_GLSL
        g.irr = g_gcl.irr;
        g.refl = vec4(g_gcl.irr, g_gcl.refl);
        g.cov = g_gcl.cov;
#endif
    } else {
        g = gi_sample(s.pos, n, rd, true);
    }
    vec3 c = vec3(0.0);
    const vec3 Y = vec3(0.2126, 0.7152, 0.0722);
    float sky_y = max(dot(irr, Y), 1e-6);
    lit = 0.0;
    if (g.cov < 1.0) {
        // Под грунтом неба нет, что бы ни видели конусы (gi_buried)
        float vis;
#ifdef GCACHE_GLSL
        if (cached) vis = g_gcl.vis;
        else
#endif
        {
            vis = sdf_sky(s.pos, n).x;
            if (g_ground == 0.0) vis *= 1.0 - gi_buried(s.pos + pc.frame.sdf_cam.xyz);
        }
        lit = vis * (1.0 - g.cov);
        g_e_local += A_PI * sky_y * lit;
        g_c_local += A_PI * irr * lit;
        float ao = s.ao * vis;
        float so = spec_occlusion(nv, ao, a);
        c = (kd * irr * ao + (fss_ess * env + fms_ems * irr) * so) * (1.0 - g.cov);
    }
    if (g.cov > 0.0) {
        // Ячейки света (docs/lcache.md): рассеянный отражённый свет на самой поверхности вместо смеси зондов; ближнее
        // затенение — только мельче ячейки (крупнее ячейка видит сама)
        float lao;
#ifdef GCACHE_GLSL
        if (cached) lao = g_gcl.ao;
        else
#endif
        {
            float ao_c = 1.0;
            vec4 lc = lc_use() ? lc_gather(s.pos, n, g_ground != 0.0, ao_c) : vec4(0.0);
            g.irr = mix(g.irr, lc.rgb, lc.a);
            lao = lc_ao(s.pos, n, lc.a, ao_c);
        }
        lit += clamp(dot(g.irr, Y) / sky_y, 0.0, 1.0) * g.cov;
        g_e_local += A_PI * max(dot(g.irr, Y), 0.0) * g.cov;
        g_c_local += A_PI * max(g.irr, vec3(0.0)) * g.cov;
        float ao = s.ao * lao;
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
    float ao_c = 1.0;
    vec4 lc = lc_use() ? lc_gather(s.pos, s.normal, g_ground != 0.0, ao_c) : vec4(0.0);
    vec3 gi = g.cov > 0.0 ? mix(g.irr, lc.rgb, lc.a) * (s.ao * lc_ao(s.pos, s.normal, lc.a, ao_c)) : vec3(0.0);
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
    float vis = nl > 0.0 ? sun_vis_of(s, l) : 0.0;
    g_e_local = 0.0;
    g_c_local = vec3(0.0);
    if (vis > 0.0) {
        vec3 h = normalize(l + v);
        float nh = max(dot(n, h), 0.0), vh = max(dot(v, h), 0.0);
        // Диск солнца (0.53°) не точка: лепесток шире на квадрат его полурадиуса — у глади блик не иглой
        float a2s = min(a2 + f.a_sun_t.w * f.a_sun_t.w * 0.25, 1.0);
        vec3 spec = brdf_d(nh, a2s) * brdf_v(nv, nl, a2s) * brdf_f(vh, f0) * comp;
        c += (kd / A_PI + spec) * f.sun_color.rgb * (nl * vis);
    }

    c += light_lamps(s, n, v, nv, a, a2, kd, f0, comp);
    // Небо: рассеянное — освещённость/π по нормали; отражение — небо по направлению отражения, у шероховатого
    // переходит в свет неба по главному направлению лепестка (Lagarde 2014). Под горизонт отражение не уходит.
    // После огней, а не до: через их цикл эти числа не живут (у земли — 128 регистров против 96)
    vec3 irr = atmo_ambient(n);
    vec3 r = reflect(-v, n);
    vec3 rd = normalize(mix(n, r, (1.0 - a) * (sqrt(1.0 - a) + a)));
    float blur = light_blur(s.rough);
    vec3 env = atmo_ambient(rd);
    // Таблица неба — только у гладкого и со ступени «Средне» (на «Низко» отражение — из гармоник неба)
    if (blur < 1.0 && f.gi_q >= LIGHT_Q_MEDIUM) env = mix(atmo_sky(vec3(rd.x, max(rd.y, 0.0), rd.z)), env, blur);
    float lit;
    c += light_indirect(s, n, nv, a, rd, blur, irr, env, kd, fss_ess, fms * ems, lit);
    // Свечение: у стекла лампы — цветом её огня при той же яркости (g_glow_*, light_lamps)
    vec3 em = s.emissive;
    if (g_glow_d < 1e8) {
        const vec3 YE = vec3(0.2126, 0.7152, 0.0722);
        em = dot(em, YE) * g_glow_c / max(dot(g_glow_c, YE), 1e-9);
    }
    c += em;
    // Воздух отрезка освещён как у точки вдали и как у глаза вблизи (глаз — eye.comp, sky_out [5].w)
    float lc = clamp(f.sky_out.v[SKY_OUT_EYE2].w, 0.0, 1.0);
    // Свет места: к непрямому свету и огням — светило на землю в доле открытости небу (lit: под открытым небом
    // весь, в зале — ничего). Вдали пиксель — уже не поверхность, а воздух перед ней: свет места сходит к небу
    float open = smoothstep(0.25, 0.75, lit);
    g_e_local += f.sky_out.v[SKY_OUT_EYE2].x * open;
    float air = 1.0 - dot(exp(-atmo_depth(s.pos)), vec3(1.0 / 3.0));
    g_e_local = exp2(mix(log2(max(g_e_local, 1e-6)), log2(max(local_sky_e(), 1e-6)), air));
    // Цвет: светило того же света (его доля в свете открытой земли), к дали — цвет света открытой земли
    const vec3 YL = vec3(0.2126, 0.7152, 0.0722);
    g_c_local += f.sun_color.rgb * (f.sky_out.v[SKY_OUT_EYE2].x * open / max(dot(f.sun_color.rgb, YL), 1e-12));
    g_c_local = mix(g_c_local / max(dot(g_c_local, YL), 1e-12), local_sky_c(), air);
    return atmo_fog_lit(c, s.pos, mix(lit, lc, exp(-dist / 32.0)));
}
