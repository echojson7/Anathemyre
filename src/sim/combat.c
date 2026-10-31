#include "combat.h"

#include <string.h>

#include "../core/hash.h"
#include "../core/jobs.h"
#include "action.h"
#include "anim.h"
#include "combat_tune.h"

CombatTune g_tune;

#define MAX_STRIKE_TARGETS 8
#define GRID_SLACK 1.5f  // запас запроса: за тик сбитого с ног отбрасывает до 1.5 м

// ---------------------------------------------------------------- служебное

// Случайное число без общего потока: зависит только от seed, тика и участников,
// поэтому порядок обработки бойцов (и потоки) на исход не влияют
f32 combat_roll(const CombatWorld *w, u32 a, u32 b, u32 salt)
{
    u32 h = hash_u32((u32)w->seed ^ hash_u32((u32)(w->seed >> 32) + w->tick * 0x9E3779B9u));
    h = hash_u32(h ^ (a * 0x85EBCA6Bu) ^ hash_u32(b + 0x27d4eb2du) ^ (salt * 0xC2B2AE35u));
    return (f32)(h >> 8) * (1.0f / 16777216.0f);
}

void combat_event(CombatWorld *w, u8 kind, u32 a, u32 b)
{
    // Переполненный журнал пишет в запасной слот, чтобы вызывающий мог дописать поля
    w->last_ev = w->ev_count < w->ev_cap ? w->ev_count++ : w->ev_cap;
    CombatEvent *e = &w->ev[w->last_ev];
    memset(e, 0, sizeof *e);
    e->tick = w->tick;
    e->kind = kind;
    e->a = (u16)a;
    e->b = (u16)b;
}

void combat_events_done(CombatWorld *w)
{
    if (w->sink && w->ev_count) w->sink(w->sink_user, w);
    w->ev_count = 0;
}

static void act_update(CombatWorld *w, u32 i);

void combat_set_state(CombatWorld *w, u32 i, u8 s, f32 dur)
{
    // Как ляжет (prone), решают go_down, begin_getup и check_body до смены состояния.
    // Новое состояние — новое действие всем телом: откуда пришёл, решает клип падения и переворот
    u8 old = w->state[i];
    if (s != old) {
        bool lay = fs_lying(old);
        f32 frac = combat_act_frac(w, i, 0.0f);
        u8 was_front = w->act_flags[i] & ACTF_WAS_FRONT, fl = was_front;
        if ((!lay && old != FS_SURRENDER) || (old == FS_KNOCKED && frac < 0.25f) || (old == FS_GETUP && frac > 0.8f))
            fl |= ACTF_STAND;
        if (old == FS_SURRENDER) fl |= ACTF_KNEEL;
        if (s == FS_CRAWL && lay && !was_front) fl |= ACTF_TURN;
        w->act_flags[i] = fl;
        w->act_t[i] = 0.0f;
    }
    w->state[i] = s;
    w->timer[i] = dur;
    w->dur[i] = dur;
    act_update(w, i);
}

static f32 sat01(f32 x) { return clampf(x, 0.0f, 1.0f); }

static f32 part_ratio(const CombatWorld *w, u32 i, u32 p)
{
    if (w->severed[i] & (1u << p)) return 0.0f;
    f32 r = sat01(w->hp[p][i] / g_part_hp[p]);
    return (w->fracture[i] & (1u << p)) ? r * 0.5f : r;
}

f32 combat_vital_ratio(const CombatWorld *w, u32 i)
{
    f32 r = 1.0f;
    for (u32 p = 0; p <= PART_STOMACH; p++) r = fminf(r, w->hp[p][i] / g_part_hp[p]);
    return r;
}

static const WeaponDef *weapon_of(const CombatWorld *w, u32 i) { return &g_weapons[w->weapon[i]]; }

static f32 sk(const CombatWorld *w, u32 i, u32 s) { return w->skill[s][i]; }

static f32 load_kg(const CombatWorld *w, u32 i)
{
    f32 kg = weapon_of(w, i)->weight;
    for (u32 s = 0; s < SLOT_COUNT; s++)
        if (w->armor[s][i] != ARMOR_NONE) kg += g_armors[w->armor[s][i]].weight;
    return kg;
}

// Перегруз: доля штрафа к скорости, уклонению и замаху
static f32 encumbrance(const CombatWorld *w, u32 i)
{
    f32 over = load_kg(w, i) - (g_tune.load_free_kg + sk(w, i, SK_STRENGTH) / g_tune.load_free_str_div);
    return over > 0.0f ? fminf(over * g_tune.encumbrance_per_kg, g_tune.encumbrance_max) : 0.0f;
}

static f32 weapon_overweight(const CombatWorld *w, u32 i)
{
    return fmaxf(0.0f, weapon_of(w, i)->weight - (g_tune.weapon_free_kg + sk(w, i, SK_STRENGTH) / g_tune.weapon_free_str_div));
}

static f32 shock_factor(const CombatWorld *w, u32 i)
{
    return 1.0f - g_tune.shock_penalty * sat01(w->shock[i] / g_tune.shock_full);
}

static f32 armor_dodge_pen(const CombatWorld *w, u32 i)
{
    f32 p = 0.0f;
    for (u32 s = 0; s < SLOT_COUNT; s++)
        if (w->armor[s][i] != ARMOR_NONE) p += g_armors[w->armor[s][i]].dodge_pen;
    return fminf(p, g_tune.armor_dodge_pen_max);
}

// Враги перед бойцом в радиусе surround_r: каждый следующий умножает защиту на surround_mul
static f32 surround_factor(const CombatWorld *w, u32 i)
{
    u32 n = 0;
    f32 fx = sinf(w->heading[i]), fz = cosf(w->heading[i]);
    u32 near[COMBAT_NEAR_MAX];
    u32 nn = combat_near(w, w->x[i], w->z[i], g_tune.surround_r + GRID_SLACK, combat_foe_tags(w, w->team[i]), near);
    for (u32 q = 0; q < nn; q++) {
        u32 j = near[q];
        if (!combat_hostile(w, i, j) || fs_out(w->state[j]) || fs_lying(w->state[j])) continue;
        f32 dx = w->x[j] - w->x[i], dz = w->z[j] - w->z[i];
        if (dx * dx + dz * dz > g_tune.surround_r * g_tune.surround_r || dx * fx + dz * fz < 0.0f) continue;
        n++;
    }
    return n > 1 ? powf(g_tune.surround_mul, (f32)(n - 1)) : 1.0f;
}

static f32 attack_eff(const CombatWorld *w, u32 i)
{
    const WeaponDef *wd = weapon_of(w, i);
    f32 base = 0.5f * (sk(w, i, SK_ATTACK) + sk(w, i, SK_WEAPON + wd->wclass)) - g_tune.attack_overweight_pen * weapon_overweight(w, i);
    f32 arm = g_tune.attack_arm_min + (1.0f - g_tune.attack_arm_min) * part_ratio(w, i, PART_ARM_R);
    return fmaxf(0.0f, base) * arm * shock_factor(w, i);
}

static f32 defense_eff(const CombatWorld *w, u32 i)
{
    const WeaponDef *wd = weapon_of(w, i);
    f32 base = 0.5f * (sk(w, i, SK_DEFENSE) + sk(w, i, SK_WEAPON + wd->wclass));
    f32 arm = (g_tune.defense_arm_l_min + (1.0f - g_tune.defense_arm_l_min) * part_ratio(w, i, PART_ARM_L)) *
              (g_tune.defense_arm_r_min + (1.0f - g_tune.defense_arm_r_min) * part_ratio(w, i, PART_ARM_R));
    return base * arm * shock_factor(w, i) * surround_factor(w, i);
}

static f32 dodge_eff(const CombatWorld *w, u32 i)
{
    f32 legs = 0.5f * (part_ratio(w, i, PART_LEG_L) + part_ratio(w, i, PART_LEG_R));
    return sk(w, i, SK_DODGE) * (1.0f - armor_dodge_pen(w, i)) * (1.0f - encumbrance(w, i)) * legs *
           shock_factor(w, i) * surround_factor(w, i);
}

f32 combat_reach(const CombatWorld *w, u32 i, u8 kind)
{
    return ARM_REACH + weapon_of(w, i)->length + g_attacks[kind].reach_add;
}

f32 combat_arc(const CombatWorld *w, u32 i, u8 kind)
{
    f32 deg = kind == ATK_SWING ? weapon_of(w, i)->arc_deg : g_attacks[kind].arc_deg;
    return deg * (PI_F / 180.0f);
}

