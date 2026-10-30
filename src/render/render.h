// Рендерер: сцена в полном разрешении окна -> атмосфера -> стилизация с пиксельностью по глубине
#ifndef ANA_RENDER_H
#define ANA_RENDER_H

#include "../core/arena.h"
#include "../core/math3d.h"
#include "mesh.h"
#include "rb.h"

#define RENDER_MAX_BONES 24
// Последний слот набора — не кость, а внешность экземпляра (scene.vert): числа 0..7 — веса морфов,
// 8..10 и 12..14 — оттенок кожи и волос минус 1. Нулевая матрица — сетка как есть
#define RENDER_LOOK_SLOT 23
#define RENDER_MAX_ITEMS 4096    // элементов отрисовки за кадр
#define RENDER_MAX_POSED 1024    // разных наборов костей за кадр (персонаж с оружием — один набор)
#define RENDER_PORTRAIT_PX 96     // наибольший портрет, пикселей
#define RENDER_PORTRAIT_SLOTS 10
#define RENDER_MAX_VIS_INST 131072  // видимых экземпляров (трава, деревья, камни) за проход
#define RENDER_MAX_LIGHTS 16        // точечных источников в кадре: ближайшие из видимых (common.glsl)

typedef struct GpuMesh {
    RbBuf vbo, ibo, skin_vbo;     // skin_vbo — веса до 4 костей, 0 у жёстких сеток
    RbBuf morph_buf;              // сдвиги морфов внешности (mesh.h), 0 — нет
    u32 morph_count;
    u32 index_count;
    V4  bound;                          // сфера всей сетки: центр xyz, радиус w
    V4  bone_bound[RENDER_MAX_BONES];   // сфера вершин каждой кости в позе привязки
    u32 bone_mask;                      // какие кости есть в сетке
    // Уровень детализации: если сфера на экране меньше lod_px пикселей, рисуется lod_next
    const struct GpuMesh *lod_next;
    f32 lod_px;
    // Для экземпляров: редеет с дальности fade[0] и пропадает к fade[1], м (0 — не редеет)
    f32  fade[2];
    bool two_sided;     // без отсечения задних граней (листья, трава)
    // В общем буфере (gpu_mesh_upload_pooled): буферы свои не заводятся, сетка лежит с первой вершины
    // pool_vbase и первого индекса pool_ibase, морфы — с pool_moff (в uint)
    bool pooled;
    u32  pool_vbase, pool_ibase, pool_moff, pool_vcount;
} GpuMesh;

// Экземпляр статической сетки (32 байта, InstRec в scene.vert): деревья, трава, камни
typedef struct RInst {
    f32 pos[3];
    f32 yaw;
    f32 scale, hscale;  // по ширине и по высоте
    f32 lean;           // наклон оси вверх, рад; в сторону локальной X после поворота
    u32 tint;           // RGB — множитель цвета (128 = 1.0), старший байт — порядок прореживания 0..255
} RInst;

// Группа экземпляров одной сетки, подряд в постоянном буфере. Рендер отсекает группу, затем каждый
// экземпляр (bulk — без проверки по одному, для травы), выбирает LOD и сводит одинаковые сетки в один вызов.
// У редеющих сеток экземпляры в группе идут по возрастанию порядка прореживания
typedef struct InstGroup {
    const GpuMesh *mesh;
    u32  first, count;
    V4   bound;         // сфера всей группы
    bool bulk, no_shadow;
} InstGroup;

// Участок: группы first..first+count подряд и общая сфера. Невидимый участок отбрасывается целиком,
// у целиком видимого группы и экземпляры не проверяются по одному
typedef struct InstCluster {
    V4  bound;
    u32 first, count;
} InstCluster;

// Что рисовать; bones == NULL для статики (вершины уже в мировых координатах).
// Видимость и уровень детализации рендер выбирает сам по границам сетки
typedef struct DrawItem {
    const GpuMesh *mesh;
    const M4      *bones;
    bool           no_shadow;  // не отбрасывает тень (рельеф, дальние мегаструктуры)
} DrawItem;

// Точечный свет без теней: факел, фонарь, прожектор (конусом)
typedef struct PointLight {
    V3  pos;
    f32 radius;     // дальше не светит, м
    V3  color;      // цвет с яркостью: на 1 м освещённость color / 2
    f32 cone;       // косинус половины угла конуса; -1 — во все стороны
    V3  dir;        // ось конуса
    f32 glow;       // свечение пыли вокруг источника, доля от color
} PointLight;

typedef struct FrameView {
    M4  view, proj;
    V3  cam_pos;
    V3  sun_dir;        // направление НА главный свет (солнце или луна)
    V3  sun_color;
    V3  sky_amb, ground_amb;  // рассеянный свет неба и отражённый от земли
    V3  haze, zenith;         // цвет дымки и неба в зените
    V3  shadow_center;  // центр области теней; к нему же сгущается рельеф (не дальше 40 м от глаза)
    const PointLight *lights;  // рендер сам берёт видимые, не больше RENDER_MAX_LIGHTS
    u32 light_count;
    f32 time;
    u32 frame;
} FrameView;

