// Огни кадра из экземпляров с компонентом light (world/objects.h, data/defs/objects.txt): лампы зала, свет ринга.
// Одна функция на все огни мира; огни отладочных мест — debug_lights, факел в руке — figures.c
#ifndef ANA_LIGHTS_H
#define ANA_LIGHTS_H

#include "../render/render.h"
#include "../world/objects.h"

#define LIGHTS_SEE 300.0f  // дальше от камеры огни не собираются, м

// night — темнота (Sky.night), time — время шейдеров. Возвращает сколько записано в out (до max)
u32 lights_collect(const Objects *o, V3 eye, f32 night, f32 time, PointLight *out, u32 max);

#endif
