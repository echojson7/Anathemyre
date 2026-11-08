// Отладка: стенды, кнопки и подстановка ввода. Игра собирается без неё (NODEBUG=1 → ANA_NODEBUG), шов 7
#pragma once
#include "../core/dbgdraw.h"
#include "../core/mem.h"
#include "../core/prof.h"
#include "../core/rng.h"
#include "../platform/platform.h"
#include "../sim/intent.h"
#include "../sim/terrain.h"

struct DebugState {
    // Что держится между кадрами (правда — в настройках, run_window копирует туда и обратно)
    bool lines = true;        // L — отладочные отрезки
    bool pixel = true;        // P — правило ячейки
    bool test_lights = false; // тестовые огни вокруг камеры (froxels)
    bool froxel_heat = false; // тепловая карта огней по клеткам froxels
    bool gcache_off = false;  // без кеша поверхности земли (сравнение, не сохраняется)
    // Стенд людей (stand.cpp): человек у руины, кость согнута (B), скелет, морф (0 — нет, k — морф k − 1)
    bool stand = true;
    bool stand_bend = false;
    u32 stand_bone = 0;       // 0 — локоть, 1 — колено
    u32 stand_skel = 0;       // 0 — game_engine, 1 — как у Mixamo
    u32 stand_morph = 0;
    u32 tab = 0;              // вкладка панели: DEBUG_TAB_*
    // Просьбы этого кадра от клавиш и панели (debug_clear_frame после исполнения)
    bool step_once = false;   // «.» на паузе — один шаг
    i32 clock_hours = 0;      // [ ] — часы назад и вперёд на час
    bool clock_speed = false; // T — следующая скорость суток
    i32 clock_speed_set = -1; // панель: скорость суток по номеру в CLOCK_SPEEDS
    bool hour_set = false;    // панель: поставить час
    f64 hour = 0;
    bool pause_toggle = false;
    i32 speed_set = -1;       // панель: скорость мира по номеру
    bool flight_toggle = false;
};
inline void debug_clear_frame(DebugState &d) {
    d.step_once = false;
    d.clock_hours = 0;
    d.clock_speed = false;
    d.clock_speed_set = -1;
    d.hour_set = false;
    d.pause_toggle = false;
    d.speed_set = -1;
    d.flight_toggle = false;
}
// Скорости суток по T: тиков часов за шаг (1 — обычный ход: сутки за 48 минут; 60 — за 48 секунд)
constexpr u32 CLOCK_SPEEDS[] = {1, 20, 120, 0};
enum DebugTab : u32 { DEBUG_TAB_MAIN, DEBUG_TAB_FRAME, DEBUG_TAB_WORLD, DEBUG_TABS };

// Строк проходов GPU и переполнений в панели
constexpr u32 DEBUG_MAX_PASSES = 16, DEBUG_MAX_OVERFLOWS = 8, DEBUG_MAX_FRAME_PARTS = PROF_MAX_PARTS;
// Что панель показывает: числа кадра (раз в секунду) и состояние, которое знает run_window
struct DebugInfo {
    f64 fps, frame_ms, frame_max_ms, cpu_ms, gpu_ms, step_ms;
    f64 gpu_max_ms;           // худший кадр GPU за секунду
    const FrameProf *prof;    // кадр по частям (вкладка «Кадр»), nullptr — нет
    const char *gpu_name;
    const char *unfair;       // включённые отладочные режимы (docs/budget.md); пусто — замер честный
    u64 step, hash;
    f64 hour;
    u32 clock_speed;          // номер в CLOCK_SPEEDS
    bool paused;
    u32 speed;                // номер скорости мира
    const f32 *speeds;
    u32 nspeeds;
    bool cam_free;
    f64 cam[3];               // глаз камеры, метры от начала карты
    f64 cam_above;            // над землёй, м
    u32 passes;               // проходы GPU прошлого кадра (render_gpu_passes)
    const char *pass_names[DEBUG_MAX_PASSES];
    f64 pass_ms[DEBUG_MAX_PASSES];
    u32 vis_clusters;         // видимых кластеров (кадр мира)
    // Работа освещения прошлой секунды (render_work, docs/tiles.md); work = false — ещё не прочитана
    bool work;
    u32 work_tiles[5];        // плитки: небо, земля, сетки, смесь, земля на пиксель
    u32 work_px[3];           // пиксели: небо, земля, сетки
    u32 work_surf[2];         // поверхности (surface() и свет раз на ячейку): земля, сетки
    u32 work_shadow;           // выборки карты горизонта (тень земли, docs/horizon.md)
    u32 work_gc[4];           // кеш земли: страницы кадра, запечено, без места, поверхности мимо кеша
    u32 work_skin;            // вершин скинено (люди)
    const char *gc_off;       // почему кеш земли выключен (nullptr — включён)
    // Что не влезло за секунду (render_overflows): сколько просили, сколько есть (0 — счёт клеток без предела)
    u32 overflows;
    const char *over_what[DEBUG_MAX_OVERFLOWS];
    u64 over_want[DEBUG_MAX_OVERFLOWS], over_cap[DEBUG_MAX_OVERFLOWS];
};

