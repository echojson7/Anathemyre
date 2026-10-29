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
    MAT_TERRAIN  = 9,   // земля: цвет вершин, на крутых склонах порода
    MAT_ROCK     = 10,
    MAT_WATER    = 11,  // цвет вершин — толща воды
    MAT_BARK     = 12,  // параметр < 0.5 — светлая кора с тёмными метками
    MAT_LEAF     = 13,  // параметр — глубина в кроне: 0 внутри, 1 снаружи
    MAT_GRASS    = 14,  // параметр — высота по травинке
} Material;

#define BONE_NONE 255
#define MESH_MAX_MORPHS 10  // веса морфов экземпляра — первые 8 чисел матрицы внешности и m[11], m[15] (render.h, RENDER_LOOK_SLOT)

typedef struct Vertex {
    f32 pos[3];
    f32 nrm[3];
    u8  color[4];
    u8  mat, bone;
    u8  tint;  // класс оттенка экземпляра: 0 нет, 1 кожа, 2 волосы
    u8  pad1;
} Vertex;

// Мягкий скиннинг: до 4 костей на вершину, веса в сумме 255, самая тяжёлая первой
typedef struct VertexSkin {
    u8 bone[4];
    u8 weight[4];
} VertexSkin;

typedef struct MeshBuilder {
    Vertex *v;
    VertexSkin *skin;  // NULL — жёсткий скиннинг по Vertex.bone
    u32 *morph;        // сдвиги морфов: на вершину и морф 3 u32 (snorm16 пары: dx dy, dz nx, ny nz)
    u32  morph_count;
    u32    *idx;
    u32     vcount, vcap;
    u32     icount, icap;
    bool    overflow;
    // Текущие атрибуты для новых вершин
    V3      color;
    u8      mat;
    u8      bone;
    f32     param;   // параметр материала в альфе цвета (1 по умолчанию; у бетона 1 - мох)
} MeshBuilder;

bool mb_init(MeshBuilder *mb, Arena *arena, u32 vcap, u32 icap);
// Готовая сетка из файла .mesh (tools/humanconv): "AMSH", версия, число вершин и индексов, Vertex[], u32[];
// с версии 2 следом VertexSkin[], с версии 3 — u32 число морфов и их сдвиги. Буферы берутся из арены по размеру файла
bool mb_load(MeshBuilder *mb, Arena *arena, const char *path);
void mb_set(MeshBuilder *mb, V3 color, Material mat, u8 bone);
// Вершина и треугольник вручную (для своих форм: трава, рельеф)
u32  mb_vertex(MeshBuilder *mb, V3 p, V3 n);
void mb_tri(MeshBuilder *mb, u32 a, u32 b, u32 c);
// Место под nv вершин и ni индексов; false — буфер полон
bool mb_reserve(MeshBuilder *mb, u32 nv, u32 ni);

// Единичный куб [-0.5, 0.5]^3, преобразованный матрицей; плоские нормали
void mb_box(MeshBuilder *mb, M4 xf);
// Тело вращения вдоль a->b; profile: x = смещение вдоль оси (м), y = радиус.
// ex/ez растягивают сечение по боковой и передней оси
void mb_lathe(MeshBuilder *mb, V3 a, V3 b, const V2 *profile, u32 n, u32 segs, f32 ex, f32 ez);
// Пересчитать гладкие нормали для вершин начиная с vfirst по треугольникам начиная с ifirst
void mb_smooth_normals(MeshBuilder *mb, u32 vfirst, u32 ifirst);
// Капсула с разными радиусами на концах
void mb_capsule(MeshBuilder *mb, V3 a, V3 b, f32 r0, f32 r1, u32 segs, f32 ex, f32 ez);
// Цилиндр с плоскими торцами
void mb_cylinder(MeshBuilder *mb, V3 a, V3 b, f32 r, u32 segs);
// Прямоугольник: центр, полуоси right/up; лицевая сторона по cross(right, up)
void mb_quad(MeshBuilder *mb, V3 c, V3 right, V3 up);

#endif
