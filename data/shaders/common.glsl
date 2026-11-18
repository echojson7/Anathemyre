// Общее для шейдеров: данные кадра (FrameGpu в src/render/vk_state.h), вершина детали, экземпляр, материал.
// Всё читается по адресам буферов (buffer_reference), раскладка scalar — как структуры C++ без дыр
#ifndef COMMON_GLSL
#define COMMON_GLSL
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_control_flow_attributes : require

// Вершина детали — MeshVertex (src/asset/mesh.h), 24 байта
struct Vertex { float px, py, pz; uint normal; uint mats; uint misc; };
// Экземпляр — RenderInstance (src/render/render.h), 48 байт
struct Instance { int cx, cz; float x, y, z; uint rot_xy, rot_zw; float scale; uint mesh, material, seed, lod_min; };
// Материал — RenderMaterial, 64 байта: p0 = color.rgb + rough, p1 = color2.rgb + texel, p2 = extra;
// pad0 — биты f32 metal (0 — диэлектрик, 1 — металл)
struct Material { uint kind, pad0, scan, pad2; vec4 p0, p1, p2, p3; };  // scan, p3 — сканы (docs/scans.md)
struct LineVtx { float x, y, z; uint rgba; };
// Декаль — RenderDecal (src/render/render.h), 48 байт
struct Decal { vec3 c; float ux, uz; vec3 h; uint kind, material, seed, pad; };
// Огонь — RenderPointLight (src/render/render.h), 64 байта: колба или трубка относительно камеры, цвет × сила, кд
// (docs/lamps.md); axis — полуось трубки, face — куда светит (нули — во все стороны), back — доля света назад
struct Light { vec3 c; float radius; vec3 color; float size; vec3 axis; float back; vec3 face; uint flags; };
const uint LIGHT_SHADOW = 1u;  // тень по полю расстояний мира
struct PointVtx { float x, y, z, size; uint rgba, pad0, pad1, pad2; };

layout(buffer_reference, scalar) readonly buffer Vertices { Vertex v[]; };
layout(buffer_reference, scalar) readonly buffer Instances { Instance v[]; };
layout(buffer_reference, scalar) readonly buffer Materials { Material v[]; };
layout(buffer_reference, scalar) readonly buffer Lines { LineVtx v[]; };
layout(buffer_reference, scalar) readonly buffer Points { PointVtx v[]; };
layout(buffer_reference, scalar) readonly buffer Words { uint v[]; };
layout(buffer_reference, scalar) readonly buffer Decals { Decal v[]; };
layout(buffer_reference, scalar) readonly buffer Lights { Light v[]; };
layout(buffer_reference, scalar) buffer Froxels { uint v[]; };  // списки клеток (froxel.glsl)
struct GroundVert { vec3 rel; float wdepth; vec3 normal; uint pad; };  // вершина земли кадра (ground_bake.comp)
layout(buffer_reference, scalar) buffer GroundVerts { GroundVert v[]; };
layout(buffer_reference, scalar) buffer Vec4s { vec4 v[]; };  // таблицы неба (пишут sky_*.comp)
layout(buffer_reference, scalar) buffer CloudWords { uint v[]; };
layout(buffer_reference, scalar) buffer GlareTexels { uvec2 v[]; };  // блеск (glare.glsl): rgb половинками  // облака (clouds.glsl): карта купола, шум, карта тени
// Счётчики GPU кадра (docs/graph.md «Переполнения»): сколько не влезло; номера — как STAT_* в vk_state.h
layout(buffer_reference, scalar) buffer Stats { uint v[]; };
// Грань карты огня кадра (LsmJobGpu, vk_lamp_shadow.cpp): огонь от камеры, м; радиус его светильника от отрезка, м;
// полуось трубки; место · 6 + грань; куда светит (нули — во все стороны)
struct LsmJob { vec3 c; float self_r; vec3 axis; uint layer; vec3 face; uint pad; };
layout(buffer_reference, scalar) readonly buffer LsmJobs { LsmJob v[]; };
layout(buffer_reference, scalar) buffer LsmCounts { uint v[]; };  // вызовов на грань кадра (атомарно)
layout(buffer_reference, scalar) buffer HMap { uint v[]; };  // карта горизонта (horizon.glsl), пишет horizon.comp
// Воздух (docs/air.md, air.glsl): коробка пыли зала от камеры — середина, пыль 1/м; ось x коробки в плоскости xz,
// мягкий край, м; половины сторон (RenderAirBox). Расстояния половины кадра, свет солнца в воздухе (rgb половинками),
// дальний край плитки воздуха (биты f32, атомарный максимум)
struct AirBox { vec3 c; float dust; vec2 u; float soft; float pad0; vec3 h; float pad1; };
layout(buffer_reference, scalar) readonly buffer AirBoxes { AirBox v[]; };
layout(buffer_reference, scalar) buffer AirFloats { float v[]; };
layout(buffer_reference, scalar) buffer AirTexels { uvec2 v[]; };
layout(buffer_reference, scalar) buffer AirWords { uint v[]; };
const uint STAT_VIS_WANT = 0u;       // видимых кластеров до обрезки по vis_max
const uint STAT_FROXEL_LIGHTS = 1u;  // клеток, где огни не влезли (свет перебирает все огни кадра)
const uint STAT_FROXEL_DECALS = 2u;  // клеток, где декали не влезли
// Счётчики работы освещения (docs/tiles.md): плитки по видам, пиксели по видам, поверхности (surface() и
// light_surface() раз на ячейку), выборки карты горизонта (тень земли)
const uint STAT_TILES = 3u;          // 5 подряд: TILE_SKY..TILE_GROUND_PX
const uint STAT_PX_SKY = 8u, STAT_PX_GROUND = 9u, STAT_PX_MESH = 10u;
const uint STAT_SURF_GROUND = 11u, STAT_SURF_MESH = 12u;
const uint STAT_SHADOW_TAPS = 13u;
// Кеш поверхности земли (docs/gcache.md): страницы, которые кадр читал; запечённые; не получившие места;
// пиксели земли мимо кеша (крутые склоны, правило пиксельности выключено)
const uint STAT_GC_USED = 14u, STAT_GC_BAKED = 15u, STAT_GC_OVER = 16u, STAT_GC_PX_DIRECT = 17u;
const uint STAT_SKIN_VERTS = 18u;  // вершин скинено (skin.comp)
// Тени солнца страницами (docs/shadow.md): страницы, прочитанные кадром; нарисованные в нём; заявки без места;
// выборки без страницы; вызовы кластеров в страницы; выборки текселей карты
const uint STAT_SM_USED = 19u, STAT_SM_NEW = 20u, STAT_SM_OVER = 21u, STAT_SM_MISS = 22u, STAT_SM_DRAWS = 23u,
           STAT_SM_TAPS = 24u;
