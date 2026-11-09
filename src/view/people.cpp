#include "people.h"
#include "view.h"
#include "../anim/ik.h"
#include "../core/hash.h"
#include "../core/log.h"
#include "../sim/collide.h"
#include "../sim/ground.h"
#include <string.h>

// Поза покоя: клипа покоя пока нет — верх тела из шага в середине опоры на левую (ключ 30 из 33: руки у бёдер, корпус
// прямой), стопы ставит IK на ширину бинда (ноги из этого ключа — на полушаге)
static const char *const IDLE_CLIP = "walk";
constexpr f32 IDLE_T = 1.0f;
static const char *const BONE_NAMES[] = {"pelvis", "spine_01", "thigh_l", "thigh_r", "calf_l", "calf_r",
                                         "foot_l", "foot_r", "ball_l", "ball_r"};
enum { B_PELVIS, B_SPINE, B_THIGH, B_CALF = 4, B_FOOT = 6, B_BALL = 8, B_COUNT = 10 };

constexpr f32 HEAD_ABOVE_M = 1.62f;  // опора камеры за человеком над его корнем
constexpr f32 STEP_TIME = 0.3f;      // перестановка стопы на месте, с
constexpr f32 STEP_LIFT = 0.07f;     // подъём стопы при перестановке, м
constexpr f32 STEP_FAR = 0.13f;      // стопа ушла от места под телом дальше — переставить
constexpr f32 STEP_TURN = 0.45f;     // тело повернулось от стопы больше (рад) — переставить

void people_view_init(PeopleView &pv) {
    memset((void *)&pv, 0, sizeof pv);  // ~1.3 МБ: не через временный объект на стеке
    pv.sel = -1;
}

static f32 approach(f32 cur, f32 goal, f32 rate, f32 dt) { return cur + (goal - cur) * f_min(1.0f, rate * dt); }

static WPos add(const WPos &p, f32 x, f32 y, f32 z) {
    WPos q = p;
    q.x += x;
    q.y += y;
    q.z += z;
    wpos_rebase(q);
    return q;
}

// Оси тела: лицо +z, курс yaw (как quat_yaw). Сетка → мир и обратно (без высоты)
static void body_to_world(Turn yaw, f32 x, f32 z, f32 *wx, f32 *wz) {
    f32 s, c;
    sincos_turn(yaw, &s, &c);
    *wx = c * x + s * z;
    *wz = -s * x + c * z;
}
static void world_to_body(Turn yaw, f32 wx, f32 wz, f32 *x, f32 *z) {
    f32 s, c;
    sincos_turn(yaw, &s, &c);
    *x = c * wx - s * wz;
    *z = s * wx + c * wz;
}

static const ViewClip *find_clip(const ViewAssets &a, const char *name, u64 skel) {
    u64 h = hash_bytes(name, strlen(name));
    for (u32 i = 0; i < a.clip_count; ++i)
        if (a.clips[i].name == h && a.clips[i].clip.skel == skel) return &a.clips[i];
    return nullptr;
}

static bool bind(PeopleView &pv, const ViewAssets &a) {
    if (pv.bound) return !pv.failed;
    pv.bound = true;
    pv.failed = true;
    if (!a.body_count) return false;
    const ViewBody &b = a.bodies[0];
    u64 ge = hash_str("game_engine");
    u32 k = 0;
    while (k < b.skin_count && b.skel_name[k] != ge) ++k;
    if (k == b.skin_count) { log_warn("люди: у тела нет скелета game_engine"); return false; }
    const Skel &s = b.skel[k];
    const ViewClip *idle = find_clip(a, IDLE_CLIP, ge), *walk = find_clip(a, "walk", ge), *run = find_clip(a, "run", ge);
    if (!idle || !walk || !run ||
        !gait_bind(pv.gait, s, &idle->clip, IDLE_T, &walk->clip, &walk->labels, &run->clip, &run->labels)) {
        log_warn("люди: нет клипов %s, walk или run для game_engine — тел не будет", IDLE_CLIP);
        return false;
    }
    for (u32 i = 0; i < B_COUNT; ++i) {
        pv.bones[i] = skel_find(s, hash_str(BONE_NAMES[i]));
        if (pv.bones[i] < 0) { log_warn("люди: в скелете нет кости %s", BONE_NAMES[i]); return false; }
    }
    // Стопы в покое: как в бинде (ширина бёдер), вдоль хода — под тазом
    const SkelBone &pel = s.bones.data[pv.bones[B_PELVIS]];
    for (u32 f = 0; f < 2; ++f) {
        const SkelBone &fb = s.bones.data[pv.bones[B_FOOT + f]];
        pv.rest_foot[f] = Vec3{fb.pos[0], fb.pos[1], fb.pos[2] - pel.pos[2]};
    }
    pv.body = 0;
    pv.skin = k;
    pv.failed = false;
    return true;
}

