// Режимы без окна и GPU: --sim (шаг мира, хеши, бенч) и --replay (повтор записи с поиском расхождения)
#include "app.h"
#include "../core/log.h"
#include "../debug/debug.h"
#include "../platform/platform.h"
#include "../sim/ground.h"
#include "../sim/record.h"
#include "../sim/step.h"
#include <inttypes.h>
#include <stdio.h>

static constexpr u32 MAX_INTENTS_PER_STEP = 1024;  // подставной игрок и ИИ всех бойцов

int run_sim(const Args &a, AppMemory &m) {
    World w;
    u64 desc = 0;
    if (!app_world_create(w, m, a.seed, &desc)) return 1;
    DuelDesc duel{};
    GroupDesc group{};
    if (!app_fight_load(duel, group, m)) return 1;
    const bool brawl = a.brawl[0] && a.brawl[1];
    BrawlStats *bs = brawl ? arena_push<BrawlStats>(m.load, 1) : nullptr;
    if (bs) *bs = BrawlStats{};
    Recorder rec;
    if (a.record) {
        recorder_init(rec, m.record, w, a.seed, a.sim_steps, 1u << 20);
        rec.desc_hash = desc;
    }
    Rng player;
    rng_seed(player, a.seed, 0x9137ull);  // подставной игрок — свой RNG, не мира
    FixedArray<Intent> intents;
    intents.init(m.world, MAX_INTENTS_PER_STEP);

    // Текст хешей: строка на каждые 1000 шагов (эталон — tests/sim_hashes.txt)
    char *text = nullptr;
    usize text_len = 0, text_cap = 0;
    if (a.hashes) {
        text_cap = 512 + (a.sim_steps / 1000 + 1) * 48;
        text = arena_push<char>(m.load, text_cap);
        char thex[17];
        char shex[17], whex[17];
        text_len = (usize)snprintf(text, text_cap,
                                   "# anathemyre --sim: хеш земли, сетки ходимости, неизменного (коллайдеры, сетка, длины шага) и хеш мира "
                                   "каждые 1000 шагов\nsim_version %u\nseed %" PRIu64 "\nterrain %s\nwalk %s\nstatic %s\n",
                                   SIM_VERSION, a.seed, hex_u64(w.terrain.hash, thex), hex_u64(w.walk.hash, whex),
                                   hex_u64(world_static_hash(w), shex));
    }

    // --rewind-check: перемотка окна (debug/rewind.cpp) против хешей прямого хода последних 600 шагов
    Rewind *rw = nullptr;
    u64 *ring_hash = nullptr;
    if (a.rewind_check) {
        rw = arena_push<Rewind>(m.load, 1);
        rewind_init(*rw, w);
        ring_hash = arena_push<u64>(m.load, 600);
    }
    f64 max_us = 0, people_us = 0;
    u64 max_at = 0;
    u64 people_steps = 0, ai_ns = 0;
    StepStats stats;
    stats.now_ns = platform_time_ns;
    u64 t0 = platform_time_ns();
    for (u64 s = 0; s < a.sim_steps; ++s) {
        intents.clear();
        if (brawl) brawl_intents(w, a.brawl, intents);
        else if (a.duel) duel_intents(w, a.duel, intents);
        else autoplay_intents(w, player, intents);
        u64 ta = platform_time_ns();
        ai_fight(w, duel, group, intents);
        ai_ns += platform_time_ns() - ta;
        StepCtx ctx{intents.span(), &m.step, nullptr, &stats};
        u64 ts = platform_time_ns();
        sim_step(w, ctx);
        f64 us = (f64)(platform_time_ns() - ts) / 1000.0;
        if (w.people.live) { people_steps++; people_us += us; }
        if (us > max_us) { max_us = us; max_at = w.step; }
        if (bs) debug_brawl_step(w, *bs);
        if (a.record) recorder_step(rec, w, intents.span());
        if (rw) {
            rewind_after_step(*rw, w, intents.span());
            ring_hash[w.step % 600] = world_hash(w);
        }
        if (text && w.step % 1000 == 0 && text_len + 48 < text_cap) {
            char hex[17];
            text_len += (usize)snprintf(text + text_len, text_cap - text_len, "step %" PRIu64 " %s\n",
                                        w.step, hex_u64(world_hash(w), hex));
        }
    }
    f64 total_ms = (f64)(platform_time_ns() - t0) / 1e6;
    char hex[17];
    log_info("--sim: %" PRIu64 " шагов за %.1f мс, шаг в среднем %.1f мкс, макс %.1f мкс (шаг %" PRIu64 "); точек %u; хеш %s",
             a.sim_steps, total_ms, total_ms * 1000.0 / (f64)a.sim_steps, max_us, max_at, w.dots.live,
             hex_u64(world_hash(w), hex));
    // Шаг по системам (docs/sim.md «Замер шага»): среднее на шаг за весь прогон; ИИ боя — до шага, вне sim_step
    {
        char line[512];
        i32 len = snprintf(line, sizeof line, "--sim по системам, мкс на шаг:");
        for (u32 i = 0; i < SYS_COUNT && len > 0 && len < (i32)sizeof line; ++i)
            len += snprintf(line + len, sizeof line - (usize)len, "%s %s %.1f", i ? " ·" : "", STEP_SYS_NAMES[i],
                            (f64)stats.sys_ns[i] / 1000.0 / (f64)a.sim_steps);
        if (len > 0 && len < (i32)sizeof line)
            snprintf(line + len, sizeof line - (usize)len, " · ИИ боя %.1f", (f64)ai_ns / 1000.0 / (f64)a.sim_steps);
        log_info("%s", line);
    }
    u64 people_ns = 0;
    for (u32 i = SYS_PEOPLE_FIRST; i <= SYS_PEOPLE_LAST; ++i) people_ns += stats.sys_ns[i];
    // Бенч людей (docs/people.md, цель шага мира ≤ 2 мс на 512 в бою): средний шаг с людьми и работа поиска пути
    if (people_steps) {
        const People &p = w.people;
        u32 on_box = 0, high = 0, going = 0;
        for (u32 s = 0; s < PEOPLE_CAPACITY; ++s) {
            if (!p.alive[s]) continue;
            on_box += p.support[s] != 0;
            high += p.support[s] && p.y[s] > ground_at(w.terrain, p.cx[s], p.cz[s], p.x[s], p.z[s]).height + 3.0f;
            going += p.state[s] != MOVE_STAND;
        }
        log_info("--sim люди: %u живых (на постройках %u, из них выше 3 м %u; в пути %u), шаг с людьми в среднем %.1f мкс, "
                 "из них системы людей %.1f мкс; ИИ боя %.1f мкс; путей %u, раскрыто узлов %" PRIu64 " (%.0f на путь); ударов %u, попаданий %u, "
                 "отбито %u, защит %u, по своим %u",
                 p.live, on_box, high, going, people_us / (f64)people_steps, (f64)people_ns / 1000.0 / (f64)people_steps,
                 (f64)ai_ns / 1000.0 / (f64)people_steps,
                 stats.paths, stats.expanded,
                 stats.paths ? (f64)stats.expanded / (f64)stats.paths : 0.0, stats.strikes, stats.hits, stats.blocks, stats.defends, stats.friendly);
    }
    if (a.duel && !brawl) debug_duel_report(w, a.duel);
    if (bs) debug_brawl_report(w, *bs, a.brawl);
    int rc = 0;
    if (rw) {
        bool good = w.step > 600;
        const u64 end = w.step;
        // Назад всё дальше: каждая перемотка — от уже перемотанного мира к более раннему шагу
        for (u64 back = 1; good && back < 500; back += 37) {
            u64 target = end - back;
            u64 want = ring_hash[target % 600];
            good = rewind_to(*rw, w, target, m.step) && world_hash(w) == want;
            if (!good) log_error("--rewind-check: перемотка на шаг %" PRIu64 " не совпала с прямым ходом", target);
        }
        if (good) log_info("--rewind-check: перемотка совпала с прямым ходом (14 точек до 500 шагов назад)");
        else rc = 1;
        rewind_free(*rw);
    }
    if (text && !platform_write_file(a.hashes, text, text_len)) { log_error("не записать %s", a.hashes); rc = 1; }
    if (a.record && !save_record(rec, a.record, m.load)) rc = 1;
    return rc;
}

