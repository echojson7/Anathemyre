// Сетки оружия и факела по описаниям: рукоять в начале координат, острие по +Y, лезвие по +Z
#ifndef ANA_WEAPON_H
#define ANA_WEAPON_H

#include "../core/arena.h"
#include "../core/mesh.h"
#include "../sim/anim.h"

// Оружие на кости bone (слот CHAR_BONE_WEAPON)
void weapon_build(MeshBuilder *mb, u8 weapon, u8 look, u8 bone);
// Готовые формы из Blender (model в описании): data/meshes/weapons/<model>.mesh, один раз до мира.
// Нет файла — оружие собирается из частей описания
void weapon_models_load(Arena *perm, const char *data_dir);
// То же неподвижной деталью сцены (стойки): сразу в мировых координатах, без кости
void weapon_build_xf(MeshBuilder *mb, u8 weapon, u8 look, M4 xf);
// Факел: древко с обмоткой (flame = false) или пламя отдельной сеткой; слот CHAR_BONE_TORCH
void torch_build(MeshBuilder *mb, bool flame);

#endif
