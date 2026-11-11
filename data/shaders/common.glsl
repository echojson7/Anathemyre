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
struct Material { uint kind, pad0, pad1, pad2; vec4 p0, p1, p2; };
struct LineVtx { float x, y, z; uint rgba; };
// Декаль — RenderDecal (src/render/render.h), 48 байт
struct Decal { vec3 c; float ux, uz; vec3 h; uint kind, material, seed, pad; };
// Огонь — RenderPointLight (src/render/render.h), 32 байта: шар относительно камеры, линейный цвет × сила
struct Light { vec3 c; float radius; vec3 color; uint pad; };
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
// Счётчики GPU кадра (docs/graph.md «Переполнения»): сколько не влезло; номера — как STAT_* в vk_state.h
layout(buffer_reference, scalar) buffer Stats { uint v[]; };
layout(buffer_reference, scalar) buffer HMap { uint v[]; };  // карта горизонта (horizon.glsl), пишет horizon.comp
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

// Плитки 8×8 кадра мира по видам (docs/tiles.md, light_tiles.comp): небо — пусто; земля — земля из кеша
// (docs/gcache.md) и, может быть, небо; сетки — сетки и, может быть, небо; смесь — земля и сетки; земля на
// пиксель — земля, которой нет в кеше (крутые склоны), и, может быть, небо. На вид — косвенный dispatch и список
const uint TILE_SKY = 0u, TILE_GROUND = 1u, TILE_MESH = 2u, TILE_MIX = 3u, TILE_GROUND_PX = 4u, TILE_KINDS = 5u;
struct TileCmd { uint x, y, z, pad; };  // VkDispatchIndirectCommand, шаг 16 байт
// Плитка в списке — x | y << 16 (номер плитки, не пикселя); список вида k — с k · tile_cap
layout(buffer_reference, scalar) buffer Tiles { TileCmd cmd[TILE_KINDS]; uint list[]; };

// Кеш поверхности земли (docs/gcache.md, gcache.glsl): страница — 8×8 текселей одного уровня ячейки
struct GcPage { uint key0, key1, used, pad; };  // ключ страницы (уровень << 24 | x, z), кадр последнего чтения
layout(buffer_reference, scalar) buffer GcTable { uint v[]; };  // окна страниц уровней
layout(buffer_reference, scalar) buffer GcPages { GcPage v[]; };
layout(buffer_reference, scalar) buffer GcTexels { uvec4 v[]; };  // тексель: два варианта по 8 байт
// Заявки кадра и свободные страницы: счёт заявок, «старых» и «недавних» свободных, dispatch запекания; дальше
// заявки (ключи), старые и недавние — по req_cap и pages_n мест
layout(buffer_reference, scalar) buffer GcReq { uint req_n, old_n, recent_n, pad; uvec4 bake; uint v[]; };
struct GcCell { uint kt; int tz; float depth; };  // ячейка земли пикселя: уровень << 24 | x, z; глубина
layout(buffer_reference, scalar) buffer GcCells { GcCell v[]; };

// Тени солнца страницами (docs/shadow.md, shadow.glsl): страница — SM_PAGE² текселей одного уровня; тексель —
// высота ближайшей к светилу заслонки (sm_enc), 0 — заслонки нет
struct SmPage { uint key0, key1, used, made; };  // ключ (уровень << 24 | x, z), кадр чтения, кадр отрисовки
layout(buffer_reference, scalar) buffer SmTable { uint v[]; };
layout(buffer_reference, scalar) buffer SmPages { SmPage v[]; };
layout(buffer_reference, scalar) buffer SmTexels { uint v[]; };
// Заявки и свободные страницы: счёт заявок, старых и недавних свободных, dispatch выдачи; на уровень — есть ли
// новые страницы и их биты по месту в окне; дальше заявки (ключи), старые и недавние
layout(buffer_reference, scalar) buffer SmReq { uint req_n, old_n, recent_n, pad; uvec4 alloc; uint fresh[16];
                                                 uint bits[16 * 32]; uint v[]; };
struct SmDrawCmd { uint index_count, instance_count, first_index; int vertex_offset; uint first_instance; };
layout(buffer_reference, scalar) buffer SmDraws { uint count, pad0, pad1, pad2; SmDrawCmd v[]; };
layout(buffer_reference, scalar) buffer SmList { uvec2 v[]; };  // экземпляр; кластер | уровень << 28

