// Размер пикселя (level_at) каждой точки кадра: раз за кадр, дальше только чтение
out uint o_level;

void main() { o_level = uint(level_at(ivec2(gl_FragCoord.xy))); }
