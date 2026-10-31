// Экземпляры объектов в мире: постройки, их части и мелочи локаций. Строятся при старте из заготовки world
// (data/defs/prefabs.txt) и зерна мира; лежат структурой массивов, по чанкам OBJ_CHUNK м. Отличия от заготовок
// не сохраняются: экземпляры в сохранение не идут (docs/objects.md). Вид строит из них сетки, физика — стены
#ifndef ANA_OBJECTS_H
#define ANA_OBJECTS_H

#include "../core/arena.h"
#include "../defs/objects.h"
#include "../sim/phys.h"

#define OBJ_CHUNK  256.0f
#define OBJ_SIDE   110u       // чанков по стороне карты 28 км
#define OBJ_MAX    32768u
#define OBJ_GROUPS 32u        // строк заготовки world: локации
#define OBJ_GROUP_NONE 255u

enum { OBJF_LIT = 1, OBJF_NO_SHADOW = 2 };

// 30 байт на экземпляр
typedef struct Objects {
    u32  count;
    u16 *def;
    u8  *flags;
    u8  *group;           // какая строка заготовки world: у каждой локации свои сетки
    u32 *seed;            // оттенок, износ, выбор варианта
    V3  *pos;
    u16 *yaw;             // 0..65535 — полный оборот
    i16 (*size)[3];       // см; 0 0 0 — размер объекта
    u8  (*mat)[OBJ_MATS]; // номер в g_mats; 255 — материал объекта
    u32 *chunk;           // OBJ_SIDE² + 1: начало чанка в массивах
    u32  lit_count;
    u32 *lit;             // экземпляры со светом по порядку
    u32  groups;
    const char *group_name[OBJ_GROUPS];
} Objects;

// Развернуть заготовку world. false — ошибка в журнале
bool objects_build(Objects *o, Arena *perm, u64 seed);
// Коробки физики: объекты, которые стоят намертво
void objects_solids(const Objects *o, Phys *p);

u32  obj_chunk_of(f32 x, f32 z);
static inline f32 obj_yaw(const Objects *o, u32 i) { return (f32)o->yaw[i] * (2.0f * PI_F / 65536.0f); }
V3   obj_size(const Objects *o, u32 i);
const MatDef *obj_mat(const Objects *o, u32 i, u32 slot);
// Оси экземпляра: сдвиг в pos и поворот вокруг вертикали
M4   obj_frame(const Objects *o, u32 i);

#endif
