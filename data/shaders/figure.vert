// Прямоугольник спрайта персонажа на экране
layout(location = 44) uniform vec4 u_rect;    // левый нижний угол и размер, пиксели кадра
layout(location = 46) uniform vec2 u_screen;  // размер кадра

void main()
{
    vec2 q = vec2((0x34 >> gl_VertexID) & 1, (0x26 >> gl_VertexID) & 1);  // два треугольника
    vec2 p = (u_rect.xy + q * u_rect.zw) / u_screen;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
