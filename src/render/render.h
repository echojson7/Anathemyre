// Рендер: Vulkan 1.3. Только рисует то, что дал вид; мир не читает и не меняет (шов 2).
// Швы кадра (docs/stack.md, docs/frame.md): общий буфер сеток, экземпляры в буфере GPU, 64-битный ключ порядка,
// обратная глубина, всё относительно камеры, surface()/light_surface(). Кадр мира — visibility buffer (этап 5)
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
    u32 lod_min;       // не тоньше этого LOD; пишет рендер (у человека — LOD, вершины которого скинены), вид кладёт 0
};
static_assert(sizeof(RenderInstance) == 48, "RenderInstance — 48 байт");
constexpr u32 RENDER_NO_MATERIAL = 0xFFFFFFFFu;

// Декаль (docs/ruin.md, «Декали»): коробка проекции относительно камеры, вид узора, материал, зерно. 48 байт,
// как Decal в data/shaders/common.glsl
enum RenderDecalKind : u32 { DECAL_STREAK = 0, DECAL_GRIME = 1, DECAL_DUST = 2, DECAL_STAIN = 3, DECAL_KINDS = 4 };
struct RenderDecal {
    f32 x, y, z;       // середина коробки, метры от камеры
    f32 ux, uz;        // ось x коробки в мире (ux, 0, uz); ось z — (−uz, 0, ux)
    f32 hx, hy, hz;    // половины сторон
    u32 kind;          // RenderDecalKind
    u32 material;      // номер в таблице материалов: цвет, шероховатость, сила
    u32 seed;
    u32 pad;
};
static_assert(sizeof(RenderDecal) == 48, "RenderDecal — 48 байт");

// Огонь (вход света пользователя, docs/frame.md «Froxels»): шар относительно камеры, цвет. 32 байта, как Light
// в data/shaders/common.glsl. Каркас огни только раскладывает по клеткам, светит ими light.glsl (этап 6)
struct RenderPointLight {
    f32 x, y, z;       // метры от камеры
    f32 radius;        // дальше света нет, м
    f32 r, g, b;       // линейный цвет × сила
    u32 pad;
};
static_assert(sizeof(RenderPointLight) == 32, "RenderPointLight — 32 байта");

