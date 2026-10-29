// Общий вершинный шейдер сцены и теней
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
layout(location = 2) in vec4 a_color;
layout(location = 3) in uvec2 a_mat_bone;
layout(location = 4) in uvec4 a_bones;    // до 4 костей; без потока скиннинга вес первой 0
layout(location = 5) in vec4 a_weights;

layout(location = 0) uniform mat4 u_view_proj;
layout(location = 17) uniform int u_skinned;
layout(location = 19) uniform int u_inst_base;

// Наборы костей кадра по 24 матрицы; экземпляр берёт номер своего набора из списка прохода
layout(std430, binding = 0) readonly buffer BoneBuf { mat4 bones[]; };
layout(std430, binding = 1) readonly buffer InstBuf { uint inst_pose[]; };

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
    if (u_skinned != 0) {
        uint base = inst_pose[u_inst_base + gl_InstanceID] * 24u;
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
    v_color = a_color.rgb;
    v_mat = a_mat_bone.x;
    v_local = a_pos;
    gl_Position = u_view_proj * p;
}
