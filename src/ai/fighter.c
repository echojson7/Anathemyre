#include "fighter.h"

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
    if (j == i || w->team[j] == w->team[i]) return false;
    u8 s = w->state[j];
    if (s == FS_DEAD) return false;
    if (w->mercy && fs_out(s)) return false;
    return true;
}

static i32 pick_target(const CombatWorld *w, u32 i)
{
    i32 best = -1;
    f32 best_score = -1e9f;
    for (u32 j = 0; j < w->count; j++) {
        if (!valid_target(w, i, j)) continue;
        f32 score = -sqrtf(dist2(w, i, j));
        if (w->target[j] == (i32)i) score += 2.0f;               // кто бьёт меня
        if (w->target[i] == (i32)j) score += 1.5f;               // не метаться
        score += 1.5f * (1.0f - clampf(combat_vital_ratio(w, j), 0.0f, 1.0f));  // добить раненого
        score -= 0.5f * fabsf(angle_from(w, i, j));              // цена поворота
        if (score > best_score || (score == best_score && (i32)j < best)) {
            best_score = score;
            best = (i32)j;
        }
    }
    return best;
}

// Сколько врагов и своих попадёт под размах
static void count_arc(const CombatWorld *w, u32 i, u8 kind, u32 *enemies, u32 *allies)
{
    f32 reach = combat_reach(w, i, kind) + 0.3f;
    f32 half = 0.5f * combat_arc(w, i, kind);
    *enemies = *allies = 0;
    for (u32 j = 0; j < w->count; j++) {
        if (j == i || w->state[j] == FS_DEAD || fs_lying(w->state[j])) continue;
        if (dist2(w, i, j) > reach * reach || fabsf(angle_from(w, i, j)) > half) continue;
        if (w->team[j] == w->team[i]) (*allies)++;
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

static void think(CombatWorld *w, u32 i)
{
    u8 s = w->state[i];
    w->mvx[i] = w->mvz[i] = 0.0f;
    if (fs_out(s) || fs_lying(s)) return;

    // Численный перевес врага рядом подтачивает мораль
    u32 en = 0, al = 1;
    for (u32 j = 0; j < w->count; j++) {
        if (j == i || fs_out(w->state[j]) || dist2(w, i, j) > 64.0f) continue;
        if (w->team[j] == w->team[i]) al++;
        else en++;
    }
    if (en > al + 1) w->morale[i] -= 0.1f * (f32)(en - al);

    i32 t = pick_target(w, i);
    w->target[i] = t;
    if (t < 0) {
        if (s == FS_RETREAT) combat_set_state(w, i, FS_READY, 0.0f);
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
        combat_set_state(w, i, FS_SURRENDER, 0.0f);
        combat_event(w, EV_SURRENDER, i, i);
        return;
    }
    if (w->morale[i] < 25.0f && s == FS_READY) {
        combat_set_state(w, i, FS_RETREAT, 0.0f);
        combat_event(w, EV_RETREAT, i, i);
        s = FS_RETREAT;
    } else if (w->morale[i] >= 30.0f && s == FS_RETREAT) {
        combat_set_state(w, i, FS_READY, 0.0f);
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

    // Держать дистанцию своего оружия и кружить
    f32 want = reach * 0.85f;
    f32 side = (i & 1) ? 1.0f : -1.0f;
    if (d > reach * 0.95f) {
        w->mvx[i] = dx;
        w->mvz[i] = dz;
    } else if (d < want * 0.6f) {
        w->mvx[i] = -dx * 0.7f;
        w->mvz[i] = -dz * 0.7f;
    } else {
        w->mvx[i] = -dz * side * 0.5f;
        w->mvz[i] = dx * side * 0.5f;
    }

    // Враг уже замахнулся на меня и ударит раньше: держать защиту, а не меняться ударами
    if (w->state[tt] == FS_WINDUP && w->target[tt] == (i32)i) return;

    u8 kind = choose_attack(w, i, tt);
    if (w->pause[i] <= 0.0f && d <= combat_reach(w, i, kind) &&
        fabsf(angle_from(w, i, tt)) < 25.0f * (PI_F / 180.0f)) {
        combat_begin_attack(w, i, kind, tt, false);
    }
}

void fighter_think_all(CombatWorld *w)
{
    if (w->halted) {
        for (u32 i = 0; i < w->count; i++) w->mvx[i] = w->mvz[i] = 0.0f;
        return;
    }
    for (u32 i = 0; i < w->count; i++)
        if ((i + w->tick) % COMBAT_THINK_TICKS == 0) think(w, i);
}