// Корень человека i в кадре: место, курс, скорость, фаза — между двумя выписками; ускорение и поворот — за шаг
struct Root {
    WPos at;
    Turn yaw;
    f32 v, acc, yaw_rate;
    u32 phase;
};
static Root person_root(const Extract &p, const Extract &c, f32 a, u32 i) {
    Root r;
    r.at = WPos{c.person_cx[i], c.person_cz[i], c.person_x[i], c.person_y[i], c.person_z[i]};
    r.yaw = c.person_heading[i];
    r.v = c.person_speed[i];
    r.phase = c.person_phase[i];
    r.acc = r.yaw_rate = 0.0f;
    if (p.person_gen[i] != c.person_gen[i]) return r;
    WPos q{p.person_cx[i], p.person_cz[i], p.person_x[i], p.person_y[i], p.person_z[i]};
    Vec2 d = wpos_delta_xz(q, r.at);
    f32 b = 1.0f - a;
    r.at = add(q, d.x * a, (r.at.y - q.y) * a, d.y * a);
    f32 dy = turn_diff_rad(p.person_heading[i], c.person_heading[i]);
    r.yaw = turn_add_rad(p.person_heading[i], dy * a);
    r.v = p.person_speed[i] * b + c.person_speed[i] * a;
    r.phase = p.person_phase[i] + (u32)(i32)((f32)(i32)(c.person_phase[i] - p.person_phase[i]) * a);
    r.acc = (c.person_speed[i] - p.person_speed[i]) * (f32)SIM_HZ;
    r.yaw_rate = dy * (f32)SIM_HZ;
    return r;
}

// Опора под точкой мира: высота и нормаль (земля — наклон, коробка — вверх)
static f32 support(const Extract &c, const WPos &p, f32 from_y, Vec3 *n) {
    Support s = support_at(*c.terrain, *c.coll, p.cx, p.cz, p.x, p.z, from_y);
    if (n) *n = s.kind ? Vec3{0, 1, 0} : ground_at(*c.terrain, p).normal;
    return s.height;
}

static void trail_add(PersonLook &L, const WPos &p) {
    L.trail[L.trail_at] = p;
    L.trail_at = (L.trail_at + 1) % VIEW_TRAIL;
    if (L.trail_n < VIEW_TRAIL) L.trail_n++;
}

struct Lines {
    RenderLineVertex *v;
    u32 &n;
    WPos eye;
    void line(const WPos &a, const WPos &b, u32 rgba) {
        if (n + 2 > VIEW_PEOPLE_LINES) return;
        Vec2 da = wpos_delta_xz(eye, a), db = wpos_delta_xz(eye, b);
        v[n++] = {da.x, a.y - eye.y, da.y, rgba};
        v[n++] = {db.x, b.y - eye.y, db.y, rgba};
    }
    void ring(const WPos &c, f32 r, u32 rgba, u32 seg = 24) {
        for (u32 k = 0; k < seg; ++k) {
            f32 s0, c0, s1, c1;
            sincos_turn((Turn)(k * (0x100000000ull / seg)), &s0, &c0);
            sincos_turn((Turn)((k + 1) * (0x100000000ull / seg)), &s1, &c1);
            line(add(c, s0 * r, 0, c0 * r), add(c, s1 * r, 0, c1 * r), rgba);
        }
    }
};

