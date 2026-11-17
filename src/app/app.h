// Приложение: режимы запуска (окно, --sim, --replay) и общее для них
#pragma once
#include "../core/mem.h"
#include "../ai/duel.h"
#include "../ai/group.h"

struct World;
struct MaterialTable;
struct ViewAssets;

struct Args {
    u64 seed = 1;
    u64 sim_steps = 0;          // --sim N
    const char *record = nullptr;   // --record файл.rec
    const char *replay = nullptr;   // --replay файл.rec
    const char *hashes = nullptr;   // --hashes файл.txt: хеш каждые 1000 шагов
    u32 duel = 0;                   // --duel N: с --sim вместо подставного игрока N пар бьются друг с другом (этап 11)
    u32 lineup = 0;                 // --lineup N: ряд из N людей на площадке Остова вместо людей старта (отладка)
    u32 brawl[2] = {0, 0};          // --brawl AxB: стенка на стенку, A против B (этап 12; с --sim — бенч, в окне — видео)
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
    bool cells = false;             // --cells: старое правило ячеек (свет раз на ячейку), для сравнения
    i32 aa = -1;                    // --aa off|fxaa|taa: сглаживание поверх настройки; −1 — из настроек
    bool test_lights = false;       // --lights: тестовые огни вокруг камеры
    bool lamps_off = false;         // --nolamps: лампы карты не светят
    bool froxel_heat = false;       // --heat: тепловая карта огней по клеткам froxels
    bool nogcache = false;          // --nogcache: без кеша поверхности земли (сравнение)
    bool scans = false;             // --scans: поверхности из сканов CC0 (docs/scans.md), в F3 — переключатель
    u32 shadow_pages = 0;           // --smpages: страницы теней поверх кадра, --smonly: только тень (docs/shadow.md)
    u32 sdf_view = 0;               // --sdfsky, --sdffield, --nosdf: поле расстояний мира (docs/sdf.md, RENDER_SDF_*)
    u32 gi_view = 0;                // --gionly, --nogi: зонды непрямого света (docs/gi.md, RENDER_GI_*)
    bool local_off = false;         // --nolocal: без местной экспозиции (сравнение, docs/light.md)
    i32 tonemap = -1;               // --tonemap agx|aces: тонмаппинг (снимки для сравнения); −1 — как в F3
    i32 light_quality = -1;         // --light low|medium|high|ultra: качество света; −1 — из настроек
    bool nohud = false;             // --nohud: без интерфейса (видео и снимки мира)
    const char *stand = nullptr;    // --stand off|elbow|knee|mixamo|morphN через запятую: стенд людей (снимки)
    const char *open = nullptr;     // --open menu|keys|debug|world: открыть меню или вкладку панели (снимки интерфейса)
    bool hour_set = false;          // --hour H: час дня в начале (намерением, запись повтора его помнит)
    f32 hour = 0;
    i32 clock = -1;                 // --clock 1|20|120|0: скорость суток с начала (номер в CLOCK_SPEEDS; −1 — обычная)
    const char *timelapse = nullptr;  // --timelapse префикс H0 H1: каждый кадр — свой час, снимок в префикс_NNNN.bmp
    f32 timelapse_hours[2] = {};
    u32 look = 0;         // --look sun|moon град: взгляд по азимуту светила + поворот (1 — солнце, 2 — луна)
    f32 look_yaw = 0.0f;
    f32 spin = 0.0f;      // --spin град/с: камера поворачивается сама (видео мерцания света при повороте, docs/lcache.md)
    f32 pivot_above = -1e9f;          // --pivot м: опора камеры от земли (крупные планы стенда; ниже нуля — под землёй)
    const char *video = nullptr;      // --video префикс: каждый кадр в префикс_NNNN.bmp, мир идёт ровно 1/30 с на кадр
    const char *cam_test = nullptr;   // --cam-test файл.txt: проверка камеры записанным вводом без окна и выход
    bool cam_play = false;            // --cam-play: камера окна по записанному показу (видео камеры, с --video)
    i32 select = -1;                  // --select место: выбрать человека (и --follow — камера за ним)
    bool follow = false;
    const char *mech = nullptr;       // --mech pack|stand|walk|show: шагающая турель в одном режиме (кадры и видео)
    bool people_lines = false;        // --people-lines: капсулы шага, путь выбранного и след стоп (отладка, при отрезках)
    bool sides = false;               // --sides: стороны боя и цели (отладка, при отрезках; с --brawl — сами)
    const char *go = nullptr;         // --go "кадр место x z y аллюр; …": приказы «идти» по кадрам (видео, проверка)
    i64 slow_from = -1;               // --slow кадр: с этого кадра мир ×0.25 (замедленное видео)
    const char *attack = nullptr;     // --attack "кадр место цель; …": приказы «атаковать» по кадрам (видео удара)
    const char *terrain_map = nullptr;  // --terrain-map файл.bmp: карта земли сверху и выход
    const char *walk_map = nullptr;     // --walk-map файл.bmp: сетка ходимости у Остова и выход
    bool mech_follow = false;         // --mech-follow: опора камеры идёт за первой турелью (видео)
    const char *mech_trace = nullptr;   // --mech-trace файл.txt: след движения шагающей турели без окна (с --frames N) и выход
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

// Мир из зерна и описаний (data/scene.txt); desc_hash — хеш описаний для записи повтора. erode_cache — файл
// кеша эрозии земли (docs/startup.md), только у окна; nullptr — эрозия считается всегда
bool app_world_create(World &w, AppMemory &m, u64 seed, u64 *desc_hash, const char *erode_cache = nullptr);
// Скелет из data/ без тела и GPU (след движения машины)
struct Skel;
bool app_skel_load(const char *rel, AppMemory &m, Skel &out);
// Действия людей (data/actions/*.txt, этап 10) и дорожки тел — в w.actions. Зовёт app_world_create; повторно —
// горячая перезагрузка описаний в окне (отладка: запись повтора после неё не сходится с описаниями)
bool app_actions_load(World &w, AppMemory &m);
// ИИ боя: поединок (data/duel.txt, этап 11) и группа (data/group.txt, этап 12); повторно — горячая перезагрузка
// (F5, вместе с действиями). Ошибка в любом — оба старые
bool app_fight_load(DuelDesc &d, GroupDesc &g, AppMemory &m);
// Материалы и сетки для кадра (после render_init)
bool app_assets_load(AppMemory &m, MaterialTable &mats, ViewAssets &view);
// Земля мира — в видеопамять (после render_init)
bool app_terrain_upload(const World &w);

int run_window(const Args &a, AppMemory &m);
int run_sim(const Args &a, AppMemory &m);
int run_replay(const Args &a, AppMemory &m);
int run_terrain_map(const Args &a, AppMemory &m);
int run_walk_map(const Args &a, AppMemory &m);
int run_mech_trace(const Args &a, AppMemory &m);
int run_cam_test(const Args &a, AppMemory &m);  // cam_test.cpp
struct Input;
void cam_show_input(f32 t, f32 t_prev, f32 t_next, f32 dt, i32 w, i32 h, Input &in);

// Записать запись повтора в файл; false — ошибка с сообщением в журнале
struct Recorder;
bool save_record(const Recorder &r, const char *path, Arena &scratch);
