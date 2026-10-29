#include "inventory.h"

//                                     имя              w  h стоп слот        кг    +ноша
const GoodsDef g_goods[G_COUNT] = {
    [G_WATER]   = { "ФЛЯГА ВОДЫ", 1, 2, 1, SLOT_NONE, 1.0f, 0, "Вода с привкусом ржавчины." },
    [G_RUSK]    = { "СУХАРИ", 1, 1, 10, SLOT_NONE, 0.2f, 0, "Твёрдые, как бетон. Едят." },
    [G_MEAT]    = { "ВЯЛЕНОЕ МЯСО", 1, 1, 10, SLOT_NONE, 0.3f, 0, "Чьё мясо, лучше не спрашивать." },
    [G_BANDAGE] = { "БИНТЫ", 1, 1, 10, SLOT_NONE, 0.1f, 0, "Серые тряпки. Останавливают кровь." },
    [G_SCRAP]   = { "МЕТАЛЛОЛОМ", 2, 2, 5, SLOT_NONE, 4.0f, 0, "Ржавое железо. Берут кузнецы." },
    [G_PART]    = { "ДЕТАЛЬ МАШИНЫ", 2, 1, 1, SLOT_NONE, 2.5f, 0, "Назначение неизвестно. Покупают." },
    [G_FUEL]    = { "ТОПЛИВО", 1, 2, 1, SLOT_NONE, 3.0f, 0, "Канистра. Пахнет старым миром." },
    [G_TOKEN]   = { "ЖЕТОН СО ЗНАКАМИ", 1, 1, 1, SLOT_NONE, 0.05f, 0, "Знаки никто не читает." },
    [G_WRAP]    = { "РЕСПИРАТОР", 1, 1, 1, SLOT_FACE, 0.1f, 0, "Резина и банка фильтра. Пыль держит." },
    [G_BOOTS]   = { "РАБОЧИЕ БОТИНКИ", 1, 2, 1, SLOT_FEET, 0.6f, 0, "Окованный носок. Подошва из старой резины." },
    [G_MITTS]   = { "КРАГИ", 1, 1, 1, SLOT_HANDS, 0.3f, 0, "Резина с раструбом. Рукоять не скользит." },
    [G_PACK]    = { "РАНЕЦ", 2, 2, 1, SLOT_BACK, 1.0f, 15.0f, "Рама из труб, ящик и баллон. Что в баллоне, никто не знает." },
    [G_BELT]    = { "ПОЯС И ПОДСУМКИ", 2, 1, 1, SLOT_BELT, 0.5f, 4.0f, "Подсумки и шланг. Мелочь под рукой." },
};

const char *g_skill_name[SK_COUNT] = {
    [SK_ATTACK] = "АТАКА", [SK_DEFENSE] = "ЗАЩИТА", [SK_DODGE] = "УВОРОТ",
    [SK_STRENGTH] = "СИЛА", [SK_DEXTERITY] = "ЛОВКОСТЬ", [SK_TOUGHNESS] = "СТОЙКОСТЬ", [SK_ATHLETICS] = "АТЛЕТИКА",
    [SK_WEAPON + WC_BLADE] = "КЛИНКИ", [SK_WEAPON + WC_CLEAVER] = "ТЕСАКИ", [SK_WEAPON + WC_BLUNT] = "ДРОБЯЩЕЕ",
    [SK_WEAPON + WC_POLEARM] = "ДРЕВКОВОЕ", [SK_WEAPON + WC_HEAVY] = "ТЯЖЁЛОЕ",
};

const char *g_slot_name[SLOT_COUNT + 1] = {
    [SLOT_HEAD] = "ГОЛОВА", [SLOT_FACE] = "ЛИЦО", [SLOT_TORSO_UNDER] = "РУБАХА", [SLOT_TORSO_ARMOR] = "БРОНЯ",
    [SLOT_LEGS] = "НОГИ", [SLOT_FEET] = "СТУПНИ", [SLOT_HANDS] = "РУКИ", [SLOT_BACK] = "СПИНА",
    [SLOT_BELT] = "ПОЯС", [SLOT_WEAPON] = "ОРУЖИЕ",
};

