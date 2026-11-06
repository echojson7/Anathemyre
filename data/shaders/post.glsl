// Стиль кадра (docs/render.md, проход 8): экспозиция по освещённости неба (sky_sum.comp), ночная синева и
// тонмаппинг. Пока отдельного прохода нет (этапы 1–4), его зовут в конце шейдеры земли, сеток и неба
#ifndef POST_GLSL
#define POST_GLSL
#include "common.glsl"

// ACES в приближении Narkowicz: мягкое плечо, светлое не обрезается
vec3 tonemap_aces(vec3 x) { return clamp(x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }

vec3 post_color(vec3 c) {
    FrameData f = pc.frame;
    vec4 o = f.sky_out.v[4];
    c *= o.x;
    // Ночью глаз видит палочками: цвет уходит в холодный серо-синий
    float night = f.a_post.w * (1.0 - smoothstep(0.0003, 0.02, o.y));
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(c, l * vec3(0.72, 0.88, 1.25), night);
    return tonemap_aces(c);
}
#endif
