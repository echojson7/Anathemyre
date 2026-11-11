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
    bool rewind_check = false;      // --rewind-check: с --sim проверить перемотку окна против прямого хода
    u64 frames = 0;             // --frames N: окно закрывается после N кадров (CI)
    u64 win_w = 1600, win_h = 900;  // --window W H: размер окна (CI берёт меньше: lavapipe считает пиксели)
    bool vsync = true;
    bool validation = false;
    bool gpu_validation = false;  // --gpuav: слои проверки с проверкой на GPU
    const char *gpu_validation_skip = nullptr;  // --gpuav-skip: шейдеры без неё, через запятую
    const char *shot = nullptr;     // --shot файл.bmp: снимок последнего кадра (с --frames)
    const char *work = nullptr;     // --work файл.txt: счётчики работы освещения последнего кадра мира (CI, docs/tiles.md)
    bool cam_set = false;           // --cam yaw pitch dist x z (градусы, метры): камера для снимков
    f32 cam[5] = {};
    bool nopixel = false;           // --nopixel: без правила ячейки
    bool test_lights = false;       // --lights: тестовые огни вокруг камеры
    bool froxel_heat = false;       // --heat: тепловая карта огней по клеткам froxels
    bool nogcache = false;          // --nogcache: без кеша поверхности земли (сравнение)
    u32 shadow_pages = 0;           // --smpages: страницы теней поверх кадра, --smonly: только тень (docs/shadow.md)
    i32 tonemap = -1;               // --tonemap agx|aces: тонмаппинг (снимки для сравнения); −1 — как в F3
    i32 light_quality = -1;         // --light low|medium|high|ultra: качество света; −1 — из настроек
    bool nohud = false;             // --nohud: без интерфейса (видео и снимки мира)
    const char *stand = nullptr;    // --stand off|elbow|knee|mixamo|morphN через запятую: стенд людей (снимки)
    const char *open = nullptr;     // --open menu|keys|debug|world: открыть меню или вкладку панели (снимки интерфейса)
    bool hour_set = false;          // --hour H: час дня в начале (намерением, запись повтора его помнит)
    f32 hour = 0;
    const char *timelapse = nullptr;  // --timelapse префикс H0 H1: каждый кадр — свой час, снимок в префикс_NNNN.bmp
    f32 timelapse_hours[2] = {};
    f32 pivot_above = -1.0f;          // --pivot м: опора камеры над землёй (крупные планы стенда)
    const char *video = nullptr;      // --video префикс: каждый кадр в префикс_NNNN.bmp, мир идёт ровно 1/30 с на кадр
    i32 select = -1;                  // --select место: выбрать человека (и --follow — камера за ним)
    bool follow = false;
    bool people_lines = false;        // --people-lines: капсулы шага, путь выбранного и след стоп (отладка, при отрезках)
    const char *go = nullptr;         // --go "кадр место x z y аллюр; …": приказы «идти» по кадрам (видео, проверка)
    i64 slow_from = -1;               // --slow кадр: с этого кадра мир ×0.25 (замедленное видео)
    const char *attack = nullptr;     // --attack "кадр место цель; …": приказы «атаковать» по кадрам (видео удара)
    const char *terrain_map = nullptr;  // --terrain-map файл.bmp: карта земли сверху и выход
    const char *walk_map = nullptr;     // --walk-map файл.bmp: сетка ходимости у руины и выход
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
// Действия людей (data/actions/*.txt, этап 10) и дорожки тел — в w.actions. Зовёт app_world_create; повторно —
// горячая перезагрузка описаний в окне (отладка: запись повтора после неё не сходится с описаниями)
bool app_actions_load(World &w, AppMemory &m);
// Материалы и сетки для кадра (после render_init)
bool app_assets_load(AppMemory &m, MaterialTable &mats, ViewAssets &view);
// Земля мира — в видеопамять (после render_init)
bool app_terrain_upload(const World &w);

int run_window(const Args &a, AppMemory &m);
int run_sim(const Args &a, AppMemory &m);
int run_replay(const Args &a, AppMemory &m);
int run_terrain_map(const Args &a, AppMemory &m);
int run_walk_map(const Args &a, AppMemory &m);

// Записать запись повтора в файл; false — ошибка с сообщением в журнале
struct Recorder;
bool save_record(const Recorder &r, const char *path, Arena &scratch);
