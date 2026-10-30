#include "render.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/clock.h"
#include "../core/log.h"
#include "rb.h"
#include "terrain.h"

#define SHADOW_SIZE 2048
#define SHADOW_EXTENT 45.0f   // полуразмер области теней, м
// Размер пикселя по глубине (pixel.glsl): k = PIXEL_K * s^PIXEL_P, s — пикселей кадра на метр в точке.
// Шаг 0.5 пикселя, ниже 2 — 0.25; ближе ~6 м рост вдвое медленнее, ближе ~5 м ещё медленнее (pixel.glsl PIXEL_KNEE, PIXEL_KNEE2).
// При 1080p: 1 м — 4, 1.5 м — 3, 2–3.4 м — 2.5, 3.5–6.4 м — 2, 7 м — 1.75, 8 м — 1.5, с 9 м — 1.25 и дальше не меняется
#define PIXEL_K 0.00728f
#define PIXEL_P 1.1f

// Привязки шейдеров (layout(binding = N)): номера сквозные для всех типов, как в наборе Vulkan
enum {
    B_LIGHTS = 0, B_FRAME = 1,                                          // UBO
    B_BONES = 2, B_INST = 3, B_MORPH = 4, B_STATIC_INST = 5,            // SSBO
    B_SHADOW = 6, B_DEPTH = 7, B_COLOR = 8, B_RAYS = 9, B_AO = 10,      // текстуры
    B_SRC_MAP = 11, B_LEVEL = 12, B_LEVEL_MAX = 13, B_BLOCK = 14, B_NOISE = 15,
};
#define SKIN_STATIC 0
#define SKIN_BONES 1
#define SKIN_INSTANCED 2
#define SKIN_POOL 4   // кости, сетка из общего буфера: набор костей и морфы — из записи экземпляра
// Разных сеток среди экземпляров прохода (с учётом уровней детализации)
#define MAX_INST_MESHES 256
// Участки экземпляров (деревья, трава, камни) дальше этого от глаза не рисуются: их не видно в дымке
#define INST_FAR 1200.0f

// Кости и списки экземпляров пишутся каждый кадр; кольцо буферов, чтобы не ждать GPU с прошлым кадром
#define STREAM_FRAMES RB_FRAMES
#define NO_POSE 0xFFFFFFFFu

// Общий буфер сеток (gpu_mesh_upload_pooled): люди, их вещи и причёски всех уровней детализации (~450 тыс. вершин,
// ~1.8 млн индексов, ~41 МБ морфов), оружие, факелы и метки
#define POOL_VERTS (640u * 1024u)
#define POOL_INDICES (2560u * 1024u)
#define POOL_MORPH (12u * 1024u * 1024u)   // uint
// Записей экземпляров и команд за кадр: тени, глубина, сцена и портреты
#define POOL_RECS (4u * RENDER_MAX_ITEMS)

// Запись экземпляра (атрибут a_rec в scene.vert): набор костей, морфов у сетки, её первая вершина и начало морфов
typedef struct PoolRec {
    u32 pose, morphs, vbase, moff;
} PoolRec;

// Команда косвенного вызова (rb_draw_indexed_indirect)
typedef struct PoolCmd {
    u32 count, instances, first_index;
    i32 base_vertex;
    u32 base_instance;
} PoolCmd;

// Экземпляр в проходе: сетка выбранного уровня детализации и набор костей
typedef struct Inst {
    const GpuMesh *mesh;
    u32            pose;   // NO_POSE — статика
} Inst;

// Вызов для экземпляров одной сетки: номера экземпляров лежат в потоке с base
typedef struct InstDraw {
    const GpuMesh *mesh;
    u32            base, count;
} InstDraw;

typedef struct Frustum {
    V4 p[6];   // плоскости: нормаль внутрь xyz, смещение w
} Frustum;

// Проход отрисовки: что видно и в каком размере на экране
typedef struct PassView {
    Frustum fr;
    V3      eye, fwd;   // для теней fwd нулевой: размер не зависит от дальности
    f32     px_k;       // пикселей на метр на единичной дальности (или просто на метр для теней)
    f32     inst_lod_k; // множитель размера экземпляров при выборе LOD (тени — грубее)
    bool    full;       // всегда полный уровень детализации (портреты)
} PassView;

// Метки времени GPU: кольцо из нескольких кадров, чтобы чтение не ждало GPU
#define TIMER_FRAMES 4
#define TIMER_MARKS (PASS_COUNT + 1)

// Константы кадра в раскладке std140 (frame.glsl, блок Frame)
typedef struct GpuFrame {
    f32 light_vp[16];
    f32 inv_vp[16];
    f32 cam_pos[3], time;
    f32 sun_dir[3], texel_on;
    f32 sun_color[3], proj_scale;
    f32 sky_amb[3];
    i32 frame;
    f32 ground_amb[3];
    i32 noise_tex;
    f32 haze[3];
    i32 shadow_on;
    f32 zenith[3];
    i32 unlit;
    f32 pix[4];
    i32 post, ao_half, parts, pad;
    i32 tile[4];
} GpuFrame;
_Static_assert(sizeof(GpuFrame) == 288, "GpuFrame — как блок Frame в frame.glsl");
// Блоков кадра в кольце на кадр: сам кадр и портреты (у каждого своя камера)
#define FRAME_BLOCKS (1 + RENDER_PORTRAIT_SLOTS)

// Константы вызова сцены и теней (frame.glsl: u_push)
typedef struct ScenePush {
    f32 view_proj[16];
    i32 skinned, inst_base, morphs, pad0;
    f32 fade[2], pad1[2];
    i32 terr_grid[4];
    f32 terr_view[4];
} ScenePush;
_Static_assert(sizeof(ScenePush) == RB_PUSH_BYTES, "ScenePush — весь u_push");
#define PUSH_DRAW 64   // смещение skinned, inst_base, morphs
#define PUSH_FADE 80

// Цель: цвет и глубина (по желанию) одного размера
typedef struct Target {
    RbTex color, depth;
    i32   w, h;
} Target;

// Раскладки вершин сеток: обычная (вершины и веса костей), общий буфер (и запись экземпляра), без вершин (рельеф)
enum { LAY_MESH, LAY_POOL, LAY_NONE, LAYS };
// Наборы конвейеров сеток по проходам
enum { PS_SHADOW, PS_DEPTH, PS_SCENE, PS_PORTRAIT, PSETS };

static struct {
    Arena *scratch;
    // Конвейеры сеток: [проход][раскладка][0 — с отсечением задних граней, 1 — без]
    RbPipe mesh_pipe[PSETS][LAYS][2];
    RbPipe pipe_rays, pipe_atmos, pipe_ao, pipe_style, pipe_src_map, pipe_vrs_block, pipe_vrs, pipe_level, pipe_level_max;
    u32    pset;      // набор текущего прохода
    RbTex  noise_tex; // значения hash13 в узлах решётки NOISE_N³: noise3 одной выборкой (common.glsl)
    RbTex  shadow_tex;
    Target scene, rays, atmos;
    Target ao;        // AO в половинном разрешении (RG16F), ao.frag
    Target level;     // размер пикселя каждой точки (R8UI), level.frag
    Target level_max; // самый крупный размер в блоке 8x8 (R8UI), level_max.frag
    Target src_map;   // откуда каждый пиксель берёт цвет (RG16UI), src_map.frag
    Target block;     // частота шейдинга блоков 4x4 (R8UI)
    Target rate;      // карта частоты шейдинга (R8UI), клетка vrs_tile пикселей
    bool   vrs;       // есть карта частоты шейдинга
    i32    vrs_tile[2];
    Target portrait;  // атлас портретов: ячейки RENDER_PORTRAIT_PX в ряд, HDR
    bool   timer_pending[TIMER_FRAMES];
    u32    timer_slot;
    bool   timing;    // текущий кадр пишет метки времени
    u64    timer_frame[TIMER_FRAMES]; // номер кадра в слоте
    u64    frame_no;
    u64    last_frame, last_end;      // последний прочитанный кадр и его конец на GPU
    f64    stat_gap;
    u32    stat_gaps;
    f64    stat_ms[PASS_COUNT];
    u32    stat_samples;
    RbBuf  bone_buf[STREAM_FRAMES], inst_buf[STREAM_FRAMES];
    RbBuf  light_buf[STREAM_FRAMES];
    RbBuf  frame_buf;                // блоки кадра: STREAM_FRAMES × FRAME_BLOCKS по frame_step
    u32    frame_step;
    GpuFrame frame;                  // константы последнего кадра
    u32    stream;
    u32    used_poses, used_insts;   // занято в буферах текущего кадра (кадр, затем портреты)
    f64    stat_draws[2], stat_tris[2], stat_visible[2], stat_prep, stat_poses;
    bool   stat_vrs;
    u32    stat_frames, stat_items;
    bool   warned_items, warned_poses, warned_inst;
    RbBuf  static_inst_buf;          // постоянный буфер RInst
    const RInst *inst_cpu;           // его копия для отсечения
    u32    inst_total;
    const InstGroup *groups;
    const InstCluster *clusters;
    u32    cluster_count;
    V3     cam_eye;                  // для прореживания по дальности во всех проходах
    // Общий буфер сеток и записи экземпляров с командами (кольцо кадров по POOL_RECS)
    RbBuf  pool_vbo, pool_skin, pool_ibo, pool_morph, pool_rec, pool_cmd;
    bool   pool_on;
    u32    pool_verts, pool_indices;
    size_t pool_words;
    u32    used_recs, used_cmds;     // занято в кольце текущего кадра
    bool   warned_pool, warned_recs;
    bool   mdi;
    f64    cpu_t[TIMER_MARKS];       // CPU в метках проходов
    f64    stat_cpu[PASS_COUNT];
} R;

// Данные кадра на CPU: размер постоянный, выделяются один раз
static V4   g_sphere[RENDER_MAX_ITEMS];
static u32  g_pose[RENDER_MAX_ITEMS];
static Inst g_inst[2][RENDER_MAX_ITEMS];
static u32  g_inst_pose[2 * RENDER_MAX_ITEMS + 2 * RENDER_MAX_VIS_INST];
static InstDraw g_idraw[2][MAX_INST_MESHES];
static u32  g_vis_id[RENDER_MAX_VIS_INST];
static u16  g_vis_bucket[RENDER_MAX_VIS_INST];
static M4   g_bones[RENDER_MAX_POSED * RENDER_MAX_BONES];

