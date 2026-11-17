// Интерфейс (src/render/vk_ui.cpp, docs/ui.md): прямоугольник — RenderUiQuad, данные по адресам из push-констант
#ifndef UI_COMMON_GLSL
#define UI_COMMON_GLSL
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

struct UiQuad { vec4 rect; vec4 uv; vec4 clip; uint rgba; float feather; float radius; float stroke; };
layout(buffer_reference, scalar) readonly buffer UiQuads { UiQuad v[]; };
layout(buffer_reference, scalar) readonly buffer UiAtlas { uint v[]; };

layout(push_constant, scalar) uniform UiPush {
    UiQuads quads;
    UiAtlas atlas;
    vec2 screen;  // пиксели цели
    uint atlas_w;
    uint pad;
} up;
#endif
