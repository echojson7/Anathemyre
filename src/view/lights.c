#include "lights.h"

#include "../core/hash.h"

u32 lights_collect(const Objects *o, V3 eye, f32 night, f32 time, PointLight *out, u32 max)
{
    u32 n = 0;
    for (u32 k = 0; k < o->lit_count && n < max; k++) {
        u32 i = o->lit[k];
        const ObjLight *g = &g_objects[o->def[i]].light;
        V3 d = v3_sub(o->pos[i], eye);
        f32 see = LIGHTS_SEE + g->radius;
        if (v3_dot(d, d) > see * see) continue;
        u32 seed = o->seed[i];
        f32 on = 1.0f;
        if (g->night >= 0.0f) {
            // Зажигаются не разом: порог у каждой лампы свой от зерна. Пока лампа разгорается, она мигает
            f32 a = g->night + g->spread * hashf(seed);
            on = smoothstepf(a, a + 0.12f, night);
            if (on <= 0.0f) continue;
            if (on < 1.0f && hashf((u32)(time * 9.0f) * 7u + seed) < 0.35f) continue;
        }
        if (g->flicker == FLICK_HUM) {
            // Дрожь напряжения: свет едва заметно дышит, изредка проседает
            on *= 0.96f + 0.04f * sinf(time * 13.0f + (f32)(seed & 255u) * 0.0246f);
            if (hashf((u32)(time * 4.0f) * 13u + seed * 101u) < 0.02f) on *= 0.6f;
        }
        M4 L = obj_frame(o, i);
        out[n++] = (PointLight){ .pos = m4_point(L, g->at), .radius = g->radius, .color = v3_mul(g->color, on),
                                 .cone = g->cone, .dir = v3_norm(m4_point(m4_rot_y(obj_yaw(o, i)), g->dir)), .glow = g->glow };
    }
    return n;
}
