#include "fighter.h"

#include <string.h>

#include "../core/jobs.h"

#define PICK_MAX_R 48.0f   // дальше врагов не ищут, а идут к середине вражеского строя
#define PICK_BONUS 3.5f    // наибольшая прибавка к счёту нетекущей цели сверх −расстояния

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
    if (w->target[j] == (i32)i) score += 2.0f;               // кто бьёт меня
    if (w->target[i] == (i32)j) score += 1.5f;               // не метаться
    score += 1.5f * (1.0f - clampf(combat_vital_ratio(w, j), 0.0f, 1.0f));  // добить раненого
    score -= 0.5f * fabsf(angle_from(w, i, j));              // цена поворота
    return score;
}

// Поиск расширяющимся кругом. Любая цель, кроме текущей, набирает не больше
// PICK_BONUS − расстояние, так что когда лучший счёт не ниже PICK_BONUS − r,
// дальше смотреть незачем: ответ тот же, что при полном переборе
static i32 pick_target(const CombatWorld *w, u32 i)
{
    u32 near[COMBAT_NEAR_MAX];
    // Метки сетки, среди которых может быть цель
    u32 want = combat_foe_tags(w, w->team[i]);
    if (!w->mercy) want |= TAG_BIT(TAG_OUT);
    // Текущая цель считается сразу: только ей положена прибавка «не метаться»
    i32 cur = w->target[i], best = -1;
    f32 best_score = -1e9f;
    if (cur >= 0 && valid_target(w, i, (u32)cur) && dist2(w, i, (u32)cur) <= PICK_MAX_R * PICK_MAX_R) {
        best = cur;
        best_score = target_score(w, i, (u32)cur, sqrtf(dist2(w, i, (u32)cur)));
    }
    for (f32 r = 4.0f;; r = fminf(r * 1.5f, PICK_MAX_R)) {
        if (best >= 0 && best_score >= PICK_BONUS - r) return best;
        if (combat_any_near(w, w->x[i], w->z[i], r, want)) {
            u32 nn = combat_near(w, w->x[i], w->z[i], r, want, near);
            for (u32 q = 0; q < nn; q++) {
                u32 j = near[q];
                if ((i32)j == cur || !valid_target(w, i, j)) continue;
                f32 d2 = dist2(w, i, j);
                if (d2 > r * r) continue;
                f32 d = sqrtf(d2);
                if (PICK_BONUS - d < best_score) continue;  // даже с прибавками не догонит
                f32 score = target_score(w, i, j, d);
                if (score > best_score || (score == best_score && (i32)j < best)) {
                    best_score = score;
                    best = (i32)j;
                }
            }
        }
        if (r >= PICK_MAX_R) return best;
    }
}

// Сколько врагов и своих попадёт под размах
static void count_arc(const CombatWorld *w, u32 i, u8 kind, u32 *enemies, u32 *allies)
{
    f32 reach = combat_reach(w, i, kind) + 0.3f;
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
    if ((mask & (1u << ATK_THRUST)) && d > combat_reach(w, i, ATK_SWING) - 0.1f) return ATK_THRUST;
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

    // Численный перевес врага рядом подтачивает мораль
    u32 en = 0, al = 1;
    u32 near[COMBAT_NEAR_MAX];
    u32 nn = combat_near(w, w->x[i], w->z[i], 8.0f, TAGS_FIGHTING, near);
    for (u32 q = 0; q < nn; q++) {
        u32 j = near[q];
        if (j == i || fs_out(w->state[j]) || dist2(w, i, j) > 64.0f) continue;
        if (w->team[j] == w->team[i]) al++;
        else if (combat_hostile(w, i, j)) en++;
    }
    if (en > al + 1) w->morale[i] -= 0.1f * (f32)(en - al);

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

    if (w->morale[i] < 10.0f && w->mercy && (s == FS_READY || s == FS_RETREAT)) {
        ai->act[i] = ACT_SURRENDER;
        return;
    }
    if (w->morale[i] < 25.0f && s == FS_READY) {
        ai->act[i] |= ACT_RETREAT;
        s = FS_RETREAT;
    } else if (w->morale[i] >= 30.0f && s == FS_RETREAT) {
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
            cornered = r > w->fence_r - 1.0f && d < reach;
        }
        if (!cornered) return;
    }
    if (s != FS_READY && !cornered) return;

    // Держать дистанцию своего оружия: ударом достаёт, вплотную не лезет; между — кружит
    f32 side = (i & 1) ? 1.0f : -1.0f;
    if (d > reach * 0.98f) {
        w->mvx[i] = dx;
        w->mvz[i] = dz;
    } else if (d < reach * 0.82f) {
        w->mvx[i] = -dx * 0.7f;
        w->mvz[i] = -dz * 0.7f;
    } else {
        w->mvx[i] = -dz * side * 0.5f;
        w->mvz[i] = dx * side * 0.5f;
    }

    // Враг уже замахнулся на меня и ударит раньше: держать защиту, а не меняться ударами
    if (w->state[tt] == FS_WINDUP && w->target[tt] == (i32)i) return;

    u8 kind = choose_attack(w, i, tt);
    if (!w->disarmed[i] && w->pause[i] <= 0.0f && d <= combat_reach(w, i, kind) &&
        fabsf(angle_from(w, i, tt)) < 25.0f * (PI_F / 180.0f)) {
        ai->act[i] |= ACT_ATTACK;
        ai->kind[i] = kind;
    }
}

// Применение решения. Идёт по номерам в одном потоке: замах меняет состояние цели,
// поэтому каждое намерение перепроверяется по тому, что сделали бойцы с меньшим номером
static void apply(FighterAI *ai, CombatWorld *w, u32 i)
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
        s = FS_RETREAT;
    } else if ((act & ACT_RALLY) && s == FS_RETREAT) {
        combat_set_state(w, i, FS_READY, 0.0f);
        s = FS_READY;
    }
    if (!(act & ACT_ATTACK) || (s != FS_READY && s != FS_RETREAT)) return;
    u32 tt = (u32)ai->target[i];
    if (!valid_target(w, i, tt)) return;
    if (w->state[tt] == FS_WINDUP && w->target[tt] == (i32)i) return;
    combat_begin_attack(w, i, ai->kind[i], tt, false);
}

typedef struct ThinkCtx {
    FighterAI  *ai;
    CombatWorld *w;
} ThinkCtx;

static void decide_job(void *p, u32 b, u32 e)
{
    ThinkCtx *c = p;
    for (u32 i = b; i < e; i++)
        if ((i + c->w->tick) % COMBAT_THINK_TICKS == 0) decide(c->ai, c->w, i);
}

void fighter_ai_init(FighterAI *ai, Arena *a, u32 cap)
{
    ai->cap = cap;
    ai->act = ARENA_PUSH_ARRAY(a, u8, cap);
    ai->kind = ARENA_PUSH_ARRAY(a, u8, cap);
    ai->target = ARENA_PUSH_ARRAY(a, i32, cap);
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
    ThinkCtx c = { ai, w };
    jobs_for(w->count, 16, decide_job, &c);
    for (u32 i = 0; i < w->count; i++)
        if ((i + w->tick) % COMBAT_THINK_TICKS == 0) apply(ai, w, i);
}