const uint STAT_SM_WAIT = 25u;  // страниц нового шага светила, отложенных до следующих кадров (стройка в фоне)

// Поле расстояний мира (docs/sdf.md): выборки поля светом, треугольники засыпки, кирпичи, собранные в кадре
const uint STAT_SDF_TAPS = 26u, STAT_SDF_TRIS = 27u, STAT_SDF_BRICKS = 28u;
// Кольца: SDF_LEVELS по SDF_N³ вокселей, слой уровня в картинке — SDF_SLAB по z; полоса засыпки — SDF_BAND
// вокселей (дальше поле — «пусто», ровно SDF_BAND вокселей уровня)
const int SDF_LEVELS = 5, SDF_N = 128, SDF_SLAB = 130;
const float SDF_BAND = 4.0;
const uint SDF_EMPTY = 0xFFFFFFFFu;  // засыпка: воксель без треугольника рядом
const uint SDF_VIEW_FRAME = 0u, SDF_VIEW_SKY = 1u, SDF_VIEW_FIELD = 2u, SDF_VIEW_OFF = 3u;
layout(buffer_reference, scalar) readonly buffer SdfJobs { uvec2 v[]; };    // экземпляр; кластер | уровень << 24
layout(buffer_reference, scalar) readonly buffer SdfBricks { ivec4 v[]; };  // мировой номер кирпича, уровень
layout(buffer_reference, scalar) buffer SdfScratch { uint v[]; };  // уровень · SDF_N³ + место: расстояние << 1 | внутри

// Зонды непрямого света (docs/gi.md, gi.glsl): зонд — место (м от якоря поля) после сдвига из стен, состояние,
// мировой номер и уровень. Лучи работ кадра — яркость и расстояние до попадания половинками (−1 — небо)
const uint STAT_GI_PROBES = 29u, STAT_GI_RAYS = 30u, STAT_GI_TAPS = 31u;
struct GiProbe { vec3 pos; uint state; ivec3 home; int level; };
layout(buffer_reference, scalar) buffer GiProbes { GiProbe v[]; };
layout(buffer_reference, scalar) buffer GiRays { uvec2 v[]; };
layout(buffer_reference, scalar) buffer GiJobs { uint v[]; };

// Ячейки света на поверхностях (docs/lcache.md, lcache.glsl): таблица ключей корзинами по 8, на место —
// мировой номер, кадр последнего взгляда, состояние; показанный свет (половинки) и сумма круга
const uint STAT_LC_LIVE = 32u, STAT_LC_NEW = 33u, STAT_LC_JOBS = 34u, STAT_LC_RAYS = 35u, STAT_LC_OVER = 36u,
           STAT_LC_TAPS = 37u, STAT_LC_WAIT = 38u;
