// Апскейл до окна с ровными пикселями: внутри пикселя цвет постоянный,
// а на границе смешивание шириной в один пиксель экрана — без ряби при дробном масштабе

in vec2 v_uv;

layout(location = 43) uniform vec2 u_out;  // размер окна
layout(binding = 0) uniform sampler2D u_src;

out vec4 o_color;

void main()
{
    vec2 src = vec2(textureSize(u_src, 0));
    vec2 scale = u_out / src;
    vec2 texel = v_uv * src;
    vec2 d = fract(texel) - 0.5;
    vec2 region = 0.5 - 0.5 / scale;
    vec2 f = (d - clamp(d, -region, region)) * scale + 0.5;
    o_color = texture(u_src, (floor(texel) + f) / src);
}
