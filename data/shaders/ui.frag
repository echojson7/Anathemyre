// Интерфейс: буквы и заливки из атласа, панели со сканлайнами, портреты из HDR-атласа
layout(binding = 0) uniform sampler2D u_atlas;
layout(binding = 1) uniform sampler2D u_image;
layout(location = 1) uniform float u_time;
in vec2 v_uv;
in vec4 v_color;
out vec4 o_color;

float hash(vec2 p)
{
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return fract((p.x + p.y) * p.x);
}

void main()
{
    float n = hash(gl_FragCoord.xy + floor(u_time * 24.0) * vec2(7.31, 3.17)) - 0.5;
    if (v_uv.x >= 1.5) {
        // Портрет: чистый цвет материалов (без света), только гамма, плюс зерно и строки старого экрана
        vec4 p = texture(u_image, v_uv - vec2(2.0, 0.0));
        vec3 c = pow(clamp(p.rgb, 0.0, 1.0), vec3(1.0 / 2.2));
        c *= 1.0 - 0.18 * mod(floor(gl_FragCoord.y), 2.0);
        c += n * 0.06;
        o_color = vec4(c * v_color.rgb, p.a * v_color.a);
    } else if (v_uv.x < -0.5) {
        vec3 c = v_color.rgb * (1.0 - 0.14 * mod(floor(gl_FragCoord.y), 2.0)) + n * 0.03;
        o_color = vec4(c, v_color.a);
    } else {
        o_color = vec4(v_color.rgb, v_color.a * texture(u_atlas, v_uv).r);
    }
}
