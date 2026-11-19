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
// Морок (docs/tat.md): material ≥ RENDER_VEIL — подмены нет (материалы вершин), младший байт — видимость 0..255;
// RENDER_NO_MATERIAL — то же, что «видна целиком»
constexpr u32 RENDER_VEIL = 0xFFFFFF00u;
inline u32 render_veil_material(f32 veil) {
    f32 v = veil < 0.0f ? 0.0f : (veil > 1.0f ? 1.0f : veil);
    return RENDER_VEIL | (u32)((1.0f - v) * 255.0f + 0.5f);
}

// Декаль (docs/ostov.md, «Декали»): коробка проекции относительно камеры, вид узора, материал, зерно. 48 байт,
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

// Коробка пыли зала (воздух, docs/air.md) относительно камеры. 48 байт, как AirBox в data/shaders/common.glsl
struct RenderAirBox {
    f32 x, y, z, dust;       // середина коробки, метры от камеры; пыль, 1/м
    f32 ux, uz, soft, pad0;  // ось x коробки в мире (ux, 0, uz), ось z — (−uz, 0, ux); мягкий край внутрь, м
    f32 hx, hy, hz, pad1;    // половины сторон
};
static_assert(sizeof(RenderAirBox) == 48, "RenderAirBox — 48 байт");

// Огонь (огни 6е, docs/lamps.md): колба (шар) или трубка (отрезок) относительно камеры. 64 байта, как Light в
// data/shaders/common.glsl. Каркас раскладывает огни по клеткам (froxels), светит ими light.glsl
constexpr u32 LIGHT_SHADOW = 1;  // flags: тень по полю расстояний мира
struct RenderPointLight {
    f32 x, y, z;       // середина тела света, м от камеры
    f32 radius;        // дальше света нет (от отрезка трубки), м
    f32 r, g, b;       // линейный цвет × сила света, кд (яркость цвета — 1)
    f32 size;          // радиус тела света, м: ширина блика и мягкость тени
    f32 ax, ay, az;    // полуось трубки, м; нули — колба
    f32 back;          // доля света назад от face
    f32 fx, fy, fz;    // куда светит (единичный); нули — во все стороны
    u32 flags;         // LIGHT_*
};
static_assert(sizeof(RenderPointLight) == 64, "RenderPointLight — 64 байта");

struct RenderMaterial {
    u32 kind;
    f32 metal;  // 0 — диэлектрик, 1 — металл (docs/materials.md)
    u32 scan;   // слой сканов + 1 (docs/scans.md); 0 — нет, только узор
    u32 tex;    // карты вида baked (docs/textures.md): номера текстур по байту — цвет, нормаль, кривизна, шероховатость; 0 — нет
    f32 p[12];  // смысл — asset/material.h
    f32 scan_tint[3], scan_tile;  // множитель цвета скана; сторона плитки скана, м (делит 256 нацело)
};
static_assert(sizeof(RenderMaterial) == 80, "RenderMaterial — 80 байт");

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

// Старое правило ячеек (docs/render.md, «На пиксель»): узор и свет раз на мировую ячейку, ячейка не мельче
// cell_min метров и растёт с глубиной степенями двойки, пока не станет пикселем кадра. Выключено: свет на пиксель.
// cell_min и cell_pixels по-прежнему задают решётки кеша земли и теней солнца
// Тонмаппинг (post.glsl) и ступени качества света (docs/light.md, «Ступени»): номера — как в common.glsl
enum RenderTonemap : u32 { RENDER_TONEMAP_AGX, RENDER_TONEMAP_ACES, RENDER_TONEMAP_UE, RENDER_TONEMAPS };
// Поле расстояний мира (docs/sdf.md), режим F3: кадр, только видимость неба, поле изолиниями, свет без поля
enum RenderSdfView : u32 { RENDER_SDF_FRAME, RENDER_SDF_SKY, RENDER_SDF_FIELD, RENDER_SDF_OFF, RENDER_SDF_VIEWS };
// Зонды непрямого света (docs/gi.md), режим F3: кадр, только непрямой свет, без зондов
enum RenderGiView : u32 { RENDER_GI_FRAME, RENDER_GI_ONLY, RENDER_GI_OFF, RENDER_GI_CELLS, RENDER_GI_PROBES, RENDER_GI_VIEWS };
enum RenderLightQuality : u32 { RENDER_LIGHT_LOW, RENDER_LIGHT_MEDIUM, RENDER_LIGHT_HIGH, RENDER_LIGHT_ULTRA, RENDER_LIGHT_QUALITIES };
// Сглаживание краёв (docs/aa.md): номера — как AA_* в common.glsl
enum RenderAa : u32 { RENDER_AA_OFF, RENDER_AA_FXAA, RENDER_AA_TAA, RENDER_AAS };

