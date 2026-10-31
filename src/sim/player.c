#include "player.h"

#include <string.h>

#include "anim.h"
#include "../core/hash.h"

#define ARRIVE_DIST 0.15f
#define MARKER_S    1.2f
#define ACCEL       3.0f   // разгон, м/с²
#define BRAKE       6.0f   // торможение, м/с²
#define ARRIVE_DEC  2.5f   // мягкое торможение к цели, м/с²
#define LAT_ACC     4.5f   // предел бокового ускорения в повороте, м/с² (~0.45 g, живой на бегу берёт 0.3–0.5 g)
#define TURN_K      3.0f   // руление: поворот скорости, рад/с на радиан отклонения от цели
#define TURN_MOVE   6.0f   // предел поворота скорости на малом ходу, рад/с
#define TURN_BODY   5.5f   // разворот тела на месте, рад/с: 180° за ~0.6 с
#define TURN_TRACK  10.0f  // на ходу тело следует за направлением шага
#define PIVOT_SPEED 1.3f   // темп, до которого сбрасывает перед крутым разворотом, м/с
#define REST_AFTER  14.0f  // поза отдыха не раньше, чем постоял столько (+ до REST_AFTER случайно), с
#define REST_HURRY  1.3f   // позвали: встаёт и садится назад быстрее спокойного
#define STRIDE_ROOM 1.5f   // за столько до начала торможения шаги начинают подгоняться под остановку, м
#define STRIDE_RATE 0.8f   // как быстро меняется длина шага, доля в секунду

const f32 g_gait_speed[GAIT_COUNT] = { 1.6f, 3.2f, 4.8f };

// Сглаживание к цели за время tau, с
static f32 follow(f32 x, f32 goal, f32 tau, f32 dt) { return x + (goal - x) * fminf(1.0f, dt / tau); }

// Фаза шага, которую тело пройдёт обычными шагами за последние u метров до остановки (у цели за ARRIVE_DIST,
// скорость по ARRIVE_DEC и не выше нынешней), циклов
static f32 stop_phase(const Player *p, f32 u)
{
    const u32 n = 16;
    f32 ph = 0.0f, du = u / (f32)n;
    for (u32 i = 0; i < n; i++) {
        f32 v = fminf(p->speed, fmaxf(0.4f, sqrtf(2.0f * ARRIVE_DEC * (((f32)i + 0.5f) * du + ARRIVE_DIST))));
        f32 run = fminf(p->run, char_run_from_speed(v));
        if (p->gait != GAIT_WALK) run = fminf(p->run, fmaxf(run, (p->gait == GAIT_RUN ? 0.6f : 0.5f) * smooth01((v - 1.0f) / 0.6f)));
        ph += du / char_stride_run(v, run);
    }
    return ph;
}

f32 player_stride(const Player *p) { return 1.0f + (f32)p->stride / 400.0f; }

// Какой множитель шага нужен, чтобы встать у цели в точке покоя; hold — держать прежний (у самой цели
// остаток фазы мал и расчёт неустойчив)
static f32 stride_goal(const Player *p, bool *hold)
{
    *hold = false;
    if (!p->moving || p->speed < 0.3f) return 1.0f;
    f32 dx = p->target.x - p->pos.x, dz = p->target.z - p->pos.z;
    f32 u = sqrtf(dx * dx + dz * dz) - ARRIVE_DIST;  // путь до остановки
    f32 zone = p->speed * p->speed / (2.0f * ARRIVE_DEC) + STRIDE_ROOM;
    if (u <= 0.0f || u >= zone) return 1.0f;
    f32 need = stop_phase(p, u);  // столько фазы до цели обычными шагами
    if (need < 0.2f) {
        *hold = true;
        return 1.0f;
    }
    // Доля цикла до следующей точки покоя; к остановке фаза ещё набегает ~0.1 (торможение, удержание шага у цели)
    f32 r = p->phase - (CHAR_REST_PHASE - 0.11f);
    r = 0.5f - (r - floorf(r * 2.0f) * 0.5f);
    // Точка покоя, до которой шаг меняется меньше всего: на столько же раз длиннее или короче
    f32 k = fmaxf(0.0f, floorf((need - r) * 2.0f));
    f32 f0 = need / (r + 0.5f * k), f1 = need / (r + 0.5f * (k + 1.0f));
    f32 f = fabsf(logf(f0)) < fabsf(logf(f1)) ? f0 : f1;
    return lerpf(1.0f, clampf(f, 0.75f, 1.3f), smooth01((zone - u) / STRIDE_ROOM));
}

