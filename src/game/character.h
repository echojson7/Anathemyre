// Процедурный персонаж: модель из деталей, скелет из 13 костей, походка с IK ног
#ifndef ANA_CHARACTER_H
#define ANA_CHARACTER_H

#include "../core/rng.h"
#include "../render/mesh.h"

enum {
    BONE_PELVIS, BONE_CHEST, BONE_HEAD,
    BONE_UPPER_ARM_L, BONE_FOREARM_L, BONE_UPPER_ARM_R, BONE_FOREARM_R,
    BONE_THIGH_L, BONE_SHIN_L, BONE_FOOT_L,
    BONE_THIGH_R, BONE_SHIN_R, BONE_FOOT_R,
    BONE_COUNT
};

// Длина полного цикла шага (два шага), м
#define CHAR_STRIDE 1.15f

typedef struct CharPose {
    V3  pos;         // точка на земле
    f32 heading;     // поворот вокруг Y, 0 = смотрит в +Z
    f32 phase;       // фаза цикла шага, 0..1
    f32 walk;        // 0 стоит .. 1 идёт
    f32 time;
    f32 seed;
} CharPose;

// Вариант 0..N задаёт одежду и снаряжение
void character_build(MeshBuilder *mb, u32 variant);
// Пишет BONE_COUNT матриц скиннинга
void character_pose(const CharPose *p, M4 *out_bones);

#endif
