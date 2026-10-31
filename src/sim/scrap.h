// Лом — вещи для проверки физики: обрезки трубы, арматура, лист, бетонный обломок, швеллер, бочонок.
// Описание формы для физики одно на вид; сетки — view/scrap_view.c
#ifndef ANA_SCRAP_H
#define ANA_SCRAP_H

#include "../core/rng.h"
#include "../defs/material.h"
#include "phys.h"

typedef enum ScrapKind { SCRAP_PIPE, SCRAP_REBAR, SCRAP_SHEET, SCRAP_CHUNK, SCRAP_CHANNEL, SCRAP_KEG, SCRAP_COUNT } ScrapKind;

#define SCRAP_USER  0x1000u  // user тела лома: SCRAP_USER + вид

extern const PhysDesc g_scrap_phys[SCRAP_COUNT];
// Из чего вид лома (основной материал): по нему звучит удар
const MatDef *scrap_mat(u32 kind);

// n штук лома над точкой at: падают кучей (PHYS_SETTLE, брошены сверху)
void scrap_drop(Phys *p, Rng *rng, V3 at, u32 n);
// Расставить лом над землёй в месте: висит, пока зона не придёт (осесть), и пара вещей «висеть»
void scrap_place(Phys *p, Rng *rng, V3 at, u32 n, f32 (*ground)(f32, f32));

#endif