// Свет кадра в раскладке std140 (common.glsl): число, затем три массива vec4
typedef struct GpuLights {
    i32 count[4];
    f32 pos[RENDER_MAX_LIGHTS][4];   // xyz, w — радиус
    f32 col[RENDER_MAX_LIGHTS][4];   // rgb, a — косинус конуса
    f32 dir[RENDER_MAX_LIGHTS][4];   // xyz — ось конуса, w — свечение пыли
} GpuLights;
static GpuLights g_lights;

// Раскладки вершин: Vertex и VertexSkin (mesh.h), запись экземпляра PoolRec
static const RbLayout g_layout[LAYS] = {
    [LAY_MESH] = {
        .attr = { { 0, 0, RB_ATTR_F32x3, offsetof(Vertex, pos) }, { 1, 0, RB_ATTR_F32x3, offsetof(Vertex, nrm) },
                  { 2, 0, RB_ATTR_UN8x4, offsetof(Vertex, color) }, { 3, 0, RB_ATTR_U8x4, offsetof(Vertex, mat) },
                  { 4, 1, RB_ATTR_U8x4, offsetof(VertexSkin, bone) }, { 5, 1, RB_ATTR_UN8x4, offsetof(VertexSkin, weight) } },
        .attrs = 6, .stride = { sizeof(Vertex), sizeof(VertexSkin) },
    },
    [LAY_POOL] = {
        .attr = { { 0, 0, RB_ATTR_F32x3, offsetof(Vertex, pos) }, { 1, 0, RB_ATTR_F32x3, offsetof(Vertex, nrm) },
                  { 2, 0, RB_ATTR_UN8x4, offsetof(Vertex, color) }, { 3, 0, RB_ATTR_U8x4, offsetof(Vertex, mat) },
                  { 4, 1, RB_ATTR_U8x4, offsetof(VertexSkin, bone) }, { 5, 1, RB_ATTR_UN8x4, offsetof(VertexSkin, weight) },
                  { 6, 2, RB_ATTR_U32x4, 0 } },
        .attrs = 7, .stride = { sizeof(Vertex), sizeof(VertexSkin), sizeof(PoolRec) }, .per_instance = { false, false, true },
    },
};

// vrs — рисуется по карте частоты шейдинга (атмосфера)
static RbPipe pipe_full(const char *name, const char *const *fs, RbFmt fmt, const char *defines, bool vrs)
{
    RbPipeDesc d = { .vs = { "fullscreen.vert" }, .name = name, .defines = defines, .color = { fmt }, .vrs = vrs };
    memcpy(d.fs, fs, sizeof d.fs);
    return rb_pipe_make(&d);
}

// Конвейер сетки: проход задаёт шейдер и состояние, раскладка — вершины
static RbPipe pipe_mesh(u32 set, u32 lay, bool two_sided)
{
    RbPipeDesc d = {
        .vs = { "frame.glsl", "scene.vert" }, .name = "сетка", .layout = g_layout[lay],
        .cull = two_sided ? RB_CULL_NONE : RB_CULL_BACK,
        .depth = set == PS_SCENE ? RB_DEPTH_LEQUAL : RB_DEPTH_LESS, .depth_write = set != PS_SCENE,
        .color = { RB_FMT_RGBA16F }, .depth_fmt = RB_FMT_D32F,
    };
    if (set == PS_SHADOW || set == PS_DEPTH) {
        d.fs[0] = "shadow.frag";
        d.defines = "#define SHADOW_PASS\n";
    } else {
        d.fs[0] = "frame.glsl";
        d.fs[1] = "common.glsl";
        d.fs[2] = "scene.frag";
        // Портрет — та же программа, только без карты частоты: размер блока там 1
        d.frag_size = R.vrs;
        d.vrs = R.vrs && set == PS_SCENE;
    }
    if (set == PS_SHADOW) {
        // Что ближе к солнцу, чем ближняя плоскость, всё равно отбрасывает тень (а не режется срезом)
        d.depth_clamp = true;
        d.bias_slope = 1.5f;
        d.bias_const = 2.0f;
        d.color[0] = RB_FMT_NONE;
        d.depth_fmt = RB_FMT_D24;
    }
    d.no_color = set == PS_DEPTH;
    return rb_pipe_make(&d);
}

static void pipes_free(void)
{
    for (u32 s = 0; s < PSETS; s++)
        for (u32 l = 0; l < LAYS; l++)
            for (u32 c = 0; c < 2; c++) rb_pipe_free(&R.mesh_pipe[s][l][c]);
    RbPipe *full[] = { &R.pipe_rays, &R.pipe_atmos, &R.pipe_ao, &R.pipe_style, &R.pipe_src_map,
                       &R.pipe_vrs_block, &R.pipe_vrs, &R.pipe_level, &R.pipe_level_max };
    for (u32 i = 0; i < ARRAY_COUNT(full); i++) rb_pipe_free(full[i]);
}

// Все конвейеры кадра; false — что-то не собралось (и ничего не собрано)
static bool pipes_make(void)
{
    for (u32 s = 0; s < PSETS; s++)
        for (u32 l = 0; l < LAYS; l++) {
            // Без отсечения задних граней рисуются только экземпляры (листья, трава) и не в портрете
            u32 variants = l == LAY_MESH && s != PS_PORTRAIT ? 2 : 1;
            if (s == PS_PORTRAIT && l == LAY_NONE) continue;
            for (u32 c = 0; c < variants; c++)
                if (!(R.mesh_pipe[s][l][c] = pipe_mesh(s, l, c == 1))) {
                    pipes_free();
                    return false;
                }
        }
    const char *rays_fs[] = { "frame.glsl", "common.glsl", "rays.frag" };
    const char *atmos_fs[] = { "frame.glsl", "common.glsl", "ao.glsl", "atmos.frag" };
    const char *ao_fs[] = { "frame.glsl", "common.glsl", "ao.glsl", "ao.frag" };
    const char *style_fs[] = { "frame.glsl", "common.glsl", "pixel.glsl", "style.frag", NULL, NULL };
    const char *src_map_fs[] = { "frame.glsl", "pixel.glsl", "src_map.frag", NULL, NULL, NULL };
    const char *vrs_block_fs[] = { "vrs_block.frag", NULL, NULL, NULL, NULL, NULL };
    const char *level_fs[] = { "frame.glsl", "pixel.glsl", "level.frag", NULL, NULL, NULL };
    const char *level_max_fs[] = { "level_max.frag", NULL, NULL, NULL, NULL, NULL };
    const char *vrs_fs[] = { "frame.glsl", "vrs.frag", NULL, NULL, NULL, NULL };
    const char *rays6[RB_SHADER_FILES] = { 0 }, *atmos6[RB_SHADER_FILES] = { 0 }, *ao6[RB_SHADER_FILES] = { 0 };
    memcpy(rays6, rays_fs, sizeof rays_fs);
    memcpy(atmos6, atmos_fs, sizeof atmos_fs);
    memcpy(ao6, ao_fs, sizeof ao_fs);
    // Лучи всегда считали шум хешем: выбор текстурного шума до них не доходил
    R.pipe_rays = pipe_full("лучи", rays6, RB_FMT_RG16F, "#define NOISE_HASH\n", false);
    R.pipe_atmos = pipe_full("атмосфера", atmos6, RB_FMT_RGBA16F, NULL, R.vrs);
    R.pipe_ao = pipe_full("AO", ao6, RB_FMT_RG16F, NULL, false);
    R.pipe_style = pipe_full("фильтр", style_fs, RB_FMT_NONE, NULL, false);
    R.pipe_src_map = pipe_full("источники", src_map_fs, RB_FMT_RG16UI, NULL, false);
    R.pipe_vrs_block = pipe_full("блоки", vrs_block_fs, RB_FMT_R8UI, NULL, false);
    R.pipe_vrs = pipe_full("частота", vrs_fs, RB_FMT_R8UI, NULL, false);
    R.pipe_level = pipe_full("размер пикселя", level_fs, RB_FMT_R8UI, NULL, false);
    R.pipe_level_max = pipe_full("размер в блоке", level_max_fs, RB_FMT_R8UI, NULL, false);
    if (!R.pipe_rays || !R.pipe_atmos || !R.pipe_ao || !R.pipe_style || !R.pipe_src_map || !R.pipe_vrs_block ||
        !R.pipe_vrs || !R.pipe_level || !R.pipe_level_max) {
        pipes_free();
        return false;
    }
    return true;
}

bool render_reload_shaders(void)
{
    // Старые конвейеры живут, пока новые не собраны целиком
    RbPipe *full[] = { &R.pipe_rays, &R.pipe_atmos, &R.pipe_ao, &R.pipe_style, &R.pipe_src_map,
                       &R.pipe_vrs_block, &R.pipe_vrs, &R.pipe_level, &R.pipe_level_max };
    RbPipe old_mesh[PSETS][LAYS][2], old_full[ARRAY_COUNT(full)];
    memcpy(old_mesh, R.mesh_pipe, sizeof old_mesh);
    memset(R.mesh_pipe, 0, sizeof R.mesh_pipe);
    for (u32 i = 0; i < ARRAY_COUNT(full); i++) {
        old_full[i] = *full[i];
        *full[i] = 0;
    }
    rb_shaders_changed();
    bool ok = pipes_make();
    if (!ok && R.vrs) {
        // Драйвер заявил карту частоты, но шейдер с ней не собрался: считаем каждый пиксель
        log_error("РЕНДЕР: шейдинг блоками отключён, шейдер сцены без него");
        R.vrs = false;
        ok = pipes_make();
    }
    if (!ok) {
        memcpy(R.mesh_pipe, old_mesh, sizeof old_mesh);
        for (u32 i = 0; i < ARRAY_COUNT(full); i++) *full[i] = old_full[i];
        return false;
    }
    for (u32 s = 0; s < PSETS; s++)
        for (u32 l = 0; l < LAYS; l++)
            for (u32 c = 0; c < 2; c++) rb_pipe_free(&old_mesh[s][l][c]);
    for (u32 i = 0; i < ARRAY_COUNT(old_full); i++) rb_pipe_free(&old_full[i]);
    log_info("ШЕЙДЕРЫ: загружены");
    return true;
}

