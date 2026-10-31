// Вещи: одна запись item с общими полями и компонентами (оружие, броня, носится, ёмкость, свет, тело),
// стопка вещи в мире (ItemStack). Числа баланса — в data/defs (docs/content.md, docs/items.md), читаются при старте.
#ifndef ANA_ITEMS_H
#define ANA_ITEMS_H

#include "body.h"

typedef enum WeaponClass { WC_BLADE, WC_CLEAVER, WC_BLUNT, WC_POLEARM, WC_HEAVY, WC_COUNT } WeaponClass;
typedef enum AttackKind { ATK_OVERHEAD, ATK_SWING, ATK_THRUST, ATK_COUNT } AttackKind;

// Компонент «оружие». Имя, вес — копии из вещи (их читает бой)
typedef struct WeaponDef {
    const char *id;   // имя вещи: по нему сохранение сверяет номера
    const char *name;
    u16 item;         // чья это часть: номер вещи
    u8  wclass;
    u8  attacks;      // маска (1 << AttackKind)
    u8  max_targets;  // предел целей размаха
    f32 cut, blunt, pierce;
    f32 length;       // досягаемость = рука 0.7 + длина
    f32 weight;       // кг
    f32 speed;        // 1 = быстрое, меньше = медленнее
    f32 arc_deg;      // дуга размаха
    f32 knock;        // сила сбивания
    f32 pen;          // пробой брони уколом, 0..1
    const char *sound, *sound_strike;  // голос железа по имени (звук находит вид): закрылся им и ударил им
    u16 part, parts;  // форма: части в g_weapon_parts
    const char *model;  // готовая форма data/meshes/weapons/<model>.mesh (tools/weaponconv), части — запасные
    bool common;      // в общем запасе: отряды, арена, толпа берут случайное оружие отсюда
    bool unique;      // единственная вещь: вид не зависит от качества
} WeaponDef;

// Часть формы оружия в своей системе: рукоять в начале координат, острие по +Y, лезвие по +Z.
// Клинок и пластина — с точками в g_weapon_pts (pt — начало, npt — сколько чисел)
typedef enum WeaponPartKind { WP_ROD, WP_BOX, WP_BLADE, WP_PLATE, WP_HOLE, WP_WRAP, WP_KNOB } WeaponPartKind;
typedef struct WeaponPart {
    u8  kind, mat;
    u32 pt;
    u16 npt;
    f32 color[3];
    // стержень: от, до, радиус, сдвиг; брусок: от, до, ширина, толщина, сдвиг, наклон;
    // пластина: толщина; прорезь: центр по оси и к лезвию, полуоси; обмотка: от, до, радиус, витков;
    // навершие: от, до, радиус, сдвиг
    f32 v[6];
} WeaponPart;

#define WEAPON_CAP 64  // сеток оружия на GPU

// Компонент «броня». Имя, слот, вес — копии из вещи (их читает бой)
typedef struct ArmorDef {
    const char *id;
    const char *name;
    u16 item;
    u8  slot;
    f32 cover[PART_COUNT];
    f32 res_cut, res_blunt, res_pierce;
    f32 weight;
    f32 dodge_pen;
} ArmorDef;

// Компонент «свет»: вещь светит, когда горит (факел)
typedef struct LightDef {
    V3  color;
    f32 power, radius;
} LightDef;

// Компонент «ёмкость»: сетка клеток внутри вещи (ящик, стойка — объекты шага 2б)
typedef struct ContainerDef {
    u8 w, h;
} ContainerDef;

// Компонент «тело»: своя форма на земле вместо свёртка по клеткам (полуразмеры бруска, м)
typedef struct PhysCompDef {
    V3 half;
} PhysCompDef;

#define COMP_NONE 255
#define ITEM_NONE 0   // запись 0 — «ничего»: пустая стопка, пустой слот
#define ITEM_CAP 1024

