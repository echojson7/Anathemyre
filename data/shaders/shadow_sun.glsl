// Тени солнца группы СВЕТ (docs/lighting.md): каскады дальше карты ±45 м, мягкий край по расстоянию
// до заслонки (PCSS) и контактные тени с экрана. Константы — блок Shade (render.c, GpuShade)
layout(std140, binding = 33) uniform Shade {
    mat4  u_casc_vp[2];  // каскады 1 и 2 (±180 и ±700 м); каскад 0 — u_light_vp
    mat4  u_scene_vp;    // камера: контактные тени
    vec4  u_casc_ext;    // полуразмеры каскадов 0, 1, 2, м; w — глубина карты, м
    ivec4 u_shade;       // x — каскады, y — мягкий край, z — контактные тени, w — GTAO (углы)
};
layout(binding = 6) uniform sampler2DShadow u_shadow;           // каскад 0: прежняя карта ±45 м
layout(binding = 32) uniform sampler2DArrayShadow u_casc;
layout(binding = 34) uniform sampler2D u_contact;      // 1 — не закрыто вблизи (contact.frag)
layout(binding = 35) uniform sampler2D u_shadow_raw;   // карта каскада 0 без сравнения: поиск заслонки

// Диск Пуассона, 16 точек в единичном круге
const vec2 POISSON[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(-0.09418410, -0.92938870),
    vec2(0.34495938, 0.29387760), vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379), vec2(0.44323325, -0.97511554),
    vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590), vec2(0.19984126, 0.78641367),
    vec2(0.14383161, -0.14100790));

// Каскад 0 с мягким краем: заслонки в круге 0.6 м, полутень растёт с расстоянием от заслонки до точки
// (солнце — диск 0.53°, плюс немного рассеяния в пыльном воздухе). Узор выборок один на весь кадр:
// поворот от точки к точке давал зерно по краю тени, а полутень не шире ~7 текселей ступенек не даёт
float sun_shadow_soft(vec3 p, vec3 n)
{
    vec3 off = p + n * 0.06 + u_sun_dir * 0.03;
    vec4 lp = u_light_vp * vec4(off, 1.0);
    vec3 s = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(s.xy, vec2(0.0))) || any(greaterThan(s.xy, vec2(1.0)))) return 1.0;
    float texel_m = 2.0 * u_casc_ext.x / 2048.0;
    float search = 0.6 / (2.0 * u_casc_ext.x);
    float bsum = 0.0, bn = 0.0;
    for (int i = 0; i < 12; i++) {
        float d = texture(u_shadow_raw, s.xy + POISSON[i] * search).r;
        if (d < s.z - 0.0004) { bsum += d; bn += 1.0; }
    }
    if (bn == 0.0) return 1.0;
    float gap = (s.z - bsum / bn) * u_casc_ext.w;   // м от заслонки до точки
    float pen = clamp(gap * 0.012, texel_m * 1.2, 0.3);
    float r = pen / (2.0 * u_casc_ext.x);
    float sum = 0.0;
    for (int i = 0; i < 16; i++) sum += texture(u_shadow, vec3(s.xy + POISSON[i] * r, s.z));
    return sum / 16.0;
}

// Дальние каскады: сдвиг по нормали под размер их текселя, 3×3
float casc_shadow(int c, vec3 p, vec3 n)
{
    float texel_m = 2.0 * (c == 0 ? u_casc_ext.y : u_casc_ext.z) / 2048.0;
    vec3 off = p + n * texel_m * 1.5 + u_sun_dir * texel_m;
    vec4 lp = u_casc_vp[c] * vec4(off, 1.0);
    vec3 s = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(s.xy, vec2(0.0))) || any(greaterThan(s.xy, vec2(1.0))) || s.z > 1.0) return 1.0;
    float sum = 0.0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++)
            sum += texture(u_casc, vec4(s.xy + vec2(x, y) / 2048.0, float(c), s.z));
    return sum / 9.0;
}

// Доля каскада 0 в точке: 1 внутри, к краю карты переходит в каскад 1
float casc0_weight(vec3 p)
{
    vec4 lp = u_light_vp * vec4(p, 1.0);
    vec2 e = abs(lp.xy / lp.w);
    return 1.0 - smoothstep(0.85, 0.98, max(e.x, e.y));
}

float casc1_weight(vec3 p)
{
    vec4 lp = u_casc_vp[0] * vec4(p, 1.0);
    vec2 e = abs(lp.xy / lp.w);
    return 1.0 - smoothstep(0.85, 0.98, max(e.x, e.y));
}

// Контактная тень этого пикселя (0 — закрыто вблизи)
float contact_shadow(ivec2 px) { return u_shade.z != 0 ? texelFetch(u_contact, px, 0).r : 1.0; }
