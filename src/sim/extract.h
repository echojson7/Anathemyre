// Выписка для кадра: в конце шага симуляция копирует то, что нужно кадру. Кадр читает только выписки,
// не таблицы мира (кольцо из трёх: две читает вид, третью пишет шаг). docs/architecture.md
#pragma once
#include "world.h"

struct Extract {
    const Terrain *terrain;  // земля не меняется после создания мира: кадр читает её напрямую
    const Decals *decals;    // декали карты — тоже
    const Colliders *coll;   // коллайдеры построек — тоже (опора стоп у вида, support_at)
    const GaitTable *gait;   // длины цикла шага — тоже (подгонка шага у вида)
    u64 step;
    Clock clock;
    u16 *gen;      // 0 — места нет или точка мертва
    i32 *cx, *cz;
    f32 *x, *y, *z;
    u32 attract_on;
    WPos attract_at;
    // Статичные объекты: места 0..props_count, gen 0 — места нет
    u32 props_count;
    u16 *prop_gen;
    u64 *prop_mesh, *prop_material;
    i32 *prop_cx, *prop_cz;
    f32 *prop_x, *prop_y, *prop_z;
    u32 *prop_yaw;
    f32 *prop_scale;
    u32 *prop_seed;
    // Люди (этап 9): корень тела для вида — место, курс тела, скорость, фаза шага; gen 0 — места нет
    u16 *person_gen;
    i32 *person_cx, *person_cz;
    f32 *person_x, *person_y, *person_z;
    u32 *person_heading;
    f32 *person_speed;
    u32 *person_phase;
    u32 *person_seed;
    u8 *person_state;        // MoveState: стоит, ждёт путь, идёт
};

void extract_init(Extract &e, Arena &arena);
void sim_extract(const World &w, Extract &e);

struct ExtractRing {
    Extract slot[3];
    u32 head = 0;    // последняя записанная
    u32 filled = 0;
};

void ring_init(ExtractRing &r, Arena &arena);
// Место под следующую выписку и пометка, что она записана
inline Extract &ring_next(ExtractRing &r) { return r.slot[(r.head + 1) % 3]; }
inline void ring_commit(ExtractRing &r) { r.head = (r.head + 1) % 3; if (r.filled < 3) r.filled++; }
inline const Extract &ring_cur(const ExtractRing &r) { return r.slot[r.head]; }
inline const Extract &ring_prev(const ExtractRing &r) { return r.slot[r.filled > 1 ? (r.head + 2) % 3 : r.head]; }