// Вещь: общие поля и номера компонентов (COMP_NONE — нет), как ThingComps у RimWorld
typedef struct ItemDef {
    const char *id;
    const char *name;
    const char *desc;
    f32 weight;       // кг за штуку
    f32 carry;        // носится: сколько кг добавляет к переносимому
    u32 hides;        // какие сетки под ней не рисуются: биты номеров g_gear_file, GEAR_HIDES_HAIR — причёска
    u8  w, h;         // клетки инвентаря
    u8  stack;        // сколько в одной клетке
    u8  slot;         // носится: слот тела (SLOT_*), SLOT_NONE — нет
    u8  mat;          // материал по умолчанию: номер в g_mats
    u8  look;         // сетка на теле: номер в g_gear_file, GEAR_NONE — не видна
    u8  weapon, armor, light, container, phys;
} ItemDef;

// Вещь в мире: 8 байт. Износа нет (решение пользователя); зерно — вариант вида
typedef struct ItemStack {
    u16 def;      // номер описания, ITEM_NONE — пусто
    u16 seed;
    u8  mat;      // материал (номер в g_mats)
    u8  quality;  // Q_*
    u8  count;
    u8  pad;
} ItemStack;
_Static_assert(sizeof(ItemStack) == 8, "ItemStack — 8 байт");

#define ARMOR_NONE 255
#define GEAR_NONE 255
#define GEAR_CAP 24   // разных сеток одежды в описаниях
#define GEAR_HIDES_HAIR (1u << 31)
#define WEAPON_PARTS 48  // частей формы на оружие
#define WEAPON_PTS 1536  // чисел точек клинков и пластин на оружие

// Качество: множитель урона оружия и сопротивлений брони
enum { Q_JUNK, Q_WORN, Q_SOUND, Q_MASTER, Q_ANCIENT, Q_COUNT };

typedef struct AttackDef {
    f32 dmg_mul;
    f32 windup_mul;
    f32 reach_add;
    f32 arc_deg;      // 0 = дуга оружия (только для размаха)
} AttackDef;

#define ARM_REACH 0.7f

// Заполняет defs_load при старте
extern const ItemDef    *g_items;
extern u32               g_item_count;
extern const LightDef   *g_lights;
extern const ContainerDef *g_containers;
extern const PhysCompDef *g_phys_comps;
// Сетки одежды по имени (look в описаниях): файл data/meshes/gear/<тело>_<имя>.mesh находит вид
extern const char       *g_gear_file[GEAR_CAP];
extern u32               g_gear_count;
extern const WeaponDef  *g_weapons;
extern u32               g_weapon_count;
extern const WeaponPart *g_weapon_parts;
extern const f32        *g_weapon_pts;
// Общий запас (common yes): из него случайное оружие отрядов, арены и толпы, по порядку записей
extern u8                g_weapon_pool[WEAPON_CAP];
extern u32               g_weapon_pool_count;
extern const ArmorDef   *g_armors;
extern u32               g_armor_count;
extern AttackDef         g_attacks[ATK_COUNT];
extern f32               g_quality_mul[Q_COUNT];
extern const char       *g_quality_name[Q_COUNT];
extern const char       *g_attack_name[ATK_COUNT];
extern const u8          g_armor_layer_order[5];  // слоты снаружи внутрь

static inline const ItemDef *item_def(ItemStack it) { return &g_items[it.def]; }
static inline bool item_live(ItemStack it) { return it.def != ITEM_NONE; }
static inline u8   item_weapon(ItemStack it) { return g_items[it.def].weapon; }  // COMP_NONE — не оружие
static inline u8   item_armor(ItemStack it) { return g_items[it.def].armor; }
// Стопка вещи def из материала по умолчанию
static inline ItemStack item_make(u16 def, u8 quality, u8 count)
{
    return (ItemStack){ .def = def, .mat = g_items[def].mat, .quality = quality, .count = count };
}
static inline ItemStack item_of_weapon(u8 w, u8 quality) { return item_make(g_weapons[w].item, quality, 1); }
static inline ItemStack item_of_armor(u8 a, u8 quality) { return item_make(g_armors[a].item, quality, 1); }
// Номер сетки одежды по имени, GEAR_NONE — нет такой
u8 gear_find(const char *name);

