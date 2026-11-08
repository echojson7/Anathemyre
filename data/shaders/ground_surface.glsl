#ifndef GROUND_SURFACE_GLSL
#define GROUND_SURFACE_GLSL
#include "terrain.glsl"
#include "surface.glsl"
// Земля: точка → центр ячейки → нормаль → слой → surface(), как mesh_surface() у сеток (shade.glsl).
// Слой выбирают правила по уклону сетки 8 м и маскам (те же пороги, что ground_kind() у шага мира) с плавным
// шумом, а на стыке двух лучших слоёв побеждает выступающий узор: песок затекает между камнями и травой
// (docs/terrain.md). Вода — гладь на уровне воды (ground.vert), под ней на мелком видно дно
// Слои земли в точке p (мир: метры от начала чанка ORIGIN_CHUNK, центр ячейки) с нормалью n: слой по правилам,
// стык двух лучших по узору, сырость, вода (wdepth > 0 — глубина воды над дном). Зовут ground_surface() на
// пиксель и запекание кеша земли (gcache.glsl) — правила слоёв одни. Свет не считает
Surface ground_layers(vec3 p, vec3 n, float cell, float foot, float wdepth) {
    FrameData f = pc.frame;
    bool water = wdepth > 0.0;
    ivec2 g;
    vec2 fr;
    t_split(p.xz, g, fr);
    vec4 mk = vec4(t_masks(g.x, g.y)) / 255.0;
    float rough = mk.x, sand = mk.y, grass = mk.z, wet = mk.w;
    float desert = float(t_masks2(g.x, g.y).y) / 255.0;
    float slope = t_base_slope(vec2(g) + fr);
    // Плавные пятна (градиентный шум): сотни метров — против повторяемости, метры — рваный край слоёв
    float big = fbm_n(p, 128.0, 3, 71u) - 0.5;
    float mid = fbm_n(p, 16.0, 4, 72u) - 0.5;
    float fine = fbm_n(p, 2.0, 3, 74u) - 0.5;  // рваный край слоёв на метрах
    // В пустыне песок держится и на склоне осыпания (~32°, местами круче): камень — только где маска песка мала
    float ds = desert * sand;
    float steep = mix(smoothstep(0.55, 0.85, slope), smoothstep(1.3, 1.6, slope), ds);
    float sand_lim = mix(1.0 - smoothstep(0.62, 0.78, slope), 1.0 - smoothstep(1.3, 1.6, slope), ds);
    float w[5];
    w[0] = sand * 0.95 * sand_lim + (big * 0.3 + mid * 0.9 + fine * 0.4) * (1.0 - 0.7 * ds);              // песок
    w[1] = 0.42 + mid * 0.25 - grass * 0.15;                                                     // сухая земля
    w[2] = smoothstep(0.5, 0.8, rough) * (1.0 - steep) * (1.0 - grass) + mid * 0.3 + big * 0.2;  // щебень
    w[3] = steep * 1.6 + smoothstep(0.85, 1.0, rough) * 0.4 + big * 0.2;                         // камень
    w[4] = grass * 1.0 * (1.0 - smoothstep(0.5, 0.85, slope)) + big * 0.25 - mid * 0.3;          // трава
    int a = 0;
    for (int k = 1; k < 5; ++k) if (w[k] > w[a]) a = k;
    int b = a == 0 ? 1 : 0;
    for (int k = 0; k < 5; ++k) if (k != a && w[k] > w[b]) b = k;
    const uint SEED = 0x7E77u;
    // Пустыня: песок и камень — свои материалы (слои 6 и 7); граница рваная шумом, а не линией маски
    bool dz = desert + mid * 0.5 + fine * 0.25 > 0.5;
    int la = dz && (a == 0 || a == 3) ? (a == 0 ? 6 : 7) : a;
    int lb = dz && (b == 0 || b == 3) ? (b == 0 ? 6 : 7) : b;
    Surface s = surface(f.ground_layers[la], p, n, cell, foot, SEED);
    // Широкий стык: кочки травы и камни прорастают сквозь песок, песок затекает между ними
    if (w[a] - w[b] < 0.7) {
        Surface sb = surface(f.ground_layers[lb], p, n, cell, foot, SEED);
        if (w[b] + sb.height * 0.9 > w[a] + s.height * 0.9) s = sb;
    }
    // Сырость у воды: земля темнее, трава гуще
    s.albedo *= 1.0 - wet * 0.3 * (0.6 + 0.4 * fbm_n(p, 16.0, 2, 73u));
    if (water) {
        // Гладь: свой узор и блик; на мелком сквозь неё видно дно
        Surface sw = surface(f.ground_layers[5], p, n, cell, foot, SEED);
        float k = 1.0 - exp(-wdepth * 2.5);
        sw.albedo = mix(s.albedo * 0.6, sw.albedo, k);
        s = sw;
    }
    return s;
}

