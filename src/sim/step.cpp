// Порядок систем шага мира — один список в sim_step. Над системой: что читает и что пишет
#include "step.h"
#include "../core/fpu.h"
#include "../core/mathf.h"
#include "ground.h"

static constexpr f32 DOTS_ALIVE_RADIUS = 150.0f;   // дальше от начала карты точка гибнет
static constexpr f32 DOTS_RESPAWN_RADIUS = 30.0f;
static constexpr u32 ATTRACT_STEPS = 3 * SIM_HZ;
static constexpr u32 BURST_COUNT = 40;

// Читает: намерения шага. Пишет: attract_*, точки (BURST), rng
static void step_intents(World &w, StepCtx &ctx) {
    for (const Intent &it : ctx.intents) {
        switch (it.kind) {
        case INTENT_ATTRACT:
            w.attract_on = 1;
            w.attract_at = it.target;
            w.attract_until = w.step + ATTRACT_STEPS;
            break;
        case INTENT_RELEASE: w.attract_on = 0; break;
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
    w.step++;
    step_debug_draw(w, ctx);
    ANA_ASSERT(fpu_sim_ok());
}