// ---------------------------------------------------------------- место вещи и переход

// Где лежит вещь. Вещь всегда в одном месте; из места в место — только item_move
typedef enum PlaceKind {  // IP_* — место вещи (item place)
    IP_NONE,    // вне мира: новая вещь или брошенная из окна мешка (it)
    IP_GROUND,  // на земле: номер в Ground, тело в физике
    IP_HAND,    // в руке: слот оружия листа; без листа — рука бойца арены (it)
    IP_BODY,    // надета: слот листа
    IP_BAG,     // в мешке: сетка INV_W×INV_H листа
    IP_BOX,     // в стойке или ящике: объект с компонентом container (объекты — шаг 2б)
    IP_COUNT
} PlaceKind;

struct CharSheet;
struct Ground;
struct Phys;

typedef struct ItemPlace {
    u8  kind;
    u8  slot;     // ТЕЛО: слот (SLOT_*)
    u8  x, y;     // МЕШОК, куда: клетка; x = 255 — в стопку или первое свободное место
    i32 at;       // ЗЕМЛЯ, откуда: номер на земле; МЕШОК, откуда: номер в мешке; ЯЩИК: номер объекта
    u32 who;      // чья рука, тело, мешок: номер человека (для журнала); ITEM_NOBODY — ничьи
    struct CharSheet *sheet;
    ItemStack it;  // ВНЕ МИРА и РУКА без листа: что там
    // ЗЕМЛЯ, куда: поза (у оружия — рукоять в начале осей, острие по +Y), скорость, вращение, чья вещь (Ground.owner)
    M4  xf;
    V3  vel, spin;
    u16 owner;
} ItemPlace;

#define ITEM_NOBODY 0xFFFFFFFFu

static inline ItemPlace place_none(ItemStack it) { return (ItemPlace){ .kind = IP_NONE, .who = ITEM_NOBODY, .it = it }; }
static inline ItemPlace place_ground(i32 k) { return (ItemPlace){ .kind = IP_GROUND, .at = k, .who = ITEM_NOBODY }; }
static inline ItemPlace place_drop(M4 xf, V3 vel, V3 spin, u16 owner)
{
    return (ItemPlace){ .kind = IP_GROUND, .at = -1, .who = ITEM_NOBODY, .xf = xf, .vel = vel, .spin = spin, .owner = owner };
}
static inline ItemPlace place_hand(struct CharSheet *c, u32 who) { return (ItemPlace){ .kind = IP_HAND, .who = who, .sheet = c }; }
static inline ItemPlace place_body(struct CharSheet *c, u32 who, u8 slot)
{
    return (ItemPlace){ .kind = IP_BODY, .slot = slot, .who = who, .sheet = c };
}
static inline ItemPlace place_bag(struct CharSheet *c, u32 who, i32 at) { return (ItemPlace){ .kind = IP_BAG, .x = 255, .at = at, .who = who, .sheet = c }; }

// Переложить вещь из from в to целиком: снять с места, положить на новое. На земле — создаёт тело в физике,
// с земли — гасит его. Перемены, заметные миру (земля, чужие руки), пишет в журнал мира (Ground.chron).
// Только из шага мира. Положить некуда — ничего не меняется, -1; иначе номер на новом месте (на земле, в мешке)
i32 item_move(struct Ground *g, struct Phys *p, const ItemPlace *from, const ItemPlace *to);
// Что лежит в месте (пустая стопка — ничего)
ItemStack item_at(const struct Ground *g, const ItemPlace *pl);

#endif
