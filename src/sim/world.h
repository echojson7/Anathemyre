// Мир: таблицы + RNG + номер шага, больше ничего (docs/architecture.md, «Модель симуляции»).
// Таблицы: точки-заглушка этапа 0 (ходят по RNG мира и своим sin/cos) и статичные объекты (этап 1).
#pragma once
#include "../core/dbgdraw.h"
#include "../core/mem.h"
#include "../core/rng.h"
#include "action.h"
#include "collide.h"
#include "people.h"
#include "terrain.h"
#include "walk.h"

constexpr u32 SIM_HZ = 60;
constexpr f32 SIM_DT = 1.0f / 60.0f;
// Меняется при правке шага мира, после которой старые записи не повторяются
constexpr u32 SIM_VERSION = 12;

// Номер сущности: место (младшие 16 бит) + поколение (старшие 16). Связи в мире — только номерами
struct Id {
    u32 v = 0;
};
inline Id make_id(u32 slot, u16 gen) { return {slot | ((u32)gen << 16)}; }
inline u32 id_slot(Id id) { return id.v & 0xFFFFu; }
inline u16 id_gen(Id id) { return (u16)(id.v >> 16); }

constexpr u32 DOTS_CAPACITY = 1024;
constexpr u32 DOTS_TARGET = 1000;

// Точка старта окна (строка start в data/scene.txt, docs/text.md): опора камеры и люди окна у неё.
// Шаг мира её не читает и в хеш она не входит; --sim и --replay её не трогают
struct SceneStart {
    bool set = false;
    f32 x = 0, z = 0;                       // метры от начала карты
    f32 yaw = 0, pitch = 14, dist = 34;     // камера: градусы, метры (как --cam)
    u32 people = 0;                         // сколько людей родить у точки в первом шаге окна
    u32 radius = 4;                         // в каком круге, целые м (как у INTENT_PEOPLE_SPAWN)
};

// Таблица точек, SoA на DOTS_CAPACITY мест. Мёртвое место: alive = 0, его номер в free
struct Dots {
    u16 *gen;      // поколение места, растёт при каждом рождении
    u8 *alive;
    i32 *cx, *cz;  // чанк 256 м
    f32 *x, *z;    // смещение в чанке
    f32 *y;        // высота земли под точкой (ground_at)
    f32 *vx, *vz;  // скорость, м/с
    f32 *speed;    // своя скорость хода, м/с
    u32 *phase;    // курс в долях оборота (Turn)
    u32 *omega;    // поворот курса за шаг (Turn)
    u16 *free;     // стек свободных мест
    u32 free_count;
    u32 live;
};

constexpr u32 PROPS_CAPACITY = 1024;

// Статичные объекты: сетка и материал по имени (устойчивый хеш строки), положение на опоре, поворот, масштаб.
// Шаг мира их пока не двигает; кадр рисует их экземплярами. Ставит описание сцены (sim/scene.h)
struct Props {
    u16 *gen;      // поколение места
    u8 *alive;
    u64 *mesh;     // хеш имени сетки (.mesh)
    u64 *material; // хеш имени материала-подмены, 0 — материалы из вершин сетки
    i32 *cx, *cz;
    f32 *x, *y, *z;  // смещение в чанке и высота от нуля карты
    u32 *yaw;      // поворот вокруг вертикали (Turn)
    f32 *scale;
    u32 *seed;     // зерно разнообразия узора
    u32 count;     // занятые места подряд с начала (удаления пока нет)
};

constexpr u32 DECALS_CAPACITY = 512;

// Декали карты (docs/ostov.md): потёки, грязь, пыль, пятна — коробка проекции с видом узора и материалом по
// имени. Шаг мира их не читает и не меняет; ставит описание сцены (заготовки). Кадр читает таблицу напрямую,
// как землю: после создания мира она не меняется
struct Decals {
    u64 *kind;       // хеш имени вида узора (streak, grime, dust, stain — data/shaders/decal.glsl)
    u64 *material;   // хеш имени материала: цвет, шероховатость, сила (data/materials.txt)
    i32 *cx, *cz;
    f32 *x, *y, *z;  // середина коробки: смещение в чанке и высота от нуля карты
    u32 *yaw;        // поворот коробки вокруг вертикали (Turn)
    f32 *hx, *hy, *hz;  // половины сторон коробки в её осях
    u32 *seed;
    u32 count;
};

