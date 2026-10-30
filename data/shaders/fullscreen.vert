// Один треугольник на весь экран
out vec2 v_uv;
void main()
{
    vec2 p = vec2((RB_VERTEX_ID << 1) & 2, RB_VERTEX_ID & 2);
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
