// Рендер: Vulkan 1.3. Только рисует то, что дал вид; мир не читает и не меняет (шов 2).
// Швы кадра (docs/stack.md, docs/frame.md): общий буфер сеток, экземпляры в буфере GPU, 64-битный ключ порядка,
// обратная глубина, всё относительно камеры, surface()/light_surface(). Поверх — простой forward этапов 1–4
#pragma once
#include "../core/mem.h"

// Раскладки ниже — как в data/shaders/common.glsl (std430)

// Экземпляр сетки, 48 байт: чанк + смещение, сжатый поворот, масштаб, сетка, подмена материала, зерно
struct RenderInstance {
    i32 cx, cz;        // чанк 256 м
    f32 x, y, z;       // смещение в чанке, высота от нуля карты
    u32 rot_xy, rot_zw;  // кватернион, по два snorm16
    f32 scale;
    u32 mesh;          // номер сетки в таблице
    u32 material;      // подмена материала, RENDER_NO_MATERIAL — материалы вершин
    u32 seed;          // зерно разнообразия узора
    u32 pad;
};
static_assert(sizeof(RenderInstance) == 48, "RenderInstance — 48 байт");
constexpr u32 RENDER_NO_MATERIAL = 0xFFFFFFFFu;

struct RenderMaterial {
    u32 kind;
    u32 pad[3];
    f32 p[12];  // смысл — asset/material.h
};
static_assert(sizeof(RenderMaterial) == 64, "RenderMaterial — 64 байта");

// Отладочные отрезки и точки — метры относительно камеры
struct RenderLineVertex {
    f32 x, y, z;
    u32 rgba;  // sRGB
};
struct RenderPoint {
    f32 x, y, z;
    f32 size;  // пиксели
    u32 rgba;  // sRGB
    u32 pad[3];
};

// Ключ порядка отрисовки (не адрес сетки): слой 4 бита | конвейер 8 | сетка 20 | глубина 32.
// Глубина — биты положительного f32 расстояния: для непрозрачного ближние раньше
enum RenderLayer : u32 { LAYER_OPAQUE = 0, LAYER_TRANSPARENT = 8 };
enum RenderPipe : u32 { PIPE_MESH = 0 };
inline u64 render_key(u32 layer, u32 pipe, u32 mesh, f32 depth) {
    u32 d;
    memcpy(&d, &depth, 4);
    if (depth < 0.0f) d = 0;
    return ((u64)(layer & 0xF) << 60) | ((u64)(pipe & 0xFF) << 52) | ((u64)(mesh & 0xFFFFF) << 32) | d;
}
inline u32 render_key_mesh(u64 key) { return (u32)(key >> 32) & 0xFFFFF; }
inline u32 render_key_pipe(u64 key) { return (u32)(key >> 52) & 0xFF; }

struct RenderDraw {
    u64 key;
    u32 instance;  // номер в RenderFrame::instances
    u32 pad;
};

// Камера: положение чанк + смещение, базис взгляда, проекция с обратной глубиной до бесконечности
struct RenderView {
    i32 cx, cz;
    f32 x, y, z;
    f32 right[3], up[3], forward[3];
    f32 tan_half_fov;  // по вертикали
    f32 near_m;
};

// Правило ячейки (docs/render.md «Пиксельность»): узор и свет раз на мировую ячейку, ячейка не мельче
// cell_min метров и растёт с глубиной степенями двойки, пока не станет пикселем кадра
struct RenderStyle {
    bool pixel = true;     // выключается клавишей P
    f32 cell_min = 1.0f / 80.0f;
    f32 cell_pixels = 1.0f;  // ячейка — не меньше стольких пикселей кадра
};

// Временный свет до света пользователя (light.glsl): солнце и небо одним цветом
struct RenderLight {
    f32 sun_dir[3];    // к солнцу
    f32 sun_color[3];  // линейный
    f32 sky_color[3];  // линейный
};

