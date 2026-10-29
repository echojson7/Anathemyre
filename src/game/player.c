#include "player.h"

#include "character.h"

#define PLAYER_TURN 4.0f    // рад/с
#define ARRIVE_DIST 0.15f
#define MARKER_S 1.2f

const f32 g_gait_speed[GAIT_COUNT] = { 1.6f, 3.2f, 4.8f };

static f32 wrap_angle(f32 a)
{
    while (a > PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

void player_init(Player *p, V3 pos, f32 heading)
{
    *p = (Player){ .pos = pos, .heading = heading, .target = pos, .gait = GAIT_RUN, .vmax = g_gait_speed[GAIT_RUN] };
}

void player_order_move(Player *p, V3 target)
{
    p->target = target;
    p->moving = true;
    p->marker = MARKER_S;
}

void player_update(Player *p, f32 dt, GroundFn ground)
{
    p->marker = fmaxf(0.0f, p->marker - dt);
    f32 dx = p->target.x - p->pos.x, dz = p->target.z - p->pos.z;
    f32 d = sqrtf(dx * dx + dz * dz);
    if (p->moving && d < ARRIVE_DIST) p->moving = false;
    // Темп: предел отряда; у цели сбавляет до шага, а не тормозит с разбега
    f32 pace = fminf(p->vmax, fmaxf(g_gait_speed[GAIT_WALK], 2.0f * d));
    f32 v = 0.0f;
    if (p->moving) {
        // Сначала разворот на месте, на ходу только подруливание; на бегу поворот шире
        f32 diff = wrap_angle(atan2f(dx, dz) - p->heading);
        f32 turn = PLAYER_TURN * (1.0f - 0.4f * p->run) * dt;
        p->heading = wrap_angle(p->heading + clampf(diff, -turn, turn));
        v = pace * p->walk * clampf(1.0f - fabsf(diff) / (PI_F * 0.5f), 0.0f, 1.0f);
        v = fminf(v, d / dt);
        p->pos.x += sinf(p->heading) * v * dt;
        p->pos.z += cosf(p->heading) * v * dt;
    }
    // Разгон и остановка за треть секунды; ноги дошагивают цикл
    f32 goal = p->moving ? 1.0f : 0.0f;
    p->walk += clampf(goal - p->walk, -dt * 3.0f, dt * 3.0f);
    if (!p->moving) v = p->walk * g_gait_speed[GAIT_WALK];
    // Походка по скорости: отряд в общем темпе идёт тем шагом, каким реально движется
    f32 run_goal = p->moving ? char_run_from_speed(pace) : 0.0f;
    p->run += clampf(run_goal - p->run, -dt * 2.5f, dt * 2.5f);
    p->phase += v * dt / char_stride(p->run);
    p->phase -= floorf(p->phase);
    p->pos.y = ground(p->pos.x, p->pos.z);
}