struct RenderStyle {
    bool pixel = false;    // старое правило ячеек (P, --cells), только для сравнения
    u32 aa = RENDER_AA_TAA;  // сглаживание краёв (docs/aa.md)
    bool froxel_heat = false;  // тепловая карта числа огней в клетке froxels поверх кадра (F3)
    bool scans = false;        // поверхности из сканов CC0 вместо узора, где они есть (docs/scans.md, --scans, F3)
    bool gcache = true;        // кеш поверхности земли (docs/gcache.md); выкл — земля на пиксель (F3, --nogcache)
    bool gclight = true;       // отражённый свет земли в кеше (docs/gcache.md); выкл — на пиксель (F3, --nogclight)
    u32 shadow_pages = 0;      // F3 (docs/shadow.md): 1 — страницы теней поверх кадра, 2 — только тень светила
    u32 sdf_view = RENDER_SDF_FRAME;  // F3 (docs/sdf.md): поле расстояний мира
    u32 gi_view = RENDER_GI_FRAME;    // F3 (docs/gi.md): зонды непрямого света
    f32 cell_min = 1.0f / 80.0f;
    f32 cell_pixels = 1.0f;  // ячейка — не меньше стольких пикселей кадра
    // Свет (docs/light.md)
    u32 tonemap = RENDER_TONEMAP_UE;
    // Ступени качества (docs/light.md, «Ступени»): тени; отражённый свет; воздух и облака
    u32 shadow_quality = RENDER_LIGHT_HIGH;
    u32 gi_quality = RENDER_LIGHT_HIGH;
    u32 air_quality = RENDER_LIGHT_HIGH;
    // Местная экспозиция (data/sky.txt, local_tone): колено, стопы; наклон в свет; в тень; 1 — включена
    f32 local_tone[4] = {1.0f, 0.05f, 1.0f, 1.0f};
    // Замер и цвет глаза (data/sky.txt: meter_view, white_balance, adapt_color): доля замера по кадру; [1] ставит
    // рендер; доля привыкания к цвету света; время привыкания к цвету, с
    f32 eye_meter[4] = {0.8f, 0.0f, 0.8f, 1.5f};
    f32 dt = 0.0f;         // секунды кадра: привыкание глаза
    bool eye_snap = true;  // глаз привыкает сразу (первый кадр, --timelapse: кадр — свой час)
    bool clock_cut = false; // первый кадр скачка часов: сглаживание без истории (в ней свет прошлого часа)
    bool glow = true;      // светящиеся детали (стекло ламп, табло) светятся; выкл — вместе с лампами (F3, --nolamps)
    bool lamp_maps = true; // тени огней картами (docs/lamp_shadows.md); выкл — полем расстояний (F3, --nolsm)
    bool shadow_dyn = true; // тени подвижных — людей, машин, оружия (docs/shadow.md); выкл — сравнение (F3, --nodyn)
    bool air = true;       // воздух: лучи солнца и огней в пыли (docs/air.md); выкл — без него (F3, --noair)
};

// Воздух (docs/air.md, data/sky.txt air_*): пыль у земли — слой по высоте от нуля карты, и коробки пыли залов
struct RenderAir {
    f32 dust = 0.0f;        // рассеяние и поглощение у нуля карты, 1/м
    f32 dust_h = 30.0f;     // высота слоя, м
    f32 tint[3] = {1.0f, 1.0f, 1.0f};  // доля рассеяния по каналам
    f32 g = 0.8f, iso = 0.3f;          // лепесток вперёд; доля рассеяния во все стороны
    Span<const RenderAirBox> boxes;
};