static u32 body_snorm2(f32 a, f32 b) {
    auto q = [](f32 v) -> u32 {
        f32 x = f_clamp(v, -1.0f, 1.0f) * 32767.0f;
        i32 i = (i32)(x + (x >= 0 ? 0.5f : -0.5f));
        return (u32)(u16)(i16)i;
    };
    return q(a) | (q(b) << 16);
}

// Поза одного человека: повороты костей → палитра, корень экземпляра. Возвращает высоту корня экземпляра
struct PoseOut {
    WPos base;  // корень экземпляра (с опусканием таза)
};

static PoseOut pose_person(PeopleView &pv, const ViewAssets &assets, const ViewInput &in, u32 i, const Root &r, f32 dist,
                           PoseMatrix *palette, bool debug) {
    const Extract &c = *in.cur;
    const Skel &s = assets.bodies[pv.body].skel[pv.skin];
    const u32 nb = (u32)s.bones.count;
    PersonLook &L = pv.look[i];
    const f32 dt = f_clamp(in.dt, 0.0f, 0.1f);
    const i32 *B = pv.bones;
    bool fresh = L.gen != c.person_gen[i] || !L.drawn;
    if (L.gen != c.person_gen[i]) {
        L = PersonLook{};
        L.gen = c.person_gen[i];
    }
    // Сглаживание корня: высота (ступени), ускорение и поворот (наклон)
    if (fresh || f_abs(r.at.y - L.y) > 1.0f) L.y = r.at.y;
    else L.y = approach(L.y, r.at.y, 14.0f, dt);
    L.acc = fresh ? 0.0f : approach(L.acc, f_clamp(r.acc, -8.0f, 8.0f), 6.0f, dt);
    L.side = fresh ? 0.0f : approach(L.side, f_clamp(r.v * r.yaw_rate, -8.0f, 8.0f), 6.0f, dt);

    Quat rot[SKEL_MAX_BONES], tmp[SKEL_MAX_BONES], wr[SKEL_MAX_BONES];
    Vec3 wp[SKEL_MAX_BONES], trans;
    f32 contact[2];
    GaitMix m = gait_mix(r.v);
    gait_pose(pv.gait, nb, m, r.phase, rot, &trans, tmp, contact);
    const i32 tb = pv.gait.trans_bone;

    // Наклон: вперёд от ускорения (позвоночник), вбок к центру поворота (таз; стопы вернёт IK)
    pose_world(s, rot, tb, &trans, wr, wp);
    f32 lean_f = f_clamp(L.acc * 0.03f, -0.1f, 0.14f), lean_s = f_clamp(L.side * 0.035f, -0.15f, 0.15f);
    pose_turn_world(rot, wr, (u32)B[B_SPINE], quat_axis_angle(Vec3{1, 0, 0}, lean_f));
    pose_turn_world(rot, wr, (u32)B[B_PELVIS], quat_axis_angle(Vec3{0, 0, 1}, -lean_s));
    pose_world(s, rot, tb, &trans, wr, wp);

    PoseOut out;
    bool ik = dist < VIEW_IK_FAR_M && c.coll && c.terrain;
    if (!ik) {
        L.lock = false;
        L.drop = 0.0f;
        out.base = r.at;
        out.base.y = L.y;
    } else {
        // Подгонка длины шага: стопа вдоль хода — от таза в k раз, чтобы стоящая стопа не скользила
        f32 nat = c.gait->walk_len + (c.gait->run_len - c.gait->walk_len) * m.run;
        f32 k = 1.0f + (gait_cycle_len(*c.gait, r.v) / nat - 1.0f) * m.move;
        f32 pz = wp[B[B_PELVIS]].z;
        WPos want[2];
        f32 ankle_h[2], toe_dz[2];
        for (u32 f = 0; f < 2; ++f) {
            Vec3 a = wp[B[B_FOOT + f]];
            if (m.move < 1.0f) {  // покой: стопы — как в бинде под тазом
                Vec3 rest = pv.rest_foot[f] + Vec3{0, 0, pz};
                a = rest + (a - rest) * m.move;
            }
            f32 z = pz + (a.z - pz) * k, wx, wz;
            body_to_world(r.yaw, a.x, z, &wx, &wz);
            want[f] = add(r.at, wx, 0, wz);
            ankle_h[f] = a.y;
            toe_dz[f] = wp[B[B_BALL + f]].z - a.z;
        }
        // Стоит: стопы держатся на месте и переставляются по одной, если тело ушло или повернулось
        bool standing = m.move < 0.05f;
        if (!standing) L.lock = false;
        else if (!L.lock || fresh) {
            L.lock = true;
            for (u32 f = 0; f < 2; ++f) {
                L.lock_at[f] = want[f];
                L.lock_yaw[f] = r.yaw;
                L.step_t[f] = -1.0f;
            }
        } else {
            for (u32 f = 0; f < 2; ++f)
                if (L.step_t[f] >= 0.0f && (L.step_t[f] += dt / STEP_TIME) >= 1.0f) {
                    L.step_t[f] = -1.0f;
                    L.lock_at[f] = want[f];
                    L.lock_yaw[f] = r.yaw;
                }
            if (L.step_t[0] < 0.0f && L.step_t[1] < 0.0f) {
                f32 err[2];
                for (u32 f = 0; f < 2; ++f) {
                    Vec2 d = wpos_delta_xz(L.lock_at[f], want[f]);
                    f32 turn = f_abs(turn_diff_rad(L.lock_yaw[f], r.yaw));
                    err[f] = (length(d) > STEP_FAR || turn > STEP_TURN) ? length(d) + turn * 0.3f : 0.0f;
                }
                u32 f = err[1] > err[0] ? 1 : 0;
                if (err[f] > 0.0f) {
                    L.step_t[f] = 0.0f;
                    L.step_from[f] = L.lock_at[f];
                    L.step_yaw0[f] = L.lock_yaw[f];
                }
            }
        }
        // Цели стоп: место, опора под пяткой и носком, высота стопы клипа над его полом
        Vec3 target[2], normal[2];
        f32 yaw_off[2] = {0, 0}, lift[2] = {0, 0}, sup[2];
        for (u32 f = 0; f < 2; ++f) {
            WPos at = want[f];
            if (L.lock) {
                if (L.step_t[f] >= 0.0f) {
                    f32 t = L.step_t[f], e = t * t * (3.0f - 2.0f * t);
                    Vec2 d = wpos_delta_xz(L.step_from[f], want[f]);
                    at = add(L.step_from[f], d.x * e, 0, d.y * e);
                    yaw_off[f] = turn_diff_rad(r.yaw, L.step_yaw0[f]) * (1.0f - e);
                    lift[f] = STEP_LIFT * 4.0f * t * (1.0f - t);
                } else {
                    at = L.lock_at[f];
                    yaw_off[f] = turn_diff_rad(r.yaw, L.lock_yaw[f]);
                }
            }
            f32 tx, tz;
            body_to_world(r.yaw, 0.0f, toe_dz[f], &tx, &tz);
            // Опора — не выше шага над корнем (стена и цоколь рядом — не опора); носок над ступенью выше пятки
            // (марш: стопа на ребре) поднимает стопу, носок над чем-то выше ступени — нет
            Vec3 n;
            f32 h = support(c, at, L.y + 0.05f, &n);
            f32 ht = support(c, add(at, tx, 0, tz), L.y + 0.05f, nullptr);
            sup[f] = ht > h && ht - h < 0.22f ? ht : h;
            normal[f] = n;
            if (fresh) L.foot_h[f] = sup[f];
            else L.foot_h[f] = approach(L.foot_h[f], sup[f], contact[f] > 0.5f ? 30.0f : 16.0f, dt);
            Vec2 d = wpos_delta_xz(r.at, at);
            f32 bx, bz;
            world_to_body(r.yaw, d.x, d.y, &bx, &bz);
            target[f] = Vec3{bx, 0.0f, bz};
            at.y = L.foot_h[f] + ankle_h[f] + lift[f];
            if (debug && contact[f] > 0.5f && m.move > 0.5f) {
                const WPos &last = L.trail[(L.trail_at + VIEW_TRAIL - 1) % VIEW_TRAIL];
                WPos mark = at;
                mark.y = L.foot_h[f];
                if (!L.trail_n || length(wpos_delta_xz(last, mark)) > 0.03f || f_abs(last.y - mark.y) > 0.03f) trail_add(L, mark);
            }
            target[f].y = L.foot_h[f] + ankle_h[f] + lift[f];
        }
        // Таз опускается до нижней опоры стоп (спуск, ступень ниже корня)
        f32 low = f_min(L.foot_h[0], L.foot_h[1]) - L.y;
        L.drop = fresh ? f_clamp(low, -0.45f, 0.0f) : approach(L.drop, f_clamp(low, -0.45f, 0.0f), 10.0f, dt);
        f32 base_y = L.y + L.drop;
        out.base = r.at;
        out.base.y = base_y;
        for (u32 f = 0; f < 2; ++f) target[f].y -= base_y;
        for (u32 f = 0; f < 2; ++f)
            ik_two_bone(rot, wr, wp, (u32)B[B_THIGH + f], (u32)B[B_CALF + f], (u32)B[B_FOOT + f], target[f], Vec3{0, 0, 1});
        pose_world(s, rot, tb, &trans, wr, wp);
        for (u32 f = 0; f < 2; ++f) {
            // Стопа по опоре (на касании) и по своему курсу, пока стоит на месте
            Vec3 n = normal[f];
            f32 bx, bz;
            world_to_body(r.yaw, n.x, n.z, &bx, &bz);
            ik_foot_align(rot, wr, (u32)B[B_FOOT + f], Vec3{bx, n.y, bz}, 0.35f, contact[f]);
            if (yaw_off[f] != 0.0f) pose_turn_world(rot, wr, (u32)B[B_FOOT + f], quat_axis_angle(Vec3{0, 1, 0}, yaw_off[f]));
        }
        pose_world(s, rot, tb, &trans, wr, wp);
    }
    pose_palette_world(s, wr, wp, palette);
    L.root = out.base;
    L.yaw = r.yaw;
    return out;
}

