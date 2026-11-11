// Действия и удар (этап 10, docs/people.md «Удар»). Корень, капсулы и клинок — из запечённых дорожек клипа целыми
// мм, без смешивания. Порядок внутри people_act — три прохода, чтобы номер места не давал форы (урок беты):
// 1) стоп на попадании убывает у всех; 2) ходы: реакция, действие, подход к цели; 3) попадания ищутся по
// положению после ходов у всех и применяются списком (взаимные удары в один тик — размен)
#include "action.h"
#include "../core/hash.h"
#include "../core/text.h"
#include "step.h"
#include <string.h>

static constexpr f32 ACT_BLADE_R = 0.015f;   // полутолщина клинка, м
static constexpr u32 ACT_SWEEP = 4;          // проверок между тиками: клинок идёт до 0.15 м за тик
static constexpr f32 ACT_NEAR = 3.2f;        // дальше от бьющего — не проверять
static constexpr u16 ACT_PUSH_TICKS = 18;    // отброс задетого идёт 0.3 с, скорость убывает до нуля
static constexpr f32 ACT_FACE = 0.25f;       // начинать удар, когда тело смотрит на цель точнее, рад
static constexpr f32 ACT_STOP_DEC = 6.0f;    // торможение (как BRAKE в people.cpp): с какого расстояния вставать
static constexpr u16 ACT_REPATH = 15;        // подход: не чаще раза в 0.25 с новый путь к ушедшей цели
static constexpr f32 ACT_REPATH_DIST = 0.7f; // цель ушла от точки приказа дальше — новый путь
static constexpr f32 ACT_RUN_DIST = 8.0f;    // дальше — бегом, ближе — трусцой

static const char *const PART_NAMES[BODY_PARTS] = {"голова", "грудь", "таз", "плечо л", "плечо п", "предплечье л",
                                                    "предплечье п", "бедро л", "бедро п", "голень л", "голень п"};
const char *body_part_name(u32 part) { return part < BODY_PARTS ? PART_NAMES[part] : "?"; }

