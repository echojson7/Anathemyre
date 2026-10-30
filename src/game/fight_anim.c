#include "fight_anim.h"

#include <string.h>

// Длительности клипов вида, с. Темп по тяжести: обмякшее тело падает почти свободно, живой
// притормаживает; падение сбитого (1.1) и подъём (2.0–2.2) задаёт бой (combat.c)
#define COLLAPSE_S 1.1f   // теряет сознание стоя: оседает на колени и валится
#define DIE_S 0.8f        // убит ударом стоя: падает мешком
#define BLEED_S 2.0f      // истёк кровью стоя: шатается, колено, бок
#define SINK_S 1.1f       // ноги не держат: оседает, ловит себя руками
#define TURN_S 1.2f       // со спины на живот, прежде чем ползти
#define KNEEL_S 1.6f      // опускается на колени
#define FADE_S 0.35f      // переход из прежнего клипа в новый
#define DROP_FALL_S 0.4f  // выроненное оружие падает из руки на землю
#define DRAW_S 0.85f      // достать или убрать оружие
#define SHEATHE_AFTER 2.5f  // бой стоит столько, прежде чем убирают оружие

_Static_assert((int)CHAR_FALL_FRONT == (int)FALL_FRONT && (int)CHAR_FALL_LEFT == (int)FALL_LEFT && (int)CHAR_FALL_RIGHT == (int)FALL_RIGHT,
               "стороны падения вида и боя");

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
    f->fade = 1.0f;
    f->fade_s = FADE_S;
    for (u32 p = 0; p < PART_COUNT; p++) f->hp_prev[p] = w->hp[p][i];
}

void fight_anim_pre_tick(FightAnim *f, const CombatWorld *w, u32 i)
{
    f->prev_x = w->x[i];
    f->prev_z = w->z[i];
    f->prev_h = w->heading[i];
}

// Какой клип всем телом показывать сейчас и на какой доле
static void clip_now(FightAnim *f, const CombatWorld *w, u32 i, f32 alpha)
{
    u8 st = w->state[i];
    u8 c = CLIP_NONE;
    f32 t = 0.0f;
    switch (st) {
    case FS_KNOCKED: c = CLIP_FALL; t = state_progress(w, i, alpha); break;
    case FS_GETUP: c = CLIP_GETUP; t = state_progress(w, i, alpha); break;
    case FS_PICKUP: c = CLIP_PICKUP; t = state_progress(w, i, alpha); break;
    case FS_SURRENDER: c = CLIP_KNEEL; t = f->vt / KNEEL_S; break;
    case FS_UNCONSCIOUS:
    case FS_DEAD: {
        // Стоя: смерть от удара, от потери крови или обморок; с колен (сдался) — оседает дальше;
        // лёжа — так и лежит
        bool bleed = st == FS_DEAD && w->blood[i] <= 0.0f;
        f32 dur = st == FS_UNCONSCIOUS ? COLLAPSE_S : bleed ? BLEED_S : DIE_S;
        if (f->from_kneel && f->vt < 0.6f * COLLAPSE_S) {
            c = CLIP_COLLAPSE;
            t = 0.4f + f->vt / COLLAPSE_S;
        } else if (f->from_stand && f->vt < dur) {
            c = st == FS_UNCONSCIOUS ? CLIP_COLLAPSE : bleed ? CLIP_BLEED : CLIP_DIE;
            t = f->vt / dur;
        } else {
            c = CLIP_LIE;
            t = f->clock;
        }
        break;
    }
    case FS_DOWN: c = CLIP_LIE; t = f->clock; break;
    case FS_CRAWL:
        if (f->from_stand && f->vt < SINK_S) {
            c = CLIP_SINK;
            t = f->vt / SINK_S;
        } else if (f->turn && f->vt < TURN_S) {
            c = CLIP_TURN;
            t = f->vt / TURN_S;
        } else {
            c = CLIP_CRAWL;
            t = f->phase * 2.6f + f->vt;
        }
        break;
    default: break;
    }
    f->clip = c;
    f->clip_t = t;
}