// Часы мира (docs/sky.md): целые тики. Тик — шаг при обычном ходе суток; солнце и луна выводятся в кадре
constexpr u32 CLOCK_DAY_TICKS_DEFAULT = 48 * 60 * SIM_HZ;  // сутки — 48 минут реального времени
struct Clock {
    u64 tick;       // тиков от полуночи дня 0
    u32 day_ticks;  // тиков в сутках
    u32 rate;       // тиков за шаг: 1 — обычный ход, 0 — часы стоят, больше — ускорение (отладка)
};
// Час дня (доля суток в оборотах Turn: 2^32 — сутки) → тик того же дня
inline u64 clock_tick_at(const Clock &c, u32 day_turn) {
    u64 day = c.tick / c.day_ticks;
    return day * c.day_ticks + (((u64)day_turn * c.day_ticks) >> 32);
}

struct World {
    Terrain terrain;  // из зерна при создании, дальше не меняется (docs/terrain.md)
    u64 step;      // время мира — целый номер шага, секунды выводятся из него
    Clock clock;   // время суток
    Rng rng;
    // Притяжение точек к точке приказа (намерение игрока)
    u32 attract_on;
    WPos attract_at;
    u64 attract_until;
    Dots dots;
    Props props;
    Decals decals;
    People people;
    // Неизменное после создания мира (как земля): коллайдеры построек, сетка ходимости, длины шага клипов.
    // В хеш шага не входят, их хеши — в хеше описаний записи повтора
    Colliders coll;
    WalkGrid walk;
    GaitTable gait;
    ActionTable actions;  // действия и дорожки тел (этап 10): ставит app до world_build_static
    SceneStart start;  // из сцены, для окна
};

// Земля строится первой (объекты и точки ставятся на неё); scratch — временная память генерации
void world_init(World &w, Arena &arena, u64 seed, const TerrainDesc &terrain, Arena &scratch);
// Мёртвых мест нет в выписке и в шаге; спавн и смерть только через эти две функции
bool dots_spawn(World &w, const WPos &center, f32 radius);
void dots_kill(World &w, u32 slot);
// Поставить объект на землю: высота из ground_at() плюс lift. false — таблица полна
bool props_add(World &w, u64 mesh, u64 material, const WPos &at, f32 lift, u32 yaw, f32 scale, u32 seed);
// То же с высотой at.y от нуля карты (детали заготовки ставятся от высоты её середины, а не от земли под собой)
bool props_add_at(World &w, u64 mesh, u64 material, const WPos &at, u32 yaw, f32 scale, u32 seed);
// Декаль: at — середина коробки (с высотой), h — половины сторон. false — таблица полна
bool decals_add(World &w, u64 kind, u64 material, const WPos &at, u32 yaw, const f32 h[3], u32 seed);
// После сцены: коллайдеры построек (коробки COLL деталей — читает app) и сетка ходимости по ним.
// false — коробок или чанков с ними больше предела
bool world_build_static(World &w, const CollBoxIn *boxes, u32 n, const GaitTable &gait, Arena &arena, Arena &scratch);
// Хеш неизменного: коллайдеры, сетка ходимости, длины шага, действия (в хеш описаний и в эталон --sim)
u64 world_static_hash(const World &w);

// Столбцы мира для хеша и проверки повтора: имя, данные, байты. Порядок — порядок в хеше
// Тип и число значений — только для показа (панель отладки «Мир»), в хеш не входят
enum ColType : u8 { COL_RAW, COL_U8, COL_U16, COL_U32, COL_I32, COL_U64, COL_F32 };
struct Column {
    const char *name;
    const void *data;
    usize bytes;
    ColType type = COL_RAW;
    u32 count = 0;  // значений; у COL_RAW — 0
};
template <typename T> constexpr ColType col_type() {
    if constexpr (sizeof(T) == 1) return COL_U8;
    else if constexpr (sizeof(T) == 2) return COL_U16;
    else if constexpr (__is_same(T, f32)) return COL_F32;
    else if constexpr (__is_same(T, i32)) return COL_I32;
    else if constexpr (__is_same(T, u32)) return COL_U32;
    else if constexpr (__is_same(T, u64)) return COL_U64;
    else return COL_RAW;
}
// Столбец из n значений типа T (у составных — COL_RAW, байты целиком)
template <typename T> Column column(const char *name, const T *p, u32 n) {
    ColType t = col_type<T>();
    return {name, p, n * sizeof(T), t, t == COL_RAW ? 0u : n};
}
constexpr u32 MAX_COLUMNS = 128;
u32 world_columns(const World &w, Column *out);
void world_column_hashes(const World &w, u64 *out, u32 count);
u64 world_hash(const World &w);
