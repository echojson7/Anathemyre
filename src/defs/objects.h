// Объекты и заготовки из data/defs/objects.txt и prefabs.txt (как ThingDef с компонентами у RimWorld и pack-in
// у Bethesda). Объект — форма (генератор content/ по имени), материалы, коробки физики, режим расстановки,
// свет. Заготовка — список объектов и других заготовок с местом, поворотом и заменой материала.
// Экземпляры в мире — world/objects.h; что это и зачем — docs/objects.md
#ifndef ANA_OBJECTS_DEF_H
#define ANA_OBJECTS_DEF_H

#include "deftext.h"
#include "material.h"

#define OBJDEF_CAP   256
#define PREFAB_CAP   64
#define PREFAB_PUTS  4096  // строк put во всех заготовках
#define OBJ_MATS     2     // слотов материала у объекта
#define OBJ_SOLIDS   4     // коробок физики
#define OBJ_ACTS     4     // точек действия
#define PUT_PICK     8     // вариантов в одной строке put (a|b|c)
#define PUT_PREFAB   0x8000u  // в PrefabPut.what: заготовка, а не объект

// Мерцание огня
typedef enum LightFlicker { FLICK_NONE, FLICK_HUM, FLICK_COUNT } LightFlicker;

// Свет объекта (компонент light): точка и ось в осях объекта, поворачиваются вместе с ним
typedef struct ObjLight {
    V3  color;        // цвет с яркостью (PointLight.color): материал лампы × сила или r g b
    V3  at, dir;
    f32 radius, cone, glow;
    f32 night, spread;  // горит с темноты night + spread·(зерно 0..1); night < 0 — всегда
    u8  flicker;        // LightFlicker
    bool shadow;        // на будущее: свет пока без своих теней
} ObjLight;

// Коробка физики в осях объекта при размере по умолчанию: растягивается вместе с размером экземпляра
typedef struct ObjSolid {
    V3 c, half;
} ObjSolid;

// Точка действия (сесть, торговать, работать): только поле, логики пока нет
typedef struct ObjAct {
    V3  at;
    f32 yaw;
    const char *what;
} ObjAct;

typedef struct ObjectDef {
    const char *id;
    const char *shape;           // генератор content/ по имени (content/shapes.c)
    V3   size;                   // размер по умолчанию, м
    const MatDef *mat[OBJ_MATS]; // слоты материала: генератор красит ими, заготовка может заменить
    const char   *mat_ref[OBJ_MATS];
    f32  tint;                   // разброс цвета по зерну
    u8   place;                  // PhysMode: осесть, висеть, намертво. Пока все стоят намертво
    bool cast;                   // отбрасывает тень
    u8   solids;
    bool solid_full;             // одна коробка во весь размер
    ObjSolid solid[OBJ_SOLIDS];
    bool lit;
    ObjLight light;
    const char *light_mat;       // light <материал> <сила>: цвет — после всех файлов
    f32  light_power;
    u8   acts;
    ObjAct act[OBJ_ACTS];
} ObjectDef;

// Строка заготовки: что поставить (один из вариантов по зерну), где, как повернуть, каким размером и материалом
typedef struct PrefabPut {
    u16 what[PUT_PICK];   // номер объекта или PUT_PREFAB | номер заготовки
    u8  picks;
    V3  pos;
    f32 yaw;              // радианы
    V3  size;             // 0 — размер объекта
    const MatDef *mat[OBJ_MATS];  // NULL — материал объекта
    const char   *ref;    // имена до проверки: «a|b|c»
    const char   *mat_ref[OBJ_MATS];
    u32 line;
    const char *file;
} PrefabPut;

typedef struct PrefabDef {
    const char *id;
    u32 first, count;   // строки в g_prefab_puts
} PrefabDef;

extern const ObjectDef *g_objects;
extern u32 g_object_count;
extern const PrefabDef *g_prefabs;
extern u32 g_prefab_count;
extern const PrefabPut *g_prefab_puts;

// Крючки разборщика (defs.c): строка — заголовок object/prefab или поле такой записи. 1 — строка разобрана,
// 0 — не своя (запись объекта кончилась), -1 — ошибка в журнале
i32  objdefs_line(const DefLine *l, Arena *mem);
void objdefs_reset(void);
void objdefs_end(void);
// После всех файлов: имена материалов, объектов и заготовок, вложенность без петель
bool objdefs_check(void);

i32  object_find(const char *id);
i32  prefab_find(const char *id);

#endif
