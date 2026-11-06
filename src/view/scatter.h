// Рассыпка камней вокруг камеры (docs/terrain.md): экземпляры из зерна клетки 2 м, плотность по виду земли.
// Только кадр: в шаг мира и сохранение не идёт, коллайдера нет (всё, обо что спотыкаются, — объекты мира).
// Пока считается на CPU и только когда камера перешла в другую клетку; на этапе 5 переедет в compute на GPU
#pragma once
#include "../render/render.h"
#include "../sim/terrain.h"
#include "../core/wpos.h"

constexpr u32 SCATTER_MAX = 8192;
constexpr u32 SCATTER_MESHES = 3;

struct ScatterCache {
    i32 cell_x = 0, cell_z = 0;  // клетка камеры (2 м карты), при которой построено
    bool valid = false;
    RenderInstance *items = nullptr;
    u32 count = 0;
};

void scatter_init(ScatterCache &c, Arena &arena);
// Перестроить, если камера сменила клетку. meshes — номера сеток камней у рендера (0 — нет)
void scatter_update(ScatterCache &c, const Terrain &t, const WPos &eye, const u32 meshes[SCATTER_MESHES]);
