// Откуда каждый пиксель кадра берёт цвет (pixel_src): раз за кадр, дальше только чтение

out uvec2 o_src;

void main() { o_src = uvec2(pixel_src(ivec2(gl_FragCoord.xy))); }
