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
    u32 shadow_pages = 0;     // тени (docs/shadow.md): 0 — кадр, 1 — страницы поверх, 2 — только тень (не сохраняется)
    u32 tonemap = 0;          // тонмаппинг: 0 — AgX, 1 — ACES (RENDER_TONEMAP_*; docs/light.md), выбор по кадрам
    // Стенд людей (stand.cpp): человек у Остова, кость согнута (B), скелет, морф (0 — нет, k — морф k − 1)
    bool people_lines = false; // люди мира: капсулы шага, путь выбранного, след касаний стоп (при отрезках L)
    bool people_sides = false; // группа (этап 12): круг стороны под ногами, линия к цели, место вокруг цели
    bool stand = false;       // выкл.: у людей мира есть тела (9б); включается в F3 и --stand
    bool stand_bend = false;
    u32 stand_bone = 0;       // 0 — локоть, 1 — колено
    u32 stand_skel = 0;       // 0 — game_engine, 1 — как у Mixamo
    u32 stand_morph = 0;
    // Клипы на стенде (docs/anim.md): номер в списке имён (0 — нет), второй — для смешивания с долей blend
    u32 stand_clip = 0;
    u32 stand_clip_b = 0;
    f32 stand_blend = 0.5f;
    u32 stand_speed = 0;      // номер в STAND_SPEEDS
    bool stand_pause = false;
    bool stand_restart = false;
    bool stand_inplace = true;  // на месте: корень клипа только поворачивает
    bool stand_caps = true;     // капсулы и клинок из запечённых дорожек
    bool stand_both = false;    // оба скелета рядом: game_engine слева, Mixamo справа
    u32 tab = 0;              // вкладка панели: DEBUG_TAB_*
    // Просьбы этого кадра от клавиш и панели (debug_clear_frame после исполнения)
    bool step_once = false;   // «.» на паузе — один шаг
    bool step_back = false;   // «,» на паузе — шаг назад (перемотка)
    bool reload_actions = false;  // F5 — перечитать описания действий (data/actions, метки клипов)
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
    d.step_back = false;
    d.reload_actions = false;
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
// Скорости клипа на стенде
constexpr f32 STAND_SPEEDS[] = {1.0f, 0.5f, 0.25f, 0.1f};
enum DebugTab : u32 { DEBUG_TAB_MAIN, DEBUG_TAB_FRAME, DEBUG_TAB_WORLD, DEBUG_TABS };

// Строк проходов GPU и переполнений в панели
constexpr u32 DEBUG_MAX_PASSES = 20, DEBUG_MAX_OVERFLOWS = 8, DEBUG_MAX_FRAME_PARTS = PROF_MAX_PARTS;
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
    u32 work_sm[8];           // тени: страниц кадра, нарисовано, без места, выборок без страницы, вызовов, выборок,
                              // отложено стройкой нового шага светила, перетекание на него (%)
    const char *gc_off;       // почему кеш земли выключен (nullptr — включён)
    // Глаз прочитанного кадра (render_light, docs/light.md): освещённость земли, лк, и EV100; light = false — нет
    bool light;
    f32 lux_key, lux_sky, ev100, ev100_goal;
    // Что не влезло за секунду (render_overflows): сколько просили, сколько есть (0 — счёт клеток без предела)
    u32 overflows;
    const char *over_what[DEBUG_MAX_OVERFLOWS];
    u64 over_want[DEBUG_MAX_OVERFLOWS], over_cap[DEBUG_MAX_OVERFLOWS];
};

struct World;
struct ViewAssets;
struct RenderFrame;
// Перемотка мира назад (rewind.cpp): кольцо снимков столбцов и журнал намерений
constexpr u32 REWIND_SNAPS = 100, REWIND_EVERY = 6, REWIND_LOG = 32768;  // 10 с назад (с намерениями ИИ всех бойцов)
struct Rewind {
    u8 *buf = nullptr;
    usize snap_bytes = 0;
    u64 snap_step[REWIND_SNAPS] = {};
    u32 head = 0, count = 0;
    Intent *log = nullptr;
    u32 log_head = 0, log_count = 0;
    bool ok = false;
};
// Осмотр мира (вкладка «Мир»): только читает столбцы мира (world_columns), ничего в мире не решает
struct WorldInspect {
    u32 table = 1;            // выбранная таблица (номер в списке таблиц)
    i32 row = -1;             // выбранная строка, −1 — нет
    u64 col_hash[128];         // хеши столбцов прошлого кадра
    u32 col_changed[128];      // кадр последнего изменения столбца
    u32 frame = 0;
};
// Бенч группы (--brawl, этап 12): счёт по шагам — толпятся ли нападающие и ждут ли хором
struct BrawlStats {
    u64 ticks = 0;
    u64 crowd_ticks = 0;    // шаго-целей с двумя и больше нападающими рядом (ближе 4.5 м)
    u64 idle_ticks = 0;     // из них никто не бьёт, не идёт бить, не защищается и не в реакции (все ждут хором)
    u64 over_ticks = 0;     // из них бьющих разом больше жетонов
    u64 close_ticks = 0;    // из них двое нападающих ближе 0.9 м друг к другу (толпятся)
    u32 strikers_max = 0;   // наибольшее число бьющих одного разом
    f64 gap_sum = 0;        // наименьший угол между соседями-нападающими, град (среднее — gap_sum / gap_n)
    u64 gap_n = 0;
    u32 switches = 0;       // смен цели
    u32 prev[1024] = {};    // цели прошлого шага (PEOPLE_CAPACITY)
};
struct GroupDesc;

