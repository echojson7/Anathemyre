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
const uint GC_NONE = 0xFFFFFFFFu;  // GcCell.kt: у пикселя нет текселя кеша (крутой склон, правило выключено)
const uint GC_AGE = 120u;          // страница без чтения дольше стольких кадров — «старая», отдаётся первой

// Уровень ячейки по глубине — как cell_size() (surface.glsl), только номер степени
int gc_level(float depth) {
    FrameData f = pc.frame;
    float foot = max(depth, 0.0) * f.pixel_angle * f.cell_pixels;
    return min(int(ceil(log2(max(foot / f.cell_min, 1.0)))), GC_LEVELS - 1);
}

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
    return s;
}

#ifdef GC_ORIGIN_VAR
// Тексель t уровня k: поверхность земли в центре ячейки, два варианта следа пикселя — у ближнего края полосы
// уровня (foot = ячейка/2) и у дальнего (foot = ячейка); освещение смешивает их по своему следу, и узор мельче
// пикселя гаснет плавно, как на пиксель (docs/gcache.md, «След пикселя»). Всё — от текселя: точка — от начала его
// чанка (ORIGIN_CHUNK), высота — t_height_f, нормаль — по размеру ячейки
uvec4 ground_texel(int k, ivec2 t) {
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
    float cp = f.cell_pixels;
    Surface lo = ground_layers(p, nrm, c, c * 0.5 / cp, wdepth);
    Surface hi = ground_layers(p, nrm, c, c / cp, wdepth);
    ORIGIN_CHUNK = saved;
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

// Пиксель земли → тексель кеша: уровень по глубине, номер текселя; GC_NONE — мимо кеша (плоскость ячейки не
// горизонтальна: крутой склон, или правило пиксельности выключено). Та же плоскость, что у cell_snap()
GcCell gc_cell_of(vec3 rel, vec3 ng) {
    FrameData f = pc.frame;
    GcCell c;
    c.kt = GC_NONE;
    c.tz = 0;
    c.depth = dot(rel, f.forward.xyz);
    vec3 a = abs(ng);
    bool flat_ = !(a.x >= a.y && a.x >= a.z) && a.y >= a.z;
    if (f.gc_on == 0u || f.pixel == 0u || !flat_) return c;
    int k = gc_level(c.depth);
    ivec2 t = gc_texel(f.cam_off + rel, k);
    c.kt = uint(k) << 24 | (uint(t.x) & 0xFFFFFFu);
    c.tz = t.y;
    return c;
}
int gc_cell_level(GcCell c) { return int(c.kt >> 24); }
ivec2 gc_cell_texel(GcCell c) { return ivec2(int(c.kt << 8) >> 8, c.tz); }
#endif
