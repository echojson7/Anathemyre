// Сборка процедурной геометрии в CPU-буферы (из арены)
#ifndef ANA_MESH_H
#define ANA_MESH_H

#include "../core/arena.h"
#include "../core/math3d.h"

// Материалы: шейдер выбирает процедурную поверхность по номеру
typedef enum Material {
    MAT_CONCRETE = 0,
    MAT_RUST     = 1,
    MAT_SAND     = 2,
    MAT_CLOTH    = 3,
    MAT_LEATHER  = 4,
    MAT_GLYPH    = 5,   // светящиеся знаки неизвестного языка
    MAT_EMISSIVE = 6,
    MAT_SKIN     = 7,
    MAT_METAL    = 8,
} Material;

#define BONE_NONE 255

typedef struct Vertex {
    f32 pos[3];
    f32 nrm[3];
    u8  color[4];
    u8  mat, bone, pad0, pad1;
} Vertex;

typedef struct MeshBuilder {
    Vertex *v;
    u32    *idx;
    u32     vcount, vcap;
    u32     icount, icap;
    bool    overflow;
    // Текущие атрибуты для новых вершин
    V3      color;
    u8      mat;
    u8      bone;
} MeshBuilder;

bool mb_init(MeshBuilder *mb, Arena *arena, u32 vcap, u32 icap);
void mb_set(MeshBuilder *mb, V3 color, Material mat, u8 bone);

// Единичный куб [-0.5, 0.5]^3, преобразованный матрицей; плоские нормали
void mb_box(MeshBuilder *mb, M4 xf);
// Тело вращения вдоль a->b; profile: x = смещение вдоль оси (м), y = радиус.
// ex/ez растягивают сечение по боковой и передней оси
void mb_lathe(MeshBuilder *mb, V3 a, V3 b, const V2 *profile, u32 n, u32 segs, f32 ex, f32 ez);
// Капсула с разными радиусами на концах
void mb_capsule(MeshBuilder *mb, V3 a, V3 b, f32 r0, f32 r1, u32 segs, f32 ex, f32 ez);
// Цилиндр с плоскими торцами
void mb_cylinder(MeshBuilder *mb, V3 a, V3 b, f32 r, u32 segs);
// Прямоугольник: центр, полуоси right/up; лицевая сторона по cross(right, up)
void mb_quad(MeshBuilder *mb, V3 c, V3 right, V3 up);
// Рельеф вокруг начала координат: height(x, z) в мировых координатах
void mb_terrain(MeshBuilder *mb, f32 half_size, u32 cells, f32 (*height)(f32 x, f32 z, void *u), void *user);

#endif
