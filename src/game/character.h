// Процедурный персонаж: модель из деталей на стандартном скелете тела (sim/body.h).
// Позы и анимации — в anim.h
#ifndef ANA_CHARACTER_H
#define ANA_CHARACTER_H

#include "../core/rng.h"
#include "../render/mesh.h"
#include "../sim/body.h"
#include "../sim/items.h"
#include "anim.h"

// Уровни детализации фигуры: 0 — полный, каждый следующий грубее
#define CHAR_LODS 3

// Вариант 0..N задаёт одежду; armed — встроенное оружие тестовой комнаты; lod — уровень детализации
void character_build(MeshBuilder *mb, u32 variant, bool armed, u32 lod);

#endif
