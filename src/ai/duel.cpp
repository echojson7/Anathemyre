// ИИ поединка (этап 11, docs/people.md «Обмен ударами»). Каждый боец думает в свой тик и решает по состоянию до
// шага, пишет только своё намерение: номер места не даёт форы (урок беты). Случайность — хеш шага и номера, а не
// RNG мира: решение одного не сдвигает числа другого. В группе (этап 12) угроза — замах любого, а удар ждёт жетона
#include "duel.h"
#include "group.h"
#include "../core/hash.h"
#include "../core/text.h"
#include "../sim/world.h"
#include <stddef.h>
#include <string.h>

bool duel_parse(const u8 *data, usize size, DuelDesc &out, char *err, usize err_size) {
    TextReader r = text_reader(data, size);
    TextLine l;
    u32 have = 0;
    out = DuelDesc{};
    while (text_next(r, l)) {
        Str k = l.w[0];
        u32 u = 0;
        f32 a = 0, b = 0;
        bool two = l.count == 3 && parse_f32(l.w[1], a) && parse_f32(l.w[2], b);
        bool one = l.count == 2 && parse_f32(l.w[1], a);
        if (str_eq(k, "think") && l.count == 2 && parse_u32(l.w[1], u) && u > 0 && u < 120) { out.think = u; have |= 1; }
        else if (str_eq(k, "notice") && two && a >= 0 && b >= 0) { out.notice[0] = a; out.notice[1] = b; have |= 2; }
        else if (str_eq(k, "noise") && two && a >= 0 && b >= 0) { out.noise[0] = a; out.noise[1] = b; have |= 4; }
        else if (str_eq(k, "attack") && two && a >= 0 && a <= 1 && b >= 0 && b <= 1) { out.attack[0] = a; out.attack[1] = b; have |= 8; }
        else if (str_eq(k, "open") && one && a >= 0 && a <= 1) { out.open = a; have |= 16; }
        else if (str_eq(k, "dodge") && one) { out.dodge = a; have |= 32; }
        else if (str_eq(k, "counter") && one) { out.counter = a; have |= 64; }
        else if (str_eq(k, "none") && one) { out.none = a; have |= 128; }
        else if (str_eq(k, "margin") && l.count == 2 && parse_u32(l.w[1], u) && u < 60) { out.margin = u; have |= 256; }
        else {
            text_err(err, err_size, l.line, "непонятная строка (think, notice, noise, attack, open, dodge, counter, none, margin)");
            return false;
        }
    }
    if (have != 511) {
        text_err(err, err_size, 0, "нужны все поля: think, notice, noise, attack, open, dodge, counter, none, margin");
        return false;
    }
    out.hash = hash_bytes(&out, offsetof(DuelDesc, hash));
    return true;
}

static f32 lerp2(const f32 v[2], f32 k) { return v[0] + (v[1] - v[0]) * k; }


// Свой (не цель, не враг) впереди-сбоку ближе 1.8 м: широкий удар задел бы его
static bool duel_ally_near(const World &w, const FightView &v, u32 s, u32 t) {
    const People &p = w.people;
    PeopleSet near;
    people_grid_near(v.grid, p.cx[s], p.cz[s], p.x[s], p.z[s], 1.8f, near);
    bool any = false;
    people_set_each(near, [&](u32 a) {
        if (any || a == s || a == t || people_foes(p, s, a)) return;
        Vec2 d = wpos_delta_xz(p.cx[s], p.cz[s], p.x[s], p.z[s], p.cx[a], p.cz[a], p.x[a], p.z[a]);
        if (dot(d, d) > 1.8f * 1.8f || dot(d, d) < 1e-6f) return;
        any = f_abs(turn_diff_rad(p.heading[s], turn_of_dir(d.x, d.y))) < 1.6f;
    });
    return any;
}

