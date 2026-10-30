#include "combat.h"

#include <string.h>

#include "../core/hash.h"
#include "../core/jobs.h"

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

void combat_set_state(CombatWorld *w, u32 i, u8 s, f32 dur)
{
    w->state[i] = s;
    w->timer[i] = dur;
    w->dur[i] = dur;
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
    f32 over = load_kg(w, i) - (15.0f + sk(w, i, SK_STRENGTH) / 4.0f);
    return over > 0.0f ? fminf(over * 0.02f, 0.6f) : 0.0f;
}

static f32 weapon_overweight(const CombatWorld *w, u32 i)
{
    return fmaxf(0.0f, weapon_of(w, i)->weight - (4.0f + sk(w, i, SK_STRENGTH) / 8.0f));
}

static f32 shock_factor(const CombatWorld *w, u32 i)
{
    return 1.0f - 0.3f * sat01(w->shock[i] / 50.0f);
}

static f32 armor_dodge_pen(const CombatWorld *w, u32 i)
{
    f32 p = 0.0f;
    for (u32 s = 0; s < SLOT_COUNT; s++)
        if (w->armor[s][i] != ARMOR_NONE) p += g_armors[w->armor[s][i]].dodge_pen;
    return fminf(p, 0.8f);
}

// Враги перед бойцом в радиусе 3 м: каждый следующий снижает защиту на 15%
static f32 surround_factor(const CombatWorld *w, u32 i)
{
    u32 n = 0;
    f32 fx = sinf(w->heading[i]), fz = cosf(w->heading[i]);
    u32 near[COMBAT_NEAR_MAX];
    u32 nn = combat_near(w, w->x[i], w->z[i], 3.0f + GRID_SLACK, combat_foe_tags(w, w->team[i]), near);
    for (u32 q = 0; q < nn; q++) {
        u32 j = near[q];
        if (!combat_hostile(w, i, j) || fs_out(w->state[j]) || fs_lying(w->state[j])) continue;
        f32 dx = w->x[j] - w->x[i], dz = w->z[j] - w->z[i];
        if (dx * dx + dz * dz > 9.0f || dx * fx + dz * fz < 0.0f) continue;
        n++;
    }
    return n > 1 ? powf(0.85f, (f32)(n - 1)) : 1.0f;
}

static f32 attack_eff(const CombatWorld *w, u32 i)
{
    const WeaponDef *wd = weapon_of(w, i);
    f32 base = 0.5f * (sk(w, i, SK_ATTACK) + sk(w, i, SK_WEAPON + wd->wclass)) - 2.0f * weapon_overweight(w, i);
    f32 arm = 0.5f + 0.5f * part_ratio(w, i, PART_ARM_R);
    return fmaxf(0.0f, base) * arm * shock_factor(w, i);
}

static f32 defense_eff(const CombatWorld *w, u32 i)
{
    const WeaponDef *wd = weapon_of(w, i);
    f32 base = 0.5f * (sk(w, i, SK_DEFENSE) + sk(w, i, SK_WEAPON + wd->wclass));
    f32 arm = (0.7f + 0.3f * part_ratio(w, i, PART_ARM_L)) * (0.5f + 0.5f * part_ratio(w, i, PART_ARM_R));
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
    if (s == FS_RECOVER && w->timer[j] < lead * 0.6f) free = true;
    if (!free) return false;
    return fabsf(angle_to(w, j, attacker)) < 100.0f * (PI_F / 180.0f);
}

// ---------------------------------------------------------------- навыки

static void gain(CombatWorld *w, u32 i, u32 s, f32 k, f32 opp)
{
    f32 own = w->skill[s][i];
    f32 m = clampf(1.0f + (opp - own) / 25.0f, 0.1f, 3.0f);
    w->skill[s][i] = fminf(100.0f, own + 1.3f * k * m / (1.0f + own / 20.0f));
}

// ---------------------------------------------------------------- состояния тела

static void enter_ready(CombatWorld *w, u32 i)
{
    f32 r = combat_roll(w, i, 0, 11);
    f32 p = (0.3f + 0.5f * r) * (1.0f - sk(w, i, SK_ATTACK) / 200.0f);
    if (w->morale[i] < 40.0f) p *= 1.6f;
    w->pause[i] = p;
    combat_set_state(w, i, FS_READY, 0.0f);
}

