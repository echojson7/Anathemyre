// ОТРАЖЁННЫЙ СВЕТ: ЗОНДЫ (docs/lighting.md). Сцена в клетках 1 м (VOX_N, тот же угол, что у сетки неба):
// в клетке — цвет поверхности и признак «занято». Зонды через 4 м (PROBE_N) пускают лучи по клеткам и копят
// свет, отражённый от того, во что попали (сферические гармоники L1), и расстояния до препятствий по 64
// направлениям (среднее и средний квадрат, как в DDGI): по ним точка понимает, что зонд за стеной.
// Константы — блок Probes (render.c, GpuProbes)
layout(std140, binding = 38) uniform Probes {
    vec4  u_vox_origin;  // угол сетки клеток, м; w — 1, если клетки есть
    ivec4 u_probe;       // x — зонды читаются в сцене, y — первый обновляемый зонд, z — сколько, w — номер обхода
};
const ivec3 VOX_N = ivec3(128, 32, 128);
const float VOX_CELL = 1.0;
const ivec3 PROBE_N = ivec3(32, 8, 32);
const float PROBE_STEP = 4.0;
const int   PROBE_VEC = 5;   // vec4 на зонд: 4 коэффициента (rgb; w первого — 1, если зонд годен), положение
const int   PROBE_OCT = 8;   // расстояния: 8×8 направлений в октаэдрической развёртке

#if defined(VOXEL_PASS) || defined(PROBE_WRITE)
layout(std430, binding = 39) buffer Vox { uint g_vox[]; };
#else
layout(std430, binding = 39) readonly buffer Vox { uint g_vox[]; };
#endif
#ifdef PROBE_WRITE
layout(std430, binding = 40) buffer ProbeBuf { vec4 g_probe[]; };
layout(std430, binding = 41) buffer ProbeDist { vec2 g_pdist[]; };
#else
layout(std430, binding = 40) readonly buffer ProbeBuf { vec4 g_probe[]; };
layout(std430, binding = 41) readonly buffer ProbeDist { vec2 g_pdist[]; };
#endif

// Октаэдрическая развёртка: направление ↔ квадрат [-1, 1]²
vec2 oct_encode(vec3 d)
{
    d /= abs(d.x) + abs(d.y) + abs(d.z);
    vec2 e = d.xz;
    if (d.y < 0.0) e = (1.0 - abs(e.yx)) * vec2(e.x >= 0.0 ? 1.0 : -1.0, e.y >= 0.0 ? 1.0 : -1.0);
    return e;
}
vec3 oct_decode(vec2 e)
{
    vec3 d = vec3(e.x, 1.0 - abs(e.x) - abs(e.y), e.y);
    if (d.y < 0.0) d.xz = (1.0 - abs(d.zx)) * vec2(d.x >= 0.0 ? 1.0 : -1.0, d.z >= 0.0 ? 1.0 : -1.0);
    return normalize(d);
}

int vox_index(ivec3 c) { return (c.z * VOX_N.y + c.y) * VOX_N.x + c.x; }
bool vox_inside(ivec3 c) { return all(greaterThanEqual(c, ivec3(0))) && all(lessThan(c, VOX_N)); }
ivec3 vox_cell(vec3 p) { return ivec3(floor((p - u_vox_origin.xyz) / VOX_CELL)); }
// 0 — пусто (и за краем сетки)
uint vox_at(vec3 p)
{
    ivec3 c = vox_cell(p);
    return vox_inside(c) ? g_vox[vox_index(c)] : 0u;
}
// Цвет клетки хранится корнем: тёмным цветам больше ступеней
vec3 vox_albedo(uint v)
{
    vec3 a = vec3(float(v & 255u), float((v >> 8u) & 255u), float((v >> 16u) & 255u)) / 255.0;
    return a * a;
}

int probe_index(ivec3 c) { return (c.z * PROBE_N.y + c.y) * PROBE_N.x + c.x; }
vec3 probe_center(ivec3 c) { return u_vox_origin.xyz + (vec3(c) + 0.5) * PROBE_STEP; }

// Свет, приходящий к поверхности с нормалью n, делённый на π (в тех же единицах, что рассеянный свет сцены)
vec3 probe_irr(int i, vec3 n)
{
    vec3 c0 = g_probe[i * PROBE_VEC].rgb, c1 = g_probe[i * PROBE_VEC + 1].rgb;
    vec3 c2 = g_probe[i * PROBE_VEC + 2].rgb, c3 = g_probe[i * PROBE_VEC + 3].rgb;
    return max(c0 * 0.282095 + (0.325735 * (c1 * n.y + c2 * n.z + c3 * n.x)), vec3(0.0));
}

// Свет зондов в точке p: 8 соседних, вес — близость, сторона нормали и видимость. Видимость — по расстояниям
// зонда (Чебышёв, как в DDGI): если зонд в этом направлении упирается в стену раньше точки, точка за стеной
// и зонд почти не светит в бункер. vis = false — без проверки (лучи зондов)
// w — сумма весов: 0, если ни один зонд не дошёл
vec3 probe_light(vec3 p, vec3 n, bool vis, out float wsum)
{
    vec3 g = (p - u_vox_origin.xyz) / PROBE_STEP - 0.5;
    ivec3 b = ivec3(floor(g));
    vec3 f = g - vec3(b);
    vec3 sum = vec3(0.0);
    wsum = 0.0;
    for (int k = 0; k < 8; k++) {
        ivec3 o = ivec3(k & 1, (k >> 1) & 1, k >> 2);
        ivec3 c = clamp(b + o, ivec3(0), PROBE_N - 1);
        int i = probe_index(c);
        if (g_probe[i * PROBE_VEC].w < 0.5) continue;
        vec3 t = mix(1.0 - f, f, vec3(o));
        float w = t.x * t.y * t.z;
        vec3 d = g_probe[i * PROBE_VEC + 4].xyz - p;
        float l = length(d);
        float side = (dot(d, n) / max(l, 1e-4) + 1.0) * 0.5;
        w *= side * side + 0.2;
        if (vis && w > 1e-5) {
            // От зонда к точке, сдвинутой с поверхности на клетку: грань клетки может стоять до 1 м перед
            // самой поверхностью, и без сдвига пол под зондом «прятался» бы за своей же клеткой
            vec3 q = p + n * VOX_CELL - g_probe[i * PROBE_VEC + 4].xyz;
            float r = length(q);
            // Четыре соседних направления с весами: иначе видны треугольники развёртки
            vec2 u = (oct_encode(q / max(r, 1e-4)) * 0.5 + 0.5) * float(PROBE_OCT) - 0.5;
            ivec2 t0 = ivec2(floor(u));
            vec2 fu = u - vec2(t0);
            vec2 m = vec2(0.0);
            for (int k2 = 0; k2 < 4; k2++) {
                ivec2 oo = ivec2(k2 & 1, k2 >> 1);
                ivec2 tx = clamp(t0 + oo, ivec2(0), ivec2(PROBE_OCT - 1));
                vec2 bw = mix(1.0 - fu, fu, vec2(oo));
                m += g_pdist[i * PROBE_OCT * PROBE_OCT + tx.y * PROBE_OCT + tx.x] * (bw.x * bw.y);
            }
            if (r > m.x) {
                float var = max(m.y - m.x * m.x, 0.04);
                float c = var / (var + (r - m.x) * (r - m.x));
                w *= max(c * c * c, 0.002);
            }
        }
        sum += probe_irr(i, n) * w;
        wsum += w;
    }
    return wsum > 1e-5 ? sum / wsum : vec3(0.0);
}
