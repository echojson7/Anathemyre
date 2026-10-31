// Сценарии замеров (data/debug/bench.txt): имя, вид, числа вида; числа командной строки заменяют их по порядку
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/log.h"
#include "bench.h"

#define BENCH_ARGS 4

int debug_bench(const char *data_dir, const char *name, char **args, u32 nargs, u64 seed, u32 threads)
{
    char path[600], line[256];
    snprintf(path, sizeof path, "%s/debug/bench.txt", data_dir);
    FILE *f = fopen(path, "r");
    if (!f) {
        log_error("ЗАМЕР: нет %s", path);
        return 1;
    }
    char kind[32] = "";
    f64 v[BENCH_ARGS] = { 0 };
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *tok = strtok(line, " \t\r\n");
        if (!tok || strcmp(tok, name)) continue;
        tok = strtok(NULL, " \t\r\n");
        snprintf(kind, sizeof kind, "%s", tok ? tok : "");
        for (u32 k = 0; k < BENCH_ARGS && (tok = strtok(NULL, " \t\r\n")); k++) v[k] = strtod(tok, NULL);
        break;
    }
    fclose(f);
    if (!kind[0]) {
        log_error("ЗАМЕР: сценария %s нет в %s", name, path);
        return 1;
    }
    if (!strcmp(kind, "move")) return player_bench(nargs ? args[0] : NULL);
    for (u32 k = 0; k < nargs && k < BENCH_ARGS; k++) v[k] = strtod(args[k], NULL);
    if (!strcmp(kind, "arena")) return arena_bench((u32)v[0], seed, (u32)v[1]);
    if (!strcmp(kind, "battle") || !strcmp(kind, "verify"))
        return battle_bench((u32)v[0], (u32)v[1], seed, threads, kind[0] == 'v');
    if (!strcmp(kind, "field")) return field_bench((u32)v[0], seed);
    if (!strcmp(kind, "bands")) return bands_bench((u32)v[0], (f32)v[1], seed, TICK_HZ, (u32)v[2]);
    if (!strcmp(kind, "frame")) return DEBUG_BENCH_FRAME;
    if (!strcmp(kind, "stages")) return DEBUG_BENCH_STAGES;
    log_error("ЗАМЕР: вид %s не знаю (%s)", kind, path);
    return 1;
}
