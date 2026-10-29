// Атлас: 1 — пиксель буквы или сплошная заливка
layout(binding = 0) uniform sampler2D u_atlas;
in vec2 v_uv;
in vec4 v_color;
out vec4 o_color;
void main()
{
    o_color = vec4(v_color.rgb, v_color.a * texture(u_atlas, v_uv).r);
}
