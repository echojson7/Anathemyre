// Тени солнца страницами (docs/shadow.md): виртуальная карта теней clipmap'ом вокруг камеры. Уровень — решётка
// текселей sm_t0·2^уровень в плоскости поперёк светила (оси r, u), тексели привязаны к миру: сдвиг камеры их не
// двигает. Окно уровня — SM_WIN² страниц по SM_PAGE² текселей вокруг камеры по модулю, запись окна указывает на
// страницу общего пула. Тексель хранит высоту к светилу ближайшей к нему заслонки — грани сетки, повёрнутой от
// светила (sm_enc).
// Наборов два (6б-2): светило идёт шагами, и на шаг строится новый набор, пока старый ещё показан; потом свет
// перетекает со старого на новый за SM_BLEND (vk_shadow.cpp). Место уровня k = набор · SM_LEVELS + уровень: у
// каждого места своё окно, свои страницы и свои оси набора. Пишут: разбор плиток (заявки, sm_request),
// shadow_*.comp и shadow.vert/.frag (отрисовка новых страниц); читает свет (sm_visibility)
#ifndef SHADOW_GLSL
#define SHADOW_GLSL
#include "common.glsl"

const int SM_LEVELS = 12;          // уровней в наборе
const int SM_SLOTS = 24;           // мест уровней: два набора
const int SM_PAGE = 128;           // текселей в стороне страницы
const int SM_WIN = 32;             // страниц в стороне окна уровня
const uint SM_REQ = 0x80000000u;   // запись окна: заявка кадра (младшие биты — кадр); иначе физическая + 1
const uint SM_AGE = 120u;          // страница без чтения дольше стольких кадров — «старая», отдаётся первой
// Точка не дальше стольких текселей уровня от камеры по r и u: окно — 16 страниц в каждую сторону без одной
// (камера где угодно в своей странице), запас — на сдвиг выборки, поиск PCSS и смещение по нормали
const float SM_REACH = 1880.0;
const float SM_NOFF = 1.0;         // смещение точки по нормали, текселей уровня
const float SM_MIN_TEXELS = 0.5;   // заслонка с радиусом меньше стольких текселей в уровень не рисуется
// Мягкий край (PCSS): полутень — расстояние до заслонки × тангенс радиуса диска светила (sm_disc). Заслонки ищутся
// не дальше SM_PCSS_FAR к светилу (дальше полутень не шире) и не дальше SM_PCSS_MAX текселей по карте
const float SM_PCSS_FAR = 24.0;
const float SM_PCSS_MAX = 12.0;

int sm_set_of(int k) { return k >= SM_LEVELS ? 1 : 0; }
int sm_level_of(int k) { return k >= SM_LEVELS ? k - SM_LEVELS : k; }
float sm_texel(int k) { return ldexp(pc.frame.sm_t0, sm_level_of(k)); }

// Высота → слово, где порядок слов — порядок высот (атомарный максимум). 0 — заслонки нет
uint sm_enc(float h) {
    uint b = floatBitsToUint(h);
    return (b & 0x80000000u) != 0u ? ~b : b | 0x80000000u;
}
float sm_dec(uint e) { return uintBitsToFloat((e & 0x80000000u) != 0u ? e & 0x7FFFFFFFu : ~e); }

// Уровень точки в наборе: тексель — ячейка правила на этой глубине (как cell_size), но не мельче того уровня,
// чьё окно ещё накрывает точку. depth — глубина вдоль взгляда, far — max(|r|, |u|) от камеры, м. SM_LEVELS — нет
int sm_level(float depth, float far) {
    FrameData f = pc.frame;
    float t0 = f.sm_t0;
    float foot = max(depth, 0.0) * f.pixel_angle * f.cell_pixels;
    int kd = int(ceil(log2(max(foot / t0, 1.0))));
    int kw = int(ceil(log2(max(far / (SM_REACH * t0), 1.0))));
    return min(max(kd, kw), SM_LEVELS);
}

// Ключ страницы: место уровня и номер на мировой решётке уровня (24 бита x со знаком, z целиком)
uvec2 sm_page_key(int k, ivec2 page) { return uvec2(uint(k) << 24 | (uint(page.x) & 0xFFFFFFu), uint(page.y)); }
int sm_key_level(uvec2 key) { return int(key.x >> 24); }
ivec2 sm_key_page(uvec2 key) { return ivec2(int(key.x << 8) >> 8, int(key.y)); }
// Место страницы в окне (по модулю)
uint sm_slot(int k, ivec2 page) {
    return uint(k) * uint(SM_WIN * SM_WIN) + uint(page.y & (SM_WIN - 1)) * uint(SM_WIN) + uint(page.x & (SM_WIN - 1));
}

