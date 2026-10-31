// Вещи на земле: предмет (ItemStack) и его тело в физике. Брошенное из мешка, выроненное в бою, расставленное
// в местах. Лежит, пока не подберут; вдали застывает вместе с физикой. Одна модель на вещь: на земле её
// позу ведёт тело физики, в руке — кость кисти (docs/physics.md, раздел «Вещи на земле»)
#ifndef ANA_GROUND_H
#define ANA_GROUND_H

#include "inventory.h"
#include "phys.h"
#include "../io/chronicle.h"

#define GROUND_MAX   1024
#define GROUND_USER  0x2000u  // user тела вещи на земле: GROUND_USER + номер вещи
#define GROUND_ASKS  16

// Чья вещь: выроненное в бою помнит хозяина, пока он может его поднять. 0 — ничья
#define GROUND_OWN_FIELD 0x0000u  // + слот поля + 1
#define GROUND_OWN_ARENA 0x1000u  // + слот боя арены + 1
#define GROUND_OWN_SRC   0xF000u

typedef struct Ground {
    u32   count;  // верхняя граница занятых номеров
    ItemStack *item;   // ITEM_NONE — место свободно
    i32  *body;   // тело в физике
    u16  *owner;
    Chronicle *chron;  // журнал мира для item_move; NULL — некуда (арена)
} Ground;

// Просьбы интерфейса бросить вещь из мешка: исполняет шаг мира (мир меняется только в шаге)
typedef struct GroundAsks {
    u32  n;
    ItemStack it[GROUND_ASKS];
    V3   at[GROUND_ASKS];   // откуда летит: из рук бросающего
    f32  yaw[GROUND_ASKS];  // куда смотрит бросающий
    u8   who[GROUND_ASKS];  // номер в отряде: некуда положить — вещь возвращается ему в мешок
} GroundAsks;

void ground_init(Ground *g, Arena *perm);
// Форма вещи для физики: оружие — стержень от навершия до острия (и тяжёлая голова), прочее — узел
PhysDesc ground_desc(ItemStack it);
// Положить вещь телом: xf — поза вещи (у оружия — рукоять в начале осей, острие по +Y). Только из шага мира.
// -1 — нет места в таблице или в физике
i32  ground_drop(Ground *g, Phys *p, ItemStack it, M4 xf, V3 vel, V3 angvel, u16 owner);
// Убрать вещь с земли и отдать её
ItemStack ground_take(Ground *g, Phys *p, u32 k);
static inline bool ground_live(const Ground *g, i32 k) { return k >= 0 && (u32)k < g->count && g->item[k].def != ITEM_NONE; }
static inline V3   ground_pos(const Ground *g, const Phys *p, u32 k) { return p->x[g->body[k]]; }
// Ближайшая вещь в круге r по земле; -1 — нет
i32  ground_nearest(const Ground *g, const Phys *p, V3 at, f32 r);
u32  ground_live_count(const Ground *g);
void ground_ser(Ser *s, Ground *g);

#endif
