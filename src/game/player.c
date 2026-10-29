#include "player.h"

#include <stdio.h>

#include "anim.h"

#define ARRIVE_DIST 0.15f
#define MARKER_S    1.2f
#define ACCEL       3.0f   // разгон, м/с²
#define BRAKE       6.0f   // торможение, м/с²
#define ARRIVE_DEC  2.5f   // мягкое торможение к цели, м/с²
#define LAT_ACC     7.0f   // предел бокового ускорения в повороте, м/с²
#define TURN_MOVE   6.0f   // предел поворота скорости на малом ходу, рад/с
#define TURN_BODY   5.5f   // разворот тела на месте, рад/с: 180° за ~0.6 с
#define TURN_TRACK  10.0f  // на ходу тело следует за направлением шага
#define PIVOT_SPEED 1.3f   // темп, до которого сбрасывает перед крутым разворотом, м/с

const f32 g_gait_speed[GAIT_COUNT] = { 1.6f, 3.2f, 4.8f };

static f32 wrap_angle(f32 a)
{
    while (a > PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

static f32 smooth01(f32 t)
{
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Сглаживание к цели за время tau, с
static f32 follow(f32 x, f32 goal, f32 tau, f32 dt) { return x + (goal - x) * fminf(1.0f, dt / tau); }

void player_init(Player *p, V3 pos, f32 heading)
{
    *p = (Player){ .pos = pos, .heading = heading, .move_yaw = heading, .target = pos, .gait = GAIT_RUN,
                   .vmax = g_gait_speed[GAIT_RUN] };
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
        // Направление скорости поворачивает не быстрее, чем позволяет боковое ускорение
        f32 w = fminf(TURN_MOVE, LAT_ACC / fmaxf(p->speed, 0.1f)) * dt;
        p->move_yaw = wrap_angle(p->move_yaw + clampf(wrap_angle(want - p->move_yaw), -w, w));
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
    // Походка по фактической скорости: в повороте с бега видно сброс на трусцу
    p->run += clampf(char_run_from_speed(p->speed) - p->run, -dt * 2.5f, dt * 2.5f);
    p->phase += (p->speed + step_turn) * dt / char_stride(p->speed);
    p->phase -= floorf(p->phase);
}

// ---------------------------------------------------------------- проверка разворотов

static f32 flat_ground(f32 x, f32 z) { (void)x; (void)z; return 0.0f; }

int player_bench(const char *trace_path)
{
    FILE *tr = trace_path ? fopen(trace_path, "w") : NULL;
    if (tr) fputs("case,t,x,z,speed,heading,lean_side\n", tr);
    static const char *gname[GAIT_COUNT] = { "ШАГ   ", "ТРУСЦА", "БЕГ   " };
    static const f32 angles[] = { 90.0f, 135.0f, 180.0f };
    const f32 dt = 1.0f / 60.0f;
    puts("РЕЖИМ  СТАРТ    УГОЛ  ВРЕМЯ,С  МИН.СКОРОСТЬ  ОСТАНОВКА  КРЕН,°  ПУТЬ/ПРЯМАЯ");
    u32 id = 0;
    for (u32 g = 0; g < GAIT_COUNT; g++)
        for (u32 moving = 0; moving < 2; moving++)
            for (u32 a = 0; a < ARRAY_COUNT(angles); a++, id++) {
                Player p;
                player_init(&p, v3(0, 0, 0), 0.0f);
                p.gait = (u8)g;
                p.vmax = g_gait_speed[g];
                // На ходу: разгон по прямой, приказ приходит на полной скорости
                if (moving) {
                    player_order_move(&p, v3(0, 0, 100));
                    for (u32 k = 0; k < 600 && p.speed < p.vmax - 0.01f; k++) player_update(&p, dt, flat_ground);
                }
                f32 ang = angles[a] * PI_F / 180.0f;
                V3 start = p.pos;
                V3 goal = v3_add(start, v3(sinf(p.heading + ang) * 8.0f, 0, cosf(p.heading + ang) * 8.0f));
                player_order_move(&p, goal);
                f32 t = 0.0f, vmin = 1e9f, lean = 0.0f, path = 0.0f;
                while (p.moving && t < 20.0f) {
                    V3 prev = p.pos;
                    player_update(&p, dt, flat_ground);
                    t += dt;
                    path += v3_len(v3_sub(p.pos, prev));
                    f32 left = v3_len(v3_sub(goal, p.pos));
                    // Скорость на развороте: со старта не считаем первые 0.3 с, у цели — последние 1.5 м
                    if (left > 1.5f && (moving || t > 0.3f)) vmin = fminf(vmin, p.speed);
                    lean = fmaxf(lean, fabsf(p.lean_side));
                    if (tr) fprintf(tr, "%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", id, t, p.pos.x - start.x, p.pos.z - start.z, p.speed, p.heading, p.lean_side);
                }
                printf("%s %s %4.0f°  %6.2f   %9.2f     %s        %5.1f  %8.2f\n", gname[g], moving ? "НА ХОДУ" : "СТОЯ   ",
                       angles[a], t, vmin, moving && vmin < 0.3f ? "ДА " : "нет", lean * 180.0f / PI_F, path / 8.0f);
            }
    if (tr) fclose(tr);
    return 0;
}
