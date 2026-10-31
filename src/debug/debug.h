// Отладка и тестовые места (docs/debug.md): бои арены у зала, Мерильня, стенд и ряд людей, редактор внешности,
// отладочные кнопки меню НАСТРОЙКИ, замеры без окна. Слой над view, его видит только app. Мир об отладке
// не знает: она меняет мир только в крючке шага (World.hook), очередью команд. У арены свои вещи на земле и своя
// физика, в мир они не попадают, поэтому арена не идёт в сохранение. Сборка NODEBUG=1 — без модуля (nodebug.c)
#ifndef ANA_DEBUG_H
#define ANA_DEBUG_H

#include "../platform/platform.h"
#include "../render/render.h"
#include "../sim/arena_ring.h"
#include "../view/camera.h"
#include "../view/figures.h"
#include "../view/hud.h"
#include "../world/world.h"

extern const bool g_debug;  // модуль есть в сборке

// Отладка на старте, из командной строки
typedef struct DebugStart {
    bool humans;                // ряд людей для сравнения
    u32  stand;                 // стенд фигур: ряд + 1, 0 — нет
    f32  hall_t;                // сдвиг часов Мерильни, с: кадр нужной фазы
    bool bands_test;            // пробные отряды вдали на первом шаге
    u32  phys_drop, loot_drop;  // столько лома и вещей у точки взгляда на первом шаге
} DebugStart;

// Что стало со стендом фигур за кадр (debug_update)
typedef enum DebugStand { DEBUG_STAND_SAME, DEBUG_STAND_SHOWN, DEBUG_STAND_HIDDEN } DebugStand;

typedef struct Debug Debug;

// --bench <имя> [числа]: замер по сценарию из data/debug/bench.txt, числа заменяют числа сценария по порядку.
// Замер без окна — код выхода; DEBUG_BENCH_FRAME и DEBUG_BENCH_STAGES — замер кадра в окне (app/bench.c)
enum { DEBUG_BENCH_FRAME = -1, DEBUG_BENCH_STAGES = -2 };
int  debug_bench(const char *data_dir, const char *name, char **args, u32 nargs, u64 seed, u32 threads);

// После world_init и figures_init: арена, Мерильня; крючок шага ставится в мир. NULL — нет памяти (или нет модуля)
Debug *debug_init(Arena *perm, MeshBuilder *mb, const char *data_dir, World *w, const DebugStart *st);
// Арена для вида (фигуры, интерфейс, звук); NULL — арены нет
ArenaSession *debug_arena(Debug *d);
ArenaRing    *debug_ring(Debug *d);

// Просьбы меню прошлого кадра: ряд людей, стенд (камера к нему)
DebugStand debug_update(Debug *d, RtsCamera *cam);
// Кнопки раздела ОТЛАДКА меню НАСТРОЙКИ: пробные отряды, лом и вещи, Мерильня, ряд людей, стенд
void debug_menu(Debug *d, Hud *h, RtsCamera *cam);
// Кнопка камеры в Мерильню в разделе СВЕТ
void debug_menu_light(HudForm *f, RtsCamera *cam);
// Окно ВНЕШНОСТЬ: между menu_input и hud_input; если мышь занята окном, клики из *in убраны
void debug_input(Debug *d, Hud *h, Input *in, World *w, RtsCamera *cam);
// За кем держать камеру (номер в таблице людей), -1 — ни за кем
i32  debug_follow(const Debug *d);
// Вид боя арены для кадра, после world_update
void debug_present(Debug *d, const World *w, f32 dt);

// Пол Мерильни; вне её — base
f32  debug_ground(f32 x, f32 z, f32 base);
// Глаз камеры не выходит из Мерильни сквозь стены, пока точка взгляда внутри
V3   debug_clamp_eye(V3 target, V3 eye);
// Помещение без неба для рендера (Мерильня)
void debug_room(RoomDark *r);
// Лампы Мерильни
u32  debug_lights(f32 time, PointLight *out, u32 max);
// Мерильня (фигуры — если people), вещи арены, ряд людей и стенд. dt — шаг мира кадра (0 — мир стоит)
u32  debug_draw(Debug *d, const CharMeshes *cm, const World *w, V3 eye, f32 time, f32 dt, bool people, DrawItem *items);
// Подписи Мерильни
void debug_labels(const Debug *d, M4 view_proj, V3 eye);

#endif