const char *g_state_name[FS_COUNT] = {
    [FS_READY] = "В СТОЙКЕ", [FS_WINDUP] = "ЗАМАХ", [FS_STRIKE] = "УДАР", [FS_RECOVER] = "ВОЗВРАТ",
    [FS_BLOCK] = "БЛОК", [FS_DODGE] = "УВОРОТ", [FS_STAGGER] = "ОШЕЛОМЛЁН", [FS_KNOCKED] = "СБИТ С НОГ",
    [FS_DOWN] = "ЛЕЖИТ", [FS_GETUP] = "ВСТАЁТ", [FS_UNCONSCIOUS] = "БЕЗ СОЗНАНИЯ", [FS_CRAWL] = "ПОЛЗЁТ",
    [FS_RETREAT] = "ОТСТУПАЕТ", [FS_SURRENDER] = "СДАЛСЯ", [FS_DEAD] = "МЁРТВ",
};

// Размер в клетках: оружие стоймя, длина по высоте
static const u8 g_weapon_size[W_COUNT][2] = {
    [W_RESSORA] = { 1, 3 }, [W_SEKACH] = { 1, 3 }, [W_ARMATURA] = { 1, 3 }, [W_BAGOR] = { 1, 5 }, [W_BALKA] = { 2, 4 },
};
static const u8 g_armor_size[A_COUNT][2] = {
    [A_RAGS] = { 2, 2 }, [A_JACKET] = { 2, 3 }, [A_QUILT] = { 2, 2 }, [A_PLATE] = { 2, 3 }, [A_BUCKET] = { 2, 2 },
    [A_GREAVES] = { 2, 2 },
};

const char *item_name(Item it)
{
    switch (it.kind) {
    case IK_WEAPON: return g_weapons[it.id].name;
    case IK_ARMOR: return g_armors[it.id].name;
    case IK_GOODS: return g_goods[it.id].name;
    default: return "";
    }
}

void item_size(Item it, u8 *w, u8 *h)
{
    const u8 *s = it.kind == IK_WEAPON ? g_weapon_size[it.id] : it.kind == IK_ARMOR ? g_armor_size[it.id] : NULL;
    *w = s ? s[0] : g_goods[it.id].w;
    *h = s ? s[1] : g_goods[it.id].h;
}

f32 item_weight(Item it)
{
    switch (it.kind) {
    case IK_WEAPON: return g_weapons[it.id].weight;
    case IK_ARMOR: return g_armors[it.id].weight;
    case IK_GOODS: return g_goods[it.id].weight * (f32)it.count;
    default: return 0.0f;
    }
}

u8 item_slot(Item it)
{
    switch (it.kind) {
    case IK_WEAPON: return SLOT_WEAPON;
    case IK_ARMOR: return g_armors[it.id].slot;
    case IK_GOODS: return g_goods[it.id].slot;
    default: return SLOT_NONE;
    }
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

bool inv_fits(const Inventory *inv, Item it, i32 x, i32 y)
{
    u8 w, h;
    item_size(it, &w, &h);
    if (x < 0 || y < 0 || x + w > INV_W || y + h > INV_H || inv->n >= INV_MAX) return false;
    for (i32 cy = y; cy < y + h; cy++)
        for (i32 cx = x; cx < x + w; cx++)
            if (inv_at(inv, cx, cy) >= 0) return false;
    return true;
}

bool inv_put(Inventory *inv, Item it, i32 x, i32 y)
{
    if (!inv_fits(inv, it, x, y)) return false;
    inv->item[inv->n] = it;
    inv->x[inv->n] = (u8)x;
    inv->y[inv->n] = (u8)y;
    inv->n++;
    return true;
}

bool inv_add(Inventory *inv, Item it)
{
    if (it.kind == IK_GOODS) {
        for (u32 i = 0; i < inv->n && it.count; i++) {
            Item *s = &inv->item[i];
            if (s->kind != IK_GOODS || s->id != it.id) continue;
            u8 room = (u8)(g_goods[it.id].stack - MIN(s->count, g_goods[it.id].stack));
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

Item inv_take(Inventory *inv, i32 index)
{
    Item it = inv->item[index];
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

bool sheet_equip(CharSheet *c, u8 slot, Item it, Item *old)
{
    if (slot > SLOT_WEAPON || (it.kind != IK_NONE && item_slot(it) != slot)) return false;
    *old = c->equip[slot];
    c->equip[slot] = it;
    // Бой видит только своё: броню по слотам и оружие
    if (slot == SLOT_WEAPON) {
        if (it.kind == IK_WEAPON) {
            c->d.weapon = it.id;
            c->d.weapon_q = it.quality;
        }
    } else {
        c->d.armor[slot] = it.kind == IK_ARMOR ? it.id : ARMOR_NONE;
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
        if (c->equip[s].kind == IK_GOODS) m += g_goods[c->equip[s].id].carry;
    return m;
}