// Кадр мира (этап 5, docs/frame.md «Кадр мира»): таблицы сеток и кластеров (MeshGpu, ClusterGpu в
// src/render/vk_state.h), видимые кластеры кадра и косвенные вызовы, которые пишет отсечение (cull_*.comp)
struct MeshInfo { uint first_vertex, first_index, lod_count, flags; vec4 sphere; uvec4 lods[8]; };  // lods: первый кластер, число, ошибка (биты f32), 0
const uint MESH_DYNAMIC = 1u;  // MeshInfo.flags: вершины пишет скиннинг каждый кадр (в страницы теней не идёт)
struct Cluster { uint first_index, tri_count; vec4 sphere; uint pad0, pad1; };  // first_index — от начала индексов сетки
struct DrawCmd { uint index_count, instance_count, first_index; int vertex_offset; uint first_instance; };
layout(buffer_reference, scalar) readonly buffer MeshInfos { MeshInfo v[]; };
layout(buffer_reference, scalar) readonly buffer Clusters { Cluster v[]; };
layout(buffer_reference, scalar) buffer VisClusters { uvec2 v[]; };  // экземпляр, кластер (номер в Clusters)
layout(buffer_reference, scalar) buffer DrawCmds { DrawCmd v[]; };
// Счёт отсечения: число вызовов (его читает vkCmdDrawIndexedIndirectCount), на экземпляр — видимые кластеры
// и LOD (count | lod << 29) и начало его кластеров в списке кадра
// Скиннинг людей (src/render/vk_skin.cpp, skin.comp): работа на человека, палитры костей (3 строки vec4 на
// кость), запись общих вершин и кластеров
struct SkinJob {
    uint src_vertex, dst_vertex, vertex_count, first_bone;
    Words skin, morphs;  // 2 слова на вершину (кости, веса); 3 слова на вершину на морф
    uint morph_count, first_cluster, cluster_count, first_index;
    float morph[8];      // веса морфов
    float step[8];       // шаг морфа, м на единицу сдвига
};
layout(buffer_reference, scalar) readonly buffer SkinJobs { SkinJob v[]; };
layout(buffer_reference, scalar) readonly buffer Bones { vec4 v[]; };
layout(buffer_reference, scalar) buffer VerticesW { Vertex v[]; };
layout(buffer_reference, scalar) buffer ClustersW { Cluster v[]; };
layout(buffer_reference, scalar) buffer CullData { uint draw_count, pad0, pad1, pad2; uint count[65536]; uint offset[65536]; };

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
    uint pixel, pad1;
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
    vec4 a_misc;       // x — множитель рассеянного света неба; y, z — время привыкания к темноте и к свету, с
    Decals decals;     // декали (docs/ruin.md), decal_count подряд
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
    Vec4s media;              // туман и пыль на клетку — место для света пользователя, пока нули
    uint light_count, froxel_x, froxel_y, froxel_z;
    float froxel_near, froxel_far;
    uint froxel_on, froxel_heat;
    GroundVerts g_verts;      // вершины колец земли этого кадра: кольцо L — с L·(2n+1)², см. ground_bake.comp
    Stats stats;              // счётчики GPU кадра (STAT_*)
    Tiles tiles;              // плитки по видам (light_tiles.comp → light_cells.comp)
    uint tile_cap, tile_pad;  // мест в списке одного вида (плиток кадра)
    // Кеш поверхности земли (docs/gcache.md): окно страниц уровня в gc_table — gc_w × gc_w, уровни подряд
    GcTable gc_table;         // физическая страница + 1; 0 — нет; GC_REQ | кадр — заявка этого кадра
    GcPages gc_pages;
    GcTexels gc_texels;
    GcReq gc_req;
    GcCells gc_cells;         // на пиксель кадра, строками
    uint gc_w, gc_pages_n, gc_req_cap, gc_frame;  // кадр кеша: с 1, растёт на кадр мира
    int gc_tpc0;              // текселей уровня 0 на чанк (256 м / cell_min)
    uint gc_on, gc_pad0, gc_pad1;  // 0 — кеш выключен: вся земля на пиксель (F3, --nogcache)
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
    uint sm_draw_cap, sm_inval_n, sm_debug, sm_pad;
    vec4 sm_r, sm_u, sm_s;    // оси: r, u — плоскость карты, s — к светилу; r.w — тексель уровня 0, u.w — высота
                              // камеры к светилу от якоря, s.w — дальность теней, м
    ivec2 sm_base[16];        // окно уровня: первая страница на мировой решётке уровня
    vec2 sm_off[16];          // камера от начала окна уровня по r, u, м
};

layout(push_constant, scalar) uniform Push {
    FrameData frame;
    uint a, b;
} pc;

const float CHUNK_M = 256.0;
const uint TONEMAP_AGX = 0u, TONEMAP_ACES = 1u;
const uint LIGHT_Q_LOW = 0u, LIGHT_Q_MEDIUM = 1u, LIGHT_Q_HIGH = 2u, LIGHT_Q_ULTRA = 3u;
// Свет неба и экспозиция (sky_sum.comp → sky_out): [4] — x множитель экспозиции (на него умножен HDR кадра),
// y — освещённость горизонтальной земли, лк, z — EV100 глаза (с привыканием), w — от неба, лк; [5] — x от светила,
// лк, y — EV100, к которому глаз привыкает
const int SKY_OUT_EYE = 4, SKY_OUT_EYE2 = 5;
// Начало отсчёта узоров земли и материалов (world_cell, t_split…): по умолчанию чанк камеры. Запекание кеша
// земли считает тексель от его собственного чанка (gcache.glsl), чтобы тексель зависел только от себя
#ifndef ORIGIN_CHUNK
#define ORIGIN_CHUNK pc.frame.cam_chunk
#endif

// Точка относительно камеры → клип. Обратная глубина до бесконечности: z = near, w = глубина вдоль взгляда
vec4 to_clip(vec3 rel) {
    FrameData f = pc.frame;
    float vx = dot(rel, f.right.xyz), vy = dot(rel, f.up.xyz), vz = dot(rel, f.forward.xyz);
    return vec4(vx * f.proj_x, -vy * f.proj_y, f.near_m, vz);
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
