// Шов «материал ↔ свет» (docs/invariants.md, шов 6): материал отдаёт Surface, свет её освещает
// (light_surface() в light.glsl). Узор считается в мировых координатах (без UV), раз на ячейку правила
// пиксельности (docs/render.md). Виды материалов — data/shaders/materials/<вид>.glsl, функция surf_<вид>
#ifndef SURFACE_GLSL
#define SURFACE_GLSL
#include "common.glsl"

struct Surface {
    vec3 albedo;    // линейный; у металла — цвет отражения (F0)
    float rough;    // воспринимаемая шероховатость, α = rough² (docs/light.md)
    vec3 normal;    // мировая
    float ao;       // 1 — без затенения
    vec3 pos;       // относительно камеры, центр ячейки
    vec3 emissive;
    float height;   // высота узора 0..1: на стыке слоёв земли побеждает выступающий (docs/terrain.md)
    float metal;    // где виден металл: материал отдаёт долю (ржавчина и краска — 0), surface() умножает на metal таблицы
    float nvar;     // разброс наклона узора, погашенного мельче пикселя (дисперсия по оси): surface() переводит
                    // его в шероховатость (Токсвиг), свет его не видит
};

// Вход вида материала
struct SurfIn {
    vec3 p;         // точка мира: метры от начала чанка камеры (x, z), высота (y); центр ячейки
    vec3 n;         // нормаль
    float cell;     // размер ячейки, м (степень двойки — ступенями)
    float foot;     // пиксель кадра на этой глубине, м (плавно): гасить узор мельче пикселя без ступенек
    uint seed;      // зерно экземпляра
    Material m;
};

