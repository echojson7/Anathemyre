// Шов «материал ↔ свет» (docs/invariants.md, шов 6): материал отдаёт Surface, свет её освещает
// (light_surface() в light.glsl). Узор считается в мировых координатах (без UV), раз на ячейку правила
// пиксельности (docs/render.md). Виды материалов — data/shaders/materials/<вид>.glsl, функция surf_<вид>
#ifndef SURFACE_GLSL
#define SURFACE_GLSL
#include "common.glsl"

struct Surface {
    vec3 albedo;    // линейный
    float rough;
    vec3 normal;    // мировая
    float ao;       // 1 — без затенения
    vec3 pos;       // относительно камеры, центр ячейки
    vec3 emissive;
    float height;   // высота узора 0..1: на стыке слоёв земли побеждает выступающий (docs/terrain.md)
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
    ivec2 ch = pc.frame.cam_chunk * per_chunk;
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

// Точка в центр квадрата size: узор как текстура низкого разрешения
vec3 snap(vec3 p, float size) { return (floor(p / size) + 0.5) * size; }

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
