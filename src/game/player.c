#include "player.h"

#include "character.h"

#define PLAYER_SPEED 1.5f   // м/с, шаг как у жителей зала, чуть быстрее
#define PLAYER_TURN 4.0f    // рад/с
#define ARRIVE_DIST 0.15f
#define MARKER_S 1.2f

static f32 wrap_angle(f32 a)
{
    while (a > PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

void player_init(Player *p, V3 pos, f32 heading)
{
    *p = (Player){ .pos = pos, .heading = heading, .target = pos };
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
    if (p->moving) {
        // Сначала разворот на месте, на ходу только подруливание
        f32 diff = wrap_angle(atan2f(dx, dz) - p->heading);
        f32 turn = PLAYER_TURN * dt;
        p->heading = wrap_angle(p->heading + clampf(diff, -turn, turn));
        f32 v = PLAYER_SPEED * p->walk * clampf(1.0f - fabsf(diff) / (PI_F * 0.5f), 0.0f, 1.0f);
        v = fminf(v, d / dt);
        p->pos.x += sinf(p->heading) * v * dt;
        p->pos.z += cosf(p->heading) * v * dt;
        p->phase += v * dt / CHAR_STRIDE;
    }
    // Разгон и остановка за треть секунды; ноги дошагивают цикл
    f32 goal = p->moving ? 1.0f : 0.0f;
    p->walk += clampf(goal - p->walk, -dt * 3.0f, dt * 3.0f);
    if (!p->moving && p->walk > 0.0f) p->phase += dt * p->walk * PLAYER_SPEED / CHAR_STRIDE;
    p->phase -= floorf(p->phase);
    p->pos.y = ground(p->pos.x, p->pos.z);
}
