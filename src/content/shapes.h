// Формы объектов: генератор по имени (поле shape в data/defs/objects.txt). Генератор строит экземпляр в его осях
// (+z вперёд, +x вправо) его размером и материалами; разброс оттенка и износа — из зерна экземпляра
#ifndef ANA_SHAPES_H
#define ANA_SHAPES_H

#include "../core/mesh.h"
#include "../core/rng.h"
#include "../world/objects.h"

typedef struct ShapeCtx {
    MeshBuilder  *mb;
    Rng          *r;       // поток из зерна экземпляра
    M4            L;       // оси экземпляра
    V3            pos;
    f32           yaw;
    V3            size;
    const MatDef *mat[OBJ_MATS];
    f32           tint;
    u32           arg;     // вариант у общих генераторов (вид прилавка, что висит на проводе)
} ShapeCtx;

typedef void (*ShapeFn)(const ShapeCtx *c);

typedef struct Shape {
    const char *name;
    ShapeFn     fn;
    u32         arg;
} Shape;

// Генераторы локаций: таблицы в своих файлах
extern const Shape g_town_shapes[];
extern const u32   g_town_shape_count;

// Имена форм всех объектов найдены. false — ошибка в журнале
bool shapes_bind(void);
// Сетка экземпляра i
void shape_build(MeshBuilder *mb, const Objects *o, u32 i);

#endif