#ifndef ANA_NODEBUG
// Стенка на стенку (--brawl AxB): в шаге 0 две стороны рядами с шагом 2.5 м, 12 м друг от друга, с серединой в
// (x0, z0) м от середины Остова (по умолчанию — дюны к западу). Целей не дают — бойцы со стороной находят врагов сами
void brawl_intents(const World &w, const u32 n[2], FixedArray<Intent> &out, f32 x0 = -40.0f, f32 z0 = -30.0f);
void debug_brawl_step(const World &w, BrawlStats &b);
void debug_brawl_report(const World &w, const BrawlStats &b, const u32 n[2]);
// Клавиши отладки за кадр
void debug_keys(const Input &in, DebugState &d);
// Подставной игрок для --sim: намерения из своего RNG (не RNG мира) по номеру шага
void autoplay_intents(const World &w, Rng &rng, FixedArray<Intent> &out);
// Бенч поединков для --sim --duel N (этап 11): N пар в дюнах у Остова, в шаге 1 первый из пары нападает на второго,
// дальше их ведёт ИИ; в конце — счёт пар по навыку (debug_duel_report)
void duel_intents(const World &w, u32 pairs, FixedArray<Intent> &out);
void debug_duel_report(const World &w, u32 pairs);
// Карта сетки ходимости у Остова и путь от входа на галерею (--walk-map файл.bmp)
bool debug_write_walk_map(const World &w, const char *path, Arena &scratch);
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
// Стенд людей (stand.cpp, docs/people.md «Стенд», docs/anim.md): до view_build — время клипа, поза и капсулы
// дорожек в dbg (nullptr — без капсул); после — экземпляры, палитры и морфы в кадр
void debug_stand_update(DebugState &d, const ViewAssets &assets, const Terrain &ground, f64 dt, DebugDraw *dbg);
void debug_stand(const DebugState &d, const ViewAssets &assets, Arena &frame, RenderFrame &rf);
// Имена клипов для панели (0 — «нет»)
const char *const *debug_stand_clip_names(u32 *count);
// --stand через запятую (снимки, видео, CI): off, elbow, knee, mixamo, morphN, both, move, nocaps, clip=имя,
// blend=имя, w=доля %, speed=100|50|25|10, t=начало в сотых с (< 0 — выдержка), seq=a+b+… (очередь клипов); nullptr — как есть
void debug_stand_args(DebugState &d, const char *spec);
// Люди шага мира капсулами и курсом поверх тела (stand.cpp); у выбранного — путь узлами сетки и цель
void debug_people(const World &w, DebugDraw &dbg, i32 selected);
// Стороны боя (stand.cpp): круг под ногами цветом стороны (1 — красный, 2 — синий, ничьи — серый), линия к цели,
// у ждущего очереди — круг меньше
void debug_sides(const World &w, DebugDraw &dbg);
// Перемотка: память под кольцо; после каждого шага окна — намерения шага и снимок раз в REWIND_EVERY шагов;
// назад — к шагу target (снимок и повтор шагов). false — так далеко снимков нет
void rewind_init(Rewind &r, const World &w);
void rewind_free(Rewind &r);
void rewind_after_step(Rewind &r, const World &w, Span<const Intent> its);
bool rewind_to(Rewind &r, World &w, u64 target, Arena &step_arena);
#else
inline void debug_keys(const Input &, DebugState &) {}
inline void autoplay_intents(const World &, Rng &, FixedArray<Intent> &) {}
inline void duel_intents(const World &, u32, FixedArray<Intent> &) {}
inline void debug_duel_report(const World &, u32) {}
inline void brawl_intents(const World &, const u32[2], FixedArray<Intent> &, f32 = 0.0f, f32 = 0.0f) {}
inline void debug_brawl_step(const World &, BrawlStats &) {}
inline void debug_sides(const World &, DebugDraw &) {}
inline void debug_brawl_report(const World &, const BrawlStats &, const u32[2]) {}
inline bool debug_write_walk_map(const World &, const char *, Arena &) { return false; }
inline void debug_print_help() {}
inline void debug_panel(const DebugInfo &, const World &, DebugState &, WorldInspect &) {}
inline void debug_inspect_pick(const World &, WorldInspect &, const WPos &) {}
inline void debug_inspect_mark(const World &, const WorldInspect &, DebugDraw &) {}
inline bool debug_write_shot(const char *, Arena &) { return false; }
inline bool debug_write_terrain_map(const Terrain &, const char *, Arena &) { return false; }
inline void debug_stand_update(DebugState &, const ViewAssets &, const Terrain &, f64, DebugDraw *) {}
inline void debug_stand(const DebugState &, const ViewAssets &, Arena &, RenderFrame &) {}
inline void debug_stand_args(DebugState &, const char *) {}
inline void debug_people(const World &, DebugDraw &, i32) {}
inline void rewind_init(Rewind &, const World &) {}
inline void rewind_free(Rewind &) {}
inline void rewind_after_step(Rewind &, const World &, Span<const Intent>) {}
inline bool rewind_to(Rewind &, World &, u64, Arena &) { return false; }
#endif
