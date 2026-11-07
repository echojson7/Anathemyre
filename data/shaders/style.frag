#version 460
#extension GL_GOOGLE_include_directive : require
// Проход стиля (docs/render.md, проход 8): HDR-картинка кадра мира → экспозиция, ночная синева, тонмаппинг
// (post_color). Потом сюда же — виньетка, зерно, сканлайны, аберрация
#include "post.glsl"
layout(set = 0, binding = 1, rgba16f) uniform readonly image2D u_hdr;
layout(location = 0) out vec4 o_color;

void main() { o_color = vec4(post_color(imageLoad(u_hdr, ivec2(gl_FragCoord.xy)).rgb), 1.0); }
