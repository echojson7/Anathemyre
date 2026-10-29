// Внешность человека: тип тела, веса морфов сетки, оттенки кожи и волос.
// Считается из зерна и сложения; у людей мира хранится в таблице (People.lk), редактор правит её там. Рендер берёт матрицу из слота RENDER_LOOK_SLOT
#ifndef ANA_LOOK_H
#define ANA_LOOK_H

#include "../core/math3d.h"
#include "../sim/body.h"
#include "../sim/inventory.h"

// Уровни детализации тел и одежды: 0 — для крупных планов, каждый следующий грубее (tools/humanconv LOD_TRIS)
#define CHAR_LODS 5

// Сетки тел (data/meshes, tools/humanconv)
typedef enum BodyKind { BODY_MALE, BODY_FEMALE, BODY_THIN, BODY_KIND_COUNT } BodyKind;
extern const char *const g_body_file[BODY_KIND_COUNT];
// Суставы каждого тела в позе привязки (печатает tools/humanconv): по ним вращаются кости, иначе
// у женщины бёдра и плечи крутились бы вокруг мужских суставов
extern const V3 g_body_joints[BODY_KIND_COUNT][BONE_COUNT];

// Морфы, порядок как MORPHS в tools/humanconv. Лицо — только местные морфы: нос, разрез глаз, челюсть, губы
enum { LOOK_FAT, LOOK_LEAN, LOOK_STRONG, LOOK_OLD, LOOK_NOSE, LOOK_EYES, LOOK_JAW, LOOK_LIPS, LOOK_MORPHS };

typedef struct CharLook {
    f32 morph[LOOK_MORPHS];
    V3  skin, hair;    // считаются из тонов ниже (look_paint)
    f32 skin_t;        // тон кожи 0 бледная .. 1 тёмная
    f32 hair_t;        // цвет волос по ряду оттенков 0..1
    f32 gray;          // седина 0..1
    u8  cut;           // причёска: 0 — нет (щетина), иначе вид 1..HAIR_MAX у этого тела
} CharLook;

// Одежда и броня: сетки поверх тела (data/meshes/gear, tools/humanconv GEAR), на том же наборе костей
typedef enum GearId {
    GEAR_RAGS, GEAR_QUILT, GEAR_JACKET, GEAR_PLATE, GEAR_BUCKET, GEAR_GREAVES,
    GEAR_WRAP, GEAR_BOOTS, GEAR_MITTS, GEAR_PACK, GEAR_BELT, GEAR_PANTS,
    GEAR_COUNT
} GearId;
#define GEAR_MAX_WORN 11  // по вещи на слот и причёска
extern const char *const g_gear_file[GEAR_COUNT];

// Причёски (data/meshes/hair): рисуются как вещи, в ряду сеток тела после вещей. NULL — вида нет
#define HAIR_MAX 4
#define GEAR_ROW (GEAR_COUNT + HAIR_MAX)
extern const char *const g_hair_file[BODY_KIND_COUNT][HAIR_MAX];
// Причёска в список вещей gear[0..n) (номер в ряду): возвращает 0 или 1. Под кожухом причёски нет
u32 look_hair(const CharLook *l, u8 *gear, u32 n);

// Видимые вещи: из брони по слотам (A_*, ARMOR_NONE — пусто); возвращает число
u32 gear_from_armor(const u8 *armor, u8 *out);
// Из надетого листа персонажа: броня и вещи (G_*)
u32 gear_from_sheet(const CharSheet *cs, u8 *out);
// Случайная одежда прохожего
u32 gear_random(u32 seed, u8 *out);

// mass — кг, height — м, strength — навык силы 0..100
void look_make(CharLook *l, u32 body, u32 seed, f32 mass, f32 height, f32 strength);
// Цвета кожи и волос из тонов и седины
void look_paint(CharLook *l);
// Сколько причёсок у тела
u32  look_cuts(u32 body);
// Матрица для слота RENDER_LOOK_SLOT набора костей
M4   look_matrix(const CharLook *l, u32 body);

#endif
