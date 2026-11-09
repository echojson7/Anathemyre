// Тени солнца страницами (docs/shadow.md): виртуальная карта теней clipmap'ом вокруг камеры. Уровень k — решётка
// текселей с шагом ячейки правила пиксельности cell_min·2^k в плоскости поперёк светила (оси r, u), тексели
// привязаны к миру: сдвиг камеры их не двигает. Окно уровня — SM_WIN² страниц по SM_PAGE² текселей вокруг
// камеры по модулю, запись окна указывает на страницу общего пула. Тексель хранит высоту к светилу ближайшей к
// нему заслонки — грани сетки, повёрнутой от светила (sm_enc). Пишут: разбор плиток (заявки, sm_request),
// shadow_*.comp и shadow.vert/.frag (отрисовка новых страниц); читает свет (sm_visibility)
#ifndef SHADOW_GLSL
#define SHADOW_GLSL
#include "common.glsl"

const int SM_LEVELS = 12;
const int SM_PAGE = 128;           // текселей в стороне страницы
const int SM_WIN = 32;             // страниц в стороне окна уровня
const uint SM_REQ = 0x80000000u;   // запись окна: заявка кадра (младшие биты — кадр); иначе физическая + 1
const uint SM_AGE = 120u;          // страница без чтения дольше стольких кадров — «старая», отдаётся первой
// Точка не дальше стольких текселей уровня от камеры по r и u: окно — 16 страниц в каждую сторону без одной
// (камера где угодно в своей странице), запас — на сдвиг выборки и смещение по нормали
const float SM_REACH = 1880.0;
const float SM_NOFF = 1.0;         // смещение точки по нормали, текселей уровня
const float SM_MIN_TEXELS = 0.5;   // заслонка с радиусом меньше стольких текселей в уровень не рисуется

float sm_texel(int k) { return ldexp(pc.frame.sm_r.w, k); }

// Высота → слово, где порядок слов — порядок высот (атомарный максимум). 0 — заслонки нет
uint sm_enc(float h) {
    uint b = floatBitsToUint(h);
    return (b & 0x80000000u) != 0u ? ~b : b | 0x80000000u;
}
float sm_dec(uint e) { return uintBitsToFloat((e & 0x80000000u) != 0u ? e & 0x7FFFFFFFu : ~e); }

// Уровень точки: тексель — ячейка правила на этой глубине (как cell_size), но не мельче того уровня, чьё окно
// ещё накрывает точку. depth — глубина вдоль взгляда, far — max(|r|, |u|) от камеры, м. SM_LEVELS — нет карты
int sm_level(float depth, float far) {
    FrameData f = pc.frame;
    float t0 = f.sm_r.w;
    float foot = max(depth, 0.0) * f.pixel_angle * f.cell_pixels;
    int kd = int(ceil(log2(max(foot / t0, 1.0))));
    int kw = int(ceil(log2(max(far / (SM_REACH * t0), 1.0))));
    return min(max(kd, kw), SM_LEVELS);
}

// Ключ страницы: уровень и номер на мировой решётке уровня (24 бита x со знаком, z целиком)
uvec2 sm_page_key(int k, ivec2 page) { return uvec2(uint(k) << 24 | (uint(page.x) & 0xFFFFFFu), uint(page.y)); }
int sm_key_level(uvec2 key) { return int(key.x >> 24); }
ivec2 sm_key_page(uvec2 key) { return ivec2(int(key.x << 8) >> 8, int(key.y)); }
// Место страницы в окне уровня (по модулю)
uint sm_slot(int k, ivec2 page) {
    return uint(k) * uint(SM_WIN * SM_WIN) + uint(page.y & (SM_WIN - 1)) * uint(SM_WIN) + uint(page.x & (SM_WIN - 1));
}

// Точка относительно камеры → тексель окна уровня k (дробный, от угла окна)
vec2 sm_window(vec3 rel, int k) {
    FrameData f = pc.frame;
    return (vec2(dot(rel, f.sm_r.xyz), dot(rel, f.sm_u.xyz)) + f.sm_off[k]) / sm_texel(k);
}
// Высота к светилу от якоря
float sm_height(vec3 rel) { return dot(rel, pc.frame.sm_s.xyz) + pc.frame.sm_u.w; }

