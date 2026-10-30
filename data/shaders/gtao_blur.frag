// УГЛЫ: размытие GTAO квадратом 4x4 (ровно узор поворотов gtao.frag), соседи с другой дальностью не берутся

layout(binding = 37) uniform sampler2D u_gtao_raw;

layout(location = 0) out vec2 o_gtao;

void main()
{
    ivec2 c = ivec2(gl_FragCoord.xy), hi = textureSize(u_gtao_raw, 0) - 1;
    float dc = texelFetch(u_gtao_raw, c, 0).g;
    float sum = 0.0, wsum = 0.0;
    for (int y = -2; y < 2; y++)
        for (int x = -2; x < 2; x++) {
            vec2 v = texelFetch(u_gtao_raw, clamp(c + ivec2(x, y), ivec2(0), hi), 0).rg;
            float w = max(1.0 - abs(v.g - dc) / (0.03 * dc + 0.05), 0.0);
            sum += v.r * w;
            wsum += w;
        }
    o_gtao = vec2(wsum > 0.0 ? sum / wsum : 1.0, dc);
}
