#include "fight_anim.h"

#include <string.h>

#include "../core/hash.h"

// Длительности клипов вида, с. Темп по тяжести: обмякшее тело падает почти свободно, живой
// притормаживает; падение сбитого (1.1) и подъём (2.0–2.2) задаёт бой (combat.c)
#define COLLAPSE_S 1.1f   // теряет сознание стоя: оседает на колени и валится
#define DIE_S 0.8f        // убит ударом стоя: падает мешком
#define BLEED_S 2.0f      // истёк кровью стоя: шатается, колено, бок
#define SINK_S 1.1f       // ноги не держат: оседает, ловит себя руками
#define TURN_S 1.2f       // со спины на живот, прежде чем ползти
#define KNEEL_S 1.6f      // опускается на колени
#define FADE_S 0.35f      // переход из прежнего клипа в новый
#define DRAW_S 0.6f       // достать или убрать оружие: основа, с, и прибавка за килограмм (DRAW_KG)
#define DRAW_KG 0.04f     // клинок 3 кг — 0.72 с, балка 18 кг — 1.3 с
#define DRAW_HURRY 1.8f   // в замахе и в блоке достаёт быстрее во столько раз
#define STRIKE_S 0.11f    // сам удар от верха замаха до цели: основа, с, и прибавка за килограмм (STRIKE_KG)
#define STRIKE_KG 0.012f  // клинок 3 кг — 0.15 с, балка 18 кг — 0.33 с
#define KNOCK_TRAVEL 0.7f // отброшенный пятится назад первые столько от падения
#define DODGE_LEAD 0.3f   // уворачивается за столько до удара; до того отступает в стойке
#define SHEATHE_AFTER 2.5f  // бой стоит столько, прежде чем убирают оружие

_Static_assert((int)CHAR_FALL_FRONT == (int)FALL_FRONT && (int)CHAR_FALL_LEFT == (int)FALL_LEFT && (int)CHAR_FALL_RIGHT == (int)FALL_RIGHT,
               "стороны падения вида и боя");

// Боевой шаг: в стойке стопа стоит на земле, пока тело не уйдёт от её места; тогда она переставляется.
// Вбок и назад первой идёт нога со стороны движения, вторая подтягивается; ноги не скрещиваются
#define STEP_S     0.22f  // шаг, с
#define STEP_FAR   0.13f  // стопа дальше этого от места в стойке — шагает
#define STEP_TIDY  0.05f  // стоит, а стопа не на месте больше этого ...
#define STEP_TIDY_S 0.5f  // ... столько секунд — поправляет
#define STEP_LIFT  0.07f  // подъём стопы в шаге, м

static f32 approach(f32 x, f32 target, f32 rate, f32 dt) { return x + (target - x) * fminf(1.0f, rate * dt); }

static f32 step_to(f32 x, f32 target, f32 step) { return x + clampf(target - x, -step, step); }

// Вес оружия в руке, кг (без оружия — 0)
static f32 weapon_kg(const CombatWorld *w, u32 i) { return w->disarmed[i] ? 0.0f : g_weapons[w->weapon[i]].weight; }

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
    f->fade = 1.0f;
    f->fade_s = FADE_S;
    for (u32 p = 0; p < PART_COUNT; p++) f->hp_prev[p] = w->hp[p][i];
}

void fight_anim_settle(FightAnim *f, const CombatWorld *w, u32 i, f32 seed)
{
    fight_anim_reset(f, w, i, seed);
    // Две секунды вида без хода боя: оружие достано, выпавшее легло, стойка встала
    for (u32 k = 0; k < 60; k++) fight_anim_update(f, w, i, 0.0f, 1.0f / 30.0f);
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
    f->clock += dt;
    // Как лежит, решает бой: ничком, на спине или на боку (убитый сбоку, истёкший кровью).
    // От этого, где лежит тело и как его обходят
    f->prone = w->prone[i] == LIE_FRONT;
    f->side = w->prone[i] == LIE_SIDE_L ? 1 : w->prone[i] == LIE_SIDE_R ? -1 : 0;
    f->fade = fminf(1.0f, f->fade + dt / fmaxf(f->fade_s, 0.05f));

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
    // Тяжёлое достаёт дольше; в замахе — быстрее и в любом случае до начала самого удара
    f32 kg = weapon_kg(w, i), draw_s = DRAW_S + DRAW_KG * kg, strike_s = STRIKE_S + STRIKE_KG * kg;
    f32 rate = (st == FS_WINDUP || st == FS_BLOCK) ? DRAW_HURRY / draw_s : 1.0f / draw_s;
    if (st == FS_WINDUP) {
        f32 left = (1.0f - state_progress(w, i, alpha)) * w->dur[i] - strike_s;
        rate = fmaxf(rate, (1.0f - f->wield) / fmaxf(left, 0.05f));
    }
    f->wield = step_to(f->wield, wield_t, rate * dt);
    // Замах и удар: бой засчитывает попадание в конце замаха, поэтому сам удар (0..1 → 1..2) идёт в его
    // последние strike_s секунд и доходит до цели в миг попадания; длина удара — по весу оружия. После
    // попадания (FS_STRIKE) оружие на миг остаётся у цели, затем возврат
    if (st == FS_WINDUP) {
        f32 wt = w->dur[i], e = state_progress(w, i, alpha) * wt, raise = fmaxf(wt - strike_s, 0.4f * wt);
        f->swing = e < raise ? e / raise : 1.0f + fminf((e - raise) / strike_s, 1.0f);
    } else if (st == FS_STRIKE) {
        f->swing = fminf(2.0f, fmaxf(f->swing, 1.0f) + dt / strike_s);
    } else if (st == FS_RECOVER) {
        f->swing = 2.0f + state_progress(w, i, alpha);
    } else {
        f->swing = 0.0f;
    }
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

    // Клип всем телом
    clip_now(f, w, i, alpha);

    // Оружие на земле — вещью мира (world.c, docs/physics.md): в руке его нет, пока не поднимет
    bool lost = w->gitem[i] != COMBAT_IN_HAND;
    if (f->dropped && !lost) f->wield = 1.0f;
    f->dropped = lost;
    if (f->dropped) f->wield = 0.0f;
}

bool fight_anim_pose(const FightAnim *f, const CombatWorld *w, u32 i, f32 alpha, V3 origin, f32 floor_y, f32 time,
                     CharPose *p, CharAction *a)
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
    f32 swing = f->swing;
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
    a->clip = f->clip;
    a->clip_t = f->clip_t;
    a->side = f->side;
    a->fall_dir = w->fall[i];
    // Убитый стоя ложится так, как решил бой: клип смерти по этому, а не по удару
    if (st == FS_DEAD)
        a->fall_dir = f->side > 0 ? FALL_LEFT : f->side < 0 ? FALL_RIGHT : f->prone ? FALL_FRONT : FALL_BACK;
    a->dead = st == FS_DEAD;
    a->still = f->vt - (f->from_stand ? (w->blood[i] <= 0.0f ? BLEED_S : DIE_S) : 0.0f);
    a->prev_clip = f->pclip;
    a->prev_t = f->pclip_t;
    a->prev_prone = f->pprone;
    a->prev_side = f->pside;
    a->fade = f->fade;
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