// Объектив (docs/lens.md, data/sky.txt lens_*, settings.txt): то, что добавляет кадру камера, а не глаз. Биты —
// как LENS_* в lens.glsl; 0 в силе — то же, что выключено
enum RenderLensFx : u32 { RENDER_LENS_FLARE = 1, RENDER_LENS_CA = 2, RENDER_LENS_VIGNETTE = 4, RENDER_LENS_GRAIN = 8 };
struct RenderLens {
    u32 on = 0;              // биты RENDER_LENS_*
    f32 flare = 1.0f;        // блики солнца в линзах: множитель долей отражения (1 — просветлённый объектив)
    f32 ca = 0.0f;           // хроматическая аберрация: сдвиг крайних длин волн в углу кадра, доля полудиагонали
    f32 vignette = 0.0f;     // виньетка: доля закона cos⁴ (1 — объектив без поправки)
    f32 grain = 0.0f;        // зерно: разброс в средних тонах, доля шкалы экрана
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
    f32 sun_t[3], sun_radius;     // пропускание к солнцу от камеры; угловой радиус, рад (тени, блик)
    f32 moon_t[3], moon_radius;
    f32 rayleigh[3], rayleigh_h;  // рассеяние на уровне моря, 1/м; высота слоя, м
    f32 mie[3], mie_h;            // пыль высокого слоя
    f32 mie_abs[3], mie_g;        // её поглощение; g фазовой функции
    f32 haze[3], haze_h;          // нижняя дымка
    f32 haze_abs[3], alt0;        // её поглощение; высота нуля карты над уровнем моря, м
    f32 ozone[3], moon_albedo;
    f32 ground[3], night_sky;     // альбедо планеты; яркость ночного неба в зените, кд/м² (docs/sky.md, «Ночь»)
    f32 star[3][4];               // строки поворота «вид → небесная сфера», star[0][3] — яркость звёзд
    f32 post[4];                  // глаз: поправка экспозиции, EV; степень привыкания; EV100 «нормы»; сумеречное зрение
    f32 sky_gain;                 // множитель рассеянного света неба (docs/sky.md)
    f32 adapt_dark, adapt_light;  // время привыкания глаза к темноте и к свету, с (docs/light.md)
    f32 night_dark;               // ночь темнее привыкания, EV (docs/sky.md, «Ночь»)
    // Ореол (docs/sky.md): узкий лепесток пыли вперёд рисуется у светила в кадре, в таблицы не идёт
    f32 sun_halo[3], sun_size;    // толщина узкого лепестка к солнцу от камеры; видимый радиус диска, рад
    f32 moon_halo[3], moon_size;
    f32 halo_g, time;             // g узкого лепестка; время кадра, с (мерцание звёзд)
    f32 milky, twinkle;           // яркость Млечного пути; сила мерцания звёзд
    f32 adapt_night;              // степень привыкания глаза ниже света полной луны (docs/sky.md, «Ночь»)
    f32 spare;                    // свободно
    f32 glare;                    // блеск: доля света, что глаз рассеивает вокруг ярких мест (docs/glare.md)
    f32 pad;
    // Облака (docs/clouds.md)
    f32 cloud_shape[4];           // низ и верх кучевых, м над уровнем моря; доля неба под ними; плотность, 1/м
    f32 cloud_wind[4];            // сдвиг облаков ветром к этому кадру по x и z, м; время формы, с; яркость (множитель)
    f32 cloud_cirrus[4];          // высота перистых, м; доля неба под ними; направление ветра (cos, sin)
    f32 cloud_light[3], cloud_e;  // светило облаков (то, что сильнее на их высоте): к нему; освещённость над атмосферой
};
static_assert(sizeof(RenderSky) == 24 * 16, "RenderSky — 24 vec4");

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
    RenderAir air;                       // пыль у земли и в залах (docs/air.md)
    RenderLens lens;                     // эффекты объектива (docs/lens.md)
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
    u32 pipe_threads = 0;  // потоков сборки конвейеров при запуске (--pipe-threads); 0 — по ядрам
};

constexpr u32 RENDER_MAX_POINTS = 4096;
constexpr u32 RENDER_MAX_PASSES = 40;  // строк времени проходов GPU (F3)
constexpr u32 RENDER_MAX_LINE_VERTICES = 32768;
constexpr u32 RENDER_MAX_INSTANCES = 65536;
constexpr u32 RENDER_MAX_MESHES = 4096;
constexpr u32 RENDER_MAX_MATERIALS = 1024;
constexpr u32 RENDER_MAX_DECALS = 512;
constexpr u32 RENDER_MAX_LIGHTS = 1024;
constexpr u32 RENDER_MAX_AIR_BOXES = 32;
constexpr u32 RENDER_MAX_SKINS = 64;       // источников скиннинга (сетка × скелет)
constexpr u32 RENDER_MAX_SKINNED = 768;    // сеток на костях в кадре: люди (48 × тело и до 10 вещей), машины
constexpr u32 RENDER_MAX_BONES = 16384;    // матриц костей в кадре
constexpr u32 RENDER_MAX_MORPHS = 48;     // как MESH_MAX_MORPHS и SkinJob в common.glsl

