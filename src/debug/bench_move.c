// Замеры без окна (--bench, data/debug/bench.txt): развороты шагом, трусцой и бегом
#include "bench.h"

#include <stdio.h>

#include "../sim/player.h"

static f32 flat_ground(f32 x, f32 z) { (void)x; (void)z; return 0.0f; }

int player_bench(const char *trace_path)
{
    FILE *tr = trace_path ? fopen(trace_path, "w") : NULL;
    if (tr) fputs("case,t,x,z,speed,heading,lean_side\n", tr);
    static const char *gname[GAIT_COUNT] = { "ШАГ   ", "ТРУСЦА", "БЕГ   " };
    static const f32 angles[] = { 90.0f, 135.0f, 180.0f };
    const f32 dt = 1.0f / 20.0f;  // как шаг мира (TICK_HZ)
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
