// Порядок систем шага мира — один список в sim_step. Над системой: что читает и что пишет
#include "step.h"
#include "../core/fpu.h"
#include "../core/mathf.h"
#include "ground.h"

static constexpr f32 DOTS_ALIVE_RADIUS = 150.0f;   // дальше от начала карты точка гибнет
static constexpr f32 DOTS_RESPAWN_RADIUS = 30.0f;
static constexpr u32 ATTRACT_STEPS = 3 * SIM_HZ;
static constexpr u32 BURST_COUNT = 40;

static constexpr u64 CLOCK_RATE_MAX = 1u << 16;

// Читает: намерения шага. Пишет: attract_*, точки (BURST), rng, clock, люди (рождение, приказы)
static void step_intents(World &w, StepCtx &ctx) {
    for (const Intent &it : ctx.intents) {
        switch (it.kind) {
        case INTENT_ATTRACT:
            w.attract_on = 1;
            w.attract_at = it.target;
            w.attract_until = w.step + ATTRACT_STEPS;
            break;
        case INTENT_RELEASE: w.attract_on = 0; break;
        case INTENT_CLOCK_SET: w.clock.tick = clock_tick_at(w.clock, (u32)it.arg); break;
        case INTENT_CLOCK_RATE: w.clock.rate = it.arg > CLOCK_RATE_MAX ? CLOCK_RATE_MAX : (u32)it.arg; break;
        case INTENT_PEOPLE_SPAWN: {
            u32 n = (u32)(it.arg & 0xFFFFu);
            f32 r = (f32)(u32)((it.arg >> 16) & 0xFFFFu);
            for (u32 i = 0; i < n && people_spawn(w, it.target, r); ++i) {}
        } break;
        case INTENT_GO: people_order_go(w, (u32)it.arg, it.target, (u32)(it.arg >> 32)); break;
        case INTENT_BURST: {
            // Гибнут BURST_COUNT случайных живых, столько же рождается у цели
            u32 killed = 0;
            for (u32 tries = 0; tries < BURST_COUNT * 8 && killed < BURST_COUNT; ++tries) {
                u32 s = rng_below(w.rng, DOTS_CAPACITY);
                if (w.dots.alive[s]) { dots_kill(w, s); killed++; }
            }
            for (u32 i = 0; i < killed; ++i) dots_spawn(w, it.target, 4.0f);
        } break;
        default: break;
        }
    }
    if (w.attract_on && w.step >= w.attract_until) w.attract_on = 0;
}

// Читает: phase, omega, speed, положение, attract_*. Пишет: vx, vz, phase, rng
static void step_dots_steer(World &w, StepCtx &) {
    Dots &d = w.dots;
    const WPos &t = w.attract_at;
    for (u32 i = 0; i < DOTS_CAPACITY; ++i) {
        if (!d.alive[i]) continue;
        f32 s, c;
        sincos_turn(d.phase[i], &s, &c);
        d.phase[i] += d.omega[i];
        f32 wx = c * d.speed[i];
        f32 wz = s * d.speed[i];
        if (w.attract_on) {
            Vec2 to = wpos_delta_xz(d.cx[i], d.cz[i], d.x[i], d.z[i], t.cx, t.cz, t.x, t.z);
            f32 len = length(to);
            if (len > 0.5f) {
                f32 k = 9.0f / len;
                wx += to.x * k;
                wz += to.y * k;
            }
        }
        wx += rng_range(w.rng, -1.0f, 1.0f);
        wz += rng_range(w.rng, -1.0f, 1.0f);
        d.vx[i] += (wx - d.vx[i]) * 0.08f;
        d.vz[i] += (wz - d.vz[i]) * 0.08f;
    }
}

// Читает: vx, vz, землю. Пишет: cx, cz, x, z, y
static void step_dots_move(World &w, StepCtx &) {
    Dots &d = w.dots;
    for (u32 i = 0; i < DOTS_CAPACITY; ++i) {
        if (!d.alive[i]) continue;
        d.x[i] += d.vx[i] * SIM_DT;
        d.z[i] += d.vz[i] * SIM_DT;
        wpos_rebase(d.cx[i], d.x[i]);
        wpos_rebase(d.cz[i], d.z[i]);
        d.y[i] = ground_at(w.terrain, d.cx[i], d.cz[i], d.x[i], d.z[i]).height;
    }
}

// Читает: положение. Пишет: всё о рождении и смерти точек, rng
static void step_dots_life(World &w, StepCtx &) {
    Dots &d = w.dots;
    const WPos origin{};
    const f32 r2 = DOTS_ALIVE_RADIUS * DOTS_ALIVE_RADIUS;
    for (u32 i = 0; i < DOTS_CAPACITY; ++i) {
        if (!d.alive[i]) continue;
        Vec2 o = wpos_delta_xz(origin.cx, origin.cz, origin.x, origin.z, d.cx[i], d.cz[i], d.x[i], d.z[i]);
        if (dot(o, o) > r2) dots_kill(w, i);
    }
    while (d.live < DOTS_TARGET && dots_spawn(w, origin, DOTS_RESPAWN_RADIUS)) {}
}