static void act_err(char *err, usize n, u32 line, const char *what) {
    char num[12];
    u32 k = 0, v = line;
    char tmp[12];
    do { tmp[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 11);
    for (u32 i = 0; i < k; ++i) num[i] = tmp[k - 1 - i];
    num[k] = 0;
    usize o = 0;
    const char *pre = "строка ";
    for (const char *q = pre; *q && o + 1 < n; ++q) err[o++] = *q;
    for (const char *q = num; *q && o + 1 < n; ++q) err[o++] = *q;
    for (const char *q = ": "; *q && o + 1 < n; ++q) err[o++] = *q;
    for (const char *q = what; *q && o + 1 < n; ++q) err[o++] = *q;
    if (n) err[o] = 0;
}

bool action_parse(const u8 *data, usize size, ActionDesc &out, char *err, usize err_size) {
    // Поля, которые не задали, — ошибка: все числа боя в описании, без запасных значений в коде (урок беты)
    TextReader r = text_reader(data, size);
    TextLine l;
    u32 have = 0;
    enum { H_CLIP = 1, H_END = 2, H_REACH = 4, H_STOP = 8, H_PUSH = 16, H_REACT = 32, H_ALL = 63 };
    while (text_next(r, l)) {
        Str k = l.w[0];
        f32 a = 0, b = 0;
        u32 u = 0;
        if (str_eq(k, "clip") && l.count == 2 && l.w[1].n < sizeof out.clip_name) {
            out.clip = str_hash(l.w[1]);
            memcpy(out.clip_name, l.w[1].p, l.w[1].n);
            out.clip_name[l.w[1].n] = 0;
            have |= H_CLIP;
        }
        else if (str_eq(k, "end") && l.count == 2 && parse_u32(l.w[1], u) && u < 0x7FFF) { out.end = (u16)(u * 2); have |= H_END; }
        else if (str_eq(k, "reach") && l.count == 3 && parse_f32(l.w[1], a) && parse_f32(l.w[2], b) && a > 0 && b >= a) {
            out.reach_min = a;
            out.reach_max = b;
            have |= H_REACH;
        } else if (str_eq(k, "hitstop") && l.count == 2 && parse_u32(l.w[1], u) && u < 60) { out.hitstop = (u8)u; have |= H_STOP; }
        else if (str_eq(k, "push") && l.count == 2 && parse_f32(l.w[1], a) && a >= 0 && a < 2) { out.push = a; have |= H_PUSH; }
        else if (str_eq(k, "react") && l.count == 2 && parse_f32(l.w[1], a) && a > 0 && a < 5) {
            out.react = (u16)(a * (f32)SIM_HZ + 0.5f);
            if (out.react < ACT_PUSH_TICKS) out.react = ACT_PUSH_TICKS;
            have |= H_REACT;
        } else {
            act_err(err, err_size, l.line, "непонятная строка (clip, end, reach, hitstop, push, react)");
            return false;
        }
    }
    if (have != H_ALL) {
        act_err(err, err_size, 0, "нужны все поля: clip, end, reach, hitstop, push, react");
        return false;
    }
    return true;
}

// Оси корня → мир: лицо +z, курс yaw (как quat_yaw у вида)
static void act_rot(Turn yaw, f32 x, f32 z, f32 *wx, f32 *wz) {
    f32 s, c;
    sincos_turn(yaw, &s, &c);
    *wx = c * x + s * z;
    *wz = -s * x + c * z;
}

Turn action_aim(const ActionDesc &a) {
    if (!a.track || !a.track->weapon) return 0;
    const Track &t = *a.track;
    u32 k = track_tick(t, ((u32)a.hit_a + a.hit_b) / 2);
    const i32 *r = t.root + (usize)k * 3;
    const i16 *wv = t.weapon + (usize)k * 6;
    f32 x, z;
    act_rot((Turn)(u32)r[2], (f32)wv[3] * 0.001f, (f32)wv[5] * 0.001f, &x, &z);
    return turn_of_dir(x + (f32)r[0] * 0.001f, z + (f32)r[1] * 0.001f);
}

u64 action_hash(const ActionDesc &a) {
    u64 h = hash_combine(hash_combine(a.name, a.clip), ((u64)a.hit_a << 48) | ((u64)a.hit_b << 32) | ((u64)a.end << 16) | a.cancel);
    u32 f[4];
    memcpy(&f[0], &a.reach_min, 4);
    memcpy(&f[1], &a.reach_max, 4);
    memcpy(&f[2], &a.push, 4);
    f[3] = ((u32)a.hitstop << 16) | a.react;
    return hash_combine(hash_combine(h, hash_bytes(f, sizeof f)), a.aim);
}


static const Track *body_track(const World &w, u32 s, u32 *tick) {
    const People &p = w.people;
    const ActionTable &T = w.actions;
    if (p.act[s] && p.act[s] <= T.count && T.a[p.act[s] - 1].track) {
        const Track *t = T.a[p.act[s] - 1].track;
        *tick = track_tick(*t, p.act_t[s]);
        return t;
    }
    // Ходьба или бег по фазе шага (середина смеси вида — 2.4 м/с); стоит — ходьба на своей фазе
    const Track *t = p.speed[s] > 2.4f ? T.run : T.walk;
    if (!t) return nullptr;
    *tick = (u32)(((u64)p.phase[s] * (u64)(t->ticks - 1)) >> 32);
    return t;
}

void people_body(const World &w, u32 s, const WPos &ref, BodyCaps &out) {
    const People &p = w.people;
    out.n = 0;
    u32 tick;
    const Track *t = body_track(w, s, &tick);
    if (!t) return;
    Vec2 d = wpos_delta_xz(ref.cx, ref.cz, ref.x, ref.z, p.cx[s], p.cz[s], p.x[s], p.z[s]);
    f32 dy = p.y[s] - ref.y;
    u32 n = t->parts < BODY_PARTS ? t->parts : BODY_PARTS;
    const i16 *c = t->caps + (usize)tick * t->parts * 6;
    for (u32 i = 0; i < n; ++i, c += 6) {
        f32 x, z;
        act_rot(p.heading[s], (f32)c[0] * 0.001f, (f32)c[2] * 0.001f, &x, &z);
        out.a[i][0] = d.x + x; out.a[i][1] = dy + (f32)c[1] * 0.001f; out.a[i][2] = d.y + z;
        act_rot(p.heading[s], (f32)c[3] * 0.001f, (f32)c[5] * 0.001f, &x, &z);
        out.b[i][0] = d.x + x; out.b[i][1] = dy + (f32)c[4] * 0.001f; out.b[i][2] = d.y + z;
        out.r[i] = (f32)t->part[i].radius * 0.001f;
    }
    out.n = n;
}

bool people_blade(const World &w, u32 s, u32 tick, const WPos &ref, f32 a[3], f32 b[3]) {
    const People &p = w.people;
    const ActionTable &T = w.actions;
    if (!p.act[s] || p.act[s] > T.count) return false;
    const Track *t = T.a[p.act[s] - 1].track;
    if (!t || !t->weapon) return false;
    // Корень на тике tick: нынешний корень + сдвиг дорожки корня от нынешнего тика (в осях начала действия)
    u32 k = track_tick(*t, tick), now = track_tick(*t, p.act_t[s]);
    const i32 *rk = t->root + (usize)k * 3, *rn = t->root + (usize)now * 3;
    f32 ox, oz;
    act_rot(p.act_yaw[s], (f32)(rk[0] - rn[0]) * 0.001f, (f32)(rk[1] - rn[1]) * 0.001f, &ox, &oz);
    Vec2 d = wpos_delta_xz(ref.cx, ref.cz, ref.x, ref.z, p.cx[s], p.cz[s], p.x[s], p.z[s]);
    Turn yaw = p.act_yaw[s] + (u32)rk[2];
    const i16 *wv = t->weapon + (usize)k * 6;
    f32 x, z;
    act_rot(yaw, (f32)wv[0] * 0.001f, (f32)wv[2] * 0.001f, &x, &z);
    a[0] = d.x + ox + x; a[1] = p.y[s] - ref.y + (f32)wv[1] * 0.001f; a[2] = d.y + oz + z;
    act_rot(yaw, (f32)wv[3] * 0.001f, (f32)wv[5] * 0.001f, &x, &z);
    b[0] = d.x + ox + x; b[1] = p.y[s] - ref.y + (f32)wv[4] * 0.001f; b[2] = d.y + oz + z;
    return true;
}

// Квадрат расстояния между отрезками p1q1 и p2q2 (Эриксон, «Real-Time Collision Detection», 5.1.9)
static f32 seg_seg_d2(const f32 p1[3], const f32 q1[3], const f32 p2[3], const f32 q2[3]) {
    f32 d1[3], d2[3], r[3];
    for (u32 i = 0; i < 3; ++i) { d1[i] = q1[i] - p1[i]; d2[i] = q2[i] - p2[i]; r[i] = p1[i] - p2[i]; }
    f32 a = d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2];
    f32 e = d2[0] * d2[0] + d2[1] * d2[1] + d2[2] * d2[2];
    f32 f = d2[0] * r[0] + d2[1] * r[1] + d2[2] * r[2];
    f32 s, t;
    const f32 eps = 1e-8f;
    if (a <= eps && e <= eps) { s = t = 0.0f; }
    else if (a <= eps) { s = 0.0f; t = f_clamp(f / e, 0.0f, 1.0f); }
    else {
        f32 c = d1[0] * r[0] + d1[1] * r[1] + d1[2] * r[2];
        if (e <= eps) { t = 0.0f; s = f_clamp(-c / a, 0.0f, 1.0f); }
        else {
            f32 b = d1[0] * d2[0] + d1[1] * d2[1] + d1[2] * d2[2];
            f32 den = a * e - b * b;
            s = den > eps ? f_clamp((b * f - c * e) / den, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) { t = 0.0f; s = f_clamp(-c / a, 0.0f, 1.0f); }
            else if (t > 1.0f) { t = 1.0f; s = f_clamp((b - c) / a, 0.0f, 1.0f); }
        }
    }
    f32 d2s = 0.0f;
    for (u32 i = 0; i < 3; ++i) {
        f32 v = (p1[i] + d1[i] * s) - (p2[i] + d2[i] * t);
        d2s += v * v;
    }
    return d2s;
}

