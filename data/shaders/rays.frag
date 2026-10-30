// Объёмный свет солнца в половинном разрешении: шагаем по лучу в теневой карте.
// R — накопленный свет, G — длина шага по лучу (для апскейла с учётом глубины)

in vec2 v_uv;

layout(binding = 7) uniform sampler2D u_depth;
layout(binding = 6) uniform sampler2DShadow u_shadow;

out vec2 o_rays;

float light_vis(vec3 p)
{
    vec4 lp = u_light_vp * vec4(p, 1.0);
    vec3 s = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(s, vec3(0.0))) || any(greaterThan(s, vec3(1.0)))) return 1.0;
    return texture(u_shadow, vec3(s.xy, s.z - 0.002));
}

void main()
{
    float d = texture(u_depth, v_uv).r;
    vec4 w = u_inv_vp * vec4(v_uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec3 ray = w.xyz / w.w - u_cam_pos;
    float dist = d >= 1.0 ? 4000.0 : length(ray);
    vec3 dir = normalize(ray);

    // 12 шагов: против солнца кадр тот же, что при 24 (разница не больше 14/255), а проход вдвое дешевле
    const int STEPS = 12;
    float march = min(dist, 110.0);
    float step_len = march / float(STEPS);
    float jitter = ign(gl_FragCoord.xy, u_frame);
    float lit = 0.0;
    for (int i = 0; i < STEPS; i++) {
        float t = (float(i) + jitter) * step_len;
        vec3 p = u_cam_pos + dir * t;
        float dens = 0.7 + 0.6 * noise3(p * 0.15 + vec3(u_time * 0.2, 0.0, u_time * 0.1));
        lit += light_vis(p) * dens * exp(-t * 0.012);
    }
    o_rays = vec2(lit * step_len, march);
}
