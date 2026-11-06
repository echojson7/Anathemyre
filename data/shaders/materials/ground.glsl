// Сухая земля: color — земля, color2 — пыль пятнами. texel — квадрат узора,
// extra.x — размер крупных пятен (м), extra.y — доля пятен пыли, extra.z — сила мелкого разброса,
// extra.w — сила трещин (такыр: сетка трещин ~0.4 м)
Surface surf_ground(SurfIn i) {
    float t = i.m.p1.a;
    vec3 q = snap(i.p, t);
    float big = value_noise(i.p, max(i.m.p2.x, 1.0), 11u) * 0.6 + value_noise(i.p, max(i.m.p2.x, 1.0) * 0.25, 12u) * 0.4;
    float dust = smoothstep(i.m.p2.y - 0.12, i.m.p2.y + 0.12, big);
    float grain = cell_noise(q, t, 13u) - 0.5;
    float clump = value_noise(i.p, t * 4.0, 14u) - 0.5;
    vec3 c = mix(i.m.p0.rgb, i.m.p1.rgb, dust);
    c *= 1.0 + (grain * 0.6 + clump * 0.8) * i.m.p2.z;
    // Трещины такыра: края ячеек Вороного 0.5 м, только пятнами (где нет пыли)
    float crack = 0.0;
    float takyr = smoothstep(0.5, 0.65, value_noise(i.p, 16.0, 16u)) * (1.0 - dust);
    float kf = 1.0 - smoothstep(0.008, 0.025, i.foot);  // трещины мельче пикселя гаснут
    if (i.m.p2.w > 0.0 && takyr > 0.0 && kf > 0.0) {
        const float S = 0.5;
        ivec3 c0 = world_cell(vec3(i.p.x, 0.0, i.p.z), S);
        vec2 l = fract(i.p.xz / S);
        float f1 = 9.0, f2 = 9.0;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                ivec3 cc = c0 + ivec3(dx, 0, dz);
                vec2 pt = vec2(dx, dz) + vec2(hash3(cc, 18u), hash3(cc, 19u)) * 0.9 + 0.05;
                float d = length(pt - l);
                if (d < f1) { f2 = f1; f1 = d; } else if (d < f2) f2 = d;
            }
        crack = (1.0 - smoothstep(0.03, 0.07, f2 - f1)) * i.m.p2.w * takyr * kf;
        c *= 1.0 - crack * 0.5;
    }
    // Редкие камешки: тёмные квадраты узора, гаснут, когда квадрат мельче пары пикселей
    if (cell_noise(q, t, 15u) > 0.985) c *= mix(1.0, 0.7, 1.0 - smoothstep(t * 0.25, t * 0.5, i.foot));
    Surface s;
    s.albedo = max(c, vec3(0.0));
    s.rough = i.m.p0.a;
    s.normal = i.n;
    s.ao = 1.0 - crack * 0.5;
    s.emissive = vec3(0.0);
    s.height = 0.45 + clump * 0.4 - crack * 0.3;
    return s;
}