// Точка относительно камеры → r, u набора
vec2 sm_ru(vec3 rel, int set) { return vec2(dot(rel, pc.frame.sm_r[set].xyz), dot(rel, pc.frame.sm_u[set].xyz)); }
// Точка относительно камеры → тексель окна места k (дробный, от угла окна)
vec2 sm_window(vec3 rel, int k) { return (sm_ru(rel, sm_set_of(k)) + pc.frame.sm_off[k]) / sm_texel(k); }
// Высота к светилу от якоря набора
float sm_height(vec3 rel, int set) { return dot(rel, pc.frame.sm_s[set].xyz) + pc.frame.sm_u[set].w; }

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

// Радиус поиска заслонок PCSS на месте с текселем t, текселей; 0 — край жёсткий (2×2). На «Низко» PCSS нет
float sm_search(float t) {
    FrameData f = pc.frame;
    if (f.light_q < LIGHT_Q_MEDIUM) return 0.0;
    float r = min(SM_PCSS_FAR * f.sm_disc / t, SM_PCSS_MAX);
    return r >= 1.0 ? r : 0.0;
}

// Тексель окна (целый) места k: высота заслонки, −1e30 — заслонки нет. Нет страницы — свет, счётчик
const float SM_NONE = -1.0e30;
uint g_sm_taps = 0u, g_sm_miss = 0u;
float sm_occ(int k, ivec2 t) {
    FrameData f = pc.frame;
    g_sm_taps++;
    int phys = sm_find(k, f.sm_base[k] + (t >> 7));
    if (phys < 0) { g_sm_miss++; return SM_NONE; }
    uint e = f.sm_texels.v[uint(phys) * uint(SM_PAGE * SM_PAGE) + uint((t.y & 127) * SM_PAGE + (t.x & 127))];
    return e != 0u ? sm_dec(e) : SM_NONE;
}

// Четыре соседних текселя вокруг w (тексели окна) с весами по месту точки между ними: 1 — свет, 0 — тень. hr —
// высота точки, g — её прирост на тексель по r и u (плоскость приёмника): сосед сравнивается со своей высотой
float sm_pcf(int k, vec2 w, float hr, vec2 g) {
    vec2 q = w - 0.5;
    ivec2 b = ivec2(floor(q));
    vec2 fr = q - vec2(b);
    vec2 o = vec2(b) + 0.5 - w;  // от точки до центра текселя b, тексели
    float h00 = hr + dot(o, g), dx = g.x, dy = g.y;
    float a = mix(sm_occ(k, b) > h00 ? 0.0 : 1.0, sm_occ(k, b + ivec2(1, 0)) > h00 + dx ? 0.0 : 1.0, fr.x);
    float c = mix(sm_occ(k, b + ivec2(0, 1)) > h00 + dy ? 0.0 : 1.0,
                  sm_occ(k, b + ivec2(1, 1)) > h00 + dx + dy ? 0.0 : 1.0, fr.x);
    return mix(a, c, fr.y);
}

// Точка i из n диска Фогеля радиуса 1: узор один и тот же во всех кадрах и во всех точках (без поворота от кадра
// или пикселя — иначе кромка зернит и кипит, docs/light.md, правило 2)
vec2 sm_vogel(int i, int n) {
    float a = float(i) * 2.39996323;
    return vec2(cos(a), sin(a)) * sqrt((float(i) + 0.5) / float(n));
}

