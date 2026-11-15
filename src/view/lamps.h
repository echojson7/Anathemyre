// Огни кадра (6е, docs/lamps.md): лампы карты и тестовые огни F3 → RenderPointLight относительно глаза
#pragma once
#include "view.h"

// Лампы карты в круге LAMPS_VIEW_R м от глаза (у края круга гаснут плавно) и, если test, тестовые огни вокруг
// опоры камеры. Пишет out.lights
void lamps_build(const Lamps *lamps, bool test, const Camera &cam, const Terrain *t, const WPos &eye, Arena &frame,
                 RenderFrame &out);

// Цвет света по температуре (К): линейный sRGB с яркостью Y = 1 (Planckian locus, Kim и др. 2002)
void lamp_kelvin_rgb(f32 kelvin, f32 rgb[3]);