typedef struct RenderSettings {
    bool post;          // фильтр кадра вкл/выкл
    bool pixel;         // пиксельность (квадраты на поверхностях и блоки вблизи) вкл/выкл
    bool vrs;           // шейдинг блоками там, где фильтр сливает пиксели (если есть у видеокарты)
    bool noise_tex;     // шум материалов одной выборкой из 3D-текстуры вместо 8 хешей (по умолчанию, +14% к/с на RTX)
    bool ao_half;       // AO в половинном разрешении с апскейлом по глубине (сравнение)
    // Слои для разбора производительности (ЭТАП СЦЕНЫ в меню НАСТРОЙКИ): выключенный проход не идёт вовсе
    bool shadows;       // тени солнца: проход теней и выборка в сцене
    bool atmos;         // небо, туман, свечение у огня; без неё фильтр берёт цвет прямо из сцены, небо — цвет дымки
    bool rays;          // объёмные лучи солнца (только с тенями и атмосферой)
    bool ao;            // затенение углов (только с атмосферой)
    bool flora;         // экземпляры: деревья, кусты, трава, камни
    bool mdi;           // сетки из общего буфера (люди) — один вызов на проход вместо вызова на сетку
    bool skin_cache;    // поза и морфы людей раз за кадр вычислительным проходом, проходы берут готовое
    // Группа СВЕТ (docs/lighting.md): у каждой части несколько способов, нули — прежний свет
    u8   light_mat;     // LIGHT_MAT_*: модель отражения поверхностей
    u8   light_curve;   // LIGHT_CURVE_*: тонмаппинг
    bool light_expo;    // автоэкспозиция: глаз привыкает к темноте и к яркому свету
    bool light_bloom;   // слабое свечение ярких мест, как в объективе
} RenderSettings;

enum { LIGHT_MAT_OLD, LIGHT_MAT_GGX, LIGHT_MAT_COUNT };
enum { LIGHT_CURVE_ACES, LIGHT_CURVE_AGX, LIGHT_CURVE_NEUTRAL, LIGHT_CURVE_COUNT };

// Время проходов на GPU, мс, среднее с прошлого render_take_stats
// глубина — проход глубины и карты пикселей; интерфейс — портреты и HUD после render_frame
enum { PASS_SHADOW, PASS_DEPTH, PASS_SCENE, PASS_ATMOS, PASS_STYLE, PASS_UI, PASS_COUNT };
typedef struct RenderStats {
    f64 gpu_ms[PASS_COUNT];
    f64 gpu_total_ms;
    f64 gap_ms;         // GPU между соседними кадрами: простой или работа вне проходов (-1 — не замерено)
    u32 samples;
    // Среднее за кадр: вызовы отрисовки и треугольники в проходах теней и сцены, видимые элементы
    f64 draws[2], tris[2], visible[2];
    u32 items;          // элементов в последнем кадре
    f64 prep_ms;        // CPU: границы, отсечение, LOD и раскладка по вызовам до первого вызова GL, мс за кадр
    f64 cpu_ms[PASS_COUNT];  // CPU: вызовы рендера каждого прохода после сбора, мс за кадр (интерфейс — портреты и HUD)
    f64 poses;          // наборов костей за кадр
    f64 skin_verts;     // вершин в кеше поз за кадр (0 — кеш выключен)
    bool vrs;           // последний кадр шейдился блоками вблизи
} RenderStats;

// Источник рельефа (render/terrain.c): заполнить w×h узлов с шагом step от (x0, z0), строками по Z —
// высоту и цвет земли RGBA8 (альбедо, в альфе вид: 0 — песок, 1 — земля долины). Зовётся из render_frame,
// когда кольца рельефа сдвигаются за камерой
typedef void (*TerrainFill)(f32 x0, f32 z0, f32 step, u32 w, u32 h, f32 *height, u32 *color, void *user);

bool render_init(Arena *scratch, const char *data_dir);
// Рельеф под всей сценой; NULL — без рельефа
void render_set_terrain(TerrainFill fill, void *user);
void render_take_stats(RenderStats *out);
void render_shutdown(void);
bool render_reload_shaders(void);
bool gpu_mesh_upload(GpuMesh *gm, const MeshBuilder *mb);
// То же в общий буфер сеток: такие сетки с костями рисуются одним вызовом на проход (люди, их вещи, оружие).
// Если буфер полон — обычная загрузка
bool gpu_mesh_upload_pooled(GpuMesh *gm, const MeshBuilder *mb);
// Занятость общего буфера в журнал
void render_pool_log(void);
// Сделать coarse следующим уровнем детализации fine: fine рисуется, пока сфера не меньше min_px пикселей
void gpu_mesh_set_lod(GpuMesh *fine, const GpuMesh *coarse, f32 min_px);
// Постоянный буфер экземпляров (заменяет прежний). Массив inst должен жить, пока он нужен рендеру:
// по нему отсекаются экземпляры
bool render_upload_instances(const RInst *inst, u32 count);
// Группы экземпляров по участкам для следующих кадров (NULL — нет); массивы должны жить, пока используются
void render_set_groups(const InstGroup *groups, const InstCluster *clusters, u32 cluster_count);
void render_frame(const FrameView *fv, const DrawItem *items, u32 count, const RenderSettings *rs, i32 win_w, i32 win_h);
// Конец кадра GPU: после портретов и интерфейса, перед swap
void render_frame_end(void);
// Портрет в ячейку slot атласа (size x size от левого нижнего угла ячейки), после render_frame:
// тот же свет и тени, фон прозрачный. cam_pos во fv — глаз портретной камеры
void render_portrait(u32 slot, i32 size, const FrameView *fv, M4 view_proj, const DrawItem *items, u32 count);
RbTex render_portrait_texture(void);  // HDR, ячейки в ряд, RENDER_PORTRAIT_PX
// RGB, снизу вверх, размер окна
void render_read_pixels(u8 *rgb, i32 w, i32 h);

#endif
