// Статика мира на экране: зал, мегаструктуры, Скудель, Остов, долина, лом и вещи на земле; свет зала и ринга.
// Сетки строит content/ при старте; мир (World) о них не знает
#ifndef ANA_SCENE_H
#define ANA_SCENE_H

#include "../render/render.h"
#include "../sim/arena_ring.h"
#include "../world/town.h"
#include "loot_view.h"
#include "scrap_view.h"
#include "valley_view.h"

#define SCENE_STATICS 4

typedef struct Scene {
    GpuMesh     hall, far, town, ostov;  // зал с рингом и обломки, мегаструктуры, посёлок, руина Остов
    ValleyView *valley;                  // долина у реки: вода, руины, растения
    ScrapView   scrap;
    LootView    loot;
} Scene;

// Сетки статики и видов; места жителей Скудели — в town (их собирает та же постройка, что и сетку)
bool scene_init(Scene *s, MeshBuilder *mb, Arena *perm, u64 seed, Town *town);
// SCENE_STATICS элементов кадра: статика с тенями и мегаструктуры без них
u32  scene_statics(const Scene *s, DrawItem *out);
// Лампы над стойками с оружием в зале: горят всегда
u32  scene_hall_lights(PointLight *out, u32 max);
// Свет арены: зажигается с темнотой night (Sky.night), в out — до RING_LIGHTS источников
u32  scene_ring_lights(f32 night, f32 time, PointLight *out);

#endif
