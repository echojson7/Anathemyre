// Самый крупный размер пикселя в блоке 8x8 (level.frag): с него pixel_q начинает перебор
layout(binding = 12) uniform usampler2D u_level;

layout(location = 0) out uint o_level;

void main()
{
    ivec2 b0 = ivec2(gl_FragCoord.xy) * 8, size = textureSize(u_level, 0) - 1;
    uint m = 0u;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) m = max(m, texelFetch(u_level, min(b0 + ivec2(x, y), size), 0).r);
    o_level = m;
}