static void target_free(Target *t)
{
    rb_tex_free(&t->color);
    rb_tex_free(&t->depth);
    memset(t, 0, sizeof *t);
}

static bool target_make(Target *t, i32 w, i32 h, RbFmt color_fmt, bool depth)
{
    target_free(t);
    t->w = w;
    t->h = h;
    t->color = rb_tex_make(&(RbTexDesc){ .fmt = color_fmt, .w = w, .h = h, .usage = RB_TEX_TARGET | RB_TEX_SAMPLED });
    if (depth) t->depth = rb_tex_make(&(RbTexDesc){ .fmt = RB_FMT_D32F, .w = w, .h = h, .usage = RB_TEX_TARGET | RB_TEX_SAMPLED });
    return t->color && (!depth || t->depth);
}

// Проход в цель t
static void pass_to(const Target *t, bool clear, const f32 *color, RbTex rate)
{
    RbPass p = { .color = { { t->color } }, .depth = { t->depth }, .clear_color = clear, .clear_depth = clear && t->depth, .rate = rate };
    if (color) memcpy(p.clear, color, sizeof p.clear);
    rb_pass_begin(&p);
}

// Полноэкранный треугольник в цель t
static void draw_full(const Target *t, RbPipe pipe, RbTex rate)
{
    pass_to(t, false, NULL, rate);
    rb_bind_pipe(pipe);
    rb_draw(3, 0, 1);
    rb_pass_end();
}

// hash13 из common.glsl теми же операциями: узлы решётки для шума одной выборкой
#define NOISE_N 128
static f32 fract_f(f32 x) { return x - floorf(x); }
static f32 hash13_c(f32 x, f32 y, f32 z)
{
    x = fract_f(x * 0.1031f); y = fract_f(y * 0.1031f); z = fract_f(z * 0.1031f);
    f32 d = x * (z + 31.32f) + y * (y + 31.32f) + z * (x + 31.32f);
    x += d; y += d; z += d;
    return fract_f((x + y) * z);
}

static void noise_make(void)
{
    static u16 slice[NOISE_N * NOISE_N];
    R.noise_tex = rb_tex_make(&(RbTexDesc){ .kind = RB_TEX_3D, .fmt = RB_FMT_R16, .w = NOISE_N, .h = NOISE_N, .d = NOISE_N,
                                            .usage = RB_TEX_SAMPLED });
    for (u32 z = 0; z < NOISE_N; z++) {
        for (u32 y = 0; y < NOISE_N; y++)
            for (u32 x = 0; x < NOISE_N; x++)
                slice[y * NOISE_N + x] = (u16)(hash13_c((f32)x, (f32)y, (f32)z) * 65535.0f + 0.5f);
        rb_tex_write(R.noise_tex, 0, 0, 0, (i32)z, NOISE_N, NOISE_N, 1, slice, 0);
    }
    rb_bind_tex(B_NOISE, R.noise_tex, RB_SMP_LINEAR_REPEAT);  // привязка больше никем не занята
}

bool render_init(Arena *scratch, const char *data_dir)
{
    R.scratch = scratch;
    if (!rb_init(scratch, data_dir)) return false;
    // Частота шейдинга по блокам: вблизи фильтр всё равно сливает пиксели в блоки 2x2 и 4x4.
    // Клетка карты должна вмещать целые ячейки 4x4
    const RbCaps *caps = rb_caps();
    R.vrs = caps->vrs && caps->vrs_tile[0] >= 4 && caps->vrs_tile[1] >= 4 && caps->vrs_tile[0] % 4 == 0 &&
            caps->vrs_tile[1] % 4 == 0;
    if (R.vrs) memcpy(R.vrs_tile, caps->vrs_tile, sizeof R.vrs_tile);
    log_info("РЕНДЕР: частота шейдинга по блокам %s", R.vrs ? "есть" : "нет (каждый пиксель)");
    if (!render_reload_shaders() || !terrain_init(scratch)) return false;

    noise_make();
    for (u32 i = 0; i < STREAM_FRAMES; i++) {
        R.bone_buf[i] = rb_buf_make(sizeof g_bones, RB_BUF_STORAGE | RB_BUF_DYNAMIC, NULL);
        R.inst_buf[i] = rb_buf_make(sizeof g_inst_pose, RB_BUF_STORAGE | RB_BUF_DYNAMIC, NULL);
        R.light_buf[i] = rb_buf_make(sizeof g_lights, RB_BUF_UNIFORM | RB_BUF_DYNAMIC, NULL);
    }
    R.frame_step = (u32)((sizeof(GpuFrame) + caps->ubo_align - 1) / caps->ubo_align * caps->ubo_align);
    R.frame_buf = rb_buf_make((size_t)R.frame_step * FRAME_BLOCKS * STREAM_FRAMES, RB_BUF_UNIFORM | RB_BUF_DYNAMIC, NULL);
    if (!rb_timer_init(TIMER_FRAMES, TIMER_MARKS)) return false;

    R.shadow_tex = rb_tex_make(&(RbTexDesc){ .fmt = RB_FMT_D24, .w = SHADOW_SIZE, .h = SHADOW_SIZE,
                                             .usage = RB_TEX_TARGET | RB_TEX_SAMPLED });
    if (!R.shadow_tex || !R.frame_buf) return false;
    return target_make(&R.portrait, RENDER_PORTRAIT_PX * RENDER_PORTRAIT_SLOTS, RENDER_PORTRAIT_PX, RB_FMT_RGBA16F, true);
}

void render_set_terrain(TerrainFill fill, void *user) { terrain_source(fill, user); }

void render_shutdown(void)
{
    terrain_shutdown();
    Target *t[] = { &R.scene, &R.rays, &R.ao, &R.atmos, &R.src_map, &R.level, &R.level_max, &R.block, &R.rate, &R.portrait };
    for (u32 i = 0; i < ARRAY_COUNT(t); i++) target_free(t[i]);
    rb_tex_free(&R.noise_tex);
    rb_tex_free(&R.shadow_tex);
    for (u32 i = 0; i < STREAM_FRAMES; i++) {
        rb_buf_free(&R.bone_buf[i]);
        rb_buf_free(&R.inst_buf[i]);
        rb_buf_free(&R.light_buf[i]);
    }
    RbBuf *bufs[] = { &R.frame_buf, &R.static_inst_buf, &R.pool_vbo, &R.pool_skin, &R.pool_ibo, &R.pool_morph, &R.pool_rec, &R.pool_cmd };
    for (u32 i = 0; i < ARRAY_COUNT(bufs); i++) rb_buf_free(bufs[i]);
    pipes_free();
    rb_shutdown();
    memset(&R, 0, sizeof R);
}

// Кости вершины с ненулевым весом, номера как в scene.vert
static u32 vertex_bones(const MeshBuilder *mb, u32 i, u32 *out)
{
    if (!mb->skin) {
        out[0] = MIN((u32)mb->v[i].bone, RENDER_MAX_BONES - 1u);
        return 1;
    }
    u32 n = 0;
    for (u32 k = 0; k < 4; k++)
        if (mb->skin[i].weight[k]) out[n++] = MIN((u32)mb->skin[i].bone[k], RENDER_MAX_BONES - 1u);
    return n;
}

// Поля и границы сетки, общие для обычной загрузки и общего буфера
static void mesh_finish(GpuMesh *gm, const MeshBuilder *mb);

bool gpu_mesh_upload(GpuMesh *gm, const MeshBuilder *mb)
{
    gm->pooled = false;
    gm->vbo = rb_buf_make(mb->vcount * sizeof(Vertex), RB_BUF_VERTEX, mb->v);
    gm->ibo = rb_buf_make(mb->icount * sizeof(u32), RB_BUF_INDEX, mb->idx);
    // Без весов костей сетка рисуется с потоком нулей: вес первой кости 0 — жёсткий скиннинг
    gm->skin_vbo = mb->skin ? rb_buf_make(mb->vcount * sizeof(VertexSkin), RB_BUF_VERTEX, mb->skin) : 0;
    gm->morph_buf = 0;
    gm->morph_count = mb->morph ? mb->morph_count : 0;
    if (gm->morph_count)
        gm->morph_buf = rb_buf_make((size_t)mb->vcount * gm->morph_count * 3 * sizeof(u32), RB_BUF_STORAGE, mb->morph);
    mesh_finish(gm, mb);
    return gm->vbo && gm->ibo;
}

// Общий буфер заводится при первой сетке. Записи экземпляров — поток 2 по экземплярам:
// первый экземпляр команды указывает на первую запись её экземпляров
static bool pool_init(void)
{
    if (R.pool_on) return true;
    R.pool_vbo = rb_buf_make(POOL_VERTS * sizeof(Vertex), RB_BUF_VERTEX, NULL);
    R.pool_skin = rb_buf_make(POOL_VERTS * sizeof(VertexSkin), RB_BUF_VERTEX, NULL);
    R.pool_ibo = rb_buf_make(POOL_INDICES * sizeof(u32), RB_BUF_INDEX, NULL);
    R.pool_morph = rb_buf_make(POOL_MORPH * sizeof(u32), RB_BUF_STORAGE, NULL);
    R.pool_rec = rb_buf_make(STREAM_FRAMES * POOL_RECS * sizeof(PoolRec), RB_BUF_VERTEX | RB_BUF_DYNAMIC, NULL);
    R.pool_cmd = rb_buf_make(STREAM_FRAMES * POOL_RECS * sizeof(PoolCmd), RB_BUF_INDIRECT | RB_BUF_DYNAMIC, NULL);
    R.pool_on = R.pool_vbo && R.pool_skin && R.pool_ibo && R.pool_morph && R.pool_rec && R.pool_cmd;
    return R.pool_on;
}

