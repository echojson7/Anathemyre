// ИИ группы (этап 12, docs/people.md «Группа»). Как и поединок, решает по миру до шага, каждый пишет только
// своё намерение; случайность — хеш шага и номера. Очередь на удар и места считаются так, что все бойцы у одной
// цели видят одно и то же: двое в один тик не займут один жетон, двое не встанут на одно место
#include "group.h"
#include "duel.h"
#include "../core/hash.h"
#include "../core/text.h"
#include "../sim/world.h"
#include <stddef.h>

static void group_err(char *err, usize n, const char *what) {
    usize o = 0;
    for (const char *q = what; *q && o + 1 < n; ++q) err[o++] = *q;
    if (n) err[o] = 0;
}

bool group_parse(const u8 *data, usize size, GroupDesc &out, char *err, usize err_size) {
    TextReader r = text_reader(data, size);
    TextLine l;
    u32 have = 0;
    out = GroupDesc{};
    while (text_next(r, l)) {
        Str k = l.w[0];
        u32 u = 0;
        f32 a = 0;
        bool one = l.count == 2 && parse_f32(l.w[1], a);
        if (str_eq(k, "retarget") && l.count == 2 && parse_u32(l.w[1], u) && u > 0 && u < 600) { out.retarget = u; have |= 1; }
        else if (str_eq(k, "aggro") && one && a > 0 && a < 200) { out.aggro = a; have |= 2; }
        else if (str_eq(k, "tokens") && l.count == 2 && parse_u32(l.w[1], u) && u > 0 && u < 16) { out.tokens = u; have |= 4; }
        else if (str_eq(k, "crowd") && one && a >= 0) { out.crowd = a; have |= 8; }
        else if (str_eq(k, "near") && one && a > 0) { out.near = a; have |= 16; }
        else if (str_eq(k, "keep") && one && a >= 0) { out.keep = a; have |= 32; }
        else if (str_eq(k, "threat") && one && a >= 0) { out.threat = a; have |= 64; }
        else if (str_eq(k, "open") && one && a >= 0) { out.open = a; have |= 128; }
        else if (str_eq(k, "wait") && one && a >= 0 && a <= 6.3f) { out.wait = a; have |= 256; }
        else if (str_eq(k, "gap") && one && a >= 10 && a <= 180) { out.gap = a / 360.0f; have |= 512; }
        else {
            group_err(err, err_size, "непонятная строка (retarget, aggro, tokens, crowd, near, keep, threat, open, wait, gap)");
            return false;
        }
    }
    if (have != 1023) {
        group_err(err, err_size, "нужны все поля: retarget, aggro, tokens, crowd, near, keep, threat, open, wait, gap");
        return false;
    }
    out.hash = hash_bytes(&out, offsetof(GroupDesc, hash));
    return true;
}


bool fight_holds(const World &w, u32 s) {
    const People &p = w.people;
    const ActionDesc *A = ai_act(w, p.act[s]);
    if (A && A->kind == ACT_STRIKE && p.act_t[s] <= A->hit_b) return true;
    const ActionDesc *W = ai_act(w, p.want[s]);
    return W && W->kind == ACT_STRIKE;
}

void fight_view(const World &w, FightView &v) {
    const People &p = w.people;
    u16 tgt[PEOPLE_CAPACITY], fill[PEOPLE_CAPACITY];
    i32 soon[PEOPLE_CAPACITY];
    for (u32 i = 0; i < PEOPLE_CAPACITY; ++i) { v.holders[i] = 0; v.urgent[i] = 0xFFFF; soon[i] = 0x7FFFFFFF; }
    // Цель каждого; счёт нападающих — во временном (holders), потом список по цели в порядке номеров
    for (u32 s = 0; s < PEOPLE_CAPACITY; ++s) {
        u32 t;
        tgt[s] = 0xFFFF;
        if (!p.alive[s] || !p.target[s] || !people_valid(p, p.target[s], &t)) continue;
        tgt[s] = (u16)t;
        v.holders[t]++;
    }
    u32 acc = 0;
    for (u32 i = 0; i < PEOPLE_CAPACITY; ++i) { v.first[i] = fill[i] = (u16)acc; acc += v.holders[i]; v.holders[i] = 0; }
    v.first[PEOPLE_CAPACITY] = (u16)acc;
    for (u32 s = 0; s < PEOPLE_CAPACITY; ++s) {
        const u32 t = tgt[s];
        if (t == 0xFFFF) continue;
        v.list[fill[t]++] = (u16)s;
        // Жетоны и самый близкий к окну удар по каждому (поровну — меньший номер: идём по порядку)
        if (fight_holds(w, s)) v.holders[t]++;
        const ActionDesc *A = ai_act(w, p.act[s]);
        if (!A || A->kind != ACT_STRIKE || p.act_hit[s] || p.act_t[s] > A->hit_b) continue;
        i32 until = (i32)A->hit_a - (i32)p.act_t[s];
        if (until < soon[t]) { soon[t] = until; v.urgent[t] = (u16)s; }
    }
}