void people_view_build(PeopleView &pv, const ViewAssets &assets, const ViewInput &in, const WPos &eye, Arena &frame,
                       RenderInstance *inst, u32 &ni, RenderFrame &out, RenderLineVertex *lines, u32 &ln, bool debug) {
    const Extract &c = *in.cur;
    if (!c.person_gen || !bind(pv, assets)) return;
    const ViewBody &body = assets.bodies[pv.body];
    // Ближние к глазу — с телом
    u32 pick[VIEW_BODIES_MAX];
    f32 pick_d[VIEW_BODIES_MAX];
    u32 np = 0;
    for (u32 i = 0; i < PEOPLE_CAPACITY; ++i) {
        if (!c.person_gen[i]) {
            pv.look[i].drawn = false;
            continue;
        }
        Vec2 d = wpos_delta_xz(eye.cx, eye.cz, eye.x, eye.z, c.person_cx[i], c.person_cz[i], c.person_x[i], c.person_z[i]);
        f32 d2 = dot(d, d);
        if (d2 > VIEW_BODY_FAR_M * VIEW_BODY_FAR_M) continue;
        if (np == VIEW_BODIES_MAX && d2 >= pick_d[np - 1]) continue;
        u32 k = np < VIEW_BODIES_MAX ? np++ : np - 1;
        while (k > 0 && pick_d[k - 1] > d2) {
            pick[k] = pick[k - 1];
            pick_d[k] = pick_d[k - 1];
            --k;
        }
        pick[k] = i;
        pick_d[k] = d2;
    }
    // Копии сетки: у кого была — та же; остальным — свободные или новые
    bool keep[VIEW_BODIES_MAX] = {};
    i32 slot_copy[VIEW_BODIES_MAX];
    for (u32 k = 0; k < np; ++k) {
        slot_copy[k] = -1;
        for (u32 j = 0; j < pv.copy_count; ++j)
            if (pv.copy_owner[j] == pick[k] + 1) { slot_copy[k] = (i32)j; keep[j] = true; }
    }
    for (u32 j = 0; j < pv.copy_count; ++j)
        if (!keep[j]) {
            if (pv.copy_owner[j]) pv.look[pv.copy_owner[j] - 1].drawn = false;
            pv.copy_owner[j] = 0;
        }
    for (u32 k = 0; k < np; ++k) {
        if (slot_copy[k] >= 0) continue;
        u32 j = 0;
        while (j < pv.copy_count && pv.copy_owner[j]) ++j;
        if (j == pv.copy_count) {
            if (pv.copy_count == VIEW_BODIES_MAX) continue;
            u32 mesh = render_skinned_create(body.skin[pv.skin]);
            if (!mesh) break;
            pv.copy[pv.copy_count++] = mesh;
        }
        pv.copy_owner[j] = (u16)(pick[k] + 1);
        pv.look[pick[k]].drawn = false;
        slot_copy[k] = (i32)j;
    }

    const Skel &s = body.skel[pv.skin];
    const u32 nb = (u32)s.bones.count;
    static_assert(sizeof(PoseMatrix) == sizeof(RenderBone), "матрица позы — как кость рендера");
    usize nb0 = out.bones.count, ns0 = out.skinned.count;
    RenderBone *bones = arena_push<RenderBone>(frame, nb0 + (usize)np * nb);
    if (nb0) memcpy(bones, out.bones.data, nb0 * sizeof(RenderBone));
    RenderSkinned *sk = arena_push<RenderSkinned>(frame, ns0 + np);
    if (ns0) memcpy(sk, out.skinned.data, ns0 * sizeof(RenderSkinned));
    usize nbo = nb0, nso = ns0;
    // Морфы внешности по зерну: полнота или худоба, сила, нос, челюсть
    static const char *const MORPHS[] = {"full", "thin", "strong", "nose", "jaw"};
    i32 morph_at[5];
    for (u32 q = 0; q < 5; ++q) {
        morph_at[q] = -1;
        for (u32 j = 0; j < body.morph_count; ++j)
            if (body.morph_name[j] == hash_str(MORPHS[q])) morph_at[q] = (i32)j;
    }
    for (u32 k = 0; k < np; ++k) {
        if (slot_copy[k] < 0 || ni >= RENDER_MAX_INSTANCES) continue;
        u32 i = pick[k];
        Root r = person_root(*in.prev, c, in.alpha, i);
        PoseOut po = pose_person(pv, assets, in, i, r, f_sqrt(pick_d[k]), (PoseMatrix *)(bones + nbo), debug);
        pv.look[i].drawn = true;
        RenderInstance &it = inst[ni];
        it = RenderInstance{};
        it.cx = po.base.cx;
        it.cz = po.base.cz;
        it.x = po.base.x;
        it.y = po.base.y;
        it.z = po.base.z;
        Quat q = quat_yaw(r.yaw);
        it.rot_xy = body_snorm2(q.x, q.y);
        it.rot_zw = body_snorm2(q.z, q.w);
        it.scale = 1.0f;
        it.mesh = pv.copy[slot_copy[k]];
        it.material = RENDER_NO_MATERIAL;
        it.seed = c.person_seed[i];
        RenderSkinned &o = sk[nso++];
        o = RenderSkinned{};
        o.instance = ni;
        o.first_bone = (u32)nbo;
        u64 h = hash_mix(c.person_seed[i] | 0x5EED00000000ull);
        auto r01 = [&](u32 sh) { return (f32)((h >> sh) & 0xFF) / 255.0f; };
        f32 t = r01(0) * 2.0f - 1.0f;
        f32 w[5] = {f_max(t, 0.0f) * 0.7f, f_max(-t, 0.0f) * 0.6f, r01(8) * 0.6f, r01(16) * 0.7f, r01(24) * 0.7f};
        for (u32 q2 = 0; q2 < 5; ++q2)
            if (morph_at[q2] >= 0 && morph_at[q2] < (i32)RENDER_MAX_MORPHS) o.morph[morph_at[q2]] = w[q2];
        ni++;
        nbo += nb;
    }
    out.bones = {bones, nbo};
    out.skinned = {sk, nso};

    // Метки: выбранный — кольцо у ног; точка приказа — кольцо с крестом, гаснет, когда человек встал
    Lines L{lines, ln, eye};
    bool sel_ok = pv.sel >= 0 && c.person_gen[pv.sel] == pv.sel_gen;
    if (pv.sel >= 0 && !sel_ok) pv.sel = -1;
    if (sel_ok) {
        const PersonLook &P = pv.look[pv.sel];
        Root r = person_root(*in.prev, c, in.alpha, (u32)pv.sel);
        WPos at = P.drawn ? P.root : r.at;
        at.y = (P.drawn ? P.y : r.at.y) + 0.03f;
        L.ring(at, 0.45f, rgba8(255, 214, 90));
        if (pv.mark_on && c.step > pv.mark_step && c.person_state[pv.sel] == 0 && c.person_speed[pv.sel] < 0.05f)
            pv.mark_fade = f_max(0.0f, pv.mark_fade - f_clamp(in.dt, 0.0f, 0.1f) * 2.0f);
    }
    if (pv.mark_on && (pv.mark_fade <= 0.0f || !sel_ok)) pv.mark_on = false;
    if (pv.mark_on) {
        u8 a = (u8)(255.0f * pv.mark_fade);
        u32 col = rgba8((u8)(120 * pv.mark_fade), a, (u8)(150 * pv.mark_fade));
        WPos m = pv.mark;
        m.y += 0.03f;
        L.ring(m, 0.35f, col);
        L.line(add(m, -0.25f, 0, 0), add(m, 0.25f, 0, 0), col);
        L.line(add(m, 0, 0, -0.25f), add(m, 0, 0, 0.25f), col);
    }
    // След касаний стоп (отладка): крестик на каждое место опоры; скольжение — цепочка крестиков
    if (debug)
        for (u32 k = 0; k < np; ++k) {
            const PersonLook &P = pv.look[pick[k]];
            for (u32 t = 0; t < P.trail_n; ++t) {
                const WPos &p = P.trail[t];
                WPos q = add(p, 0, 0.01f, 0);
                L.line(add(q, -0.05f, 0, -0.05f), add(q, 0.05f, 0, 0.05f), rgba8(255, 230, 40));
                L.line(add(q, -0.05f, 0, 0.05f), add(q, 0.05f, 0, -0.05f), rgba8(255, 230, 40));
            }
        }
}