// Читает и пишет: clock.tick. Часы идут целыми тиками, без накопления float
static void step_clock(World &w, StepCtx &) { w.clock.tick += w.clock.rate; }

// Только отладочное рисование: курс первых точек и цель притяжения. Мир не меняет
static void step_debug_draw(const World &w, StepCtx &ctx) {
    if (!ctx.dbg) return;
    DebugDraw &g = *ctx.dbg;
    dbg_clear(g);
    const Dots &d = w.dots;
    for (u32 i = 0, shown = 0; i < DOTS_CAPACITY && shown < 64; ++i) {
        if (!d.alive[i]) continue;
        WPos a{d.cx[i], d.cz[i], d.x[i], d.y[i] + 0.3f, d.z[i]};  // над землёй, чтобы отрезок не тонул в ней
        WPos b = a;
        b.x += d.vx[i] * 0.5f;
        b.z += d.vz[i] * 0.5f;
        wpos_rebase(b);
        dbg_line(g, a, b, rgba8(230, 200, 120));
        shown++;
    }
    // Люди: капсула (столбик 1.8 м), курс тела, путь первых 32 — отрезками по узлам
    const People &pp = w.people;
    for (u32 s = 0, shown = 0; s < PEOPLE_CAPACITY; ++s) {
        if (!pp.alive[s]) continue;
        WPos a{pp.cx[s], pp.cz[s], pp.x[s], pp.y[s], pp.z[s]};
        WPos top = a;
        top.y += 1.8f;
        dbg_line(g, a, top, pp.state[s] == MOVE_FOLLOW ? rgba8(90, 220, 120) : rgba8(220, 220, 220));
        f32 sn, cs;
        sincos_turn(pp.heading[s], &sn, &cs);
        WPos nose = a;
        nose.y += 1.5f;
        WPos tip = nose;
        tip.x += sn * 0.5f;
        tip.z += cs * 0.5f;
        wpos_rebase(tip);
        dbg_line(g, nose, tip, rgba8(240, 120, 60));
        if (shown >= 32 || pp.state[s] != MOVE_FOLLOW || pp.path_area[s] == PATH_DIRECT) continue;
        shown++;
        WPos prev = a;
        prev.y += 0.1f;
        for (u32 k = pp.path_i[s]; k < pp.path_n[s]; ++k) {
            WPos n = walk_node_pos(w.walk.area[pp.path_area[s]], pp.path[s * PATH_MAX + k]);
            n.y += 0.1f;
            dbg_line(g, prev, n, rgba8(120, 180, 255));
            prev = n;
        }
    }
    if (w.attract_on) {
        // Крест и сетка 1 м по ground_at() вокруг цели: если картинка земли с ними совпадает, отрезки лежат
        // на ней вплотную, не висят и не тонут (docs/terrain.md, проверка этапа 2)
        WPos c = w.attract_at;
        c.x = (f32)f_floor_i(c.x);  // на узлы решётки 1 м: вдоль её рёбер земля линейна
        c.z = (f32)f_floor_i(c.z);
        c.y = ground_at(w.terrain, c).height + 0.02f;
        WPos top = c;
        top.y += 1.5f;
        dbg_line(g, c, top, rgba8(220, 60, 50));  // столбик над целью: прямой крест на склоне ушёл бы в землю
        for (i32 k = -4; k <= 4; ++k)
            for (i32 m = -4; m < 4; ++m) {
                for (u32 axis = 0; axis < 2; ++axis) {
                    WPos a = c, b = c;
                    f32 u = (f32)k, v0 = (f32)m, v1 = (f32)(m + 1);
                    if (axis == 0) { a.x += u; a.z += v0; b.x += u; b.z += v1; }
                    else { a.x += v0; a.z += u; b.x += v1; b.z += u; }
                    wpos_rebase(a);
                    wpos_rebase(b);
                    a.y = ground_at(w.terrain, a).height + 0.02f;
                    b.y = ground_at(w.terrain, b).height + 0.02f;
                    dbg_line(g, a, b, rgba8(240, 230, 120));
                }
            }
    }
}

void sim_step(World &w, StepCtx &ctx) {
    fpu_enter_sim();
    if (ctx.step_arena) arena_reset(*ctx.step_arena);
    step_intents(w, ctx);
    step_dots_steer(w, ctx);
    step_dots_move(w, ctx);
    step_dots_life(w, ctx);
    u64 t0 = ctx.stats && ctx.stats->now_ns ? ctx.stats->now_ns() : 0;
    people_paths(w, ctx);
    people_steer(w, ctx);
    people_separate(w, ctx);
    people_settle(w, ctx);
    if (t0) ctx.stats->people_ns += ctx.stats->now_ns() - t0;
    step_clock(w, ctx);
    w.step++;
    step_debug_draw(w, ctx);
    ANA_ASSERT(fpu_sim_ok());
}
