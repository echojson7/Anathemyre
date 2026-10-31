// Общий вершинный шейдер сцены и теней
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
layout(location = 2) in vec4 a_color;
layout(location = 3) in uvec4 a_mat_bone;  // материал, кость, класс оттенка (1 кожа, 2 волосы)
layout(location = 4) in uvec4 a_bones;    // до 4 костей; без потока скиннинга вес первой 0
layout(location = 5) in vec4 a_weights;
// Сетка из общего буфера: набор костей, морфов у сетки (в режиме 5 — начало экземпляра в кеше поз),
// её первая вершина, начало её морфов
layout(location = 6) in uvec4 a_rec;


// Кости и морфы — в skin.glsl; экземпляр берёт номер своего набора костей из списка прохода
layout(std430, binding = 3) readonly buffer InstBuf { uint inst_pose[]; };
// Кеш поз (skin.comp): на вершину положение и нормаль
layout(std430, binding = 26) readonly buffer SkinOut { vec4 skin_out[]; };
// Экземпляры статики (RInst): a — положение и поворот, b — ширина, высота, наклон; оттенок RGBA8
struct InstRec { vec4 a; vec3 b; uint tint; };
layout(std430, binding = 5) readonly buffer StaticInstBuf { InstRec insts[]; };
// Рельеф кольцами (render/terrain.c): уровень — слой; узел g лежит в (g & 255) текстур уровня
layout(binding = 16) uniform sampler2DArray u_terr_h;  // высота, м
layout(binding = 17) uniform sampler2DArray u_terr_c;  // альбедо; вид в альфе: 0 — песок, 1 — земля долины
const int TERR_G = 129, TERR_MASK = 255;  // узлов на сторону уровня (TERRAIN_CELLS + 1), сторона текстур − 1
// От центра колец: с 46 до 62 клеток уровня высота переходит к уровню крупнее, к краю (не ближе 62) — целиком
const float TERR_MORPH0 = 46.0, TERR_MORPH_W = 16.0;

layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_nrm;
layout(location = 2) out vec4 v_color;
layout(location = 3) flat out uint v_mat;
layout(location = 4) out vec3 v_local;      // координаты тела (у статики — мира): к ним привязаны квадраты материала
layout(location = 5) flat out mat3 v_rot;   // поворот из координат тела в мир
layout(location = 8) flat out vec3 v_tint;  // оттенок экземпляра
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
    if (u_skinned == 1) {
        // Люди: поза и морфы уже в кеше поз (skin.comp); всё о сетке — в записи экземпляра,
        // RB_VERTEX_ID считается от начала общего буфера
        uint base = a_rec.x * 24u;
        uint o = (a_rec.y + uint(RB_VERTEX_ID) - a_rec.z) * 2u;
        p = skin_out[o];
        n = skin_out[o + 1u].xyz;
        // Внешность экземпляра — оттенок кожи и волос; поворот квадратов материала — от главной кости
        mat4 look = bones[base + 23u];
        if (a_mat_bone.z != 0u) v_color.rgb *= 1.0 + (a_mat_bone.z == 1u ? look[2].xyz : look[3].xyz);
        v_rot = mat3(bones[base + skin_main_bone(a_bones, a_weights, a_mat_bone.y)]);
    } else if (u_skinned == 2) {
        InstRec r = insts[inst_pose[u_inst_base + RB_INSTANCE_ID]];
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
        ivec2 g = u_terr_grid.xy + ivec2(RB_VERTEX_ID % TERR_G, RB_VERTEX_ID / TERR_G);
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
