#version 450
// Проверка вывода: снизу тёмная охра, кверху бетон; полоса бледно-зелёного у нижнего края.
// gl_FragCoord.y = 0 — низ картинки, как в GL; на снимке полоса должна быть внизу
layout(push_constant) uniform Push { vec4 size_time; } pc;
layout(location = 0) out vec4 o_color;

void main()
{
    float v = gl_FragCoord.y / pc.size_time.y;
    vec3 c = mix(vec3(0.20, 0.14, 0.07), vec3(0.42, 0.41, 0.38), v);
    if (gl_FragCoord.y < 16.0) c = vec3(0.55, 0.70, 0.55);
    float scan = 0.5 + 0.5 * sin(pc.size_time.z * 2.0 + gl_FragCoord.y * 0.05);
    o_color = vec4(c * (0.9 + 0.1 * scan), 0.5);
}
