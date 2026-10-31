#include "inventory.h"

const char *g_skill_name[SK_COUNT] = {
    [SK_ATTACK] = "АТАКА", [SK_DEFENSE] = "ЗАЩИТА", [SK_DODGE] = "УВОРОТ",
    [SK_STRENGTH] = "СИЛА", [SK_DEXTERITY] = "ЛОВКОСТЬ", [SK_TOUGHNESS] = "СТОЙКОСТЬ", [SK_ATHLETICS] = "АТЛЕТИКА",
    [SK_WEAPON + WC_BLADE] = "КЛИНКИ", [SK_WEAPON + WC_CLEAVER] = "ТЕСАКИ", [SK_WEAPON + WC_BLUNT] = "ДРОБЯЩЕЕ",
    [SK_WEAPON + WC_POLEARM] = "ДРЕВКОВОЕ", [SK_WEAPON + WC_HEAVY] = "ТЯЖЁЛОЕ",
};

const char *g_slot_name[SLOT_COUNT + 1] = {
    [SLOT_HEAD] = "ГОЛОВА", [SLOT_FACE] = "ЛИЦО", [SLOT_TORSO_UNDER] = "РУБАХА", [SLOT_TORSO_ARMOR] = "БРОНЯ",
    [SLOT_LEGS] = "НОГИ", [SLOT_FEET] = "СТУПНИ", [SLOT_HANDS] = "РУКИ", [SLOT_BACK] = "СПИНА",
    [SLOT_BELT] = "ПОЯС", [SLOT_PANTS] = "ШТАНЫ", [SLOT_WEAPON] = "ОРУЖИЕ",
};

const char *g_state_name[FS_COUNT] = {
    [FS_READY] = "В СТОЙКЕ", [FS_WINDUP] = "ЗАМАХ", [FS_STRIKE] = "УДАР", [FS_RECOVER] = "ВОЗВРАТ",
    [FS_BLOCK] = "БЛОК", [FS_DODGE] = "УВОРОТ", [FS_STAGGER] = "ОШЕЛОМЛЁН", [FS_KNOCKED] = "СБИТ С НОГ",
    [FS_DOWN] = "ЛЕЖИТ", [FS_GETUP] = "ВСТАЁТ", [FS_UNCONSCIOUS] = "БЕЗ СОЗНАНИЯ", [FS_CRAWL] = "ПОЛЗЁТ",
    [FS_RETREAT] = "ОТСТУПАЕТ", [FS_SURRENDER] = "СДАЛСЯ", [FS_DEAD] = "МЁРТВ",
    [FS_PICKUP] = "ПОДНИМАЕТ",
};

u8 item_slot(ItemStack it)
{
    const ItemDef *d = item_def(it);
    return d->weapon != COMP_NONE ? SLOT_WEAPON : d->slot;
}

i32 inv_at(const Inventory *inv, i32 x, i32 y)
{
    for (u32 i = 0; i < inv->n; i++) {
        u8 w, h;
        item_size(inv->item[i], &w, &h);
        if (x >= inv->x[i] && x < inv->x[i] + w && y >= inv->y[i] && y < inv->y[i] + h) return (i32)i;
    }
    return -1;
}

bool inv_fits(const Inventory *inv, ItemStack it, i32 x, i32 y)
{
    u8 w, h;
    item_size(it, &w, &h);
    if (x < 0 || y < 0 || x + w > INV_W || y + h > INV_H || inv->n >= INV_MAX) return false;
    for (i32 cy = y; cy < y + h; cy++)
        for (i32 cx = x; cx < x + w; cx++)
            if (inv_at(inv, cx, cy) >= 0) return false;
    return true;
}

bool inv_put(Inventory *inv, ItemStack it, i32 x, i32 y)
{
    if (!inv_fits(inv, it, x, y)) return false;
    inv->item[inv->n] = it;
    inv->x[inv->n] = (u8)x;
    inv->y[inv->n] = (u8)y;
    inv->n++;
    return true;
}

bool inv_add(Inventory *inv, ItemStack it)
{
    u8 stack = item_def(it)->stack;
    if (stack > 1) {
        for (u32 i = 0; i < inv->n && it.count; i++) {
            ItemStack *s = &inv->item[i];
            if (s->def != it.def || s->mat != it.mat || s->quality != it.quality) continue;
            u8 room = (u8)(stack - MIN(s->count, stack));
            u8 k = MIN(room, it.count);
            s->count += k;
            it.count -= k;
        }
        if (!it.count) return true;
    }
    for (i32 y = 0; y < INV_H; y++)
        for (i32 x = 0; x < INV_W; x++)
            if (inv_put(inv, it, x, y)) return true;
    return false;
}

ItemStack inv_take(Inventory *inv, i32 index)
{
    ItemStack it = inv->item[index];
    inv->n--;
    inv->item[index] = inv->item[inv->n];
    inv->x[index] = inv->x[inv->n];
    inv->y[index] = inv->y[inv->n];
    return it;
}

f32 inv_weight(const Inventory *inv)
{
    f32 w = 0.0f;
    for (u32 i = 0; i < inv->n; i++) w += item_weight(inv->item[i]);
    return w;
}

bool sheet_equip(CharSheet *c, u8 slot, ItemStack it, ItemStack *old)
{
    if (slot > SLOT_WEAPON || (item_live(it) && item_slot(it) != slot)) return false;
    *old = c->equip[slot];
    c->equip[slot] = it;
    // Бой видит только своё: броню по слотам и оружие
    if (slot == SLOT_WEAPON) {
        if (item_weapon(it) != COMP_NONE) {
            c->d.weapon = item_weapon(it);
            c->d.weapon_q = it.quality;
        }
    } else {
        c->d.armor[slot] = item_armor(it) == COMP_NONE ? ARMOR_NONE : item_armor(it);
    }
    return true;
}

f32 sheet_load(const CharSheet *c)
{
    f32 w = inv_weight(&c->inv);
    for (u32 s = 0; s <= SLOT_COUNT; s++) w += item_weight(c->equip[s]);
    return w;
}

f32 sheet_carry_max(const CharSheet *c)
{
    f32 m = 20.0f + 0.6f * c->d.skill[SK_STRENGTH];
    for (u32 s = 0; s < SLOT_COUNT; s++)
        m += item_def(c->equip[s])->carry;
    return m;
}
