// Мир: таблицы + RNG + номер шага, больше ничего (docs/architecture.md, «Модель симуляции»).
// Пока одна таблица-заглушка — точки, которые ходят по RNG мира и своим sin/cos (этап 0).
#pragma once
#include "../core/dbgdraw.h"
#include "../core/mem.h"
#include "../core/rng.h"

constexpr u32 SIM_HZ = 60;
constexpr f32 SIM_DT = 1.0f / 60.0f;
// Меняется при правке шага мира, после которой старые записи не повторяются
constexpr u32 SIM_VERSION = 1;

// Номер сущности: место (младшие 16 бит) + поколение (старшие 16). Связи в мире — только номерами
struct Id {
    u32 v = 0;
};
inline Id make_id(u32 slot, u16 gen) { return {slot | ((u32)gen << 16)}; }
inline u32 id_slot(Id id) { return id.v & 0xFFFFu; }
inline u16 id_gen(Id id) { return (u16)(id.v >> 16); }

constexpr u32 DOTS_CAPACITY = 1024;
constexpr u32 DOTS_TARGET = 1000;

// Таблица точек, SoA на DOTS_CAPACITY мест. Мёртвое место: alive = 0, его номер в free
struct Dots {
    u16 *gen;      // поколение места, растёт при каждом рождении
    u8 *alive;
    i32 *cx, *cz;  // чанк 256 м
    f32 *x, *z;    // смещение в чанке
    f32 *vx, *vz;  // скорость, м/с
    f32 *speed;    // своя скорость хода, м/с
    u32 *phase;    // курс в долях оборота (Turn)
    u32 *omega;    // поворот курса за шаг (Turn)
    u16 *free;     // стек свободных мест
    u32 free_count;
    u32 live;
};

struct World {
    u64 step;      // время мира — целый номер шага, секунды выводятся из него
    Rng rng;
    // Притяжение точек к точке приказа (намерение игрока)
    u32 attract_on;
    WPos attract_at;
    u64 attract_until;
    Dots dots;
};

void world_init(World &w, Arena &arena, u64 seed);
// Мёртвых мест нет в выписке и в шаге; спавн и смерть только через эти две функции
bool dots_spawn(World &w, const WPos &center, f32 radius);
void dots_kill(World &w, u32 slot);

// Столбцы мира для хеша и проверки повтора: имя, данные, байты. Порядок — порядок в хеше
struct Column {
    const char *name;
    const void *data;
    usize bytes;
};
constexpr u32 MAX_COLUMNS = 32;
u32 world_columns(const World &w, Column *out);
void world_column_hashes(const World &w, u64 *out, u32 count);
u64 world_hash(const World &w);