const uint STAT_GC_LIT = 39u;  // текселей кеша земли, чей отражённый свет посчитан в кадре (docs/gcache.md)
// Тени огней картами (docs/lamp_shadows.md): граней нарисовано, вызовов кластеров в них (до обрезки), граней, где
// вызовы не влезли, выборок карт светом
const uint STAT_LSM_FACES = 40u, STAT_LSM_DRAWS = 41u, STAT_LSM_OVER = 42u, STAT_LSM_TAPS = 43u;
// Воздух (docs/air.md): клеток с пылью, шагов лучей солнца, выборок страниц теней ими
const uint STAT_AIR_CELLS = 44u, STAT_AIR_STEPS = 45u, STAT_AIR_TAPS = 46u;
const uint GC_LIGHT_ON = 1u, GC_LIGHT_ALL = 2u;  // gc_lmode: свет в кеше читается; пересчитать все страницы кадра
// org — точка лучей, м от якоря; ao — половинками ближнее затенение по полю у точки ячейки (x) и оно же,
// сглаженное как свет (y): его лучи ячейки уже видят, пиксель делит своё на него (light.glsl, lc_ao)
struct LcMeta { ivec3 home; uint seen; uint key; uint state; vec3 org; uint ao; };
struct LcSh { uvec4 a; uvec2 b; };      // a0.rgb, a1x.rgb, a1y.rgb, a1z.rgb половинками
struct LcAcc { vec4 a, b, c; };         // те же 12 чисел суммой круга
layout(buffer_reference, scalar) buffer LcKeys { uint v[]; };
layout(buffer_reference, scalar) readonly buffer LcKeys4 { uvec4 v[]; };  // те же ключи по 4: корзина — два чтения
layout(buffer_reference, scalar) buffer LcMetas { LcMeta v[]; };
layout(buffer_reference, scalar) buffer LcShs { LcSh v[]; };
layout(buffer_reference, scalar) buffer LcAccs { LcAcc v[]; };
// Работы кадра: молодых и на обновление (до предела), живых; косвенный dispatch лучей; молодые — с v[0], на
// обновление — с v[LC_CAP]
// Работы ячеек: счётчики, полный круг новым в этом кадре (full), косвенный dispatch, молодые по корзинам очереди
// (bn — сколько, bs — начало корзины в номерах работ кадра; lcache_list.comp, lcache_args.comp); age — срок жизни
// невидимой ячейки на следующий кадр (по заполнению таблицы; не чистится каждый кадр); места: молодые [0, LC_CAP),
// обновление [LC_CAP, 2·LC_CAP)
layout(buffer_reference, scalar) buffer LcJobs {
    uint new_n, ref_n, live_n, full;
    uvec4 cmd;
    uint bn[16];
    uint bs[16];
    uint age, pad0, pad1, pad2;
    uint v[];
};

// Плитки 8×8 кадра мира по видам (docs/tiles.md, light_tiles.comp): небо — пусто; земля — земля из кеша
// (docs/gcache.md) и, может быть, небо; сетки — сетки и, может быть, небо; смесь — земля и сетки; земля на
// пиксель — земля, которой нет в кеше (крутые склоны), и, может быть, небо. На вид — косвенный dispatch и список
const uint TILE_SKY = 0u, TILE_GROUND = 1u, TILE_MESH = 2u, TILE_MIX = 3u, TILE_GROUND_PX = 4u, TILE_KINDS = 5u;
struct TileCmd { uint x, y, z, pad; };  // VkDispatchIndirectCommand, шаг 16 байт
// Плитка в списке — x | y << 16 (номер плитки, не пикселя); список вида k — с k · tile_cap
layout(buffer_reference, scalar) buffer Tiles { TileCmd cmd[TILE_KINDS]; uint list[]; };

// Кеш поверхности земли (docs/gcache.md, gcache.glsl): страница — 8×8 текселей одного уровня ячейки
struct GcPage { uint key0, key1, used, lit; };  // ключ страницы (уровень << 24 | x, z), кадр последнего чтения, света (0 — нет)
layout(buffer_reference, scalar) buffer GcTable { uint v[]; };  // окна страниц уровней
layout(buffer_reference, scalar) buffer GcPages { GcPage v[]; };
layout(buffer_reference, scalar) buffer GcTexels { uvec4 v[]; };  // тексель: два варианта по 8 байт
// Отражённый свет текселя (docs/gcache.md): высота точки, E/π (половинки) с отметкой «посчитан», доли зондов,
// неба, ближнее затенение, доля неба к отражению (по 8 бит)
layout(buffer_reference, scalar) buffer GcLight { uvec4 v[]; };
// Заявки кадра и свободные страницы: счёт заявок, «старых» и «недавних» свободных, dispatch запекания; дальше
// заявки (ключи), старые и недавние — по req_cap и pages_n мест
layout(buffer_reference, scalar) buffer GcReq { uint req_n, old_n, recent_n, pad; uvec4 bake; uint v[]; };
struct GcCell { uint kt; int tz; float depth; };  // ячейка земли пикселя: уровень << 24 | x, z; глубина
layout(buffer_reference, scalar) buffer GcCells { GcCell v[]; };

// Тени солнца страницами (docs/shadow.md, shadow.glsl): страница — SM_PAGE² текселей одного уровня; тексель —
// высота ближайшей к светилу заслонки (sm_enc), 0 — заслонки нет
struct SmPage { uint key0, key1, used, made, top; };  // ключ (место уровня << 24 | x, z), кадр чтения, кадр отрисовки,
                                                    // самая высокая заслонка страницы (sm_enc; 0 — пустая)
layout(buffer_reference, scalar) buffer SmTable { uint v[]; };
layout(buffer_reference, scalar) buffer SmPages { SmPage v[]; };
layout(buffer_reference, scalar) buffer SmTexels { uint v[]; };
// Заявки и свободные страницы: счёт заявок (показанных наборов), старых и недавних свободных, заявок набора, что
// строится в фоне; dispatch выдачи (w — сколько из них заявки показанных); на место уровня (оба набора) — есть ли
// новые страницы и их биты по месту в окне; дальше заявки показанных, заявки фона (по req_cap), старые и недавние
// свободные (по pages_n)
layout(buffer_reference, scalar) buffer SmReq { uint req_n, old_n, recent_n, bg_n; uvec4 alloc; uint fresh[32];
                                                 uint bits[32 * 32]; uint v[]; };
