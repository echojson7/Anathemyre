// Сетка ходимости (docs/people.md, «Путь»): клетки 0.5 м на области 512 × 512 м вокруг построек, в клетке до
// четырёх уровней по высоте (земля, пол, этажи руины). Строится при создании мира из ground_at() и коллайдеров;
// решения — целыми числами: высота в см, шаг между соседями ≤ 40 см (по земле ≤ 29 см — уклон ~30°), просвет
// над уровнем ≥ 1.9 м, капсула r 0.3 м (выше плеч — голова r 0.12) не задевает стен. Хеш сетки — в хеше описаний и в tests/sim_hashes.txt.
// A* по узлам «клетка + уровень» с картой посещённых в арене шага, путь сглаживается проверкой прямой видимости
#pragma once
#include "collide.h"

constexpr u32 WALK_N = 1024;           // клеток на сторону области (2 чанка)
constexpr f32 WALK_CELL_M = 0.5f;
constexpr u32 WALK_CELLS_PER_CHUNK = 512;
constexpr u32 WALK_LEVELS = 4;
constexpr u32 WALK_MAX_AREAS = 4;
constexpr i32 WALK_STEP_CM = 40;       // между соседями, если хоть один уровень — коробка: на марше (проступь
                                       // 0.28, подъём 0.18) клетки 0.5 м расходятся до двух ступеней, 36 см
constexpr i32 WALK_SLOPE_CM = 29;      // по земле между соседями (уклон ~30°)
constexpr f32 WALK_CLEAR_M = 1.9f;     // просвет над уровнем
constexpr f32 WALK_SHOULDER_M = 1.5f;  // выше — голова: ей хватает круга WALK_HEAD_R_M
constexpr f32 WALK_HEAD_R_M = 0.12f;
constexpr f32 WALK_RADIUS_M = 0.3f;    // капсула человека
constexpr f32 WALK_KNEE_M = 0.6f;      // от шага до колена коробку держит только стопа r 0.1 (ступень впереди не
constexpr f32 WALK_FOOT_R_M = 0.1f;    // мешает, край цоколя держит); выше колена — тело r 0.3
constexpr i16 WALK_NONE = -32768;

// Область: угол — чанк (cx0, cz0), клетка i по x, j по z; у клетки nlev уровней по возрастанию высоты
struct WalkArea {
    i32 cx0, cz0;
    i32 base_cm;       // высоты уровней — от неё, см
    u8 *nlev;          // N²
    i16 *h;            // N² · 4: высота уровня, см от base_cm
    u8 *box;           // N² · 4: 1 — уровень на коробке (пол постройки), 0 — земля
    u32 *edge;         // N² · 4: биты 0–7 — можно шагнуть к соседу k (WALK_DIRS), биты 8 + 2k — его уровень
};
// Соседи клетки по порядку битов рёбер: сначала четыре прямых, потом диагонали
constexpr i32 WALK_DIRS[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};

struct WalkGrid {
    u32 count = 0;
    WalkArea area[WALK_MAX_AREAS];
    u64 hash = 0;
    u32 levels_total = 0;  // для журнала
};

struct Terrain;
// Области вокруг скоплений коробок (по порядку коробок) и их клетки
void walk_build(WalkGrid &g, const Terrain &t, const Colliders &c, Arena &arena);

// Узел пути: клетка (j·N + i) · 4 + уровень
inline u32 walk_node(u32 i, u32 j, u32 lev) { return ((j * WALK_N + i) << 2) | lev; }
inline u32 walk_node_i(u32 n) { return (n >> 2) % WALK_N; }
inline u32 walk_node_j(u32 n) { return (n >> 2) / WALK_N; }
inline u32 walk_node_lev(u32 n) { return n & 3u; }

// Область точки (−1 — вне областей) и клетка в ней
i32 walk_area_of(const WalkGrid &g, i32 cx, i32 cz, f32 x, f32 z, u32 *i, u32 *j);
// Ходимый узел у точки: уровень ближе всего к y; если клетка не ходима — ближайшая ходимая в радиусе r клеток
bool walk_node_near(const WalkArea &a, u32 i, u32 j, f32 y, u32 r, u32 *node);
// Середина узла в мире (высота — уровня)
WPos walk_node_pos(const WalkArea &a, u32 node);

struct WalkSearch {
    u32 max_expand = 12000;   // предел раскрытых узлов на один поиск (~80 × 80 м открытого места)
    u32 expanded = 0;         // сколько раскрыто (журнал, бенч)
    bool reached = false;     // false — путь до ближайшего к цели узла
};
// A* от узла к узлу; путь (узлы после сглаживания, без начального) — в out, не больше cap. Память — scratch
// (откатывается). Возвращает число узлов, 0 — идти некуда (уже у цели)
u32 walk_find(const WalkArea &a, u32 from, u32 to, Arena &scratch, WalkSearch &ws, u32 *out, u32 cap);