int run_replay(const Args &a, AppMemory &m) {
    u8 *data = nullptr;
    usize size = 0;
    if (!platform_read_file(a.replay, m.load, &data, &size)) {
        log_error("нет файла записи %s", a.replay);
        return 1;
    }
    Replay rp;
    const char *err = "";
    if (!replay_parse(data, size, m.load, rp, &err)) {
        log_error("%s: %s", a.replay, err);
        return 1;
    }
    if (rp.sim_version != SIM_VERSION) {
        log_error("запись от другой версии шага мира (%u, а у сборки %u): повтор невозможен", rp.sim_version, SIM_VERSION);
        return 1;
    }
    World w;
    u64 desc = 0;
    if (!app_world_create(w, m, rp.seed, &desc)) return 1;
    if (desc != rp.desc_hash) {
        char h1[17], h2[17];
        log_error("описания мира поменялись (в записи %s, сейчас %s): повтор невозможен", hex_u64(rp.desc_hash, h1),
                  hex_u64(desc, h2));
        return 1;
    }
    Column cols[MAX_COLUMNS];
    u32 ncols = world_columns(w, cols);
    if (ncols != rp.ncols) {
        log_error("в записи %u столбцов, в мире %u: другая раскладка таблиц", rp.ncols, ncols);
        return 1;
    }
    for (u32 i = 0; i < ncols; ++i)
        if (strcmp(cols[i].name, rp.names[i]) != 0) {
            log_error("столбец %u: в записи %s, в мире %s", i, rp.names[i], cols[i].name);
            return 1;
        }

    u64 *got = arena_push<u64>(m.load, ncols);
    usize next = 0;
    u64 t0 = platform_time_ns();
    for (u64 s = 0; s < rp.steps; ++s) {
        usize first = next;
        while (next < rp.intents.count && rp.intents.data[next].step == w.step) next++;
        if (next < rp.intents.count && rp.intents.data[next].step < w.step) {
            log_error("намерения в записи не по порядку шагов");
            return 1;
        }
        StepCtx ctx{{rp.intents.data + first, next - first}, &m.step, nullptr};
        sim_step(w, ctx);
        world_column_hashes(w, got, ncols);
        const u64 *want = rp.hashes + s * ncols;
        for (u32 c = 0; c < ncols; ++c) {
            if (got[c] == want[c]) continue;
            char h1[17], h2[17];
            log_error("повтор разошёлся: шаг %" PRIu64 ", столбец %s (в записи %s, сейчас %s)", w.step,
                      cols[c].name, hex_u64(want[c], h1), hex_u64(got[c], h2));
            for (u32 k = c + 1; k < ncols; ++k)
                if (got[k] != want[k]) log_error("  на этом шаге разошёлся и %s", cols[k].name);
            return 1;
        }
    }
    char hex[17];
    log_info("повтор совпал: %" PRIu64 " шагов, %zu намерений, %.1f мс; хеш %s", rp.steps, rp.intents.count,
             (f64)(platform_time_ns() - t0) / 1e6, hex_u64(world_hash(w), hex));
    return 0;
}

int run_terrain_map(const Args &a, AppMemory &m) {
    World w;
    if (!app_world_create(w, m, a.seed, nullptr)) return 1;
    return debug_write_terrain_map(w.terrain, a.terrain_map, m.load) ? 0 : 1;
}

int run_walk_map(const Args &a, AppMemory &m) {
    World w;
    if (!app_world_create(w, m, a.seed, nullptr)) return 1;
    return debug_write_walk_map(w, a.walk_map, m.load) ? 0 : 1;
}
