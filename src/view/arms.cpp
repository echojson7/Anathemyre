#include "arms.h"
#include "../anim/ik.h"
#include "../core/hash.h"
#include "../core/mathf.h"
#include <stdio.h>

// Времена перехода (с): достать, убрать, сколько держать оружие в руке после боя
constexpr f32 DRAW_S = 0.7f;
constexpr f32 SHEATHE_S = 1.0f;
constexpr f32 CALM_S = 2.0f;
constexpr f32 GRAB = 0.45f;        // доля перехода, на которой кисть берёт оружие (до неё тянется, после — несёт)
constexpr f32 URGENT = 4.0f;       // действие уже идёт — достать во столько раз быстрее
// Стойка двумя руками — стоя и шагом; быстрее оружие в одной руке, рука почти не машет
constexpr f32 STANCE_V0 = 1.8f, STANCE_V1 = 2.7f;  // м/с: между ними стойка сменяется рукой у бедра
constexpr f32 ONE_STILL = 0.6f;    // доля покоя в правой руке с оружием на ходу (остальное — мах шага)
constexpr f32 WRIST_LOW = 0.6f;    // рад: кисть к мизинцу, клинок вперёд-вниз
constexpr f32 STANCE_TURN = 0.21f; // рад: стойка довёрнута вправо — в клипе актёр смотрит на противника через левое плечо
constexpr f32 PALM_OUT = 0.028f;   // ось рукояти над ладонью, м
// Сжатая кисть: сгиб трёх фаланг пальца, большой палец — к ладони и вдоль неё (рад)
constexpr f32 CURL[3] = {1.15f, 1.4f, 0.6f};
constexpr f32 THUMB[3] = {0.45f, 0.5f, 0.6f};