void player_init(Player *p, V3 pos, f32 heading)
{
    // Нули целиком, с байтами выравнивания: тело идёт в сохранение и в сумму мира
    memset(p, 0, sizeof *p);
    p->pos = p->target = pos;
    p->heading = p->move_yaw = heading;
    p->gait = GAIT_RUN;
    p->vmax = g_gait_speed[GAIT_RUN];
    // Зерно поз от места появления: одно и то же при каждом запуске
    u32 bx, bz;
    memcpy(&bx, &pos.x, 4);
    memcpy(&bz, &pos.z, 4);
    p->rest_seed = bx * 2654435761u ^ bz * 2246822519u;
}

void player_rest(Player *p, u8 kind, f32 hold)
{
    if (kind == REST_NONE || kind >= REST_COUNT) return;
    p->rest = kind;
    p->rest_ph = RP_ENTER;
    p->rest_t = 0.0f;
    p->rest_hold = hold;
}

// Поза отдыха. true — тело занято позой и не идёт
static bool rest_update(Player *p, f32 dt)
{
    bool called = p->moving || p->ctrl != CTRL_FREE;
    if (p->rest == REST_NONE) {
        // Постоял — садится или скрещивает руки; выбор по весам из g_rest
        u32 h = p->rest_seed + p->rest_n * 0x9e3779b9u;
        if (called || p->idle < REST_AFTER * (1.0f + hashf(h))) return false;
        f32 sum = 0.0f;
        for (u32 k = 1; k < REST_COUNT; k++) sum += g_rest[k].weight;
        f32 pick = hashf(h + 1u) * sum;
        u8 kind = REST_COUNT - 1;
        for (u32 k = 1; k < REST_COUNT; k++) {
            pick -= g_rest[k].weight;
            if (pick <= 0.0f) {
                kind = (u8)k;
                break;
            }
        }
        const RestDef *d = &g_rest[kind];
        player_rest(p, kind, lerpf(d->hold_min, d->hold_max, hashf(h + 2u)));
    }
    const RestDef *d = &g_rest[p->rest];
    f32 hurry = called ? REST_HURRY : 1.0f;
    switch (p->rest_ph) {
    case RP_ENTER:
        // Позвали на входе — встаёт тем же путём назад, а не садится до конца
        if (called) {
            p->rest_ph = RP_BACK;
            break;
        }
        p->rest_t += dt;
        if (p->rest_t >= d->enter_s) {
            p->rest_ph = RP_HOLD;
            p->rest_t = 0.0f;
        }
        break;
    case RP_HOLD:
        p->rest_t += dt;
        if (called || p->rest_t >= p->rest_hold) {
            p->rest_ph = RP_EXIT;
            p->rest_t = 0.0f;
        }
        break;
    case RP_EXIT:
        p->rest_t += dt * hurry;
        break;
    default:
        p->rest_t -= dt * hurry;
        break;
    }
    if ((p->rest_ph == RP_EXIT && p->rest_t >= d->exit_s) || (p->rest_ph == RP_BACK && p->rest_t <= 0.0f)) {
        p->rest = REST_NONE;
        p->rest_ph = RP_ENTER;
        p->rest_t = 0.0f;
        p->rest_n++;
        p->idle = 0.0f;
        return false;
    }
    return true;
}

void player_order_move(Player *p, V3 target)
{
    p->target = target;
    p->moving = true;
    p->marker = MARKER_S;
}

