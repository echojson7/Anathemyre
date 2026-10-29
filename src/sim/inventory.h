// Вещи, инвентарь-сетка и лист персонажа: навыки, тело, 9 слотов снаряжения, оружие.
// Боевые числа оружия и брони берутся из items.h; здесь только то, что нужно вне боя.
#ifndef ANA_INVENTORY_H
#define ANA_INVENTORY_H

#include "combat.h"

typedef enum ItemKind { IK_NONE, IK_WEAPON, IK_ARMOR, IK_GOODS } ItemKind;

typedef struct Item {
    u8 kind, id, quality, count;
} Item;

typedef struct GoodsDef {
    const char *name;
    u8  w, h;
    u8  stack;    // сколько в одной ячейке
    u8  slot;     // куда надевается, SLOT_NONE — никуда
    f32 weight;   // кг за штуку
    f32 carry;    // сколько кг добавляет к переносимому
    const char *desc;
} GoodsDef;

enum {
    G_WATER, G_RUSK, G_MEAT, G_BANDAGE, G_SCRAP, G_PART, G_FUEL, G_TOKEN,
    G_WRAP, G_BOOTS, G_MITTS, G_PACK, G_BELT,
    G_COUNT
};

#define SLOT_NONE 255
#define SLOT_WEAPON SLOT_COUNT  // оружие в руках: отдельно от 9 слотов

#define INV_W 9
#define INV_H 6
#define INV_MAX 32

typedef struct Inventory {
    u8   n;
    Item item[INV_MAX];
    u8   x[INV_MAX], y[INV_MAX];
} Inventory;

// Лист персонажа отряда: имя, тело и навыки в описании бойца, снаряжение, вещи
typedef struct CharSheet {
    const char *name;
    FighterDesc d;
    Item equip[SLOT_COUNT + 1];  // + SLOT_WEAPON
    Inventory inv;
} CharSheet;

extern const GoodsDef g_goods[G_COUNT];
extern const char    *g_skill_name[SK_COUNT];
extern const char    *g_slot_name[SLOT_COUNT + 1];
extern const char    *g_state_name[FS_COUNT];

static inline Item item_make(u8 kind, u8 id, u8 quality, u8 count) { return (Item){ kind, id, quality, count }; }

const char *item_name(Item it);
void item_size(Item it, u8 *w, u8 *h);
f32  item_weight(Item it);     // с учётом числа
u8   item_slot(Item it);       // SLOT_*, SLOT_WEAPON или SLOT_NONE

i32  inv_at(const Inventory *inv, i32 x, i32 y);  // номер вещи в клетке или -1
bool inv_fits(const Inventory *inv, Item it, i32 x, i32 y);
bool inv_put(Inventory *inv, Item it, i32 x, i32 y);
bool inv_add(Inventory *inv, Item it);  // в стопку или в первое свободное место
Item inv_take(Inventory *inv, i32 index);
f32  inv_weight(const Inventory *inv);

// Надеть вещь в слот; в *old — снятое (IK_NONE, если слот был пуст). false — не тот слот
bool sheet_equip(CharSheet *c, u8 slot, Item it, Item *old);
f32  sheet_load(const CharSheet *c);       // ноша, кг
f32  sheet_carry_max(const CharSheet *c);  // сколько унесёт без перегруза

#endif
