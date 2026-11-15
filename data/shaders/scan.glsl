// Сканы CC0 (docs/scans.md): поверхность материала из картинок вместо узора, где у материала есть скан и кадр
// просит сканы (FrameData.scans, --scans, F3). UV у сеток нет: проекция по трём осям мира, смесь осей по нормали.
// Правило пиксельности сохраняется: выборка одна на ячейку, в её центре, с мипом размером с ячейку (или с пиксель,
// если он крупнее) — картинка усредняется по ячейке, а не мерцает мельче неё
#ifndef SCAN_GLSL
#define SCAN_GLSL

layout(set = 0, binding = 9) uniform sampler2DArray u_scan_albedo;   // sRGB: цвет, AO
layout(set = 0, binding = 10) uniform sampler2DArray u_scan_normal;  // нормаль OpenGL · 0.5 + 0.5, шероховатость

// Оси плоскостей: u вправо по картинке, v вниз (строки). Зелёный нормали OpenGL смотрит к верху картинки (−v).
// Стены (x, z): верх картинки — вверх по миру; пол и потолок (y): u по x, v по z
void scan_apply(SurfIn i, inout Surface s) {
    float tile = i.m.p3.w;
    float layer = float(i.m.scan - 1u);
    vec3 q = i.p / tile;  // 256 м делятся на плитку нацело: смена чанка камеры сдвигает uv на целое
    vec3 w = pow(abs(i.n), vec3(4.0));
    w /= w.x + w.y + w.z;
    float texels = float(textureSize(u_scan_albedo, 0).x);
    float lod = log2(max(max(i.cell, i.foot) * texels / tile, 1.0));
    vec2 uv[3] = vec2[3](vec2(q.z, -q.y), vec2(q.x, q.z), vec2(q.x, -q.y));
    // Касательные: красный → T, зелёный → G (мир)
    const vec3 T[3] = vec3[3](vec3(0, 0, 1), vec3(1, 0, 0), vec3(1, 0, 0));
    const vec3 G[3] = vec3[3](vec3(0, 1, 0), vec3(0, 0, -1), vec3(0, 1, 0));
    vec3 albedo = vec3(0.0), tilt = vec3(0.0);
    float ao = 0.0, rough = 0.0, len = 0.0;
    for (int k = 0; k < 3; ++k) {
        if (w[k] < 0.01) continue;
        vec4 a = textureLod(u_scan_albedo, vec3(uv[k], layer), lod);
        vec4 n = textureLod(u_scan_normal, vec3(uv[k], layer), lod);
        vec3 tn = n.xyz * 2.0 - 1.0;  // мип — среднее нормалей: короче единицы, где наклон погашен
        albedo += a.rgb * w[k];
        ao += a.a * w[k];
        rough += n.a * w[k];
        len += length(tn) * w[k];
        tilt += (tn.x * T[k] + tn.y * G[k]) / max(tn.z, 0.1) * w[k];  // наклон к плоскости проекции (UDN)
    }
    s.albedo = albedo * i.m.p3.rgb;
    s.rough = rough;
    s.normal = normalize(i.n + tilt - i.n * dot(tilt, i.n));
    s.ao *= ao;
    s.metal = 0.0;  // в пробе все сканы — диэлектрики (ржавчина, краска, бетон, дерево)
    // Наклон, погашенный мипом, — в шероховатость (Токсвиг): |n| = 1 / sqrt(1 + 2σ²) по Капланяну
    float l = clamp(len, 0.2, 1.0);
    s.nvar = max(s.nvar, 0.5 * (1.0 / (l * l) - 1.0));
}
#endif
