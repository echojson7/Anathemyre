// Небо (docs/sky.md): рассеянный свет из таблиц, диск солнца, луна с фазой, звёзды. Зовут проход неба forward
// (sky.frag) и освещение visibility buffer (light_cells.comp) для пустых пикселей
#ifndef SKY_GLSL
#define SKY_GLSL
#include "atmosphere.glsl"

uint sky_hash3(uvec3 v) {
    uint h = v.x * 0x8DA6B343u ^ v.y * 0xD8163841u ^ v.z * 0xCB1AB31Fu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}
float sky_u01(uint h) { return float(h >> 8) * (1.0 / 16777216.0); }

// Звёзды: одна звезда на клетку сетки по небесной сфере, вращаются вместе с ней. Размер — в пикселях кадра
vec3 sky_stars(vec3 dir) {
    FrameData f = pc.frame;
    vec3 c = vec3(dot(f.a_star[0].xyz, dir), dot(f.a_star[1].xyz, dir), dot(f.a_star[2].xyz, dir));
    const float N = 180.0;
    vec3 g = c * N;
    vec3 cell = floor(g);
    uint h = sky_hash3(uvec3(ivec3(cell) + 4096));
    if ((h & 3u) != 0u) return vec3(0.0);  // четверть клеток с звездой
    vec3 sp = normalize((cell + vec3(sky_u01(h * 7u + 1u), sky_u01(h * 13u + 2u), sky_u01(h * 31u + 3u))) / N);
    float d = length(sp - c);
    float px = f.pixel_angle;
    float m = sky_u01(h * 101u + 7u);
    float mag = pow(m, 12.0) * 3.0 + 0.02;  // немного ярких, много тусклых
    float k = 1.0 - smoothstep(0.35 * px, 0.9 * px, d);
    float tint = sky_u01(h * 57u + 5u);
    vec3 col = mix(vec3(1.0, 0.82, 0.62), vec3(0.75, 0.85, 1.0), tint);
    return col * mag * k;
}

// Яркость неба по направлению dir (мир, единичный): рассеянный свет, солнце, луна, звёзды; линейная, до стиля
vec3 sky_radiance(vec3 dir) {
    FrameData f = pc.frame;
    vec4 sky = atmo_sky_lut(dir);
    vec3 L = sky.rgb;
    float hz = atmo_horizon(atmo_cam_alt());
    float above = smoothstep(hz - 0.002, hz + 0.002, asin(clamp(dir.y, -1.0, 1.0)));
    // Солнце: диск с потемнением к краю, яркость — освещённость на телесный угол, через атмосферу
    vec3 sun = f.a_sun.xyz;
    float rs = f.a_sun_t.w;
    float cs = dot(dir, sun);
    if (cs > cos(rs * 1.5)) {
        float x = clamp(acos(min(cs, 1.0)) / rs, 0.0, 1.5);
        float limb = x < 1.0 ? 0.4 + 0.6 * sqrt(1.0 - x * x) : 0.0;
        float edge = 1.0 - smoothstep(1.0 - 1.5 * f.pixel_angle / rs, 1.0, x);
        L += f.a_sun_t.rgb * f.a_sun.w / (A_PI * rs * rs) * limb * edge * above;
    }
    // Луна: шар, освещённый солнцем с его стороны, — фаза выходит сама. Пятна морей — шум по диску
    vec3 mo = f.a_moon.xyz;
    float rm = f.a_moon_t.w;
    float cm = dot(dir, mo);
    if (cm > cos(rm * 1.5)) {
        vec3 ax = normalize(cross(mo, abs(mo.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0)));
        vec3 ay = cross(ax, mo);
        vec2 q = vec2(dot(dir - mo, ax), dot(dir - mo, ay)) / rm;
        float rr = dot(q, q);
        if (rr < 1.0) {
            vec3 n = normalize(ax * q.x + ay * q.y - mo * sqrt(1.0 - rr));  // видимая сторона смотрит на нас
            float lit = max(dot(n, sun), 0.0);
            vec2 g = q * 3.0;
            float maria = 0.75 + 0.25 * sin(g.x * 2.1 + 1.3) * sin(g.y * 1.7 - 0.4) + 0.12 * sin(g.x * 5.3 + g.y * 4.1);
            float albedo = f.a_ozone.w * maria;
            float edge = 1.0 - smoothstep(1.0 - 2.0 * f.pixel_angle / rm, 1.0, sqrt(rr));
            vec3 disk = f.a_moon_t.rgb * f.a_sun.w * albedo / A_PI * (lit + 0.0004) * edge;
            L += disk * above;
        }
    }
    L += sky_stars(dir) * f.a_star[0].w * f.a_sun.w * 1e-6 * sky.a * above;
    return L;
}
#endif
