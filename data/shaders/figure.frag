// Спрайт персонажа из атласа: блок k×k пикселей кадра на пиксель спрайта, со своей глубиной.
// Пиксель спрайта сводится из 2x2 точек атласа: цвет — среднее закрытых точек, глубина — ближняя

layout(location = 44) uniform vec4 u_rect;
layout(location = 45) uniform vec4 u_cell;    // угол ячейки в атласе, размер блока k
layout(binding = 0) uniform sampler2D u_color;
layout(binding = 1) uniform sampler2D u_depth;

out vec4 o_color;

void main()
{
    ivec2 t = ivec2(floor((gl_FragCoord.xy - u_rect.xy) / u_cell.z)) * 2 + ivec2(u_cell.xy);
    vec4 c = vec4(0.0);
    float d = 1.0, n = 0.0;
    for (int i = 0; i < 4; i++) {
        ivec2 q = t + ivec2(i & 1, i >> 1);
        float z = texelFetch(u_depth, q, 0).r;
        if (z < 1.0) {
            c += texelFetch(u_color, q, 0);
            d = min(d, z);
            n += 1.0;
        }
    }
    if (n == 0.0) discard;
    o_color = c / n;
    gl_FragDepth = d;
}
