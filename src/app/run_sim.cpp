// Режимы без окна и GPU: --sim (шаг мира, хеши, бенч) и --replay (повтор записи с поиском расхождения)
#include "app.h"
#include "../core/log.h"
#include "../debug/debug.h"
#include "../platform/platform.h"
#include "../sim/record.h"
#include "../sim/step.h"
#include <inttypes.h>
#include <stdio.h>

static constexpr u32 MAX_INTENTS_PER_STEP = 64;

int run_sim(const Args &a, AppMemory &m) {
    World w;
    u64 desc = 0;
    if (!app_world_create(w, m, a.seed, &desc)) return 1;
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
        text_cap = 128 + (a.sim_steps / 1000 + 1) * 48;
        text = arena_push<char>(m.load, text_cap);
        char thex[17];
        text_len = (usize)snprintf(text, text_cap, "# anathemyre --sim: хеш земли и хеш мира каждые 1000 шагов\nsim_version %u\nseed %" PRIu64 "\nterrain %s\n",
                                   SIM_VERSION, a.seed, hex_u64(w.terrain.hash, thex));
    }

    f64 max_us = 0;
    u64 t0 = platform_time_ns();
    for (u64 s = 0; s < a.sim_steps; ++s) {
        intents.clear();
        autoplay_intents(w.step, player, intents);
        StepCtx ctx{intents.span(), &m.step, nullptr};
        u64 ts = platform_time_ns();
        sim_step(w, ctx);
        f64 us = (f64)(platform_time_ns() - ts) / 1000.0;
        if (us > max_us) max_us = us;
        if (a.record) recorder_step(rec, w, intents.span());
        if (text && w.step % 1000 == 0 && text_len + 48 < text_cap) {
            char hex[17];
            text_len += (usize)snprintf(text + text_len, text_cap - text_len, "step %" PRIu64 " %s\n",
                                        w.step, hex_u64(world_hash(w), hex));
        }
    }
    f64 total_ms = (f64)(platform_time_ns() - t0) / 1e6;
    char hex[17];
    log_info("--sim: %" PRIu64 " шагов за %.1f мс, шаг в среднем %.1f мкс, макс %.1f мкс; точек %u; хеш %s",
             a.sim_steps, total_ms, total_ms * 1000.0 / (f64)a.sim_steps, max_us, w.dots.live,
             hex_u64(world_hash(w), hex));
    int rc = 0;
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
