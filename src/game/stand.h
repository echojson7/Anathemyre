// Стенд фигур (меню НАСТРОЙКИ, --stand N): голые тела без вещей и причёсок на фоне мерной доски,
// чтобы осматривать пропорции. Ряд на тело: обычная поза покоя и T-поза, спереди, слева, сзади и в 3/4.
// Ряд роста — 0.85 и 1.15 без правки ширины: фигура не должна плющиться и тянуться.
// Последний ряд — причёски мужчины и женщины спереди, сбоку, сзади.
// Линии доски через 1/8 роста 1.80 м (22.5 см — голова по канону), толще — 0.9 и 1.8 м
#ifndef ANA_STAND_H
#define ANA_STAND_H

#include "camera.h"
#include "figures.h"

#define STAND_ROWS (BODY_KIND_COUNT + 2)
#define STAND_VIEWS 4
#define STAND_HEIGHTS 2
#define STAND_HAIR_VIEWS 3
#define STAND_N (BODY_KIND_COUNT * 2 * STAND_VIEWS + BODY_KIND_COUNT * STAND_HEIGHTS * 2 + 2 * HAIR_MAX * STAND_HAIR_VIEWS)

typedef struct Stand {
    bool    on, built;
    u32     count;
    u8      body[STAND_N];
    u8      hair[STAND_N];  // вещь причёски в ряду тела (GEAR_COUNT + вид) или 0 — без неё
    f32     floor[STAND_ROWS];  // высота плиты ряда
    M4     *bones;   // STAND_N наборов
    GpuMesh board;   // мерные доски за рядами
} Stand;

void stand_init(Stand *s, Arena *perm);
// Показать ряд row (камера к нему) или убрать стенд; кости и доски строятся при первом показе
void stand_show(Stand *s, bool on, u32 row, MeshBuilder *mb, Arena *perm, GroundFn ground, RtsCamera *cam);
u32  stand_draw(const Stand *s, const CharMeshes *m, DrawItem *items);

#endif