struct RenderMaterial {
    u32 kind;
    f32 metal;  // 0 — диэлектрик, 1 — металл (docs/materials.md)
    u32 pad[2];
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
// Тонмаппинг (post.glsl) и ступени качества света (docs/light.md, «Ступени»): номера — как в common.glsl
enum RenderTonemap : u32 { RENDER_TONEMAP_AGX, RENDER_TONEMAP_ACES, RENDER_TONEMAPS };
enum RenderLightQuality : u32 { RENDER_LIGHT_LOW, RENDER_LIGHT_MEDIUM, RENDER_LIGHT_HIGH, RENDER_LIGHT_ULTRA, RENDER_LIGHT_QUALITIES };

struct RenderStyle {
    bool pixel = true;     // выключается клавишей P
    bool froxel_heat = false;  // тепловая карта числа огней в клетке froxels поверх кадра (F3)
    bool gcache = true;        // кеш поверхности земли (docs/gcache.md); выкл — земля на пиксель (F3, --nogcache)
    u32 shadow_pages = 0;      // F3 (docs/shadow.md): 1 — страницы теней поверх кадра, 2 — только тень светила
    f32 cell_min = 1.0f / 80.0f;
    f32 cell_pixels = 1.0f;  // ячейка — не меньше стольких пикселей кадра
    // Свет (docs/light.md)
    u32 tonemap = RENDER_TONEMAP_AGX;
    u32 light_quality = RENDER_LIGHT_HIGH;
    f32 dt = 0.0f;         // секунды кадра: привыкание глаза
    bool eye_snap = true;  // глаз привыкает сразу (первый кадр, --timelapse: кадр — свой час)
};

// Главный свет (light.glsl): солнце, ночью луна
struct RenderLight {
    f32 sun_dir[3];    // к источнику
    f32 sun_color[3];  // освещённость перпендикулярной площадки у камеры (после атмосферы), лк по каналам
};

// Небо и атмосфера (docs/sky.md): светила из часов мира и слои воздуха из data/sky.txt. Раскладка — как хвост
// FrameData в data/shaders/common.glsl (a_sun…a_post), копируется в буфер кадра целиком
struct RenderSky {
    f32 sun_dir[3], sun_e;        // к солнцу; освещённость от солнца над атмосферой
    f32 moon_dir[3], moon_e;      // к луне; освещённость от луны над атмосферой
    f32 sun_t[3], sun_radius;     // пропускание к солнцу от камеры; угловой радиус, рад
    f32 moon_t[3], moon_radius;
    f32 rayleigh[3], rayleigh_h;  // рассеяние на уровне моря, 1/м; высота слоя, м
    f32 mie[3], mie_h;            // пыль высокого слоя
    f32 mie_abs[3], mie_g;        // её поглощение; g фазовой функции
    f32 haze[3], haze_h;          // нижняя дымка
    f32 haze_abs[3], alt0;        // её поглощение; высота нуля карты над уровнем моря, м
    f32 ozone[3], moon_albedo;
    f32 ground[3], night_glow;    // альбедо планеты; ночное свечение неба (доля освещённости солнца)
    f32 star[3][4];               // строки поворота «вид → небесная сфера», star[0][3] — яркость звёзд
    f32 post[4];                  // глаз: поправка экспозиции, EV; степень привыкания; EV100 «нормы»; синева ночи
    f32 sky_gain;                 // множитель рассеянного света неба (docs/sky.md)
    f32 adapt_dark, adapt_light;  // время привыкания глаза к темноте и к свету, с (docs/light.md)
    f32 pad;
};
static_assert(sizeof(RenderSky) == 16 * 16, "RenderSky — 16 vec4");

// Слои земли: песок, сухая земля, щебень, камень, трава, вода (GroundKind, src/sim/ground.h), затем песок и камень
// пустыни (тот же вид земли, другой материал — по маске пустыни)
constexpr u32 RENDER_GROUND_LAYERS = 8;

struct RenderSkinned;
struct RenderBone;
struct RenderFrame {
    RenderView view;
    RenderStyle style;
    RenderLight light;
    RenderSky sky;
    bool ground;       // рисовать землю (её данные — render_terrain_set)
    u32 ground_layers[RENDER_GROUND_LAYERS];  // материалы слоёв
    Span<const RenderInstance> instances;
    Span<const RenderPoint> points;
    Span<const RenderLineVertex> lines;  // пары вершин
    Span<const RenderDecal> decals;      // все, что задевают видимое; по клеткам их раскладывает рендер (froxels)
    Span<const RenderPointLight> lights; // огни кадра; по клеткам их раскладывает рендер (froxels)
    Span<const RenderSkinned> skinned;   // люди кадра: экземпляр, палитра, морфы (скиннинг, ниже)
    Span<const RenderBone> bones;        // палитры костей людей кадра подряд
};

struct RenderConfig {
    bool vsync = true;
    // Кеш конвейеров на диске (docs/frame_time.md, «Кеш конвейеров»): читается при запуске, пишется при выходе.
    // nullptr — без файла
    const char *pipeline_cache_path = nullptr;
    bool validation = false;  // слои проверки Vulkan с проверкой синхронизации
    bool gpu_validation = false;  // слои проверки с проверкой на GPU (выход за границы буферов, GPU-AV) вместо синхронизации
    const char *gpu_validation_skip = nullptr;  // шейдеры без проверки на GPU, через запятую (lavapipe их не тянет)
    const char *shader_dir = "";
};

constexpr u32 RENDER_MAX_POINTS = 4096;
constexpr u32 RENDER_MAX_PASSES = 15;  // строк времени проходов GPU (F3)
constexpr u32 RENDER_MAX_LINE_VERTICES = 32768;
constexpr u32 RENDER_MAX_INSTANCES = 65536;
constexpr u32 RENDER_MAX_MESHES = 4096;
constexpr u32 RENDER_MAX_MATERIALS = 1024;
constexpr u32 RENDER_MAX_DECALS = 512;
constexpr u32 RENDER_MAX_LIGHTS = 1024;
constexpr u32 RENDER_MAX_SKINS = 64;       // источников скиннинга (сетка × скелет)
constexpr u32 RENDER_MAX_SKINNED = 256;    // людей в кадре
constexpr u32 RENDER_MAX_BONES = 16384;    // матриц костей в кадре
constexpr u32 RENDER_MAX_MORPHS = 8;

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
// Индексы всех LOD лежат одним массивом, LOD — диапазон в нём (кусок LODS .mesh); без LOD — весь массив
// Кластеры (≤ 124 треугольников, индексы подряд) — для отсечения на GPU кадра мира; LOD — диапазон индексов
// и диапазон кластеров. Без кластеров рендер режет индексы LOD на куски сам
struct MeshVertex;
constexpr u32 RENDER_MESH_MAX_LODS = 8;
struct RenderLod {
    u32 first_index, index_count;
    u32 first_cluster, cluster_count;  // в кластерах сетки
    f32 error;                         // ошибка упрощения, м
};
struct RenderCluster {
    u32 first_index, tri_count;  // от начала индексов сетки
    f32 sphere[4];               // в осях сетки
};
u32 render_mesh_create(Span<const MeshVertex> vertices, Span<const u32> indices, Span<const RenderLod> lods,
                       Span<const RenderCluster> clusters);
void render_mesh_destroy(u32 mesh);
// Сетка движется каждый кадр целиком (меч в руках): в страницы теней не идёт до 6б-3, как скин-копии людей
void render_mesh_moving(u32 mesh);

// Скиннинг (docs/people.md, «Скиннинг»). Источник — сетка с весами одного скелета (render_mesh_create вершин
// бинд-позы) и её морфы; у каждого человека своя копия вершин и кластеров (render_skinned_create, отдельный
// номер сетки для экземпляра, индексы общие с источником). Каждый кадр compute-проход пишет в копию вершины
// префикса LOD (вершины по LOD — префиксы, кусок LODV .mesh) из палитры костей и пересчитывает сферы кластеров;
// дальше человек идёт в кадр мира как деталь. Источники и копии живут до render_shutdown
struct RenderSkinDesc {
    u32 mesh;                     // сетка вершин бинд-позы (render_mesh_create)
    u32 bone_count;               // костей скелета: палитра человека — столько матриц
    Span<const u32> bones4;       // на вершину: 4 номера костей u8
    Span<const u32> weights4;     // на вершину: 4 веса unorm8, сумма 255
    u32 morph_count;              // ≤ RENDER_MAX_MORPHS
    const f32 *morph_step;        // метров на единицу сдвига, на морф
    Span<const u32> morphs;       // 3 слова на вершину на морф (MeshMorphs, src/asset/mesh.h)
    Span<const u32> lod_vertices; // вершин LOD k (префикс), на каждый LOD сетки
};
// Номер источника (0 — ошибка). Загрузка ждёт копию на GPU: только при загрузке
u32 render_skin_create(const RenderSkinDesc &d);
// Копия вершин и кластеров источника для одного человека: номер сетки для RenderInstance::mesh (0 — ошибка).
// Вершины до первого скиннинга — бинд-поза
u32 render_skinned_create(u32 skin);
// Матрица кости 3×4 по строкам: точка сетки в бинд-позе → точка сетки в позе (поза · обратная бинд-матрица)
struct RenderBone {
    f32 m[12];
};
// Человек кадра: экземпляр в RenderFrame::instances (его сетка — из render_skinned_create), палитра —
// RenderFrame::bones[first_bone …], веса морфов источника
struct RenderSkinned {
    u32 instance;
    u32 first_bone;
    f32 morph[RENDER_MAX_MORPHS];
};
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
    const u16 *fine;    // карты дюн 2 м локаций подряд, 1/640 м (fine_n² на локацию); nullptr — нет
    usize fine_count;
    u32 fine_n;
    i32 fine_off[4];    // начало карты локации, −1 — нет
    i32 loc_m;          // сторона локации, м
    u32 nmx, nmz;
    i32 chunk0, size_x, size_z;
    u32 dune_seed;
    i32 oct_lp[RENDER_TERRAIN_OCTAVES], oct_amp[RENDER_TERRAIN_OCTAVES];
    u32 oct_seed[RENDER_TERRAIN_OCTAVES];
};
bool render_terrain_set(const RenderTerrain &t);