struct SmDrawCmd { uint index_count, instance_count, first_index; int vertex_offset; uint first_instance; };
layout(buffer_reference, scalar) buffer SmDraws { uint count, pad0, pad1, pad2; SmDrawCmd v[]; };
layout(buffer_reference, scalar) buffer SmList { uvec2 v[]; };  // экземпляр; кластер | место уровня << 27

// Кадр мира (этап 5, docs/frame.md «Кадр мира»): таблицы сеток и кластеров (MeshGpu, ClusterGpu в
// src/render/vk_state.h), видимые кластеры кадра и косвенные вызовы, которые пишет отсечение (cull_*.comp)
struct MeshInfo { uint first_vertex, first_index, lod_count, flags; vec4 sphere; uvec4 lods[8]; };  // lods: первый кластер, число, ошибка (биты f32), 0;
// lods[0].w копии со скиннингом — первая вершина бинд-позы источника + 1
const uint MESH_DYNAMIC = 1u;  // MeshInfo.flags: вершины пишет скиннинг каждый кадр (в страницы теней не идёт)
struct Cluster { uint first_index, tri_count; vec4 sphere; uint pad0, pad1; };  // first_index — от начала индексов сетки
struct DrawCmd { uint index_count, instance_count, first_index; int vertex_offset; uint first_instance; };
layout(buffer_reference, scalar) readonly buffer MeshInfos { MeshInfo v[]; };
layout(buffer_reference, scalar) readonly buffer Clusters { Cluster v[]; };
layout(buffer_reference, scalar) buffer VisClusters { uvec2 v[]; };  // экземпляр, кластер (номер в Clusters)
layout(buffer_reference, scalar) buffer DrawCmds { DrawCmd v[]; };
// Счёт отсечения: число вызовов (его читает vkCmdDrawIndexedIndirectCount), на экземпляр — видимые кластеры
// и LOD (count | lod << 29) и начало его кластеров в списке кадра; veil_count — вызовы морока (места с vis_max)
// Скиннинг людей (src/render/vk_skin.cpp, skin.comp): работа на человека, палитры костей (3 строки vec4 на
// кость), запись общих вершин и кластеров
struct SkinJob {
    uint src_vertex, dst_vertex, vertex_count, first_bone;
    Words skin, morphs;  // 2 слова на вершину (кости, веса); 3 слова на вершину на морф
    uint morph_count, first_cluster, cluster_count, first_index;
    uint hide, pad_hide; // маска областей тела под одеждой (docs/wear.md)
    float morph[48];     // веса морфов (RENDER_MAX_MORPHS)
    float step[48];      // шаг морфа, м на единицу сдвига
};
layout(buffer_reference, scalar) readonly buffer SkinJobs { SkinJob v[]; };
layout(buffer_reference, scalar) readonly buffer Bones { vec4 v[]; };
layout(buffer_reference, scalar) buffer VerticesW { Vertex v[]; };
layout(buffer_reference, scalar) buffer ClustersW { Cluster v[]; };
// Сглаживание (docs/aa.md): прошлые положения вершин людей (xyz половинками, пишет skin.comp перед новым) и прошлый
// экземпляр динамической сетки — как Instance без сетки и материала; frame — кадр, для которого он прошлый
layout(buffer_reference, scalar) buffer SkinPrev { uvec2 v[]; };
struct MeshPrev { int cx, cz; float x, y, z; uint rot_xy, rot_zw; float scale; uint frame, pad0, pad1, pad2; };
layout(buffer_reference, scalar) readonly buffer MeshPrevs { MeshPrev v[]; };
layout(buffer_reference, scalar) buffer CullData { uint draw_count, veil_count, pad1, pad2; uint count[65536]; uint offset[65536]; };

