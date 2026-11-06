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
uint t_msk2(int k) { return pc.frame.t_mask2.v[k]; }
int t_dun(int k) { return int((pc.frame.t_dune.v[k >> 1] >> uint((k & 1) * 16)) & 0xFFFFu); }

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
    gx = clamp(gx, 0, f.t_size_x);
    gz = clamp(gz, 0, f.t_size_z);
    int mx = gx >> 5, mz = gz >> 5;
    ux = gx & 31;
    uz = gz & 31;
    if (mx >= f.t_nmx - 1) { mx = f.t_nmx - 2; ux = 32; }
    if (mz >= f.t_nmz - 1) { mz = f.t_nmz - 2; uz = 32; }
    k = mz * f.t_nmx + mx;
}

ivec4 t_unpack(uint m) { return ivec4(m & 255u, (m >> 8) & 255u, (m >> 16) & 255u, m >> 24); }

// Маски в узле: шероховатость, песок, трава, сырость (0..255), как terrain_masks()
ivec4 t_masks(int gx, int gz) {
    int k, ux, uz;
    t_mask_cell(gx, gz, k, ux, uz);
    int nm = pc.frame.t_nmx;
    ivec4 a = t_unpack(t_msk(k)), b = t_unpack(t_msk(k + 1));
    ivec4 c = t_unpack(t_msk(k + nm)), d = t_unpack(t_msk(k + nm + 1));
    return ((a * (32 - ux) + b * ux) * (32 - uz) + (c * (32 - ux) + d * ux) * uz) >> 10;
}

// Вторые маски в узле: сила дюн, пустыня (0..255), как terrain_masks2()
ivec2 t_masks2(int gx, int gz) {
    int k, ux, uz;
    t_mask_cell(gx, gz, k, ux, uz);
    int nm = pc.frame.t_nmx;
    ivec2 a = t_unpack(t_msk2(k)).xy, b = t_unpack(t_msk2(k + 1)).xy;
    ivec2 c = t_unpack(t_msk2(k + nm)).xy, d = t_unpack(t_msk2(k + nm + 1)).xy;
    return ((a * (32 - ux) + b * ux) * (32 - uz) + (c * (32 - ux) + d * ux) * uz) >> 10;
}

