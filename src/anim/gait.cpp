#include "gait.h"

static f32 smooth01(f32 a, f32 b, f32 x) {
    f32 t = f_clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static bool bind_one(GaitClip &c, const Skel &s, const AnimClip *clip, const AnimLabels *l, i32 *tb) {
    c.clip = clip;
    c.labels = l;
    c.off = 0.0f;
    if (!clip || !anim_bind(*clip, s, c.map, tb)) return false;
    if (l && l->foot_count[0] && clip->keys > 1) c.off = (f32)l->foot[0][0].a / (f32)(clip->keys - 1);
    return true;
}

bool gait_bind(GaitSet &g, const Skel &s, const AnimClip *idle, f32 idle_t, const AnimClip *walk, const AnimLabels *walk_l,
               const AnimClip *run, const AnimLabels *run_l) {
    g = GaitSet{};
    i32 t0 = -1, t1 = -1, t2 = -1;
    g.ok = bind_one(g.idle, s, idle, nullptr, &t0) && bind_one(g.walk, s, walk, walk_l, &t1) && bind_one(g.run, s, run, run_l, &t2) &&
           t0 == t1 && t1 == t2 && t0 >= 0;
    g.trans_bone = t0;
    g.idle_t = idle_t;
    return g.ok;
}

GaitMix gait_mix(f32 v) {
    // Шаг до 2 м/с, бег с 2.8 м/с; трогается и встаёт — между покоем и шагом до 0.7 м/с
    return {smooth01(0.03f, 0.7f, v), smooth01(2.0f, 2.8f, v)};
}

static f32 clip_time(const GaitClip &c, u32 phase) {
    f32 f = (f32)phase * (1.0f / 4294967296.0f) + c.off;
    return f * anim_duration(*c.clip);
}

static f32 contact_at(const GaitClip &c, f32 t, u32 foot) {
    if (!c.labels) return 1.0f;
    f32 dur = anim_duration(*c.clip);
    t -= dur * (f32)f_floor_i(t / dur);
    u32 k = (u32)(t * (f32)ANIM_KEYS_PER_S + 0.5f);
    if (k >= c.clip->keys - 1) k = 0;  // петля: последний ключ — первый
    return anim_span_has(c.labels->foot[foot], c.labels->foot_count[foot], k) ? 1.0f : 0.0f;
}

void gait_pose(const GaitSet &g, u32 nb, GaitMix m, u32 phase, Quat *rot, Vec3 *trans, Quat *tmp, f32 contact[2]) {
    AnimPose a{rot, {}, 0, 0, 0}, b{tmp, {}, 0, 0, 0};
    f32 tw = clip_time(g.walk, phase), tr = clip_time(g.run, phase);
    if (m.run < 1.0f) anim_sample(*g.walk.clip, g.walk.map, nb, tw, a);
    if (m.run > 0.0f) {
        anim_sample(*g.run.clip, g.run.map, nb, tr, m.run < 1.0f ? b : a);
        if (m.run < 1.0f) anim_blend(a, b, nb, m.run);
    }
    const GaitClip &main = m.run < 0.5f ? g.walk : g.run;
    f32 tm = m.run < 0.5f ? tw : tr;
    for (u32 f = 0; f < 2; ++f) contact[f] = contact_at(main, tm, f);
    if (m.move < 1.0f) {
        anim_sample(*g.idle.clip, g.idle.map, nb, g.idle_t, b);
        // a = покой·(1 − move) + ходьба·move
        anim_blend(b, a, nb, m.move);
        for (u32 i = 0; i < nb; ++i) rot[i] = tmp[i];
        a.trans = b.trans;
        for (u32 f = 0; f < 2; ++f) contact[f] = contact[f] * m.move + (1.0f - m.move);
    }
    *trans = a.trans;
}
