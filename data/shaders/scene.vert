// Общий вершинный шейдер сцены и теней
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
layout(location = 2) in vec4 a_color;
layout(location = 3) in uvec4 a_mat_bone;  // материал, кость, класс оттенка (1 кожа, 2 волосы)
layout(location = 4) in uvec4 a_bones;    // до 4 костей; без потока скиннинга вес первой 0
layout(location = 5) in vec4 a_weights;
// Сетка из общего буфера (u_skinned 4): набор костей, морфов у сетки, её первая вершина, начало её морфов
layout(location = 6) in uvec4 a_rec;

layout(location = 0) uniform mat4 u_view_proj;
layout(location = 17) uniform int u_skinned;   // 0 — статика, 1 — кости, 2 — экземпляры статики, 3 — рельеф, 4 — кости из общего буфера
layout(location = 19) uniform int u_inst_base;
layout(location = 20) uniform vec3 u_cam_pos;
layout(location = 23) uniform float u_time;
layout(location = 28) uniform int u_morphs;   // число морфов сетки
layout(location = 29) uniform vec2 u_fade;     // экземпляры редеют с дальности x и пропадают к y (0 — нет)

// Наборы костей кадра по 24 матрицы; экземпляр берёт номер своего набора из списка прохода
layout(std430, binding = 0) readonly buffer BoneBuf { mat4 bones[]; };
layout(std430, binding = 1) readonly buffer InstBuf { uint inst_pose[]; };
// Сдвиги морфов: на вершину и морф три uint — snorm16 пары (dx dy) (dz nx) (ny nz)
layout(std430, binding = 2) readonly buffer MorphBuf { uint morph[]; };
const float MORPH_POS = 0.25, MORPH_NRM = 2.0;  // как в tools/humanconv
// Экземпляры статики (RInst): a — положение и поворот, b — ширина, высота, наклон; оттенок RGBA8
struct InstRec { vec4 a; vec3 b; uint tint; };
layout(std430, binding = 3) readonly buffer StaticInstBuf { InstRec insts[]; };
// Рельеф кольцами (render/terrain.c): уровень — слой; узел g лежит в (g & 255) текстур уровня
layout(location = 49) uniform ivec4 u_terr_grid;  // xy — узел (0,0) уровня в узлах уровня, z — уровень
layout(location = 50) uniform vec4 u_terr_view;   // x — шаг узлов, м; yz — центр колец
layout(binding = 8) uniform sampler2DArray u_terr_h;  // высота, м
layout(binding = 9) uniform sampler2DArray u_terr_c;  // альбедо; вид в альфе: 0 — песок, 1 — земля долины
const int TERR_G = 129, TERR_MASK = 255;  // узлов на сторону уровня (TERRAIN_CELLS + 1), сторона текстур − 1
// От центра колец: с 46 до 62 клеток уровня высота переходит к уровню крупнее, к краю (не ближе 62) — целиком
const float TERR_MORPH0 = 46.0, TERR_MORPH_W = 16.0;

out vec3 v_world;
out vec3 v_nrm;
out vec4 v_color;
flat out uint v_mat;
out vec3 v_local;      // координаты тела (у статики — мира): к ним привязаны квадраты материала
flat out mat3 v_rot;   // поворот из координат тела в мир
flat out vec3 v_tint;  // оттенок экземпляра
// Проход глубины и сцена должны дать одну и ту же глубину до бита
invariant gl_Position;

const uint MAT_TERRAIN = 9u, MAT_BARK = 12u, MAT_LEAF = 13u, MAT_GRASS = 14u;

float terr_h(ivec2 g) { return texelFetch(u_terr_h, ivec3(g & TERR_MASK, u_terr_grid.z), 0).r; }
vec4 terr_c(ivec2 g) { return texelFetch(u_terr_c, ivec3(g & TERR_MASK, u_terr_grid.z), 0); }

// Ветер: медленные порывы и быстрая дрожь, фаза зависит от места
vec2 wind(vec3 root)
{
    float gust = 0.6 + 0.4 * sin(u_time * 0.37 + root.x * 0.021 + root.z * 0.017);
    float sway = sin(u_time * 1.6 + root.x * 0.31 + root.z * 0.23) * 0.7 + sin(u_time * 2.9 + root.x * 0.83 - root.z * 0.57) * 0.3;
    return vec2(0.85, 0.5) * sway * gust;
}

