// Зонды непрямого света (6г, docs/gi.md, vk_gi.cpp): сетка зондов кольцами вокруг камеры, как DDGI (Majercik и
// др. 2019), лучи — по полю расстояний мира (sdf.glsl) и карте высот земли. Зонд хранит освещённость по
// направлениям (октаэдр 6×6, rgb — E/π, a — доля неба) и расстояние до заслонок (октаэдр 8×8, среднее и
// квадрат — проверка Чебышёва против протечек сквозь стены). Номер зонда — мировой (от якоря поля), место —
// по модулю окна: сдвиг камеры зонды не двигает (правило 3 docs/light.md). Лучи зонда поворачиваются по кругу из
// GI_K поворотов (от мирового номера и номера в круге, не от кадра), освещённость показывается средним за круг:
// в покое значение зонда не меняется вовсе (правило 2)
#ifndef GI_GLSL
#define GI_GLSL
#include "sdf.glsl"
#include "terrain.glsl"
layout(set = 0, binding = 4) uniform sampler2DArray u_gi_irr;
layout(set = 0, binding = 5) uniform sampler2DArray u_gi_dep;

const int GI_LEVELS = 3, GI_NX = 32, GI_NY = 16, GI_NZ = 32;
const int GI_CAM_Y = 5;              // камера — в зонде 5 окна по высоте (выше нужнее: потолок, крыша)
const int GI_IRR = 8, GI_DEP = 10;   // плитка зонда в атласе с рамкой в тексель
const float GI_S0 = 1.0;             // шаг зондов уровня 0, м
const float GI_BIAS = 0.25;          // сдвиг точки по нормали перед выборкой, доли шага
const uint GI_RAYS_MAX = 256u;
const uint GI_FRESH = 0x10000u;      // работа: зонд новый — без смешения со старым
const uint GI_SHOW = 0x80000u;       // работа: показать среднее круга (конец круга или молодой зонд)
const uint GI_K = 4u;                // поворотов лучей в круге; поворот работы — биты 17–18
const uint GI_NONE = 0u, GI_ACTIVE = 1u, GI_INACTIVE = 2u;  // состояние: не посчитан, в деле, в стене
const uint GI_VIEW_FRAME = 0u, GI_VIEW_ONLY = 1u, GI_VIEW_OFF = 2u;
const float GI_SPREAD = 0.2;         // «ширина» луча на метр пути: уровень поля и шаг, как у конуса
const uint GI_STEPS = 40u;
const float GI_TMAX = 48.0;          // дальше луч — небо
const vec3 GI_WALL_ALBEDO = vec3(0.28, 0.265, 0.24);  // заслонки поля без цвета: средний бетон и камень

float gi_step(int L) { return GI_S0 * float(1 << L); }
// Предел расстояния в атласе: полторы диагонали ячейки (как RTXGI), дальше проверке Чебышёва всё равно
float gi_dmax(int L) { return 2.6 * gi_step(L); }