static const ActionDesc *act_desc(const World &w, u32 s) {
    u8 a = w.people.act[s];
    return a && a <= w.actions.count ? &w.actions.a[a - 1] : nullptr;
}

// Реакция задетого: отброс от бьющего (скорость убывает до нуля за ACT_PUSH_TICKS), потом стоит до конца реакции
static void act_react(World &w, u32 s) {
    People &p = w.people;
    const ActionDesc &A = w.actions.a[w.actions.strike];
    u16 k = p.react_t[s];
    if (k <= ACT_PUSH_TICKS) {
        f32 step = A.push * (2.0f / (f32)ACT_PUSH_TICKS) * (1.0f - ((f32)k - 0.5f) / (f32)ACT_PUSH_TICKS);
        f32 sn, cs;
        sincos_turn(p.react_yaw[s], &sn, &cs);
        p.x[s] += sn * step;
        p.z[s] += cs * step;
        people_push_capsule(w, s);
    }
    p.react_t[s] = k >= A.react ? 0 : (u16)(k + 1);
}

// Ход действия на тик: корень по дорожке (сдвиг и поворот), конец — человек свободен, приказ «атаковать» исполнен
static void act_advance(World &w, u32 s) {
    People &p = w.people;
    const ActionDesc *A = act_desc(w, s);
    if (!A || !A->track) { p.act[s] = 0; return; }
    const Track &t = *A->track;
    u32 t0 = track_tick(t, p.act_t[s]), t1 = track_tick(t, (u32)p.act_t[s] + 1);
    const i32 *r0 = t.root + (usize)t0 * 3, *r1 = t.root + (usize)t1 * 3;
    f32 dx, dz;
    act_rot(p.act_yaw[s], (f32)(r1[0] - r0[0]) * 0.001f, (f32)(r1[1] - r0[1]) * 0.001f, &dx, &dz);
    p.x[s] += dx;
    p.z[s] += dz;
    p.heading[s] = p.move_yaw[s] = p.act_yaw[s] + (u32)r1[2];
    people_push_capsule(w, s);
    p.act_t[s]++;
    if (p.act_t[s] >= A->end) {
        p.act[s] = 0;
        p.act_t[s] = 0;
        p.act_hit[s] = 0;
        p.target[s] = 0;  // приказ игрока «идти» во время удара (state) исполнится после него
    }
}