void main()
{
    vec4 p = vec4(a_pos, 1.0);
    vec3 n = a_nrm;
    v_rot = mat3(1.0);
    v_color = a_color;
    v_local = a_pos;
    v_tint = vec3(1.0);
    if (u_skinned == 1 || u_skinned == 4) {
        // В общем буфере всё о сетке — в записи экземпляра, gl_VertexID считается от начала буфера
        bool pool = u_skinned == 4;
        uint base = (pool ? a_rec.x : inst_pose[u_inst_base + gl_InstanceID]) * 24u;
        int morphs = pool ? int(a_rec.y) : u_morphs;
        uint vid = pool ? uint(gl_VertexID) - a_rec.z : uint(gl_VertexID);
        uint moff = pool ? a_rec.w : 0u;
        // Внешность экземпляра: последний слот набора костей (render.h, RENDER_LOOK_SLOT)
        mat4 look = bones[base + 23u];
        for (int k = 0; k < morphs; k++) {
            float w = k < 8 ? look[k >> 2][k & 3] : look[k - 6][3];  // 9-й и 10-й — в свободных look[2].w, look[3].w
            if (w == 0.0) continue;
            uint o = moff + (vid * uint(morphs) + uint(k)) * 3u;
            vec2 a = unpackSnorm2x16(morph[o]), b = unpackSnorm2x16(morph[o + 1u]), c = unpackSnorm2x16(morph[o + 2u]);
            p.xyz += w * MORPH_POS * vec3(a, b.x);
            n += w * MORPH_NRM * vec3(b.y, c);
        }
        if (a_mat_bone.z != 0u) v_color.rgb *= 1.0 + (a_mat_bone.z == 1u ? look[2].xyz : look[3].xyz);
        mat4 b;
        if (a_weights.x > 0.0) {
            // Мягкий скиннинг: смесь матриц до 4 костей; поворот квадратов материала — от главной
            uvec4 k = min(a_bones, uvec4(23u));
            mat4 b0 = bones[base + k.x];
            b = b0 * a_weights.x + bones[base + k.y] * a_weights.y + bones[base + k.z] * a_weights.z + bones[base + k.w] * a_weights.w;
            v_rot = mat3(b0);
        } else {
            // Жёсткий: одна кость на вершину
            b = bones[base + min(a_mat_bone.y, 23u)];
            v_rot = mat3(b);
        }
        p = b * p;
        n = mat3(b) * n;
    } else if (u_skinned == 2) {
        InstRec r = insts[inst_pose[u_inst_base + gl_InstanceID]];
        float cy = cos(r.a.w), sy = sin(r.a.w), cl = cos(r.b.z), sl = sin(r.b.z);
        mat3 rot = mat3(cy, 0.0, -sy, 0.0, 1.0, 0.0, sy, 0.0, cy) * mat3(cl, -sl, 0.0, sl, cl, 0.0, 0.0, 0.0, 1.0);
        vec3 s = vec3(r.b.x, r.b.y, r.b.x);
        uint tint = r.tint;
        vec3 lp = a_pos * s;
        v_local = lp;
        // Прореживание: экземпляр с порядком выше плотности на своей дальности уходит в землю
        if (u_fade.y > 0.0) {
            float dens = clamp((u_fade.y - distance(r.a.xyz, u_cam_pos)) / (u_fade.y - u_fade.x), 0.0, 1.0);
            lp *= clamp((dens - float(tint >> 24u) / 255.0) * 10.0, 0.0, 1.0);
        }
        vec3 wp = rot * lp;
        uint mat = a_mat_bone.x;
        if (mat == MAT_GRASS) {
            float h = max(a_pos.y, 0.0);
            wp.xz += wind(r.a.xyz + a_pos * 3.0) * h * h * 0.9;
        } else if (mat == MAT_BARK || mat == MAT_LEAF) {
            // Дерево качается целиком, листва ещё и дрожит
            float h = max(wp.y, 0.0);
            wp.xz += wind(r.a.xyz) * h * h * 0.0012;
            if (mat == MAT_LEAF) wp += vec3(sin(u_time * 4.1 + a_pos.y * 3.0 + a_pos.x), 0.0, cos(u_time * 3.7 + a_pos.z * 3.0)) * 0.025 * h * 0.1;
        }
        p = vec4(r.a.xyz + wp, 1.0);
        n = rot * (n / s);
        v_rot = rot;
        v_tint = vec3(tint & 255u, (tint >> 8u) & 255u, (tint >> 16u) & 255u) / 128.0;
    } else if (u_skinned == 3) {
        // Рельеф: узел сетки уровня из номера вершины, высота и цвет из текстур уровня. К краю уровня
        // высота уходит к уровню крупнее (у нечётных узлов — середина соседей, как на его треугольниках)
        ivec2 g = u_terr_grid.xy + ivec2(gl_VertexID % TERR_G, gl_VertexID / TERR_G);
        float s = u_terr_view.x;
        vec2 xz = vec2(g) * s;
        vec2 dc = abs(xz - u_terr_view.yz) / s;
        float m = clamp((max(dc.x, dc.y) - TERR_MORPH0) / TERR_MORPH_W, 0.0, 1.0);
        float h = terr_h(g);
        v_color = terr_c(g);
        // Нормаль по соседям; у края — по соседям через один, как у уровня крупнее
        ivec2 ex = ivec2(1, 0), ez = ivec2(0, 1);
        n = normalize(vec3(terr_h(g - ex) - terr_h(g + ex), 2.0 * s, terr_h(g - ez) - terr_h(g + ez)));
        if (m > 0.0) {
            ivec2 odd = g & 1;
            ivec2 ga = g - odd, gb = g + odd;
            h = mix(h, 0.5 * (terr_h(ga) + terr_h(gb)), m);
            v_color = mix(v_color, 0.5 * (terr_c(ga) + terr_c(gb)), m);
            vec3 n2 = vec3(terr_h(g - 2 * ex) - terr_h(g + 2 * ex), 4.0 * s, terr_h(g - 2 * ez) - terr_h(g + 2 * ez));
            n = mix(n, normalize(n2), m);
        }
        p = vec4(xz.x, h, xz.y, 1.0);
        v_local = p.xyz;
    }
    v_world = p.xyz;
    v_nrm = n;
    v_mat = u_skinned == 3 ? MAT_TERRAIN : a_mat_bone.x;
    gl_Position = u_view_proj * p;
}