// Слои земли: песок, сухая земля, щебень, камень, трава, вода (GroundKind, src/sim/ground.h), затем песок и камень
// пустыни (тот же вид земли, другой материал — по маске пустыни)
constexpr u32 RENDER_GROUND_LAYERS = 8;

struct RenderFrame {
    f32 clear[3];      // sRGB
    RenderView view;
    RenderStyle style;
    RenderLight light;
    f32 fog[4];        // временная дымка: линейный цвет и 1/расстояние (м⁻¹)
    bool ground;       // рисовать землю (её данные — render_terrain_set)
    u32 ground_layers[RENDER_GROUND_LAYERS];  // материалы слоёв
    Span<const RenderInstance> instances;
    Span<RenderDraw> draws;  // рендер сортирует по ключу на месте
    Span<const RenderPoint> points;
    Span<const RenderLineVertex> lines;  // пары вершин
};

struct RenderConfig {
    bool vsync = true;
    bool validation = false;  // слои проверки Vulkan с проверкой синхронизации
    const char *shader_dir = "";
};

constexpr u32 RENDER_MAX_POINTS = 4096;
constexpr u32 RENDER_MAX_LINE_VERTICES = 32768;
constexpr u32 RENDER_MAX_INSTANCES = 65536;
constexpr u32 RENDER_MAX_MESHES = 4096;
constexpr u32 RENDER_MAX_MATERIALS = 1024;

bool render_init(const RenderConfig &cfg);
void render_shutdown();
// false — неустранимая ошибка устройства
bool render_frame(const RenderFrame &f);
// Окно поменяло размер: swapchain пересоздаётся перед следующим кадром
void render_resized();
// Число ошибок слоя проверки за всё время
u32 render_validation_errors();
const char *render_device_name();

// Сетки: номер в таблице (0 — нет сетки). Место в общих буферах вершин и индексов выдаёт виртуальный блок
// VMA; освобождение — через несколько кадров, когда GPU их точно дорисовал. Загрузка ждёт копию на GPU
struct MeshVertex;
u32 render_mesh_create(Span<const MeshVertex> vertices, Span<const u32> indices);
void render_mesh_destroy(u32 mesh);
// Таблица материалов целиком (номер = место). Ждёт, пока GPU закончит кадры: только при загрузке
bool render_set_materials(Span<const RenderMaterial> materials);

// Земля (docs/terrain.md): сетка высот и маски копируются в видеопамять один раз, числа — как Terrain
// в src/sim/terrain.h (рендер шаг мира не видит, их передаёт app). Рисуется clipmap'ом вокруг камеры
constexpr u32 RENDER_TERRAIN_OCTAVES = 5;
struct RenderTerrain {
    const u16 *height;  // nx·nz
    const u16 *dune;    // nx·nz: дюны в узлах сетки, 1/640 м
    u32 nx, nz;
    const u32 *mask;    // nmx·nmz: шероховатость, песок, трава, сырость
    const u32 *mask2;   // nmx·nmz: сила дюн, пустыня
    const u16 *water;   // nmx·nmz: уровень воды, 5 см; 0 — нет
    u32 nmx, nmz;
    i32 chunk0, size_x, size_z;
    u32 dune_seed;
    i32 oct_lp[RENDER_TERRAIN_OCTAVES], oct_amp[RENDER_TERRAIN_OCTAVES];
    u32 oct_seed[RENDER_TERRAIN_OCTAVES];
};
bool render_terrain_set(const RenderTerrain &t);

// Время прохода forward на GPU за прошлый кадр, мс (0 — таймеров нет)
f64 render_gpu_ms();
// Сколько последний render_frame ждал GPU и показ (забор кадра, картинка swapchain, vsync), мс
f64 render_wait_ms();
// Снимок: render_capture_next() перед render_frame, затем render_capture_take() — пиксели RGBA8 sRGB
// сверху вниз, память из арены. false — снимок не вышел
void render_capture_next();
bool render_capture_take(Arena &arena, u8 **rgba, u32 *width, u32 *height);
