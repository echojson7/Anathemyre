// Инвентарь-сетка и лист персонажа: навыки, тело, 9 слотов снаряжения, оружие.
// Боевые числа оружия и брони берутся из items.h; здесь только то, что нужно вне боя.
#ifndef ANA_INVENTORY_H
#define ANA_INVENTORY_H

#include "combat.h"

#define SLOT_NONE 255
#define SLOT_WEAPON SLOT_COUNT  // оружие в руках: отдельно от слотов одежды

#define INV_W 12
#define INV_H 10
#define INV_MAX 32

typedef struct Inventory {
    u8   n;
    ItemStack item[INV_MAX];
    u8   x[INV_MAX], y[INV_MAX];
} Inventory;

// Лист человека: имя, что видит бой из надетого, снаряжение, вещи. Тело, раны и навыки — столбцы реестра (persons.h)
typedef struct CharSheet {
    const char *name;
    const char *role;   // кто он для чужих глаз (ТОРГОВЕЦ, СТОРОЖ, ЗРИТЕЛЬ); NULL — ПРОХОЖИЙ
    u8  weapon, weapon_q;     // чем бьёт: последнее взятое в руки оружие (без оружия бьёт им же, безоружным)
    u8  armor[SLOT_COUNT];    // броня по слотам для боя; ARMOR_NONE — пусто
    ItemStack equip[SLOT_COUNT + 1];  // + SLOT_WEAPON
    Inventory inv;
} CharSheet;

extern const char    *g_skill_name[SK_COUNT];
extern const char    *g_slot_name[SLOT_COUNT + 1];
extern const char    *g_state_name[FS_COUNT];

static inline const char *item_name(ItemStack it) { return item_live(it) ? g_items[it.def].name : ""; }
static inline void item_size(ItemStack it, u8 *w, u8 *h) { *w = g_items[it.def].w, *h = g_items[it.def].h; }
static inline f32 item_weight(ItemStack it) { return g_items[it.def].weight * (f32)it.count; }  // с учётом числа
u8   item_slot(ItemStack it);  // SLOT_*, SLOT_WEAPON или SLOT_NONE

i32  inv_at(const Inventory *inv, i32 x, i32 y);  // номер вещи в клетке или -1
bool inv_fits(const Inventory *inv, ItemStack it, i32 x, i32 y);
bool inv_put(Inventory *inv, ItemStack it, i32 x, i32 y);
bool inv_add(Inventory *inv, ItemStack it);  // в стопку или в первое свободное место
ItemStack inv_take(Inventory *inv, i32 index);
f32  inv_weight(const Inventory *inv);

// Надеть вещь в слот; в *old — снятое (пустая стопка, если слот был пуст). false — не тот слот
bool sheet_equip(CharSheet *c, u8 slot, ItemStack it, ItemStack *old);
f32  sheet_load(const CharSheet *c);       // ноша, кг
f32  sheet_carry_max(const CharSheet *c, f32 strength);  // сколько унесёт без перегруза при такой силе

#endif