// Страницы окна под квадратом w ± r (тексели окна): false — квадрат мимо окна
bool sm_rect(vec2 w, float r, out ivec2 lo, out ivec2 hi) {
    lo = ivec2(floor((w - r) / float(SM_PAGE)));
    hi = ivec2(floor((w + r) / float(SM_PAGE)));
    if (any(lessThan(hi, ivec2(0))) || any(greaterThan(lo, ivec2(SM_WIN - 1)))) return false;
    lo = max(lo, ivec2(0));
    hi = min(hi, ivec2(SM_WIN - 1));
    return true;
}

// Физическая страница или −1 (нет: не заявлена, не получила места, окно занято другой страницей)
int sm_find(int k, ivec2 page) {
    FrameData f = pc.frame;
    uint e = f.sm_table.v[sm_slot(k, page)];
    if (e == 0u || (e & SM_REQ) != 0u) return -1;
    int phys = int(e - 1u);
    SmPage pg = f.sm_pages.v[phys];
    return uvec2(pg.key0, pg.key1) == sm_page_key(k, page) ? phys : -1;
}

// Тексель окна (целый) уровня k: свет (1) или тень (0) для точки на высоте hr. Нет страницы — свет, счётчик
uint g_sm_taps = 0u, g_sm_miss = 0u;
float sm_tap(int k, ivec2 t, float hr) {
    FrameData f = pc.frame;
    g_sm_taps++;
    int phys = sm_find(k, f.sm_base[k] + (t >> 7));
    if (phys < 0) { g_sm_miss++; return 1.0; }
    uint e = f.sm_texels.v[uint(phys) * uint(SM_PAGE * SM_PAGE) + uint((t.y & 127) * SM_PAGE + (t.x & 127))];
    return e != 0u && sm_dec(e) > hr ? 0.0 : 1.0;
}

// Видимость светила точкой pos (относительно камеры, центр ячейки) с нормалью n: 1 — свет, 0 — тень. Четыре
// соседних текселя с весами по месту точки между ними: край тени — мягкое число на ширину текселя, а не да/нет
float sm_visibility(vec3 pos, vec3 n) {
    FrameData f = pc.frame;
    if (f.sm_on == 0u) return 1.0;
    float depth = dot(pos, f.forward.xyz);
    if (depth > f.sm_s.w) return 1.0;
    vec2 ru = vec2(dot(pos, f.sm_r.xyz), dot(pos, f.sm_u.xyz));
    int k = sm_level(depth, max(abs(ru.x), abs(ru.y)));
    if (k >= SM_LEVELS) return 1.0;
    float t = sm_texel(k);
    vec3 p = pos + n * (SM_NOFF * t);
    float hr = sm_height(p);
    vec2 w = sm_window(p, k) - 0.5;
    ivec2 b = ivec2(floor(w));
    vec2 fr = w - vec2(b);
    float a = mix(sm_tap(k, b, hr), sm_tap(k, b + ivec2(1, 0), hr), fr.x);
    float c = mix(sm_tap(k, b + ivec2(0, 1), hr), sm_tap(k, b + ivec2(1, 1), hr), fr.x);
    // Дальний край: тень сеток гаснет в последней пятой дальности (дальше — только карта горизонта)
    return mix(mix(a, c, fr.y), 1.0, smoothstep(0.8 * f.sm_s.w, f.sm_s.w, depth));
}

// Режим F3 «страницы теней»: страница точки — нарисована в этом кадре (красная), из кеша (зелёная), нет места
// или не заявлена (пурпурная); край страницы — тёмная линия, уровень — оттенок
vec3 sm_debug_tint(vec3 c, vec3 pos) {
    FrameData f = pc.frame;
    if (f.sm_on == 0u) return c;
    float depth = dot(pos, f.forward.xyz);
    vec2 ru = vec2(dot(pos, f.sm_r.xyz), dot(pos, f.sm_u.xyz));
    int k = sm_level(depth, max(abs(ru.x), abs(ru.y)));
    if (k >= SM_LEVELS || depth > f.sm_s.w) return c;
    ivec2 t = ivec2(floor(sm_window(pos, k)));
    int phys = sm_find(k, f.sm_base[k] + (t >> 7));
    vec3 tint = phys < 0 ? vec3(1.0, 0.1, 1.0)
              : f.sm_pages.v[phys].made == f.sm_frame ? vec3(1.0, 0.15, 0.1) : vec3(0.15, 0.9, 0.2);
    tint *= 0.75 + 0.25 * float(k & 1);
    ivec2 e = t & 127;
    if (min(e.x, e.y) < 2 || max(e.x, e.y) > 125) tint *= 0.25;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return tint * l * 0.6 + c * 0.4;
}
#endif
