#include "fight_anim.h"

#include <string.h>

#define COLLAPSE_S 0.9f   // оседает на землю без сознания
#define KNEEL_S 1.1f      // опускается на колени
#define DRAW_S 0.85f      // достать или убрать оружие
#define DRAW_HEAVY_S 1.2f // тяжёлое снять со спины
#define KNOCK_TRAVEL 0.7f // отброшенный пятится назад первые столько от падения
#define DODGE_LEAD 0.3f   // уворачивается за столько до удара; до того отступает в стойке
#define SHEATHE_AFTER 2.5f  // бой стоит столько, прежде чем убирают оружие

// Боевой шаг: в стойке стопа стоит на земле, пока тело не уйдёт от её места; тогда она переставляется.
// Вбок и назад первой идёт нога со стороны движения, вторая подтягивается; ноги не скрещиваются
#define STEP_S     0.22f  // шаг, с
#define STEP_FAR   0.13f  // стопа дальше этого от места в стойке — шагает
#define STEP_TIDY  0.05f  // стоит, а стопа не на месте больше этого ...
#define STEP_TIDY_S 0.5f  // ... столько секунд — поправляет
#define STEP_LIFT  0.07f  // подъём стопы в шаге, м

static f32 hashf(u32 x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (f32)(x & 0xFFFFFF) / 16777215.0f;
}