// Не readonly: через него compute-шейдеры неба пишут свои таблицы (sky_*), сам буфер кадра никто не пишет
layout(buffer_reference, scalar) buffer FrameData {
    Vertices vertices;
    Materials materials;
    Lines lines;
    Points points;
    Words t_height;     // сетка высот земли, u16 парами (docs/terrain.md)
    Words t_mask;       // маски 32 м, u32: шероховатость, песок, трава, сырость по байту
    Words t_water;      // уровень воды 32 м, u16 парами, 5 см; 0 — воды нет
    Words t_mask2;      // маски 32 м, u32: сила дюн, пустыня по байту
    Words t_dune;       // дюны в узлах сетки высот, u16 парами, 1/640 м
    Words t_fine;       // карты дюн 2 м локаций пустынь, u16 парами, 1/640 м (лидар, симуляция ветра)
    ivec2 cam_chunk;
    vec3 cam_off;       // камера внутри своего чанка: x, z в [0, 256), y — высота
    float tan_half_fov;
    vec4 right, up, forward;
    float proj_x, proj_y, near_m, pixel_angle;
    float cell_min, cell_pixels;
    uint pixel, scans;
    vec4 sun_dir, sun_color;  // временный свет: главный источник (солнце или луна) и его цвет у камеры
    vec2 viewport, ndc_per_pixel;
    // Земля: как Terrain в src/sim/terrain.h
    int t_chunk0, t_size_x, t_size_z, t_nx, t_nz, t_nmx, t_nmz;
    uint t_dune_seed;
    int t_fine_off[4];    // карты дюн 2 м: начало локации в t_fine (номера u16), −1 — нет
    int t_fine_n, t_loc_m;
    int t_oct_lp[5];
    int t_oct_amp[5];
    uint t_oct_seed[5];
    uint t_on;
    // Clipmap: кольцо L — сетка (2n+1)² вершин с шагом 0.5·2^L м, начало в полуметрах от угла карты
    int clip_n, clip_levels, clip_morph, clip_pad;
    ivec4 clip[12];
    uint ground_layers[8];  // материалы слоёв: песок, сухая земля, щебень, камень, трава, вода, песок и камень пустыни
    int t_mip[8];         // начало уровня k сглаженной сетки высот (шаг 8·2^k м) в буфере высот, номера u16
    // Небо и дымка (docs/sky.md): таблицы неба и параметры атмосферы (SkyGpu в src/render/vk_state.h)
    Vec4s sky_trans, sky_ms, sky_sun, sky_moon, sky_out;
    vec4 a_sun;        // xyz — к солнцу, w — освещённость от солнца над атмосферой
    vec4 a_moon;       // xyz — к луне, w — освещённость от луны над атмосферой
    vec4 a_sun_t;      // rgb — пропускание атмосферы к солнцу от камеры, w — угловой радиус солнца
    vec4 a_moon_t;     // rgb — пропускание к луне, w — угловой радиус луны
    vec4 a_rayleigh;   // rgb — рассеяние 1/м на уровне моря, w — высота слоя, м
    vec4 a_mie;        // rgb — рассеяние пыли высокого слоя, w — высота слоя
    vec4 a_mie_abs;    // rgb — поглощение пыли, w — g фазовой функции (вперёд)
    vec4 a_haze;       // rgb — рассеяние нижней дымки, w — высота слоя
    vec4 a_haze_abs;   // rgb — поглощение дымки, w — высота нуля карты над уровнем моря, м
    vec4 a_ozone;      // rgb — поглощение озона, w — альбедо луны
    vec4 a_ground;     // rgb — альбедо планеты, w — ночное свечение неба (доля освещённости солнца)
    vec4 a_star[3];    // строки поворота «вид → небесная сфера»; w у [0] — яркость звёзд
    vec4 a_post;       // x — поправка экспозиции, EV; y — степень привыкания глаза; z — EV100 «нормы»; w — синева ночи
    vec4 a_misc;       // x — множитель рассеянного света неба; y, z — время привыкания к темноте и к свету, с; w — ночь темнее, EV
    vec4 a_sun_halo;   // rgb — толщина узкого лепестка пыли к солнцу (ореол), w — видимый радиус диска солнца, рад
    vec4 a_moon_halo;  // то же к луне
    vec4 a_night;      // x — g узкого лепестка, y — время кадра, с, z — яркость Млечного пути, w — мерцание звёзд
    vec4 a_night2;     // x — степень привыкания глаза ниже света полной луны, y — во сколько раз свечение ночного неба светит на землю сильнее видимого, z — блеск (docs/glare.md)
    // Облака (docs/clouds.md): параметры из data/sky.txt
    vec4 cl_shape;     // x, y — низ и верх кучевых, м над уровнем моря; z — доля неба под ними; w — плотность, 1/м
    vec4 cl_wind;      // xy — сдвиг облаков ветром к этому кадру (x, z), м; z — время формы, с; w — яркость облаков
    vec4 cl_cirrus;    // x — высота перистых, м; y — доля неба под ними; zw — направление ветра (x, z)
    vec4 cl_light;     // xyz — к светилу облаков, w — его освещённость над атмосферой
    Decals decals;     // декали (docs/ostov.md), decal_count подряд
    uint decal_count, decal_pad;
    // Кадр мира (этап 5): сетки, кластеры, постоянные экземпляры на GPU, видимые кластеры, вызовы, индексы
    MeshInfos meshes;
    Clusters clusters;
    Instances vis_instances;  // постоянный список экземпляров (vk_cull.cpp), instance_count подряд
    VisClusters vis;          // видимые кластеры кадра: номер — в visibility buffer
    CullData cull;
    DrawCmds draws;
    Words indices;            // общий буфер индексов сеток
    Words g_indices;          // индексы колец земли
    uint instance_count, vis_max;
    uint g_first[12];         // начало индексов кольца L в g_indices в этом кадре (по сдвигу дыры)
    // Froxels (docs/frame.md «Froxels», froxel.glsl): огни кадра, списки огней и декалей по клеткам, среда клеток
    Lights lights;
    Froxels froxels;
    uint light_count, froxel_x, froxel_y, froxel_z;
    float froxel_near, froxel_far;
    uint froxel_on, froxel_heat;
    GroundVerts g_verts;      // вершины колец земли этого кадра: кольцо L — с L·(2n+1)², см. ground_bake.comp
    Stats stats;              // счётчики GPU кадра (STAT_*)
    Tiles tiles;              // плитки по видам (light_tiles.comp → light_cells.comp)
    uint tile_cap, tile_pad;  // мест в списке одного вида (плиток кадра)
    // Свет мест плитки для глаза следующего кадра (light_cells.comp → eye.comp), на плитку 8×8 строками:
    // xyz — цвет света мест (Y = 1), w — log2 света места против света у камеры того кадра; средние по пикселям
    Vec4s view_light;
    // Кеш поверхности земли (docs/gcache.md): окно страниц уровня в gc_table — gc_w × gc_w, уровни подряд
    GcTable gc_table;         // физическая страница + 1; 0 — нет; GC_REQ | кадр — заявка этого кадра
    GcPages gc_pages;
    GcTexels gc_texels;
    GcReq gc_req;
    GcCells gc_cells;         // на пиксель кадра, строками
    GcLight gc_light;         // отражённый свет текселей, рядом с gc_texels
    uint gc_w, gc_pages_n, gc_req_cap, gc_frame;  // кадр кеша: с 1, растёт на кадр мира
    int gc_tpc0;              // текселей уровня 0 на чанк (256 м / cell_min)
    uint gc_on;               // 0 — кеш выключен: вся земля на пиксель (F3, --nogcache)
    uint gc_lmode, gc_lperiod;  // свет в кеше: GC_LIGHT_ON, GC_LIGHT_ALL; страница пересчитывается раз в gc_lperiod кадров
    uint gc_lit_min;            // свет страницы, посчитанный раньше этого кадра кеша, устарел (снимок, резкая смена света)
    uint gc_lturn;              // круг пересчёта в этом кадре (≤ gc_lperiod: камера быстро летит — чаще)
    // Карта горизонта земли (docs/horizon.md): H_DIRS/2 слов на узел сетки 8 м
    HMap h_map;
    uint h_on;                // 0 — карты нет (земли нет или ещё не собрана): тени земли нет
    float h_max;              // высшая точка сетки 8 м, м (сборка: где луч может кончиться)
    // Скиннинг людей (vk_skin.cpp): работы кадра, палитры, общие вершины и кластеры для записи
    SkinJobs skin_jobs;
    Bones bones;
    VerticesW vertices_w;
    ClustersW clusters_w;
    uint skin_count, skin_pad;
    // Свет (docs/light.md): время кадра для привыкания глаза, сразу ли привыкать (первый кадр, --timelapse),
    // тонмаппинг (TONEMAP_*), качество света (LIGHT_Q_*)
    float dt;
    uint eye_snap, tonemap, light_q;
    // Местная экспозиция (docs/light.md): колено, стопы; наклон в свет; наклон в тень; w — включена (F3, --nolocal)
    vec4 local_tone;
    // Замер и цвет глаза (docs/light.md, «Экспозиция», «Баланс белого»): x — доля замера по свету мест в кадре
    // (0 — только у камеры); y — 1: свет мест плиток прошлого кадра (view_light) годен для замера; z — баланс
    // белого (доля привыкания к цвету света, 0 — выключен); w — время привыкания к цвету, с
    vec4 eye_meter;
    // Тени солнца страницами (docs/shadow.md, shadow.glsl): окна страниц уровней, страницы, тексели, заявки,
    // вызовы и список отрисовки страниц, сферы сброса кадра
    SmTable sm_table;
    SmPages sm_pages;
    SmTexels sm_texels;
    SmReq sm_req;
    SmDraws sm_draws;
    SmList sm_list;
    Vec4s sm_inval;
    uint sm_on, sm_frame, sm_pages_n, sm_req_cap;
    uint sm_draw_cap, sm_inval_n, sm_debug, sm_cur;  // sm_cur — набор нового шага светила
    uint sm_read, sm_bg, sm_build_cap, sm_pad;        // читаемые наборы (биты); набор в фоне + 1 (0 — нет); его страниц за кадр
    float sm_mix, sm_disc, sm_far, sm_t0;  // вес нового набора; тангенс углового радиуса светила; дальность, м; тексель уровня 0
    // Набор (шаг светила): оси r, u — плоскость карты, s — к светилу; u.w — высота камеры к светилу от якоря набора
    vec4 sm_r[2], sm_u[2], sm_s[2];
    ivec2 sm_base[24];        // окно места уровня (набор · SM_LEVELS + уровень): первая страница на решётке уровня
    vec2 sm_off[24];          // камера от начала окна по r, u, м
    // Поле расстояний мира (docs/sdf.md, sdf.glsl): засыпка кадра, кирпичи и их биты, общий буфер засыпки
    SdfJobs sdf_jobs;
    SdfBricks sdf_bricks;
    Words sdf_bits;
    SdfScratch sdf_scratch;
    ivec2 sdf_anchor;         // чанк якоря поля
    uint sdf_on, sdf_view;    // свет читает поле; режим F3 (SDF_VIEW_*)
    uint sdf_jobs_n, sdf_bricks_n, sdf_pad0, sdf_pad1;
    vec4 sdf_cam;             // камера от якоря, м; w — воксель уровня 0, м
    ivec4 sdf_lo[8], sdf_hi[8];  // рамка грязных вокселей уровня этого кадра
    // Зонды непрямого света (docs/gi.md, gi.glsl): таблица зондов, работы кадра (место | GI_FRESH), лучи работ,
    // место и состояние зонда работы (пишут лучи, переносит в таблицу смешение)
    GiProbes gi_probes;
    GiJobs gi_jobs;
    GiRays gi_rays;
    Vec4s gi_pos;
    ivec4 gi_lo[4];           // первый зонд окна уровня: мировой номер от якоря поля
    uint gi_on, gi_view, gi_rays_n, gi_pad0;  // зонды читаются; режим F3 (GI_VIEW_*); лучей на зонд
    float gi_hyst, gi_pad2, glow_gain, gi_pad3;  // доля старого при смешении; множитель свечения glow (0 — лампы выключены)
    // Ячейки света (docs/lcache.md, lcache.glsl): таблица, работы кадра; ступень — тангенс угла ячейки, дальность,
    // лучей на счёт, кадров между счётами видимой ячейки, пределы работ кадра
    LcKeys lc_keys;
    LcKeys4 lc_keys4;
    LcMetas lc_meta;
    LcShs lc_show;
    LcShs lc_filt;            // показанный свет, сглаженный по соседям (его читает свет)
    LcAccs lc_acc;
    LcJobs lc_jobs;
    uint lc_on, lc_frame, lc_rays_n, lc_period;  // ячейки есть; кадр таблицы (с 1); лучей на счёт; кадров между счётами
    uint lc_cap_new, lc_cap_ref, lc_snap, lc_ease;  // пределы работ кадра; 1 — снимок: все видимые полным кругом;
                                                    // 1 — быстрое светило: показ каждый счёт (как у зондов)
    float lc_angle, lc_far, lc_hyst, lc_pad1;       // тангенс угла ячейки; дальность, м; доля старого в конце круга
    vec4 t_hole[4];           // проёмы земли (docs/terrain.md): x0, z0, x1, z1 от камеры, м
    uint t_hole_n, t_hole_pad0, t_hole_pad1, t_hole_pad2;
    // Сглаживание (docs/aa.md, vk_aa.cpp): режим (AA_*), дрожание точки пикселя этого кадра (пиксели, ±0.5),
    // вид камеры прошлого кадра для векторов движения: оси, проекция, сдвиг камеры (сейчас − тогда, м)
    uint aa_mode, aa_frame, aa_hist, aa_pad;  // aa_hist: 1 — прошлый кадр годен как история
    vec2 jitter, prev_proj;
    vec4 prev_right, prev_up, prev_forward;
    vec4 prev_delta;
    SkinPrev skin_prev;       // прошлое положение вершин людей (на вершину общего буфера, половинками)
    MeshPrevs mesh_prev;      // прошлый экземпляр динамической сетки (по номеру сетки), годен при frame == aa_frame
    // Облака (docs/clouds.md, clouds.glsl, vk_clouds.cpp): карта купола — три слота по cl_size² текселей в два
    // слова; шум CLOUD_N³; карта тени CLOUD_SH_N²; постоянные слота (4 vec4 на слот)
    CloudWords cl_map;
    CloudWords cl_noise;
    CloudWords cl_shadow;
    Vec4s cl_info;
    uint cl_on, cl_size, cl_slot_a, cl_slot_b;  // в кадре показаны слоты a и b, перетекание cl_mix: 0 — a, 1 — b
    float cl_mix, cl_mean_t;                    // cl_mean_t — средняя тень облаков (за окном карты тени)
    uint cl_steps, cl_pad;                      // шагов луча сборки (по ступени света)
    vec4 cl_off_a, cl_off_b;  // перенос слота к этому кадру: сдвиг камеры минус снос ветром с его сборки, м
    vec4 cl_build;            // сборка: xz — камера в осях облаков (мир − снос ветром), y — её высота над морем, w — время формы
    vec4 cl_build_light;      // сборка: светило облаков (как cl_light)
    vec4 cl_cam;              // этот кадр: то же, что cl_build (тень на землю)
    // Блеск (docs/glare.md, glare.glsl, vk_glare.cpp): пирамида кадра, уровни вниз и вверх в одном буфере
    GlareTexels gr_buf;
    uint gr_on, gr_n, gr_pad0, gr_pad1;  // gr_n — уровней
    ivec4 gr_lv[8];           // уровень: ширина, высота, начало «вниз», начало «вверх» (в текселях)
    // Тени огней картами (docs/lamp_shadows.md, vk_lamp_shadow.cpp): грани кадра, их вызовы (LSM_DRAW_CAP на
    // грань), счётчики вызовов граней, список отрисовки (экземпляр, кластер)
    LsmJobs lsm_jobs;
    DrawCmds lsm_draws;
    LsmCounts lsm_counts;
    VisClusters lsm_list;
    uint lsm_on, lsm_jobs_n, lsm_pad0, lsm_pad1;
    // Воздух (docs/air.md, air.glsl, vk_air.cpp): коробки пыли залов, расстояния половины кадра, свет солнца в
    // воздухе и его размытие, дальний край плиток воздуха
    AirBoxes air_boxes;
    AirFloats air_depth;
    AirTexels air_ray, air_tmp;
    AirWords air_tmax;
    uint air_on, air_boxes_n, air_steps, air_w2;
    uint air_h2, air_nx, air_ny, air_pad0;
    float air_far, air_lnk, air_dust, air_dust_h;
    vec3 air_tint;
    float air_g;
    float air_iso, air_pad1, air_pad2, air_pad3;
    // Объектив (docs/lens.md, lens.glsl, vk_glare.cpp): солнце в кадре (glare.comp → [0]: x — видимая доля диска, yz —
    // середина диска в пикселях, w — отступ от края кадра, 0–1); что включено (биты LENS_*), номер кадра (зерно);
    // множитель долей отражения бликов, аберрация (доля полудиагонали), виньетка (доля закона cos⁴), зерно (доля шкалы)
    Vec4s lens_sun;
    uint lens_on, lens_frame, lens_pad0, lens_pad1;
    float lens_flare, lens_ca, lens_vignette, lens_grain;
};

