#include "fighter.h"

#include <string.h>

#include "../core/jobs.h"
#include "../sim/combat_tune.h"

enum { ACT_ATTACK = 1, ACT_RETREAT = 2, ACT_RALLY = 4, ACT_SURRENDER = 8 };

static f32 dist2(const CombatWorld *w, u32 i, u32 j)
{
    f32 dx = w->x[j] - w->x[i], dz = w->z[j] - w->z[i];
    return dx * dx + dz * dz;
}

static f32 angle_from(const CombatWorld *w, u32 i, u32 j)
{
    f32 a = atan2f(w->x[j] - w->x[i], w->z[j] - w->z[i]) - w->heading[i];
    while (a > PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

// Можно ли бить этого бойца: выбывших (без сознания, сдавшихся) при милосердии не трогают
static bool valid_target(const CombatWorld *w, u32 i, u32 j)
{
    if (j == i || !combat_hostile(w, i, j) || w->idle[j]) return false;
    u8 s = w->state[j];
    if (s == FS_DEAD) return false;
    if (w->mercy && fs_out(s)) return false;
    return true;
}

static f32 target_score(const CombatWorld *w, u32 i, u32 j, f32 d)
{
    f32 score = -d;
    if (w->target[j] == (i32)i) score += g_tune.target_attacker;  // кто бьёт меня
    if (w->target[i] == (i32)j) score += g_tune.target_current;   // не метаться
    score += g_tune.target_wounded * (1.0f - clampf(combat_vital_ratio(w, j), 0.0f, 1.0f));  // добить раненого
    score -= g_tune.target_turn * fabsf(angle_from(w, i, j));     // цена поворота
    return score;
}

// Поиск расширяющимся кругом до pick_max_r (дальше врагов не ищут, а идут к середине вражеского строя).
// Любая цель, кроме текущей, набирает не больше bonus − расстояние, так что когда лучший счёт
// не ниже bonus − r, дальше смотреть незачем: ответ тот же, что при полном переборе
static i32 pick_target(const CombatWorld *w, u32 i)
{
    u32 near[COMBAT_NEAR_MAX];
    // Метки сетки, среди которых может быть цель
    u32 want = combat_foe_tags(w, w->team[i]);
    if (!w->mercy) want |= TAG_BIT(TAG_OUT);
    // Текущая цель считается сразу: только ей положена прибавка «не метаться»
    const f32 max_r = g_tune.pick_max_r, bonus = g_tune.target_attacker + g_tune.target_wounded;
    i32 cur = w->target[i], best = -1;
    f32 best_score = -1e9f;
    if (cur >= 0 && valid_target(w, i, (u32)cur) && dist2(w, i, (u32)cur) <= max_r * max_r) {
        best = cur;
        best_score = target_score(w, i, (u32)cur, sqrtf(dist2(w, i, (u32)cur)));
    }
    for (f32 r = 4.0f;; r = fminf(r * 1.5f, max_r)) {
        if (best >= 0 && best_score >= bonus - r) return best;
        if (combat_any_near(w, w->x[i], w->z[i], r, want)) {
            u32 nn = combat_near(w, w->x[i], w->z[i], r, want, near);
            for (u32 q = 0; q < nn; q++) {
                u32 j = near[q];
                if ((i32)j == cur || !valid_target(w, i, j)) continue;
                f32 d2 = dist2(w, i, j);
                if (d2 > r * r) continue;
                f32 d = sqrtf(d2);
                if (bonus - d < best_score) continue;  // даже с прибавками не догонит
                f32 score = target_score(w, i, j, d);
                if (score > best_score || (score == best_score && (i32)j < best)) {
                    best_score = score;
                    best = (i32)j;
                }
            }
        }
        if (r >= max_r) return best;
    }
}

// Сколько врагов и своих попадёт под размах
static void count_arc(const CombatWorld *w, u32 i, u8 kind, u32 *enemies, u32 *allies)
{
    f32 reach = combat_reach(w, i, kind) + g_tune.strike_reach_add;
    f32 half = 0.5f * combat_arc(w, i, kind);
    *enemies = *allies = 0;
    u32 near[COMBAT_NEAR_MAX];
    u32 nn = combat_near(w, w->x[i], w->z[i], reach, TAGS_ALIVE, near);
    for (u32 q = 0; q < nn; q++) {
        u32 j = near[q];
        if (j == i || w->state[j] == FS_DEAD || fs_lying(w->state[j])) continue;
        if (dist2(w, i, j) > reach * reach || fabsf(angle_from(w, i, j)) > half) continue;
        if (!combat_hostile(w, i, j)) (*allies)++;
        else if (!fs_out(w->state[j])) (*enemies)++;
    }
}

static u8 choose_attack(const CombatWorld *w, u32 i, u32 t)
{
    u8 mask = g_weapons[w->weapon[i]].attacks;
    u32 en = 0, al = 0;
    if (mask & (1u << ATK_SWING)) {
        count_arc(w, i, ATK_SWING, &en, &al);
        if (en >= 2 && al == 0) return ATK_SWING;
        if (al > 0) mask &= (u8)~(1u << ATK_SWING);
    }
    if (fs_lying(w->state[t])) mask &= (u8)~(1u << ATK_SWING);
    // Дальний укол для длинного оружия, когда цель у края досягаемости
    f32 d = sqrtf(dist2(w, i, t));
    if ((mask & (1u << ATK_THRUST)) && d > combat_reach(w, i, ATK_SWING) - g_tune.thrust_edge) return ATK_THRUST;
    u8 opts[ATK_COUNT];
    u32 n = 0;
    for (u8 k = 0; k < ATK_COUNT; k++)
        if (mask & (1u << k)) opts[n++] = k;
    if (!n) return ATK_OVERHEAD;
    u32 pick = (u32)(combat_roll(w, i, t, 90) * (f32)n);
    return opts[pick < n ? pick : n - 1];
}

// Врагов рядом нет: идти к середине ближайшего вражеского строя
static void march_to_enemy(const FighterAI *ai, CombatWorld *w, u32 i)
{
    f32 best = 1e30f, dx = 0.0f, dz = 0.0f;
    for (u32 t = 0; t < COMBAT_MAX_TEAMS; t++) {
        if (!((w->foe[w->team[i]] >> t) & 1u) || !ai->cn[t]) continue;
        f32 ex = ai->cx[t] - w->x[i], ez = ai->cz[t] - w->z[i];
        f32 d2 = ex * ex + ez * ez;
        if (d2 < best) {
            best = d2;
            dx = ex;
            dz = ez;
        }
    }
    if (best >= 1e30f || best < 1e-4f) return;
    f32 d = sqrtf(best);
    w->mvx[i] = dx / d;
    w->mvz[i] = dz / d;
}

// Решение бойца: пишет только его движение, мораль и намерение
static void decide(FighterAI *ai, CombatWorld *w, u32 i)
{
    u8 s = w->state[i];
    ai->act[i] = 0;
    ai->target[i] = w->target[i];
    w->mvx[i] = w->mvz[i] = 0.0f;
    if (w->idle[i] || fs_out(s) || fs_lying(s)) return;

    // Численный перевес врага рядом подтачивает мораль. Счёт по меткам сетки (команда; выбывшие и мёртвые
    // вне маски): свои вместе с ним самим, враги — команды, с которыми враждует
    u32 cnt[GRID_MAX_TAGS] = { 0 };
    combat_count(w, w->x[i], w->z[i], g_tune.outnumber_r, TAGS_FIGHTING, cnt);
    u32 al = cnt[2u + w->team[i]], en = 0;
    for (u32 m = combat_foe_tags(w, w->team[i]) & TAGS_FIGHTING; m; m &= m - 1) en += cnt[__builtin_ctz(m)];
    if (en > al + 1) w->morale[i] -= g_tune.morale_outnumber * (f32)(en - al);

    // По приказу бьёт только указанного, пока тот в бою; иначе выбирает сам
    i32 o = w->order[i];
    i32 t = o >= 0 && valid_target(w, i, (u32)o) ? o : pick_target(w, i);
    ai->target[i] = t;
    if (t < 0) {
        if (s == FS_RETREAT) ai->act[i] = ACT_RALLY;
        if (s == FS_READY || s == FS_RETREAT) march_to_enemy(ai, w, i);
        return;
    }
    u32 tt = (u32)t;
    f32 dx = w->x[tt] - w->x[i], dz = w->z[tt] - w->z[i];
    f32 d = sqrtf(dx * dx + dz * dz) + 1e-4f;
    dx /= d;
    dz /= d;
    if (s == FS_DODGE) {
        w->mvx[i] = -dx;
        w->mvz[i] = -dz;
        return;
    }

    if (w->morale[i] < g_tune.morale_surrender && w->mercy && (s == FS_READY || s == FS_RETREAT)) {
        ai->act[i] = ACT_SURRENDER;
        return;
    }
    if (w->morale[i] < g_tune.morale_flee && s == FS_READY) {
        ai->act[i] |= ACT_RETREAT;
        s = FS_RETREAT;
    } else if (w->morale[i] >= g_tune.morale_rally && s == FS_RETREAT) {
        ai->act[i] |= ACT_RALLY;
        s = FS_READY;
    }

    f32 reach = combat_reach(w, i, ATK_SWING);
    bool cornered = false;
    if (s == FS_RETREAT) {
        // Отход от ближайшего врага; у ограды деваться некуда — отбивается
        w->mvx[i] = -dx;
        w->mvz[i] = -dz;
        if (w->fence_r > 0.0f) {
            f32 r = sqrtf(w->x[i] * w->x[i] + w->z[i] * w->z[i]);
            cornered = r > w->fence_r - g_tune.cornered_r && d < reach;
        }
        if (!cornered) return;
    }
    if (s != FS_READY && !cornered) return;

    // Держать дистанцию своего оружия: ударом достаёт, вплотную не лезет; между — кружит.
    // Издали идёт к цели обычным шагом, в паре шагов от досягаемости — приставным, медленнее
    f32 side = (i & 1) ? 1.0f : -1.0f;
    if (d > reach * g_tune.reach_far) {
        f32 k = d > reach + g_tune.approach_slow_d ? 1.0f : g_tune.approach_slow;
        w->mvx[i] = dx * k;
        w->mvz[i] = dz * k;
    } else if (d < reach * g_tune.reach_close) {
        w->mvx[i] = -dx * g_tune.back_off;
        w->mvz[i] = -dz * g_tune.back_off;
    } else {
        // Не кружит навстречу своему: свой рядом с той стороны — обходит цель с другой
        u32 near[COMBAT_NEAR_MAX];
        u32 nn = combat_near(w, w->x[i], w->z[i], g_tune.ally_r, TAG_BIT(2u + w->team[i]), near);
        f32 best = g_tune.ally_r * g_tune.ally_r;
        for (u32 q = 0; q < nn; q++) {
            u32 j = near[q];
            f32 ax = w->x[j] - w->x[i], az = w->z[j] - w->z[i];
            f32 a2 = ax * ax + az * az;
            if (j == i || a2 >= best) continue;
            best = a2;
            // Касательная в свою сторону смотрит на своего — в другую сторону
            f32 own = (i & 1) ? 1.0f : -1.0f;
            side = (-dz * ax + dx * az) * own > 0.0f ? -own : own;
        }
        w->mvx[i] = -dz * side * g_tune.circle;
        w->mvz[i] = dx * side * g_tune.circle;
    }

    // Враг уже замахнулся на меня и ударит раньше: держать защиту, а не меняться ударами
    if (w->state[tt] == FS_WINDUP && w->target[tt] == (i32)i) return;

    u8 kind = choose_attack(w, i, tt);
    if (!w->disarmed[i] && w->pause[i] <= 0.0f && d <= combat_reach(w, i, kind) &&
        fabsf(angle_from(w, i, tt)) < g_tune.attack_angle_deg * (PI_F / 180.0f)) {
        ai->act[i] |= ACT_ATTACK;
        ai->kind[i] = kind;
    }
}

// Своё состояние. Чужих не трогает: замах соседа в этом же проходе ещё не начат.
static void apply_self(FighterAI *ai, CombatWorld *w, u32 i)
{
    if (w->idle[i]) return;
    w->target[i] = ai->target[i];
    u8 act = ai->act[i], s = w->state[i];
    if (act & ACT_SURRENDER) {
        if (s == FS_READY || s == FS_RETREAT) {
            combat_set_state(w, i, FS_SURRENDER, 0.0f);
            combat_event(w, EV_SURRENDER, i, i);
        }
        return;
    }
    if ((act & ACT_RETREAT) && s == FS_READY) {
        combat_set_state(w, i, FS_RETREAT, 0.0f);
        combat_event(w, EV_RETREAT, i, i);
    } else if ((act & ACT_RALLY) && s == FS_RETREAT) {
        combat_set_state(w, i, FS_READY, 0.0f);
    }
}

// Замах по решению, принятому до этого прохода. Замах с меньшим номером его не отменяет.
static void apply_attack(FighterAI *ai, CombatWorld *w, u32 i)
{
    if (w->idle[i] || !(ai->act[i] & ACT_ATTACK)) return;
    u8 s = w->state[i];
    if (s != FS_READY && s != FS_RETREAT) return;
    i32 tt = ai->target[i];
    if (tt < 0 || !valid_target(w, i, (u32)tt)) return;
    combat_begin_attack(w, i, ai->kind[i], (u32)tt, false);
}

typedef struct ThinkCtx {
    FighterAI  *ai;
    CombatWorld *w;
} ThinkCtx;

static void decide_job(void *p, u32 b, u32 e)
{
    ThinkCtx *c = p;
    for (u32 i = b; i < e; i++)
        if (c->w->tick % COMBAT_THINK_TICKS == 0) decide(c->ai, c->w, i);
}

void fighter_ai_init(FighterAI *ai, Arena *a, u32 cap)
{
    ai->cap = cap;
    ai->act = ARENA_PUSH_ARRAY(a, u8, cap);
    ai->kind = ARENA_PUSH_ARRAY(a, u8, cap);
    ai->target = ARENA_PUSH_ARRAY(a, i32, cap);
}

void fighter_ai_ser(Ser *s, FighterAI *ai, u32 count)
{
    ser_check(s, count <= ai->cap);
    if (s->bad) return;
    SER_N(s, ai->act, count);
    SER_N(s, ai->kind, count);
    SER_N(s, ai->target, count);
    SER(s, ai->cx);
    SER(s, ai->cz);
    SER(s, ai->cn);
}

void fighter_think_all(FighterAI *ai, CombatWorld *w)
{
    if (w->halted) {
        for (u32 i = 0; i < w->count; i++) w->mvx[i] = w->mvz[i] = 0.0f;
        return;
    }
    combat_sync_grid(w);
    // Середины строя считаются в одном потоке и по порядку: сумма от потоков не зависит
    memset(ai->cx, 0, sizeof ai->cx);
    memset(ai->cz, 0, sizeof ai->cz);
    memset(ai->cn, 0, sizeof ai->cn);
    for (u32 i = 0; i < w->count; i++) {
        u8 t = w->team[i];
        if (t >= COMBAT_MAX_TEAMS || w->idle[i] || fs_out(w->state[i])) continue;
        ai->cx[t] += w->x[i];
        ai->cz[t] += w->z[i];
        ai->cn[t]++;
    }
    for (u32 t = 0; t < COMBAT_MAX_TEAMS; t++)
        if (ai->cn[t]) {
            ai->cx[t] /= (f32)ai->cn[t];
            ai->cz[t] /= (f32)ai->cn[t];
        }
    // Все думают в одни тики. Фаза от номера слота давала младшим замах раньше старших.
    if (w->tick % COMBAT_THINK_TICKS == 0) {
        ThinkCtx c = { ai, w };
        jobs_for(w->count, 16, decide_job, &c);
        // Сначала своё состояние у всех, потом все замахи.
        // Иначе меньший номер успевает закрыть цель блоком до её собственного удара.
        for (u32 i = 0; i < w->count; i++) apply_self(ai, w, i);
        for (u32 i = 0; i < w->count; i++) apply_attack(ai, w, i);
    }
    for (u32 i = 0; i < w->count; i++)
        if (w->state[i] == FS_WINDUP && w->timer[i] == w->dur[i] && w->dur[i] > 0.0f) combat_swing_xp(w, i);
    combat_seal_guards(w);
}
