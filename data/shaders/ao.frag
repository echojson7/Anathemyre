// AO в половинном разрешении: R — затенение, G — дальность (для апскейла с учётом глубины)

layout(location = 0) out vec2 o_ao;

void main()
{
    // Точка — левый нижний пиксель полного разрешения из четырёх, как ближайшая выборка
    ivec2 t = min(ivec2(gl_FragCoord.xy) * 2, textureSize(u_depth, 0) - 1);
    vec2 uv = (vec2(t) + 0.5) / vec2(textureSize(u_depth, 0));
    float d = texelFetch(u_depth, t, 0).r;
    if (d >= 1.0) { o_ao = vec2(1.0, 4000.0); return; }
    vec3 P = world_at(uv, d);
    float dist = length(P - u_cam_pos);
    o_ao = vec2(ambient_occlusion(uv, P, dist), dist);
}
