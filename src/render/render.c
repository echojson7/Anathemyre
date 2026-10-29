#include "render.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/clock.h"
#include "../core/log.h"
#include "gl.h"

#define SHADOW_SIZE 2048
#define SHADOW_EXTENT 45.0f   // полуразмер области теней, м
// Размер пикселя по глубине (pixel.glsl): k = PIXEL_K * s^PIXEL_P, s — пикселей кадра на метр в точке.
// Шаг 0.5 пикселя, ниже 2 — 0.25; ближе ~6 м рост вдвое медленнее (pixel.glsl PIXEL_KNEE).
// При 1080p: 2 м — 4.5, 3 м — 3, 4 м — 2.5, 5–6 м — 2, 7 м — 1.75, 8 м — 1.5, с 9 м — 1.25 и дальше не меняется
#define PIXEL_K 0.00728f
#define PIXEL_P 1.1f

// Расположения uniform задаются явно в GLSL (layout(location = N))
enum {
    U_VIEW_PROJ = 0, U_SKINNED = 17, U_LIGHT_VP = 18, U_INST_BASE = 19,
    U_CAM_POS = 20, U_SUN_DIR = 21, U_SUN_COLOR = 22, U_TIME = 23, U_SKY_AMB = 24, U_GROUND_AMB = 25, U_UNLIT = 26, U_TEXEL = 27, U_MORPHS = 28,
    U_FADE = 29,
    U_A_INV_VP = 30, U_A_CAM = 31, U_A_SUN_DIR = 32, U_A_SUN_COLOR = 33,
    U_A_LIGHT_VP = 34, U_A_TIME = 35, U_A_FRAME = 36, U_A_PROJ_SCALE = 37, U_A_HAZE = 38, U_A_ZENITH = 39, U_A_AO_HALF = 47, U_NOISE_TEX = 48,
    U_S_POST = 41, U_S_FRAME = 42, U_U_OUT = 43, U_S_PIX = 44, U_V_TILE = 45, U_S_PIX_INT = 46,
};

// Буферы хранения (layout(binding = N) в scene.vert)
enum { SB_BONES = 0, SB_INST = 1, SB_MORPH = 2, SB_STATIC_INST = 3 };
// Буфер констант со светом кадра (common.glsl, блок Lights)
#define UB_LIGHTS 0
#define SKIN_STATIC 0
#define SKIN_BONES 1
#define SKIN_INSTANCED 2
// Разных сеток среди экземпляров прохода (с учётом уровней детализации)
#define MAX_INST_MESHES 256

// Кости и списки экземпляров пишутся каждый кадр; кольцо буферов, чтобы не ждать GPU с прошлым кадром
#define STREAM_FRAMES 3
#define NO_POSE 0xFFFFFFFFu

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

typedef struct Target {
    u32 fbo, color, depth;
    i32 w, h;
} Target;

static struct {
    Arena *scratch;
    char   data_dir[512];
    u32    prog_scene, prog_shadow, prog_rays, prog_atmos, prog_style, prog_upscale, prog_src_map, prog_vrs_block, prog_vrs, prog_level, prog_level_max, prog_ao;
    u32    noise_tex;  // значения hash13 в узлах решётки NOISE_N³: noise3 одной выборкой (common.glsl)
    u32    empty_vao;
    u32    shadow_fbo, shadow_tex;
    Target scene, rays, atmos, ldr;
    Target ao;        // AO в половинном разрешении (RG16F), ao.frag
    Target level;     // размер пикселя каждой точки (R8UI), level.frag
    Target level_max; // самый крупный размер в блоке 8x8 (R8UI), level_max.frag
    Target src_map;   // откуда каждый пиксель берёт цвет (RG16UI), src_map.frag
    Target block;     // частота шейдинга блоков 4x4 (R8UI)
    Target rate;      // карта частоты шейдинга (R8UI), клетка vrs_tile пикселей
    bool   vrs;       // есть GL_NV_shading_rate_image
    i32    vrs_tile[2];
    Target portrait;  // атлас портретов: ячейки RENDER_PORTRAIT_PX в ряд, HDR
    u32    timer[TIMER_FRAMES][TIMER_MARKS];
    bool   timer_pending[TIMER_FRAMES];
    u32    timer_slot;
    bool   timing;    // текущий кадр пишет метки времени
    f64    stat_ms[PASS_COUNT];
    u32    stat_samples;
    u32    bone_buf[STREAM_FRAMES], inst_buf[STREAM_FRAMES];
    u32    light_buf[STREAM_FRAMES];
    u32    stream;
    u32    used_poses, used_insts;   // занято в буферах текущего кадра (кадр, затем портреты)
    f64    stat_draws[2], stat_tris[2], stat_visible[2], stat_prep, stat_poses;
    bool   stat_vrs;
    u32    stat_frames, stat_items;
    bool   warned_items, warned_poses, warned_inst;
    u32    static_inst_buf;          // постоянный буфер RInst
    const RInst *inst_cpu;           // его копия для отсечения
    u32    inst_total;
    const InstGroup *groups;
    const InstCluster *clusters;
    u32    cluster_count;
    V3     cam_eye;                  // для прореживания по дальности во всех проходах
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

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        log_error("ШЕЙДЕР: не открыт %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = arena_push(R.scratch, (size_t)n + 1, 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return NULL;
    }
    buf[n] = 0;
    fclose(f);
    return buf;
}

