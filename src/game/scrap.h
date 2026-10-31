// Лом — вещи для проверки физики: обрезки трубы, арматура, лист, бетонный обломок, швеллер, бочонок.
// Описание формы для физики и сетка одна на вид; сетка в осях вещи на кости 0 (кость = поза тела)
#ifndef ANA_SCRAP_H
#define ANA_SCRAP_H

#include "../core/rng.h"
#include "../render/render.h"
#include "../sim/phys.h"

typedef enum ScrapKind { SCRAP_PIPE, SCRAP_REBAR, SCRAP_SHEET, SCRAP_CHUNK, SCRAP_CHANNEL, SCRAP_KEG, SCRAP_COUNT } ScrapKind;

#define SCRAP_USER  0x1000u  // user тела лома: SCRAP_USER + вид
#define SCRAP_DRAW_MAX 256   // рисуется ближайших

typedef struct ScrapView {
    GpuMesh mesh[SCRAP_COUNT];
    M4     *bones;  // поза на тело подряд: рендер читает RENDER_MAX_BONES матриц от начала набора
} ScrapView;

extern const PhysDesc g_scrap_phys[SCRAP_COUNT];

void scrap_view_init(ScrapView *v, MeshBuilder *mb, Arena *perm);
// n штук лома над точкой at: падают кучей (PHYS_SETTLE, брошены сверху)
void scrap_drop(Phys *p, Rng *rng, V3 at, u32 n);
// Расставить лом над землёй в месте: висит, пока зона не придёт (осесть), и пара вещей «висеть»
void scrap_place(Phys *p, Rng *rng, V3 at, u32 n, f32 (*ground)(f32, f32));
u32  scrap_draw(ScrapView *v, const Phys *p, f32 alpha, V3 eye, DrawItem *out, u32 cap);

#endif