// Сбит с ног: падение, подъём со спины и ничком, подбор оружия, с. Падать помогает тяжесть, вставать
// приходится через силу: подъём вдвое дольше падения. От навыков не зависят
#define KNOCKED_S 1.1f
#define GETUP_BACK_S 2.2f
#define GETUP_FRONT_S 2.0f
#define PICKUP_S 0.9f

static bool legs_hold(const CombatWorld *w, u32 i)
{
    return part_ratio(w, i, PART_LEG_L) > 0.0f && part_ratio(w, i, PART_LEG_R) > 0.0f;
}

// Пора вставать: на перебитых ногах не встаёт вовсе, а сразу ползёт
static void begin_getup(CombatWorld *w, u32 i)
{
    if (!legs_hold(w, i)) {
        w->prone[i] = 1;
        combat_set_state(w, i, FS_CRAWL, 0.0f);
        return;
    }
    combat_set_state(w, i, FS_GETUP, w->prone[i] ? GETUP_FRONT_S : GETUP_BACK_S);
    combat_event(w, EV_GETUP, i, i);
}

// Валится с ног: ничком — если толкнуло в спину или (без сознания) с броском
static void go_down(CombatWorld *w, u32 i, u8 s, f32 dur, f32 front_p, u32 salt)
{
    if (!fs_lying(w->state[i])) w->prone[i] = w->fall[i] == FALL_FRONT || combat_roll(w, i, 0, salt) < front_p;
    combat_set_state(w, i, s, dur);
}

// После подъёма или пробуждения: может ли снова драться
static void after_getup(CombatWorld *w, u32 i)
{
    if (w->disarmed[i]) {
        combat_set_state(w, i, FS_SURRENDER, 0.0f);
    } else if (!legs_hold(w, i)) {
        w->prone[i] = 1;
        combat_set_state(w, i, FS_CRAWL, 0.0f);
    } else if (w->loose[i]) {
        combat_set_state(w, i, FS_PICKUP, PICKUP_S);
    } else {
        enter_ready(w, i);
    }
}