static u32 compile(GLenum type, const char *defines, const char *const *files, u32 nfiles)
{
    const char *src[8];
    u32 n = 0;
    src[n++] = "#version 450 core\n";
    src[n++] = defines ? defines : "";
    for (u32 i = 0; i < nfiles && n < ARRAY_COUNT(src); i++) {
        char path[640];
        snprintf(path, sizeof path, "%s/shaders/%s", R.data_dir, files[i]);
        src[n] = read_file(path);
        if (!src[n]) return 0;
        n++;
    }
    u32 sh = glCreateShader(type);
    glShaderSource(sh, (GLsizei)n, src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof log, NULL, log);
        log_error("ШЕЙДЕР %s: %s", files[nfiles - 1], log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static u32 link(u32 vs, u32 fs)
{
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    u32 p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        log_error("ПРОГРАММА: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

bool render_reload_shaders(void)
{
    size_t mark = R.scratch->used;
    const char *scene_vs[] = { "scene.vert" };
    const char *scene_fs[] = { "common.glsl", "scene.frag" };
    const char *shadow_fs[] = { "shadow.frag" };
    const char *full_vs[] = { "fullscreen.vert" };
    const char *rays_fs[] = { "common.glsl", "rays.frag" };
    const char *atmos_fs[] = { "common.glsl", "ao.glsl", "atmos.frag" };
    const char *ao_fs[] = { "common.glsl", "ao.glsl", "ao.frag" };
    const char *style_fs[] = { "common.glsl", "pixel.glsl", "style.frag" };
    const char *src_map_fs[] = { "pixel.glsl", "src_map.frag" };
    const char *vrs_block_fs[] = { "vrs_block.frag" };
    const char *level_fs[] = { "pixel.glsl", "level.frag" };
    const char *level_max_fs[] = { "level_max.frag" };
    const char *vrs_fs[] = { "vrs.frag" };
    const char *upscale_fs[] = { "upscale.frag" };

    const char *vrs_def = R.vrs ? "#extension GL_NV_shading_rate_image : require\n#define VRS\n" : NULL;
    u32 scene = link(compile(GL_VERTEX_SHADER, NULL, scene_vs, 1), compile(GL_FRAGMENT_SHADER, vrs_def, scene_fs, 2));
    if (!scene && R.vrs) {
        // Драйвер заявил расширение, но шейдер с ним не собрался: считаем каждый пиксель
        log_error("РЕНДЕР: шейдинг блоками отключён, шейдер сцены без него");
        R.vrs = false;
        scene = link(compile(GL_VERTEX_SHADER, NULL, scene_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, scene_fs, 2));
    }
    u32 shadow = link(compile(GL_VERTEX_SHADER, "#define SHADOW_PASS\n", scene_vs, 1),
                      compile(GL_FRAGMENT_SHADER, NULL, shadow_fs, 1));
    u32 rays = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, rays_fs, 2));
    u32 atmos = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, atmos_fs, 3));
    u32 ao = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, ao_fs, 3));
    u32 style = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, style_fs, 3));
    u32 src_map = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, src_map_fs, 2));
    u32 level = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, level_fs, 2));
    u32 level_max = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, level_max_fs, 1));
    u32 vrs_block = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, vrs_block_fs, 1));
    u32 vrs = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, vrs_fs, 1));
    u32 upscale = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, upscale_fs, 1));
    R.scratch->used = mark;

    u32 progs[] = { scene, shadow, rays, atmos, style, upscale, src_map, vrs_block, vrs, level, level_max, ao };
    u32 *slots[] = { &R.prog_scene, &R.prog_shadow, &R.prog_rays, &R.prog_atmos, &R.prog_style, &R.prog_upscale,
                     &R.prog_src_map, &R.prog_vrs_block, &R.prog_vrs, &R.prog_level, &R.prog_level_max, &R.prog_ao };
    bool ok = true;
    for (u32 i = 0; i < ARRAY_COUNT(progs); i++) ok = ok && progs[i];
    for (u32 i = 0; i < ARRAY_COUNT(progs); i++) {
        u32 *old = ok ? slots[i] : &progs[i];
        if (*old) glDeleteProgram(*old);
        if (ok) *slots[i] = progs[i];
    }
    if (!ok) return false;
    log_info("ШЕЙДЕРЫ: загружены");
    return true;
}

static void GLAPIENTRY gl_debug_cb(GLenum source, GLenum type, GLuint id, GLenum severity,
                                   GLsizei length, const GLchar *message, const void *user)
{
    (void)source; (void)type; (void)id; (void)length; (void)user;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    log_error("GL: %s", message);
}

static u32 make_tex(GLenum fmt, i32 w, i32 h, GLenum filter)
{
    u32 t;
    glCreateTextures(GL_TEXTURE_2D, 1, &t);
    glTextureStorage2D(t, 1, fmt, w, h);
    glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, (GLint)filter);
    glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, (GLint)filter);
    glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static void target_free(Target *t)
{
    if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
    if (t->color) glDeleteTextures(1, &t->color);
    if (t->depth) glDeleteTextures(1, &t->depth);
    memset(t, 0, sizeof *t);
}

