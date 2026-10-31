// Материалы: цвет (один или набор оттенков) и поверхность шейдера по имени, из data/defs/materials.txt.
// Один бетон, одна ржавчина на все постройки; местные оттенки — свои записи. Что это и зачем — docs/materials.md
#ifndef ANA_MATERIAL_H
#define ANA_MATERIAL_H

#include "../core/math3d.h"

#define MAT_TONES  8    // оттенков в наборе: ткани, листы
#define MATDEF_CAP 128

// Чем звучит удар по вещи из материала и её падение (ключ impact). Звук по классу выбирает вид
typedef enum Impact { IMP_METAL, IMP_WOOD, IMP_STONE, IMP_SOFT, IMP_COUNT } Impact;

typedef struct MatDef {
    const char *id;
    u8 surface;  // Material (MAT_*) из render/mesh.h
    u8 impact;   // Impact
    u8 tones;
    V3 tone[MAT_TONES];  // tone[0] — основной цвет
} MatDef;

// Материалы, которые берёт код (как ThingDefOf у RimWorld): поле = имя записи, проверка при загрузке.
// Остальные записи — для данных: оружие, моды
#define MAT_OF(X)                                                                                                   \
    X(concrete) X(concrete_dark) X(rust) X(sand) X(metal) X(iron) X(lamp) X(glyph_red) X(glyph_green) X(hollow)     \
    X(cable) X(cloth) X(sheet) X(concrete_pale) X(concrete_pale_dark) X(rust_pale) X(window) X(sign_paint)          \
    X(paint_pale) X(iron_dark) X(lamp_sodium) X(rust_dark) X(arena_floor) X(glyph_dim) X(lamp_white) X(rust_deep)   \
    X(concrete_chip)

typedef struct MatOf {
#define MAT_FIELD(n) const MatDef *n;
    MAT_OF(MAT_FIELD)
#undef MAT_FIELD
} MatOf;

extern MatOf g_mat;
extern const MatDef *g_mats;
extern u32 g_mat_count;

static inline V3 mat_c(const MatDef *m) { return m->tone[0]; }
// Материал → класс звука удара (Impact)
static inline u8 mat_impact(const MatDef *m) { return m->impact; }

#endif
