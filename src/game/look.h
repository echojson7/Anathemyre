// Внешность человека: тип тела, веса морфов сетки, оттенки кожи и волос.
// Считается из зерна и сложения, отдельно не хранится; рендер берёт матрицу из слота RENDER_LOOK_SLOT
#ifndef ANA_LOOK_H
#define ANA_LOOK_H

#include "../core/math3d.h"
#include "../sim/inventory.h"

// Уровни детализации тел и одежды: 0 — полный, каждый следующий грубее
#define CHAR_LODS 3

// Сетки тел (data/meshes, tools/humanconv)
typedef enum BodyKind { BODY_MALE, BODY_FEMALE, BODY_THIN, BODY_KIND_COUNT } BodyKind;
extern const char *const g_body_file[BODY_KIND_COUNT];

// Морфы, порядок как MORPHS в tools/humanconv
enum { LOOK_FAT, LOOK_LEAN, LOOK_STRONG, LOOK_WEAK, LOOK_OLD, LOOK_FACE_A, LOOK_FACE_B, LOOK_JAW, LOOK_MORPHS };

typedef struct CharLook {
    f32 morph[LOOK_MORPHS];
    V3  skin, hair;
} CharLook;

// Одежда и броня: сетки поверх тела (data/meshes/gear, tools/humanconv GEAR), на том же наборе костей
typedef enum GearId {
    GEAR_RAGS, GEAR_QUILT, GEAR_JACKET, GEAR_PLATE, GEAR_BUCKET, GEAR_GREAVES,
    GEAR_WRAP, GEAR_BOOTS, GEAR_MITTS, GEAR_PACK, GEAR_BELT,
    GEAR_COUNT
} GearId;
#define GEAR_MAX_WORN 9  // по вещи на слот
extern const char *const g_gear_file[GEAR_COUNT];

// Видимые вещи: из брони по слотам (A_*, ARMOR_NONE — пусто); возвращает число
u32 gear_from_armor(const u8 *armor, u8 *out);
// Из надетого листа персонажа: броня и вещи (G_*)
u32 gear_from_sheet(const CharSheet *cs, u8 *out);
// Случайная одежда прохожего
u32 gear_random(u32 seed, u8 *out);

// mass — кг, height — м, strength — навык силы 0..100
void look_make(CharLook *l, u32 body, u32 seed, f32 mass, f32 height, f32 strength);
// Матрица для слота RENDER_LOOK_SLOT набора костей
M4   look_matrix(const CharLook *l, u32 body);

#endif
