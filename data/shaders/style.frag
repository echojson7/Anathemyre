#version 460
#extension GL_GOOGLE_include_directive : require
// Проход стиля (docs/render.md, проход 8): HDR-картинка кадра мира после сглаживания (история этого кадра, pc.a;
// docs/aa.md) → ночная синева, тонмаппинг (post_color). Потом сюда же — виньетка, зерно, сканлайны, аберрация
#include "post.glsl"
layout(set = 0, binding = 11, rgba16f) uniform readonly image2D u_aa[2];
layout(location = 0) out vec4 o_color;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 c = pc.a == 0u ? imageLoad(u_aa[0], p).rgb : imageLoad(u_aa[1], p).rgb;
    o_color = vec4(post_color(c), 1.0);
}