// Целочисленный хеш (PCG) → [0, 1). Одинаков на всех GPU, узор не плывёт
uint hash_u(uint v) {
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
float hash3(ivec3 c, uint seed) {
    uint h = hash_u(uint(c.x) ^ hash_u(uint(c.y) ^ hash_u(uint(c.z) ^ hash_u(seed))));
    return float(h >> 8) * (1.0 / 16777216.0);
}

// Номер квадрата узора размера size вокруг точки — в мировых целых: чанк камеры × квадратов на чанк + место.
// size должен делить 256 м нацело (1/10, 1/8, 1/2, 1, 2 м…), иначе узор сдвинется на границе чанка
ivec3 world_cell(vec3 p, float size) {
    int per_chunk = int(CHUNK_M / size + 0.5);
    ivec2 ch = ORIGIN_CHUNK * per_chunk;
    ivec3 c = ivec3(floor(p / size));
    return c + ivec3(ch.x, 0, ch.y);
}
float cell_noise(vec3 p, float size, uint seed) { return hash3(world_cell(p, size), seed); }

// Плавный шум по решётке size (трилинейно между узлами)
float value_noise(vec3 p, float size, uint seed) {
    ivec3 c = world_cell(p, size);
    vec3 f = fract(p / size);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash3(c, seed), n100 = hash3(c + ivec3(1, 0, 0), seed);
    float n010 = hash3(c + ivec3(0, 1, 0), seed), n110 = hash3(c + ivec3(1, 1, 0), seed);
    float n001 = hash3(c + ivec3(0, 0, 1), seed), n101 = hash3(c + ivec3(1, 0, 1), seed);
    float n011 = hash3(c + ivec3(0, 1, 1), seed), n111 = hash3(c + ivec3(1, 1, 1), seed);
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

// Плавный шум значений по плоскости в единицах решётки (x — уже мировые метры / шаг; вызывающий сам отвечает за
// то, чтобы x не зависел от чанка камеры). Для узоров, вытянутых по своему направлению (пряди травы, полосы ветра)
float value_noise2(vec2 x, uint seed) {
    vec2 c = floor(x), f = x - c;
    f = f * f * (3.0 - 2.0 * f);
    ivec3 k = ivec3(int(c.x), 0, int(c.y));
    float a = hash3(k, seed), b = hash3(k + ivec3(1, 0, 0), seed);
    float d = hash3(k + ivec3(0, 0, 1), seed), e = hash3(k + ivec3(1, 0, 1), seed);
    return mix(mix(a, b, f.x), mix(d, e, f.x), f.y);
}

// Градиентный шум по плоскости xz на решётке size → [0, 1]. Пятна круглые, без квадратов шума значений
float grad_noise(vec3 p, float size, uint seed) {
    ivec3 c = world_cell(vec3(p.x, 0.0, p.z), size);
    vec2 f = fract(p.xz / size);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float r00 = hash3(c, seed) * 6.2831853, r10 = hash3(c + ivec3(1, 0, 0), seed) * 6.2831853;
    float r01 = hash3(c + ivec3(0, 0, 1), seed) * 6.2831853, r11 = hash3(c + ivec3(1, 0, 1), seed) * 6.2831853;
    float n00 = dot(vec2(cos(r00), sin(r00)), f);
    float n10 = dot(vec2(cos(r10), sin(r10)), f - vec2(1.0, 0.0));
    float n01 = dot(vec2(cos(r01), sin(r01)), f - vec2(0.0, 1.0));
    float n11 = dot(vec2(cos(r11), sin(r11)), f - vec2(1.0, 1.0));
    return clamp(0.5 + 0.75 * mix(mix(n00, n10, u.x), mix(n01, n11, u.x), u.y), 0.0, 1.0);
}

// Сумма октав градиентного шума: size, size/2… (size — степень двойки), вес вдвое меньше. [0, 1]
float fbm_n(vec3 p, float size, int octaves, uint seed) {
    float s = 0.0, a = 1.0, norm = 0.0;
    for (int k = 0; k < octaves; ++k) {
        s += grad_noise(p, size, seed + uint(k) * 101u) * a;
        norm += a;
        a *= 0.5;
        size *= 0.5;
    }
    return s / norm;
}

// Точка в центр квадрата size (привязка к ячейке правила, F3 «ячейки»; узор материалов ею не пользуется)
vec3 snap(vec3 p, float size) { return (floor(p / size) + 0.5) * size; }

// Узор свет считает на пиксель (docs/render.md, «На пиксель»): квадратов узора нет, всё плавное, а деталь мельче
// пикселя гаснет к среднему по следу пикселя foot (м) — иначе она рябит, и никакое сглаживание её не спасёт.
// Доля, с которой виден узор размера size: 1 — крупнее пикселя в четыре раза, 0 — с пиксель и мельче
float px_fade(float size, float foot) { return 1.0 - smoothstep(size * 0.25, size, foot); }

// Зерно без среднего, около −0.5..0.5: плавный шум на решётке size и вдвое мельче, каждая октава гаснет сама.
// Разброс — как у прежнего зерна квадратами (×1.6: у плавного шума он меньше, чем у шума ячеек)
float grain_noise(vec3 p, float size, float foot, uint seed) {
    float a = (value_noise(p, size, seed) - 0.5) * px_fade(size, foot);
    float b = (value_noise(p, size * 0.5, seed + 7u) - 0.5) * px_fade(size * 0.5, foot);
    return (a + b * 0.7) * 1.6;
}

// Плавная ступень на краю edge, ширина — пиксель: x меняется на dx за метр (сколько единиц x на метр). Край
// пятна, порога шума или линии без лесенки и без мерцания
float aa_step(float edge, float x, float dx, float foot) {
    float w = max(dx * foot, 1e-4);
    return smoothstep(edge - w, edge + w, x);
}

// Редкие круглые пятна (поры, камешки, крупинки): в квадрате size на плоскости грани (поперёк главной оси нормали n,
// трипланарно) с вероятностью dens — круг радиуса до r м в случайном месте. 0..1 с краем в пиксель; пятно мельче
// пикселя гаснет (в средний цвет вносит мало, а рябит сильно). Круг не выходит из своего квадрата — соседей не нужно
float spot_noise(vec3 p, vec3 n, float size, float dens, float r, float foot, uint seed) {
    vec3 an = abs(n);
    vec3 mask = an.x >= an.y && an.x >= an.z ? vec3(0.0, 1.0, 1.0) : an.y >= an.z ? vec3(1.0, 0.0, 1.0) : vec3(1.0, 1.0, 0.0);
    ivec3 c = world_cell(p, size) * ivec3(mask);
    if (hash3(c, seed) >= dens) return 0.0;
    float m = min(r / size, 0.45);
    vec3 ctr = (vec3(hash3(c, seed + 1u), hash3(c, seed + 2u), hash3(c, seed + 3u)) * (1.0 - 2.0 * m) + m) * size;
    float rad = r * (0.6 + 0.4 * hash3(c, seed + 4u));
    float d = length((fract(p / size) * size - ctr) * mask);
    return (1.0 - aa_step(rad, d, 1.0, foot)) * px_fade(rad * 2.0, foot);
}

// Линия ширины w (м): d — расстояние до её середины, м. Доля линии в пикселе; линия тоньше пикселя бледнеет по
// своей доле в нём, а не пропадает и не рябит
float aa_line(float d, float w, float foot) {
    float hw = w * 0.5;
    return (1.0 - aa_step(hw, abs(d), 1.0, foot)) * min(1.0, w / max(foot, 1e-6));
}

// Против искр без TAA (docs/light.md, «Искры»): узор, погашенный мельче пикселя, не пропадает, а уходит в
// шероховатость — блик ячейки шире на столько, сколько нормалей она усреднила (Токсвиг, Капланян 2016):
// α'² = α² + 2σ². Предел 0.18 — как у Капланяна: дальняя гладь не становится мелом
float surf_toksvig(float rough, float nvar) {
    float a = rough * rough;
    return sqrt(sqrt(a * a + min(2.0 * max(nvar, 0.0), 0.18)));
}

#include "materials.glsl"

Surface surface(uint id, vec3 p, vec3 n, float cell, float foot, uint seed) {
    SurfIn i;
    i.p = p;
    i.n = n;
    i.cell = cell;
    i.foot = foot;
    i.seed = seed;
    i.m = pc.frame.materials.v[id];
    Surface s = surface_kind(i.m.kind, i);
    s.normal = normalize(s.normal);
    s.metal = clamp(s.metal * uintBitsToFloat(i.m.pad0), 0.0, 1.0);
    s.rough = surf_toksvig(s.rough, s.nvar);
    s.nvar = 0.0;
    s.pos = vec3(0.0);
    return s;
}

// Правило ячейки: не мельче cell_min, растёт степенями двойки, пока не станет cell_pixels пикселей кадра.
// Одно правило на кадр, только от глубины (visibility buffer этапа 5 считает так же, без производных)
float cell_size(float depth) {
    FrameData f = pc.frame;
    float foot = max(depth, 0.0) * f.pixel_angle * f.cell_pixels;
    return f.cell_min * exp2(ceil(log2(max(foot / f.cell_min, 1.0))));
}

// Привязка точки к центру ячейки в плоскости, поперёк главной оси нормали (трипланарно). Вдоль главной оси
// точка уходит на 2 мм внутрь: грань, лежащая ровно на границе решётки узора (верх куба на 2 м), иначе
// берёт то верхний, то нижний слой решётки по ошибке интерполяции и покрывается диагоналями
vec3 cell_snap(vec3 p, vec3 n, float cell) {
    vec3 a = abs(n);
    vec3 s = snap(p, cell);
    const float IN = 0.002;
    if (a.x >= a.y && a.x >= a.z) return vec3(p.x - sign(n.x) * IN, s.y, s.z);
    if (a.y >= a.z) return vec3(s.x, p.y - sign(n.y) * IN, s.z);
    return vec3(s.x, s.y, p.z - sign(n.z) * IN);
}
#endif
