// Приложение: режимы запуска (окно, --sim, --replay) и общее для них
#pragma once
#include "../core/mem.h"

struct World;
struct MaterialTable;
struct ViewAssets;

struct Args {
    u64 seed = 1;
    u64 sim_steps = 0;          // --sim N
    const char *record = nullptr;   // --record файл.rec
    const char *replay = nullptr;   // --replay файл.rec
    const char *hashes = nullptr;   // --hashes файл.txt: хеш каждые 1000 шагов
    u64 frames = 0;             // --frames N: окно закрывается после N кадров (CI)
    bool vsync = true;
    bool validation = false;
    const char *shot = nullptr;     // --shot файл.bmp: снимок последнего кадра (с --frames)
    bool cam_set = false;           // --cam yaw pitch dist x z (градусы, метры): камера для снимков
    f32 cam[5] = {};
    bool nopixel = false;           // --nopixel: без правила ячейки
    const char *terrain_map = nullptr;  // --terrain-map файл.bmp: карта земли сверху и выход
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

// Мир из зерна и описаний (data/scene.txt); desc_hash — хеш описаний для записи повтора
bool app_world_create(World &w, AppMemory &m, u64 seed, u64 *desc_hash);
// Материалы и сетки для кадра (после render_init)
bool app_assets_load(AppMemory &m, MaterialTable &mats, ViewAssets &view);
// Земля мира — в видеопамять (после render_init)
bool app_terrain_upload(const World &w);

int run_window(const Args &a, AppMemory &m);
int run_sim(const Args &a, AppMemory &m);
int run_replay(const Args &a, AppMemory &m);
int run_terrain_map(const Args &a, AppMemory &m);

// Записать запись повтора в файл; false — ошибка с сообщением в журнале
struct Recorder;
bool save_record(const Recorder &r, const char *path, Arena &scratch);
