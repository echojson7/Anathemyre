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
    if (!l || !l->foot_count[0] || clip->keys < 2) return true;
    // Фаза 0 — удар левой пяткой: начало отрезка касания, перед которым стопа в воздухе. У петли отрезок с ключа 0
    // может быть продолжением отрезка, что идёт до конца клипа (последний ключ — первый): тогда начало — у того
    const u32 last = clip->keys - 1;
    u32 a = l->foot[0][0].a;
    for (u32 k = 0; k < l->foot_count[0]; ++k) {
        const AnimSpan &sp = l->foot[0][k];
        const bool cont = sp.a == 0 && anim_span_has(l->foot[0], l->foot_count[0], last - 1);
        if (!cont) {
            a = sp.a;
            break;
        }
    }
    c.off = (f32)a / (f32)last;
    return true;
}

static f32 root_len(const AnimClip &c) {
    // Ход корня за клип, м: у петли шага — длина цикла
    if (c.keys < 2) return 0.0f;
    const i32 *r = c.root + (usize)(c.keys - 1) * 3;
    f32 x = (f32)r[0] * 0.001f, z = (f32)r[1] * 0.001f;
    return f_sqrt(x * x + z * z);
}

bool gait_bind(GaitSet &g, const Skel &s, const AnimClip *idle, const AnimClip *walk, const AnimLabels *walk_l,
               const AnimClip *jog, const AnimLabels *jog_l, const AnimClip *run, const AnimLabels *run_l) {
    g = GaitSet{};
    i32 t[4] = {-1, -1, -1, -1};
    g.ok = bind_one(g.idle, s, idle, nullptr, &t[0]) && bind_one(g.walk, s, walk, walk_l, &t[1]) &&
           bind_one(g.jog, s, jog, jog_l, &t[2]) && bind_one(g.run, s, run, run_l, &t[3]) && t[0] == t[1] &&
           t[1] == t[2] && t[2] == t[3] && t[0] >= 0;
    g.trans_bone = t[0];
    if (g.ok) {
        g.len[0] = root_len(*walk);
        g.len[1] = root_len(*jog);
        g.len[2] = root_len(*run);
    }
    return g.ok;
}

GaitMix gait_mix(f32 v) {
    // Покой → шаг до 0.7 м/с (трогается и встаёт), шаг → трусца 2.0–2.6, трусца → бег 3.6–4.3 м/с
    return {smooth01(0.03f, 0.7f, v), smooth01(2.0f, 2.6f, v), smooth01(3.6f, 4.3f, v)};
}

f32 gait_natural_len(const GaitSet &g, GaitMix m) {
    f32 a = g.len[0] + (g.len[1] - g.len[0]) * m.jog;
    return a + (g.len[2] - a) * m.run;
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

// Смесь двух соседних клипов походки по фазе: out = a·(1 − w) + b·w; поворот в out, таз в *trans
static void mix_two(const GaitClip &a, const GaitClip &b, f32 w, u32 nb, u32 phase, Quat *out, Quat *tmp, Vec3 *trans) {
    AnimPose pa{out, {}, 0, 0, 0}, pb{tmp, {}, 0, 0, 0};
    if (w < 1.0f) anim_sample(*a.clip, a.map, nb, clip_time(a, phase), pa);
    if (w > 0.0f) {
        anim_sample(*b.clip, b.map, nb, clip_time(b, phase), w < 1.0f ? pb : pa);
        if (w < 1.0f) anim_blend(pa, pb, nb, w);
    }
    *trans = pa.trans;
}

void gait_pose(const GaitSet &g, u32 nb, GaitMix m, u32 phase, f32 idle_t, Quat *rot, Vec3 *trans, Quat *tmp,
               Quat *tmp2, f32 contact[2]) {
    // Шаг ↔ трусца, потом с бегом: соседние клипы одного актёра смешиваются по общей фазе (фаза 0 — левая стопа)
    if (m.run <= 0.0f) mix_two(g.walk, g.jog, m.jog, nb, phase, rot, tmp, trans);
    else if (m.jog >= 1.0f) mix_two(g.jog, g.run, m.run, nb, phase, rot, tmp, trans);
    else {
        Vec3 t2;
        mix_two(g.walk, g.jog, m.jog, nb, phase, rot, tmp, trans);
        mix_two(g.jog, g.run, 1.0f, nb, phase, tmp2, tmp, &t2);
        AnimPose a{rot, *trans, 0, 0, 0}, b{tmp2, t2, 0, 0, 0};
        anim_blend(a, b, nb, m.run);
        *trans = a.trans;
    }
    const GaitClip &main = m.run >= 0.5f ? g.run : m.jog >= 0.5f ? g.jog : g.walk;
    f32 tm = clip_time(main, phase);
    for (u32 f = 0; f < 2; ++f) contact[f] = contact_at(main, tm, f);
    if (m.move < 1.0f) {
        // Покой — своя петля по времени человека; a = покой·(1 − move) + ходьба·move
        AnimPose a{rot, *trans, 0, 0, 0}, b{tmp, {}, 0, 0, 0};
        f32 dur = anim_duration(*g.idle.clip);
        f32 t = idle_t - dur * (f32)f_floor_i(idle_t / dur);
        anim_sample(*g.idle.clip, g.idle.map, nb, t, b);
        anim_blend(b, a, nb, m.move);
        for (u32 i = 0; i < nb; ++i) rot[i] = tmp[i];
        *trans = b.trans;
        for (u32 f = 0; f < 2; ++f) contact[f] = contact[f] * m.move + (1.0f - m.move);
    }
}