static bool target_make(Target *t, i32 w, i32 h, GLenum color_fmt, bool depth)
{
    target_free(t);
    t->w = w;
    t->h = h;
    glCreateFramebuffers(1, &t->fbo);
    t->color = make_tex(color_fmt, w, h, GL_NEAREST);
    glNamedFramebufferTexture(t->fbo, GL_COLOR_ATTACHMENT0, t->color, 0);
    if (depth) {
        t->depth = make_tex(GL_DEPTH_COMPONENT32F, w, h, GL_NEAREST);
        glNamedFramebufferTexture(t->fbo, GL_DEPTH_ATTACHMENT, t->depth, 0);
    }
    if (glCheckNamedFramebufferStatus(t->fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log_error("FBO: неполный %dx%d", w, h);
        return false;
    }
    return true;
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
    glCreateTextures(GL_TEXTURE_3D, 1, &R.noise_tex);
    glTextureStorage3D(R.noise_tex, 1, GL_R16, NOISE_N, NOISE_N, NOISE_N);
    for (u32 z = 0; z < NOISE_N; z++) {
        for (u32 y = 0; y < NOISE_N; y++)
            for (u32 x = 0; x < NOISE_N; x++)
                slice[y * NOISE_N + x] = (u16)(hash13_c((f32)x, (f32)y, (f32)z) * 65535.0f + 0.5f);
        glTextureSubImage3D(R.noise_tex, 0, 0, 0, (GLint)z, NOISE_N, NOISE_N, 1, GL_RED, GL_UNSIGNED_SHORT, slice);
    }
    glTextureParameteri(R.noise_tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(R.noise_tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(R.noise_tex, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(R.noise_tex, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(R.noise_tex, GL_TEXTURE_WRAP_R, GL_REPEAT);
    glBindTextureUnit(7, R.noise_tex);  // блок 7 больше никто не занимает
}

bool render_init(Arena *scratch, const char *data_dir)
{
    R.scratch = scratch;
    snprintf(R.data_dir, sizeof R.data_dir, "%s", data_dir);
    if (!gl_load()) return false;
    log_info("GL: %s | %s", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
#ifndef NDEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(gl_debug_cb, NULL);
#else
    (void)gl_debug_cb;
#endif
    // Частота шейдинга по блокам: вблизи фильтр всё равно сливает пиксели в блоки 2x2 и 4x4
    R.vrs = gl_has_extension("GL_NV_shading_rate_image") && glBindShadingRateImageNV &&
            glShadingRateImagePaletteNV && glShadingRateImageBarrierNV;
    if (R.vrs) {
        glGetIntegerv(GL_SHADING_RATE_IMAGE_TEXEL_WIDTH_NV, &R.vrs_tile[0]);
        glGetIntegerv(GL_SHADING_RATE_IMAGE_TEXEL_HEIGHT_NV, &R.vrs_tile[1]);
        // Клетка карты должна вмещать целые ячейки 4x4
        R.vrs = R.vrs_tile[0] >= 4 && R.vrs_tile[1] >= 4 && R.vrs_tile[0] % 4 == 0 && R.vrs_tile[1] % 4 == 0;
    }
    if (R.vrs) {
        const GLenum rates[3] = { GL_SHADING_RATE_1_INVOCATION_PER_PIXEL_NV, GL_SHADING_RATE_1_INVOCATION_PER_2X2_PIXELS_NV,
                                  GL_SHADING_RATE_1_INVOCATION_PER_4X4_PIXELS_NV };
        glShadingRateImagePaletteNV(0, 0, 3, rates);
    }
    log_info("РЕНДЕР: частота шейдинга по блокам %s", R.vrs ? "есть" : "нет (каждый пиксель)");
    if (!render_reload_shaders()) return false;

    glCreateVertexArrays(1, &R.empty_vao);
    noise_make();
    for (u32 i = 0; i < STREAM_FRAMES; i++) {
        glCreateBuffers(1, &R.bone_buf[i]);
        glNamedBufferStorage(R.bone_buf[i], (GLsizeiptr)sizeof g_bones, NULL, GL_DYNAMIC_STORAGE_BIT);
        glCreateBuffers(1, &R.inst_buf[i]);
        glNamedBufferStorage(R.inst_buf[i], (GLsizeiptr)sizeof g_inst_pose, NULL, GL_DYNAMIC_STORAGE_BIT);
        glCreateBuffers(1, &R.light_buf[i]);
        glNamedBufferStorage(R.light_buf[i], (GLsizeiptr)sizeof g_lights, NULL, GL_DYNAMIC_STORAGE_BIT);
    }
    for (u32 i = 0; i < TIMER_FRAMES; i++) glCreateQueries(GL_TIMESTAMP, TIMER_MARKS, R.timer[i]);

    R.shadow_tex = make_tex(GL_DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE, GL_LINEAR);
    glTextureParameteri(R.shadow_tex, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(R.shadow_tex, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glCreateFramebuffers(1, &R.shadow_fbo);
    glNamedFramebufferTexture(R.shadow_fbo, GL_DEPTH_ATTACHMENT, R.shadow_tex, 0);
    glNamedFramebufferDrawBuffer(R.shadow_fbo, GL_NONE);
    if (glCheckNamedFramebufferStatus(R.shadow_fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log_error("FBO: теневая карта неполна");
        return false;
    }
    return target_make(&R.portrait, RENDER_PORTRAIT_PX * RENDER_PORTRAIT_SLOTS, RENDER_PORTRAIT_PX, GL_RGBA16F, true);
}

void render_shutdown(void)
{
    target_free(&R.scene);
    target_free(&R.rays);
    target_free(&R.ao);
    if (R.noise_tex) glDeleteTextures(1, &R.noise_tex);
    target_free(&R.atmos);
    target_free(&R.ldr);
    target_free(&R.src_map);
    target_free(&R.level);
    target_free(&R.level_max);
    target_free(&R.block);
    target_free(&R.rate);
    target_free(&R.portrait);
    if (R.shadow_fbo) glDeleteFramebuffers(1, &R.shadow_fbo);
    if (R.shadow_tex) glDeleteTextures(1, &R.shadow_tex);
    if (R.empty_vao) glDeleteVertexArrays(1, &R.empty_vao);
    for (u32 i = 0; i < STREAM_FRAMES; i++) {
        if (R.bone_buf[i]) glDeleteBuffers(1, &R.bone_buf[i]);
        if (R.inst_buf[i]) glDeleteBuffers(1, &R.inst_buf[i]);
        if (R.light_buf[i]) glDeleteBuffers(1, &R.light_buf[i]);
    }
    if (R.static_inst_buf) glDeleteBuffers(1, &R.static_inst_buf);
    for (u32 i = 0; i < TIMER_FRAMES; i++)
        if (R.timer[i][0]) glDeleteQueries(TIMER_MARKS, R.timer[i]);
    u32 progs[] = { R.prog_scene, R.prog_shadow, R.prog_rays, R.prog_atmos, R.prog_style, R.prog_upscale,
                    R.prog_src_map, R.prog_vrs_block, R.prog_vrs, R.prog_level, R.prog_level_max, R.prog_ao };
    for (u32 i = 0; i < ARRAY_COUNT(progs); i++)
        if (progs[i]) glDeleteProgram(progs[i]);
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

bool gpu_mesh_upload(GpuMesh *gm, const MeshBuilder *mb)
{
    glCreateBuffers(1, &gm->vbo);
    glCreateBuffers(1, &gm->ibo);
    glNamedBufferStorage(gm->vbo, (GLsizeiptr)(mb->vcount * sizeof(Vertex)), mb->v, 0);
    glNamedBufferStorage(gm->ibo, (GLsizeiptr)(mb->icount * sizeof(u32)), mb->idx, 0);
    glCreateVertexArrays(1, &gm->vao);
    glVertexArrayVertexBuffer(gm->vao, 0, gm->vbo, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(gm->vao, gm->ibo);
    glEnableVertexArrayAttrib(gm->vao, 0);
    glEnableVertexArrayAttrib(gm->vao, 1);
    glEnableVertexArrayAttrib(gm->vao, 2);
    glEnableVertexArrayAttrib(gm->vao, 3);
    glVertexArrayAttribFormat(gm->vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, pos));
    glVertexArrayAttribFormat(gm->vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, nrm));
    glVertexArrayAttribFormat(gm->vao, 2, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(Vertex, color));
    glVertexArrayAttribIFormat(gm->vao, 3, 4, GL_UNSIGNED_BYTE, offsetof(Vertex, mat));
    for (u32 i = 0; i < 4; i++) glVertexArrayAttribBinding(gm->vao, i, 0);
    gm->skin_vbo = 0;
    if (mb->skin) {
        // Без этого потока вес первой кости в шейдере 0 (значение атрибута по умолчанию) — жёсткий скиннинг
        glCreateBuffers(1, &gm->skin_vbo);
        glNamedBufferStorage(gm->skin_vbo, (GLsizeiptr)(mb->vcount * sizeof(VertexSkin)), mb->skin, 0);
        glVertexArrayVertexBuffer(gm->vao, 1, gm->skin_vbo, 0, sizeof(VertexSkin));
        glEnableVertexArrayAttrib(gm->vao, 4);
        glEnableVertexArrayAttrib(gm->vao, 5);
        glVertexArrayAttribIFormat(gm->vao, 4, 4, GL_UNSIGNED_BYTE, offsetof(VertexSkin, bone));
        glVertexArrayAttribFormat(gm->vao, 5, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(VertexSkin, weight));
        glVertexArrayAttribBinding(gm->vao, 4, 1);
        glVertexArrayAttribBinding(gm->vao, 5, 1);
    }
    gm->morph_buf = 0;
    gm->morph_count = mb->morph ? mb->morph_count : 0;
    if (gm->morph_count) {
        glCreateBuffers(1, &gm->morph_buf);
        glNamedBufferStorage(gm->morph_buf, (GLsizeiptr)((size_t)mb->vcount * gm->morph_count * 3 * sizeof(u32)), mb->morph, 0);
    }
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
    return true;
}

void gpu_mesh_set_lod(GpuMesh *fine, const GpuMesh *coarse, f32 min_px)
{
    fine->lod_next = coarse;
    fine->lod_px = min_px;
}

_Static_assert(sizeof(RInst) == 32, "RInst — 32 байта, как InstRec в scene.vert");

bool render_upload_instances(const RInst *inst, u32 count)
{
    if (R.static_inst_buf) glDeleteBuffers(1, &R.static_inst_buf);
    R.static_inst_buf = 0;
    R.inst_cpu = inst;
    R.inst_total = count;
    R.groups = NULL;
    R.clusters = NULL;
    R.cluster_count = 0;
    if (!count) return true;
    glCreateBuffers(1, &R.static_inst_buf);
    glNamedBufferStorage(R.static_inst_buf, (GLsizeiptr)(sizeof(RInst) * count), inst, 0);
    return true;
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
    if (!R.timer_pending[slot]) return;
    GLuint ready = 0;
    glGetQueryObjectuiv(R.timer[slot][TIMER_MARKS - 1], GL_QUERY_RESULT_AVAILABLE, &ready);
    if (!ready) return;
    GLuint64 t[TIMER_MARKS];
    for (u32 i = 0; i < TIMER_MARKS; i++) glGetQueryObjectui64v(R.timer[slot][i], GL_QUERY_RESULT, &t[i]);
    for (u32 p = 0; p < PASS_COUNT; p++) R.stat_ms[p] += (f64)(t[p + 1] - t[p]) * 1e-6;
    R.stat_samples++;
    R.timer_pending[slot] = false;
}

static void mark(u32 i)
{
    glQueryCounter(R.timer[R.timer_slot][i], GL_TIMESTAMP);
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
    memset(R.stat_ms, 0, sizeof R.stat_ms);
    R.stat_samples = 0;
    f64 k = R.stat_frames ? 1.0 / R.stat_frames : 0.0;
    for (u32 p = 0; p < 2; p++) {
        out->draws[p] = R.stat_draws[p] * k;
        out->tris[p] = R.stat_tris[p] * k;
        out->visible[p] = R.stat_visible[p] * k;
    }
    out->items = R.stat_items;
    out->prep_ms = R.stat_prep * 1000.0 * k;
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

// Статика — по вызову на сетку, скелетные — один вызов на все экземпляры одной сетки
static void draw_insts(const Inst *in, u32 n, u32 base, u32 pass)
{
    u32 draws = 0;
    f64 tris = 0;
    for (u32 i = 0; i < n;) {
        const GpuMesh *m = in[i].mesh;
        u32 j = i + 1;
        if (in[i].pose != NO_POSE)
            while (j < n && in[j].mesh == m && in[j].pose != NO_POSE) j++;
        glBindVertexArray(m->vao);
        if (in[i].pose == NO_POSE) {
            glUniform1i(U_SKINNED, SKIN_STATIC);
            glDrawElements(GL_TRIANGLES, (GLsizei)m->index_count, GL_UNSIGNED_INT, NULL);
        } else {
            glUniform1i(U_SKINNED, SKIN_BONES);
            glUniform1i(U_INST_BASE, (GLint)(base + i));
            glUniform1i(U_MORPHS, (GLint)m->morph_count);
            if (m->morph_count) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SB_MORPH, m->morph_buf);
            glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)m->index_count, GL_UNSIGNED_INT, NULL, (GLsizei)(j - i));
        }
        draws++;
        tris += (f64)(m->index_count / 3) * (f64)(j - i);
        i = j;
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
        glNamedBufferSubData(R.inst_buf[R.stream], (GLintptr)(sizeof(u32) * base), (GLsizeiptr)(sizeof(u32) * nv),
                             &g_inst_pose[base]);
    R.used_insts += nv;
    return ga.nb;
}

static void draw_groups(const InstDraw *draws, u32 n, u32 pass)
{
    if (!n) return;
    glUniform1i(U_SKINNED, SKIN_INSTANCED);
    f64 tris = 0, vis = 0;
    for (u32 i = 0; i < n; i++) {
        const InstDraw *d = &draws[i];
        if (!d->count) continue;
        const GpuMesh *m = d->mesh;
        glBindVertexArray(m->vao);
        if (m->two_sided) glDisable(GL_CULL_FACE);
        glUniform1i(U_INST_BASE, (GLint)d->base);
        glUniform2f(U_FADE, m->fade[0], m->fade[1]);
        glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)m->index_count, GL_UNSIGNED_INT, NULL, (GLsizei)d->count);
        if (m->two_sided) glEnable(GL_CULL_FACE);
        tris += (f64)(m->index_count / 3) * d->count;
        vis += d->count;
    }
    if (pass >= 2) return;
    R.stat_draws[pass] += n;
    R.stat_tris[pass] += tris;
    R.stat_visible[pass] += vis;
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
        glNamedBufferSubData(R.bone_buf[R.stream], (GLintptr)(sizeof(M4) * RENDER_MAX_BONES * first),
                             (GLsizeiptr)(sizeof(M4) * RENDER_MAX_BONES * (poses - first)),
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
        glNamedBufferSubData(R.inst_buf[R.stream], (GLintptr)(sizeof(u32) * base), (GLsizeiptr)(sizeof(u32) * *n),
                             &g_inst_pose[base]);
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
    glNamedBufferSubData(R.light_buf[R.stream], 0, (GLsizeiptr)sizeof g_lights, &g_lights);
    glBindBufferBase(GL_UNIFORM_BUFFER, UB_LIGHTS, R.light_buf[R.stream]);
}

void render_frame(const FrameView *fv, const DrawItem *items, u32 count, const RenderSettings *rs, i32 win_w, i32 win_h)
{
    if (win_w <= 0 || win_h <= 0 || !R.prog_scene) return;
    i32 ih = CLAMP(rs->internal_h, 120, win_h);
    i32 iw = (i32)((f32)ih * (f32)win_w / (f32)win_h + 0.5f);
    if (R.scene.w != iw || R.scene.h != ih) {
        target_make(&R.scene, iw, ih, GL_RGBA16F, true);
        target_make(&R.rays, (iw + 1) / 2, (ih + 1) / 2, GL_RG16F, false);
        target_make(&R.ao, (iw + 1) / 2, (ih + 1) / 2, GL_RG16F, false);
        target_make(&R.atmos, iw, ih, GL_RGBA16F, false);
        target_make(&R.ldr, iw, ih, GL_RGBA8, false);
        glTextureParameteri(R.ldr.color, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(R.ldr.color, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        target_make(&R.src_map, iw, ih, GL_RG16UI, false);
        target_make(&R.level, iw, ih, GL_R8UI, false);
        target_make(&R.level_max, (iw + 7) / 8, (ih + 7) / 8, GL_R8UI, false);
        if (R.vrs && !(target_make(&R.block, (iw + 3) / 4, (ih + 3) / 4, GL_R8UI, false) &&
                       target_make(&R.rate, (iw + R.vrs_tile[0] - 1) / R.vrs_tile[0], (ih + R.vrs_tile[1] - 1) / R.vrs_tile[1],
                                   GL_R8UI, false)))
            R.vrs = false;
        log_info("РЕНДЕР: внутреннее разрешение %dx%d -> окно %dx%d", iw, ih, win_w, win_h);
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
    R.stream = (R.stream + 1) % STREAM_FRAMES;
    R.used_poses = R.used_insts = 0;
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
    stream_lights(fv, &scene_view.fr);
    u32 n_shadow = gather(items, count, &shadow_view, true, g_inst[0]);
    u32 n_scene = gather(items, count, &scene_view, false, g_inst[1]);
    u32 base_shadow = stream_insts(g_inst[0], &n_shadow);
    u32 base_scene = stream_insts(g_inst[1], &n_scene);
    u32 ng_shadow = gather_groups(&shadow_view, true, g_idraw[0]);
    u32 ng_scene = gather_groups(&scene_view, false, g_idraw[1]);
    R.stat_prep += time_now() - t_prep;
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SB_BONES, R.bone_buf[R.stream]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SB_INST, R.inst_buf[R.stream]);
    if (R.static_inst_buf) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SB_STATIC_INST, R.static_inst_buf);
    R.stat_frames++;
    R.stat_items = count;

    // 1. Тени
    glBindFramebuffer(GL_FRAMEBUFFER, R.shadow_fbo);
    glViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(R.prog_shadow);
    glUniformMatrix4fv(U_VIEW_PROJ, 1, GL_FALSE, light_vp.m);
    // Ветер и прореживание в вершинном шейдере: те же значения во всех проходах
    glUniform3f(U_CAM_POS, fv->cam_pos.x, fv->cam_pos.y, fv->cam_pos.z);
    glUniform1f(U_TIME, fv->time);
    draw_insts(g_inst[0], n_shadow, base_shadow, 0);
    draw_groups(g_idraw[0], ng_shadow, 0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    if (timing) mark(1);

    // 2. Сцена в низком разрешении
    glBindFramebuffer(GL_FRAMEBUFFER, R.scene.fbo);
    glViewport(0, 0, iw, ih);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // Сначала только глубина: тяжёлый шейдер материалов затем считает каждый пиксель один раз
    glUseProgram(R.prog_shadow);
    glUniformMatrix4fv(U_VIEW_PROJ, 1, GL_FALSE, vp.m);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    draw_insts(g_inst[1], n_scene, base_scene, 2);
    draw_groups(g_idraw[1], ng_scene, 2);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    // Размер пикселя на дальности 1 м; s^P = (px_m / d)^P = PIXEL_K * px_m^P * (1/d)^P
    f32 px_k = rs->pixel && rs->pixel_div <= 1 ? PIXEL_K * powf(fv->proj.m[5] * 0.5f * (f32)ih, PIXEL_P) : 0.0f;
    bool vrs = R.vrs && rs->vrs && rs->pixel;
    R.stat_vrs = vrs;
    // По готовой глубине: размер пикселя в каждой точке и его максимум по блокам 8x8,
    // затем откуда каждый пиксель берёт цвет, раз за кадр (фильтр и карта частоты)
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(R.empty_vao);
    glBindTextureUnit(1, R.scene.depth);
    glBindFramebuffer(GL_FRAMEBUFFER, R.level.fbo);
    glUseProgram(R.prog_level);
    glUniform4f(U_S_PIX, px_k, PIXEL_P, fv->proj.m[10], fv->proj.m[14]);
    glUniform1i(U_S_PIX_INT, rs->pixel_int ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, R.level_max.fbo);
    glViewport(0, 0, R.level_max.w, R.level_max.h);
    glUseProgram(R.prog_level_max);
    glBindTextureUnit(4, R.level.color);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, R.src_map.fbo);
    glViewport(0, 0, iw, ih);
    glUseProgram(R.prog_src_map);
    glUniform4f(U_S_PIX, px_k, PIXEL_P, fv->proj.m[10], fv->proj.m[14]);
    glUniform1i(U_S_PIX_INT, rs->pixel_int ? 1 : 0);
    glBindTextureUnit(5, R.level_max.color);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (vrs) {
        // Где фильтр сольёт пиксели в блоки, материалы и атмосфера считаются раз на блок
        glBindFramebuffer(GL_FRAMEBUFFER, R.block.fbo);
        glViewport(0, 0, R.block.w, R.block.h);
        glUseProgram(R.prog_vrs_block);
        glBindTextureUnit(2, R.src_map.color);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindFramebuffer(GL_FRAMEBUFFER, R.rate.fbo);
        glViewport(0, 0, R.rate.w, R.rate.h);
        glUseProgram(R.prog_vrs);
        glUniform2i(U_V_TILE, R.vrs_tile[0] / 4, R.vrs_tile[1] / 4);
        glBindTextureUnit(3, R.block.color);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glShadingRateImageBarrierNV(GL_TRUE);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, R.scene.fbo);
    glViewport(0, 0, iw, ih);
    glEnable(GL_DEPTH_TEST);
    if (vrs) {
        glBindShadingRateImageNV(R.rate.color);
        glEnable(GL_SHADING_RATE_IMAGE_NV);
    }
    if (timing) mark(2);
    glUseProgram(R.prog_scene);
    glUniform1i(U_NOISE_TEX, rs->noise_tex ? 1 : 0);
    glUniformMatrix4fv(U_VIEW_PROJ, 1, GL_FALSE, vp.m);
    glUniformMatrix4fv(U_LIGHT_VP, 1, GL_FALSE, light_vp.m);
    glUniform3f(U_CAM_POS, fv->cam_pos.x, fv->cam_pos.y, fv->cam_pos.z);
    glUniform3f(U_SUN_DIR, L.x, L.y, L.z);
    glUniform3f(U_SUN_COLOR, fv->sun_color.x, fv->sun_color.y, fv->sun_color.z);
    glUniform1f(U_TIME, fv->time);
    glUniform3f(U_SKY_AMB, fv->sky_amb.x, fv->sky_amb.y, fv->sky_amb.z);
    glUniform3f(U_GROUND_AMB, fv->ground_amb.x, fv->ground_amb.y, fv->ground_amb.z);
    glUniform1f(U_TEXEL, rs->pixel ? 1.0f : 0.0f);
    glBindTextureUnit(0, R.shadow_tex);
    draw_insts(g_inst[1], n_scene, base_scene, 1);
    draw_groups(g_idraw[1], ng_scene, 1);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    if (timing) mark(3);

    // 3. Атмосфера. Сначала объёмный свет в половинном разрешении, затем небо, туман и AO
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(R.empty_vao);
    M4 inv_vp = m4_inverse(vp);
    glBindFramebuffer(GL_FRAMEBUFFER, R.rays.fbo);
    glViewport(0, 0, R.rays.w, R.rays.h);
    glUseProgram(R.prog_rays);
    glUniformMatrix4fv(U_A_INV_VP, 1, GL_FALSE, inv_vp.m);
    glUniform3f(U_A_CAM, fv->cam_pos.x, fv->cam_pos.y, fv->cam_pos.z);
    glUniformMatrix4fv(U_A_LIGHT_VP, 1, GL_FALSE, light_vp.m);
    glUniform1f(U_A_TIME, fv->time);
    glUniform1i(U_A_FRAME, (GLint)fv->frame);
    glBindTextureUnit(1, R.scene.depth);
    glBindTextureUnit(2, R.shadow_tex);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (rs->ao_half) {
        // AO в половинном разрешении; блоки сцены к этой цели не относятся
        if (vrs) glDisable(GL_SHADING_RATE_IMAGE_NV);
        glBindFramebuffer(GL_FRAMEBUFFER, R.ao.fbo);
        glUseProgram(R.prog_ao);
        glUniformMatrix4fv(U_A_INV_VP, 1, GL_FALSE, inv_vp.m);
        glUniform3f(U_A_CAM, fv->cam_pos.x, fv->cam_pos.y, fv->cam_pos.z);
        glUniform1i(U_A_FRAME, (GLint)fv->frame);
        glUniform1f(U_A_PROJ_SCALE, fv->proj.m[5] * 0.5f * (f32)ih);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        if (vrs) glEnable(GL_SHADING_RATE_IMAGE_NV);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, R.atmos.fbo);
    glViewport(0, 0, iw, ih);
    glUseProgram(R.prog_atmos);
    glUniform1i(U_NOISE_TEX, rs->noise_tex ? 1 : 0);
    glUniformMatrix4fv(U_A_INV_VP, 1, GL_FALSE, inv_vp.m);
    glUniform3f(U_A_CAM, fv->cam_pos.x, fv->cam_pos.y, fv->cam_pos.z);
    glUniform3f(U_A_SUN_DIR, L.x, L.y, L.z);
    glUniform3f(U_A_SUN_COLOR, fv->sun_color.x, fv->sun_color.y, fv->sun_color.z);
    glUniform1f(U_A_TIME, fv->time);
    glUniform1i(U_A_FRAME, (GLint)fv->frame);
    glUniform1f(U_A_PROJ_SCALE, fv->proj.m[5] * 0.5f * (f32)ih);
    glUniform3f(U_A_HAZE, fv->haze.x, fv->haze.y, fv->haze.z);
    glUniform3f(U_A_ZENITH, fv->zenith.x, fv->zenith.y, fv->zenith.z);
    glBindTextureUnit(0, R.scene.color);
    glBindTextureUnit(1, R.scene.depth);
    glUniform1i(U_A_AO_HALF, rs->ao_half ? 1 : 0);
    glBindTextureUnit(3, R.rays.color);
    glBindTextureUnit(4, R.ao.color);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (vrs) glDisable(GL_SHADING_RATE_IMAGE_NV);
    if (timing) mark(4);

    // 4. Фильтр: виньетка, зерно, сканлайны, аберрация
    glBindFramebuffer(GL_FRAMEBUFFER, R.ldr.fbo);
    glUseProgram(R.prog_style);
    glUniform1i(U_S_POST, rs->post ? 1 : 0);
    glUniform1i(U_S_FRAME, (GLint)fv->frame);
    glUniform4f(U_S_PIX, px_k, PIXEL_P, fv->proj.m[10], fv->proj.m[14]);
    glBindTextureUnit(0, R.atmos.color);
    glBindTextureUnit(1, R.scene.depth);
    glBindTextureUnit(2, R.src_map.color);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (timing) mark(5);

    // 5. Апскейл до окна: крупный ровный пиксель
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, win_w, win_h);
    glUseProgram(R.prog_upscale);
    glUniform2f(U_U_OUT, (f32)win_w, (f32)win_h);
    glBindTextureUnit(0, R.ldr.color);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (timing) mark(6);
}

void render_frame_end(void)
{
    if (R.timing) {
        mark(7);
        R.timer_pending[R.timer_slot] = true;
        R.timing = false;
    }
    R.timer_slot = (R.timer_slot + 1) % TIMER_FRAMES;
    // Кадр уходит на GPU сразу: при swap после логики GPU рисует, пока CPU считает следующий
    glFlush();
    // Старые кадры, до которых очередь дойдёт не скоро, забираем сразу
    for (u32 i = 0; i < TIMER_FRAMES; i++) timer_collect(i);
}

void render_read_pixels(u8 *rgb, i32 w, i32 h)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
}

u32 render_program(const char *vs_file, const char *fs_file)
{
    size_t mark = R.scratch->used;
    u32 p = link(compile(GL_VERTEX_SHADER, NULL, &vs_file, 1), compile(GL_FRAGMENT_SHADER, NULL, &fs_file, 1));
    R.scratch->used = mark;
    return p;
}

void render_portrait(u32 slot, i32 size, const FrameView *fv, M4 view_proj, const DrawItem *items, u32 count)
{
    if (slot >= RENDER_PORTRAIT_SLOTS || !R.prog_scene) return;
    size = CLAMP(size, 8, RENDER_PORTRAIT_PX);
    // Шейдер сцены со своим неподвижным светом, без солнца и теней мира
    i32 x = (i32)slot * RENDER_PORTRAIT_PX;
    glBindFramebuffer(GL_FRAMEBUFFER, R.portrait.fbo);
    glViewport(x, 0, size, size);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, 0, size, size);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glUseProgram(R.prog_scene);  // шум материалов — как в последнем кадре (U_NOISE_TEX хранится в программе)
    glUniformMatrix4fv(U_VIEW_PROJ, 1, GL_FALSE, view_proj.m);
    glUniform3f(U_CAM_POS, fv->cam_pos.x, fv->cam_pos.y, fv->cam_pos.z);
    glUniform1f(U_TIME, fv->time);
    glUniform1i(U_UNLIT, 1);
    count = MIN(count, (u32)RENDER_MAX_ITEMS);
    stream_poses(items, count);
    PassView pv = { .fr = frustum_from(view_proj), .full = true };
    u32 n = gather(items, count, &pv, false, g_inst[0]);
    u32 base = stream_insts(g_inst[0], &n);
    draw_insts(g_inst[0], n, base, 2);
    glUniform1i(U_UNLIT, 0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

u32 render_portrait_texture(void) { return R.portrait.color; }
