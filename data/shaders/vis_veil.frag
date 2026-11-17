#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Морок (docs/tat.md): машина остаётся долей своих пикселей — сито по шуму экрана, новое каждый кадр, сглаживание
// (TAA) сводит его в полупрозрачность. Морок поднимается от лап к мачте полосой с рваным краем; под ним остаётся
// дрожащая рябь, сильнее на гранях взгляда — читается как марево над песком, а не как дыра
layout(location = 0) flat in uint v_slot;
layout(location = 1) flat in float v_vis;
layout(location = 2) in vec3 v_local;
layout(location = 0) out uint o_id;

const float VEIL_TOP = 1.25;    // выше мачты, м от подошвы
const float VEIL_BAND = 0.22;   // ширина рваного края, м
const float VEIL_KEEP = 0.07;   // доля пикселей, что остаётся под мороком

float veil_hash(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
float veil_noise(vec3 p) {
    vec3 i = floor(p), u = fract(p);
    u = u * u * (3.0 - 2.0 * u);
    return mix(mix(mix(veil_hash(i), veil_hash(i + vec3(1, 0, 0)), u.x),
                   mix(veil_hash(i + vec3(0, 1, 0)), veil_hash(i + vec3(1, 1, 0)), u.x), u.y),
               mix(mix(veil_hash(i + vec3(0, 0, 1)), veil_hash(i + vec3(1, 0, 1)), u.x),
                   mix(veil_hash(i + vec3(0, 1, 1)), veil_hash(i + vec3(1, 1, 1)), u.x), u.y), u.z);
}

void main() {
    FrameData f = pc.frame;
    float t = f.a_night.y;
    float u = 1.0 - v_vis;
    // Край морока: фронт идёт от −полосы (весь виден) до верха (весь под мороком), рвётся шумом
    float front = mix(-VEIL_BAND, VEIL_TOP, u);
    float edge = veil_noise(v_local * 9.0 + vec3(0.0, -t * 1.5, 0.0));
    float k = smoothstep(front, front + VEIL_BAND, v_local.y + (edge - 0.5) * VEIL_BAND);
    // Под мороком — бегущая рябь: полосы шума, что текут вверх, как марево
    float ripple = veil_noise(v_local * vec3(6.0, 2.5, 6.0) + vec3(0.0, -t * 2.2, t * 0.7));
    float keep = VEIL_KEEP * (0.35 + 1.3 * ripple * ripple);
    float a = mix(keep, 1.0, k);
    // Сито: шум экрана с новым сдвигом каждый кадр (interleaved gradient noise)
    vec2 px = gl_FragCoord.xy + 5.588238 * float(f.aa_frame & 63u);
    float d = fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715))));
    if (d >= a) discard;
    o_id = ((v_slot + 1u) << 7) | uint(gl_PrimitiveID);
}