struct World;
struct ViewAssets;
struct RenderFrame;
// Осмотр мира (вкладка «Мир»): только читает столбцы мира (world_columns), ничего в мире не решает
struct WorldInspect {
    u32 table = 1;            // выбранная таблица (номер в списке таблиц)
    i32 row = -1;             // выбранная строка, −1 — нет
    u64 col_hash[64];         // хеши столбцов прошлого кадра
    u32 col_changed[64];      // кадр последнего изменения столбца
    u32 frame = 0;
};
#ifndef ANA_NODEBUG
// Клавиши отладки за кадр
void debug_keys(const Input &in, DebugState &d);
// Подставной игрок для --sim: намерения из своего RNG (не RNG мира) по номеру шага
void autoplay_intents(u64 step, Rng &rng, FixedArray<Intent> &out);
void debug_print_help();
// Панель отладки (F3) справа сверху: числа кадра и мира, управление временем, сутками, камерой и видом;
// вкладка «Мир» — осмотр таблиц. Пишет только просьбы в DebugState
void debug_panel(const DebugInfo &info, const World &w, DebugState &d, WorldInspect &wi);
// Метка выбранной строки в мире (столбик и крест отладочными отрезками)
void debug_inspect_mark(const World &w, const WorldInspect &wi, DebugDraw &dbg);
// Выбрать строку таблицы точек ближайшей к точке на земле (клик в режиме осмотра)
void debug_inspect_pick(const World &w, WorldInspect &wi, const WPos &at);
// Снимок кадра (render_capture_next перед кадром) → файл .bmp
bool debug_write_shot(const char *path, Arena &scratch);
// Карта земли сверху (отмывка рельефа и маски) → .bmp
bool debug_write_terrain_map(const Terrain &t, const char *path, Arena &scratch);
// Стенд людей (stand.cpp, docs/people.md «Стенд»): человек у руины — экземпляр, палитра и морфы в кадр
void debug_stand(const DebugState &d, const ViewAssets &assets, const Terrain &ground, Arena &frame, RenderFrame &rf);
// --stand: off, elbow, knee, mixamo, morphN через запятую (снимки и CI); nullptr — как есть
void debug_stand_args(DebugState &d, const char *spec);
#else
inline void debug_keys(const Input &, DebugState &) {}
inline void autoplay_intents(u64, Rng &, FixedArray<Intent> &) {}
inline void debug_print_help() {}
inline void debug_panel(const DebugInfo &, const World &, DebugState &, WorldInspect &) {}
inline void debug_inspect_pick(const World &, WorldInspect &, const WPos &) {}
inline void debug_inspect_mark(const World &, const WorldInspect &, DebugDraw &) {}
inline bool debug_write_shot(const char *, Arena &) { return false; }
inline bool debug_write_terrain_map(const Terrain &, const char *, Arena &) { return false; }
inline void debug_stand(const DebugState &, const ViewAssets &, const Terrain &, Arena &, RenderFrame &) {}
inline void debug_stand_args(DebugState &, const char *) {}
#endif
