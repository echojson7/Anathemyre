#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "textures.glsl"
// Карточки с прозрачным краем (волосы, docs/hair.md): альфа карты цвета материала (покрытие волосками) против
// порога по шуму экрана, новому каждый кадр. Пиксель остаётся долей своего покрытия, сглаживание (TAA) сводит
// это в мягкий край прядей. Ниже NEAR_SOLID — всегда сплошь (тело пряди не мерцает), у материала без карты — сплошь
layout(location = 0) flat in uint v_slot;
layout(location = 1) flat in uint v_mat;
layout(location = 2) in vec2 v_uv;
layout(location = 0) out uint o_id;

const float CUT_LO = 0.08;   // покрытие ниже — всегда пусто
const float CUT_HI = 0.80;   // выше — всегда волосы

void main() {
    FrameData f = pc.frame;
    uint tc = f.materials.v[v_mat].tex & 0xFFu;
    if (tc != 0u) {
        float a = texture(sampler2D(u_tex[nonuniformEXT(tc)], u_tex_smp), v_uv).a;
        vec2 px = gl_FragCoord.xy + 5.588238 * float(f.aa_frame & 63u);
        float d = fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715))));
        if (a < mix(CUT_LO, CUT_HI, d)) discard;
    }
    o_id = ((v_slot + 1u) << 7) | uint(gl_PrimitiveID);
}
