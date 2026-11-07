// Вид: что видно в кадре. Собирает RenderFrame из двух последних выписок с интерполяцией.
// Только читает выписки и буфер отладочного рисования, мир не трогает. Камера — часть вида, не мира:
// на шаг мира и повтор она не влияет (docs/invariants.md)
#pragma once
#include "../asset/material.h"
#include "../core/dbgdraw.h"
#include "../platform/platform.h"
#include "../render/render.h"
#include "../sim/extract.h"
#include "scatter.h"
#include "sky.h"

// Камера: орбита вокруг точки на земле или свободный полёт (F). yaw, pitch — направление от цели к глазу
struct Camera {
    bool free = false;
    WPos target;            // орбита: точка, вокруг которой
    WPos eye;               // свободный полёт: глаз
    f32 yaw = 0.55f;        // радианы, 0 — глаз на юге от цели (−z), смотрит на север
    f32 pitch = 0.42f;      // радианы над горизонтом
    f32 dist = 24.0f;       // метров от цели (орбита)
    f32 fov_y = 1.0f;       // радианы по вертикали
};

// Сетки и материалы, загруженные для кадра: имя (хеш) → номер в рендере
struct ViewMesh {
    u64 name;
    u32 id;
    u32 lod_count;                         // LOD сетки (1 — без упрощения)
    f32 lod_error[RENDER_MESH_MAX_LODS];   // ошибка упрощения LOD, м
};
constexpr u32 VIEW_MAX_MESHES = 1024;
struct ViewAssets {
    ViewMesh meshes[VIEW_MAX_MESHES];
    u32 mesh_count = 0;
    const MaterialTable *materials = nullptr;
    u32 ground_layers[RENDER_GROUND_LAYERS] = {};  // материалы слоёв земли по GroundKind
    u32 rocks[SCATTER_MESHES] = {};                 // сетки камней рассыпки
    SkyDesc sky;                                    // небо и воздух (data/sky.txt)
};

struct ViewInput {
    const Extract *prev;
    const Extract *cur;
    f32 alpha;              // 0 — prev, 1 — cur
    const DebugDraw *dbg;   // может быть nullptr
    i32 width, height;      // пиксели кадра
    bool pixel;             // правило ячейки (P)
    ScatterCache *scatter;  // рассыпка камней (кеш между кадрами), может быть nullptr
};

// Память под экземпляры, точки и отрезки — из арены кадра
void view_build(const Camera &cam, const ViewAssets &assets, const ViewInput &in, Arena &frame, RenderFrame &out);
// Мышь, колесо и клавиши → камера. dt — секунды кадра (камера движется по времени кадра, не мира).
// Цель орбиты лежит на земле, глаз не уходит под землю
void view_camera_control(Camera &cam, const Input &in, f32 dt, const Terrain &ground);
// Глаз камеры (чанк + смещение)
WPos view_eye(const Camera &cam);
// Луч из пикселя окна до земли (по ground_at, шагами с уточнением). false — луч не попал в землю
bool view_screen_to_ground(const Camera &cam, i32 width, i32 height, f32 px, f32 py, const Terrain &ground, WPos &out);