// Уровень воды в узле, единицы 1/1280 м; 0 — воды нет. Как terrain_water(): у края пустые узлы — среднее
// непустых минус 1.5 м
int t_water(int gx, int gz) {
    int k, ux, uz;
    t_mask_cell(gx, gz, k, ux, uz);
    int nm = pc.frame.t_nmx;
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

// Дюны — как terrain_dune() в terrain.cpp (docs/terrain.md, «Дюны»), бит в бит
uint dune_hash(int x, int z, uint seed) { return t_hash((uint(x) * 0x8DA6B343u) ^ (uint(z) * 0xD8163841u) ^ seed); }

int dune_noise(int gx, int gz, int lp, uint seed) {
    int m = (1 << lp) - 1;
    int cx = gx >> lp, cz = gz >> lp;
    int tx = ((gx & m) << 12) >> lp, tz = ((gz & m) << 12) >> lp;   // Q12: точно в узлах 1 м при lp ≤ 12
    int wx = (((tx * tx) >> 12) * (12288 - 2 * tx)) >> 12, wz = (((tz * tz) >> 12) * (12288 - 2 * tz)) >> 12;
    int v00 = int(dune_hash(cx, cz, seed) >> 16) - 32768;
    int v10 = int(dune_hash(cx + 1, cz, seed) >> 16) - 32768;
    int v01 = int(dune_hash(cx, cz + 1, seed) >> 16) - 32768;
    int v11 = int(dune_hash(cx + 1, cz + 1, seed) >> 16) - 32768;
    int a = v00 + (((v10 - v00) * wx) >> 12);
    int b = v01 + (((v11 - v01) * wx) >> 12);
    return a + (((b - a) * wz) >> 12);
}

const int DUNE_R = 80;
const int DUNE_R2 = 96;

int dune_profile_q12(int p, int lam, int lee, out int windward) {
    int c = lam - lee;
    windward = p < c ? (p << 12) / c : 0;
    int dx = p - c;
    if (dx > -DUNE_R && dx < DUNE_R) {
        int e = (dx + DUNE_R) * (dx + DUNE_R);
        return 4096 + (3277 * dx) / c - (5530 * e) / (lee * 4 * DUNE_R) - (3277 * e) / (c * 4 * DUNE_R);
    }
    int dt = p < DUNE_R2 ? p : (p > lam - DUNE_R2 ? p - lam : DUNE_R2);
    if (dt < DUNE_R2) {
        int e = (dt + DUNE_R2) * (dt + DUNE_R2);
        return -(2662 * dt) / lee + (2662 * e) / (lee * 4 * DUNE_R2);
    }
    if (p < c) {
        int t = (p << 12) / c;
        int t2 = (t * t) >> 12, t3 = (t2 * t) >> 12;
        return (6 * (3 * t2 - 2 * t3) + 4 * t2) / 10;
    }
    int s = ((p - c) << 12) / lee;
    return ((4096 - s) * (4096 - (s * 35) / 100)) >> 12;
}

const int DUNE_LAM = 320 * 16;        // волна больших дюн, Q4 м (320 м)
const int DUNE_H = 56 * 1280;         // наибольшая высота, единицы 1/1280 м
const int DUNE_ARC = 560 * 16;        // ширина серпа вдоль гребня, Q4 м
const int DUNE_ARC2 = 880 * 16;       // второй, шире: смесь двух рядов серпов не стоит строем
const int DUNE_ARC_A = 140 * 16;      // насколько середина серпа выдвинута против ветра, Q4 м
const int DUNE2_LAM = 90 * 16;        // мелкие дюны на наветренных склонах
const int DUNE2_H = 4 * 1280;         // 4 м

int t_dune(int gx, int gz, int amp) {
    if (amp == 0) return 0;
    uint seed = pc.frame.t_dune_seed;
    // Вдоль ветра (24, 7)/25 и поперёк, Q4 м. Искривление: 1 км — извилистость гребней, 256 и 64 м — рваность
    int u = ((gx * 24 - gz * 7) * 16) / 25;
    int v = ((gz * 24 + gx * 7) * 16) / 25;
    int w = ((dune_noise(gx, gz, 10, seed) * 90 * 16) >> 15) + ((dune_noise(gx, gz, 8, seed + 1u) * 22 * 16) >> 15) +
            ((dune_noise(gx, gz, 6, seed + 2u) * 4 * 16) >> 15);
    // Серпы: вдоль гребня — дуги, середина выдвинута против ветра, рога (стыки дуг) — по ветру и ниже. Дуги
    // соседних рядов сдвинуты шумом, чтобы не стояли строем
    int vs = v + ((dune_noise(gx, gz, 9, seed + 6u) * (DUNE_ARC * 4 / 10)) >> 15) + DUNE_ARC2 * 1024;
    int s = ((vs % DUNE_ARC) * 8192) / DUNE_ARC - 4096;   // −1…1, Q12
    int arc = 4096 - ((s * s) >> 12);
    arc = (arc * arc) >> 12;   // (1 − s²)²: 1 в середине серпа, 0 у рогов, без излома на стыке серпов
    s = ((vs % DUNE_ARC2) * 8192) / DUNE_ARC2 - 4096;
    int arc2 = 4096 - ((s * s) >> 12);
    arc2 = (arc2 * arc2) >> 12;
    int am = dune_noise(gx, gz, 10, seed + 7u);
    am = am < -12000 ? 0 : (am > 12000 ? 4096 : ((am + 12000) * 4096) / 24000);
    arc += ((arc2 - arc) * am) >> 12;
    // Пятна, сдвинутые на полволны: на их краю гребень переходит к соседнему — «игрек», обрыв гребня
    int sel = dune_noise(gx, gz, 10, seed + 3u);
    int st = sel < -20000 ? 0 : (sel > 20000 ? 4096 : ((sel + 20000) * 4096) / 40000);
    st = (((st * st) >> 12) * (12288 - 2 * st)) >> 12;
    int p = (u + w + ((st * DUNE_LAM) >> 13) + ((DUNE_ARC_A * arc) >> 12) + DUNE_LAM * 64) % DUNE_LAM;  // > 0: % одинаков у C++ и GLSL
    // Высота: по маске, к рогам ниже (0.45…1) и шумом вдоль гребня (0.4…1)
    int hn = dune_noise(gx, gz, 8, seed + 4u) + 32768;   // 0..65535
    int h = (DUNE_H * amp) / 255;
    h = ((h >> 4) * (1843 + ((arc * 2253) >> 12))) >> 8;
    h = ((h >> 4) * (26214 + ((hn * 9830) >> 14))) >> 12;
    // Склон осыпания ~32° (тангенс 0.62): длина = высота / 0.62, Q4 м
    int lee = (h * 16 * 100) / (1280 * 62);
    lee = clamp(lee, DUNE_LAM / 12, DUNE_LAM * 2 / 5);
    int ww;
    int d = (h * dune_profile_q12(p, DUNE_LAM, lee, ww)) >> 12;
    // Мелкие дюны: ветер с запада-северо-запада, только на наветренном склоне (у подошвы и бровки гаснут)
    if (ww > 0) {
        int u2 = ((gx * 20 + gz * 15) * 16) / 25 + ((dune_noise(gx, gz, 7, seed + 5u) * 18 * 16) >> 15) +
                 ((dune_noise(gx, gz, 5, seed + 8u) * 4 * 16) >> 15);
        int p2 = (u2 + DUNE2_LAM * 512) % DUNE2_LAM;
        int h2 = (DUNE2_H * amp) / 255;
        h2 = (h2 * (6554 + ((dune_noise(gx, gz, 8, seed + 9u) + 32768) >> 2))) >> 14;  // пятнами 0.4…1.4
        int lee2 = (h2 * 16 * 100) / (1280 * 62);
        lee2 = max(lee2, DUNE2_LAM * 3 / 10);
        int wf = (ww * (4096 - ww)) >> 10;   // 4t(1 − t), Q12
        int w2;
        d += (((h2 * dune_profile_q12(p2, DUNE2_LAM, lee2, w2)) >> 12) * wf) >> 12;
    }
    return d;
}

// Дюны во float в любой точке g (метры карты), м: та же функция, что t_dune(), но без целых ступеней узла 1 м.
// Только для нормали картинки: наклон по клетке 1 м менялся бы клетками, и граница света и тени на гребне шла бы
// зубцами 1 м. Высоты (вершины, ноги) — целые t_dune()
float dune_noise_f(vec2 g, int lp, uint seed) {
    float per = float(1 << lp);
    vec2 c = floor(g / per);
    vec2 t = g / per - c;
    t = t * t * (3.0 - 2.0 * t);
    ivec2 ic = ivec2(c);
    float v00 = float(dune_hash(ic.x, ic.y, seed) >> 16), v10 = float(dune_hash(ic.x + 1, ic.y, seed) >> 16);
    float v01 = float(dune_hash(ic.x, ic.y + 1, seed) >> 16), v11 = float(dune_hash(ic.x + 1, ic.y + 1, seed) >> 16);
    return mix(mix(v00, v10, t.x), mix(v01, v11, t.x), t.y) * (1.0 / 32768.0) - 1.0;
}

float dune_profile_f(float p, float lam, float lee, out float windward) {
    const float R = 5.0, R2 = 6.0;
    float c = lam - lee;
    windward = p < c ? p / c : 0.0;
    float dx = p - c;
    if (abs(dx) < R) {
        float e = (dx + R) * (dx + R);
        return 1.0 + 0.8 * dx / c - 1.35 * e / (lee * 4.0 * R) - 0.8 * e / (c * 4.0 * R);
    }
    float dt = p < R2 ? p : (p > lam - R2 ? p - lam : R2);
    if (dt < R2) return -0.65 * dt / lee + 0.65 * (dt + R2) * (dt + R2) / (lee * 4.0 * R2);
    if (p < c) {
        float t = p / c;
        return 0.6 * t * t * (3.0 - 2.0 * t) + 0.4 * t * t;
    }
    float s = (p - c) / lee;
    return (1.0 - s) * (1.0 - 0.35 * s);
}

float t_dune_f(vec2 g, float amp) {
    if (amp <= 0.0) return 0.0;
    uint seed = pc.frame.t_dune_seed;
    const float LAM = 320.0, ARC = 560.0, ARC2 = 880.0;
    float u = dot(g, vec2(24.0, -7.0)) / 25.0;
    float v = dot(g, vec2(7.0, 24.0)) / 25.0;
    float w = dune_noise_f(g, 10, seed) * 90.0 + dune_noise_f(g, 8, seed + 1u) * 22.0 + dune_noise_f(g, 6, seed + 2u) * 4.0;
    float vs = v + dune_noise_f(g, 9, seed + 6u) * ARC * 0.4;
    float sa = 2.0 * fract(vs / ARC) - 1.0, sb = 2.0 * fract(vs / ARC2) - 1.0;
    float arc = mix((1.0 - sa * sa) * (1.0 - sa * sa), (1.0 - sb * sb) * (1.0 - sb * sb),
                    clamp((dune_noise_f(g, 10, seed + 7u) * 32768.0 + 12000.0) / 24000.0, 0.0, 1.0));
    float st = smoothstep(-20000.0 / 32768.0, 20000.0 / 32768.0, dune_noise_f(g, 10, seed + 3u));
    float p = mod(u + w + st * LAM * 0.5 + 140.0 * arc, LAM);
    float hn = dune_noise_f(g, 8, seed + 4u) * 0.5 + 0.5;
    float h = 56.0 * amp / 255.0 * (0.45 + 0.55 * arc) * (0.4 + 0.6 * hn);
    float lee = clamp(h / 0.62, LAM / 12.0, LAM * 0.4);
    float ww;
    float d = h * dune_profile_f(p, LAM, lee, ww);
    if (ww > 0.0) {
        float u2 = dot(g, vec2(20.0, 15.0)) / 25.0 + dune_noise_f(g, 7, seed + 5u) * 18.0 + dune_noise_f(g, 5, seed + 8u) * 4.0;
        float h2 = 4.0 * amp / 255.0 * (0.4 + (dune_noise_f(g, 8, seed + 9u) * 0.5 + 0.5));
        float w2;
        d += h2 * dune_profile_f(mod(u2, 90.0), 90.0, max(h2 / 0.62, 90.0 * 0.3), w2) * 4.0 * ww * (1.0 - ww);
    }
    return d;
}

// Дюны сетки 8 м билинейно во float, м (их же сетка высот уже содержит)
float t_dune_base_f(vec2 g) {
    FrameData f = pc.frame;
    vec2 b = clamp(g, vec2(0.0), vec2(f.t_size_x, f.t_size_z) - 0.001) / 8.0;
    ivec2 i = ivec2(floor(b));
    vec2 fr = b - vec2(i);
    int k = i.y * f.t_nx + i.x;
    float h00 = float(t_dun(k)), h10 = float(t_dun(k + 1));
    float h01 = float(t_dun(k + f.t_nx)), h11 = float(t_dun(k + f.t_nx + 1));
    return mix(mix(h00, h10, fr.x), mix(h01, h11, fr.x), fr.y) * (1.0 / 640.0);
}

// Сила дюн во float (билинейно из масок 32 м), 0..255
float t_dune_amp_f(vec2 g) {
    FrameData f = pc.frame;
    vec2 b = clamp(g, vec2(0.0), vec2(f.t_size_x, f.t_size_z) - 0.001) / 32.0;
    ivec2 i = ivec2(floor(b));
    vec2 fr = b - vec2(i);
    int k = i.y * f.t_nmx + i.x;
    float a00 = float(t_msk2(k) & 255u), a10 = float(t_msk2(k + 1) & 255u);
    float a01 = float(t_msk2(k + f.t_nmx) & 255u), a11 = float(t_msk2(k + f.t_nmx + 1) & 255u);
    return mix(mix(a00, a10, fr.x), mix(a01, a11, fr.x), fr.y);
}

// Клетка сетки 8 м вокруг узла (уже зажатого в карту): номер узла и место в клетке (0..8)
int t_base_cell(int gx, int gz, out int fx, out int fz) {
    FrameData f = pc.frame;
    int bx = gx >> 3, bz = gz >> 3;
    fx = gx & 7;
    fz = gz & 7;
    if (bx >= f.t_nx - 1) { bx = f.t_nx - 2; fx = 8; }
    if (bz >= f.t_nz - 1) { bz = f.t_nz - 2; fz = 8; }
    return bz * f.t_nx + bx;
}

// Поправка дюн в узле (зажатом): точная функция минус билинейная смесь её значений в узлах сетки 8 м
int t_dune_fix(int gx, int gz) {
    int fx, fz;
    int k = t_base_cell(gx, gz, fx, fz);
    int nx = pc.frame.t_nx;
    int h00 = t_dun(k), h10 = t_dun(k + 1), h01 = t_dun(k + nx), h11 = t_dun(k + nx + 1);
    int db = (h00 | h10 | h01 | h11) == 0 ? 0 : ((h00 * (8 - fx) + h10 * fx) * (8 - fz) + (h01 * (8 - fx) + h11 * fx) * fz) / 32;
    int amp = t_masks2(gx, gz).x;
    return (db | amp) == 0 ? 0 : t_dune(gx, gz, amp) - db;
}

// Высота узла решётки 1 м, единицы 1/1280 м. Октавы с периодом меньше 2^min_lp м отброшены (дальние кольца)
int t_node(int gx, int gz, int min_lp) {
    FrameData f = pc.frame;
    gx = clamp(gx, 0, f.t_size_x);
    gz = clamp(gz, 0, f.t_size_z);
    int fx, fz;
    int k = t_base_cell(gx, gz, fx, fz);
    int h00 = t_hgt(k), h10 = t_hgt(k + 1);
    int h01 = t_hgt(k + f.t_nx), h11 = t_hgt(k + f.t_nx + 1);
    int base = (h00 * (8 - fx) + h10 * fx) * (8 - fz) + (h01 * (8 - fx) + h11 * fx) * fz;
    return base + t_dune_fix(gx, gz) + t_detail(gx, gz, min_lp);
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
vec3 t_smooth_normal(vec2 p, bool detail, bool dunes) {
    FrameData f = pc.frame;
    vec2 g = p + vec2((f.cam_chunk - ivec2(f.t_chunk0)) * 256);
    vec2 b = clamp(g, vec2(8.0), vec2(f.t_size_x, f.t_size_z) - 8.001) / 8.0;
    ivec2 i = ivec2(floor(b));
    vec2 fr = b - vec2(i);
    ivec2 gi = clamp(ivec2(floor(g)), ivec2(0), ivec2(f.t_size_x, f.t_size_z) - 1);
    vec2 ff = g - vec2(gi);
    // Дюны: сетка без дюн — плавно (как всюду), а сами дюны — точным наклоном (t_dune_f): гребень и бровка
    // склона осыпания острые и не пилятся ни клетками 8 м, ни клетками 1 м
    bool dn = dunes && t_masks2(gi.x, gi.y).x > 0;
    const float kd = 1.0 / 32.0;  // дюны в узлах — единицы 1/640 м, сетка — 5 см
    vec2 gr[4];
    for (int k = 0; k < 4; ++k) {
        ivec2 c = i + ivec2(k & 1, k >> 1);
        int kk = c.y * f.t_nx + c.x;
        float hx = float(t_hgt(kk + 1) - t_hgt(kk - 1));
        float hz = float(t_hgt(kk + f.t_nx) - t_hgt(kk - f.t_nx));
        if (dn) {
            hx -= float(t_dun(kk + 1) - t_dun(kk - 1)) * kd;
            hz -= float(t_dun(kk + f.t_nx) - t_dun(kk - f.t_nx)) * kd;
        }
        gr[k] = vec2(hx, hz) * (0.05 / 16.0);
    }
    vec2 gs = mix(mix(gr[0], gr[1], fr.x), mix(gr[2], gr[3], fr.x), fr.y);
    if (dn) {
        const float E = 0.25;
        float amp = t_dune_amp_f(g);
        float dx = t_dune_f(g + vec2(E, 0.0), amp) - t_dune_f(g - vec2(E, 0.0), amp);
        float dz = t_dune_f(g + vec2(0.0, E), amp) - t_dune_f(g - vec2(0.0, E), amp);
        gs += vec2(dx, dz) / (2.0 * E);
    }
    if (detail) {
        float d00 = float(t_detail(gi.x, gi.y, 0)), d10 = float(t_detail(gi.x + 1, gi.y, 0));
        float d01 = float(t_detail(gi.x, gi.y + 1, 0)), d11 = float(t_detail(gi.x + 1, gi.y + 1, 0));
        gs += vec2(mix(d10 - d00, d11 - d01, ff.y), mix(d01 - d00, d11 - d10, ff.x)) * T_M_PER_U;
    }
    return normalize(vec3(-gs.x, 1.0, -gs.y));
}

// Сетка 8 м билинейно (без деталей), метры. g — метры карты (float), за краем — край
float t_base_height(vec2 g) {
    FrameData f = pc.frame;
    vec2 b = clamp(g, vec2(0.0), vec2(f.t_size_x, f.t_size_z) - 0.001) / 8.0;
    ivec2 i = ivec2(floor(b));
    vec2 fr = b - vec2(i);
    int k = i.y * f.t_nx + i.x;
    float h00 = float(t_hgt(k)), h10 = float(t_hgt(k + 1));
    float h01 = float(t_hgt(k + f.t_nx)), h11 = float(t_hgt(k + f.t_nx + 1));
    return mix(mix(h00, h10, fr.x), mix(h01, h11, fr.x), fr.y) * 0.05;
}

// Наклон сетки 8 м (тангенс): разности через 8 м по билинейной сетке, как terrain_base_slope() (там — в узле)
float t_base_slope(vec2 g) {
    float dx = t_base_height(g + vec2(4.0, 0.0)) - t_base_height(g - vec2(4.0, 0.0));
    float dz = t_base_height(g + vec2(0.0, 4.0)) - t_base_height(g - vec2(0.0, 4.0));
    return max(abs(dx), abs(dz)) / 8.0;
}

// Тень рельефа: луч от точки к солнцу по сетке 8 м, шаги растут до ~12 км. 1 — свет, 0 — тень.
// Детали ±1 м сетка не видит: запас bias, иначе точка в ямке деталей затеняет сама себя. В дюнах первые ~50 м
// луч видит дюны точно (t_dune_f вместо их размытой копии в сетке 8 м): гребень острый, свет протекал бы под
// бровку пятнами
float t_shadow(vec3 rel, vec3 l) {
    FrameData f = pc.frame;
    if (f.t_on == 0u || l.y <= 0.0) return 1.0;
    vec3 p = f.cam_off + rel;
    vec2 g0 = p.xz + vec2((f.cam_chunk - ivec2(f.t_chunk0)) * 256);
    float y0 = p.y;
    float lit = 1.0;
    float t = 3.0;
    ivec2 gc = clamp(ivec2(floor(g0)), ivec2(0), ivec2(f.t_size_x, f.t_size_z));
    bool dunes = t_masks2(gc.x, gc.y).x > 0;
    for (int k = 0; k < 44; ++k) {
        vec3 q = vec3(g0.x, y0, g0.y) + l * t;
        if (q.y > 3300.0) break;
        float h = t_base_height(q.xz);
        float bias = 1.2 + t * 0.002;
        if (dunes && t < 50.0) {
            h += t_dune_f(q.xz, t_dune_amp_f(q.xz)) - t_dune_base_f(q.xz);
            bias = 0.3 + t * 0.002;
        }
        lit = min(lit, clamp((q.y - h + bias) / (t * 0.035 + 0.3), 0.0, 1.0));
        if (lit <= 0.0) break;
        t *= 1.2;
    }
    return lit;
}
#endif