static f32 smooth(f32 x) {
    x = f_clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

// a·(1 − w) + b·w по короткой дуге, нормированная сумма
static Quat nlerp(Quat a, Quat b, f32 w) {
    f32 d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w, s = d < 0.0f ? -w : w, u = 1.0f - w;
    Quat q{a.x * u + b.x * s, a.y * u + b.y * s, a.z * u + b.z * s, a.w * u + b.w * s};
    f32 n = f_sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n < 1e-6f) return a;
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

static Vec3 across(Vec3 v, Vec3 axis) { return normalize(v - axis * dot(v, axis)); }

// Поворот оружия: ось сетки +Y — в d, лезвие +X — в e (поперёк d)
static Quat weapon_quat(Vec3 d, Vec3 e) {
    Quat q1 = quat_between(Vec3{0, 1, 0}, d);
    return quat_mul(quat_between(quat_rotate(q1, Vec3{1, 0, 0}), across(e, d)), q1);
}

static i32 bone(const Skel &s, const char *name, const char *side) {
    char buf[32];
    snprintf(buf, sizeof buf, "%s_%s", name, side);
    return skel_find(s, hash_str(buf));
}

static Quat bind_rot(const Skel &s, i32 i) {
    const SkelBone &b = s.bones.data[i];
    return {b.rot[0], b.rot[1], b.rot[2], b.rot[3]};
}
static Vec3 bind_pos(const Skel &s, i32 i) {
    const SkelBone &b = s.bones.data[i];
    return {b.pos[0], b.pos[1], b.pos[2]};
}

bool arms_bind(ArmsBind &b, const Skel &s, const AnimClip &stance, f32 stance_t, const AnimClip &rest, f32 rest_t) {
    b = ArmsBind{};
    static const char *const SIDE[2] = {"l", "r"};
    static const char *const FINGER[5] = {"index", "middle", "ring", "pinky", "thumb"};
    b.pelvis = skel_find(s, hash_str("pelvis"));
    b.spine1 = skel_find(s, hash_str("spine_01"));
    b.chest = skel_find(s, hash_str("spine_03"));
    bool ok = b.pelvis >= 0 && b.spine1 >= 0 && b.chest >= 0;
    for (u32 k = 0; k < 2; ++k) {
        b.clav[k] = bone(s, "clavicle", SIDE[k]);
        b.upper[k] = bone(s, "upperarm", SIDE[k]);
        b.lower[k] = bone(s, "lowerarm", SIDE[k]);
        b.hand[k] = bone(s, "hand", SIDE[k]);
        ok = ok && b.clav[k] >= 0 && b.upper[k] >= 0 && b.lower[k] >= 0 && b.hand[k] >= 0;
        for (u32 f = 0; f < 5; ++f)
            for (u32 j = 0; j < 3; ++j) {
                char name[32];
                snprintf(name, sizeof name, "%s_%02u", FINGER[f], j + 1);
                b.finger[k][f * 3 + j] = bone(s, name, SIDE[k]);
                ok = ok && b.finger[k][f * 3 + j] >= 0;
            }
    }
    const i32 spine2 = skel_find(s, hash_str("spine_02")), neck = skel_find(s, hash_str("neck_01")),
              head = skel_find(s, hash_str("head"));
    if (!ok || spine2 < 0 || neck < 0 || head < 0) return false;

    // Кисть в бинде: ладонь смотрит по n, рукоять лежит вдоль костяшек от мизинца к указательному (остриё — со
    // стороны большого пальца), лезвие — по пальцам от запястья
    for (u32 k = 0; k < 2; ++k) {
        const i32 H = b.hand[k];
        const Quat hq = bind_rot(s, H), hc = quat_conj(hq);
        const Vec3 h = bind_pos(s, H), ix = bind_pos(s, b.finger[k][0]), md = bind_pos(s, b.finger[k][3]),
                   pk = bind_pos(s, b.finger[k][9]);
        const Vec3 n = normalize(cross(ix - h, pk - h)) * (k ? -1.0f : 1.0f);
        const Vec3 d = across(ix - pk, n);
        b.palm[k] = quat_rotate(hc, (h + md) * 0.5f + n * PALM_OUT - h);
        for (u32 f = 0; f < 5; ++f)
            for (u32 j = 0; j < 3; ++j) {
                const i32 F = b.finger[k][f * 3 + j];
                const Quat fq = bind_rot(s, F);
                const Vec3 v = quat_rotate(fq, Vec3{0, 1, 0});
                // Фаланга гнётся к ладони; большой палец — первая кость к ладони, дальше вдоль неё к мизинцу
                const Vec3 to = f < 4 || j == 0 ? n : d * -1.0f;
                const Vec3 ax = cross(v, to);
                if (length(ax) < 1e-4f) continue;
                b.curl[k][f * 3 + j] = quat_axis_angle(quat_rotate(quat_conj(fq), normalize(ax)), f < 4 ? CURL[j] : THUMB[j]);
            }
        if (!k) continue;
        b.grip_q = quat_mul(hc, weapon_quat(d, quat_rotate(hq, Vec3{0, 1, 0})));
        // К мизинцу — туда, где остриё уходит по ходу предплечья
        const Vec3 fore = quat_rotate(bind_rot(s, b.lower[1]), Vec3{0, 1, 0});
        b.wrist_low = quat_axis_angle(quat_rotate(hc, n), dot(cross(n, d), fore) >= 0.0f ? WRIST_LOW : -WRIST_LOW);
    }

    // Стойка и рука у бедра — позы клипов захвата
    const u32 nb = (u32)s.bones.count;
    i16 map[SKEL_MAX_BONES];
    Quat rot[SKEL_MAX_BONES], wr[SKEL_MAX_BONES];
    Vec3 wp[SKEL_MAX_BONES];
    i32 tb = -1;
    if (!anim_bind(stance, s, map, &tb)) return false;
    AnimPose p{rot, {}, 0, 0, 0};
    anim_sample(stance, map, nb, stance_t, p);
    pose_world(s, rot, tb, &p.trans, wr, wp);
    b.stance_spine = quat_mul(quat_axis_angle(Vec3{0, 1, 0}, STANCE_TURN), wr[b.spine1]);
    {
        // Оружие в стойке — по линии ладоней этой позы, в осях груди
        const u32 H = (u32)b.hand[1], HL = (u32)b.hand[0];
        const Vec3 pr = wp[H] + quat_rotate(wr[H], b.palm[1]), pl = wp[HL] + quat_rotate(wr[HL], b.palm[0]);
        if (length(pl - pr) < 1e-3f) return false;
        b.stance_weapon = quat_mul(quat_conj(wr[b.chest]), weapon_quat(normalize(pl - pr), wp[H] - wp[b.lower[1]]));
    }
    const i32 set[ARMS_STANCE] = {spine2,     b.chest,    neck,       head,      b.clav[0],  b.upper[0],
                                  b.lower[0], b.hand[0],  b.clav[1],  b.upper[1], b.lower[1], b.hand[1]};
    for (u32 k = 0; k < ARMS_STANCE; ++k) {
        b.stance_bone[k] = set[k];
        b.stance_rot[k] = rot[set[k]];
    }
    if (!anim_bind(rest, s, map, &tb)) return false;
    anim_sample(rest, map, nb, rest_t, p);
    const i32 arm[4] = {b.clav[1], b.upper[1], b.lower[1], b.hand[1]};
    for (u32 k = 0; k < 4; ++k) b.rest_arm[k] = rot[arm[k]];

    b.pelvis_unbind = quat_conj(bind_rot(s, b.pelvis));
    b.chest_unbind = quat_conj(bind_rot(s, b.chest));
    b.pelvis_at = bind_pos(s, b.pelvis);
    b.chest_at = bind_pos(s, b.chest);
    b.ok = true;
    return true;
}

ArmsState arms_update(ArmsLook &L, const WeaponHold &h, bool fight, bool urgent, f32 speed, f32 dt, bool fresh) {
    if (fresh) {
        L.t = fight ? 1.0f : 0.0f;
        L.calm = fight ? 0.0f : CALM_S;
    }
    L.calm = fight ? 0.0f : f_min(L.calm + dt, CALM_S);
    if (L.calm < CALM_S) L.t = f_min(1.0f, L.t + dt / DRAW_S * (urgent ? URGENT : 1.0f));
    else L.t = f_max(0.0f, L.t - dt / SHEATHE_S);
    ArmsState a;
    a.held = L.t >= GRAB;
    a.s = smooth((L.t - GRAB) / (1.0f - GRAB));
    a.reach = L.t <= 0.0f ? 0.0f : L.t < GRAB ? smooth(L.t / GRAB) : 1.0f - a.s;
    a.fist = smooth((L.t - (GRAB - 0.12f)) / 0.12f);
    const f32 g = h.hands >= 2 ? 1.0f - smooth((speed - STANCE_V0) / (STANCE_V1 - STANCE_V0)) : 0.0f;
    a.stance = g;
    a.two = a.s * g;
    a.one = a.s * (1.0f - g);
    return a;
}

void arms_pose_hold(const ArmsBind &b, const ArmsState &a, Quat *rot, const Quat *wr) {
    if (a.two > 0.0f) {
        // Первый позвонок — как в стойке в осях корня (таз шагает под ним), выше — местные повороты стойки
        const Quat d = quat_mul(b.stance_spine, quat_conj(wr[b.spine1]));
        pose_turn_world(rot, wr, (u32)b.spine1, nlerp(Quat{}, d, a.two));
        for (u32 k = 0; k < ARMS_STANCE; ++k) rot[b.stance_bone[k]] = nlerp(rot[b.stance_bone[k]], b.stance_rot[k], a.two);
    }
    if (a.one > 0.0f) {
        const i32 arm[4] = {b.clav[1], b.upper[1], b.lower[1], b.hand[1]};
        for (u32 k = 0; k < 4; ++k) rot[arm[k]] = nlerp(rot[arm[k]], b.rest_arm[k], a.one * ONE_STILL);
        rot[b.hand[1]] = quat_mul(rot[b.hand[1]], nlerp(Quat{}, b.wrist_low, a.one));
    }
}

// Место ношения в позе: начало и поворот оружия в осях сетки человека, подсказка локтю для руки, которая тянется
static void carry_place(const ArmsBind &b, const WeaponHold &h, const Quat *wr, const Vec3 *wp, Vec3 *at, Quat *q, Vec3 *pole) {
    Vec3 hang, dir, edge, base;
    Quat unbind;
    i32 at_bone;
    if (h.carry == CARRY_HIP_L) {
        // У левого бедра на ремне: рукоять вперёд-вверх, остриё вниз-назад; длинное ложится положе, чтобы не
        // чертить землю
        at_bone = b.pelvis;
        unbind = b.pelvis_unbind;
        base = b.pelvis_at;
        hang = base + Vec3{-0.19f, 0.03f, 0.05f};
        const f32 c = f_clamp((hang.y - 0.12f) / f_max(h.tip - h.carry_at, 0.1f), 0.3f, 0.8f);
        dir = Vec3{-0.06f, -c, -f_sqrt(1.0f - c * c - 0.0036f)};
        edge = Vec3{0.0f, dir.z, -dir.y};
        *pole = Vec3{0.7f, -0.2f, 0.7f};
    } else {
        // За спиной: рукоять над правым плечом, остриё к левому бедру; древко — почти отвесно
        at_bone = b.chest;
        unbind = b.chest_unbind;
        base = b.chest_at;
        hang = base + Vec3{0.02f, 0.13f, -0.13f};
        dir = normalize(h.carry == CARRY_BACK_POLE ? Vec3{-0.14f, -0.99f, -0.03f} : Vec3{-0.50f, -0.86f, -0.04f});
        edge = Vec3{-dir.y, dir.x, 0.0f};
        *pole = Vec3{0.8f, 0.45f, 0.35f};
    }
    const Quat rel = quat_mul(wr[at_bone], unbind);
    *q = quat_mul(rel, weapon_quat(dir, edge));
    *at = wp[at_bone] + quat_rotate(rel, hang - dir * h.carry_at - base);
}

ArmsWeapon arms_pose_weapon(const ArmsBind &b, const Skel &s, const WeaponHold &h, const ArmsState &a, f32 act_any,
                            f32 act_two, f32 act_block, i32 trans_bone, const Vec3 *trans, Quat *rot, Quat *wr, Vec3 *wp) {
    const u32 U = (u32)b.upper[1], F = (u32)b.lower[1], H = (u32)b.hand[1], HL = (u32)b.hand[0];
    Vec3 cat, pole;
    Quat cq;
    carry_place(b, h, wr, wp, &cat, &cq, &pole);
    bool moved = false;
    if (a.reach > 0.001f) {
        // Кисть к рукояти на месте ношения: ладонь — в начало оружия, костяшки — вдоль его оси; потом доля перехода
        const Quat r0[3] = {rot[U], rot[F], rot[H]};
        const Vec3 wd = quat_rotate(cq, Vec3{0, 1, 0});
        for (u32 pass = 0; pass < 2; ++pass) {
            ik_arm(rot, wr, wp, U, F, H, cat - quat_rotate(wr[H], b.palm[1]), pole);
            pose_world(s, rot, trans_bone, trans, wr, wp);
            const Vec3 hd = quat_rotate(quat_mul(wr[H], b.grip_q), Vec3{0, 1, 0});
            pose_turn_world(rot, wr, H, quat_between(hd, wd));
            pose_world(s, rot, trans_bone, trans, wr, wp);
        }
        rot[U] = nlerp(r0[0], rot[U], a.reach);
        rot[F] = nlerp(r0[1], rot[F], a.reach);
        rot[H] = nlerp(r0[2], rot[H], a.reach);
        moved = true;
    }
    // Пальцы: правая кисть держит оружие, левая — когда хват двумя руками (стойка или клип с оружием)
    const f32 stance = a.two * (1.0f - act_any), two = f_max(act_two, stance);
    const f32 fist[2] = {two, a.fist};
    for (u32 k = 0; k < 2; ++k) {
        if (fist[k] <= 0.0f) continue;
        for (u32 f = 0; f < ARMS_FINGERS; ++f) rot[b.finger[k][f]] = nlerp(rot[b.finger[k][f]], b.curl[k][f], fist[k]);
        moved = true;
    }
    if (moved) pose_world(s, rot, trans_bone, trans, wr, wp);
    if (!a.held) return {cat, cq};

    // В правой кисти — жёстко за костью; в блоке (клип бокса, хвата нет) клинок вверх и через тело влево
    const Vec3 pr = wp[H] + quat_rotate(wr[H], b.palm[1]), pl = wp[HL] + quat_rotate(wr[HL], b.palm[0]);
    Quat one = quat_mul(wr[H], b.grip_q);
    if (act_block > 0.0f) {
        const Vec3 d1 = quat_rotate(one, Vec3{0, 1, 0});
        one = quat_mul(quat_between(d1, normalize(d1 + (normalize(Vec3{-0.45f, 0.85f, 0.3f}) - d1) * act_block)), one);
    }
    // Двумя руками начало оружия — левая ладонь (у гарды). В стойке поворот — из позы стойки (в осях груди): пока
    // левая кисть идёт к рукояти, линия ладоней ничего не значит. В клипе с оружием — по линии ладоней, как отрезок
    // клинка в дорожках шага мира; лезвие — по правому предплечью
    const Quat stance_q = quat_mul(wr[b.chest], b.stance_weapon);
    Quat hq = nlerp(one, stance_q, stance);
    if (act_two > 0.0f && length(pl - pr) > 1e-3f) {
        const Vec3 d2 = normalize(pl - pr);
        Vec3 e = wp[H] - wp[F];
        if (length(e - d2 * dot(e, d2)) < 1e-3f) e = Vec3{1, 0, 0};
        hq = nlerp(hq, weapon_quat(d2, e), act_two);
    }
    const Vec3 hat = pr + (pl - pr) * two;
    if (a.s >= 1.0f) return {hat, hq};
    // Переход: сразу после захвата оружие ещё у места ношения и догоняет кисть за первую треть пути; поворот — от
    // места ношения к рабочему положению, из-за спины — верхом (остриё вверх-назад над плечом), а не короткой дугой
    // сквозь плечо
    const Quat goal = nlerp(one, stance_q, a.stance * (1.0f - act_any));
    Quat q = nlerp(cq, goal, a.s);
    if (h.carry != CARRY_HIP_L) {
        const Quat mid = weapon_quat(normalize(Vec3{-0.1f, 0.92f, -0.38f}), quat_rotate(goal, Vec3{1, 0, 0}));
        q = nlerp(nlerp(cq, mid, a.s), nlerp(mid, goal, a.s), a.s);
    }
    q = nlerp(q, hq, smooth((a.s - 0.7f) / 0.3f));
    const f32 k = 1.0f - smooth(a.s * 3.0f);
    return {hat + (cat - hat) * k, q};
}
