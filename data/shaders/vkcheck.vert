// Проверка Vulkan (tools/vkcheck): треугольник на весь экран без буферов
void main()
{
    vec2 p = vec2((RB_VERTEX_ID << 1) & 2, RB_VERTEX_ID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