layout(push_constant, scalar) uniform Push {
    FrameData frame;
    uint a, b;
} pc;

const float CHUNK_M = 256.0;
const uint TONEMAP_AGX = 0u, TONEMAP_ACES = 1u, TONEMAP_UE = 2u;
const uint LIGHT_Q_LOW = 0u, LIGHT_Q_MEDIUM = 1u, LIGHT_Q_HIGH = 2u, LIGHT_Q_ULTRA = 3u;
// Свет неба и экспозиция (sky_sum.comp, eye.comp → sky_out): [4] — x множитель экспозиции (на него умножен HDR
// кадра), y — освещённость открытой горизонтальной земли, лк, z — EV100 глаза (с привыканием), w — от неба, лк;
// [5] — x от светила, лк, y — EV100, к которому глаз привыкает, z — освещённость у глаза с заслонкой полем, лк,
// w — рассеянный свет у глаза против открытого неба (дымка ближних отрезков, light.glsl);
// [6] — баланс белого (eye.comp → post.glsl): xyz — множители конусов CAT02, w — доля привыкания к цвету (0 — нет);
// [7] — xyz — цвет света, к которому глаз привык (линейный sRGB, Y = 1), w — свет мест в кадре (замер), лк;
// [8] — xyz — свет открытой земли (небо и светило на горизонтальную площадку, лк, цветом), w — свет у камеры, лк;
// [9], [10] — свет мест в кадре (eye_view.comp → eye.comp): x — сумма логарифма с весами, y — сумма весов; цвет
const int SKY_OUT_EYE = 4, SKY_OUT_EYE2 = 5, SKY_OUT_WB = 6, SKY_OUT_WB2 = 7, SKY_OUT_VIEW = 8;
const int SKY_OUT_VSUM = 9, SKY_OUT_VSUM2 = 10;
// Начало отсчёта узоров земли и материалов (world_cell, t_split…): по умолчанию чанк камеры. Запекание кеша
// земли считает тексель от его собственного чанка (gcache.glsl), чтобы тексель зависел только от себя
#ifndef ORIGIN_CHUNK
#define ORIGIN_CHUNK pc.frame.cam_chunk
#endif

