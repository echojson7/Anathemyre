// Долина у реки на экране: вода, руины, деревья, трава, камни и мелочи. Рельеф — часть карты мира (map.c).
// Строится в координатах долины и переносится на карту (VALLEY_ORIGIN)
#ifndef ANA_VALLEY_VIEW_H
#define ANA_VALLEY_VIEW_H

#include "../render/render.h"
#include "../content/flora.h"
#include "../content/props.h"

#define VV_TREE_VARIANTS 6   // разных сеток каждой породы
#define VV_GRASS_VARIANTS 3
#define VV_ROCKS 8
#define VV_PROP_VARIANTS 3
#define VV_ITEMS 3

typedef struct ValleyView {
    bool    built;
    GpuMesh water, ruins, mega;
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
    V3  spawn;          // где появляется отряд, на карте мира
    f32 spawn_heading;
} ValleyView;

// Строит всё один раз; подробная карта высот (valley_init) — тоже здесь. Данные экземпляров живут в perm
bool valley_view_build(ValleyView *v, Arena *perm, u64 seed);
// Статика долины в out (до VV_ITEMS): вода и руины — если глаз ближе ~1.2 км; группы экземпляров отдаются
// рендеру отдельно (render_set_groups)
u32  valley_view_draw(const ValleyView *v, V3 eye, DrawItem *out);

#endif
