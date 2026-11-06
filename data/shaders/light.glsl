// Свет — пользователя (docs/invariants.md, шов 6; docs/roadmap.md, этап 6).
// ВРЕМЕННЫЙ свет этапа 1, чтобы было видно сетки: солнце по Ламберту и небо одним цветом. Без теней и дымки.
// Пользователь пишет свой алгоритм на месте этой функции; каркас зовёт только light_surface()
vec3 light_surface(Surface s) {
    FrameData f = pc.frame;
    vec3 l = normalize(f.sun_dir.xyz);
    float ndl = max(dot(s.normal, l), 0.0);
    vec3 sky = f.sky_color.rgb * (0.6 + 0.4 * s.normal.y);
    return s.albedo * (f.sun_color.rgb * ndl + sky * s.ao) + s.emissive;
}