// Видимость светила в наборе set: точка pos (от камеры, центр ячейки), нормаль n, глубина вдоль взгляда
float sm_vis_set(int set, vec3 pos, vec3 n, float depth) {
    FrameData f = pc.frame;
    vec2 ru = sm_ru(pos, set);
    int lv = sm_level(depth, max(abs(ru.x), abs(ru.y)));
    if (lv >= SM_LEVELS) return 1.0;
    int k = set * SM_LEVELS + lv;
    float t = sm_texel(k);
    vec3 p = pos + n * (SM_NOFF * t);
    float hr = sm_height(p, set);
    vec2 w = sm_window(p, k);
    // Плоскость приёмника: высота к светилу растёт по r и u так, как наклонена поверхность (крутой наклон —
    // предел 4 на тексель: у скользящего света сосед иначе прячет точку за её же гранью)
    float ns = max(dot(n, f.sm_s[set].xyz), 0.25);
    vec2 g = -vec2(dot(n, f.sm_r[set].xyz), dot(n, f.sm_u[set].xyz)) / ns * t;
    float rs = sm_search(t);
    if (rs == 0.0) return sm_pcf(k, w, hr, g);
    // PCSS: средняя высота заслонок над точкой в круге поиска → полутень → фильтр её радиуса
    int ns_taps = f.light_q >= LIGHT_Q_ULTRA ? 12 : f.light_q >= LIGHT_Q_HIGH ? 8 : 6;
    int nf_taps = f.light_q >= LIGHT_Q_ULTRA ? 20 : f.light_q >= LIGHT_Q_HIGH ? 12 : 8;
    float sum = 0.0;
    int cnt = 0;
    for (int i = 0; i < ns_taps; ++i) {
        vec2 o = sm_vogel(i, ns_taps) * rs;
        vec2 c = floor(w + o) + 0.5;
        float h = sm_occ(k, ivec2(c));
        float hc = hr + dot(c - w, g);
        if (h > hc) {
            sum += min(h - hc, SM_PCSS_FAR);
            cnt++;
        }
    }
    if (cnt == ns_taps) return 0.0;  // заслонки над всем кругом: полутень меньше круга, точка в тени
    // Радиус фильтра — полутень без полутекселя, который уже даёт 2×2: от жёсткого края к мягкому без скачка
    float pr = cnt == 0 ? 0.0 : min(sum / float(cnt) * f.sm_disc / t, rs) - 0.5;
    if (pr <= 0.25) return sm_pcf(k, w, hr, g);
    float v = 0.0;
    for (int i = 0; i < nf_taps; ++i) {
        vec2 o = sm_vogel(i, nf_taps) * pr;
        v += sm_pcf(k, w + o, hr + dot(o, g), g);
    }
    return v / float(nf_taps);
}

// Видимость светила точкой pos (относительно камеры, центр ячейки) с нормалью n: 1 — свет, 0 — тень, мягко.
// Пока свет перетекает со старого шага светила на новый, читаются оба набора с весом sm_mix у нового
float sm_visibility(vec3 pos, vec3 n) {
    FrameData f = pc.frame;
    if (f.sm_on == 0u) return 1.0;
    float dist = length(pos);
    if (dist > f.sm_far) return 1.0;
    float depth = dot(pos, f.forward.xyz);
    int cur = int(f.sm_cur), old = 1 - cur;
    bool rc = ((f.sm_read >> uint(cur)) & 1u) != 0u, ro = ((f.sm_read >> uint(old)) & 1u) != 0u;
    float v = 1.0;
    if (rc && ro) v = mix(sm_vis_set(old, pos, n, depth), sm_vis_set(cur, pos, n, depth), f.sm_mix);
    else if (rc || ro) v = sm_vis_set(rc ? cur : old, pos, n, depth);
    // Дальний край: тень сеток гаснет в последней пятой дальности (дальше — только карта горизонта). По
    // расстоянию, а не по глубине: поворот камеры стык не двигает
    return mix(v, 1.0, smoothstep(0.8 * f.sm_far, f.sm_far, dist));
}

// Режим F3 «страницы теней»: страница точки в показанном наборе (новом, если он уже виден) — нарисована в этом
// кадре (красная), из кеша (зелёная), нет места или не заявлена (пурпурная); край страницы — тёмная линия,
// уровень — оттенок. Пока новый набор строится в фоне или перетекает — синеватый оттенок
vec3 sm_debug_tint(vec3 c, vec3 pos) {
    FrameData f = pc.frame;
    if (f.sm_on == 0u) return c;
    int cur = int(f.sm_cur);
    int set = ((f.sm_read >> uint(cur)) & 1u) != 0u ? cur : 1 - cur;
    float depth = dot(pos, f.forward.xyz);
    vec2 ru = sm_ru(pos, set);
    int lv = sm_level(depth, max(abs(ru.x), abs(ru.y)));
    if (lv >= SM_LEVELS || length(pos) > f.sm_far) return c;
    int k = set * SM_LEVELS + lv;
    ivec2 t = ivec2(floor(sm_window(pos, k)));
    int phys = sm_find(k, f.sm_base[k] + (t >> 7));
    vec3 tint = phys < 0 ? vec3(1.0, 0.1, 1.0)
              : f.sm_pages.v[phys].made == f.sm_frame ? vec3(1.0, 0.15, 0.1) : vec3(0.15, 0.9, 0.2);
    tint *= 0.75 + 0.25 * float(lv & 1);
    if (f.sm_bg != 0u || f.sm_read == 3u) tint = mix(tint, vec3(0.2, 0.4, 1.0), 0.35);
    ivec2 e = t & 127;
    if (min(e.x, e.y) < 2 || max(e.x, e.y) > 125) tint *= 0.25;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return tint * l * 0.6 + c * 0.4;
}
#endif
