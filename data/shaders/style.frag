#version 460
#extension GL_GOOGLE_include_directive : require
// Проход стиля (docs/render.md, проход 8): HDR-картинка кадра мира после сглаживания (история этого кадра, pc.a;
// docs/aa.md) → блеск (docs/glare.md), ночная синева, тонмаппинг (post_color). Потом сюда же — виньетка, зерно,
// сканлайны, аберрация
#include "post.glsl"
#include "glare.glsl"
layout(set = 0, binding = 13, rgba16f) uniform readonly image2D u_aa[2];
layout(location = 0) out vec4 o_color;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 c = pc.a == 0u ? imageLoad(u_aa[0], p).rgb : imageLoad(u_aa[1], p).rgb;
    // Блеск: доля света кадра уходит в рассеяние вокруг ярких мест (энергия сохраняется), у солнца — корона
    FrameData f = pc.frame;
    if (f.gr_on != 0u) {
        c = mix(c, glare_at(gl_FragCoord.xy), f.a_night2.z);
        float vf = dot(f.a_sun.xyz, f.forward.xyz);
        if (vf > 0.05 && f.a_sun.y > -0.02) {
            // Видимость диска: пять точек по нему в кадре после сглаживания ярче половины его предела
            vec2 ndc = vec2(dot(f.a_sun.xyz, f.right.xyz) * f.proj_x, -dot(f.a_sun.xyz, f.up.xyz) * f.proj_y) / vf;
            vec2 sp = (ndc + 1.0) / f.ndc_per_pixel;
            float rp = 0.5 * f.a_sun_halo.w / (f.pixel_angle * vf);
            float thr = 0.4 * atmo_sun_cap(), vis = 0.0;
            for (int k = 0; k < 5; ++k) {
                vec2 o = k == 0 ? vec2(0.0) : vec2(k == 1 ? rp : k == 2 ? -rp : 0.0, k == 3 ? rp : k == 4 ? -rp : 0.0);
                ivec2 q = ivec2(sp + o);
                if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, ivec2(f.viewport)))) continue;
                vec3 h = pc.a == 0u ? imageLoad(u_aa[0], q).rgb : imageLoad(u_aa[1], q).rgb;
                vis += step(thr, max(h.r, max(h.g, h.b))) * 0.2;
            }
            vec2 nd = (gl_FragCoord.xy * f.ndc_per_pixel) - 1.0;
            vec3 dir = normalize(f.forward.xyz + f.right.xyz * (nd.x / f.proj_x) - f.up.xyz * (nd.y / f.proj_y));
            c += glare_corona(dir, vis);
        }
    }
    o_color = vec4(post_color(c), 1.0);
}