bool gpu_mesh_upload_pooled(GpuMesh *gm, const MeshBuilder *mb)
{
    u32 morphs = mb->morph ? mb->morph_count : 0;
    size_t mwords = (size_t)mb->vcount * morphs * 3;
    if (!pool_init() || R.pool_verts + mb->vcount > POOL_VERTS || R.pool_indices + mb->icount > POOL_INDICES ||
        R.pool_words + mwords > POOL_MORPH) {
        if (!R.warned_pool) log_error("РЕНДЕР: общий буфер сеток полон, дальше сетки по отдельности");
        R.warned_pool = true;
        return gpu_mesh_upload(gm, mb);
    }
    memset(gm, 0, sizeof *gm);
    gm->pooled = true;
    gm->pool_vbase = R.pool_verts;
    gm->pool_ibase = R.pool_indices;
    gm->pool_moff = (u32)R.pool_words;
    rb_buf_write(R.pool_vbo, R.pool_verts * sizeof(Vertex), mb->vcount * sizeof(Vertex), mb->v);
    // Жёстким сеткам — нулевые веса: в шейдере это жёсткий скиннинг, как без потока весов
    static const VertexSkin zero[1024];
    for (u32 i = 0; i < mb->vcount; i += ARRAY_COUNT(zero)) {
        u32 k = MIN(mb->vcount - i, (u32)ARRAY_COUNT(zero));
        rb_buf_write(R.pool_skin, (R.pool_verts + i) * sizeof(VertexSkin), k * sizeof(VertexSkin), mb->skin ? mb->skin + i : zero);
    }
    rb_buf_write(R.pool_ibo, R.pool_indices * sizeof(u32), mb->icount * sizeof(u32), mb->idx);
    if (mwords) rb_buf_write(R.pool_morph, R.pool_words * sizeof(u32), mwords * sizeof(u32), mb->morph);
    R.pool_verts += mb->vcount;
    R.pool_indices += mb->icount;
    R.pool_words += mwords;
    gm->morph_count = morphs;
    mesh_finish(gm, mb);
    return true;
}

void render_pool_log(void)
{
    log_info("РЕНДЕР: общий буфер сеток: вершин %u из %u, индексов %u из %u, морфов %.1f из %.0f МБ", R.pool_verts, POOL_VERTS,
             R.pool_indices, POOL_INDICES, (f64)R.pool_words * 4.0 / 1048576.0, (f64)POOL_MORPH * 4.0 / 1048576.0);
}

static void mesh_finish(GpuMesh *gm, const MeshBuilder *mb)
{
    gm->index_count = mb->icount;
    gm->lod_next = NULL;
    gm->lod_px = 0.0f;
    gm->fade[0] = gm->fade[1] = 0.0f;
    gm->two_sided = false;

    // Границы: сфера вокруг рамки вершин, для всей сетки и для каждой кости.
    // Вершина с весами входит в сферы всех своих костей: смесь поз лежит внутри их объединения
    V3 lo[RENDER_MAX_BONES + 1], hi[RENDER_MAX_BONES + 1];
    for (u32 b = 0; b <= RENDER_MAX_BONES; b++) {
        lo[b] = v3(FLT_MAX, FLT_MAX, FLT_MAX);
        hi[b] = v3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    }
    gm->bone_mask = 0;
    for (u32 i = 0; i < mb->vcount; i++) {
        const Vertex *v = &mb->v[i];
        V3 p = v3(v->pos[0], v->pos[1], v->pos[2]);
        u32 slots[5];
        u32 ns = vertex_bones(mb, i, slots);
        slots[ns++] = RENDER_MAX_BONES;
        for (u32 k = 0; k < ns; k++) {
            u32 b = slots[k];
            lo[b] = v3(fminf(lo[b].x, p.x), fminf(lo[b].y, p.y), fminf(lo[b].z, p.z));
            hi[b] = v3(fmaxf(hi[b].x, p.x), fmaxf(hi[b].y, p.y), fmaxf(hi[b].z, p.z));
            if (b < RENDER_MAX_BONES) gm->bone_mask |= 1u << b;
        }
    }
    V4 sph[RENDER_MAX_BONES + 1];
    for (u32 b = 0; b <= RENDER_MAX_BONES; b++) {
        V3 c = lo[b].x <= hi[b].x ? v3_mul(v3_add(lo[b], hi[b]), 0.5f) : v3(0, 0, 0);
        sph[b] = (V4){ c.x, c.y, c.z, 0.0f };
    }
    for (u32 i = 0; i < mb->vcount; i++) {
        const Vertex *v = &mb->v[i];
        V3 p = v3(v->pos[0], v->pos[1], v->pos[2]);
        u32 slots[5];
        u32 ns = vertex_bones(mb, i, slots);
        slots[ns++] = RENDER_MAX_BONES;
        for (u32 k = 0; k < ns; k++) {
            V4 *s = &sph[slots[k]];
            s->w = fmaxf(s->w, v3_len(v3_sub(p, v3(s->x, s->y, s->z))));
        }
    }
    memcpy(gm->bone_bound, sph, sizeof gm->bone_bound);
    gm->bound = sph[RENDER_MAX_BONES];
}

void gpu_mesh_set_lod(GpuMesh *fine, const GpuMesh *coarse, f32 min_px)
{
    fine->lod_next = coarse;
    fine->lod_px = min_px;
}

_Static_assert(sizeof(RInst) == 32, "RInst — 32 байта, как InstRec в scene.vert");

bool render_upload_instances(const RInst *inst, u32 count)
{
    rb_buf_free(&R.static_inst_buf);
    R.inst_cpu = inst;
    R.inst_total = count;
    R.groups = NULL;
    R.clusters = NULL;
    R.cluster_count = 0;
    if (!count) return true;
    R.static_inst_buf = rb_buf_make(sizeof(RInst) * count, RB_BUF_STORAGE, inst);
    return R.static_inst_buf != 0;
}

void render_set_groups(const InstGroup *groups, const InstCluster *clusters, u32 cluster_count)
{
    R.groups = groups;
    R.clusters = clusters;
    R.cluster_count = groups && clusters && R.inst_cpu ? cluster_count : 0;
}


// Забирает результаты самого старого кадра в кольце, если GPU их уже посчитал
static void timer_collect(u32 slot)
{
    if (!R.timer_pending[slot] || !rb_timer_ready(slot, TIMER_MARKS - 1)) return;
    u64 t[TIMER_MARKS];
    for (u32 i = 0; i < TIMER_MARKS; i++) t[i] = rb_timer_get(slot, i);
    for (u32 p = 0; p < PASS_COUNT; p++) R.stat_ms[p] += (f64)(t[p + 1] - t[p]) * 1e-6;
    R.stat_samples++;
    // Между концом прошлого кадра и началом этого: простой GPU или работа вне проходов
    if (R.last_end && R.timer_frame[slot] == R.last_frame + 1 && t[0] > R.last_end) {
        R.stat_gap += (f64)(t[0] - R.last_end) * 1e-6;
        R.stat_gaps++;
    }
    R.last_frame = R.timer_frame[slot];
    R.last_end = t[TIMER_MARKS - 1];
    R.timer_pending[slot] = false;
}

static void mark(u32 i) { rb_timer_mark(R.timer_slot, i); }

// Время CPU в той же метке: сколько отправка вызовов прохода заняла у процессора
static void pass_mark(u32 i)
{
    R.cpu_t[i] = time_now();
}

void render_take_stats(RenderStats *out)
{
    memset(out, 0, sizeof *out);
    out->samples = R.stat_samples;
    if (R.stat_samples) {
        for (u32 p = 0; p < PASS_COUNT; p++) {
            out->gpu_ms[p] = R.stat_ms[p] / R.stat_samples;
            out->gpu_total_ms += out->gpu_ms[p];
        }
    }
    out->gap_ms = R.stat_gaps ? R.stat_gap / R.stat_gaps : -1.0;
    memset(R.stat_ms, 0, sizeof R.stat_ms);
    R.stat_samples = 0;
    R.stat_gap = 0.0;
    R.stat_gaps = 0;
    f64 k = R.stat_frames ? 1.0 / R.stat_frames : 0.0;
    for (u32 p = 0; p < 2; p++) {
        out->draws[p] = R.stat_draws[p] * k;
        out->tris[p] = R.stat_tris[p] * k;
        out->visible[p] = R.stat_visible[p] * k;
    }
    out->items = R.stat_items;
    out->prep_ms = R.stat_prep * 1000.0 * k;
    for (u32 p = 0; p < PASS_COUNT; p++) out->cpu_ms[p] = R.stat_cpu[p] * 1000.0 * k;
    memset(R.stat_cpu, 0, sizeof R.stat_cpu);
    out->poses = R.stat_poses * k;
    R.stat_prep = R.stat_poses = 0;
    out->vrs = R.stat_vrs;
    memset(R.stat_draws, 0, sizeof R.stat_draws);
    memset(R.stat_tris, 0, sizeof R.stat_tris);
    memset(R.stat_visible, 0, sizeof R.stat_visible);
    R.stat_frames = 0;
}

// Плоскости отсечения из матрицы view-proj (по строкам, как у Gribb и Hartmann)
static Frustum frustum_from(M4 m)
{
    V4 row[4];
    for (u32 i = 0; i < 4; i++) row[i] = (V4){ m.m[i], m.m[4 + i], m.m[8 + i], m.m[12 + i] };
    Frustum f;
    for (u32 i = 0; i < 3; i++) {
        f.p[i * 2 + 0] = (V4){ row[3].x + row[i].x, row[3].y + row[i].y, row[3].z + row[i].z, row[3].w + row[i].w };
        f.p[i * 2 + 1] = (V4){ row[3].x - row[i].x, row[3].y - row[i].y, row[3].z - row[i].z, row[3].w - row[i].w };
    }
    for (u32 i = 0; i < 6; i++) {
        f32 k = 1.0f / v3_len(v3(f.p[i].x, f.p[i].y, f.p[i].z));
        f.p[i] = (V4){ f.p[i].x * k, f.p[i].y * k, f.p[i].z * k, f.p[i].w * k };
    }
    return f;
}

static bool sphere_visible(const Frustum *f, V4 s)
{
    for (u32 i = 0; i < 6; i++)
        if (f->p[i].x * s.x + f->p[i].y * s.y + f->p[i].z * s.z + f->p[i].w < -s.w) return false;
    return true;
}

