// Приложение: режимы запуска (окно, --sim, --replay) и общее для них
#pragma once
#include "../core/mem.h"

struct Args {
    u64 seed = 1;
    u64 sim_steps = 0;          // --sim N
    const char *record = nullptr;   // --record файл.rec
    const char *replay = nullptr;   // --replay файл.rec
    const char *hashes = nullptr;   // --hashes файл.txt: хеш каждые 1000 шагов
    u64 frames = 0;             // --frames N: окно закрывается после N кадров (CI)
    bool vsync = true;
    bool validation = false;
};

// Память режима: всё выделяется один раз при старте
struct AppMemory {
    Arena world;
    Arena step;
    Arena frame;
    Arena load;
    Arena record;
};
void app_memory_init(AppMemory &m, bool with_record);
void app_memory_free(AppMemory &m);

int run_window(const Args &a, AppMemory &m);
int run_sim(const Args &a, AppMemory &m);
int run_replay(const Args &a, AppMemory &m);

// Записать запись повтора в файл; false — ошибка с сообщением в журнале
struct Recorder;
bool save_record(const Recorder &r, const char *path, Arena &scratch);
