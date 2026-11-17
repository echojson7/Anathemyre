// Кеш поверхности земли (docs/gcache.md): виртуальная текстура в мировых координатах. Уровень k — решётка
// текселей с шагом ячейки правила пиксельности cell_min·2^k, тексель — ровно ячейка. Страница — 8×8 текселей,
// окно страниц уровня вокруг камеры (gc_w × gc_w, по модулю) указывает на физическую страницу в общем пуле.
// Тексель — поверхность земли без света (Surface), посчитанная раз и читаемая, пока страница живёт. Значение —
// функция только уровня и номера текселя (ground_texel), поэтому кадр не зависит от того, когда страница
// запеклась. Пишут: разбор плиток (заявки, light_tiles.comp), gcache_*.comp; читает освещение (light_cells.comp)
#ifndef GCACHE_GLSL
#define GCACHE_GLSL
// Шейдер, который запекает тексели (ground_texel), задаёт начало отсчёта узоров переменной до включения:
//     ivec2 g_origin;  #define ORIGIN_CHUNK g_origin  #define GC_ORIGIN_VAR
// и в начале main() ставит g_origin = pc.frame.cam_chunk (пути на пиксель считают от чанка камеры)
#include "ground_surface.glsl"

const int GC_LEVELS = 13;          // уровни 0..12: ячейка до cell_min·4096 (51.2 м при 1/80 м)
const int GC_PAGE = 8;             // текселей в стороне страницы
const uint GC_REQ = 0x80000000u;   // запись окна: заявка кадра (младшие биты — кадр); иначе физическая + 1
const uint GC_NONE = 0xFFFFFFFFu;  // GcCell.kt: у пикселя нет текселя кеша (крутой склон)
const uint GC_AGE = 120u;          // страница без чтения дольше стольких кадров — «старая», отдаётся первой

// Уровень ячейки по глубине — как cell_size() (surface.glsl), только номер степени
int gc_level_foot(float foot) { return min(int(ceil(log2(max(foot / pc.frame.cell_min, 1.0)))), GC_LEVELS - 1); }
int gc_level(float depth) { return gc_level_foot(max(depth, 0.0) * pc.frame.pixel_angle * pc.frame.cell_pixels); }

float gc_cell(int k) { return ldexp(pc.frame.cell_min, k); }

// Целое деление вниз (номера текселей у края карты бывают отрицательными)
int gc_floor_div(int a, int b) { return a >= 0 ? a / b : -((b - 1 - a) / b); }

// Ключ страницы: уровень и номер (24 бита x со знаком, z целиком)
uvec2 gc_page_key(int k, ivec2 page) { return uvec2(uint(k) << 24 | (uint(page.x) & 0xFFFFFFu), uint(page.y)); }
int gc_key_level(uvec2 key) { return int(key.x >> 24); }
ivec2 gc_key_page(uvec2 key) { return ivec2(int(key.x << 8) >> 8, int(key.y)); }

// Место страницы в окне уровня
uint gc_slot(int k, ivec2 page) {
    uint w = pc.frame.gc_w;
    return uint(k) * w * w + (uint(page.y) & (w - 1u)) * w + (uint(page.x) & (w - 1u));
}

// Тексель кадра: уровень и номер текселя (метры карты от угла / ячейка) точки p (от начала чанка камеры)
ivec2 gc_texel(vec3 p, int k) {
    FrameData f = pc.frame;
    return ivec2(floor(p.xz / gc_cell(k))) + (f.cam_chunk - ivec2(f.t_chunk0)) * (f.gc_tpc0 >> k);
}

// Физическая страница текселя или −1 (нет: не заявлена, не получила места или окно занято другой страницей)
int gc_find(int k, ivec2 t) {
    FrameData f = pc.frame;
    ivec2 page = t >> 3;
    uint e = f.gc_table.v[gc_slot(k, page)];
    if (e == 0u || (e & GC_REQ) != 0u) return -1;
    int phys = int(e - 1u);
    GcPage pg = f.gc_pages.v[phys];
    return uvec2(pg.key0, pg.key1) == gc_page_key(k, page) ? phys : -1;
}

