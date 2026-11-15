// Порядок систем шага мира — один список в sim_step. Над системой: что читает и что пишет
#include "step.h"
#include "../core/fpu.h"
#include "../core/mathf.h"
#include "ground.h"

static constexpr u64 CLOCK_RATE_MAX = 1u << 16;

const char *const STEP_SYS_NAMES[SYS_COUNT] = {"намерения", "действия", "пути", "руление", "расталкивание",
                                              "опора", "часы и прочее"};

// Читает: намерения шага. Пишет: clock, rng и люди (рождение, приказы)
static void step_intents(World &w, StepCtx &ctx) {
    for (const Intent &it : ctx.intents) {
        switch (it.kind) {
        case INTENT_CLOCK_SET: w.clock.tick = clock_tick_at(w.clock, (u32)it.arg); break;
        case INTENT_CLOCK_RATE: w.clock.rate = it.arg > CLOCK_RATE_MAX ? CLOCK_RATE_MAX : (u32)it.arg; break;
        case INTENT_PEOPLE_SPAWN: {
            u32 n = (u32)(it.arg & 0xFFFFu);
            f32 r = (f32)(u32)((it.arg >> 16) & 0xFFFFu);
            u32 side = (u32)((it.arg >> 32) & 0xFFu);
            for (u32 i = 0; i < n && people_spawn(w, it.target, r, side); ++i) {}
        } break;
        case INTENT_GO: people_order_go(w, (u32)it.arg, it.target, (u32)(it.arg >> 32)); break;
        case INTENT_ATTACK: people_order_attack(w, (u32)it.arg, (u32)(it.arg >> 32)); break;
        case INTENT_ACT: people_order_act(w, (u32)it.arg, (u32)(it.arg >> 32)); break;
        case INTENT_PLACE: people_order_place(w, (u32)it.arg, (u32)(it.arg >> 32)); break;
        default: break;
        }
    }
}

// Читает и пишет: clock.tick. Часы идут целыми тиками, без накопления float
static void step_clock(World &w, StepCtx &) { w.clock.tick += w.clock.rate; }

// Только отладочное рисование: капсулы, курс и пути людей. Мир не меняет
static void step_debug_draw(const World &w, StepCtx &ctx) {
    if (!ctx.dbg) return;
    DebugDraw &g = *ctx.dbg;
    dbg_clear(g);
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
}

void sim_step(World &w, StepCtx &ctx) {
    fpu_enter_sim();
    if (ctx.step_arena) arena_reset(*ctx.step_arena);
    // Замер по системам — только с часами бенча; мир от него не зависит
    StepStats *st = ctx.stats && ctx.stats->now_ns ? ctx.stats : nullptr;
    u64 mark = st ? st->now_ns() : 0;
    auto done = [&](StepSysId id) {
        if (!st) return;
        u64 t = st->now_ns();
        st->sys_ns[id] += t - mark;
        mark = t;
    };
    step_intents(w, ctx);
    done(SYS_INTENTS);
    people_act(w, ctx);
    done(SYS_ACT);
    people_paths(w, ctx);
    done(SYS_PATHS);
    people_steer(w, ctx);
    done(SYS_STEER);
    people_separate(w, ctx);
    done(SYS_SEPARATE);
    people_settle(w, ctx);
    done(SYS_SETTLE);
    step_clock(w, ctx);
    w.step++;
    step_debug_draw(w, ctx);
    done(SYS_REST);
    ANA_ASSERT(fpu_sim_ok());
}