// Подход к цели приказа «атаковать»: идти, встать на дистанции удара, развернуться, начать удар
static void act_approach(World &w, u32 s, StepStats *stats) {
    People &p = w.people;
    const ActionTable &T = w.actions;
    u32 t;
    if (!people_valid(p, p.target[s], &t)) { p.target[s] = 0; return; }
    const ActionDesc &A = T.a[T.strike];
    Vec2 d = wpos_delta_xz(p.cx[s], p.cz[s], p.x[s], p.z[s], p.cx[t], p.cz[t], p.x[t], p.z[t]);
    f32 dist = length(d);
    f32 v = p.speed[s];
    f32 stop = A.reach_max - 0.1f + v * v / (2.0f * ACT_STOP_DEC);
    if (dist > stop) {
        Vec2 g = wpos_delta_xz(p.gcx[s], p.gcz[s], p.gx[s], p.gz[s], p.cx[t], p.cz[t], p.x[t], p.z[t]);
        bool moved = dot(g, g) > ACT_REPATH_DIST * ACT_REPATH_DIST && (w.step + s) % ACT_REPATH == 0;
        if (p.state[s] == MOVE_STAND || moved) {
            WPos to{p.cx[t], p.cz[t], p.x[t], p.y[t], p.z[t]};
            people_order_go(w, make_id(s, p.gen[s]).v, to, dist > ACT_RUN_DIST ? GAIT_RUN : GAIT_JOG, false);
        }
        return;
    }
    p.state[s] = MOVE_STAND;
    if (dist > 1e-3f) p.move_yaw[s] = turn_of_dir(d.x, d.y) - A.aim;  // клинок пойдёт в цель
    f32 face = f_abs(turn_diff_rad(p.heading[s], p.move_yaw[s]));
    if (v < 0.5f && face < ACT_FACE && A.track) {
        p.act[s] = (u8)(T.strike + 1);
        p.act_t[s] = 0;
        p.act_yaw[s] = p.heading[s];
        p.act_hit[s] = 0;
        p.speed[s] = 0.0f;
        if (stats) stats->strikes++;
    }
}

struct ActHit {
    u32 att, def, part;
    u8 hitstop;
};

