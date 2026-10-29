// Общие функции: хеши и шум

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float hash13(vec3 p3)
{
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

// Те же hash13 в узлах решётки 128³ (render.c, noise_make): аппаратная трилинейная выборка
// со сглаженной дробной частью даёт ту же интерполяцию, что и восемь хешей ниже. Узор повторяется через 128
layout(binding = 7) uniform sampler3D u_noise;
layout(location = 48) uniform int u_noise_tex;

float noise3(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    if (u_noise_tex != 0) return textureLod(u_noise, (mod(i, 128.0) + f + 0.5) / 128.0, 0.0).r;
    float a = hash13(i);
    float b = hash13(i + vec3(1, 0, 0));
    float c = hash13(i + vec3(0, 1, 0));
    float d = hash13(i + vec3(1, 1, 0));
    float e = hash13(i + vec3(0, 0, 1));
    float g = hash13(i + vec3(1, 0, 1));
    float h = hash13(i + vec3(0, 1, 1));
    float k = hash13(i + vec3(1, 1, 1));
    return mix(mix(mix(a, b, f.x), mix(c, d, f.x), f.y),
               mix(mix(e, g, f.x), mix(h, k, f.x), f.y), f.z);
}

float fbm3(vec3 p)
{
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) {
        s += a * noise3(p);
        p = p * 2.03 + vec3(1.7, 9.2, 3.1);
        a *= 0.5;
    }
    return s;
}

// Шум на кадр для дизеринга шагов лучей
float ign(vec2 px, int frame)
{
    px += 5.588238 * float(frame & 63);
    return fract(52.9829189 * fract(0.06711056 * px.x + 0.00583715 * px.y));
}
