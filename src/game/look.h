// Внешность человека: тип тела, веса морфов сетки, оттенки кожи и волос.
// Считается из зерна и сложения, отдельно не хранится; рендер берёт матрицу из слота RENDER_LOOK_SLOT
#ifndef ANA_LOOK_H
#define ANA_LOOK_H

#include "../core/math3d.h"

// Сетки тел (data/meshes, tools/humanconv)
typedef enum BodyKind { BODY_MALE, BODY_FEMALE, BODY_THIN, BODY_KIND_COUNT } BodyKind;
extern const char *const g_body_file[BODY_KIND_COUNT];

// Морфы, порядок как MORPHS в tools/humanconv
enum { LOOK_FAT, LOOK_LEAN, LOOK_STRONG, LOOK_WEAK, LOOK_OLD, LOOK_FACE_A, LOOK_FACE_B, LOOK_JAW, LOOK_MORPHS };

typedef struct CharLook {
    f32 morph[LOOK_MORPHS];
    V3  skin, hair;
} CharLook;

// mass — кг, height — м, strength — навык силы 0..100
void look_make(CharLook *l, u32 body, u32 seed, f32 mass, f32 height, f32 strength);
// Матрица для слота RENDER_LOOK_SLOT набора костей
M4   look_matrix(const CharLook *l, u32 body);

#endif
