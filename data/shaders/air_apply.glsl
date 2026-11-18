// Воздух поверх освещения (docs/air.md, light_cells.comp): цвет точки пикселя гаснет по пути к камере, взамен —
// свет, рассеянный пылью: клетки (рассеянный и огни, сумма вдоль луча — трилинейно) и лучи солнца половины кадра
// (четыре соседа: билинейно × вес по расстоянию — у края предмета свет в пыли перед ним не течёт на даль за ним).
// Только compute: место 18 набора кадра мира
#ifndef AIR_APPLY_GLSL
#define AIR_APPLY_GLSL
#include "air.glsl"
layout(set = 0, binding = 20) uniform sampler3D u_air_sum;

// Воздух показан: включён в кадре, и режим F3 не заменяет кадр картой (тени, поле, зонды, froxels)
bool air_shown() {
    FrameData f = pc.frame;
    return f.air_on != 0u && f.sm_debug == 0u && (f.sdf_view == SDF_VIEW_FRAME || f.sdf_view == SDF_VIEW_OFF) &&
           f.froxel_heat == 0u && f.gi_view != 1u && f.gi_view != 3u;  // GI_VIEW_ONLY, GI_VIEW_CELLS
}

// c — цвет точки (× экспозиция кадра и местная), d — до неё вдоль луча, м (небо — больше AIR_FAR), gain — местная
// экспозиция точки: свет в пыли её получает, только если она приглушает (яркое место за пылью), — подъём тёмного
// места пелену перед ним не поднимает
vec3 air_apply(ivec2 px, vec3 c, float d, float gain) {
    FrameData f = pc.frame;
    vec2 q = vec2(px) + 0.5;
    vec4 a = textureLod(u_air_sum, vec3(q / (float(AIR_TILE) * vec2(f.air_nx, f.air_ny)), air_z(min(d, AIR_FAR))), 0.0);
    vec2 hq = q * 0.5 - 0.5;
    ivec2 b = ivec2(floor(hq));
    vec2 fr = hq - vec2(b);
    ivec2 hmax = ivec2(f.air_w2, f.air_h2) - 1;
    vec3 s = vec3(0.0);
    float ws = 0.0;
    for (int j = 0; j < 4; ++j) {
        ivec2 o = ivec2(j & 1, j >> 1);
        ivec2 h = clamp(b + o, ivec2(0), hmax);
        uint i = uint(h.y) * f.air_w2 + uint(h.x);
        float wb = (o.x == 1 ? fr.x : 1.0 - fr.x) * (o.y == 1 ? fr.y : 1.0 - fr.y);
        float w = wb * (air_depth_w(f.air_depth.v[i], d) + 1e-4);
        s += w * air_unpack(f.air_tmp.v[i]);
        ws += w;
    }
    return c * a.a + (a.rgb + s / max(ws, 1e-12)) * min(gain, 1.0);
}
#endif
