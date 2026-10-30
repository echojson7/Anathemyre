// Общая палитра материалов мира: один бетон, одна ржавчина, один песок на все постройки и рельеф.
// Местные оттенки (выбеленный бетон Остова, свет ламп посёлка) остаются у своих построек
#ifndef ANA_PALETTE_H
#define ANA_PALETTE_H

#include "math3d.h"

#define PAL_CONCRETE      v3(0.42f, 0.40f, 0.37f)
#define PAL_CONCRETE_DARK v3(0.30f, 0.29f, 0.27f)
#define PAL_RUST          v3(0.42f, 0.24f, 0.13f)
#define PAL_SAND          v3(0.40f, 0.34f, 0.26f)
// Редкие яркие знаки — только для важного
#define PAL_GLYPH_RED     v3(0.95f, 0.18f, 0.10f)
#define PAL_GLYPH_GREEN   v3(0.25f, 1.0f, 0.40f)

#endif