// Время кадра мира на GPU за прошлый кадр (без интерфейса), мс (0 — таймеров нет)
f64 render_gpu_ms();
// Время проходов прошлого кадра: имена и мс, до cap; возвращает число проходов (0 — таймеров нет)
u32 render_gpu_passes(const char **names, f64 *ms, u32 cap);
// Видимых кластеров в прошлом кадре мира
u32 render_vis_clusters();
// Переполнение (docs/graph.md, «Переполнения»): что не влезло в буфер с пределом. cap == 0 — счёт без предела
// (например, клеток froxels, где огни не влезли и читатель перебирает весь список кадра)
struct RenderOverflow {
    const char *what;
    u64 want, cap;
};
// Переполнения последнего кадра: процессор — этого кадра, GPU — прочитанного (на кадр-два раньше)
u32 render_overflows(RenderOverflow *out, u32 cap);
// Работа освещения прочитанного кадра мира (docs/tiles.md): счётчики шейдеров. Для ракурса и часа они те же от
// запуска к запуску, по ним CI ловит «стало делать больше работы». false — кадр ещё не прочитан
constexpr u32 RENDER_TILE_KINDS = 5;  // плитки: небо, земля, сетки, смесь, земля на пиксель
struct RenderWork {
    u32 tiles[RENDER_TILE_KINDS];
    u32 px_sky, px_ground, px_mesh;  // пиксели по видам
    u32 surf_ground, surf_mesh;      // поверхности: surface() и light_surface() раз на ячейку
    u32 shadow_taps;                // выборки карты горизонта (тень земли, docs/horizon.md)
    // Кеш поверхности земли (docs/gcache.md): страницы, прочитанные кадром; запечённые; без места; поверхности
    // земли, посчитанные освещением мимо кеша (крутые склоны, нет места)
    u32 gc_used, gc_baked, gc_over, gc_direct;
    u32 skin_verts;                  // вершин скинено (люди, docs/people.md)
    // Тени солнца страницами (docs/shadow.md): страницы, прочитанные кадром; нарисованные в нём; заявки без
    // места; выборки без страницы; вызовы кластеров в страницы; выборки текселей карты
    u32 sm_used, sm_new, sm_over, sm_miss, sm_draws, sm_taps;
    const char *gc_off;  // почему кеш земли выключен (nullptr — включён); в --work не пишется
};
bool render_work(RenderWork &out);
// Глаз прочитанного кадра (docs/light.md, «Экспозиция»): освещённость горизонтальной земли и EV100. F3
struct RenderLightInfo {
    f32 lux, lux_sky, lux_key;  // всего, от неба, от главного светила (солнце или луна)
    f32 ev100, ev100_goal;      // глаз сейчас и к чему привыкает
    f32 exposure;               // множитель кадра
};
bool render_light(RenderLightInfo &out);
// Процессорное время последнего render_frame по частям, мс (docs/frame_time.md). Ожидание — fence, acquire и
// present: там процессор стоит, пока GPU доделывает кадр N−2, swapchain отдаёт картинку или ждёт vsync
struct RenderCpuTimes {
    f64 fence;    // ожидание слота: GPU доделывает кадр, который писал этот слот
    f64 prepare;  // пересоздание swapchain, сбор прошлого кадра (метки времени, счётчики), данные кадра в буферы
    f64 acquire;  // картинка swapchain (vkAcquireNextImageKHR)
    f64 record;   // граф кадра и запись команд
    f64 submit;   // vkQueueSubmit2
    f64 present;  // vkQueuePresentKHR
};
RenderCpuTimes render_cpu_times();
// Снимок: render_capture_next() перед render_frame, затем render_capture_take() — пиксели RGBA8 sRGB
// сверху вниз, память из арены. false — снимок не вышел
void render_capture_next();
bool render_capture_take(Arena &arena, u8 **rgba, u32 *width, u32 *height);

// Интерфейс поверх кадра (src/ui, docs/ui.md): последний проход по готовой картинке, в пикселях окна.
// Не часть кадра мира: этап 5 перестраивает кадр, не трогая его
struct RenderUiQuad {
    f32 x0, y0, x1, y1;  // пиксели окна, от левого верхнего угла
    f32 u0, v0, u1, v1;  // пиксели атласа букв; u0 < 0 — заливка без атласа
    u32 rgba;            // sRGB
    f32 feather;         // мягкий край внутрь, пикселей (0 — резкий)
    u32 pad[2];
};
static_assert(sizeof(RenderUiQuad) == 48, "RenderUiQuad — 48 байт");
// Атлас букв R8 (ширина кратна 4) — в видеопамять, при загрузке
bool render_ui_font(const u8 *atlas, u32 width, u32 height);
// Прямоугольники интерфейса следующего render_frame (рисуются по порядку); без вызова кадр без интерфейса
void render_ui_set(Span<const RenderUiQuad> quads);
// Вертикальная синхронизация: swapchain пересоздаётся перед следующим кадром
void render_set_vsync(bool on);
