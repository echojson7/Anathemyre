// Процедурный персонаж: модель из деталей на стандартном скелете тела (sim/body.h), походка с IK ног
#ifndef ANA_CHARACTER_H
#define ANA_CHARACTER_H

#include "../core/rng.h"
#include "../render/mesh.h"
#include "../sim/body.h"

// Длина полного цикла шага (два шага), м
#define CHAR_STRIDE 1.15f

_Static_assert(BONE_COUNT <= 24, "скелет не влезает в RENDER_MAX_BONES");

typedef struct CharPose {
    V3  pos;         // точка на земле
    f32 heading;     // поворот вокруг Y, 0 = смотрит в +Z
    f32 phase;       // фаза цикла шага, 0..1
    f32 walk;        // 0 стоит .. 1 идёт
    f32 time;
    f32 seed;
    f32 height;      // множитель роста, 0 = 1
    f32 width;       // множитель ширины, 0 = 1
} CharPose;

// Вариант 0..N задаёт одежду и снаряжение
void character_build(MeshBuilder *mb, u32 variant);
// Пишет BONE_COUNT матриц скиннинга
void character_pose(const CharPose *p, M4 *out_bones);

#endif
