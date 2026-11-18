#version 460
#extension GL_GOOGLE_include_directive : require
// Проход стиля (docs/render.md, проход 8): HDR-картинка кадра мира после сглаживания (история этого кадра, pc.a;
// docs/aa.md) → аберрация объектива, блеск (docs/glare.md), блики солнца в линзах, виньетка (docs/lens.md),
// баланс белого и тонмаппинг (post_color) → зерно и дизеринг
#include "post.glsl"
#include "glare.glsl"
#include "lens.glsl"
layout(set = 0, binding = 12) uniform sampler2D u_hist[2];
layout(set = 0, binding = 13, rgba16f) uniform readonly image2D u_aa[2];
layout(location = 0) out vec4 o_color;

// HDR кадра между пикселями (билинейно, по краю — край): px — в пикселях, центр пикселя — +0.5
vec3 hist_at(vec2 px) {
    vec2 uv = px / pc.frame.viewport;
    return pc.a == 0u ? textureLod(u_hist[0], uv, 0.0).rgb : textureLod(u_hist[1], uv, 0.0).rgb;
}

void main() {
    FrameData f = pc.frame;
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 px = gl_FragCoord.xy;
    vec3 c;
    // Аберрация (lens.glsl): пять длин волн со сдвигом от середины кадра; у середины сдвиг меньше 0.05 пикселя —
    // одна выборка
    vec2 sh = (px - 0.5 * f.viewport) * f.lens_ca;
    if ((f.lens_on & LENS_CA) != 0u && dot(sh, sh) > 0.0025) {
        c = vec3(0.0);
        for (int i = 0; i < 5; ++i) c += CA_W[i] * hist_at(px + sh * CA_T[i]);
    } else {
        c = pc.a == 0u ? imageLoad(u_aa[0], p).rgb : imageLoad(u_aa[1], p).rgb;
    }
    // Блеск: доля света кадра уходит в рассеяние вокруг ярких мест (энергия сохраняется), у солнца — корона по
    // видимой доле диска (glare.comp)
    float vis = (f.lens_on & LENS_SUN) != 0u ? f.lens_sun.v[0].x : 0.0;
    if (f.gr_on != 0u) {
        c = mix(c, glare_at(px), f.a_night2.z);
        if (vis > 0.0) {
            vec2 nd = px * f.ndc_per_pixel - 1.0;
            vec3 dir = normalize(f.forward.xyz + f.right.xyz * (nd.x / f.proj_x) - f.up.xyz * (nd.y / f.proj_y));
            c += glare_corona(dir, vis);
        }
    }
    if ((f.lens_on & LENS_FLARE) != 0u && vis > 0.0) c += lens_flare(px);
    if ((f.lens_on & LENS_VIGNETTE) != 0u) c *= lens_vignette(px);
    o_color = vec4(lens_film(post_color(c), px), 1.0);
}