// Клинок s в окне удара против капсул всех рядом: первое касание на пути клинка от прошлого тика к этому
static bool act_find_hit(const World &w, u32 s, ActHit &out) {
    const People &p = w.people;
    const ActionDesc *A = act_desc(w, s);
    if (!A || p.act_hit[s] || p.act_t[s] < A->hit_a || p.act_t[s] > A->hit_b || !p.act_t[s]) return false;
    WPos ref{p.cx[s], p.cz[s], p.x[s], p.y[s], p.z[s]};
    f32 a0[3], b0[3], a1[3], b1[3];
    if (!people_blade(w, s, (u32)p.act_t[s] - 1, ref, a0, b0) || !people_blade(w, s, p.act_t[s], ref, a1, b1)) return false;
    u32 best_k = ACT_SWEEP + 1, best_d = 0, best_part = 0;
    f32 best_pen = 0.0f;
    for (u32 d = 0; d < PEOPLE_CAPACITY; ++d) {
        if (!p.alive[d] || d == s) continue;
        Vec2 o = wpos_delta_xz(ref.cx, ref.cz, ref.x, ref.z, p.cx[d], p.cz[d], p.x[d], p.z[d]);
        if (dot(o, o) > ACT_NEAR * ACT_NEAR) continue;
        BodyCaps bc;
        people_body(w, d, ref, bc);
        for (u32 k = 1; k <= ACT_SWEEP && k <= best_k; ++k) {
            f32 u = (f32)k / (f32)ACT_SWEEP, sa[3], sb[3];
            for (u32 i = 0; i < 3; ++i) {
                sa[i] = a0[i] + (a1[i] - a0[i]) * u;
                sb[i] = b0[i] + (b1[i] - b0[i]) * u;
            }
            bool any = false;
            for (u32 c = 0; c < bc.n; ++c) {
                f32 r = bc.r[c] + ACT_BLADE_R;
                f32 d2 = seg_seg_d2(sa, sb, bc.a[c], bc.b[c]);
                if (d2 >= r * r) continue;
                f32 pen = r - f_sqrt(d2);
                // Раньше на пути клинка — главнее; в один момент — глубже; поровну — меньший номер
                if (k < best_k || pen > best_pen) {
                    best_k = k;
                    best_d = d;
                    best_part = c;
                    best_pen = pen;
                }
                any = true;
            }
            if (any) break;
        }
    }
    if (best_k > ACT_SWEEP) return false;
    out = {s, best_d, best_part, A->hitstop};
    return true;
}

void people_act(World &w, StepCtx &ctx) {
    People &p = w.people;
    if (!w.actions.count || !p.live) return;
    Arena *ar = ctx.step_arena;
    usize mark = ar ? arena_mark(*ar) : 0;
    u8 *held = ar ? arena_push<u8>(*ar, PEOPLE_CAPACITY) : nullptr;
    // 1) Стоп на попадании: замершие этот тик не ходят
    for (u32 s = 0; s < PEOPLE_CAPACITY; ++s) {
        bool h = p.alive[s] && p.freeze[s];
        if (h) p.freeze[s]--;
        if (held) held[s] = (u8)h;
    }
    // 2) Ходы
    for (u32 s = 0; s < PEOPLE_CAPACITY; ++s) {
        if (!p.alive[s] || (held && held[s])) continue;
        if (p.react_t[s]) act_react(w, s);
        else if (p.act[s]) act_advance(w, s);
        else if (p.target[s]) act_approach(w, s, ctx.stats);
    }
    // 3) Попадания по положению после ходов у всех
    ActHit hits[64];
    u32 nh = 0;
    for (u32 s = 0; s < PEOPLE_CAPACITY && nh < 64; ++s)
        if (p.alive[s] && p.act[s] && !(held && held[s]) && act_find_hit(w, s, hits[nh])) nh++;
    for (u32 i = 0; i < nh; ++i) {
        const ActHit &h = hits[i];
        p.act_hit[h.att] = (u8)(h.part + 1);
        p.freeze[h.att] = h.hitstop;
        u32 d = h.def;
        // Задетый: действие сорвано, ход снят (приказ «атаковать» остаётся — после реакции подойдёт снова)
        p.act[d] = 0;
        p.act_t[d] = 0;
        p.act_hit[d] = 0;
        p.state[d] = MOVE_STAND;
        p.speed[d] = 0.0f;
        p.freeze[d] = h.hitstop;
        p.react_t[d] = 1;
        p.react_part[d] = (u8)h.part;
        Vec2 o = wpos_delta_xz(p.cx[h.att], p.cz[h.att], p.x[h.att], p.z[h.att], p.cx[d], p.cz[d], p.x[d], p.z[d]);
        p.react_yaw[d] = dot(o, o) > 1e-6f ? turn_of_dir(o.x, o.y) : p.heading[h.att];
        if (p.hits[d] < 0xFFFF) p.hits[d]++;
        if (ctx.stats) ctx.stats->hits++;
    }
    if (ar) arena_restore(*ar, mark);
}