bool render_init(const RenderConfig &cfg);
void render_shutdown();
// false — неустранимая ошибка устройства
bool render_frame(const RenderFrame &f);
// Окно поменяло размер: swapchain пересоздаётся перед следующим кадром
void render_resized();
// Дождаться, пока GPU доделает всё отправленное (строка запуска после первого кадра). В игровом цикле не нужно
void render_wait_idle();
// Дождаться GPU и прочитать счётчики и глаз последнего отправленного кадра (конец снимка: иначе прочитан кадр N−2)
void render_collect();
// Число ошибок слоя проверки за всё время
u32 render_validation_errors();
const char *render_device_name();

// Сетки: номер в таблице (0 — нет сетки). Место в общих буферах вершин и индексов выдаёт виртуальный блок
// VMA; освобождение — через несколько кадров, когда GPU их точно дорисовал. Загрузка ждёт копию на GPU
// Индексы всех LOD лежат одним массивом, LOD — диапазон в нём (кусок LODS .mesh); без LOD — весь массив
// Кластеры (≤ 124 треугольников, индексы подряд) — для отсечения на GPU кадра мира; LOD — диапазон индексов
// и диапазон кластеров. Без кластеров рендер режет индексы LOD на куски сам
struct MeshVertex;
struct MeshUv;
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
// uvs — развёртка и касательные (кусок VUV0, по вершине) или пусто
u32 render_mesh_create(Span<const MeshVertex> vertices, Span<const u32> indices, Span<const RenderLod> lods,
                       Span<const RenderCluster> clusters, Span<const MeshUv> uvs = {});
void render_mesh_destroy(u32 mesh);
// Сетка движется каждый кадр целиком (меч в руках, машина): в кеш страниц теней и кубов огней не идёт, как скин-копии
// людей, — тень слоем подвижных каждый кадр (docs/shadow.md)
void render_mesh_moving(u32 mesh);
// Сетка с прозрачным краем (волосы карточками, docs/hair.md): visibility buffer рисует её конвейером с отсечением
// по альфе карты цвета её материалов (у материала без карты — сплошь). До копий скиннинга — они берут флаг у источника
void render_mesh_alpha(u32 mesh);

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
    u32 hide;  // области тела под одеждой (docs/wear.md, байт доли смеси вершины тела): не рисуются
    f32 morph[RENDER_MAX_MORPHS];
};
// Таблица материалов целиком (номер = место). Ждёт, пока GPU закончит кадры: только при загрузке
bool render_set_materials(Span<const RenderMaterial> materials);
// Сканы CC0 (docs/scans.md): слои size × size, мипы до 1, RGBA8 — цвет sRGB + AO и нормаль + шероховатость; внутри
// мипа слои подряд. Номер слоя материала (RenderMaterial::scan) — по этому порядку
struct RenderScans {
    u32 layers, size, mips;
    const u8 *albedo, *normal;
    usize array_bytes;
};
bool render_set_scans(const RenderScans &s);
// Текстура ассета (docs/textures.md): блоки BC всех мипов подряд, format — TexFormat (src/asset/tex.h). Номер в
// таблице текстур кадра мира (1…RENDER_MAX_TEXTURES) или 0 — не загрузилась. Только при загрузке
constexpr u32 RENDER_MAX_TEXTURES = 255;
struct RenderTexture {
    u32 format, width, height, mips;
    const u8 *blocks;
    const usize *mip_offset;  // начало мипа в blocks
    usize bytes;
};
u32 render_texture_create(const RenderTexture &t);