static f32 wrap_angle(f32 a)
{
    while (a > PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

static f32 approach(f32 x, f32 target, f32 rate, f32 dt) { return x + (target - x) * fminf(1.0f, rate * dt); }

static f32 step_to(f32 x, f32 target, f32 step) { return x + clampf(target - x, -step, step); }

static f32 smooth01(f32 t)
{
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Множители позы: рост и ширина, как в fight_anim_pose
static f32 body_h(const CombatWorld *w, u32 i) { return w->height[i] / 1.8f; }
static f32 body_w(const FightAnim *f) { return 1.0f + 0.08f * sinf(f->seed * 3.17f + 1.0f); }

// Место стопы в стойке, координаты боя
static void stance_slot(f32 x, f32 z, f32 hs, f32 hc, f32 sw, u32 side, f32 *sx, f32 *sz)
{
    V3 j = g_body_bones[side ? BONE_FOOT_R : BONE_FOOT_L].joint;
    f32 mx = side ? j.x - CHAR_STANCE_OUT_R : j.x + CHAR_STANCE_OUT_L, mz = side ? -CHAR_STANCE_Z : CHAR_STANCE_Z;
    *sx = x + sw * (mx * hc + mz * hs);
    *sz = z + sw * (-mx * hs + mz * hc);
}

// Стопы в стойке: стоят, пока тело не уйдёт от их места, затем переставляются по одной.
// h — показанный разворот, v — скорость тела (шаг ставится с упреждением)
static void feet_update(FightAnim *f, const CombatWorld *w, u32 i, f32 h, f32 vx, f32 vz, bool on, f32 dt)
{
    f32 sh = body_h(w, i), sw = body_w(f) * sh;
    f32 hs = sinf(h), hc = cosf(h);
    f32 sx[2], sz[2];
    for (u32 s = 0; s < 2; s++) stance_slot(f->disp_x, f->disp_z, hs, hc, sw, s, &sx[s], &sz[s]);
    if (!on) {
        for (u32 s = 0; s < 2; s++) {
            f->foot_x[s] = sx[s];
            f->foot_z[s] = sz[s];
            f->step_t[s] = -1.0f;
            f->feet_off[s] = v3(0, 0, 0);
        }
        f->untidy = 0.0f;
        return;
    }
    f32 lead = STEP_S * 0.5f;
    f32 err[2];
    for (u32 s = 0; s < 2; s++) {
        f32 tx = sx[s] + vx * lead, tz = sz[s] + vz * lead;
        if (f->step_t[s] >= 0.0f) {
            // Отставшая нога шагает быстрее
            f32 dx = tx - f->from_x[s], dz = tz - f->from_z[s];
            f32 rate = fmaxf(1.0f, sqrtf(dx * dx + dz * dz) / 0.35f);
            f->step_t[s] += rate * dt / STEP_S;
            if (f->step_t[s] >= 1.0f) {
                f->step_t[s] = -1.0f;
                f->foot_x[s] = tx;
                f->foot_z[s] = tz;
                f->steps++;
            } else {
                f32 e = smooth01(f->step_t[s]);
                f->foot_x[s] = lerpf(f->from_x[s], tx, e);
                f->foot_z[s] = lerpf(f->from_z[s], tz, e);
            }
        }
        f32 dx = f->foot_x[s] - sx[s], dz = f->foot_z[s] - sz[s];
        err[s] = sqrtf(dx * dx + dz * dz);
    }
    // Вторая нога трогается, когда первая прошла полшага
    bool busy = (f->step_t[0] >= 0.0f && f->step_t[0] < 0.5f) || (f->step_t[1] >= 0.0f && f->step_t[1] < 0.5f);
    u32 s = err[1] > err[0] ? 1u : 0u;
    if (f->step_t[s] >= 0.0f) s ^= 1u;
    bool idle = f->step_t[0] < 0.0f && f->step_t[1] < 0.0f;
    f->untidy = idle && fmaxf(err[0], err[1]) > STEP_TIDY ? f->untidy + dt : 0.0f;
    if (!busy && f->step_t[s] < 0.0f && (err[s] > STEP_FAR || (f->untidy > STEP_TIDY_S && err[s] > STEP_TIDY))) {
        f->step_t[s] = 0.0f;
        f->from_x[s] = f->foot_x[s];
        f->from_z[s] = f->foot_z[s];
        f->untidy = 0.0f;
    }
    for (u32 k = 0; k < 2; k++) {
        f32 dx = f->foot_x[k] - sx[k], dz = f->foot_z[k] - sz[k];
        f32 lift = f->step_t[k] >= 0.0f ? STEP_LIFT * sinf(PI_F * f->step_t[k]) : 0.0f;
        f->feet_off[k] = v3((dx * hc - dz * hs) / sw, lift / sh, (dx * hs + dz * hc) / sw);
    }
}

// Доля пройденного состояния с учётом времени между тиками
static f32 state_progress(const CombatWorld *w, u32 i, f32 alpha)
{
    if (w->dur[i] <= 0.0f) return 1.0f;
    return clampf(1.0f - (w->timer[i] - alpha * COMBAT_DT) / w->dur[i], 0.0f, 1.0f);
}

void fight_anim_reset(FightAnim *f, const CombatWorld *w, u32 i, f32 seed)
{
    memset(f, 0, sizeof *f);
    f->prev_x = f->disp_x = w->x[i];
    f->prev_z = f->disp_z = w->z[i];
    f->prev_h = w->heading[i];
    f->seed = seed;
    f->phase = hashf((u32)(seed * 977.0f));
    f->vst = w->state[i];
    for (u32 p = 0; p < PART_COUNT; p++) f->hp_prev[p] = w->hp[p][i];
}

void fight_anim_pre_tick(FightAnim *f, const CombatWorld *w, u32 i)
{
    f->prev_x = w->x[i];
    f->prev_z = w->z[i];
    f->prev_h = w->heading[i];
}

void fight_anim_update(FightAnim *f, const CombatWorld *w, u32 i, f32 alpha, f32 dt)
{
    u8 st = w->state[i];
    if (st != f->vst) {
        f->from_stand = !fs_lying(f->vst) && f->vst != FS_SURRENDER;
        if (st == FS_DODGE) {
            u32 h = (u32)(f->seed * 71.0f) + w->tick * 7u;
            f->dodge_kind = (u8)(hashf(h) * DODGE_COUNT);
            if (f->dodge_kind >= DODGE_COUNT) f->dodge_kind = DODGE_BACK;
            f->dodge_side = hashf(h + 1u) < 0.5f ? 1.0f : -1.0f;
        }
        if (st == FS_KNOCKED) {
            f->knock_x = f->disp_x;
            f->knock_z = f->disp_z;
        }
        if (st == FS_WINDUP && w->target[i] >= 0) {
            f->lock_x = w->x[w->target[i]];
            f->lock_z = w->z[w->target[i]];
        }
        f->vst = st;
        f->vt = 0.0f;
    } else {
        f->vt += dt;
    }
    // Ничком или на спину решает бой: от этого, где лежит тело и как его обходят
    f->prone = w->prone[i] != 0;

    // Показанная позиция: отброшенный ударом не отлетает за тик, а пятится, пока валится
    f32 tx = lerpf(f->prev_x, w->x[i], alpha), tz = lerpf(f->prev_z, w->z[i], alpha);
    if (st == FS_KNOCKED) {
        f32 k = smooth01(state_progress(w, i, alpha) / KNOCK_TRAVEL);
        f->disp_x = lerpf(f->knock_x, tx, k);
        f->disp_z = lerpf(f->knock_z, tz, k);
    } else {
        f->disp_x = tx;
        f->disp_z = tz;
    }

    // Шаг по смещению за тик
    f32 vx = (w->x[i] - f->prev_x) / COMBAT_DT, vz = (w->z[i] - f->prev_z) / COMBAT_DT;
    f32 speed = sqrtf(vx * vx + vz * vz);
    bool moving = speed > 0.15f && !fs_lying(st) && st != FS_SURRENDER;
    f->speed = approach(f->speed, moving ? speed : 0.0f, 8.0f, dt);
    f->walk = approach(f->walk, moving ? clampf(speed / 0.9f, 0.4f, 1.0f) : 0.0f, 6.0f, dt);
    if (speed > 0.15f) f->move_dir = wrap_angle(atan2f(vx, vz) - w->heading[i]);
    if (moving) f->phase += f->speed * dt / char_stride(f->speed);
    f->phase -= floorf(f->phase);
    f32 shown_h = f->prev_h + wrap_angle(w->heading[i] - f->prev_h) * alpha;

    bool armed = !w->disarmed[i];
    bool fighting = st == FS_READY || st == FS_WINDUP || st == FS_STRIKE || st == FS_RECOVER || st == FS_BLOCK ||
                    st == FS_DODGE || st == FS_STAGGER;
    f->calm = w->halted ? f->calm + dt : 0.0f;
    bool calm = f->calm > SHEATHE_AFTER;
    f32 guard_t = !armed || calm ? 0.0f : (fighting ? 1.0f : (st == FS_RETREAT ? 0.6f : 0.0f));
    if (w->halted && !calm && st == FS_READY) guard_t = 0.35f;
    // Оружие достаёт сразу; в стойку встаёт, когда оно в руке
    f32 wield_t = armed && !calm && !fs_out(st) ? 1.0f : 0.0f;
    f32 draw_s = g_weapons[w->weapon[i]].wclass == WC_HEAVY ? DRAW_HEAVY_S : DRAW_S;
    f32 rate = (st == FS_WINDUP || st == FS_BLOCK) ? 3.0f / draw_s : 1.0f / draw_s;
    f->wield = step_to(f->wield, wield_t, rate * dt);
    if (f->wield < 0.9f) guard_t *= f->wield / 0.9f;
    f->guard = approach(f->guard, guard_t, 5.0f, dt);
    f->block = approach(f->block, st == FS_BLOCK ? 1.0f : 0.0f, 14.0f, dt);
    // Уворот держится до удара и ещё 0.2 с после: уходит с линии в последний момент
    bool dodge_now = st == FS_DODGE && w->timer[i] - alpha * COMBAT_DT - 0.2f < DODGE_LEAD;
    f->dodge = approach(f->dodge, dodge_now ? 1.0f : 0.0f, 12.0f, dt);
    f->stagger = approach(f->stagger, st == FS_STAGGER ? 1.0f : 0.0f, 10.0f, dt);
    f->limp = approach(f->limp, st == FS_UNCONSCIOUS || st == FS_DEAD ? 1.0f : 0.0f, 3.0f, dt);

    // Ноги в стойке переставляются боевым шагом; отходящий идёт обычным шагом
    bool upright = !fs_lying(st) && !fs_out(st);
    // Издали к цели — обычным шагом, оружие наготове
    f->feet = approach(f->feet, upright && st != FS_RETREAT && f->speed < 1.3f ? f->guard : 0.0f, 8.0f, dt);
    feet_update(f, w, i, shown_h, vx, vz, upright && f->feet > 0.01f, dt);
    if (f->feet > 0.5f) {
        // Фаза шага по переставленным ногам (половина цикла — шаг раньше начатой ноги): звук шагов — в касание
        f32 t = fmaxf(f->step_t[0], f->step_t[1]);
        f->phase = 0.5f * (f32)(f->steps & 1u) + 0.5f * fmaxf(t, 0.0f);
    }

    // Отдача: по тому, сколько и где убыло здоровья
    f32 worst = 0.0f;
    u8 part = PART_CHEST;
    for (u32 p = 0; p < PART_COUNT; p++) {
        f32 d = f->hp_prev[p] - w->hp[p][i];
        if (d > worst) {
            worst = d;
            part = (u8)p;
        }
        f->hp_prev[p] = w->hp[p][i];
    }
    if (worst > 1.0f && !fs_lying(st)) {
        f->flinch = fmaxf(f->flinch, clampf(worst / 22.0f, 0.45f, 1.0f));
        f->flinch_part = part;
    }
    f->flinch = fmaxf(0.0f, f->flinch - 3.5f * dt);

    // Оружие выпадает из рук у выбывших и обезоруженных
    if (!f->dropped && (!armed || st == FS_UNCONSCIOUS || st == FS_DEAD || st == FS_SURRENDER)) {
        f->dropped = true;
        f32 h = w->heading[i] + (hashf((u32)(f->seed * 7.0f) + w->tick) - 0.5f) * 1.5f;
        f->drop_pos = v3(w->x[i] + sinf(h) * 0.7f - cosf(h) * 0.3f, 0.0f, w->z[i] + cosf(h) * 0.7f + sinf(h) * 0.3f);
        f->drop_yaw = h + PI_F * 0.5f;
        f->wield = 0.0f;
    }
}

bool fight_anim_pose(const FightAnim *f, const CombatWorld *w, u32 i, f32 alpha, V3 origin, f32 floor_y, f32 time,
                     CharPose *p, CharAction *a, V3 *drop_pos, f32 *drop_yaw)
{
    u8 st = w->state[i];
    f32 h = f->prev_h + wrap_angle(w->heading[i] - f->prev_h) * alpha;
    *p = (CharPose){
        .pos = v3(origin.x + f->disp_x, floor_y, origin.z + f->disp_z), .heading = h,
        .phase = f->phase, .walk = f->walk, .run = char_run_from_speed(f->speed), .speed = f->speed,
        .time = time, .seed = f->seed, .height = w->height[i] / 1.8f,
        .width = 1.0f + 0.08f * sinf(f->seed * 3.17f + 1.0f), .move_dir = f->move_dir,
        .weapon = f->dropped ? 0 : (u8)(w->weapon[i] + 1),
    };
    f32 swing = 0.0f;
    if (st == FS_WINDUP) swing = state_progress(w, i, alpha);
    else if (st == FS_STRIKE) swing = 1.0f + state_progress(w, i, alpha);
    else if (st == FS_RECOVER) swing = 2.0f + state_progress(w, i, alpha);
    *a = (CharAction){
        .guard = f->guard, .attack = w->attack[i], .swing = swing, .block = f->block,
        .dodge = f->dodge, .dodge_kind = f->dodge_kind, .dodge_side = f->dodge_side, .stagger = f->stagger,
        .flinch = f->flinch, .flinch_part = f->flinch_part, .wield = f->wield,
        .prone = f->prone, .limp = f->limp, .severed = w->severed[i],
        .steps = true, .feet = f->feet, .feet_off = { f->feet_off[0], f->feet_off[1] },
    };
    // Удар ведётся в точку на цели, куда метит бой: туда же, где он ищет, во что попало
    i32 t = w->target[i];
    if (swing > 0.0f && t >= 0) {
        bool dodged = w->react[i] == REACT_DODGE;
        f32 ts = w->height[t] / 1.8f;
        f32 rx = origin.x + (dodged ? f->lock_x : w->x[t]) - p->pos.x;
        f32 rz = origin.z + (dodged ? f->lock_z : w->z[t]) - p->pos.z;
        f32 ry = fs_lying(w->state[t]) ? 0.25f : w->aim_y[i] * ts;
        f32 hs = sinf(h), hc = cosf(h);
        f32 sw = p->width * p->height;
        a->aim = v3((rx * hc - rz * hs + w->aim_x[i] * ts) / sw, ry / p->height, (rx * hs + rz * hc) / sw);
        a->aim_s = w->aim_s[i];
        a->aim_on = true;
        a->aim_stop = w->react[i] == REACT_BLOCK;
    }
    // Всем телом
    switch (st) {
    case FS_KNOCKED:
        a->clip = CLIP_FALL;
        a->clip_t = state_progress(w, i, alpha);
        break;
    case FS_DOWN:
        a->clip = CLIP_LIE;
        a->clip_t = time;
        break;
    case FS_GETUP:
        a->clip = CLIP_GETUP;
        a->clip_t = state_progress(w, i, alpha);
        break;
    case FS_UNCONSCIOUS:
    case FS_DEAD:
        if (f->from_stand && f->vt < COLLAPSE_S) {
            a->clip = CLIP_COLLAPSE;
            a->clip_t = f->vt / COLLAPSE_S;
        } else {
            a->clip = CLIP_LIE;
            a->clip_t = time;
        }
        break;
    case FS_CRAWL:
        if (f->from_stand && f->vt < COLLAPSE_S) {
            a->clip = CLIP_COLLAPSE;
            a->clip_t = f->vt / COLLAPSE_S;
        } else {
            a->clip = CLIP_CRAWL;
            a->clip_t = f->phase * 2.6f + time * 0.0f + f->vt;
        }
        break;
    case FS_SURRENDER:
        a->clip = CLIP_KNEEL;
        a->clip_t = f->vt / KNEEL_S;
        break;
    default:
        break;
    }
    if (f->dropped) {
        *drop_pos = v3(origin.x + f->drop_pos.x, floor_y + 0.03f, origin.z + f->drop_pos.z);
        *drop_yaw = f->drop_yaw;
    }
    return f->dropped;
}