// Октаэдр с осью y вверх (как oct_decode в common.glsl): направление → [−1, 1]²
vec2 gi_oct(vec3 d) {
    vec2 p = d.xz / (abs(d.x) + abs(d.y) + abs(d.z));
    if (d.y < 0.0) p = (1.0 - abs(p.yx)) * vec2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    return p;
}
vec3 gi_unoct(vec2 e) {
    vec3 n = vec3(e.x, 1.0 - abs(e.x) - abs(e.y), e.y);
    if (n.y < 0.0) n.xz = (1.0 - abs(n.zx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0);
    return normalize(n);
}

// Место зонда: уровень, высота, z, x по модулю окна (размеры — степени двойки: & работает и для минуса)
uint gi_slot(int L, ivec3 ip) {
    ivec3 m = ip & ivec3(GI_NX - 1, GI_NY - 1, GI_NZ - 1);
    return uint(((L * GI_NY + m.y) * GI_NZ + m.z) * GI_NX + m.x);
}
// Мировой номер зонда места slot при окне этого кадра
ivec3 gi_home(uint slot, out int L) {
    int s = int(slot);
    int x = s % GI_NX, z = (s / GI_NX) % GI_NZ, yl = s / (GI_NX * GI_NZ);
    int y = yl % GI_NY;
    L = yl / GI_NY;
    ivec3 lo = pc.frame.gi_lo[L].xyz;
    return lo + ((ivec3(x, y, z) - lo) & ivec3(GI_NX - 1, GI_NY - 1, GI_NZ - 1));
}
// Тексель плитки зонда в атласе: x, y, слой
ivec3 gi_tile(uint slot, int size) {
    int s = int(slot);
    return ivec3((s % GI_NX) * size, ((s / GI_NX) % GI_NZ) * size, s / (GI_NX * GI_NZ));
}
vec3 gi_uv(uint slot, vec3 d, int size) {
    ivec3 t = gi_tile(slot, size);
    vec2 px = vec2(t.xy) + 1.0 + (gi_oct(d) * 0.5 + 0.5) * float(size - 2);
    return vec3(px / vec2(GI_NX * size, GI_NZ * size), float(t.z));
}

// Поворот лучей зонда — от его мирового номера, уровня и номера k в круге (Shoemake): от кадра не зависит
uint gi_hash(uint v) {
    v ^= v >> 16;
    v *= 0x7FEB352Du;
    v ^= v >> 15;
    v *= 0x846CA68Bu;
    return v ^ (v >> 16);
}
vec4 gi_rot(ivec3 ip, int L, uint k) {
    uint h = gi_hash(uint(ip.x) * 0x9E3779B1u ^ gi_hash(uint(ip.y) * 0x85EBCA77u ^
                     gi_hash(uint(ip.z) ^ uint(L) * 0xC2B2AE3Du ^ k * 0x27D4EB2Fu)));
    float u1 = float(h & 0x3FFu) / 1023.0, u2 = float((h >> 10) & 0x7FFu) / 2047.0, u3 = float(h >> 21) / 2047.0;
    float a = sqrt(1.0 - u1), b = sqrt(u1);
    return vec4(a * sin(6.2831853 * u2), a * cos(6.2831853 * u2), b * sin(6.2831853 * u3), b * cos(6.2831853 * u3));
}
// Направление луча i из n: сферическая решётка Фибоначчи (поровну по сфере), повёрнутая gi_rot
vec3 gi_fib(uint i, uint n) {
    float y = 1.0 - (2.0 * float(i) + 1.0) / float(n);
    float r = sqrt(max(1.0 - y * y, 0.0));
    float ph = 2.39996323 * float(i);
    return vec3(r * cos(ph), y, r * sin(ph));
}

// Земля для лучей: сетка 8 м билинейно (без деталей и дюн), расстояние — по высоте с запасом на наклон
float gi_ground_h(vec3 pa) {
    FrameData f = pc.frame;
    vec2 g = pa.xz + vec2((f.sdf_anchor - ivec2(f.t_chunk0)) * 256);
    return t_base_height(g);
}
float gi_ground_dist(vec3 pa) {
    if (pc.frame.t_on == 0u) return SDF_FREE;
    return (pa.y - gi_ground_h(pa)) * 0.85;
}
// Расстояние до мира: поле (уровень не мельче L) и земля
float gi_dist(vec3 pa, int L) { return min(sdf_dist(pa, L), gi_ground_dist(pa)); }
vec3 gi_grad(vec3 pa, int L) {
    float e = pc.frame.sdf_cam.w * float(1 << L);
    return vec3(gi_dist(pa + vec3(e, 0, 0), L) - gi_dist(pa - vec3(e, 0, 0), L),
                gi_dist(pa + vec3(0, e, 0), L) - gi_dist(pa - vec3(0, e, 0), L),
                gi_dist(pa + vec3(0, 0, e), L) - gi_dist(pa - vec3(0, 0, e), L));
}

// Цвет земли в точке для отражённого света: песок и песок пустыни по маске пустыни (среднее двух цветов слоя)
vec3 gi_ground_albedo(vec3 pa) {
    FrameData f = pc.frame;
    vec2 g = pa.xz + vec2((f.sdf_anchor - ivec2(f.t_chunk0)) * 256);
    ivec2 gi = clamp(ivec2(floor(g)), ivec2(0), ivec2(f.t_size_x, f.t_size_z) - 1);
    float desert = float(t_masks2(gi.x, gi.y).y) / 255.0;
    Material a = f.materials.v[f.ground_layers[0]], b = f.materials.v[f.ground_layers[6]];
    return mix(0.5 * (a.p0.rgb + a.p1.rgb), 0.5 * (b.p0.rgb + b.p1.rgb), desert);
}

// Свет зондов уровня L в точке pa (м от якоря) по нормали n — E/π (сумма весов в w); ir — то же в сторону rd
// (rgb) и доля неба туда (a). Веса DDGI: трилинейный, мягкая обратная сторона, Чебышёв по расстояниям, слабые
// веса прижаты к нулю (crush). Зонды в стене, не посчитанные и чужие месту (окно ещё не обновлено) — без веса
vec4 gi_level(int L, vec3 pa, vec3 n, vec3 rd, bool want_r, out vec4 ir) {
    FrameData f = pc.frame;
    float s = gi_step(L);
    vec3 pb = pa + n * (GI_BIAS * s);
    vec3 u = pb / s;
    vec3 fl = floor(u);
    ivec3 i0 = ivec3(fl);
    vec3 fr = u - fl;
    vec3 sum = vec3(0.0);
    vec4 sr = vec4(0.0);
    float ws = 0.0;
    for (int c = 0; c < 8; ++c) {
        ivec3 o = ivec3(c & 1, (c >> 1) & 1, c >> 2);
        ivec3 ip = i0 + o;
        uint slot = gi_slot(L, ip);
        GiProbe p = f.gi_probes.v[slot];
        if (p.state != GI_ACTIVE || p.home != ip || p.level != L) continue;
        vec3 tri = mix(1.0 - fr, fr, vec3(o));
        vec3 to = p.pos - pa;
        float lt = length(to);
        float w = 1.0;
        if (lt > 1e-4) {
            float k = (dot(to / lt, n) + 1.0) * 0.5;
            w = k * k + 0.2;
        }
        vec3 pp = pb - p.pos;
        float d = max(length(pp), 1e-4);
        vec2 m = textureLod(u_gi_dep, gi_uv(slot, pp / d, GI_DEP), 0.0).rg;
        if (d > m.x) {
            float var = abs(m.x * m.x - m.y);
            float dm = d - m.x;
            float ch = var / (var + dm * dm);
            w *= ch * ch * ch;
        }
        w = max(w, 1e-6);
        if (w < 0.2) w *= w * w * 25.0;
        w *= tri.x * tri.y * tri.z;
        sum += w * textureLod(u_gi_irr, gi_uv(slot, n, GI_IRR), 0.0).rgb;
        if (want_r) sr += w * textureLod(u_gi_irr, gi_uv(slot, rd, GI_IRR), 0.0);
        ws += w;
    }
    ir = ws > 0.0 ? sr / ws : vec4(0.0);
    return vec4(ws > 0.0 ? sum / ws : vec3(0.0), ws);
}

// Доля уровня L у точки pos (м от камеры): окно накрывает точку с запасом, пока она не дальше GI_REACH шагов от
// камеры (вниз и вверх — свои, камера в окне не посередине); у края значение плавно переходит к уровню крупнее.
// Только от расстояния до камеры, а не от окна: камера едет — доля течёт, а не щёлкает
float gi_cover(int L, vec3 pos) {
    vec3 q = pos / gi_step(L);
    float wx = smoothstep(11.5, 13.0, max(abs(q.x), abs(q.z)));
    float wy = q.y < 0.0 ? smoothstep(3.5, 4.5, -q.y) : smoothstep(7.5, 8.5, q.y);
    return 1.0 - max(wx, wy);
}

struct GiSample {
    vec3 irr;    // E/π по нормали (как atmo_ambient)
    vec4 refl;   // E/π в сторону отражения и доля неба туда
    float cov;   // 0 — зондов нет (вне окон, поле выключено): свет берёт запасной путь
};
// Непрямой свет точки pos (м от камеры) с нормалью n: уровни по порядку, у края окна — смесь с крупнее
GiSample gi_sample(vec3 pos, vec3 n, vec3 rd, bool want_r) {
    FrameData f = pc.frame;
    GiSample g;
    g.irr = vec3(0.0);
    g.refl = vec4(0.0);
    g.cov = 0.0;
    if (f.gi_on == 0u) return g;
    vec3 pa = pos + f.sdf_cam.xyz;
    float left = 1.0;
    for (int L = 0; L < GI_LEVELS && left > 0.0; ++L) {
        float c = gi_cover(L, pos);
        if (c <= 0.0) continue;
        vec4 ir;
        vec4 v = gi_level(L, pa, n, rd, want_r, ir);
        // Ни одного зонда в деле рядом (все в стене или ещё не посчитаны) — уровень не в счёт, берётся крупнее
        float w = c * smoothstep(0.0, 1e-3, v.w);
        g.irr += left * w * v.rgb;
        g.refl += left * w * ir;
        g.cov += left * w;
        left *= 1.0 - w;
    }
    if (g.cov > 0.0) {
        g.irr /= g.cov;
        g.refl /= g.cov;
    }
    return g;
}
#endif
