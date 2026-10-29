// Общий вершинный шейдер сцены и теней
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
layout(location = 2) in vec4 a_color;
layout(location = 3) in uvec2 a_mat_bone;

layout(location = 0) uniform mat4 u_view_proj;
layout(location = 50) uniform mat4 u_bones[24];
layout(location = 17) uniform int u_skinned;

out vec3 v_world;
out vec3 v_nrm;
out vec3 v_color;
flat out uint v_mat;
out vec3 v_local;      // координаты тела (у статики — мира): к ним привязаны квадраты материала
flat out mat3 v_rot;   // поворот из координат тела в мир

void main()
{
    vec4 p = vec4(a_pos, 1.0);
    vec3 n = a_nrm;
    v_rot = mat3(1.0);
    if (u_skinned != 0) {
        // Жёсткий скиннинг: одна кость на вершину
        mat4 b = u_bones[min(a_mat_bone.y, 23u)];
        p = b * p;
        n = mat3(b) * n;
        v_rot = mat3(b);
    }
    v_world = p.xyz;
    v_nrm = n;
    v_color = a_color.rgb;
    v_mat = a_mat_bone.x;
    v_local = a_pos;
    gl_Position = u_view_proj * p;
}
