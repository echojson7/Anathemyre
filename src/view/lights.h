// Огни кадра из компонентов light: у объектов (world/objects.h, data/defs/objects.txt) — лампы зала, свет ринга;
// у вещей (sim/items.h) — факел в руке. Огни отладочных мест — debug_lights
#ifndef ANA_LIGHTS_H
#define ANA_LIGHTS_H

#include "../render/render.h"
#include "../sim/items.h"
#include "../world/objects.h"

#define LIGHTS_SEE 300.0f  // дальше от камеры огни не собираются, м

// night — темнота (Sky.night), time — время шейдеров. Возвращает сколько записано в out (до max)
u32 lights_collect(const Objects *o, V3 eye, f32 night, f32 time, PointLight *out, u32 max);

// Свет вещи в точке pos: lit 0..1 — насколько разгорелась, пламя пляшет от зерна и времени
PointLight lights_item(const LightDef *d, V3 pos, f32 lit, f32 seed, f32 time);

#endif
