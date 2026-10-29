// Общий вершинный шейдер сцены и теней
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
layout(location = 2) in vec4 a_color;
layout(location = 3) in uvec4 a_mat_bone;  // материал, кость, класс оттенка (1 кожа, 2 волосы)
layout(location = 4) in uvec4 a_bones;    // до 4 костей; без потока скиннинга вес первой 0
layout(location = 5) in vec4 a_weights;

layout(location = 0) uniform mat4 u_view_proj;
layout(location = 17) uniform int u_skinned;
layout(location = 19) uniform int u_inst_base;
layout(location = 28) uniform int u_morphs;   // число морфов сетки

// Наборы костей кадра по 24 матрицы; экземпляр берёт номер своего набора из списка прохода
layout(std430, binding = 0) readonly buffer BoneBuf { mat4 bones[]; };
layout(std430, binding = 1) readonly buffer InstBuf { uint inst_pose[]; };
// Сдвиги морфов: на вершину и морф три uint — snorm16 пары (dx dy) (dz nx) (ny nz)
layout(std430, binding = 2) readonly buffer MorphBuf { uint morph[]; };
const float MORPH_POS = 0.25, MORPH_NRM = 2.0;  // как в tools/humanconv

out vec3 v_world;
out vec3 v_nrm;
out vec3 v_color;
flat out uint v_mat;
out vec3 v_local;      // координаты тела (у статики — мира): к ним привязаны квадраты материала
flat out mat3 v_rot;   // поворот из координат тела в мир
// Проход глубины и сцена должны дать одну и ту же глубину до бита
invariant gl_Position;

void main()
{
    vec4 p = vec4(a_pos, 1.0);
    vec3 n = a_nrm;
    v_rot = mat3(1.0);
    v_color = a_color.rgb;
    if (u_skinned != 0) {
        uint base = inst_pose[u_inst_base + gl_InstanceID] * 24u;
        // Внешность экземпляра: последний слот набора костей (render.h, RENDER_LOOK_SLOT)
        mat4 look = bones[base + 23u];
        for (int k = 0; k < u_morphs; k++) {
            float w = look[k >> 2][k & 3];
            if (w == 0.0) continue;
            uint o = (uint(gl_VertexID) * uint(u_morphs) + uint(k)) * 3u;
            vec2 a = unpackSnorm2x16(morph[o]), b = unpackSnorm2x16(morph[o + 1u]), c = unpackSnorm2x16(morph[o + 2u]);
            p.xyz += w * MORPH_POS * vec3(a, b.x);
            n += w * MORPH_NRM * vec3(b.y, c);
        }
        if (a_mat_bone.z != 0u) v_color *= 1.0 + (a_mat_bone.z == 1u ? look[2].xyz : look[3].xyz);
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
    }
    v_world = p.xyz;
    v_nrm = n;
    v_mat = a_mat_bone.x;
    v_local = a_pos;
    gl_Position = u_view_proj * p;
}