// 0 — снаружи, 1 — пересекает границу, 2 — целиком внутри
static u32 sphere_test(const Frustum *f, V4 s)
{
    u32 res = 2;
    for (u32 i = 0; i < 6; i++) {
        f32 d = f->p[i].x * s.x + f->p[i].y * s.y + f->p[i].z * s.z + f->p[i].w;
        if (d < -s.w) return 0;
        if (d < s.w) res = 1;
    }
    return res;
}

// Масштаб костей набора: наибольшая длина оси каждой матрицы. Одежда и оружие делят набор с телом,
// поэтому он считается раз на набор, а не на каждый элемент (stream_poses сбрасывает перед кадром)
static struct {
    const M4 *bones;
    f32       scale[RENDER_MAX_BONES];
} g_bone_scale;

static const f32 *bone_scales(const M4 *bones)
{
    if (g_bone_scale.bones != bones) {
        g_bone_scale.bones = bones;
        for (u32 b = 0; b < RENDER_MAX_BONES; b++) {
            const M4 *x = &bones[b];
            f32 sx = v3_len(v3(x->m[0], x->m[1], x->m[2]));
            f32 sy = v3_len(v3(x->m[4], x->m[5], x->m[6]));
            f32 sz = v3_len(v3(x->m[8], x->m[9], x->m[10]));
            g_bone_scale.scale[b] = maxf(sx, maxf(sy, sz));
        }
    }
    return g_bone_scale.scale;
}

// Мировая сфера элемента: у скелетной сетки — вокруг сфер её костей в текущей позе
static V4 item_sphere(const DrawItem *it)
{
    const GpuMesh *m = it->mesh;
    if (!it->bones) return m->bound;
    const f32 *scale = bone_scales(it->bones);
    V4 bs[RENDER_MAX_BONES];
    u32 n = 0;
    V3 lo = v3(FLT_MAX, FLT_MAX, FLT_MAX), hi = v3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (u32 mask = m->bone_mask; mask; mask &= mask - 1) {
        u32 b = (u32)__builtin_ctz(mask);
        V4 l = m->bone_bound[b];
        V3 c = m4_point(it->bones[b], v3(l.x, l.y, l.z));
        f32 r = l.w * scale[b];
        bs[n++] = (V4){ c.x, c.y, c.z, r };
        lo = v3(minf(lo.x, c.x - r), minf(lo.y, c.y - r), minf(lo.z, c.z - r));
        hi = v3(maxf(hi.x, c.x + r), maxf(hi.y, c.y + r), maxf(hi.z, c.z + r));
    }
    if (!n) return (V4){ 0, 0, 0, 0 };
    V3 c = v3_mul(v3_add(lo, hi), 0.5f);
    f32 r = 0.0f;
    for (u32 i = 0; i < n; i++) r = maxf(r, v3_len(v3_sub(v3(bs[i].x, bs[i].y, bs[i].z), c)) + bs[i].w);
    return (V4){ c.x, c.y, c.z, r };
}

// Уровень детализации по размеру сферы в пикселях прохода
static const GpuMesh *pick_lod(const GpuMesh *m, V4 s, const PassView *pv)
{
    f32 depth = v3_dot(v3_sub(v3(s.x, s.y, s.z), pv->eye), pv->fwd);
    bool ortho = pv->fwd.x == 0.0f && pv->fwd.y == 0.0f && pv->fwd.z == 0.0f;
    f32 px = 2.0f * s.w * pv->px_k / (ortho ? 1.0f : maxf(depth, s.w));
    while (!pv->full && m->lod_next && px < m->lod_px) m = m->lod_next;
    return m;
}

static int inst_cmp(const void *pa, const void *pb)
{
    const Inst *a = pa, *b = pb;
    if (a->mesh != b->mesh) return (uintptr_t)a->mesh < (uintptr_t)b->mesh ? -1 : 1;
    return a->pose < b->pose ? -1 : (a->pose > b->pose);
}

// Видимые экземпляры прохода, сгруппированные по сетке: сетки по возрастанию адреса, внутри сетки —
// по возрастанию набора костей, статика последней (тот же порядок, что сортировка по паре).
// Корзины по сеткам за линейное время: элементы и так идут по возрастанию набора
#define SORT_HASH 512   // степень двойки, больше разных сеток в проходе
#define SORT_MESHES 256

static inline u32 mesh_hash(const GpuMesh *m) { return (u32)(((uintptr_t)m >> 4) * 2654435761u) & (SORT_HASH - 1); }

static u32 gather(const DrawItem *items, u32 count, const PassView *pv, bool shadow, Inst *out)
{
    static Inst vis[RENDER_MAX_ITEMS];
    static u16 vis_b[RENDER_MAX_ITEMS];
    static const GpuMesh *slot_mesh[SORT_HASH];
    static u16 slot_b[SORT_HASH];
    const GpuMesh *bm[SORT_MESHES];
    u32 bslot[SORT_MESHES], bcount[SORT_MESHES], border[SORT_MESHES], boff[SORT_MESHES];
    u32 nb = 0, n = 0;
    bool overflow = false;
    for (u32 i = 0; i < count; i++) {
        const DrawItem *it = &items[i];
        if (shadow && it->no_shadow) continue;
        if (it->bones && g_pose[i] == NO_POSE) continue;
        if (!sphere_visible(&pv->fr, g_sphere[i])) continue;
        const GpuMesh *m = pick_lod(it->mesh, g_sphere[i], pv);
        vis[n] = (Inst){ m, it->bones ? g_pose[i] : NO_POSE };
        if (!overflow) {
            u32 h = mesh_hash(m);
            while (slot_mesh[h] && slot_mesh[h] != m) h = (h + 1) & (SORT_HASH - 1);
            if (!slot_mesh[h]) {
                if (nb == SORT_MESHES) overflow = true;
                else {
                    slot_mesh[h] = m;
                    slot_b[h] = (u16)nb;
                    bm[nb] = m;
                    bslot[nb] = h;
                    bcount[nb++] = 0;
                }
            }
            if (!overflow) {
                vis_b[n] = slot_b[h];
                bcount[slot_b[h]]++;
            }
        }
        n++;
    }
    for (u32 b = 0; b < nb; b++) slot_mesh[bslot[b]] = NULL;
    if (overflow) {
        memcpy(out, vis, sizeof *vis * n);
        qsort(out, n, sizeof *out, inst_cmp);
        return n;
    }
    // Корзины по адресу сетки (вставками: их десятки), затем раскладка с сохранением порядка
    for (u32 b = 0; b < nb; b++) {
        u32 j = b;
        for (; j > 0 && (uintptr_t)bm[border[j - 1]] > (uintptr_t)bm[b]; j--) border[j] = border[j - 1];
        border[j] = b;
    }
    for (u32 k = 0, off = 0; k < nb; k++) {
        boff[border[k]] = off;
        off += bcount[border[k]];
    }
    for (u32 pass = 0; pass < 2; pass++)
        for (u32 i = 0; i < n; i++)
            if ((vis[i].pose == NO_POSE) == (pass == 1)) out[boff[vis_b[i]]++] = vis[i];
    return n;
}

// Конвейер сеток текущего прохода для раскладки; без отсечения граней — только у обычных сеток
static void use_pipe(u32 lay, bool two_sided)
{
    rb_bind_pipe(R.mesh_pipe[R.pset][lay][two_sided && lay == LAY_MESH ? 1 : 0]);
}

static void push_draw(i32 skinned, i32 inst_base, i32 morphs)
{
    i32 v[4] = { skinned, inst_base, morphs, 0 };
    rb_push(PUSH_DRAW, sizeof v, v);
}

// Сетка из своих буферов; поток весов 0 — поток нулей
static void bind_mesh(const GpuMesh *m, bool two_sided)
{
    use_pipe(LAY_MESH, two_sided);
    rb_bind_vertex(0, m->vbo, 0);
    rb_bind_vertex(1, m->skin_vbo, 0);
    rb_bind_index(m->ibo, 0, false);
}

// Общий буфер сеток: записи экземпляров — часть кольца текущего кадра
static void bind_pool(void)
{
    use_pipe(LAY_POOL, false);
    rb_bind_vertex(0, R.pool_vbo, 0);
    rb_bind_vertex(1, R.pool_skin, 0);
    rb_bind_vertex(2, R.pool_rec, (size_t)R.stream * POOL_RECS * sizeof(PoolRec));
    rb_bind_index(R.pool_ibo, 0, false);
}

// Подряд идущие экземпляры одной сетки: статика по одному, скелетные — все сразу
typedef struct InstRun {
    u32 first, end;
    i32 cmd;   // команда общего буфера, -1 — свой вызов
} InstRun;