static f32 angle_wrap(f32 a)
{
    while (a > PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

// Угол от взгляда бойца i на точку j
static f32 angle_to(const CombatWorld *w, u32 i, u32 j)
{
    return angle_wrap(atan2f(w->x[j] - w->x[i], w->z[j] - w->z[i]) - w->heading[i]);
}

// lead — сколько времени у защитника до удара: успевает ли довести возврат оружия и закрыться
static bool can_react(const CombatWorld *w, u32 j, u32 attacker, f32 lead)
{
    u8 s = w->state[j];
    bool free = s == FS_READY || s == FS_RETREAT || s == FS_BLOCK;
    if (s == FS_RECOVER && w->timer[j] < lead * g_tune.react_recover_frac) free = true;
    if (!free) return false;
    return fabsf(angle_to(w, j, attacker)) < g_tune.react_angle_deg * (PI_F / 180.0f);
}

// ---------------------------------------------------------------- навыки

static void gain(CombatWorld *w, u32 i, u32 s, f32 k, f32 opp)
{
    f32 own = w->skill[s][i];
    f32 m = clampf(1.0f + (opp - own) / g_tune.gain_opp_div, g_tune.gain_mul_min, g_tune.gain_mul_max);
    w->skill[s][i] = fminf(g_tune.skill_max, own + g_tune.gain_rate * k * m / (1.0f + own / g_tune.gain_own_div));
}

// ---------------------------------------------------------------- состояния тела

static void enter_ready(CombatWorld *w, u32 i)
{
    f32 r = combat_roll(w, i, 0, 11);
    f32 p = (g_tune.pause_min + g_tune.pause_rand * r) * (1.0f - sk(w, i, SK_ATTACK) / g_tune.pause_attack_div);
    if (w->morale[i] < g_tune.morale_shaken) p *= g_tune.pause_shaken_mul;
    w->pause[i] = p;
    combat_set_state(w, i, FS_READY, 0.0f);
}

// Длительности действий — из набора действий по классу оружия (data/defs/actionsets.txt)
static const ActionDef *act_of(const CombatWorld *w, u32 i, ActionId a) { return action_of(weapon_of(w, i)->wclass, a); }
static f32 act_time(const CombatWorld *w, u32 i, ActionId a, f32 roll)
{
    return action_time(act_of(w, i, a), roll, weapon_of(w, i)->weight, sk(w, i, SK_TOUGHNESS));
}

// Клип действия, с: длительность с темпом клипа
static f32 clip_s(const CombatWorld *w, u32 i, ActionId a) { return act_of(w, i, a)->time / act_of(w, i, a)->speed; }

// Какой клип всем телом идёт сейчас и сколько он длится: по состоянию, по тому, откуда пришёл, и по времени в нём.
// Стоя — смерть от удара, от потери крови или обморок; с колен — оседает дальше; лёжа — так и лежит
static void act_update(CombatWorld *w, u32 i)
{
    u8 st = w->state[i], fl = w->act_flags[i], c = CLIP_NONE;
    f32 t = w->act_t[i], t0 = 0.0f, d = 0.0f;
    ActionId a = ACT_COUNT;
    switch (st) {
    case FS_KNOCKED: a = w->fall[i] == FALL_FRONT ? ACT_FALL_FRONT : ACT_FALL_BACK; break;
    case FS_GETUP: a = w->prone[i] == LIE_FRONT ? ACT_GETUP_FRONT : ACT_GETUP_BACK; break;
    case FS_PICKUP: a = ACT_PICKUP; break;
    case FS_SURRENDER: a = ACT_KNEEL; break;
    case FS_DOWN: c = act_of(w, i, ACT_DOWN)->clip; break;
    case FS_UNCONSCIOUS:
    case FS_DEAD: {
        ActionId how = st == FS_UNCONSCIOUS ? ACT_COLLAPSE : w->blood[i] <= 0.0f ? ACT_BLEED : ACT_DIE;
        f32 col = clip_s(w, i, ACT_COLLAPSE);
        if ((fl & ACTF_KNEEL) && t < 0.6f * col) {
            c = act_of(w, i, ACT_COLLAPSE)->clip;
            t0 = 0.4f;
            d = col;
        } else if ((fl & ACTF_STAND) && t < clip_s(w, i, how)) {
            a = how;
        } else {
            c = CLIP_LIE;
        }
        break;
    }
    case FS_CRAWL:
        if ((fl & ACTF_STAND) && t < clip_s(w, i, ACT_SINK)) a = ACT_SINK;
        else if ((fl & ACTF_TURN) && t < clip_s(w, i, ACT_TURN)) a = ACT_TURN;
        else c = CLIP_CRAWL;
        break;
    default: break;
    }
    if (a != ACT_COUNT) {
        c = act_of(w, i, a)->clip;
        // Падение, подъём и подбор длятся, сколько состояние боя; остальное — сколько клип
        d = st == FS_KNOCKED || st == FS_GETUP || st == FS_PICKUP ? w->dur[i] / act_of(w, i, a)->speed : clip_s(w, i, a);
    }
    w->act[i] = c;
    w->act_t0[i] = t0;
    w->act_dur[i] = d;
    w->act_flags[i] = (u8)((fl & ~ACTF_WAS_FRONT) | (w->prone[i] == LIE_FRONT ? ACTF_WAS_FRONT : 0));
}

static bool legs_hold(const CombatWorld *w, u32 i)
{
    return part_ratio(w, i, PART_LEG_L) > 0.0f && part_ratio(w, i, PART_LEG_R) > 0.0f;
}

// Пора вставать: на перебитых ногах не встаёт вовсе, а сразу ползёт
static void begin_getup(CombatWorld *w, u32 i)
{
    if (!legs_hold(w, i)) {
        w->prone[i] = LIE_FRONT;
        combat_set_state(w, i, FS_CRAWL, 0.0f);
        return;
    }
    combat_set_state(w, i, FS_GETUP, act_time(w, i, w->prone[i] == LIE_FRONT ? ACT_GETUP_FRONT : ACT_GETUP_BACK, 0.0f));
    combat_event(w, EV_GETUP, i, i);
}

// Валится с ног: ничком — если толкнуло в спину или (без сознания) с броском; убитый ударом сбоку
// остаётся на боку. Уже лежащий лежит как лежал
static void go_down(CombatWorld *w, u32 i, u8 s, f32 dur, f32 front_p, u32 salt)
{
    if (!fs_lying(w->state[i])) {
        u8 f = w->fall[i];
        if (s == FS_DEAD && w->state[i] != FS_SURRENDER && (f == FALL_LEFT || f == FALL_RIGHT)) w->prone[i] = f == FALL_LEFT ? LIE_SIDE_L : LIE_SIDE_R;
        else w->prone[i] = f == FALL_FRONT || combat_roll(w, i, 0, salt) < front_p ? LIE_FRONT : LIE_BACK;
    }
    combat_set_state(w, i, s, dur);
}

// После подъёма или пробуждения: может ли снова драться
static void after_getup(CombatWorld *w, u32 i)
{
    if (w->disarmed[i]) {
        combat_set_state(w, i, FS_SURRENDER, 0.0f);
    } else if (!legs_hold(w, i)) {
        w->prone[i] = LIE_FRONT;
        combat_set_state(w, i, FS_CRAWL, 0.0f);
    } else if (w->loose[i]) {
        combat_set_state(w, i, FS_PICKUP, act_time(w, i, ACT_PICKUP, 0.0f));
    } else {
        enter_ready(w, i);
    }
}

static void check_body(CombatWorld *w, u32 i)
{
    if (w->state[i] == FS_DEAD) return;
    f32 tough = sk(w, i, SK_TOUGHNESS);
    bool ko = w->blood[i] < g_tune.blood_ko * w->blood_max[i];
    for (u32 p = 0; p <= PART_STOMACH; p++) {
        f32 mx = g_part_hp[p];
        if (w->hp[p][i] <= -mx) {
            go_down(w, i, FS_DEAD, 0.0f, 0.0f, 0);
            combat_event(w, EV_DEATH, i, i);
            return;
        }
        if (w->hp[p][i] <= -(tough / 100.0f * g_tune.ko_tough) * mx) ko = true;
    }
    if (w->blood[i] <= 0.0f) {
        // Истёк кровью: оседает и валится набок, не ничком
        if (w->state[i] == FS_SURRENDER) w->prone[i] = LIE_BACK;
        else if (!fs_lying(w->state[i])) w->prone[i] = combat_roll(w, i, 0, 37) < 0.5f ? LIE_SIDE_L : LIE_SIDE_R;
        combat_set_state(w, i, FS_DEAD, 0.0f);
        combat_event(w, EV_DEATH, i, i);
        return;
    }
    if (ko && w->state[i] != FS_UNCONSCIOUS) {
        go_down(w, i, FS_UNCONSCIOUS, 0.0f, 0.4f, 23);
        combat_event(w, EV_KO, i, i);
        return;
    }
    if (w->state[i] == FS_UNCONSCIOUS) return;
    if (!w->disarmed[i] && part_ratio(w, i, PART_ARM_R) <= 0.0f) {
        w->disarmed[i] = 1;
        combat_event(w, EV_DISARM, i, i);
        if (!fs_lying(w->state[i])) {
            combat_set_state(w, i, FS_SURRENDER, 0.0f);
            combat_event(w, EV_SURRENDER, i, i);
        }
    }
    if (!legs_hold(w, i) && !fs_lying(w->state[i])) {
        w->prone[i] = LIE_FRONT;
        combat_set_state(w, i, FS_CRAWL, 0.0f);
        combat_event(w, EV_FALL, i, i);
    }
}

// Урон по части тела; flags — EVF_* для журнала
static u8 apply_damage(CombatWorld *w, u32 t, u32 part, f32 cut, f32 blunt, f32 pierce)
{
    u8 flags = 0;
    f32 total = cut + blunt + pierce;
    f32 mx = g_part_hp[part];
    w->hp[part][t] -= total;
    w->bleed[t] += cut * g_tune.bleed_cut + pierce * g_tune.bleed_pierce + blunt * g_tune.bleed_blunt;
    f32 dshock = cut + pierce + g_tune.shock_blunt * blunt;
    w->shock[t] += dshock;
    w->morale[t] -= total * g_tune.morale_per_dmg;
    w->since_hit[t] = 0.0f;
    u8 bit = (u8)(1u << part);
    if (part != PART_CHEST && part != PART_STOMACH && w->hp[part][t] <= -g_tune.fracture_at * mx && !(w->fracture[t] & bit)) {
        w->fracture[t] |= bit;
        flags |= EVF_FRACTURE;
    }
    if (!body_part_vital((BodyPart)part) && w->hp[part][t] <= -mx) {
        w->hp[part][t] = -mx;
        if (cut > blunt && !(w->severed[t] & bit)) {
            w->severed[t] |= bit;
            w->bleed[t] += g_tune.sever_bleed;
            flags |= EVF_SEVER;
        }
    }
    // Падение и пошатывание решает общий разбор ударов тика, не этот вычет: иначе первый в списке
    // валит цель до удара того, кто идёт следом
    gain(w, t, SK_TOUGHNESS, g_tune.gain_tough * total / 10.0f * (w->state[t] == FS_UNCONSCIOUS ? g_tune.gain_tough_ko_mul : 1.0f), sk(w, t, SK_TOUGHNESS));
    return flags;
}

// ---------------------------------------------------------------- куда пришёлся удар
//
// Удар оставляет на цели след в плоскости взгляда атакующего (x — влево от него, y — вверх,
// метры позы привязки цели): размах — наклонная линия справа налево, удар сверху — почти отвесная
// сверху вниз, укол — точка. Попадает то, во что след входит первым (укол — ближнее к атакующему).
// Тело цели — капсулы стойки g_body_hit_guard, повёрнутые так, как цель стоит к атакующему

// Куда метит: середина и разброс по виду удара; ловкость сужает разброс
static void roll_aim(CombatWorld *w, u32 i, u32 t, u8 kind)
{
    f32 r1 = combat_roll(w, i, t, 20), r2 = combat_roll(w, i, t, 21), r3 = combat_roll(w, i, t, 22);
    f32 spread = 1.0f - sk(w, i, SK_DEXTERITY) / g_tune.aim_dex_div;
    f32 tri = (r1 + r2 - 1.0f) * spread;  // -1..1, к середине гуще
    switch (kind) {
    case ATK_SWING:
        // Наискось сверху в ключицу, поперёк живота или низом по ногам
        if (r3 < g_tune.aim_high_p) {
            w->aim_y[i] = g_tune.aim_high_y + g_tune.aim_high_dy * tri;
            w->aim_s[i] = -(g_tune.aim_high_s + g_tune.aim_high_ds * r2);
        } else if (r3 < g_tune.aim_mid_p) {
            w->aim_y[i] = g_tune.aim_mid_y + g_tune.aim_mid_dy * tri;
            w->aim_s[i] = -(g_tune.aim_mid_s + g_tune.aim_mid_ds * r1);
        } else {
            w->aim_y[i] = g_tune.aim_low_y + g_tune.aim_low_dy * r2;
            w->aim_s[i] = -g_tune.aim_low_s;
        }
        w->aim_x[i] = 0.0f;
        break;
    case ATK_OVERHEAD:
        // В голову или в плечо справа от себя, клинок идёт чуть справа налево; точка — на высоте плеч
        w->aim_x[i] = -g_tune.aim_over_x + g_tune.aim_over_dx * tri;
        w->aim_y[i] = g_tune.aim_over_y;
        w->aim_s[i] = g_tune.aim_over_s + g_tune.aim_over_ds * r3;
        break;
    default:
        w->aim_x[i] = g_tune.aim_thrust_x * (r3 - 0.5f) * 2.0f * spread;
        // Укол в корпус, изредка в лицо
        w->aim_y[i] = combat_roll(w, i, t, 23) < g_tune.aim_face_p ? g_tune.aim_face_y : g_tune.aim_thrust_y + g_tune.aim_thrust_dy * tri;
        w->aim_s[i] = 0.0f;
        break;
    }
}

// С какого места по следу (o + u·d, d единичный) след входит в капсулу a-b радиуса r; 1e9 — мимо
static f32 trace_entry(f32 ox, f32 oy, f32 dx, f32 dy, f32 ax, f32 ay, f32 bx, f32 by, f32 r)
{
    // В координатах следа: u вдоль, v поперёк
    f32 ua = (ax - ox) * dx + (ay - oy) * dy, va = -(ax - ox) * dy + (ay - oy) * dx;
    f32 ub = (bx - ox) * dx + (by - oy) * dy, vb = -(bx - ox) * dy + (by - oy) * dx;
    f32 best = 1e9f;
    if (fabsf(va) <= r) best = fminf(best, ua - sqrtf(r * r - va * va));
    if (fabsf(vb) <= r) best = fminf(best, ub - sqrtf(r * r - vb * vb));
    // Боковые стороны капсулы
    f32 du = ub - ua, dv = vb - va, l = sqrtf(du * du + dv * dv);
    if (l > 1e-6f && fabsf(dv) > 1e-6f) {
        f32 nu = -dv / l * r, nv = du / l * r;
        for (f32 sgn = -1.0f; sgn <= 1.0f; sgn += 2.0f) {
            f32 k = -(va + sgn * nv) / dv;
            if (k >= 0.0f && k <= 1.0f) best = fminf(best, ua + k * du + sgn * nu);
        }
    }
    return best;
}

static u32 choose_part(const CombatWorld *w, u32 att, u32 t, u8 kind, u32 salt)
{
    if (fs_lying(w->state[t])) {
        // Лежачего бьют куда придётся
        u32 n = 0, opts[PART_COUNT];
        for (u32 p = 0; p < PART_COUNT; p++)
            if (!(w->severed[t] & (1u << p))) opts[n++] = p;
        u32 k = (u32)(combat_roll(w, att, t, salt) * (f32)n);
        return n ? opts[k < n ? k : n - 1] : PART_CHEST;
    }
    // Цель в осях атакующего: δ — разворот цели относительно линии удара
    f32 dir = atan2f(w->x[t] - w->x[att], w->z[t] - w->z[att]);
    f32 sd = sinf(w->heading[t] - dir), cd = cosf(w->heading[t] - dir);
    f32 ox = w->aim_x[att], oy = w->aim_y[att], s = w->aim_s[att];
    f32 dx, dy;
    if (kind == ATK_SWING) {
        dx = 1.0f;
        dy = s;
    } else {
        dx = s;
        dy = -1.0f;
    }
    f32 dl = sqrtf(dx * dx + dy * dy);
    dx /= dl;
    dy /= dl;
    u32 best = PART_COUNT, near = PART_CHEST;
    f32 best_u = 1e9f, near_d = 1e9f;
    for (u32 c = 0; c < g_body.hits; c++) {
        const HitCapsule *h = &g_body_hit_guard[c];
        if (w->severed[t] & (1u << h->part)) continue;
        f32 ax = h->a.x * cd + h->a.z * sd, az = -h->a.x * sd + h->a.z * cd;
        f32 bx = h->b.x * cd + h->b.z * sd, bz = -h->b.x * sd + h->b.z * cd;
        f32 ay = h->a.y, by = h->b.y;
        f32 u;
        if (kind == ATK_THRUST) {
            // Укол: точка внутри капсулы; ближе к атакующему — та, что спереди
            f32 ex = bx - ax, ey = by - ay, ee = ex * ex + ey * ey;
            f32 k = ee > 1e-9f ? clampf(((ox - ax) * ex + (oy - ay) * ey) / ee, 0.0f, 1.0f) : 0.0f;
            f32 px = ax + k * ex - ox, py = ay + k * ey - oy, d2 = px * px + py * py;
            f32 d = sqrtf(d2) - h->r;
            if (d < near_d) {
                near_d = d;
                near = h->part;
            }
            u = d2 <= h->r * h->r ? az + k * (bz - az) - sqrtf(h->r * h->r - d2) : 1e9f;
        } else {
            u = trace_entry(ox, oy, dx, dy, ax, ay, bx, by, h->r);
            // Промах мимо всех: ближайшая к следу
            f32 va = fabsf(-(ax - ox) * dy + (ay - oy) * dx), vb = fabsf(-(bx - ox) * dy + (by - oy) * dx);
            f32 d = fminf(va, vb) - h->r;
            if (d < near_d) {
                near_d = d;
                near = h->part;
            }
        }
        if (u < best_u) {
            best_u = u;
            best = h->part;
        }
    }
    return best < PART_COUNT ? best : near;
}

// ---------------------------------------------------------------- удар

static f32 block_chance(const CombatWorld *w, u32 def, u32 att, u8 kind)
{
    f32 c = g_tune.block_base + (defense_eff(w, def) - attack_eff(w, att)) / g_tune.block_skill_div;
    if (kind == ATK_SWING) c -= g_tune.block_swing_pen;
    // Древковое вплотную неудобно
    if (weapon_of(w, def)->wclass == WC_POLEARM) {
        f32 dx = w->x[att] - w->x[def], dz = w->z[att] - w->z[def];
        if (dx * dx + dz * dz < g_tune.polearm_close_d2) c -= g_tune.polearm_close_pen;
    }
    return clampf(c, g_tune.block_min, g_tune.block_max);
}

static f32 dodge_chance(const CombatWorld *w, u32 def, u32 att, u8 kind)
{
    f32 c = g_tune.dodge_base + (dodge_eff(w, def) - attack_eff(w, att)) / g_tune.dodge_skill_div;
    if (kind == ATK_SWING) c += g_tune.dodge_swing_bonus;
    return clampf(c, 0.0f, g_tune.dodge_max);
}

// Бросок реакции защитника; опыт за попытку уклониться
static u8 roll_react(CombatWorld *w, u32 def, u32 att, u8 kind, f32 lead)
{
    if (!can_react(w, def, att, lead)) return REACT_NONE;
    if (combat_roll(w, def, att, 31) < block_chance(w, def, att, kind)) return REACT_BLOCK;
    gain(w, def, SK_DODGE, g_tune.gain_dodge_try, attack_eff(w, att));
    if (combat_roll(w, def, att, 32) < dodge_chance(w, def, att, kind)) return REACT_DODGE;
    return REACT_NONE;
}


static f32 windup_time(const CombatWorld *w, u32 i, u8 kind)
{
    const WeaponDef *wd = weapon_of(w, i);
    f32 t = g_tune.windup_base / wd->speed * g_attacks[kind].windup_mul;
    t *= (1.0f - sk(w, i, SK_DEXTERITY) / g_tune.windup_dex_div) * (1.0f - sk(w, i, SK_ATTACK) / g_tune.windup_attack_div);
    t *= (1.0f + encumbrance(w, i)) * (1.0f + g_tune.windup_overweight * weapon_overweight(w, i));
    return t;
}

void combat_begin_attack(CombatWorld *w, u32 i, u8 kind, u32 t, bool counter)
{
    f32 wt = windup_time(w, i, kind) * (counter ? g_tune.counter_windup : 1.0f);
    combat_set_state(w, i, FS_WINDUP, wt);
    w->attack[i] = kind;
    w->target[i] = (i32)t;
    roll_aim(w, i, t, kind);
    combat_event(w, counter ? EV_COUNTER : EV_ATTACK, i, t);
    w->ev[w->last_ev].arg = kind;
    // Реакция по навыкам до опыта за этот замах: опыт пишется пачкой, после всех замахов прохода
    w->react[i] = roll_react(w, t, i, kind, wt);
}

void combat_swing_xp(CombatWorld *w, u32 i)
{
    i32 t = w->target[i];
    if (t < 0 || (u32)t >= w->count) return;
    gain(w, i, SK_ATTACK, g_tune.gain_attack_try, defense_eff(w, (u32)t));
    gain(w, i, SK_WEAPON + weapon_of(w, i)->wclass, g_tune.gain_weapon_try, defense_eff(w, (u32)t));
    if (weapon_overweight(w, i) > 0.0f) gain(w, i, SK_STRENGTH, g_tune.gain_strength, g_tune.gain_strength_opp);
    if (weapon_of(w, i)->weight <= g_tune.dex_weapon_kg) gain(w, i, SK_DEXTERITY, g_tune.gain_dex, sk(w, (u32)t, SK_DEXTERITY));
}

// Замах этого прохода: таймер ещё равен полной длительности, тик его не уменьшал
static bool fresh_windup(const CombatWorld *w, u32 i)
{
    return w->state[i] == FS_WINDUP && w->dur[i] > 0.0f && w->timer[i] == w->dur[i];
}

// Стойка защитника — один раз на всех замахов прохода. Блок важнее уворота, держится дольше
// из двух одинаковых. Кто сам замахнулся в этом проходе, стойку не получает: оба бьют.
void combat_seal_guards(CombatWorld *w)
{
    u32 n = w->count;
    memset(w->guard_kind, 0, n);
    for (u32 i = 0; i < n; i++) {
        if (w->idle[i] || !fresh_windup(w, i)) continue;
        i32 ti = w->target[i];
        if (ti < 0 || (u32)ti >= n) continue;
        u32 t = (u32)ti;
        if (fresh_windup(w, t)) continue;
        u8 r = w->react[i];
        if (r != REACT_BLOCK && r != REACT_DODGE) continue;
        f32 extra = act_of(w, t, r == REACT_BLOCK ? ACT_BLOCK : ACT_DODGE)->time;
        f32 hold = w->timer[i] + extra;
        u8 g = w->guard_kind[t];
        if (g == REACT_BLOCK && r != REACT_BLOCK) continue;
        if (g != r) {
            w->guard_kind[t] = r;
            w->guard_hold[t] = hold;
        } else if (hold > w->guard_hold[t]) {
            w->guard_hold[t] = hold;
        }
    }
    for (u32 t = 0; t < n; t++) {
        u8 r = w->guard_kind[t];
        if (!r || w->idle[t]) continue;
        u8 s = w->state[t];
        if (s != FS_READY && s != FS_RETREAT && s != FS_BLOCK && s != FS_DODGE && s != FS_RECOVER) continue;
        f32 hold = w->guard_hold[t];
        if (r == REACT_BLOCK) {
            if (s != FS_BLOCK || w->timer[t] < hold) combat_set_state(w, t, FS_BLOCK, hold);
        } else {
            combat_set_state(w, t, FS_DODGE, hold);
        }
    }
}

static u8 best_counter_kind(const CombatWorld *w, u32 i)
{
    u8 mask = weapon_of(w, i)->attacks;
    if (mask & (1u << ATK_THRUST)) return ATK_THRUST;
    if (mask & (1u << ATK_OVERHEAD)) return ATK_OVERHEAD;
    return ATK_SWING;
}

// Что удар сделает с целью. Считается по состоянию до всех ударов этого тика.
enum { OC_BLOCK = 1, OC_DODGE = 2, OC_HIT = 3, OC_BREAK = 4 };

struct CombatHit {
    u32 att, def;
    u8  oc, part, kind, fall;
    u8  ally, drop, counter, counter_kind;
    u8  shock_knock, blunt_stagger;
    f32 cut, blunt, pierce, arm_blunt;
    f32 push, pdx, pdz;
    f32 knock_dur, stagger_dur;
};

static void plan_strike(CombatWorld *w, u32 i, u32 *nh)
{
    const WeaponDef *wd = weapon_of(w, i);
    u8 kind = w->attack[i];
    u32 main_t = (u32)w->target[i];
    f32 reach = combat_reach(w, i, kind) + g_tune.strike_reach_add;
    f32 half_arc = 0.5f * combat_arc(w, i, kind);
    u32 limit = kind == ATK_SWING ? wd->max_targets : 1;

    // Цели в дуге: основная первой, дальше по углу, при равенстве по номеру
    u32 cand[MAX_STRIKE_TARGETS];
    f32 cang[MAX_STRIKE_TARGETS];
    u32 nc = 0;
    u32 near[COMBAT_NEAR_MAX];
    u32 nn = combat_near(w, w->x[i], w->z[i], reach + GRID_SLACK, TAGS_ALIVE, near);
    for (u32 q = 0; q < nn; q++) {
        u32 j = near[q];
        if (j == i || w->state[j] == FS_DEAD) continue;
        if (kind == ATK_SWING && fs_lying(w->state[j])) continue;
        f32 dx = w->x[j] - w->x[i], dz = w->z[j] - w->z[i];
        if (dx * dx + dz * dz > reach * reach) continue;
        f32 a = fabsf(angle_to(w, i, j));
        if (a > half_arc) continue;
        if (j == main_t) a = -1.0f;
        u32 k;
        if (nc < MAX_STRIKE_TARGETS) {
            k = nc++;
        } else {
            k = MAX_STRIKE_TARGETS - 1;
            if (a > cang[k] || (a == cang[k] && j > cand[k])) continue;  // хуже худшего из взятых
        }
        while (k > 0 && (cang[k - 1] > a || (cang[k - 1] == a && cand[k - 1] > j))) {
            cand[k] = cand[k - 1];
            cang[k] = cang[k - 1];
            k--;
        }
        cand[k] = j;
        cang[k] = a;
    }
    if (nc > limit) nc = limit;

    f32 falloff = 1.0f;
    for (u32 n = 0; n < nc; n++, falloff *= g_tune.cleave_falloff) {
        if (*nh >= w->hit_cap) return;
        CombatHit *h = &w->hits[(*nh)++];
        memset(h, 0, sizeof *h);
        u32 t = cand[n];
        h->att = i;
        h->def = t;
        h->kind = kind;
        h->ally = w->team[t] == w->team[i];
        u8 r;
        if (fs_lying(w->state[t]) || fs_out(w->state[t])) {
            r = REACT_NONE;
        } else if (t == main_t) {
            r = w->react[i];
            if (r == REACT_BLOCK && w->state[t] != FS_BLOCK) r = REACT_NONE;
            if (r == REACT_DODGE && w->state[t] != FS_DODGE) r = REACT_NONE;
        } else {
            r = roll_react(w, t, i, kind, 0.0f);
        }

        f32 ha = angle_to(w, t, i);
        f32 ah = fabsf(ha);
        h->fall = ah < 0.9f ? FALL_BACK : ah > 2.25f ? FALL_FRONT : ha > 0.0f ? FALL_RIGHT : FALL_LEFT;
        bool standing = !fs_lying(w->state[t]) && !fs_out(w->state[t]);

        if (r == REACT_BLOCK) {
            h->oc = OC_BLOCK;
            f32 fa = wd->weight * (1.0f + sk(w, i, SK_STRENGTH) / 100.0f);
            f32 fd = weapon_of(w, t)->weight * (1.0f + sk(w, t, SK_STRENGTH) / 100.0f);
            if (fa > g_tune.guard_break * fd) {
                h->oc = OC_BREAK;
                h->arm_blunt = wd->blunt * g_tune.guard_break_arm * g_quality_mul[w->weapon_q[i]];
                h->stagger_dur = act_time(w, t, ACT_STAGGER, combat_roll(w, t, i, 41));
            } else {
                f32 cc = fminf(g_tune.counter_max, (defense_eff(w, t) - attack_eff(w, i)) / 100.0f);
                if (!h->ally && combat_roll(w, t, i, 42) < cc) {
                    h->counter = 1;
                    h->counter_kind = best_counter_kind(w, t);
                }
            }
            continue;
        }
        if (r == REACT_DODGE) {
            h->oc = OC_DODGE;
            continue;
        }

        h->oc = OC_HIT;
        h->part = (u8)choose_part(w, i, t, kind, 50 + n);
        f32 m = g_quality_mul[w->weapon_q[i]] * (g_tune.dmg_str_base + sk(w, i, SK_STRENGTH) / g_tune.dmg_str_div) *
                (g_tune.dmg_rand_min + g_tune.dmg_rand * combat_roll(w, i, t, 51 + n)) * g_attacks[kind].dmg_mul * falloff;
        if (kind == ATK_THRUST) {
            h->pierce = wd->pierce * m;
        } else {
            h->cut = wd->cut * m;
            h->blunt = wd->blunt * m;
        }
        for (u32 l = 0; l < ARRAY_COUNT(g_armor_layer_order); l++) {
            u8 a = w->armor[g_armor_layer_order[l]][t];
            if (a == ARMOR_NONE) continue;
            const ArmorDef *ad = &g_armors[a];
            f32 cover = ad->cover[h->part] * (kind == ATK_THRUST ? g_tune.thrust_cover : 1.0f);
            if (cover <= 0.0f || combat_roll(w, t, i, 60 + l) >= cover) continue;
            h->cut *= 1.0f - ad->res_cut;
            h->blunt *= 1.0f - ad->res_blunt;
            h->pierce *= 1.0f - ad->res_pierce * (1.0f - wd->pen);
        }
        f32 tough = 1.0f - sk(w, t, SK_TOUGHNESS) / g_tune.dmg_tough_div;
        h->cut *= tough;
        h->blunt *= tough;
        h->pierce *= tough;

        if (standing) {
            f32 dshock = h->cut + h->pierce + g_tune.shock_blunt * h->blunt;
            f32 tough100 = 1.0f + sk(w, t, SK_TOUGHNESS) / 100.0f;
            f32 force = wd->knock * wd->weight * (1.0f + sk(w, i, SK_STRENGTH) / 100.0f) * falloff;
            f32 resist = w->mass[t] * tough100 * g_tune.knock_resist;
            if (dshock > g_tune.knock_shock * tough100) {
                h->shock_knock = 1;
                h->knock_dur = act_time(w, t, h->fall == FALL_FRONT ? ACT_FALL_FRONT : ACT_FALL_BACK, 0.0f);
            } else if (h->blunt > g_tune.stagger_blunt * tough100) {
                h->blunt_stagger = 1;
                h->stagger_dur = act_time(w, t, ACT_STAGGER, combat_roll(w, t, 0, 21));
            }
            if (force > resist) {
                f32 dx = w->x[t] - w->x[i], dz = w->z[t] - w->z[i];
                f32 len = sqrtf(dx * dx + dz * dz) + 1e-4f;
                h->push = g_tune.knock_push_min + combat_roll(w, i, t, 70);
                h->pdx = dx / len;
                h->pdz = dz / len;
                if (!h->shock_knock)
                    h->knock_dur = act_time(w, t, h->fall == FALL_FRONT ? ACT_FALL_FRONT : ACT_FALL_BACK, 0.0f);
                f32 p_drop = clampf(g_tune.drop_base + g_tune.drop_k * (force / resist - 1.0f), g_tune.drop_base, g_tune.drop_max) *
                             (1.0f - sk(w, t, SK_STRENGTH) / g_tune.drop_str_div);
                if (!w->disarmed[t] && combat_roll(w, t, i, 71) < p_drop) h->drop = 1;
            }
        }
    }
}

// Опыт, раны и журнал — по уже посчитанному плану. Падение решается по всем ударам цели сразу.
static void apply_hits(CombatWorld *w, u32 nh)
{
    memset(w->guard_kind, 0, w->count);
    for (u32 n = 0; n < nh; n++) {
        CombatHit *h = &w->hits[n];
        u32 i = h->att, t = h->def;
        w->guard_kind[t] = 1;
        if (h->oc == OC_BLOCK || h->oc == OC_BREAK) {
            combat_event(w, EV_BLOCK, i, t);
            if (h->oc == OC_BREAK) w->ev[w->last_ev].flags |= EVF_STAGGER;
            gain(w, t, SK_DEFENSE, g_tune.gain_block, attack_eff(w, i));
            gain(w, t, SK_WEAPON + weapon_of(w, t)->wclass, g_tune.gain_block_weapon, attack_eff(w, i));
            if (h->oc == OC_BREAK) apply_damage(w, t, PART_ARM_R, 0, h->arm_blunt, 0);
            continue;
        }
        if (h->oc == OC_DODGE) {
            combat_event(w, EV_DODGE, i, t);
            gain(w, t, SK_DODGE, g_tune.gain_dodge, attack_eff(w, i));
            continue;
        }
        gain(w, i, SK_ATTACK, g_tune.gain_hit, defense_eff(w, t));
        gain(w, t, SK_DEFENSE, g_tune.gain_hit_defense, attack_eff(w, i));
        combat_event(w, EV_HIT, i, t);
        u32 hit_ev = w->last_ev;
        u8 flags = apply_damage(w, t, h->part, h->cut, h->blunt, h->pierce);
        CombatEvent *e = &w->ev[hit_ev];
        e->part = h->part;
        e->flags = flags | (h->ally ? EVF_ALLY : 0);
        e->arg = h->kind;
        e->cut = h->cut;
        e->blunt = h->blunt;
        e->pierce = h->pierce;
    }

    for (u32 t = 0; t < w->count; t++) {
        if (!w->guard_kind[t]) continue;
        bool shock = false, force = false, drop = false, stagger = false;
        f32 push_x = 0, push_z = 0, stag_dur = 0, knock_dur = 0, best_shock = -1, best_push = -1;
        u8 fall = FALL_BACK;
        u32 force_att = t;
        for (u32 n = 0; n < nh; n++) {
            CombatHit *h = &w->hits[n];
            if (h->def != t) continue;
            if (h->blunt_stagger || h->oc == OC_BREAK) {
                stagger = true;
                if (h->stagger_dur > stag_dur) stag_dur = h->stagger_dur;
            }
            if (h->shock_knock) {
                f32 dshock = h->cut + h->pierce + g_tune.shock_blunt * h->blunt;
                if (!shock || dshock > best_shock) {
                    shock = true;
                    best_shock = dshock;
                    fall = h->fall;
                    knock_dur = h->knock_dur;
                }
            }
            if (h->push > 0.0f) {
                if (!force || h->push > best_push) {
                    force = true;
                    best_push = h->push;
                    if (!shock) {
                        fall = h->fall;
                        knock_dur = h->knock_dur;
                    }
                    force_att = h->att;
                }
                if (h->drop) drop = true;
                push_x += h->pdx * h->push;
                push_z += h->pdz * h->push;
            }
        }
        // Шок кладёт сам, без толчка. Толчок — только если ни один удар не уронил шоком.
        // Куда валиться — до check_body: смерть от ран берёт это направление.
        if (shock || force) w->fall[t] = fall;
        if (force && !shock) {
            w->x[t] += push_x;
            w->z[t] += push_z;
        }
        check_body(w, t);
        u8 s = w->state[t];
        if (s == FS_DEAD || s == FS_UNCONSCIOUS || fs_lying(s) || fs_out(s)) continue;
        if (shock || force) {
            go_down(w, t, FS_KNOCKED, knock_dur, 0.0f, 0);
            if (shock) combat_event(w, EV_FALL, t, t);
            if (force && !shock) {
                if (drop && !w->disarmed[t]) w->loose[t] = 1;
                combat_event(w, EV_KNOCKDOWN, force_att, t);
            }
            continue;
        }
        if (stagger) combat_set_state(w, t, FS_STAGGER, stag_dur);
    }

    for (u32 i = 0; i < w->count; i++) w->strikes[i] = ~0u;
    memset(w->guard_kind, 0, w->count);
    for (u32 n = 0; n < nh; n++) {
        CombatHit *h = &w->hits[n];
        if (!h->counter || w->state[h->def] != FS_BLOCK) continue;
        u32 t = h->def;
        if (w->strikes[t] == ~0u || (u32)w->target[t] == h->att) {
            w->strikes[t] = h->att;
            w->guard_kind[t] = h->counter_kind;
        }
    }
    for (u32 t = 0; t < w->count; t++) {
        if (w->strikes[t] == ~0u || w->state[t] != FS_BLOCK) continue;
        combat_begin_attack(w, t, w->guard_kind[t], w->strikes[t], true);
    }
    for (u32 t = 0; t < w->count; t++)
        if (fresh_windup(w, t)) combat_swing_xp(w, t);
    combat_seal_guards(w);
}

// ---------------------------------------------------------------- тик

static void tick_body(CombatWorld *w, u32 i)
{
    const f32 dt = COMBAT_DT;
    u8 s = w->state[i];
    if (s == FS_DEAD) return;
    f32 rate = w->bleed[i] * (w->hp[PART_STOMACH][i] <= 0.0f ? g_tune.bleed_gut_mul : 1.0f);
    w->blood[i] -= rate * dt;
    w->bleed[i] = fmaxf(0.0f, w->bleed[i] - g_tune.bleed_stop * dt);
    w->shock[i] = fmaxf(0.0f, w->shock[i] - (g_tune.shock_recover + sk(w, i, SK_TOUGHNESS) * g_tune.shock_recover_tough) * dt);
    w->since_hit[i] += dt;
    if (w->since_hit[i] > g_tune.morale_calm_s && w->morale[i] < w->morale_base[i]) w->morale[i] += g_tune.morale_recover * dt;

    if (s == FS_UNCONSCIOUS) {
        // Лёжа без сознания жизненные части понемногу приходят в себя
        bool vitals_ok = true;
        for (u32 p = 0; p <= PART_STOMACH; p++) {
            w->hp[p][i] = fminf(w->hp[p][i] + g_tune.ko_heal * dt, g_part_hp[p]);
            if (w->hp[p][i] <= 0.0f) vitals_ok = false;
        }
        w->blood[i] = fminf(w->blood[i] + g_tune.ko_blood * dt, w->blood_max[i]);
        if (vitals_ok && w->blood[i] > g_tune.wake_blood * w->blood_max[i]) {
            begin_getup(w, i);
        }
    }
    check_body(w, i);
}

static void tick_state(CombatWorld *w, u32 i, u32 *strikes, u32 *ns)
{
    u8 s = w->state[i];
    if (s == FS_READY) {
        w->pause[i] -= COMBAT_DT;
        return;
    }
    if (w->dur[i] <= 0.0f) return;  // бессрочные состояния
    w->timer[i] -= COMBAT_DT;
    if (w->timer[i] > 0.0f) return;
    switch (s) {
    case FS_WINDUP:
        combat_set_state(w, i, FS_STRIKE, act_time(w, i, ACT_STRIKE, 0.0f));
        strikes[(*ns)++] = i;
        break;
    case FS_STRIKE:
        combat_set_state(w, i, FS_RECOVER, act_time(w, i, ACT_RECOVER, 0.0f));
        break;
    case FS_KNOCKED: {
        combat_set_state(w, i, FS_DOWN, act_time(w, i, ACT_DOWN, combat_roll(w, i, 0, 81)));
        break;
    }
    case FS_DOWN:
        begin_getup(w, i);
        break;
    case FS_GETUP:
        after_getup(w, i);
        break;
    case FS_PICKUP:
        w->loose[i] = 0;
        enter_ready(w, i);
        break;
    default:
        enter_ready(w, i);
        break;
    }
}

// Поворот по местам до шага: все смотрят туда, где цели стояли в начале шага
static void tick_face(CombatWorld *w, u32 i)
{
    u8 s = w->state[i];
    f32 turn = g_tune.turn;
    switch (s) {
    case FS_CRAWL:  turn = g_tune.turn_crawl; break;
    case FS_WINDUP: turn = g_tune.turn_windup; break;
    case FS_READY: case FS_RETREAT: case FS_BLOCK: case FS_DODGE: break;
    default: turn = 0.0f; break;
    }
    if (turn > 0.0f && w->target[i] >= 0 && s != FS_RETREAT && s != FS_CRAWL) {
        u32 t = (u32)w->target[i];
        f32 d = angle_to(w, i, t);
        f32 step = turn * COMBAT_DT;
        w->heading[i] = angle_wrap(w->heading[i] + clampf(d, -step, step));
    }
    if (s == FS_RETREAT && w->mvx[i] * w->mvx[i] + w->mvz[i] * w->mvz[i] > 0.01f) {
        f32 want = atan2f(w->mvx[i], w->mvz[i]);
        f32 step = g_tune.turn_retreat * COMBAT_DT;
        w->heading[i] = angle_wrap(w->heading[i] + clampf(angle_wrap(want - w->heading[i]), -step, step));
    }
}

static void tick_step(CombatWorld *w, u32 i)
{
    u8 s = w->state[i];
    f32 speed = 0.0f;
    switch (s) {
    case FS_READY:   speed = g_tune.speed_ready; break;
    case FS_RETREAT: speed = g_tune.speed_retreat; break;
    case FS_BLOCK:   speed = g_tune.speed_block; break;
    case FS_DODGE:   speed = g_tune.speed_dodge; break;
    case FS_CRAWL:   speed = g_tune.speed_crawl; break;
    default: break;
    }
    if (speed <= 0.0f) return;
    f32 legs = 0.5f * (part_ratio(w, i, PART_LEG_L) + part_ratio(w, i, PART_LEG_R));
    if (s != FS_CRAWL) speed *= (g_tune.move_legs_min + (1.0f - g_tune.move_legs_min) * legs);
    speed *= (1.0f - encumbrance(w, i)) * (1.0f + sk(w, i, SK_ATHLETICS) / g_tune.athletics_div);
    w->x[i] += w->mvx[i] * speed * COMBAT_DT;
    w->z[i] += w->mvz[i] * speed * COMBAT_DT;
}

// Расталкивание тел: каждый считает свой сдвиг от соседей по позициям начала шага,
// поэтому куски можно считать в любом порядке и в любом числе потоков
static bool pinned(const CombatWorld *w, u32 i)
{
    return w->idle[i] || w->state[i] == FS_DEAD || w->state[i] == FS_UNCONSCIOUS;
}

// Тело на земле: отрезок вдоль взгляда от ног до головы. На спину падают назад, ничком — вперёд
#define LIE_FEET 0.15f   // ноги от точки тела, м
#define LIE_HEAD 1.35f   // голова от точки тела, м
#define LIE_FEET_SIDE 0.6f // на боку ноги дальше: тело сдвинуто в сторону падения
#define LIE_R    0.42f   // стоящий держится от оси лежащего не ближе
#define LIE_STEP 0.06f   // за тик отходит от лежащего не больше (1.2 м/с)

static bool on_ground(const CombatWorld *w, u32 i)
{
    u8 s = w->state[i];
    return !w->idle[i] && (s == FS_DOWN || s == FS_UNCONSCIOUS || s == FS_DEAD || s == FS_CRAWL);
}

static void separate_job(void *ctx, u32 b, u32 e)
{
    CombatWorld *w = ctx;
    const f32 r2 = 0.6f;
    u32 near[COMBAT_NEAR_MAX];
    for (u32 i = b; i < e; i++) {
        f32 sx = 0.0f, sz = 0.0f;
        if (!pinned(w, i)) {
            bool down = on_ground(w, i) || fs_lying(w->state[i]);
            u32 nn = combat_near(w, w->x[i], w->z[i], r2, GRID_ALL_TAGS, near);
            for (u32 q = 0; q < nn; q++) {
                u32 j = near[q];
                if (j == i) continue;
                // Лежащего стоящие не двигают: стоящий обходит его по всему телу, ниже
                if (on_ground(w, j) && !down) continue;
                if (down && !fs_lying(w->state[j])) continue;
                f32 dx = w->x[i] - w->x[j], dz = w->z[i] - w->z[j];
                f32 d2 = dx * dx + dz * dz;
                if (d2 >= r2 * r2) continue;
                f32 d = sqrtf(d2);
                // Совпали точно: разводим по номеру
                if (d < 1e-4f) { dx = i < j ? -1.0f : 1.0f; dz = 0.0f; d = 1.0f; d2 = 0.0f; }
                // Мёртвые и лежащие без сознания не сдвигаются, живой отходит за двоих
                f32 k = pinned(w, j) ? 2.0f : 1.0f;
                f32 push = (r2 - sqrtf(d2)) * 0.5f * k;
                sx += dx / d * push;
                sz += dz / d * push;
            }
        }
        w->sep_x[i] = sx;
        w->sep_z[i] = sz;
    }
}

// Стоящие обходят тела на земле, а не встают на них. Тел меньше, чем стоящих, поэтому ищут от тела;
// в одном потоке по возрастанию номера тела: сдвиги складываются в одном порядке
static void around_bodies(CombatWorld *w)
{
    u32 near[COMBAT_NEAR_MAX];
    const f32 len = LIE_FEET + LIE_HEAD, reach = 0.5f * len + LIE_R;
    for (u32 j = 0; j < w->count; j++) {
        if (!on_ground(w, j)) continue;
        // Ось тела от ног к голове: на спине голова сзади, ничком — впереди, на боку — поперёк, в сторону падения
        f32 sh = sinf(w->heading[j]), ch = cosf(w->heading[j]), ux, uz, feet = LIE_FEET;
        switch (w->prone[j]) {
        case LIE_FRONT: ux = sh, uz = ch; break;
        case LIE_SIDE_L: ux = ch, uz = -sh, feet = LIE_FEET_SIDE; break;
        case LIE_SIDE_R: ux = -ch, uz = sh, feet = LIE_FEET_SIDE; break;
        default: ux = -sh, uz = -ch; break;
        }
        f32 ax = w->x[j] - ux * feet, az = w->z[j] - uz * feet;
        f32 mx = ax + ux * 0.5f * len, mz = az + uz * 0.5f * len;
        u32 nn = combat_near(w, mx, mz, reach, TAGS_FIGHTING, near);
        for (u32 q = 0; q < nn; q++) {
            u32 i = near[q];
            f32 ox = w->x[i] - mx, oz = w->z[i] - mz;
            if (ox * ox + oz * oz >= reach * reach || i == j || pinned(w, i) || fs_lying(w->state[i])) continue;
            f32 t = clampf((w->x[i] - ax) * ux + (w->z[i] - az) * uz, 0.0f, len);
            f32 dx = w->x[i] - (ax + ux * t), dz = w->z[i] - (az + uz * t);
            f32 d = sqrtf(dx * dx + dz * dz);
            if (d >= LIE_R) continue;
            f32 push = fminf(LIE_R - d, LIE_STEP);
            // Стоит прямо на оси: в сторону по номеру
            if (d < 1e-4f) { dx = i < j ? -uz : uz; dz = i < j ? ux : -ux; d = 1.0f; }
            w->sep_x[i] += dx / d * push;
            w->sep_z[i] += dz / d * push;
        }
    }
}

static void separate_apply_job(void *ctx, u32 b, u32 e)
{
    CombatWorld *w = ctx;
    f32 lim = w->fence_r - 0.3f;
    for (u32 i = b; i < e; i++) {
        w->x[i] += w->sep_x[i];
        w->z[i] += w->sep_z[i];
        if (w->fence_r <= 0.0f) continue;
        f32 d = sqrtf(w->x[i] * w->x[i] + w->z[i] * w->z[i]);
        if (d > lim) {
            w->x[i] *= lim / d;
            w->z[i] *= lim / d;
        }
    }
}

static void build_grid(CombatWorld *w)
{
    u32 ntags = 3;
    for (u32 i = 0; i < w->count; i++) {
        u8 s = w->state[i];
        // Спящий слот для боя как мёртвый: его не ищут и не бьют, только расталкиваются об него
        w->grid_tag[i] = s == FS_DEAD || w->idle[i] ? TAG_DEAD : (fs_out(s) ? TAG_OUT : (u8)(2u + w->team[i]));
        if (w->grid_tag[i] + 1u > ntags) ntags = w->grid_tag[i] + 1u;
    }
    grid_build(&w->grid, w->x, w->z, w->grid_tag, ntags, w->count);
    w->grid_dirty = false;
}

void combat_sync_grid(CombatWorld *w)
{
    if (w->grid_dirty) build_grid(w);
}

bool combat_any_near(const CombatWorld *w, f32 x, f32 z, f32 r, u32 mask)
{
    return (grid_tags(&w->grid, x, z, r) & mask) != 0;
}

void combat_count(const CombatWorld *w, f32 x, f32 z, f32 r, u32 mask, u32 *count)
{
    grid_count(&w->grid, x, z, r, mask, w->x, w->z, count);
}

u32 combat_near(const CombatWorld *w, f32 x, f32 z, f32 r, u32 mask, u32 *out)
{
    return grid_query(&w->grid, x, z, r, mask, out, COMBAT_NEAR_MAX);
}

// Порядок фаз: тело, таймеры, план ударов по состоянию до них, применение, поворот, шаг, расталкивание.
// Удар не видит ран и падений других ударов этого же тика. Номер слота порядок только фиксирует.
void combat_tick(CombatWorld *w)
{
    combat_sync_grid(w);
    u32 ns = 0;
    // Время действия всем телом: до решений этого шага, чтобы новое действие начиналось с нуля
    for (u32 i = 0; i < w->count; i++)
        if (!w->idle[i]) {
            w->act_t[i] += COMBAT_DT;
            act_update(w, i);
        }
    for (u32 i = 0; i < w->count; i++)
        if (!w->idle[i]) tick_body(w, i);
    for (u32 i = 0; i < w->count; i++)
        if (!w->idle[i]) tick_state(w, i, w->strikes, &ns);
    u32 nh = 0;
    for (u32 k = 0; k < ns; k++) plan_strike(w, w->strikes[k], &nh);
    apply_hits(w, nh);
    for (u32 i = 0; i < w->count; i++)
        if (!w->idle[i]) tick_face(w, i);
    for (u32 i = 0; i < w->count; i++)
        if (!w->idle[i]) tick_step(w, i);
    // Сетка по позициям после шага: расталкивание и ИИ следующего тика читают её
    build_grid(w);
    jobs_for(w->count, 64, separate_job, w);
    around_bodies(w);
    jobs_for(w->count, 256, separate_apply_job, w);
    build_grid(w);
    w->tick++;
}

// ---------------------------------------------------------------- создание

void combat_init(CombatWorld *w, Arena *a, u32 cap, u32 ev_cap)
{
    memset(w, 0, sizeof *w);
    w->cap = cap;
    w->ev_cap = ev_cap;
#define F(name) w->name = ARENA_PUSH_ARRAY(a, f32, cap)
#define B(name) w->name = ARENA_PUSH_ARRAY(a, u8, cap)
    F(x); F(z); F(heading); F(timer); F(dur); F(pause); F(since_hit); F(mvx); F(mvz);
    B(state); B(team); B(attack); B(react); F(aim_x); F(aim_y); F(aim_s);
    w->target = ARENA_PUSH_ARRAY(a, i32, cap);
    w->gitem = ARENA_PUSH_ARRAY(a, i32, cap);
    w->order = ARENA_PUSH_ARRAY(a, i32, cap);
    B(idle);
    for (u32 p = 0; p < PART_COUNT; p++) w->hp[p] = ARENA_PUSH_ARRAY(a, f32, cap);
    F(blood); F(blood_max); F(bleed); F(shock); F(morale); F(morale_base); F(mass); F(height);
    B(fracture); B(severed); B(disarmed); B(fall); B(prone); B(loose);
    B(act); B(act_flags); F(act_t); F(act_t0); F(act_dur);
    for (u32 s = 0; s < SK_COUNT; s++) w->skill[s] = ARENA_PUSH_ARRAY(a, f32, cap);
    B(weapon); B(weapon_q);
    for (u32 s = 0; s < SLOT_COUNT; s++) w->armor[s] = ARENA_PUSH_ARRAY(a, u8, cap);
#undef F
#undef B
    w->ev = ARENA_PUSH_ARRAY(a, CombatEvent, ev_cap + 1);
    w->sep_x = ARENA_PUSH_ARRAY(a, f32, cap);
    w->sep_z = ARENA_PUSH_ARRAY(a, f32, cap);
    w->strikes = ARENA_PUSH_ARRAY(a, u32, cap);
    w->hit_cap = cap * (u32)MAX_STRIKE_TARGETS;
    w->hits = ARENA_PUSH_ARRAY(a, CombatHit, w->hit_cap);
    w->guard_kind = ARENA_PUSH_ARRAY(a, u8, cap);
    w->guard_hold = ARENA_PUSH_ARRAY(a, f32, cap);
    w->grid_tag = ARENA_PUSH_ARRAY(a, u8, cap);
    grid_init(&w->grid, a, cap, 128 * 128 * 4, 2.0f);
}

void combat_ser(Ser *s, CombatWorld *w)
{
    ser_count(s, &w->count, w->cap);
    SER(s, w->seed);
    SER(s, w->tick);
    SER(s, w->mercy);
    SER(s, w->halted);
    SER(s, w->fence_r);
    SER(s, w->foe);
    u32 n = w->count;
#define C(name) SER_N(s, w->name, n)
    if (!w->body_ext) {
        C(x); C(z); C(heading);
        for (u32 p = 0; p < PART_COUNT; p++) C(hp[p]);
        C(blood); C(morale_base); C(mass); C(height); C(fracture); C(severed);
        C(act); C(act_flags); C(act_t); C(act_t0); C(act_dur);
        for (u32 k = 0; k < SK_COUNT; k++) C(skill[k]);
    }
    C(state); C(team); C(attack); C(react); C(aim_x); C(aim_y); C(aim_s);
    C(timer); C(dur); C(pause); C(target); C(since_hit); C(mvx); C(mvz); C(idle); C(order);
    C(blood_max); C(bleed); C(shock); C(morale);
    C(disarmed); C(fall); C(prone); C(loose); C(gitem);
    C(weapon); C(weapon_q);
    for (u32 k = 0; k < SLOT_COUNT; k++) C(armor[k]);
    C(grid_tag); C(sep_x); C(sep_z); C(strikes);
#undef C
    ser_count(s, &w->ev_count, w->ev_cap);
    SER_N(s, w->ev, w->ev_count);
    SER(s, w->last_ev);
    SER(s, w->grid_dirty);
    grid_ser(s, &w->grid);
}

void combat_bind_body(CombatWorld *w, const CombatBody *b)
{
    w->x = b->x, w->z = b->z, w->heading = b->heading;
    for (u32 p = 0; p < PART_COUNT; p++) w->hp[p] = b->hp[p];
    w->blood = b->blood, w->mass = b->mass, w->height = b->height, w->morale_base = b->morale_base;
    w->fracture = b->fracture, w->severed = b->severed;
    for (u32 s = 0; s < SK_COUNT; s++) w->skill[s] = b->skill[s];
    w->act = b->act, w->act_flags = b->act_flags;
    w->act_t = b->act_t, w->act_t0 = b->act_t0, w->act_dur = b->act_dur;
    w->body_ext = true;
}

void combat_reset(CombatWorld *w, u64 seed)
{
    w->count = 0;
    w->tick = 0;
    w->seed = seed;
    w->ev_count = 0;
    w->grid_dirty = true;
    for (u32 t = 0; t < COMBAT_MAX_TEAMS; t++) w->foe[t] = ((1u << COMBAT_MAX_TEAMS) - 1u) & ~(1u << t);
}

void combat_set_foes(CombatWorld *w, u8 a, u8 b, bool on)
{
    if (a >= COMBAT_MAX_TEAMS || b >= COMBAT_MAX_TEAMS || a == b) return;
    if (on) {
        w->foe[a] |= 1u << b;
        w->foe[b] |= 1u << a;
    } else {
        w->foe[a] &= ~(1u << b);
        w->foe[b] &= ~(1u << a);
    }
    w->grid_dirty = true;
}

u64 combat_hash_events(const CombatWorld *w, u64 h)
{
    for (u32 k = 0; k < w->ev_count; k++) {
        const CombatEvent *e = &w->ev[k];
        u32 q[4] = { e->tick, (u32)e->kind | (u32)e->part << 8 | (u32)e->flags << 16 | (u32)e->arg << 24,
                     (u32)e->a | (u32)e->b << 16, (u32)((e->cut + e->blunt * 3.0f + e->pierce * 7.0f) * 100.0f) };
        for (int j = 0; j < 4; j++) h = (h ^ q[j]) * 0x100000001B3ull;
    }
    return h;
}

u32 combat_add(CombatWorld *w, const FighterDesc *d)
{
    if (w->count >= w->cap || d->team >= COMBAT_MAX_TEAMS) return w->count - 1;
    u32 i = w->count++;
    w->idle[i] = 0;
    w->order[i] = -1;
    combat_load(w, i, d);
    return i;
}

void combat_load(CombatWorld *w, u32 i, const FighterDesc *d)
{
    w->x[i] = d->pos.x;
    w->z[i] = d->pos.z;
    w->heading[i] = d->heading;
    w->team[i] = d->team;
    w->target[i] = -1;
    w->mvx[i] = w->mvz[i] = 0.0f;
    w->since_hit[i] = 100.0f;
    w->react[i] = REACT_NONE;
    w->attack[i] = ATK_SWING;
    w->aim_x[i] = w->aim_s[i] = 0.0f;
    w->aim_y[i] = 1.3f;
    bool fresh = d->blood <= 0.0f;
    for (u32 p = 0; p < PART_COUNT; p++) w->hp[p][i] = fresh ? g_part_hp[p] : d->hp[p];
    w->mass[i] = d->mass;
    w->height[i] = d->height;
    w->blood_max[i] = g_tune.blood_full * d->mass / g_tune.blood_mass;
    w->blood[i] = fresh ? w->blood_max[i] : d->blood;
    w->bleed[i] = 0.0f;
    w->shock[i] = 0.0f;
    w->morale_base[i] = d->morale_base;
    w->morale[i] = d->morale_base;
    w->fracture[i] = fresh ? 0 : d->fracture;
    w->severed[i] = fresh ? 0 : d->severed;
    w->disarmed[i] = 0;
    w->fall[i] = FALL_BACK;
    w->prone[i] = 0;
    w->loose[i] = 0;
    w->gitem[i] = COMBAT_IN_HAND;
    for (u32 s = 0; s < SK_COUNT; s++) w->skill[s][i] = d->skill[s];
    w->weapon[i] = d->weapon;
    w->weapon_q[i] = d->weapon_q;
    for (u32 s = 0; s < SLOT_COUNT; s++) w->armor[s][i] = d->armor[s];
    w->pause[i] = g_tune.first_pause + combat_roll(w, i, 0, 1);
    w->grid_dirty = true;
    // Действие всем телом с нуля: входит в бой уже в нынешнем состоянии, без клипа падения
    w->state[i] = FS_READY;
    w->act_flags[i] = 0;
    w->act_t[i] = 0.0f;
    combat_set_state(w, i, FS_READY, 0.0f);
    check_body(w, i);
    w->act_flags[i] &= ACTF_WAS_FRONT;
    act_update(w, i);
}

void combat_store(const CombatWorld *w, u32 i, FighterDesc *d)
{
    for (u32 p = 0; p < PART_COUNT; p++) d->hp[p] = w->hp[p][i];
    d->blood = fmaxf(w->blood[i], 0.0f);
    d->fracture = w->fracture[i];
    d->severed = w->severed[i];
    for (u32 s = 0; s < SK_COUNT; s++) d->skill[s] = w->skill[s][i];
}

u32 combat_team_active(const CombatWorld *w, u8 team)
{
    u32 n = 0;
    for (u32 i = 0; i < w->count; i++)
        if (w->team[i] == team && !fs_out(w->state[i])) n++;
    return n;
}

f32 combat_rating(const FighterDesc *d)
{
    const WeaponDef *wd = &g_weapons[d->weapon];
    f32 wpn = d->skill[SK_WEAPON + wd->wclass];
    f32 skill = g_tune.rating_attack * (d->skill[SK_ATTACK] + wpn) + g_tune.rating_defense * (d->skill[SK_DEFENSE] + wpn) +
                g_tune.rating_dodge * d->skill[SK_DODGE] + g_tune.rating_tough * d->skill[SK_TOUGHNESS] +
                g_tune.rating_strength * d->skill[SK_STRENGTH];
    // Снаряжение: урон за секунду замаха и средняя защита торса
    f32 dmg = (wd->cut + wd->blunt) * g_quality_mul[d->weapon_q] * wd->speed;
    f32 armor = 0.0f;
    for (u32 s = 0; s < SLOT_COUNT; s++) {
        if (d->armor[s] == ARMOR_NONE) continue;
        const ArmorDef *a = &g_armors[d->armor[s]];
        armor += a->cover[PART_CHEST] * 0.5f * (a->res_cut + a->res_blunt);
    }
    // Раны с прошлых боёв
    f32 health = 1.0f;
    if (d->blood > 0.0f)
        for (u32 p = 0; p <= PART_STOMACH; p++) health = fminf(health, d->hp[p] / g_part_hp[p]);
    return (skill + g_tune.rating_dmg * (dmg - g_tune.rating_dmg_base) + g_tune.rating_armor * armor) *
           (g_tune.rating_health_min + g_tune.rating_health_k * health);
}
