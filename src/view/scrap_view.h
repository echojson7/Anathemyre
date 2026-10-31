// Вид лома (sim/scrap.h): сетка на вид в осях вещи на кости 0 (кость = поза тела)
#ifndef ANA_SCRAP_VIEW_H
#define ANA_SCRAP_VIEW_H

#include "../render/render.h"
#include "../sim/scrap.h"

#define SCRAP_DRAW_MAX 256  // рисуется ближайших

typedef struct ScrapView {
    GpuMesh mesh[SCRAP_COUNT];
    M4     *bones;  // поза на тело подряд: рендер читает RENDER_MAX_BONES матриц от начала набора
} ScrapView;

void scrap_view_init(ScrapView *v, MeshBuilder *mb, Arena *perm);
u32  scrap_draw(ScrapView *v, const Phys *p, f32 alpha, V3 eye, DrawItem *out, u32 cap);

#endif