bool fight_may_strike(const World &w, const GroupDesc &g, const FightView &v, u32 s, u32 t) {
    if (fight_holds(w, s)) return true;
    if (v.holders[t] >= g.tokens) return false;
    const People &p = w.people;
    const u32 now = (u32)w.step;
    const u32 mine = now - p.struck[s];
    // Очередь — готовые к удару без жетона, по давности последнего удара (поровну — меньший номер)
    u32 ahead = 0;
    for (u32 i = v.first[t]; i < v.first[t + 1]; ++i) {
        const u32 a = v.list[i];
        if (a == s || fight_holds(w, a) || p.react_t[a] || p.freeze[a]) continue;
        const ActionDesc *A = ai_act(w, p.act[a]);
        if (A && p.act_t[a] < A->cancel) continue;
        const u32 theirs = now - p.struck[a];
        if (theirs > mine || (theirs == mine && a < s)) ahead++;
    }
    return ahead + v.holders[t] < g.tokens;
}

// Место s вокруг t: нападающие стоят дугой в порядке своих курсов от цели (никто не переходит через соседа),
// середина дуги — средний курс, шаг — оборот на число нападающих, не больше gap. Один нападающий — без кольца
static u16 fight_place(const World &w, const GroupDesc &g, const FightView &v, u32 s, u32 t) {
    const People &p = w.people;
    const u32 n = (u32)(v.first[t + 1] - v.first[t]);
    if (n < 2) return 0;
    struct Ent { i32 rel; u32 slot; };
    Ent e[64];
    Turn bear[64];
    f32 sx = 0.0f, sz = 0.0f;
    u32 k = 0;
    for (u32 i = v.first[t]; i < v.first[t + 1] && k < 64; ++i) {
        const u32 a = v.list[i];
        Vec2 d = wpos_delta_xz(p.cx[t], p.cz[t], p.x[t], p.z[t], p.cx[a], p.cz[a], p.x[a], p.z[a]);
        f32 len = length(d);
        bear[k] = len > 1e-3f ? turn_of_dir(d.x, d.y) : (Turn)(hash_mix(a) >> 32);
        f32 sn, cs;
        sincos_turn(bear[k], &sn, &cs);
        sx += sn;
        sz += cs;
        e[k].slot = a;
        k++;
    }
    const Turn mid = sx * sx + sz * sz > 1e-6f ? turn_of_dir(sx, sz) : bear[0];
    for (u32 i = 0; i < k; ++i) e[i].rel = (i32)(bear[i] - mid);
    // Порядок по курсу от середины, поровну — по номеру (вставками: нападающих на одного мало)
    for (u32 i = 1; i < k; ++i) {
        Ent x = e[i];
        u32 j = i;
        while (j > 0 && (e[j - 1].rel > x.rel || (e[j - 1].rel == x.rel && e[j - 1].slot > x.slot))) { e[j] = e[j - 1]; --j; }
        e[j] = x;
    }
    u32 me = 0;
    while (me < k && e[me].slot != s) ++me;
    if (me == k) return 0;
    const f32 step = f_min(1.0f / (f32)k, g.gap);
    const f32 off = ((f32)me - 0.5f * (f32)(k - 1)) * step;
    const Turn at = mid + (Turn)(i32)(off * 4294967296.0f);
    u32 back = 0;
    if (!fight_may_strike(w, g, v, s, t)) back = (u32)(g.wait * 10.0f + 0.5f);
    return (u16)(PLACE_RING | ((at + (1u << 23)) >> 24) | (back << PLACE_BACK_SHIFT));
}

