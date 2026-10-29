// Стилизация: тонмаппинг, грязный грейд, виньетка, плёночное зерно

in vec2 v_uv;

layout(location = 41) uniform int u_post;
layout(location = 42) uniform int u_frame;
layout(binding = 0) uniform sampler2D u_src;

out vec4 o_color;

vec3 aces(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    vec2 uv = v_uv;

    if (u_post == 0) {
        vec3 c = aces(texture(u_src, uv).rgb * 1.2);
        o_color = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
        return;
    }

    vec3 c = aces(texture(u_src, uv).rgb * 1.05);

    // Грейд: приглушить, тени в холодный серый, света в болезненно-жёлтый
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, 0.72);
    c *= mix(vec3(0.88, 0.93, 0.95), vec3(1.06, 1.0, 0.80), smoothstep(0.05, 0.7, l));
    c = mix(c, c * c * (3.0 - 2.0 * c), 0.6);
    c = max(c - 0.015, 0.0) * 1.03;

    // Виньетка
    vec2 q = uv - 0.5;
    c *= 1.0 - 0.55 * pow(length(q * vec2(1.1, 1.3)), 2.4);

    c = pow(max(c, 0.0), vec3(1.0 / 2.2));

    // Плёночное зерно, сильнее в тенях
    float n = hash12(gl_FragCoord.xy + vec2(float(u_frame % 97) * 13.1, float(u_frame % 89) * 7.7)) - 0.5;
    c += n * 0.10 * (1.0 - 0.5 * l);

    o_color = vec4(clamp(c, 0.0, 1.0), 1.0);
}