void player_update(Player *p, f32 dt, GroundFn ground)
{
    if (dt <= 0.0f) return;
    // Сидит или встаёт: стоит на месте, пока поза не кончится
    if (rest_update(p, dt) && p->ctrl == CTRL_FREE) {
        p->marker = fmaxf(0.0f, p->marker - dt);
        p->speed = p->turn_rate = 0.0f;
        p->walk = follow(p->walk, 0.0f, 0.15f, dt);
        p->look = follow(p->look, 0.0f, 0.12f, dt);
        p->lean_fwd = follow(p->lean_fwd, 0.0f, 0.15f, dt);
        p->lean_side = follow(p->lean_side, 0.0f, 0.12f, dt);
        if (p->rest_ph == RP_HOLD) p->idle += dt;
        return;
    }
    if (p->ctrl == CTRL_FIGHT) return;
    p->marker = fmaxf(0.0f, p->marker - dt);
    f32 dx = p->target.x - p->pos.x, dz = p->target.z - p->pos.z;
    f32 d = sqrtf(dx * dx + dz * dz);
    if (p->moving && d < ARRIVE_DIST) p->moving = false;
    f32 want = atan2f(dx, dz);

    f32 goal = 0.0f, yaw0 = p->move_yaw, v0 = p->speed;
    if (p->moving) {
        // Почти стоит: направление шага свободно, сразу на цель; тело догоняет поворотом на месте
        if (p->speed < 0.3f) p->move_yaw = want;
        f32 off = fabsf(wrap_angle(want - p->move_yaw));
        // Темп: предел отряда; к цели плавно сбавляет, но доходит шагом, а не крадётся
        goal = fminf(p->vmax, fmaxf(0.4f, sqrtf(2.0f * ARRIVE_DEC * d)));
        // Крутой поворот: сбрасывает темп, чтобы пройти дугу малого радиуса, и не встаёт
        f32 pivot = PIVOT_SPEED * clampf(d, 0.3f, 1.0f);
        goal = lerpf(goal, fminf(goal, pivot), smooth01((off - 0.4f) / 1.4f));
        // Со старта трогается, когда до цели осталось меньше ~100° поворота тела
        f32 face = fabsf(wrap_angle(p->move_yaw - p->heading));
        goal *= 1.0f - smooth01((face - 0.6f) / 1.1f);
        // Руление плавное: малую поправку курса растягивает на шаг-другой, а не берёт самой тугой дугой.
        // Поворот скорости — не быстрее, чем позволяет боковое ускорение
        f32 err = wrap_angle(want - p->move_yaw);
        f32 wmax = fminf(TURN_MOVE, LAT_ACC / fmaxf(p->speed, 0.1f));
        f32 rate = clampf(err * TURN_K, -wmax, wmax);
        f32 step = rate * dt;
        if (fabsf(step) > fabsf(err)) step = err;
        p->move_yaw = wrap_angle(p->move_yaw + step);
    }
    p->speed += clampf(goal - p->speed, -BRAKE * dt, ACCEL * dt);
    p->pos.x += sinf(p->move_yaw) * p->speed * dt;
    p->pos.z += cosf(p->move_yaw) * p->speed * dt;
    p->pos.y = ground(p->pos.x, p->pos.z);

    // Тело: на ходу смотрит по шагу, на месте разворачивается к цели
    f32 h0 = p->heading;
    f32 tw = (p->speed < 0.5f ? TURN_BODY : TURN_TRACK) * dt;
    p->heading = wrap_angle(p->heading + clampf(wrap_angle(p->move_yaw - p->heading), -tw, tw));
    p->turn_rate = wrap_angle(p->heading - h0) / dt;

    // Наклон по ускорению: вперёд на разгоне, назад на торможении, внутрь поворота
    f32 a_fwd = (p->speed - v0) / dt;
    f32 a_lat = p->speed * wrap_angle(p->move_yaw - yaw0) / dt;
    p->lean_fwd = follow(p->lean_fwd, clampf(0.6f * atanf(a_fwd / 9.81f), -0.2f, 0.15f), 0.15f, dt);
    // Крен заметен на трусце и беге; шагом человек поворачивает почти прямо
    f32 bank = 0.7f * clampf((p->speed - 1.0f) / 3.0f, 0.15f, 1.0f);
    p->lean_side = follow(p->lean_side, clampf(bank * atanf(a_lat / 9.81f), -0.3f, 0.3f), 0.12f, dt);
    // Голова поворачивает к цели раньше тела
    f32 look = p->moving ? 0.7f * clampf(wrap_angle(want - p->heading), -1.1f, 1.1f) : 0.0f;
    p->look = follow(p->look, look, 0.12f, dt);

    // Ноги: шагают по скорости; разворот на месте — переступанием, а не скольжением
    f32 still = 1.0f - clampf(p->speed, 0.0f, 1.0f);
    f32 step_turn = 0.35f * fabsf(p->turn_rate) * still;
    f32 walk_goal = fmaxf(clampf(p->speed / 0.7f, 0.0f, 1.0f), 0.45f * clampf(fabsf(p->turn_rate) / 3.0f, 0.0f, 1.0f));
    p->walk = follow(p->walk, walk_goal, 0.15f, dt);
    // Походка по фактической скорости: в повороте с бега видно сброс на трусцу. Но трусцой и бегом
    // уже с первого шага бежит, а не идёт (короткий частый шаг), и тормозит беговыми шагами до ~1 м/с
    f32 run_goal = char_run_from_speed(p->speed);
    if (p->gait != GAIT_WALK) {
        f32 intent = p->gait == GAIT_RUN ? 0.6f : 0.5f;
        bool speeding = goal > p->speed + 0.05f;
        run_goal = fmaxf(run_goal, intent * smooth01(speeding ? (p->speed - 0.2f) / 0.4f : (p->speed - 1.0f) / 0.6f));
    }
    p->run += clampf(run_goal - p->run, -dt * 2.5f, dt * 2.5f);
    // Перед целью шаги чуть короче или длиннее: встаёт, когда опорная стопа под телом, вторая нога
    // приставляется по воздуху, а не доезжает до стойки по земле. Длина шага меняется плавно
    bool hold;
    f32 sg = stride_goal(p, &hold), sc = player_stride(p);
    if (!hold) sc += clampf(sg - sc, -STRIDE_RATE * dt, STRIDE_RATE * dt);
    p->stride = (i8)clampf(roundf((sc - 1.0f) * 400.0f), -127.0f, 127.0f);
    p->phase += (p->speed + step_turn) * dt / (char_stride_run(p->speed, p->run) * player_stride(p));
    p->phase -= floorf(p->phase);
    // Совсем встал: фаза — в ближайшую точку покоя, чтобы трогаться с шага, а не со скольжения стоп
    if (!p->moving && p->speed <= 0.0f && p->walk < 0.02f && fabsf(p->turn_rate) < 0.2f) {
        f32 r = p->phase - CHAR_REST_PHASE;
        r -= floorf(r * 2.0f + 0.5f) * 0.5f;
        p->phase -= r;
        p->phase -= floorf(p->phase);
    }
    p->idle = p->speed < 0.05f && fabsf(p->turn_rate) < 0.2f ? p->idle + dt : 0.0f;
}

