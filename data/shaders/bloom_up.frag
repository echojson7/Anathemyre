// Свечение, подъём: меньшая ступень размывается шатром 3x3 и прибавляется к большей (смешение сложением)
in vec2 v_uv;

layout(binding = 28) uniform sampler2D u_bloom_src;

out vec4 o_color;

void main()
{
    vec2 t = 1.0 / vec2(textureSize(u_bloom_src, 0));
    vec3 c = textureLod(u_bloom_src, v_uv, 0.0).rgb * 4.0;
    c += (textureLod(u_bloom_src, v_uv + t * vec2(-1, 0), 0.0).rgb + textureLod(u_bloom_src, v_uv + t * vec2(1, 0), 0.0).rgb +
          textureLod(u_bloom_src, v_uv + t * vec2(0, -1), 0.0).rgb + textureLod(u_bloom_src, v_uv + t * vec2(0, 1), 0.0).rgb) * 2.0;
    c += textureLod(u_bloom_src, v_uv + t * vec2(-1, -1), 0.0).rgb + textureLod(u_bloom_src, v_uv + t * vec2(1, -1), 0.0).rgb +
         textureLod(u_bloom_src, v_uv + t * vec2(-1, 1), 0.0).rgb + textureLod(u_bloom_src, v_uv + t * vec2(1, 1), 0.0).rgb;
    o_color = vec4(c / 16.0, 1.0);
}
