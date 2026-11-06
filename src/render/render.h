// Рендер: Vulkan 1.3. Только рисует то, что дал вид; мир не читает и не меняет (шов 2).
// Этап 0: очистка кадра, треугольник, точки и отладочные отрезки. Швы кадра этапа 1 — docs/stack.md
#pragma once
#include "../core/mem.h"

// Раскладка как std430 в шейдерах (data/shaders/*.vert)
struct RenderPoint {
    f32 x, y;    // метры относительно камеры: x — восток, y — север (мировая z)
    f32 size;    // пиксели
    u32 rgba;    // sRGB
};
struct RenderLineVertex {
    f32 x, y;
    u32 rgba;
    u32 pad;
};

struct RenderFrame {
    f32 clear[3];        // sRGB
    f32 meters_to_ndc[2];  // масштаб вида
    Span<const RenderPoint> points;
    Span<const RenderLineVertex> lines;  // пары вершин
    bool triangle;
};

struct RenderConfig {
    bool vsync = true;
    bool validation = false;  // слои проверки Vulkan с проверкой синхронизации
    const char *shader_dir = "";
};

constexpr u32 RENDER_MAX_POINTS = 4096;
constexpr u32 RENDER_MAX_LINE_VERTICES = 32768;

bool render_init(const RenderConfig &cfg);
void render_shutdown();
// false — неустранимая ошибка устройства
bool render_frame(const RenderFrame &f);
// Окно поменяло размер: swapchain пересоздаётся перед следующим кадром
void render_resized();
// Число ошибок слоя проверки за всё время
u32 render_validation_errors();
const char *render_device_name();