void player_lerp(const Player *a, const Player *b, f32 t, Player *out)
{
    *out = *b;
    out->pos = v3_lerp(a->pos, b->pos, t);
    out->heading = wrap_angle(a->heading + wrap_angle(b->heading - a->heading) * t);
    out->move_yaw = wrap_angle(a->move_yaw + wrap_angle(b->move_yaw - a->move_yaw) * t);
    out->speed = lerpf(a->speed, b->speed, t);
    out->turn_rate = lerpf(a->turn_rate, b->turn_rate, t);
    out->lean_fwd = lerpf(a->lean_fwd, b->lean_fwd, t);
    out->lean_side = lerpf(a->lean_side, b->lean_side, t);
    out->look = lerpf(a->look, b->look, t);
    out->idle = lerpf(a->idle, b->idle, t);
    // Поза отдыха: время фазы между шагами, если фаза та же
    if (a->rest == b->rest && a->rest_ph == b->rest_ph) out->rest_t = lerpf(a->rest_t, b->rest_t, t);
    out->walk = lerpf(a->walk, b->walk, t);
    out->run = lerpf(a->run, b->run, t);
    out->marker = lerpf(a->marker, b->marker, t);
    // Фаза шага по кругу: за шаг мира она проходит меньше половины круга
    f32 dp = b->phase - a->phase;
    dp -= floorf(dp + 0.5f);
    out->phase = a->phase + dp * t;
    out->phase -= floorf(out->phase);
}
