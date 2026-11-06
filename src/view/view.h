// Вид: что видно в кадре. Собирает RenderFrame из двух последних выписок с интерполяцией.
// Только читает выписки и буфер отладочного рисования, мир не трогает
#pragma once
#include "../core/dbgdraw.h"
#include "../render/render.h"
#include "../sim/extract.h"

struct Camera {
    WPos center;            // точка карты в центре экрана
    f32 half_height = 90;   // метров от центра до верха экрана
};

struct ViewInput {
    const Extract *prev;
    const Extract *cur;
    f32 alpha;              // 0 — prev, 1 — cur
    const DebugDraw *dbg;   // может быть nullptr
    i32 width, height;      // пиксели кадра
};

// Память под точки и отрезки — из арены кадра
void view_build(const Camera &cam, const ViewInput &in, Arena &frame, RenderFrame &out);
// Пиксель окна → точка карты на плоскости XZ (y = 0)
WPos view_screen_to_world(const Camera &cam, i32 width, i32 height, f32 px, f32 py);
// Колесо: приближение к точке под курсором
void view_zoom(Camera &cam, f32 wheel);