const uint AA_OFF = 0u, AA_FXAA = 1u, AA_TAA = 2u;

// Точка относительно камеры → клип. Обратная глубина до бесконечности: z = near, w = глубина вдоль взгляда
vec4 to_clip(vec3 rel) {
    FrameData f = pc.frame;
    float vx = dot(rel, f.right.xyz), vy = dot(rel, f.up.xyz), vz = dot(rel, f.forward.xyz);
    return vec4(vx * f.proj_x, -vy * f.proj_y, f.near_m, vz);
}
// То же с дрожанием TAA (docs/aa.md): visibility buffer. Отладочные отрезки и точки рисуются поверх уже
// сглаженного кадра — им дрожание не нужно (to_clip)
vec4 to_clip_jittered(vec3 rel) {
    vec4 c = to_clip(rel);
    c.xy += pc.frame.jitter * pc.frame.ndc_per_pixel * c.w;
    return c;
}

// Чанк + смещение → метры относительно камеры: сначала целая разность чанков, потом смещения
vec3 rel_to_camera(ivec2 chunk, vec3 off) {
    FrameData f = pc.frame;
    vec2 dc = vec2(chunk - f.cam_chunk) * CHUNK_M;
    return vec3(dc.x + (off.x - f.cam_off.x), off.y - f.cam_off.y, dc.y + (off.z - f.cam_off.z));
}

