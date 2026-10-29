// Долина у реки на экране: куски рельефа с LOD, вода, руины, деревья, трава, камни и мелочи
#ifndef ANA_VALLEY_VIEW_H
#define ANA_VALLEY_VIEW_H

#include "../render/render.h"
#include "flora.h"

#define VV_CHUNK 64          // сторона куска рельефа, м
#define VV_CHUNKS 10         // кусков на сторону
#define VV_TERRAIN_LODS 4
#define VV_TREE_VARIANTS 6   // разных сеток каждой породы
#define VV_GRASS_VARIANTS 3
#define VV_ROCKS 8
#define VV_PROP_VARIANTS 3
#define VV_ITEMS (VV_CHUNKS * VV_CHUNKS + 4)

typedef struct ValleyView {
    bool    built;
    GpuMesh terrain[VV_CHUNKS * VV_CHUNKS][VV_TERRAIN_LODS];
    GpuMesh ring, water, ruins, mega;
    GpuMesh tree[TREE_KINDS][VV_TREE_VARIANTS][FLORA_LODS];
    GpuMesh grass[GRASS_KINDS][VV_GRASS_VARIANTS][GRASS_LODS];
    GpuMesh rock[VV_ROCKS][2];
    GpuMesh pebble[VV_ROCKS];   // мелкие камни: те же сетки, но редеют с дальностью
    GpuMesh prop[PROP_KINDS][VV_PROP_VARIANTS];
    RInst     *inst;
    u32        inst_count;
    InstGroup *groups;
    u32        group_count;
    InstCluster clusters[20 * 20];   // участки 32 м
    u32        cluster_count;
    V3  spawn;          // где появляется отряд
    f32 spawn_heading;
} ValleyView;

// Строит всё один раз; рельеф (valley_init) — тоже здесь. Данные экземпляров живут в perm
bool valley_view_build(ValleyView *v, Arena *perm, u64 seed);
// Статика долины в out (до VV_ITEMS); группы экземпляров отдаются рендеру отдельно (render_set_groups)
u32  valley_view_draw(const ValleyView *v, DrawItem *out);

#endif
