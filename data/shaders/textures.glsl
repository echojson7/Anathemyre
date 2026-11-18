// Текстуры ассетов (docs/textures.md, vk_textures.cpp): таблица картинок BC набора кадра мира и один сэмплер
// (трилинейно, анизотропия 16, с заворотом). Номер картинки — из материала (Material.tex, по байту на карту).
// Производных фрагмента в compute нет: мип и анизотропию выбирает приращение развёртки на пиксель (resolve.glsl)
#ifndef TEXTURES_GLSL
#define TEXTURES_GLSL
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 16) uniform texture2D u_tex[256];
layout(set = 0, binding = 17) uniform sampler u_tex_smp;

// Карта slot (1…255; 0 — нет) в точке uv с приращениями на пиксель вправо и вниз
vec4 tex_grad(uint slot, vec2 uv, vec4 duv) {
    return textureGrad(sampler2D(u_tex[nonuniformEXT(slot)], u_tex_smp), uv, duv.xy, duv.zw);
}
#endif