// Сетки из общего буфера с костями — команды одного косвенного вызова (порядок вызовов прежний:
// при равной глубине в проходе сцены побеждает последний). Остальное — по вызову на сетку,
// скелетные — один вызов на все экземпляры одной сетки
static void draw_insts(const Inst *in, u32 n, u32 base, u32 pass)
{
    static InstRun run[RENDER_MAX_ITEMS];
    static PoolCmd cmd[RENDER_MAX_ITEMS];
    static PoolRec rec[RENDER_MAX_ITEMS];
    u32 nrun = 0, nc = 0, nr = 0;
    for (u32 i = 0; i < n;) {
        const GpuMesh *m = in[i].mesh;
        u32 j = i + 1;
        if (in[i].pose != NO_POSE)
            while (j < n && in[j].mesh == m && in[j].pose != NO_POSE) j++;
        InstRun *r = &run[nrun++];
        *r = (InstRun){ i, j, -1 };
        if (m->pooled && in[i].pose != NO_POSE) {
            if (R.used_recs + nr + (j - i) <= POOL_RECS) {
                r->cmd = (i32)nc;
                cmd[nc++] = (PoolCmd){ m->index_count, j - i, m->pool_ibase, (i32)m->pool_vbase, R.used_recs + nr };
                for (u32 k = i; k < j; k++) rec[nr++] = (PoolRec){ in[k].pose, m->morph_count, m->pool_vbase, m->pool_moff };
            } else {
                if (!R.warned_recs) log_error("РЕНДЕР: записей экземпляров больше %u за кадр, лишние не рисуются", POOL_RECS);
                R.warned_recs = true;
                nrun--;
            }
        }
        i = j;
    }
    // Записи и команды прохода — в кольцо кадра одним куском
    size_t ring = (size_t)R.stream * POOL_RECS;
    u32 cmd0 = R.used_cmds;
    if (nc) {
        rb_buf_write(R.pool_rec, (ring + R.used_recs) * sizeof(PoolRec), nr * sizeof(PoolRec), rec);
        rb_buf_write(R.pool_cmd, (ring + cmd0) * sizeof(PoolCmd), nc * sizeof(PoolCmd), cmd);
        R.used_recs += nr;
        R.used_cmds += nc;
    }

    u32 draws = 0;
    f64 tris = 0;
    bool pool_on = false;   // привязаны общий буфер и его морфы
    for (u32 q = 0; q < nrun;) {
        const InstRun *r = &run[q];
        const GpuMesh *m = in[r->first].mesh;
        if (r->cmd >= 0) {
            u32 q2 = q + 1;
            while (q2 < nrun && run[q2].cmd >= 0) q2++;
            if (!pool_on) {
                bind_pool();
                push_draw(SKIN_POOL, 0, 0);
                rb_bind_ssbo(B_MORPH, R.pool_morph, 0, 0);
                pool_on = true;
            }
            u32 c0 = (u32)r->cmd, c1 = (u32)run[q2 - 1].cmd + 1;
            if (R.mdi) {
                rb_draw_indexed_indirect(R.pool_cmd, (ring + cmd0 + c0) * sizeof(PoolCmd), c1 - c0);
                draws++;
            } else {
                for (u32 c = c0; c < c1; c++)
                    rb_draw_indexed(cmd[c].count, cmd[c].first_index, cmd[c].base_vertex, cmd[c].instances, cmd[c].base_instance);
                draws += c1 - c0;
            }
            for (u32 c = c0; c < c1; c++) tris += (f64)(cmd[c].count / 3) * cmd[c].instances;
            q = q2;
            continue;
        }
        u32 i = r->first, j = r->end;
        pool_on = false;
        if (m->pooled) bind_pool();
        else bind_mesh(m, false);
        if (in[i].pose == NO_POSE) {
            push_draw(SKIN_STATIC, 0, 0);
            if (m->pooled) rb_draw_indexed(m->index_count, m->pool_ibase, (i32)m->pool_vbase, 1, 0);
            else rb_draw_indexed(m->index_count, 0, 0, 1, 0);
        } else {
            push_draw(SKIN_BONES, (i32)(base + i), (i32)m->morph_count);
            if (m->morph_count) rb_bind_ssbo(B_MORPH, m->morph_buf, 0, 0);
            rb_draw_indexed(m->index_count, 0, 0, j - i, 0);
        }
        draws++;
        tris += (f64)(m->index_count / 3) * (f64)(j - i);
        q++;
    }
    if (pass >= 2) return;  // портреты и проход глубины в счётчики кадра не идут
    R.stat_draws[pass] += draws;
    R.stat_tris[pass] += tris;
    R.stat_visible[pass] += n;
}

// Сбор видимых экземпляров: корзина на каждую сетку (с учётом LOD)
typedef struct Gather {
    const GpuMesh *mesh[MAX_INST_MESHES];
    u32 count[MAX_INST_MESHES];
    u32 nb, nv;
    // Поиск корзины по адресу сетки: открытая адресация, SORT_HASH ячеек; пустые — NULL
    const GpuMesh *slot_mesh[SORT_HASH];
    u16 slot_b[SORT_HASH];
} Gather;

#define CHAIN_MAX 8  // уровней детализации у сетки, не больше

// Корзина сетки: найти или завести; ~0u — корзин больше нет
static u32 gather_bucket(Gather *ga, const GpuMesh *m)
{
    u32 h = mesh_hash(m);
    while (ga->slot_mesh[h] && ga->slot_mesh[h] != m) h = (h + 1) & (SORT_HASH - 1);
    if (ga->slot_mesh[h]) return ga->slot_b[h];
    if (ga->nb == MAX_INST_MESHES) return ~0u;
    ga->slot_mesh[h] = m;
    ga->slot_b[h] = (u16)ga->nb;
    ga->mesh[ga->nb] = m;
    ga->count[ga->nb] = 0;
    return ga->nb++;
}

static inline void gather_add(Gather *ga, u32 b, u32 id)
{
    if (b == ~0u) return;
    ga->count[b]++;
    g_vis_bucket[ga->nv] = (u16)b;
    g_vis_id[ga->nv++] = id;
}

// Группа: прореживание по дальности, отсечение, LOD. inside — сфера группы целиком в пирамиде
static void gather_group(Gather *ga, const InstGroup *gr, const PassView *pv, bool inside)
{
    const GpuMesh *m = gr->mesh;
    u32 n = gr->count;
    if (m->fade[1] > 0.0f) {
        // Сколько первых экземпляров может быть видно с ближней точки группы
        f32 d = v3_len(v3_sub(v3(gr->bound.x, gr->bound.y, gr->bound.z), R.cam_eye)) - gr->bound.w;
        if (d >= m->fade[1]) return;
        n = (u32)ceilf((f32)n * clampf((m->fade[1] - d) / (m->fade[1] - m->fade[0]), 0.0f, 1.0f));
    }
    if (!inside) {
        u32 vis = sphere_test(&pv->fr, gr->bound);
        if (!vis) return;
        inside = vis == 2;
    }
    if (n > RENDER_MAX_VIS_INST - ga->nv) {
        n = RENDER_MAX_VIS_INST - ga->nv;
        if (!R.warned_inst) log_error("РЕНДЕР: видимых экземпляров больше %u, лишние не рисуются", RENDER_MAX_VIS_INST);
        R.warned_inst = true;
    }
    V4 gsph = gr->bound;
    gsph.w *= pv->inst_lod_k;
    const GpuMesh *lm = pick_lod(m, gsph, pv);
    if (gr->bulk) {
        u32 b = gather_bucket(ga, lm);
        for (u32 i = 0; i < n; i++) gather_add(ga, b, gr->first + i);
        return;
    }
    // Корзины уровней этой сетки ищутся раз на группу: соседние экземпляры часто разного уровня
    const GpuMesh *chain[CHAIN_MAX];
    u32 chain_b[CHAIN_MAX], nc = 0;
    for (const GpuMesh *c = m; c && nc < CHAIN_MAX; c = c->lod_next) {
        chain[nc] = c;
        chain_b[nc++] = ~1u;  // ещё не искали
    }
    V4 lb = m->bound;
    f32 lxz = sqrtf(lb.x * lb.x + lb.z * lb.z);
    for (u32 i = 0; i < n; i++) {
        const RInst *r = &R.inst_cpu[gr->first + i];
        f32 s = maxf(r->scale, r->hscale);
        V4 sph = { r->pos[0], r->pos[1] + lb.y * r->hscale, r->pos[2], (lb.w + lxz) * s + fabsf(r->lean) * lb.y * r->hscale };
        if (!inside && !sphere_visible(&pv->fr, sph)) continue;
        if (m->lod_next) {
            sph.w *= pv->inst_lod_k;
            lm = pick_lod(m, sph, pv);
        }
        u32 c = 0;
        while (c + 1 < nc && chain[c] != lm) c++;
        if (chain[c] != lm) {
            gather_add(ga, gather_bucket(ga, lm), gr->first + i);
            continue;
        }
        if (chain_b[c] == ~1u) chain_b[c] = gather_bucket(ga, lm);
        gather_add(ga, chain_b[c], gr->first + i);
    }
}

// Видимые экземпляры прохода по участкам. Номера экземпляров раскладываются по сеткам
// и уходят в поток кадра; возвращает число вызовов в draws
static u32 gather_groups(const PassView *pv, bool shadow, InstDraw *draws)
{
    static Gather ga;
    ga.nb = ga.nv = 0;
    for (u32 c = 0; c < R.cluster_count; c++) {
        const InstCluster *cl = &R.clusters[c];
        if (v3_len(v3_sub(v3(cl->bound.x, cl->bound.y, cl->bound.z), R.cam_eye)) - cl->bound.w > INST_FAR) continue;
        u32 vis = sphere_test(&pv->fr, cl->bound);
        if (!vis) continue;
        for (u32 g = cl->first; g < cl->first + cl->count; g++)
            if (!shadow || !R.groups[g].no_shadow) gather_group(&ga, &R.groups[g], pv, vis == 2);
    }
    // Таблица поиска пуста к следующему проходу
    for (u32 b = 0; b < ga.nb; b++) {
        u32 h = mesh_hash(ga.mesh[b]);
        while (ga.slot_mesh[h] != ga.mesh[b]) h = (h + 1) & (SORT_HASH - 1);
        ga.slot_mesh[h] = NULL;
    }
    // Раскладка по сеткам: смещения, затем номера в поток кадра
    u32 base = R.used_insts;
    u32 nv = MIN(ga.nv, (u32)ARRAY_COUNT(g_inst_pose) - base);
    u32 off = base;
    for (u32 b = 0; b < ga.nb; b++) {
        draws[b] = (InstDraw){ ga.mesh[b], off, 0 };
        off += ga.count[b];
    }
    for (u32 i = 0; i < nv; i++) {
        InstDraw *d = &draws[g_vis_bucket[i]];
        g_inst_pose[d->base + d->count++] = g_vis_id[i];
    }
    if (nv)
        rb_buf_write(R.inst_buf[R.stream], sizeof(u32) * base, sizeof(u32) * nv, &g_inst_pose[base]);
    R.used_insts += nv;
    return ga.nb;
}

