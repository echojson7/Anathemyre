// Растения, камни и мелкие предметы долины: каждый экземпляр сетки строится из своего зерна
#ifndef ANA_FLORA_H
#define ANA_FLORA_H

#include "../core/mesh.h"

// Деревья и кусты. Уровни детализации: 0 полный, 1 упрощённый, 2 грубый, 3 силуэт (та же форма)
typedef enum TreeKind { TREE_BROAD, TREE_PINE, TREE_BIRCH, TREE_DEAD, TREE_BUSH, TREE_KINDS } TreeKind;
#define FLORA_LODS 4

// Трава и мелкие растения: пучок вокруг (0, 0, 0)
typedef enum GrassKind { GRASS_MEADOW, GRASS_TALL, GRASS_REEDS, GRASS_FERN, GRASS_FLOWERS, GRASS_KINDS } GrassKind;


void flora_tree(MeshBuilder *mb, TreeKind kind, u32 seed, u32 lod);
#define GRASS_LODS 2
// lod 1 — каждая третья травинка, но шире (дальше от камеры)
void flora_grass(MeshBuilder *mb, GrassKind kind, u32 seed, u32 lod);
// Валун около единичного радиуса, основание на y = 0
void flora_rock(MeshBuilder *mb, u32 seed, u32 lod);
// Неровная сфера из икосаэдра (subdiv 0..2) со своим цветом вершин; для крон, кустов и камней
void flora_blob(MeshBuilder *mb, V3 c, V3 r, u32 subdiv, f32 jitter, u32 seed);

#endif
