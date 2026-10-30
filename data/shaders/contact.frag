// Контактные тени (СВЕТ: КОНТАКТНЫЕ ТЕНИ): от точки к солнцу 12 шагов по глубине кадра на 0.8 м.
// Карта теней в 4 см на тексель не видит щель под камнем, складку ткани, ступню на полу; экран видит
layout(location = 0) in vec2 v_uv;

layout(binding = 7) uniform sampler2D u_depth;
layout(std140, binding = 33) uniform Shade {
    mat4  u_casc_vp[2];
    mat4  u_scene_vp;
    vec4  u_casc_ext;
    ivec4 u_shade;
};

layout(location = 0) out float o_contact;

// Дальность от камеры по глубине кадра
float dist_at(float d) { return u_pix.w / min(d * 2.0 - 1.0 + u_pix.z, -1e-6); }

void main()
{
    ivec2 t = ivec2(gl_FragCoord.xy);
    float d = texelFetch(u_depth, t, 0).r;
    o_contact = 1.0;
    if (d >= 1.0) return;
    vec4 w = u_inv_vp * vec4(v_uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec3 P = w.xyz / w.w;
    float dist0 = distance(P, u_cam_pos);
    if (dist0 > 40.0) return;
    vec2 res = vec2(textureSize(u_depth, 0));
    // Шаг растёт к концу луча, начало со сдвигом по шуму: полосы превращаются в зерно
    float j = hash12(gl_FragCoord.xy + float(u_frame % 64) * 1.7);
    float occ = 0.0;
    for (int i = 0; i < 12; i++) {
        float f = (float(i) + j) / 12.0;
        vec3 q = P + u_sun_dir * (0.02 + 0.78 * f * f);
        vec4 c = u_scene_vp * vec4(q, 1.0);
        vec3 n = c.xyz / c.w;
        if (any(greaterThan(abs(n.xy), vec2(1.0)))) break;
        vec2 uv = n.xy * 0.5 + 0.5;
        float ds = dist_at(texelFetch(u_depth, ivec2(uv * res), 0).r);
        float dq = dist_at(n.z * 0.5 + 0.5);
        // Заслонка ближе точки луча, но не толще 0.4 м (иначе это стена позади, а не тень)
        float gap = dq - ds;
        if (gap > 0.02 * (1.0 + dq * 0.02) && gap < 0.4) {
            occ = 1.0 - f;
            break;
        }
    }
    // Вдали контакт мельче пикселя: плавно сходит на нет к 40 м
    o_contact = 1.0 - occ * (1.0 - smoothstep(25.0, 40.0, dist0));
}
