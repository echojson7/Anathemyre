// Интерфейс: прямоугольники и буквы в пикселях окна
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 0) uniform vec2 u_screen;
out vec2 v_uv;
out vec4 v_color;
void main()
{
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = vec4(a_pos / u_screen * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
}