static void check_body(CombatWorld *w, u32 i)
{
    if (w->state[i] == FS_DEAD) return;
    f32 tough = sk(w, i, SK_TOUGHNESS);
    bool ko = w->blood[i] < 0.5f * w->blood_max[i];
    for (u32 p = 0; p <= PART_STOMACH; p++) {
        f32 mx = g_part_hp[p];
        if (w->hp[p][i] <= -mx) {
            go_down(w, i, FS_DEAD, 0.0f, 0.0f, 0);
            combat_event(w, EV_DEATH, i, i);
            return;
        }
        if (w->hp[p][i] <= -(tough / 100.0f * 0.5f) * mx) ko = true;
    }
    if (w->blood[i] <= 0.0f) {
        // Истёк кровью: оседает и валится набок, не ничком
        if (!fs_lying(w->state[i])) w->prone[i] = 0;
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
        w->prone[i] = 1;
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
    w->bleed[t] += cut * 0.04f + pierce * 0.03f + blunt * 0.005f;
    f32 dshock = cut + pierce + 2.0f * blunt;
    w->shock[t] += dshock;
    w->morale[t] -= total * 0.08f;
    w->since_hit[t] = 0.0f;
    u8 bit = (u8)(1u << part);
    if (part != PART_CHEST && part != PART_STOMACH && w->hp[part][t] <= -0.5f * mx && !(w->fracture[t] & bit)) {
        w->fracture[t] |= bit;
        flags |= EVF_FRACTURE;
    }
    if (!body_part_vital((BodyPart)part) && w->hp[part][t] <= -mx) {
        w->hp[part][t] = -mx;
        if (cut > blunt && !(w->severed[t] & bit)) {
            w->severed[t] |= bit;
            w->bleed[t] += 3.0f;
            flags |= EVF_SEVER;
        }
    }
    u8 s = w->state[t];
    bool standing = !fs_lying(s) && !fs_out(s);
    // Резкий скачок шока валит с ног; стойкость поднимает порог
    if (standing && dshock > 45.0f * (1.0f + sk(w, t, SK_TOUGHNESS) / 100.0f)) {
        go_down(w, t, FS_KNOCKED, KNOCKED_S, 0.0f, 0);
        combat_event(w, EV_FALL, t, t);
    } else if (standing && blunt > 20.0f * (1.0f + sk(w, t, SK_TOUGHNESS) / 100.0f)) {
        combat_set_state(w, t, FS_STAGGER, (0.4f + 0.6f * combat_roll(w, t, 0, 21)) * (1.0f - sk(w, t, SK_TOUGHNESS) / 200.0f));
        flags |= EVF_STAGGER;
    }
    gain(w, t, SK_TOUGHNESS, 0.02f * total / 10.0f * (w->state[t] == FS_UNCONSCIOUS ? 2.0f : 1.0f), sk(w, t, SK_TOUGHNESS));
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
    f32 spread = 1.0f - sk(w, i, SK_DEXTERITY) / 250.0f;
    f32 tri = (r1 + r2 - 1.0f) * spread;  // -1..1, к середине гуще
    switch (kind) {
    case ATK_SWING:
        // Наискось сверху в ключицу, поперёк живота или низом по ногам
        if (r3 < 0.40f) {
            w->aim_y[i] = 1.40f + 0.14f * tri;
            w->aim_s[i] = -(0.50f + 0.35f * r2);
        } else if (r3 < 0.88f) {
            w->aim_y[i] = 1.05f + 0.12f * tri;
            w->aim_s[i] = -(0.10f + 0.30f * r1);
        } else {
            w->aim_y[i] = 0.50f + 0.25f * r2;
            w->aim_s[i] = -0.10f;
        }
        w->aim_x[i] = 0.0f;
        break;
    case ATK_OVERHEAD:
        // В голову или в плечо справа от себя, клинок идёт чуть справа налево; точка — на высоте плеч
        w->aim_x[i] = -0.12f + 0.26f * tri;
        w->aim_y[i] = 1.45f;
        w->aim_s[i] = 0.20f + 0.30f * r3;
        break;
    default:
        w->aim_x[i] = 0.12f * (r3 - 0.5f) * 2.0f * spread;
        // Укол в корпус, изредка в лицо
        w->aim_y[i] = combat_roll(w, i, t, 23) < 0.07f ? 1.60f : 1.12f + 0.34f * tri;
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
    for (u32 c = 0; c < g_body_hit_count; c++) {
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
    f32 c = 0.5f + (defense_eff(w, def) - attack_eff(w, att)) / 100.0f;
    if (kind == ATK_SWING) c -= 0.1f;
    // Древковое вплотную неудобно
    if (weapon_of(w, def)->wclass == WC_POLEARM) {
        f32 dx = w->x[att] - w->x[def], dz = w->z[att] - w->z[def];
        if (dx * dx + dz * dz < 1.44f) c -= 0.15f;
    }
    return clampf(c, 0.05f, 0.95f);
}

static f32 dodge_chance(const CombatWorld *w, u32 def, u32 att, u8 kind)
{
    f32 c = 0.1f + (dodge_eff(w, def) - attack_eff(w, att)) / 150.0f;
    if (kind == ATK_SWING) c += 0.1f;
    return clampf(c, 0.0f, 0.5f);
}

// Бросок реакции защитника; опыт за попытку уклониться
static u8 roll_react(CombatWorld *w, u32 def, u32 att, u8 kind, f32 lead)
{
    if (!can_react(w, def, att, lead)) return REACT_NONE;
    if (combat_roll(w, def, att, 31) < block_chance(w, def, att, kind)) return REACT_BLOCK;
    gain(w, def, SK_DODGE, 0.02f, attack_eff(w, att));
    if (combat_roll(w, def, att, 32) < dodge_chance(w, def, att, kind)) return REACT_DODGE;
    return REACT_NONE;
}

static f32 windup_time(const CombatWorld *w, u32 i, u8 kind)
{
    const WeaponDef *wd = weapon_of(w, i);
    f32 t = 0.55f / wd->speed * g_attacks[kind].windup_mul;
    t *= (1.0f - sk(w, i, SK_DEXTERITY) / 250.0f) * (1.0f - sk(w, i, SK_ATTACK) / 300.0f);
    t *= (1.0f + encumbrance(w, i)) * (1.0f + 0.06f * weapon_overweight(w, i));
    return t;
}

void combat_begin_attack(CombatWorld *w, u32 i, u8 kind, u32 t, bool counter)
{
    f32 wt = windup_time(w, i, kind) * (counter ? 0.6f : 1.0f);
    combat_set_state(w, i, FS_WINDUP, wt);
    w->attack[i] = kind;
    w->target[i] = (i32)t;
    roll_aim(w, i, t, kind);
    combat_event(w, counter ? EV_COUNTER : EV_ATTACK, i, t);
    w->ev[w->last_ev].arg = kind;
    gain(w, i, SK_ATTACK, 0.02f, defense_eff(w, t));
    gain(w, i, SK_WEAPON + weapon_of(w, i)->wclass, 0.03f, defense_eff(w, t));
    if (weapon_overweight(w, i) > 0.0f) gain(w, i, SK_STRENGTH, 0.02f, 50.0f);
    if (weapon_of(w, i)->weight <= 5.0f) gain(w, i, SK_DEXTERITY, 0.02f, sk(w, t, SK_DEXTERITY));

    // Защитник видит замах и успевает поднять блок или отступить
    u8 r = roll_react(w, t, i, kind, wt);
    w->react[i] = r;
    if (r == REACT_BLOCK) {
        f32 hold = wt + 0.2f;
        if (w->state[t] != FS_BLOCK || w->timer[t] < hold) combat_set_state(w, t, FS_BLOCK, hold);
    } else if (r == REACT_DODGE) {
        combat_set_state(w, t, FS_DODGE, wt + 0.2f);
    }
}

static u8 best_counter_kind(const CombatWorld *w, u32 i)
{
    u8 mask = weapon_of(w, i)->attacks;
    if (mask & (1u << ATK_THRUST)) return ATK_THRUST;
    if (mask & (1u << ATK_OVERHEAD)) return ATK_OVERHEAD;
    return ATK_SWING;
}

static void resolve_strike(CombatWorld *w, u32 i)
{
    const WeaponDef *wd = weapon_of(w, i);
    u8 kind = w->attack[i];
    u32 main_t = (u32)w->target[i];
    f32 reach = combat_reach(w, i, kind) + 0.3f;
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
    for (u32 n = 0; n < nc; n++, falloff *= 0.75f) {
        u32 t = cand[n];
        bool ally = w->team[t] == w->team[i];
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

        if (r == REACT_BLOCK) {
            combat_event(w, EV_BLOCK, i, t);
            gain(w, t, SK_DEFENSE, 0.05f, attack_eff(w, i));
            gain(w, t, SK_WEAPON + weapon_of(w, t)->wclass, 0.03f, attack_eff(w, i));
            // Тяжёлый удар пробивает стойку: ошеломление и отдача в руку
            f32 fa = wd->weight * (1.0f + sk(w, i, SK_STRENGTH) / 100.0f);
            f32 fd = weapon_of(w, t)->weight * (1.0f + sk(w, t, SK_STRENGTH) / 100.0f);
            if (fa > 2.0f * fd) {
                w->ev[w->last_ev].flags |= EVF_STAGGER;
                combat_set_state(w, t, FS_STAGGER, (0.4f + 0.6f * combat_roll(w, t, i, 41)) * (1.0f - sk(w, t, SK_TOUGHNESS) / 200.0f));
                apply_damage(w, t, PART_ARM_R, 0, wd->blunt * 0.2f * g_quality_mul[w->weapon_q[i]], 0);
                check_body(w, t);
            } else {
                f32 cc = fminf(0.3f, (defense_eff(w, t) - attack_eff(w, i)) / 100.0f);
                if (!ally && combat_roll(w, t, i, 42) < cc) combat_begin_attack(w, t, best_counter_kind(w, t), i, true);
            }
            continue;
        }
        if (r == REACT_DODGE) {
            combat_event(w, EV_DODGE, i, t);
            gain(w, t, SK_DODGE, 0.06f, attack_eff(w, i));
            continue;
        }

        // Попадание
        u32 part = choose_part(w, i, t, kind, 50 + n);
        f32 m = g_quality_mul[w->weapon_q[i]] * (0.8f + sk(w, i, SK_STRENGTH) / 250.0f) *
                (0.85f + 0.3f * combat_roll(w, i, t, 51 + n)) * g_attacks[kind].dmg_mul * falloff;
        f32 cut = 0, blunt = 0, pierce = 0;
        if (kind == ATK_THRUST) {
            pierce = wd->pierce * m;
        } else {
            cut = wd->cut * m;
            blunt = wd->blunt * m;
        }
        for (u32 l = 0; l < ARRAY_COUNT(g_armor_layer_order); l++) {
            u8 a = w->armor[g_armor_layer_order[l]][t];
            if (a == ARMOR_NONE) continue;
            const ArmorDef *ad = &g_armors[a];
            f32 cover = ad->cover[part] * (kind == ATK_THRUST ? 0.8f : 1.0f);
            if (cover <= 0.0f || combat_roll(w, t, i, 60 + l) >= cover) continue;
            cut *= 1.0f - ad->res_cut;
            blunt *= 1.0f - ad->res_blunt;
            pierce *= 1.0f - ad->res_pierce * (1.0f - wd->pen);
        }
        f32 tough = 1.0f - sk(w, t, SK_TOUGHNESS) / 400.0f;
        cut *= tough;
        blunt *= tough;
        pierce *= tough;

        gain(w, i, SK_ATTACK, 0.05f, defense_eff(w, t));
        gain(w, t, SK_DEFENSE, 0.03f, attack_eff(w, i));
        combat_event(w, EV_HIT, i, t);
        u32 hit_ev = w->last_ev;
        // Куда толкает удар: от бьющего; спереди — назад, со спины — вперёд, сбоку — вбок
        f32 ha = angle_to(w, t, i);
        f32 ah = fabsf(ha);
        w->fall[t] = ah < 0.9f ? FALL_BACK : ah > 2.25f ? FALL_FRONT : ha > 0.0f ? FALL_RIGHT : FALL_LEFT;
        u8 flags = apply_damage(w, t, part, cut, blunt, pierce);
        CombatEvent *e = &w->ev[hit_ev];
        e->part = (u8)part;
        e->flags = flags | (ally ? EVF_ALLY : 0);
        e->arg = kind;
        e->cut = cut;
        e->blunt = blunt;
        e->pierce = pierce;

        // Сбивание с ног
        u8 s = w->state[t];
        if (!fs_lying(s) && !fs_out(s)) {
            f32 force = wd->knock * wd->weight * (1.0f + sk(w, i, SK_STRENGTH) / 100.0f) * falloff;
            f32 resist = w->mass[t] * (1.0f + sk(w, t, SK_TOUGHNESS) / 100.0f) * 0.4f;
            if (force > resist) {
                f32 dx = w->x[t] - w->x[i], dz = w->z[t] - w->z[i];
                f32 l = sqrtf(dx * dx + dz * dz) + 1e-4f;
                f32 push = 0.5f + combat_roll(w, i, t, 70);
                w->x[t] += dx / l * push;
                w->z[t] += dz / l * push;
                go_down(w, t, FS_KNOCKED, KNOCKED_S, 0.0f, 0);
                // Сильный удар выбивает оружие из рук: чем больше перевес силы, тем вернее
                f32 p_drop = clampf(0.15f + 0.3f * (force / resist - 1.0f), 0.15f, 0.5f) * (1.0f - sk(w, t, SK_STRENGTH) / 200.0f);
                if (!w->disarmed[t] && combat_roll(w, t, i, 71) < p_drop) w->loose[t] = 1;
                combat_event(w, EV_KNOCKDOWN, i, t);
            }
        }
        check_body(w, t);
    }
}

// ---------------------------------------------------------------- тик

static void tick_body(CombatWorld *w, u32 i)
{
    const f32 dt = COMBAT_DT;
    u8 s = w->state[i];
    if (s == FS_DEAD) return;
    f32 rate = w->bleed[i] * (w->hp[PART_STOMACH][i] <= 0.0f ? 2.0f : 1.0f);
    w->blood[i] -= rate * dt;
    w->bleed[i] = fmaxf(0.0f, w->bleed[i] - 0.3f * dt);
    w->shock[i] = fmaxf(0.0f, w->shock[i] - (5.0f + sk(w, i, SK_TOUGHNESS) * 0.05f) * dt);
    w->since_hit[i] += dt;
    if (w->since_hit[i] > 3.0f && w->morale[i] < w->morale_base[i]) w->morale[i] += 1.0f * dt;

    if (s == FS_UNCONSCIOUS) {
        // Лёжа без сознания жизненные части понемногу приходят в себя
        bool vitals_ok = true;
        for (u32 p = 0; p <= PART_STOMACH; p++) {
            w->hp[p][i] = fminf(w->hp[p][i] + 0.5f * dt, g_part_hp[p]);
            if (w->hp[p][i] <= 0.0f) vitals_ok = false;
        }
        w->blood[i] = fminf(w->blood[i] + 0.1f * dt, w->blood_max[i]);
        if (vitals_ok && w->blood[i] > 0.55f * w->blood_max[i]) {
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
        combat_set_state(w, i, FS_STRIKE, 0.15f);
        strikes[(*ns)++] = i;
        break;
    case FS_STRIKE:
        combat_set_state(w, i, FS_RECOVER, 0.45f * (0.7f + weapon_of(w, i)->weight / 20.0f));
        break;
    case FS_KNOCKED: {
        f32 down = (1.0f + 1.5f * combat_roll(w, i, 0, 81)) * (1.0f - sk(w, i, SK_TOUGHNESS) / 200.0f);
        combat_set_state(w, i, FS_DOWN, down);
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

static void tick_move(CombatWorld *w, u32 i)
{
    u8 s = w->state[i];
    f32 speed = 0.0f, turn = 6.0f;
    switch (s) {
    case FS_READY:   speed = 1.6f; break;
    case FS_RETREAT: speed = 2.2f; break;
    case FS_BLOCK:   speed = 0.5f; break;
    case FS_DODGE:   speed = 1.2f; break;
    case FS_CRAWL:   speed = 0.25f; turn = 1.5f; break;
    case FS_WINDUP:  turn = 2.0f; break;
    default:         turn = 0.0f; break;
    }
    if (turn > 0.0f && w->target[i] >= 0 && s != FS_RETREAT && s != FS_CRAWL) {
        u32 t = (u32)w->target[i];
        f32 d = angle_to(w, i, t);
        f32 step = turn * COMBAT_DT;
        w->heading[i] = angle_wrap(w->heading[i] + clampf(d, -step, step));
    }
    if (speed <= 0.0f) return;
    f32 legs = 0.5f * (part_ratio(w, i, PART_LEG_L) + part_ratio(w, i, PART_LEG_R));
    if (s != FS_CRAWL) speed *= (0.3f + 0.7f * legs);
    speed *= (1.0f - encumbrance(w, i)) * (1.0f + sk(w, i, SK_ATHLETICS) / 200.0f);
    w->x[i] += w->mvx[i] * speed * COMBAT_DT;
    w->z[i] += w->mvz[i] * speed * COMBAT_DT;
    if (s == FS_RETREAT && w->mvx[i] * w->mvx[i] + w->mvz[i] * w->mvz[i] > 0.01f) {
        f32 want = atan2f(w->mvx[i], w->mvz[i]);
        f32 step = 6.0f * COMBAT_DT;
        w->heading[i] = angle_wrap(w->heading[i] + clampf(angle_wrap(want - w->heading[i]), -step, step));
    }
}

// Расталкивание тел: каждый считает свой сдвиг от соседей по позициям начала шага,
// поэтому куски можно считать в любом порядке и в любом числе потоков
static bool pinned(const CombatWorld *w, u32 i)
{
    return w->idle[i] || w->state[i] == FS_DEAD || w->state[i] == FS_UNCONSCIOUS;
}

static void separate_job(void *ctx, u32 b, u32 e)
{
    CombatWorld *w = ctx;
    const f32 r2 = 0.6f;
    u32 near[COMBAT_NEAR_MAX];
    for (u32 i = b; i < e; i++) {
        f32 sx = 0.0f, sz = 0.0f;
        if (!pinned(w, i)) {
            u32 nn = combat_near(w, w->x[i], w->z[i], r2, GRID_ALL_TAGS, near);
            for (u32 q = 0; q < nn; q++) {
                u32 j = near[q];
                if (j == i) continue;
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

// Порядок фаз: тело, таймеры состояний, удары по номеру бойца, шаг, расталкивание.
// Всё, что пишет в чужих бойцов или в журнал, идёт в одном потоке по возрастанию номера.
void combat_tick(CombatWorld *w)
{
    combat_sync_grid(w);
    u32 ns = 0;
    for (u32 i = 0; i < w->count; i++)
        if (!w->idle[i]) tick_body(w, i);
    for (u32 i = 0; i < w->count; i++)
        if (!w->idle[i]) tick_state(w, i, w->strikes, &ns);
    for (u32 k = 0; k < ns; k++) resolve_strike(w, w->strikes[k]);
    for (u32 i = 0; i < w->count; i++)
        if (!w->idle[i]) tick_move(w, i);
    // Сетка по позициям после шага: расталкивание и ИИ следующего тика читают её
    build_grid(w);
    jobs_for(w->count, 64, separate_job, w);
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
    w->order = ARENA_PUSH_ARRAY(a, i32, cap);
    B(idle);
    for (u32 p = 0; p < PART_COUNT; p++) w->hp[p] = ARENA_PUSH_ARRAY(a, f32, cap);
    F(blood); F(blood_max); F(bleed); F(shock); F(morale); F(morale_base); F(mass); F(height);
    B(fracture); B(severed); B(disarmed); B(fall); B(prone); B(loose);
    for (u32 s = 0; s < SK_COUNT; s++) w->skill[s] = ARENA_PUSH_ARRAY(a, f32, cap);
    B(weapon); B(weapon_q);
    for (u32 s = 0; s < SLOT_COUNT; s++) w->armor[s] = ARENA_PUSH_ARRAY(a, u8, cap);
#undef F
#undef B
    w->ev = ARENA_PUSH_ARRAY(a, CombatEvent, ev_cap + 1);
    w->sep_x = ARENA_PUSH_ARRAY(a, f32, cap);
    w->sep_z = ARENA_PUSH_ARRAY(a, f32, cap);
    w->strikes = ARENA_PUSH_ARRAY(a, u32, cap);
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
    C(x); C(z); C(heading); C(state); C(team); C(attack); C(react); C(aim_x); C(aim_y); C(aim_s);
    C(timer); C(dur); C(pause); C(target); C(since_hit); C(mvx); C(mvz); C(idle); C(order);
    for (u32 p = 0; p < PART_COUNT; p++) C(hp[p]);
    C(blood); C(blood_max); C(bleed); C(shock); C(morale); C(morale_base); C(mass); C(height);
    C(fracture); C(severed); C(disarmed); C(fall); C(prone); C(loose);
    for (u32 k = 0; k < SK_COUNT; k++) C(skill[k]);
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
    w->blood_max[i] = 100.0f * d->mass / 75.0f;
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
    for (u32 s = 0; s < SK_COUNT; s++) w->skill[s][i] = d->skill[s];
    w->weapon[i] = d->weapon;
    w->weapon_q[i] = d->weapon_q;
    for (u32 s = 0; s < SLOT_COUNT; s++) w->armor[s][i] = d->armor[s];
    w->pause[i] = 0.5f + combat_roll(w, i, 0, 1);
    w->grid_dirty = true;
    combat_set_state(w, i, FS_READY, 0.0f);
    check_body(w, i);
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
    f32 skill = 0.25f * (d->skill[SK_ATTACK] + wpn) + 0.25f * (d->skill[SK_DEFENSE] + wpn) +
                0.15f * d->skill[SK_DODGE] + 0.2f * d->skill[SK_TOUGHNESS] + 0.1f * d->skill[SK_STRENGTH];
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
    return (skill + 0.25f * (dmg - 25.0f) + 25.0f * armor) * (0.6f + 0.4f * health);
}
