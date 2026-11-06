// Общее для шейдеров этапа 0: push constants (как PushConsts в src/render/vk_frame.cpp) и цвет
#extension GL_EXT_buffer_reference : require

struct Vtx { vec2 pos; uint a; uint b; };  // RenderPoint: a = размер (float), b = цвет; RenderLineVertex: a = цвет

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer VtxBuf { Vtx v[]; };

layout(push_constant) uniform Push {
    VtxBuf data;
    vec2 meters_to_ndc;
    vec2 ndc_per_pixel;
} pc;

// sRGB 8 бит → линейный цвет (картинка swapchain *_SRGB сама кодирует обратно)
vec4 unpack_srgb(uint rgba) {
    vec4 c = unpackUnorm4x8(rgba);
    vec3 lo = c.rgb / 12.92;
    vec3 hi = pow((c.rgb + 0.055) / 1.055, vec3(2.4));
    return vec4(mix(hi, lo, lessThanEqual(c.rgb, vec3(0.04045))), c.a);
}

// Метры относительно камеры → NDC. Север (+y) — вверх экрана, у Vulkan y NDC вниз
vec2 to_ndc(vec2 m) { return vec2(m.x, -m.y) * pc.meters_to_ndc; }