// Земля (docs/terrain.md): сетка высот и маски копируются в видеопамять один раз, числа — как Terrain
// в src/sim/terrain.h (рендер шаг мира не видит, их передаёт app). Рисуется clipmap'ом вокруг камеры
constexpr u32 RENDER_TERRAIN_OCTAVES = 5;
constexpr u32 RENDER_TERRAIN_HOLES = 4;
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
    // Проёмы земли (docs/terrain.md, «Проём»): x0, z0, x1, z1 в см от юго-западного угла карты; в них земля не рисуется
    i32 hole[RENDER_TERRAIN_HOLES][4];
    u32 hole_count;
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
    u32 tiles_bail;  // плитки земли, отданные освещением «земле на пиксель» (docs/perf_arch.md, шаг 3)
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
    u32 sm_wait;   // страниц нового шага светила, отложенных на следующие кадры (стройка в фоне)
    // Поле расстояний мира (docs/sdf.md): выборки поля светом, треугольники засыпки, собранные кирпичи
    u32 sdf_taps, sdf_tris, sdf_bricks;
    // Зонды непрямого света (docs/gi.md): зондов с лучами, лучей, выборок поля лучами
    u32 gi_probes, gi_rays, gi_taps;
    // Ячейки света (docs/lcache.md): живых, заявлено заново, посчитано, их лучей, заявок без места, выборок поля,
    // отложено до следующего кадра
    u32 lc_live, lc_new, lc_jobs, lc_rays, lc_over, lc_taps, lc_wait;
    u32 gc_lit;  // текселей кеша земли с пересчитанным отражённым светом (docs/gcache.md)
    // Тени огней картами (docs/lamp_shadows.md): граней нарисовано, вызовов кластеров в них, кластеров без места,
    // выборок карт светом
    u32 lsm_faces, lsm_draws, lsm_over, lsm_taps;
    // Воздух (docs/air.md): клеток с пылью, шагов лучей солнца, выборок страниц теней ими
    u32 air_cells, air_steps, air_taps;
    // Слой подвижных (docs/shadow.md, «Подвижные заслонки»): копий страниц под людьми, машинами и оружием; вызовов их
    // кластеров в копии; копий без места
    u32 sm_dyn, sm_dyn_draws, sm_dyn_over;
    // Слой подвижных у огней (docs/lamp_shadows.md): граней его кубов (копия места, подвижные поверх) и вызовов в них
    u32 lsm_dyn_faces, lsm_dyn_draws;
    u32 sm_blend;  // перетекание шага светила: % нового набора, 100 — один набор (в --work не пишется)
    const char *gc_off;  // почему кеш земли выключен (nullptr — включён); в --work не пишется
};
bool render_work(RenderWork &out);
// Глаз прочитанного кадра (docs/light.md, «Экспозиция»): освещённость горизонтальной земли и EV100. F3
struct RenderLightInfo {
    f32 lux, lux_sky, lux_key;  // всего, от неба, от главного светила (солнце или луна) — на открытой земле
    f32 lux_eye;                // замер глаза (eye.comp): смесь света у камеры и в кадре, к нему привыкает глаз
    f32 ev100, ev100_goal;      // глаз сейчас и к чему привыкает
    f32 exposure;               // множитель кадра
    f32 lux_cam, lux_view;      // две части замера: свет у камеры и мест в кадре (по плиткам прошлого кадра), лк
    f32 wb_white[3];            // цвет света, к которому привык глаз (линейный sRGB, Y = 1)
    f32 wb_gain[3], wb_degree;  // множители конусов CAT02 и доля привыкания к цвету (баланс белого)
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
// HDR-снимок (отладка света, --hdr): вместе со снимком — HDR кадра мира до сглаживания (RGBA половинками, уже
// умножен на экспозицию; a — свет места, лк, для разбора местной экспозиции). Брать после render_capture_take
void render_capture_hdr(bool on);
bool render_capture_hdr_take(Arena &arena, u16 **rgba, u32 *width, u32 *height);
bool render_capture_take(Arena &arena, u8 **rgba, u32 *width, u32 *height);

// Интерфейс поверх кадра (src/ui, docs/ui.md): последний проход по готовой картинке, в пикселях окна.
// Не часть кадра мира: этап 5 перестраивает кадр, не трогая его
struct RenderUiQuad {
    f32 x0, y0, x1, y1;  // пиксели окна, от левого верхнего угла
    f32 u0, v0, u1, v1;  // пиксели атласа букв; u0 < 0 — заливка без атласа
    f32 cx0, cy0, cx1, cy1;  // обрезка (область прокрутки): вне неё не рисуется; форма — по полному прямоугольнику
    u32 rgba;            // sRGB
    f32 feather;         // мягкий край внутрь, пикселей (0 — резкий)
    f32 radius;          // скругление углов, пикселей (0 — прямые)
    f32 stroke;          // толщина обводки, пикселей (0 — заливка)
};
static_assert(sizeof(RenderUiQuad) == 64, "RenderUiQuad — 64 байта");
// Атлас букв R8 (ширина кратна 4) — в видеопамять, при загрузке
bool render_ui_font(const u8 *atlas, u32 width, u32 height);
// Прямоугольники интерфейса следующего render_frame (рисуются по порядку); без вызова кадр без интерфейса
void render_ui_set(Span<const RenderUiQuad> quads);
// Вертикальная синхронизация: swapchain пересоздаётся перед следующим кадром
void render_set_vsync(bool on);