vec3 quat_rotate(vec4 q, vec3 v) {
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

vec4 inst_quat(Instance it) { return normalize(vec4(unpackSnorm2x16(it.rot_xy), unpackSnorm2x16(it.rot_zw))); }

// Номер в visibility buffer (docs/frame.md): 0 — пусто; сетка — (видимый кластер + 1) << 7 | треугольник
// кластера (≤ 124); земля — старший бит | кольцо << 20 | треугольник кольца
const uint VIS_GROUND = 0x80000000u;

// Октаэдр (два snorm16) → единичная нормаль
vec3 oct_decode(uint packed) {
    vec2 e = unpackSnorm2x16(packed);
    vec3 n = vec3(e.x, 1.0 - abs(e.x) - abs(e.y), e.y);
    if (n.y < 0.0) n.xz = (1.0 - abs(n.zx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0);
    return normalize(n);
}

// sRGB 8 бит → линейный цвет (картинка swapchain *_SRGB сама кодирует обратно)
vec4 unpack_srgb(uint rgba) {
    vec4 c = unpackUnorm4x8(rgba);
    vec3 lo = c.rgb / 12.92;
    vec3 hi = pow((c.rgb + 0.055) / 1.055, vec3(2.4));
    return vec4(mix(hi, lo, lessThanEqual(c.rgb, vec3(0.04045))), c.a);
}
#endif