void duel_think(const World &w, const DuelDesc &D, const GroupDesc &G, const FightView &v, u32 s, FixedArray<Intent> &out) {
    const People &p = w.people;
    const ActionTable &T = w.actions;
    const u32 id = make_id(s, p.gen[s]).v;
    u32 t;
    if (!people_valid(p, p.target[s], &t) || p.react_t[s] || p.freeze[s]) return;
    const ActionDesc *mine = ai_act(w, p.act[s]);
    if (mine && p.act_t[s] < mine->cancel) return;  // своё действие ещё не отменить
    const f32 sk = (f32)p.skill[s] * 0.01f;
    const f32 amp = lerp2(D.noise, sk);
    u32 q = 0;
    auto noise = [&]() { return (ai_roll(w.step, id, q++) * 2.0f - 1.0f) * amp; };
    Vec2 d = wpos_delta_xz(p.cx[s], p.cz[s], p.x[s], p.z[s], p.cx[t], p.cz[t], p.x[t], p.z[t]);
    const f32 dist = length(d);
    const f32 face = dist > 1e-3f ? f_abs(turn_diff_rad(p.heading[s], turn_of_dir(d.x, d.y))) : 0.0f;
    u32 want = p.want[s];

    // Замах на меня, который я уже заметил: того, чей удар ближе всех к окну (в группе — не обязательно цели)
    const u32 u = v.urgent[s] != 0xFFFF ? v.urgent[s] : t;
    const ActionDesc *their = ai_act(w, p.act[u]);
    Vec2 du = u == t ? d : wpos_delta_xz(p.cx[s], p.cz[s], p.x[s], p.z[s], p.cx[u], p.cz[u], p.x[u], p.z[u]);
    const f32 dist_u = u == t ? dist : length(du);
    const f32 face_u = u == t ? face : dist_u > 1e-3f ? f_abs(turn_diff_rad(p.heading[s], turn_of_dir(du.x, du.y))) : 0.0f;
    bool threat = their && their->kind == ACT_STRIKE && !p.act_hit[u] && p.act_t[u] <= their->hit_b &&
                  p.target[u] == id && dist_u <= their->reach_max + 0.6f &&
                  (f32)(p.act_t[u] - their->start) >= lerp2(D.notice, sk);
    if (threat) {
        const i32 until = (i32)their->hit_a - (i32)p.act_t[u];
        f32 best = D.none + noise();
        u32 pick = 0;
        // Блок держит только удар спереди: со спины (нападающий сбоку кольца) не закрыться
        if (T.block != ACTION_NONE && face_u <= ACT_BLOCK_FACE) {
            const ActionDesc &B = T.a[T.block];
            if ((their->line & B.line) && (i32)(B.guard_a - B.start) <= until) {
                f32 c = f_clamp(B.chance + B.chance_skill * ((f32)p.skill[s] - (f32)p.skill[u]), 0.0f, 1.0f) + noise();
                if (c > best) { best = c; pick = T.block + 1u; }
            }
        }
        if (T.dodge != ACTION_NONE) {
            const ActionDesc &Gd = T.a[T.dodge];
            f32 c = D.dodge + noise();
            if ((their->line & Gd.line) && (i32)(Gd.guard_a - Gd.start) <= until && c > best) { best = c; pick = T.dodge + 1u; }
        }
        // Ответный удар — только по своей цели (удар идёт в неё)
        for (u32 k = 0; u == t && k < T.count; ++k) {
            const ActionDesc &C = T.a[k];
            if (C.kind != ACT_STRIKE || dist < C.reach_min - 0.2f || dist > C.reach_max || face > 0.6f) continue;
            f32 c = D.counter + noise();
            if ((i32)(C.hit_a - C.start + D.margin) <= until && c > best) { best = c; pick = k + 1u; }
        }
        if (pick) want = pick;
    } else if (!(want && T.a[want - 1].kind == ACT_STRIKE)) {
        // Своего удара не ждёт: бить ли сейчас и каким. Открыт противник — почти всегда. Жетон на цель занят или
        // очередь не дошла — ждать (держать дистанцию на своём месте)
        const ActionDesc *tgt = ai_act(w, p.act[t]);
        bool open = p.react_t[t] || (tgt && p.act_t[t] > (tgt->kind == ACT_STRIKE ? tgt->hit_b : tgt->guard_b));
        f32 chance = open ? D.open : lerp2(D.attack, sk);
        want = 0;
        if (ai_roll(w.step, id, 1000) < chance && fight_may_strike(w, G, v, s, t)) {
            // Свой рядом под клинком (впереди-сбоку ближе досягаемости) — не мести сбоку и по ногам, бить сверху
            const bool ally = duel_ally_near(w, v, s, t);
            f32 best = -1e9f;
            for (u32 k = 0; k < T.count; ++k) {
                const ActionDesc &C = T.a[k];
                if (C.kind != ACT_STRIKE) continue;
                f32 fit = dist >= C.reach_min - 0.2f && dist <= C.reach_max + 0.3f ? 1.0f : 0.4f;
                f32 c = fit * (p.last[s] == k + 1 ? 0.5f : 1.0f) + noise();
                if (ally && C.line != LINE_OVER) c -= 2.0f;
                if (c > best) { best = c; want = k + 1; }
            }
        }
    }
    if (want != p.want[s]) out.push_back({w.step, INTENT_ACT, WPos{}, (u64)id | ((u64)want << 32)});
}
