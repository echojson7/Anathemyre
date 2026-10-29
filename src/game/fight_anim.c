#include "fight_anim.h"

#include <string.h>

#define COLLAPSE_S 0.9f   // оседает на землю без сознания
#define KNEEL_S 1.1f      // опускается на колени
#define DRAW_S 0.85f      // достать или убрать оружие
#define SHEATHE_AFTER 2.5f  // бой стоит столько, прежде чем убирают оружие

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
        // Лицом вниз: ползущие и половина потерявших сознание; сбитые ударом — на спину
        if (st == FS_KNOCKED) f->prone = false;
        else if (st == FS_CRAWL) f->prone = true;
        else if ((st == FS_UNCONSCIOUS || st == FS_DEAD) && f->from_stand)
            f->prone = hashf((u32)(f->seed * 131.0f) + w->tick) < 0.5f;
        if (st == FS_DODGE) {
            u32 h = (u32)(f->seed * 71.0f) + w->tick * 7u;
            f->dodge_kind = (u8)(hashf(h) * DODGE_COUNT);
            if (f->dodge_kind >= DODGE_COUNT) f->dodge_kind = DODGE_BACK;
            f->dodge_side = hashf(h + 1u) < 0.5f ? 1.0f : -1.0f;
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

    // Показанная позиция: отброшенный ударом отлетает не за тик, а за доли секунды
    f32 tx = lerpf(f->prev_x, w->x[i], alpha), tz = lerpf(f->prev_z, w->z[i], alpha);
    if (st == FS_KNOCKED) {
        f->disp_x = approach(f->disp_x, tx, 7.0f, dt);
        f->disp_z = approach(f->disp_z, tz, 7.0f, dt);
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

    bool armed = !w->disarmed[i];
    bool fighting = st == FS_READY || st == FS_WINDUP || st == FS_STRIKE || st == FS_RECOVER || st == FS_BLOCK ||
                    st == FS_DODGE || st == FS_STAGGER;
    f->calm = w->halted ? f->calm + dt : 0.0f;
    bool calm = f->calm > SHEATHE_AFTER;
    f32 guard_t = !armed || calm ? 0.0f : (fighting ? 1.0f : (st == FS_RETREAT ? 0.6f : 0.0f));
    if (w->halted && !calm && st == FS_READY) guard_t = 0.35f;
    // Оружие достаёт сразу; в стойку встаёт, когда оно в руке
    f32 wield_t = armed && !calm && !fs_out(st) ? 1.0f : 0.0f;
    f32 rate = (st == FS_WINDUP || st == FS_BLOCK) ? 3.0f / DRAW_S : 1.0f / DRAW_S;
    f->wield = step_to(f->wield, wield_t, rate * dt);
    if (f->wield < 0.9f) guard_t *= f->wield / 0.9f;
    f->guard = approach(f->guard, guard_t, 5.0f, dt);
    f->block = approach(f->block, st == FS_BLOCK ? 1.0f : 0.0f, 14.0f, dt);
    f->dodge = approach(f->dodge, st == FS_DODGE ? 1.0f : 0.0f, 9.0f, dt);
    f->stagger = approach(f->stagger, st == FS_STAGGER ? 1.0f : 0.0f, 10.0f, dt);
    f->limp = approach(f->limp, st == FS_UNCONSCIOUS || st == FS_DEAD ? 1.0f : 0.0f, 3.0f, dt);

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
        a->aim = v3((rx * hc - rz * hs + w->aim_x[i] * ts) / p->width, ry / p->height, (rx * hs + rz * hc) / p->width);
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
