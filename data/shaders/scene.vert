// Общий вершинный шейдер сцены и теней
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
layout(location = 2) in vec4 a_color;
layout(location = 3) in uvec2 a_mat_bone;

layout(location = 0) uniform mat4 u_view_proj;
layout(location = 1) uniform mat4 u_bones[16];
layout(location = 17) uniform int u_skinned;

out vec3 v_world;
out vec3 v_nrm;
out vec3 v_color;
flat out uint v_mat;
out float v_local_y;

void main()
{
    vec4 p = vec4(a_pos, 1.0);
    vec3 n = a_nrm;
    if (u_skinned != 0) {
        // Жёсткий скиннинг: одна кость на вершину
        mat4 b = u_bones[min(a_mat_bone.y, 15u)];
        p = b * p;
        n = mat3(b) * n;
    }
    v_world = p.xyz;
    v_nrm = n;
    v_color = a_color.rgb;
    v_mat = a_mat_bone.x;
    v_local_y = a_pos.y;
    gl_Position = u_view_proj * p;
}