// Входы слоёв земли: точка (от начала чанка origin), нормаль, ячейка, глубина воды и след пикселя — nv вариантов
// (на пиксель — один, у текселя кеша — два, gcache.glsl)
struct GroundIn { vec3 p; vec3 n; float cell; float wdepth; float foot[2]; int nv; ivec2 origin; };

// Слои по входам. Один вызов ground_layers() на шейдер, варианты — циклом без развёртки: слои и материалы —
// самый большой код шейдеров, и каждая лишняя копия — это секунды сборки конвейера (на lavapipe под GPU-AV —
// минуты). Поэтому и путь на пиксель, и тексель без страницы, и запекание считают землю здесь (docs/gcache.md)
void ground_eval(GroundIn g, out Surface s0, out Surface s1) {
#ifdef GC_ORIGIN_VAR
    ivec2 saved = ORIGIN_CHUNK;
    ORIGIN_CHUNK = g.origin;
#endif
    [[dont_unroll]] for (int i = 0; i < g.nv; ++i) {
        Surface s = ground_layers(g.p, g.n, g.cell, g.foot[i], g.wdepth);
        if (i == 0) s0 = s;
        else s1 = s;
    }
#ifdef GC_ORIGIN_VAR
    ORIGIN_CHUNK = saved;
#endif
}

// Входы земли в точке rel (относительно камеры) на пиксель: vnormal — нормаль вершин, level — кольцо, wdepth —
// глубина воды, ng — нормаль треугольника вверх (выбирает плоскость привязки ячейки). Кадр мира берёт землю из
// кеша (gcache.glsl), этот путь — у крутых склонов и без правила пиксельности
GroundIn ground_pixel_in(vec3 v_rel, vec3 v_normal, uint v_level, float v_wdepth, vec3 ng) {
    FrameData f = pc.frame;
    float depth = dot(v_rel, f.forward.xyz);
    float cell = cell_size(depth);
    float foot = max(depth, 0.0) * f.pixel_angle;
    vec3 p = f.cam_off + v_rel;
    // Нормаль треугольника выбирает плоскость привязки (на обрыве ячейки встают вертикально)
    if (f.pixel != 0u) {
        p = cell_snap(p, ng, cell);
    } else {
        cell = f.cell_min;
        p -= ng * 0.002;
    }
    // Плавная нормаль сетки 8 м (+ слой деталей у ближних колец), дальние кольца — из вершин
    vec3 n = v_level <= 5u ? t_smooth_normal(p.xz, v_level <= 1u, true) : normalize(v_normal);
    if (v_wdepth > 0.0) n = vec3(0.0, 1.0, 0.0);
    GroundIn g;
    g.p = p;
    g.n = n;
    g.cell = cell;
    g.wdepth = v_wdepth;
    g.foot[0] = foot;
    g.foot[1] = foot;
    g.nv = 1;
    g.origin = f.cam_chunk;
    return g;
}

// Поверхность земли на пиксель (входы — ground_pixel_in)
Surface ground_surface(vec3 v_rel, vec3 v_normal, uint v_level, float v_wdepth, vec3 ng) {
    GroundIn g = ground_pixel_in(v_rel, v_normal, v_level, v_wdepth, ng);
    Surface s, s1;
    ground_eval(g, s, s1);
    s.pos = g.p - pc.frame.cam_off;
    return s;
}
#endif