static void draw_groups(const InstDraw *draws, u32 n, u32 pass)
{
    if (!n) return;
    f64 tris = 0, vis = 0;
    for (u32 i = 0; i < n; i++) {
        const InstDraw *d = &draws[i];
        if (!d->count) continue;
        const GpuMesh *m = d->mesh;
        bind_mesh(m, m->two_sided);
        push_draw(SKIN_INSTANCED, (i32)d->base, 0);
        f32 fade[4] = { m->fade[0], m->fade[1], 0.0f, 0.0f };
        rb_push(PUSH_FADE, sizeof fade, fade);
        rb_draw_indexed(m->index_count, 0, 0, d->count, 0);
        tris += (f64)(m->index_count / 3) * d->count;
        vis += d->count;
    }
    if (pass >= 2) return;
    R.stat_draws[pass] += n;
    R.stat_tris[pass] += tris;
    R.stat_visible[pass] += vis;
}

// Рельеф кольцами: видимые куски уровней. Тени отбрасывает только ближний уровень (±64 м) и не там, где
// ровно: дальние горы дали бы тень лишь в окне карты теней вокруг камеры
static void draw_terrain(const PassView *pv, u32 pass)
{
    u32 draws = 0;
    f64 tris = 0;
    use_pipe(LAY_NONE, false);
    terrain_draw(pv->fr.p, pass == 0 ? 1 : TERRAIN_LEVELS, pass == 0 ? 0.25f : -1.0f, &draws, &tris);
    if (pass >= 2) return;
    R.stat_draws[pass] += draws;
    R.stat_tris[pass] += tris;
}

// Наборы костей элементов — в буфер кадра после уже занятых; одинаковые подряд — один набор
static void stream_poses(const DrawItem *items, u32 count)
{
    u32 first = R.used_poses, poses = first;
    g_bone_scale.bones = NULL;
    for (u32 i = 0; i < count; i++) {
        g_sphere[i] = item_sphere(&items[i]);
        g_pose[i] = NO_POSE;
        if (!items[i].bones) continue;
        if (i > 0 && items[i - 1].bones == items[i].bones && g_pose[i - 1] != NO_POSE) {
            g_pose[i] = g_pose[i - 1];
        } else if (poses < RENDER_MAX_POSED) {
            memcpy(&g_bones[(size_t)poses * RENDER_MAX_BONES], items[i].bones, sizeof(M4) * RENDER_MAX_BONES);
            g_pose[i] = poses++;
        } else if (!R.warned_poses) {
            log_error("РЕНДЕР: наборов костей больше %u, лишние не рисуются", RENDER_MAX_POSED);
            R.warned_poses = true;
        }
    }
    if (poses > first)
        rb_buf_write(R.bone_buf[R.stream], sizeof(M4) * RENDER_MAX_BONES * first, sizeof(M4) * RENDER_MAX_BONES * (poses - first),
                     &g_bones[(size_t)first * RENDER_MAX_BONES]);
    R.used_poses = poses;
}

// Номера наборов для экземпляров прохода; возвращает начало списка в буфере
static u32 stream_insts(const Inst *in, u32 *n)
{
    u32 base = R.used_insts;
    *n = MIN(*n, (u32)ARRAY_COUNT(g_inst_pose) - base);
    for (u32 i = 0; i < *n; i++) g_inst_pose[base + i] = in[i].pose;
    if (*n)
        rb_buf_write(R.inst_buf[R.stream], sizeof(u32) * base, sizeof(u32) * *n, &g_inst_pose[base]);
    R.used_insts += *n;
    return base;
}

// Матрица света; привязка к текселю убирает дрожание теней при движении камеры
static M4 light_matrix(const FrameView *fv)
{
    V3 L = v3_norm(fv->sun_dir);
    V3 up = fabsf(L.y) > 0.95f ? v3(0, 0, 1) : v3(0, 1, 0);
    M4 lv = m4_look_at(v3(0, 0, 0), v3_mul(L, -1.0f), up);
    V3 c = m4_point(lv, fv->shadow_center);
    f32 texel = 2.0f * SHADOW_EXTENT / SHADOW_SIZE;
    c.x = floorf(c.x / texel) * texel;
    c.y = floorf(c.y / texel) * texel;
    M4 lp = m4_ortho(c.x - SHADOW_EXTENT, c.x + SHADOW_EXTENT, c.y - SHADOW_EXTENT, c.y + SHADOW_EXTENT,
                     -c.z - 250.0f, -c.z + 250.0f);
    return m4_mul(lp, lv);
}
// Свет кадра: видимые источники, ближайшие к камере (по краю сферы), не больше RENDER_MAX_LIGHTS
static void stream_lights(const FrameView *fv, const Frustum *fr)
{
    u32 n = 0;
    f32 key[RENDER_MAX_LIGHTS];
    const PointLight *pick[RENDER_MAX_LIGHTS];
    for (u32 i = 0; fv->lights && i < fv->light_count; i++) {
        const PointLight *l = &fv->lights[i];
        if (!sphere_visible(fr, (V4){ l->pos.x, l->pos.y, l->pos.z, l->radius })) continue;
        f32 k = v3_len(v3_sub(l->pos, fv->cam_pos)) - l->radius;
        if (n == RENDER_MAX_LIGHTS && k >= key[n - 1]) continue;
        u32 j = n < RENDER_MAX_LIGHTS ? n++ : n - 1;
        for (; j > 0 && key[j - 1] > k; j--) {
            key[j] = key[j - 1];
            pick[j] = pick[j - 1];
        }
        key[j] = k;
        pick[j] = l;
    }
    g_lights.count[0] = (i32)n;
    for (u32 i = 0; i < n; i++) {
        const PointLight *l = pick[i];
        V3 d = v3_len(l->dir) > 0.0f ? v3_norm(l->dir) : v3(0, -1, 0);
        memcpy(g_lights.pos[i], (f32[4]){ l->pos.x, l->pos.y, l->pos.z, l->radius }, sizeof g_lights.pos[i]);
        memcpy(g_lights.col[i], (f32[4]){ l->color.x, l->color.y, l->color.z, l->cone }, sizeof g_lights.col[i]);
        memcpy(g_lights.dir[i], (f32[4]){ d.x, d.y, d.z, l->glow }, sizeof g_lights.dir[i]);
    }
    rb_buf_write(R.light_buf[R.stream], 0, sizeof g_lights, &g_lights);
    rb_bind_ubo(B_LIGHTS, R.light_buf[R.stream], 0, 0);
}

// Блок констант кадра: block 0 — сам кадр, 1 + слот — портрет
static void frame_write(u32 block, const GpuFrame *f)
{
    size_t off = ((size_t)R.stream * FRAME_BLOCKS + block) * R.frame_step;
    rb_buf_write(R.frame_buf, off, sizeof *f, f);
    rb_bind_ubo(B_FRAME, R.frame_buf, off, sizeof *f);
}

static void push_view_proj(M4 m) { rb_push(0, sizeof m.m, m.m); }

