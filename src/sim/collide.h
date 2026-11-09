// Коллайдеры построек для шага мира (docs/people.md, «Опора и столкновения»): коробки COLL всех Props в осях
// мира и сетка 2 м по чанкам. Строятся при создании мира и дальше не меняются (как земля); хеш — в хеше описаний.
// Коробка повёрнута только вокруг вертикали: наклонную коробку сетки (балка) заменяет её объём в осях детали.
// Опора человека — support_at(): выше из ground_at() и верха коробок под точкой, шаг вверх ≤ 0.35 м
#pragma once
#include "../core/mem.h"
#include "../core/wpos.h"

constexpr u32 COLL_MAX_BOXES = 8192;
constexpr u32 COLL_MAX_TILES = 32;      // чанков 256 м с коробками
constexpr u32 COLL_TILE_N = 128;        // клеток 2 м на сторону чанка
constexpr f32 COLL_CELL_M = 2.0f;
constexpr f32 COLL_MARGIN_M = 0.5f;     // коробка в клетках с запасом: точка видит всё ближе 0.5 м (капсула 0.3)
constexpr f32 STEP_UP_M = 0.35f;        // выше — стена, не ступень

// Коробка на входе построения: середина (с высотой), половины, курс
struct CollBoxIn {
    WPos at;
    f32 hx, hy, hz;
    Turn yaw;
};

struct Colliders {
    u32 count = 0;
    i32 *cx, *cz;
    f32 *x, *z;        // середина в чанке
    f32 *y0, *y1;      // низ и верх от нуля карты
    f32 *hx, *hz;      // половины в своих осях
    f32 *c, *s;        // cos и sin курса: мир = lx·(c, −s) + lz·(s, c)
    u32 tiles = 0;
    i32 tile_cx[COLL_MAX_TILES], tile_cz[COLL_MAX_TILES];
    u32 *cell_start;   // tiles·N² + 1: начало списка клетки в items
    u16 *items;        // номера коробок по клеткам, по возрастанию
    u64 hash = 0;
};

// Построить из коробок (в порядке деталей сцены). false — коробок или чанков больше предела
bool colliders_build(Colliders &c, const CollBoxIn *boxes, u32 n, Arena &arena, Arena &scratch);
// Коробки, которые могут быть ближе COLL_MARGIN_M к точке (cx, cz, x, z); пусто вне чанков с коробками
Span<const u16> colliders_near(const Colliders &c, i32 cx, i32 cz, f32 x, f32 z);
// Точка в своих осях коробки k (без высоты)
void coll_local(const Colliders &c, u32 k, i32 cx, i32 cz, f32 x, f32 z, f32 *lx, f32 *lz);
// Точка над следом коробки k (в плане)
bool coll_covers(const Colliders &c, u32 k, i32 cx, i32 cz, f32 x, f32 z);
// Круг радиуса r пересекает след коробки k в плане
bool coll_touches(const Colliders &c, u32 k, i32 cx, i32 cz, f32 x, f32 z, f32 r);

struct Terrain;
// Опора: kind 0 — земля, k + 1 — верх коробки k
struct Support {
    f32 height;
    u32 kind;
};
// Выше из земли и верхов коробок под точкой, не выше from_y + STEP_UP_M (кроме земли: в неё не проваливаются)
Support support_at(const Terrain &t, const Colliders &c, i32 cx, i32 cz, f32 x, f32 z, f32 from_y);
// Вытолкнуть круг радиуса r в плане из коробок, которые пересекают высоты (lo, hi). Возвращает true, если толкнул
bool coll_push_circle(const Colliders &c, i32 cx, i32 cz, f32 &x, f32 &z, f32 r, f32 lo, f32 hi);
