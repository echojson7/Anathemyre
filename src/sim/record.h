// Запись повтора: версия шага, зерно, хеш описаний, намерения игрока по шагам и хеши столбцов после каждого шага.
// Файл .rec — куски «тег, версия, длина» (core/ser.h). Формат: docs/replay.md
#pragma once
#include "intent.h"
#include "world.h"
#include "../core/ser.h"

struct Recorder {
    u64 seed = 0;
    u64 desc_hash = 0;
    u32 ncols = 0;
    const char *names[MAX_COLUMNS] = {};
    FixedArray<Intent> intents;
    u64 *hashes = nullptr;  // steps × ncols
    u64 max_steps = 0;
    u64 steps = 0;
    bool full = false;      // место кончилось, дальше не пишется
};

void recorder_init(Recorder &r, Arena &arena, const World &w, u64 seed, u64 max_steps, u32 max_intents);
// После шага: намерения, которые он исполнил, и хеши столбцов
void recorder_step(Recorder &r, const World &w, Span<const Intent> step_intents);
void recorder_save(const Recorder &r, Writer &out);
usize recorder_bytes(const Recorder &r);

struct Replay {
    u32 sim_version = 0;
    u64 seed = 0;
    u64 desc_hash = 0;
    u64 steps = 0;
    u32 ncols = 0;
    const char *names[MAX_COLUMNS] = {};
    Span<Intent> intents;
    const u64 *hashes = nullptr;
};

// Разбор файла; при ошибке false и текст в *err
bool replay_parse(const u8 *data, usize size, Arena &arena, Replay &out, const char **err);