uvec4 gc_read(int phys, ivec2 t) { return pc.frame.gc_texels.v[phys * 64 + (t.y & 7) * 8 + (t.x & 7)]; }

// Упаковка поверхности: альбедо sRGB 8 бит и шероховатость; нормаль октаэдром 2×12 бит и затенение 8 бит.
// Точность — как у текстуры альбедо: после света и тонмаппинга разница ≤ 1/255
vec3 gc_to_srgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, c * 12.92, lessThanEqual(c, vec3(0.0031308)));
}
uvec2 gc_pack(Surface s) {
    vec3 n = s.normal / (abs(s.normal.x) + abs(s.normal.y) + abs(s.normal.z));
    vec2 e = n.y >= 0.0 ? n.xz : (1.0 - abs(n.zx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0);
    uvec2 q = uvec2(round(clamp(e * 0.5 + 0.5, 0.0, 1.0) * 4095.0));
    uint ao = uint(round(clamp(s.ao, 0.0, 1.0) * 255.0));
    return uvec2(packUnorm4x8(vec4(gc_to_srgb(s.albedo), clamp(s.rough, 0.0, 1.0))), q.x | q.y << 12 | ao << 24);
}
Surface gc_unpack(uvec2 v) {
    Surface s;
    vec4 a = unpack_srgb(v.x);
    s.albedo = a.rgb;
    s.rough = a.a;
    vec2 e = vec2(float(v.y & 4095u), float((v.y >> 12) & 4095u)) / 4095.0 * 2.0 - 1.0;
    vec3 n = vec3(e.x, 1.0 - abs(e.x) - abs(e.y), e.y);
    if (n.y < 0.0) n.xz = (1.0 - abs(n.zx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0);
    s.normal = normalize(n);
    s.ao = float(v.y >> 24) / 255.0;
    s.pos = vec3(0.0);
    s.emissive = vec3(0.0);
    s.height = 0.0;
    s.metal = 0.0;  // металла в земле нет; шероховатость уже с поправкой Токсвига (surface())
    s.nvar = 0.0;
    return s;
}

#ifdef GC_ORIGIN_VAR
// Тексель t уровня k: поверхность земли в центре ячейки, два варианта следа пикселя — у ближнего края полосы
// уровня (foot = ячейка/2) и у дальнего (foot = ячейка); освещение смешивает их по своему следу, и узор мельче
// пикселя гаснет плавно, как на пиксель (docs/gcache.md, «След пикселя»). Всё — от текселя: точка — от начала его
// чанка (ORIGIN_CHUNK), высота — t_height_f, нормаль — по размеру ячейки. Слои — ground_eval(), один вызов на шейдер
GroundIn ground_texel_in(int k, ivec2 t) {
    FrameData f = pc.frame;
    float c = gc_cell(k);
    int tpc = f.gc_tpc0 >> k;
    ivec2 ch = ivec2(gc_floor_div(t.x, tpc), gc_floor_div(t.y, tpc));
    ivec2 l = t - ch * tpc;
    ivec2 saved = ORIGIN_CHUNK;
    ORIGIN_CHUNK = ch + ivec2(f.t_chunk0);
    vec3 p;
    p.xz = (vec2(l) + 0.5) * c;
    // Узел 1 м и место в нём: целые метры чанка + доля
    ivec2 n = ch * 256 + ivec2(floor(p.xz));
    vec2 fr = p.xz - floor(p.xz);
    // Октавы деталей мельче двух ячеек в высоте не видны (как у колец)
    int min_lp = 0;
    while (min_lp < 8 && float(1 << min_lp) < 2.0 * c) ++min_lp;
    float wdepth;
    float h = t_height_f(n, fr, c <= 0.5 ? 0 : min_lp, wdepth);
    p.y = (wdepth > 0.0 ? h + wdepth : h) - 0.002;  // как cell_snap: на 2 мм внутрь
    // Нормаль по размеру ячейки (экранный LOD): слой деталей у ячеек до 10 см, сетка 8 м до 1.6 м, дальше —
    // сглаженная сетка, как у дальних колец
    vec3 nrm = k <= 3 ? t_smooth_normal(p.xz, true, true)
             : k <= 7 ? t_smooth_normal(p.xz, false, true)
                      : t_mip_normal(vec2(n) + fr, min(k - 6, 5));
    if (wdepth > 0.0) nrm = vec3(0.0, 1.0, 0.0);
    GroundIn g;
    g.p = p;
    g.n = nrm;
    g.cell = c;
    g.wdepth = wdepth;
    g.foot[0] = c * 0.5 / f.cell_pixels;
    g.foot[1] = c / f.cell_pixels;
    g.nv = 2;
    g.origin = ORIGIN_CHUNK;
    ORIGIN_CHUNK = saved;
    return g;
}

uvec4 ground_texel(int k, ivec2 t) {
    Surface lo, hi;
    ground_eval(ground_texel_in(k, t), lo, hi);
    return uvec4(gc_pack(lo), gc_pack(hi));
}
#endif

// Поверхность для света из текселя: смесь двух вариантов по следу пикселя; pos — центр ячейки по xz и высота
// точки пикселя (rel — точка пикселя относительно камеры), как у cell_snap() на пиксель
Surface gc_surface(uvec4 v, int k, ivec2 t, vec3 rel) {
    FrameData f = pc.frame;
    float c = gc_cell(k);
    float foot = max(dot(rel, f.forward.xyz), 0.0) * f.pixel_angle * f.cell_pixels;
    float w = clamp(log2(max(foot, 1e-9) / c) + 1.0, 0.0, 1.0);
    Surface a = gc_unpack(v.xy), b = gc_unpack(v.zw);
    Surface s = a;
    s.albedo = mix(a.albedo, b.albedo, w);
    s.rough = mix(a.rough, b.rough, w);
    s.normal = normalize(mix(a.normal, b.normal, w));
    s.ao = mix(a.ao, b.ao, w);
    ivec2 lt = t - (f.cam_chunk - ivec2(f.t_chunk0)) * (f.gc_tpc0 >> k);
    vec2 cxz = (vec2(lt) + 0.5) * c - f.cam_off.xz;
    s.pos = vec3(cxz.x, rel.y - 0.002, cxz.y);
    return s;
}

// На пиксель (правило ячеек выключено — по умолчанию, docs/gcache.md «На пиксель»): пиксель смешивает четыре
// соседних текселя (билинейно), без ступенек текселя. Нижний из них (номер со сдвигом чанка) и доля между ними
ivec2 gc_bilinear(vec3 rel, int k, out vec2 fr) {
    FrameData f = pc.frame;
    vec2 q = (f.cam_off.xz + rel.xz) / gc_cell(k) - 0.5;
    vec2 b = floor(q);
    fr = q - b;
    return ivec2(b) + (f.cam_chunk - ivec2(f.t_chunk0)) * (f.gc_tpc0 >> k);
}

// Отражённый свет пикселя из кеша (docs/gcache.md, «Отражённый свет в кеше»): смесь четырёх текселей теми же
// долями, что поверхность; ставит gc_surface_px, читает light_indirect. ok — свет есть у всех смешанных текселей
struct GcLit { vec3 irr; float cov, vis, ao, refl; };
GcLit g_gcl;
bool g_gcl_ok = false;

// Поверхность пикселя из четырёх текселей b…b+1; phys0 — страница текселя b. Тексель без страницы (пул кончился)
// выпадает из смеси, остальные делят его долю. pos — точка пикселя (на 2 мм внутрь, как у текселя)
Surface gc_surface_px(int k, ivec2 b, vec2 fr, vec3 rel, int phys0) {
    FrameData f = pc.frame;
    float c = gc_cell(k);
    float foot = max(dot(rel, f.forward.xyz), 0.0) * f.pixel_angle * f.cell_pixels;
    float wv = clamp(log2(max(foot, 1e-9) / c) + 1.0, 0.0, 1.0);
    vec3 alb = vec3(0.0), nrm = vec3(0.0);
    float rough = 0.0, ao = 0.0, wsum = 0.0;
    ivec2 page0 = b >> 3;
    bool lit = (f.gc_lmode & GC_LIGHT_ON) != 0u;
    vec3 l_irr = vec3(0.0);
    vec4 l_q = vec4(0.0);
    bool l_ok = lit;
    [[unroll]] for (int j = 0; j < 4; ++j) {
        ivec2 o = ivec2(j & 1, j >> 1);
        float w = (o.x == 1 ? fr.x : 1.0 - fr.x) * (o.y == 1 ? fr.y : 1.0 - fr.y);
        ivec2 t = b + o;
        int phys = (t >> 3) == page0 ? phys0 : gc_find(k, t);
        if (phys < 0) continue;
        uvec4 v = gc_read(phys, t);
        Surface lo = gc_unpack(v.xy), hi = gc_unpack(v.zw);
        if (lit) {
            uvec4 L = f.gc_light.v[phys * 64 + (t.y & 7) * 8 + (t.x & 7)];
            vec2 bz = unpackHalf2x16(L.z);
            l_ok = l_ok && bz.y == 1.0;
            l_irr += vec3(unpackHalf2x16(L.y), bz.x) * w;
            l_q += unpackUnorm4x8(L.w) * w;
        }
        alb += mix(lo.albedo, hi.albedo, wv) * w;
        rough += mix(lo.rough, hi.rough, wv) * w;
        nrm += mix(lo.normal, hi.normal, wv) * w;
        ao += mix(lo.ao, hi.ao, wv) * w;
        wsum += w;
    }
    Surface s = gc_unpack(uvec2(0u));
    float inv = 1.0 / max(wsum, 1e-6);
    s.albedo = alb * inv;
    s.rough = rough * inv;
    s.normal = normalize(nrm + vec3(0.0, 1e-6, 0.0));
    s.ao = ao * inv;
    s.pos = vec3(rel.x, rel.y - 0.002, rel.z);
    g_gcl_ok = l_ok && wsum > 0.0;
    if (g_gcl_ok) {
        g_gcl.irr = l_irr * inv;
        vec4 q = l_q * inv;
        g_gcl.cov = q.x;
        g_gcl.vis = q.y;
        g_gcl.ao = q.z;
        g_gcl.refl = q.w;
    }
    return s;
}

// Пиксель земли → тексель кеша: уровень по следу пикселя foot (м; правило ячеек — по глубине), номер текселя (на
// пиксель — нижний из четырёх смешиваемых); GC_NONE — мимо кеша (плоскость ячейки не горизонтальна: крутой
// склон). Та же плоскость, что у cell_snap()
GcCell gc_cell_of(vec3 rel, vec3 ng, float foot) {
    FrameData f = pc.frame;
    GcCell c;
    c.kt = GC_NONE;
    c.tz = 0;
    c.depth = dot(rel, f.forward.xyz);
    vec3 a = abs(ng);
    bool flat_ = !(a.x >= a.y && a.x >= a.z) && a.y >= a.z;
    if (f.gc_on == 0u || !flat_) return c;
    int k = f.pixel != 0u ? gc_level(c.depth) : gc_level_foot(foot);
    vec2 fr;
    ivec2 t = f.pixel != 0u ? gc_texel(f.cam_off + rel, k) : gc_bilinear(rel, k, fr);
    c.kt = uint(k) << 24 | (uint(t.x) & 0xFFFFFFu);
    c.tz = t.y;
    return c;
}
int gc_cell_level(GcCell c) { return int(c.kt >> 24); }
ivec2 gc_cell_texel(GcCell c) { return ivec2(int(c.kt << 8) >> 8, c.tz); }
#endif