static void v3_to(f32 *out, V3 v)
{
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

void render_frame(const FrameView *fv, const DrawItem *items, u32 count, const RenderSettings *rs, i32 win_w, i32 win_h)
{
    rb_frame_begin(win_w, win_h);
    if (win_w <= 0 || win_h <= 0 || !R.mesh_pipe[PS_SCENE][LAY_MESH][0]) return;
    i32 iw = win_w, ih = win_h;
    if (R.scene.w != iw || R.scene.h != ih) {
        target_make(&R.scene, iw, ih, RB_FMT_RGBA16F, true);
        target_make(&R.rays, (iw + 1) / 2, (ih + 1) / 2, RB_FMT_RG16F, false);
        target_make(&R.ao, (iw + 1) / 2, (ih + 1) / 2, RB_FMT_RG16F, false);
        target_make(&R.atmos, iw, ih, RB_FMT_RGBA16F, false);
        target_make(&R.src_map, iw, ih, RB_FMT_RG16UI, false);
        target_make(&R.level, iw, ih, RB_FMT_R8UI, false);
        target_make(&R.level_max, (iw + 7) / 8, (ih + 7) / 8, RB_FMT_R8UI, false);
        if (R.vrs && !(target_make(&R.block, (iw + 3) / 4, (ih + 3) / 4, RB_FMT_R8UI, false) &&
                       target_make(&R.rate, (iw + R.vrs_tile[0] - 1) / R.vrs_tile[0], (ih + R.vrs_tile[1] - 1) / R.vrs_tile[1],
                                   RB_FMT_R8UI, false)))
            R.vrs = false;
        log_info("РЕНДЕР: кадр %dx%d", iw, ih);
    }

    // Слот занят непрочитанным кадром: забираем или пропускаем замер
    timer_collect(R.timer_slot);
    bool timing = R.timing = !R.timer_pending[R.timer_slot];
    if (timing) mark(0);

    M4 vp = m4_mul(fv->proj, fv->view);
    M4 light_vp = light_matrix(fv);
    V3 L = v3_norm(fv->sun_dir);

    // Границы и позы: наборы костей кадра уходят на GPU одним буфером (кольцо из STREAM_FRAMES)
    if (count > RENDER_MAX_ITEMS) {
        if (!R.warned_items) log_error("РЕНДЕР: элементов %u, рисуются первые %u", count, RENDER_MAX_ITEMS);
        R.warned_items = true;
        count = RENDER_MAX_ITEMS;
    }
    f64 t_prep = time_now();
    R.stream = rb_frame_slot();
    R.used_poses = R.used_insts = R.used_recs = R.used_cmds = 0;
    R.mdi = rs->mdi;
    stream_poses(items, count);
    R.stat_poses += R.used_poses;
    // Экземпляры в тенях берут уровень детализации грубее: тень мельче деталей
    PassView shadow_view = { .fr = frustum_from(light_vp), .px_k = SHADOW_SIZE / (2.0f * SHADOW_EXTENT), .inst_lod_k = 0.3f };
    PassView scene_view = {
        .fr = frustum_from(vp), .eye = fv->cam_pos,
        .fwd = v3(-fv->view.m[2], -fv->view.m[6], -fv->view.m[10]),
        .px_k = fv->proj.m[5] * 0.5f * (f32)ih, .inst_lod_k = 1.0f,
    };
    R.cam_eye = fv->cam_pos;
    // Рельеф: кольца за камерой, новые полосы — у источника
    terrain_update(fv->cam_pos, fv->shadow_center);
    stream_lights(fv, &scene_view.fr);
    u32 n_shadow = rs->shadows ? gather(items, count, &shadow_view, true, g_inst[0]) : 0;
    u32 n_scene = gather(items, count, &scene_view, false, g_inst[1]);
    u32 base_shadow = stream_insts(g_inst[0], &n_shadow);
    u32 base_scene = stream_insts(g_inst[1], &n_scene);
    u32 ng_shadow = rs->shadows && rs->flora ? gather_groups(&shadow_view, true, g_idraw[0]) : 0;
    u32 ng_scene = rs->flora ? gather_groups(&scene_view, false, g_idraw[1]) : 0;
    bool atmos = rs->atmos, rays = atmos && rs->rays && rs->shadows, ao = atmos && rs->ao;
    // Размер пикселя на дальности 1 м; s^P = (px_m / d)^P = PIXEL_K * px_m^P * (1/d)^P
    f32 px_k = rs->pixel ? PIXEL_K * powf(fv->proj.m[5] * 0.5f * (f32)ih, PIXEL_P) : 0.0f;
    bool vrs = R.vrs && rs->vrs && rs->pixel;
    R.stat_vrs = vrs;

    // Константы кадра: одни на все проходы
    GpuFrame *f = &R.frame;
    memset(f, 0, sizeof *f);
    memcpy(f->light_vp, light_vp.m, sizeof f->light_vp);
    M4 inv_vp = m4_inverse(vp);
    memcpy(f->inv_vp, inv_vp.m, sizeof f->inv_vp);
    v3_to(f->cam_pos, fv->cam_pos);
    f->time = fv->time;
    v3_to(f->sun_dir, L);
    f->texel_on = rs->pixel ? 1.0f : 0.0f;
    v3_to(f->sun_color, fv->sun_color);
    f->proj_scale = fv->proj.m[5] * 0.5f * (f32)ih;
    v3_to(f->sky_amb, fv->sky_amb);
    f->frame = (i32)fv->frame;
    v3_to(f->ground_amb, fv->ground_amb);
    f->noise_tex = rs->noise_tex ? 1 : 0;
    v3_to(f->haze, fv->haze);
    f->shadow_on = rs->shadows ? 1 : 0;
    v3_to(f->zenith, fv->zenith);
    memcpy(f->pix, (f32[4]){ px_k, PIXEL_P, fv->proj.m[10], fv->proj.m[14] }, sizeof f->pix);
    f->post = rs->post ? 1 : 0;
    f->ao_half = rs->ao_half ? 1 : 0;
    f->parts = (rays ? 1 : 0) | (ao ? 2 : 0);
    f->tile[0] = R.vrs_tile[0] / 4;
    f->tile[1] = R.vrs_tile[1] / 4;
    frame_write(0, f);
    R.stat_prep += time_now() - t_prep;
    rb_bind_ssbo(B_BONES, R.bone_buf[R.stream], 0, 0);
    rb_bind_ssbo(B_INST, R.inst_buf[R.stream], 0, 0);
    if (R.static_inst_buf) rb_bind_ssbo(B_STATIC_INST, R.static_inst_buf, 0, 0);
    R.stat_frames++;
    pass_mark(0);
    R.stat_items = count;

    // 1. Тени. Ветер и прореживание в вершинном шейдере: те же значения во всех проходах (и в проходе глубины)
    if (rs->shadows) {
        rb_pass_begin(&(RbPass){ .depth = { R.shadow_tex }, .clear_depth = true });
        R.pset = PS_SHADOW;
        push_view_proj(light_vp);
        draw_insts(g_inst[0], n_shadow, base_shadow, 0);
        draw_groups(g_idraw[0], ng_shadow, 0);
        draw_terrain(&shadow_view, 0);
        rb_pass_end();
    }
    pass_mark(1);
    if (timing) mark(1);

    // 2. Сцена. Без атмосферы небо не рисуется: на его месте цвет дымки.
    // Сначала только глубина: тяжёлый шейдер материалов затем считает каждый пиксель один раз
    f32 clear[4] = { 0, 0, 0, 1 };
    if (!atmos) memcpy(clear, (f32[4]){ fv->haze.x * 1.25f, fv->haze.y * 1.25f, fv->haze.z * 1.25f, 1 }, sizeof clear);
    pass_to(&R.scene, true, clear, 0);
    R.pset = PS_DEPTH;
    push_view_proj(vp);
    draw_insts(g_inst[1], n_scene, base_scene, 2);
    draw_groups(g_idraw[1], ng_scene, 2);
    draw_terrain(&scene_view, 2);
    rb_pass_end();
    // По готовой глубине: размер пикселя в каждой точке и его максимум по блокам 8x8,
    // затем откуда каждый пиксель берёт цвет, раз за кадр (фильтр и карта частоты).
    // Без пиксельности каждый пиксель берёт свой цвет: карты не нужны
    rb_bind_tex(B_DEPTH, R.scene.depth, RB_SMP_NEAREST);
    if (rs->pixel) {
        draw_full(&R.level, R.pipe_level, 0);
        rb_bind_tex(B_LEVEL, R.level.color, RB_SMP_NEAREST);
        draw_full(&R.level_max, R.pipe_level_max, 0);
        rb_bind_tex(B_LEVEL_MAX, R.level_max.color, RB_SMP_NEAREST);
        draw_full(&R.src_map, R.pipe_src_map, 0);
    }
    rb_bind_tex(B_SRC_MAP, R.src_map.color, RB_SMP_NEAREST);
    if (vrs) {
        // Где фильтр сольёт пиксели в блоки, материалы и атмосфера считаются раз на блок
        draw_full(&R.block, R.pipe_vrs_block, 0);
        rb_bind_tex(B_BLOCK, R.block.color, RB_SMP_NEAREST);
        draw_full(&R.rate, R.pipe_vrs, 0);
    }
    RbTex rate = vrs ? R.rate.color : 0;
    pass_mark(2);
    if (timing) mark(2);
    pass_to(&R.scene, false, NULL, rate);
    R.pset = PS_SCENE;
    rb_bind_tex(B_SHADOW, R.shadow_tex, RB_SMP_SHADOW);
    draw_insts(g_inst[1], n_scene, base_scene, 1);
    draw_groups(g_idraw[1], ng_scene, 1);
    draw_terrain(&scene_view, 1);
    rb_pass_end();
    pass_mark(3);
    if (timing) mark(3);

    // 3. Атмосфера. Сначала объёмный свет и AO в половинном разрешении, затем небо, туман и AO.
    // Карта блоков — в пикселях кадра, к целям половинного разрешения она не относится
    if (rays) draw_full(&R.rays, R.pipe_rays, 0);
    if (ao && rs->ao_half) draw_full(&R.ao, R.pipe_ao, 0);
    if (atmos) {
        rb_bind_tex(B_COLOR, R.scene.color, RB_SMP_NEAREST);
        rb_bind_tex(B_RAYS, R.rays.color, RB_SMP_NEAREST);
        rb_bind_tex(B_AO, R.ao.color, RB_SMP_NEAREST);
        draw_full(&R.atmos, R.pipe_atmos, rate);
    }
    pass_mark(4);
    if (timing) mark(4);

    // 4. Фильтр: виньетка, зерно, сканлайны, аберрация; сразу в окно
    rb_bind_tex(B_COLOR, atmos ? R.atmos.color : R.scene.color, RB_SMP_NEAREST);
    rb_pass_begin(&(RbPass){ 0 });
    rb_bind_pipe(R.pipe_style);
    rb_draw(3, 0, 1);
    rb_pass_end();
    pass_mark(5);
    if (timing) mark(5);
}

void render_frame_end(void)
{
    if (R.cpu_t[0] > 0.0) {
        pass_mark(6);
        for (u32 p = 0; p < PASS_COUNT; p++) R.stat_cpu[p] += R.cpu_t[p + 1] - R.cpu_t[p];
        R.cpu_t[0] = 0.0;
    }
    if (R.timing) {
        mark(6);
        R.timer_pending[R.timer_slot] = true;
        R.timer_frame[R.timer_slot] = R.frame_no;
        R.timing = false;
    }
    R.frame_no++;
    R.timer_slot = (R.timer_slot + 1) % TIMER_FRAMES;
    // Кадр уходит на GPU сразу: при swap после логики GPU рисует, пока CPU считает следующий
    rb_frame_end();
    // Старые кадры, до которых очередь дойдёт не скоро, забираем сразу
    // по порядку от самого старого, чтобы соседние кадры шли подряд
    for (u32 i = 0; i < TIMER_FRAMES; i++) timer_collect((R.timer_slot + i) % TIMER_FRAMES);
}

void render_read_pixels(u8 *rgb, i32 w, i32 h) { rb_read_window(rgb, w, h); }

void render_portrait(u32 slot, i32 size, const FrameView *fv, M4 view_proj, const DrawItem *items, u32 count)
{
    if (slot >= RENDER_PORTRAIT_SLOTS || !R.mesh_pipe[PS_PORTRAIT][LAY_MESH][0]) return;
    size = CLAMP(size, 8, RENDER_PORTRAIT_PX);
    // Шейдер сцены со своим неподвижным светом, без солнца и теней мира; остальное — как в последнем кадре
    GpuFrame f = R.frame;
    v3_to(f.cam_pos, fv->cam_pos);
    f.time = fv->time;
    f.unlit = 1;
    frame_write(1 + slot, &f);
    i32 x = (i32)slot * RENDER_PORTRAIT_PX;
    pass_to(&R.portrait, false, NULL, 0);
    rb_viewport(x, 0, size, size);
    rb_scissor(x, 0, size, size);
    rb_clear_rect(x, 0, size, size, (f32[4]){ 0, 0, 0, 0 }, true);
    R.pset = PS_PORTRAIT;
    push_view_proj(view_proj);
    count = MIN(count, (u32)RENDER_MAX_ITEMS);
    stream_poses(items, count);
    PassView pv = { .fr = frustum_from(view_proj), .full = true };
    u32 n = gather(items, count, &pv, false, g_inst[0]);
    u32 base = stream_insts(g_inst[0], &n);
    draw_insts(g_inst[0], n, base, 2);
    rb_pass_end();
}

RbTex render_portrait_texture(void) { return R.portrait.color; }
