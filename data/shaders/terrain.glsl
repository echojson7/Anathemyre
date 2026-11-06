// Земля на GPU (docs/terrain.md): та же целая функция узла решётки 1 м, что terrain_node() в
// src/sim/terrain.cpp, — ноги, тени и картинка видят одну землю. Только 32-битные целые операции;
// октавы деталей (период, размах, зерно) кадр получает из таблицы CPU, а не из чисел в шейдере.
// Ниже — маски, наклон сетки 8 м и луч к солнцу по сетке высот (тень рельефа временного света)
#ifndef TERRAIN_GLSL
#define TERRAIN_GLSL
#include "common.glsl"

uint t_hash(uint v) {
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
uint t_hash2(int x, int z, uint seed) { return t_hash(uint(x) ^ t_hash(uint(z) ^ t_hash(seed))); }

// u16 сетки высот и уровня воды — парами в словах u32, маски — словом
int t_hgt(int k) { return int((pc.frame.t_height.v[k >> 1] >> uint((k & 1) * 16)) & 0xFFFFu); }
uint t_msk(int k) { return pc.frame.t_mask.v[k]; }
int t_wat(int k) { return int((pc.frame.t_water.v[k >> 1] >> uint((k & 1) * 16)) & 0xFFFFu); }

int t_detail_noise(int gx, int gz, int lp, uint seed, int amp) {
    int p = 1 << lp;
    int cx = gx >> lp, cz = gz >> lp;
    int fx = gx & (p - 1), fz = gz & (p - 1);
    int wx = ((fx * fx * (3 * p - 2 * fx)) << 8) >> (3 * lp);
    int wz = ((fz * fz * (3 * p - 2 * fz)) << 8) >> (3 * lp);
    int v00 = int(t_hash2(cx, cz, seed) >> 16) - 32768;
    int v10 = int(t_hash2(cx + 1, cz, seed) >> 16) - 32768;
    int v01 = int(t_hash2(cx, cz + 1, seed) >> 16) - 32768;
    int v11 = int(t_hash2(cx + 1, cz + 1, seed) >> 16) - 32768;
    int a = v00 + (((v10 - v00) * wx) >> 8);
    int b = v01 + (((v11 - v01) * wx) >> 8);
    int v = a + (((b - a) * wz) >> 8);
    return (v * amp) >> 15;
}

// Клетка масок 32 м вокруг узла и место в ней (0..32), как mask_cell() в terrain.cpp
void t_mask_cell(int gx, int gz, out int k, out int ux, out int uz) {
    FrameData f = pc.frame;
    gx = clamp(gx, 0, f.t_size);
    gz = clamp(gz, 0, f.t_size);
    int mx = gx >> 5, mz = gz >> 5;
    ux = gx & 31;
    uz = gz & 31;
    if (mx >= f.t_nm - 1) { mx = f.t_nm - 2; ux = 32; }
    if (mz >= f.t_nm - 1) { mz = f.t_nm - 2; uz = 32; }
    k = mz * f.t_nm + mx;
}

ivec4 t_unpack(uint m) { return ivec4(m & 255u, (m >> 8) & 255u, (m >> 16) & 255u, m >> 24); }

// Маски в узле: шероховатость, песок, трава, сырость (0..255), как terrain_masks()
ivec4 t_masks(int gx, int gz) {
    int k, ux, uz;
    t_mask_cell(gx, gz, k, ux, uz);
    int nm = pc.frame.t_nm;
    ivec4 a = t_unpack(t_msk(k)), b = t_unpack(t_msk(k + 1));
    ivec4 c = t_unpack(t_msk(k + nm)), d = t_unpack(t_msk(k + nm + 1));
    return ((a * (32 - ux) + b * ux) * (32 - uz) + (c * (32 - ux) + d * ux) * uz) >> 10;
}

// Уровень воды в узле, единицы 1/1280 м; 0 — воды нет. Как terrain_water(): у края пустые узлы — среднее
// непустых минус 1.5 м
int t_water(int gx, int gz) {
    int k, ux, uz;
    t_mask_cell(gx, gz, k, ux, uz);
    int nm = pc.frame.t_nm;
    ivec4 w = ivec4(t_wat(k), t_wat(k + 1), t_wat(k + nm), t_wat(k + nm + 1));
    if (w == ivec4(0)) return 0;
    ivec4 has = ivec4(notEqual(w, ivec4(0)));
    int s = max((w.x + w.y + w.z + w.w) / (has.x + has.y + has.z + has.w) - 30, 1);
    w = w + (1 - has) * s;
    return ((w.x * (32 - ux) + w.y * ux) * (32 - uz) + (w.z * (32 - ux) + w.w * ux) * uz) / 16;
}

// Уровень воды в точке (метры от начала чанка камеры), метры; < −1000 — воды нет. Плавно между узлами 1 м
float t_water_at(vec2 p) {
    FrameData f = pc.frame;
    vec2 g = p + vec2((f.cam_chunk - ivec2(f.t_chunk0)) * 256);
    ivec2 gi = ivec2(floor(g));
    int w = t_water(gi.x, gi.y);
    return w == 0 ? -1e4 : float(w) * (1.0 / 1280.0);
}

// Слой деталей в узле (уже зажатом в карту), единицы 1/1280 м; октавы мельче 2^min_lp м отброшены
int t_detail(int gx, int gz, int min_lp) {
    FrameData f = pc.frame;
    int rough = t_masks(gx, gz).x;
    if (rough == 0) return 0;
    int d = 0;
    for (int o = 0; o < 5; ++o)
        if (f.t_oct_lp[o] >= min_lp) d += t_detail_noise(gx, gz, f.t_oct_lp[o], f.t_oct_seed[o], f.t_oct_amp[o]);
    return (d * rough) >> 8;
}

// Высота узла решётки 1 м, единицы 1/1280 м. Октавы с периодом меньше 2^min_lp м отброшены (дальние кольца)
int t_node(int gx, int gz, int min_lp) {
    FrameData f = pc.frame;
    gx = clamp(gx, 0, f.t_size);
    gz = clamp(gz, 0, f.t_size);
    int bx = gx >> 3, fx = gx & 7, bz = gz >> 3, fz = gz & 7;
    if (bx >= f.t_n - 1) { bx = f.t_n - 2; fx = 8; }
    if (bz >= f.t_n - 1) { bz = f.t_n - 2; fz = 8; }
    int k = bz * f.t_n + bx;
    int h00 = t_hgt(k), h10 = t_hgt(k + 1);
    int h01 = t_hgt(k + f.t_n), h11 = t_hgt(k + f.t_n + 1);
    int base = (h00 * (8 - fx) + h10 * fx) * (8 - fz) + (h01 * (8 - fx) + h11 * fx) * fz;
    return base + t_detail(gx, gz, min_lp);
}

const float T_M_PER_U = 1.0 / 1280.0;

// Метры от начала чанка камеры → целый узел карты (метры от угла) и доля внутри клетки 1 м
void t_split(vec2 p, out ivec2 g, out vec2 fr) {
    FrameData f = pc.frame;
    vec2 fl = floor(p);
    g = ivec2(fl) + (f.cam_chunk - ivec2(f.t_chunk0)) * 256;
    fr = p - fl;
}

// Плавная нормаль для картинки: наклон сетки 8 м — центральными разностями в узлах, между узлами билинейно
// (у билинейной сетки наклон скачет на краях клеток 8 м — склоны покрывались бы квадратами), плюс наклон
// слоя деталей по клетке 1 м (ближние кольца). p — метры от начала чанка камеры. ground_at() берёт нормаль
// билинейной клетки 1 м: разница только в оттенке склона, высоты те же
vec3 t_smooth_normal(vec2 p, bool detail) {
    FrameData f = pc.frame;
    vec2 g = p + vec2((f.cam_chunk - ivec2(f.t_chunk0)) * 256);
    vec2 b = clamp(g, vec2(8.0), vec2(float(f.t_size) - 8.001)) / 8.0;
    ivec2 i = ivec2(floor(b));
    vec2 fr = b - vec2(i);
    vec2 gr[4];
    for (int k = 0; k < 4; ++k) {
        ivec2 c = i + ivec2(k & 1, k >> 1);
        int kk = c.y * f.t_n + c.x;
        float hx = float(t_hgt(kk + 1) - t_hgt(kk - 1));
        float hz = float(t_hgt(kk + f.t_n) - t_hgt(kk - f.t_n));
        gr[k] = vec2(hx, hz) * (0.05 / 16.0);
    }
    vec2 gs = mix(mix(gr[0], gr[1], fr.x), mix(gr[2], gr[3], fr.x), fr.y);
    if (detail) {
        ivec2 gi = ivec2(floor(g));
        vec2 ff = g - vec2(gi);
        gi = clamp(gi, ivec2(0), ivec2(f.t_size - 1));
        float d00 = float(t_detail(gi.x, gi.y, 0)), d10 = float(t_detail(gi.x + 1, gi.y, 0));
        float d01 = float(t_detail(gi.x, gi.y + 1, 0)), d11 = float(t_detail(gi.x + 1, gi.y + 1, 0));
        gs += vec2(mix(d10 - d00, d11 - d01, ff.y), mix(d01 - d00, d11 - d10, ff.x)) * T_M_PER_U;
    }
    return normalize(vec3(-gs.x, 1.0, -gs.y));
}

// Сетка 8 м билинейно (без деталей), метры. g — метры карты (float), за краем — край
float t_base_height(vec2 g) {
    FrameData f = pc.frame;
    vec2 b = clamp(g, vec2(0.0), vec2(float(f.t_size) - 0.001)) / 8.0;
    ivec2 i = ivec2(floor(b));
    vec2 fr = b - vec2(i);
    int k = i.y * f.t_n + i.x;
    float h00 = float(t_hgt(k)), h10 = float(t_hgt(k + 1));
    float h01 = float(t_hgt(k + f.t_n)), h11 = float(t_hgt(k + f.t_n + 1));
    return mix(mix(h00, h10, fr.x), mix(h01, h11, fr.x), fr.y) * 0.05;
}

// Наклон сетки 8 м (тангенс): разности через 8 м по билинейной сетке, как terrain_base_slope() (там — в узле)
float t_base_slope(vec2 g) {
    float dx = t_base_height(g + vec2(4.0, 0.0)) - t_base_height(g - vec2(4.0, 0.0));
    float dz = t_base_height(g + vec2(0.0, 4.0)) - t_base_height(g - vec2(0.0, 4.0));
    return max(abs(dx), abs(dz)) / 8.0;
}

// Тень рельефа: луч от точки к солнцу по сетке 8 м, шаги растут до ~12 км. 1 — свет, 0 — тень.
// Детали ±1 м сетка не видит: запас bias, иначе точка в ямке деталей затеняет сама себя
float t_shadow(vec3 rel, vec3 l) {
    FrameData f = pc.frame;
    if (f.t_on == 0u || l.y <= 0.0) return 1.0;
    vec3 p = f.cam_off + rel;
    vec2 g0 = p.xz + vec2((f.cam_chunk - ivec2(f.t_chunk0)) * 256);
    float y0 = p.y;
    float lit = 1.0;
    float t = 3.0;
    for (int k = 0; k < 44; ++k) {
        vec3 q = vec3(g0.x, y0, g0.y) + l * t;
        if (q.y > 3300.0) break;
        float h = t_base_height(q.xz);
        float bias = 1.2 + t * 0.002;
        lit = min(lit, clamp((q.y - h + bias) / (t * 0.035 + 0.3), 0.0, 1.0));
        if (lit <= 0.0) break;
        t *= 1.2;
    }
    return lit;
}
#endif