i32 people_view_pick(const PeopleView &pv, const Extract &cur, const Camera &cam, i32 width, i32 height, f32 px, f32 py) {
    WPos eye;
    Vec3 dir;
    if (!view_screen_ray(cam, width, height, px, py, eye, dir)) return -1;
    i32 best = -1;
    f32 best_t = 1e30f;
    for (u32 i = 0; i < PEOPLE_CAPACITY; ++i) {
        const PersonLook &P = pv.look[i];
        if (!cur.person_gen[i] || P.gen != cur.person_gen[i] || !P.drawn) continue;
        // Луч против вертикального отрезка тела 0.2–1.7 м с запасом 0.4 м
        Vec2 d = wpos_delta_xz(eye, P.root);
        Vec3 o{d.x, P.root.y - eye.y, d.y};
        f32 t = dot(o + Vec3{0, 0.95f, 0}, dir);
        if (t < 0.0f || t > 500.0f) continue;
        Vec3 c = dir * t;
        f32 y = f_clamp(c.y, o.y + 0.2f, o.y + 1.7f);
        Vec3 near{o.x, y, o.z};
        Vec3 dd = c - near;
        f32 r = length(dd);
        if (r < 0.4f + t * 0.004f && t < best_t) {
            best_t = t;
            best = (i32)i;
        }
    }
    return best;
}

bool people_view_head(const PeopleView &pv, const Extract &prev, const Extract &cur, f32 alpha, WPos &out) {
    if (pv.sel < 0 || cur.person_gen[pv.sel] != pv.sel_gen) return false;
    const PersonLook &P = pv.look[pv.sel];
    // Корень — на этот кадр (как у тела), высота — сглаженная (тела ещё не было — из шага); голова — без
    // покачивания шага
    Root r = person_root(prev, cur, alpha, (u32)pv.sel);
    out = r.at;
    out.y = (P.drawn && P.gen == pv.sel_gen ? P.y : r.at.y) + HEAD_ABOVE_M;
    return true;
}