void fight_anim_update(FightAnim *f, const CombatWorld *w, u32 i, f32 alpha, f32 dt)
{
    u8 st = w->state[i];
    if (st != f->vst) {
        // Откуда: тот клип, что показывался, — из него новый вступает плавно
        f->pclip = f->clip;
        f->pclip_t = f->clip_t;
        f->pprone = f->prone;
        f->pside = f->side;
        f->fade = 0.0f;
        f->fade_s = f->vst == FS_SURRENDER ? 0.6f : FADE_S;
        // Ещё стоя: не лежал, или только начал падать, или почти встал
        bool lay = fs_lying(f->vst);
        f->from_stand = (!lay && f->vst != FS_SURRENDER) || (f->vst == FS_KNOCKED && f->clip_t < 0.25f) ||
                        (f->vst == FS_GETUP && f->clip_t > 0.8f);
        f->from_kneel = f->vst == FS_SURRENDER;
        // Ползти со спины: сначала перевернуться на живот
        f->turn = st == FS_CRAWL && lay && !f->prone;
        // На боку остаются только мёртвые: убитый ударом сбоку и истёкший кровью
        f->side = 0;
        if (st == FS_DEAD && f->from_stand && !w->prone[i]) {
            bool bleed = w->blood[i] <= 0.0f;
            u8 fd = w->fall[i];
            if (fd == FALL_LEFT || fd == FALL_RIGHT) f->side = fd == FALL_LEFT ? 1 : -1;
            else if (bleed) f->side = hashf((u32)(f->seed * 131.0f) + w->tick) < 0.5f ? 1 : -1;
        }
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
    f->clock += dt;
    f->prone = w->prone[i];
    f->fade = fminf(1.0f, f->fade + dt / fmaxf(f->fade_s, 0.05f));

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

    // Клип всем телом
    clip_now(f, w, i, alpha);

    // Оружие выпадает из рук у выбывших и обезоруженных, а у сбитого с ног — если удар его выбил (loose);
    // выроненное он поднимает сам: на середине подбора оно снова в руке
    bool lost = !armed || st == FS_UNCONSCIOUS || st == FS_DEAD || st == FS_SURRENDER ||
                (w->loose[i] && !(st == FS_PICKUP && f->clip_t > 0.55f));
    if (!f->dropped && lost) {
        f->dropped = true;
        f->drop_age = 0.0f;
        f32 h = w->heading[i];
        if (w->loose[i] && armed) {
            // Рядом, справа впереди: туда потом тянется рука при подборе
            f->drop_pos = v3(w->x[i] + sinf(h) * 0.55f - cosf(h) * 0.25f, 0.0f, w->z[i] + cosf(h) * 0.55f + sinf(h) * 0.25f);
            f->drop_yaw = h + 1.2f;
        } else {
            h += (hashf((u32)(f->seed * 7.0f) + w->tick) - 0.5f) * 1.5f;
            f->drop_pos = v3(w->x[i] + sinf(h) * 0.7f - cosf(h) * 0.3f, 0.0f, w->z[i] + cosf(h) * 0.7f + sinf(h) * 0.3f);
            f->drop_yaw = h + PI_F * 0.5f;
        }
    } else if (f->dropped && !lost) {
        f->dropped = false;
        f->wield = 1.0f;
    }
    if (f->dropped) f->wield = 0.0f;
    f->drop_age += dt;
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
        f32 sw = p->width * p->height;
        a->aim = v3((rx * hc - rz * hs + w->aim_x[i] * ts) / sw, ry / p->height, (rx * hs + rz * hc) / sw);
        a->aim_s = w->aim_s[i];
        a->aim_on = true;
        a->aim_stop = w->react[i] == REACT_BLOCK;
    }
    // Всем телом
    a->clip = f->clip;
    a->clip_t = f->clip_t;
    a->side = f->side;
    a->fall_dir = w->fall[i];
    a->dead = st == FS_DEAD;
    a->still = f->vt - (f->from_stand ? (w->blood[i] <= 0.0f ? BLEED_S : DIE_S) : 0.0f);
    a->prev_clip = f->pclip;
    a->prev_t = f->pclip_t;
    a->prev_prone = f->pprone;
    a->prev_side = f->pside;
    a->fade = f->fade;
    if (f->dropped) {
        // Летит из руки: от пояса вниз с ускорением, не дальше места, где ляжет
        f32 u = clampf(f->drop_age / DROP_FALL_S, 0.0f, 1.0f);
        f32 hx = sinf(h) * 0.25f - cosf(h) * 0.2f, hz = cosf(h) * 0.25f + sinf(h) * 0.2f;
        f32 fx = lerpf(f->disp_x + hx, f->drop_pos.x, u), fz = lerpf(f->disp_z + hz, f->drop_pos.z, u);
        *drop_pos = v3(origin.x + fx, floor_y + 0.03f + 0.9f * (1.0f - u * u), origin.z + fz);
        *drop_yaw = f->drop_yaw;
    }
    return f->dropped;
}

f32 fight_anim_contact(const CombatWorld *w, u32 i, u8 ev_kind)
{
    // Касание земли в клипах: сбит — сел на землю (вперёд — колени), убит ударом — таз о землю,
    // истёк кровью — колено, без сознания — колени, ноги отказали — колени
    u8 st = w->state[i];
    switch (ev_kind) {
    case EV_KNOCKDOWN:
        return w->fall[i] == FALL_FRONT ? 0.55f * 1.1f : 0.64f * 1.1f;
    case EV_FALL:
        return st == FS_KNOCKED ? (w->fall[i] == FALL_FRONT ? 0.55f * 1.1f : 0.64f * 1.1f) : 0.45f * SINK_S;
    case EV_KO:
        return 0.50f * COLLAPSE_S;
    case EV_DEATH:
        return w->blood[i] <= 0.0f ? 0.40f * BLEED_S : 0.55f * DIE_S;
    default:
        return 0.4f;
    }
}
