// Статика мира на экране: объекты (зал, ринг, Остов, Скудель, мегаструктуры), долина, лом и вещи на земле.
// Сетки строит content/ при старте; мир (World) о них не знает
#ifndef ANA_SCENE_H
#define ANA_SCENE_H

#include "../render/render.h"
#include "../sim/arena_ring.h"
#include "../world/objects.h"
#include "../world/town.h"
#include "loot_view.h"
#include "scrap_view.h"
#include "valley_view.h"

#define SCENE_BATCHES 64  // статических пакетов объектов: локация × чанк с тенью, локация без тени
#define SCENE_STATICS SCENE_BATCHES

typedef struct Scene {
    GpuMesh    *batch;                   // объекты (world/objects.h): зал, ринг, Остов, Скудель, горизонт
    bool       *batch_no_shadow;
    u32         batches;
    ValleyView *valley;                  // долина у реки: вода, руины, растения
    ScrapView   scrap;
    LootView    loot;
} Scene;

// Сетки статики и видов из экземпляров objs; места жителей Скудели — в town
bool scene_init(Scene *s, MeshBuilder *mb, Arena *perm, u64 seed, const Objects *objs, Town *town);
// До SCENE_STATICS элементов кадра: статика с тенями и мегаструктуры без них
u32  scene_statics(const Scene *s, DrawItem *out);

#endif