// Лучшая цель s среди врагов (сторона не 0 и не своя) ближе aggro; нынешнюю держит до aggro · 1.5. Нет — его номер
static u32 group_pick(const World &w, const GroupDesc &g, const FightView &v, u32 s, u32 cur) {
    const People &p = w.people;
    const u32 id = make_id(s, p.gen[s]).v;
    f32 best = -1e9f;
    u32 pick = PEOPLE_CAPACITY;
    for (u32 e = 0; e < PEOPLE_CAPACITY; ++e) {
        if (!p.alive[e] || e == s || !p.side[e] || p.side[e] == p.side[s]) continue;
        Vec2 d = wpos_delta_xz(p.cx[s], p.cz[s], p.x[s], p.z[s], p.cx[e], p.cz[e], p.x[e], p.z[e]);
        const f32 lim = e == cur ? g.aggro * 1.5f : g.aggro;
        const f32 d2 = dot(d, d);
        if (d2 > lim * lim) continue;
        f32 n = (f32)(v.first[e + 1] - v.first[e]) - (e == cur ? 1.0f : 0.0f);
        f32 c = -f_sqrt(d2) / g.near - g.crowd * n;
        if (n >= (f32)g.tokens) c -= g.crowd * (n - (f32)g.tokens + 1.0f);
        if (e == cur) c += g.keep;
        const ActionDesc *A = ai_act(w, p.act[e]);
        if (A && A->kind == ACT_STRIKE && p.target[e] == id && p.act_t[e] <= A->hit_b) c += g.threat;
        if (p.react_t[e]) c += g.open;
        c += ai_roll(w.step, id, 2000 + e) * 0.05f;
        if (c > best) { best = c; pick = e; }
    }
    return pick;
}

void ai_fight(const World &w, const DuelDesc &D, const GroupDesc &G, FixedArray<Intent> &out) {
    const People &p = w.people;
    if (!w.actions.count || !p.live || !D.think || !G.retarget) return;
    FightView v;
    fight_view(w, v);
    for (u32 s = 0; s < PEOPLE_CAPACITY; ++s) {
        if (!p.alive[s]) continue;
        const u32 id = make_id(s, p.gen[s]).v;
        const u64 h = hash_mix(id);
        u32 cur = PEOPLE_CAPACITY;
        if (p.target[s] && !people_valid(p, p.target[s], &cur)) cur = PEOPLE_CAPACITY;
        // Выбор цели: только у бойцов со стороной. Без цели и в пути по приказу — не ищет (приказ «идти» уводит
        // из боя); во время своего удара цель не меняет
        if (p.side[s] && (w.step + (h >> 16 & 0xFFFF)) % G.retarget == 0 && (cur < PEOPLE_CAPACITY || p.state[s] == MOVE_STAND)) {
            const ActionDesc *A = ai_act(w, p.act[s]);
            if (!(A && A->kind == ACT_STRIKE && p.act_t[s] <= A->hit_b)) {
                u32 e = group_pick(w, G, v, s, cur);
                if (e < PEOPLE_CAPACITY && e != cur) {
                    out.push_back({w.step, INTENT_ATTACK, WPos{}, (u64)id | ((u64)make_id(e, p.gen[e]).v << 32)});
                    continue;  // о новой цели подумает в следующий свой тик
                }
            }
        }
        if (cur == PEOPLE_CAPACITY || (w.step + (h & 0xFFFF)) % D.think) continue;
        // Место вокруг цели: шлём, только когда сменилось заметно (отступ, кольцо или курс на 3/256 оборота)
        const u16 pl = fight_place(w, G, v, s, cur), was = p.place[s];
        const i32 da = (i32)(i8)(u8)((pl & PLACE_ANGLE) - (was & PLACE_ANGLE));
        if ((pl & ~PLACE_ANGLE) != (was & ~PLACE_ANGLE) || da > 2 || da < -2)
            out.push_back({w.step, INTENT_PLACE, WPos{}, (u64)id | ((u64)pl << 32)});
        duel_think(w, D, G, v, s, out);
    }
}
