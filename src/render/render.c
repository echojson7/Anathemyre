#include "render.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/log.h"
#include "gl.h"

#define SHADOW_SIZE 2048
#define SHADOW_EXTENT 45.0f   // полуразмер области теней, м

// Расположения uniform задаются явно в GLSL (layout(location = N))
enum {
    U_VIEW_PROJ = 0, U_SKINNED = 17, U_LIGHT_VP = 18, U_INST_BASE = 19,
    U_CAM_POS = 20, U_SUN_DIR = 21, U_SUN_COLOR = 22, U_TIME = 23, U_SKY_AMB = 24, U_GROUND_AMB = 25, U_UNLIT = 26,
    U_A_INV_VP = 30, U_A_CAM = 31, U_A_SUN_DIR = 32, U_A_SUN_COLOR = 33,
    U_A_LIGHT_VP = 34, U_A_TIME = 35, U_A_FRAME = 36, U_A_PROJ_SCALE = 37, U_A_HAZE = 38, U_A_ZENITH = 39,
    U_S_POST = 41, U_S_FRAME = 42, U_U_OUT = 43,
};

// Буферы хранения (layout(binding = N) в scene.vert)
enum { SB_BONES = 0, SB_INST = 1 };

// Кости и списки экземпляров пишутся каждый кадр; кольцо буферов, чтобы не ждать GPU с прошлым кадром
#define STREAM_FRAMES 3
#define NO_POSE 0xFFFFFFFFu

// Экземпляр в проходе: сетка выбранного уровня детализации и набор костей
typedef struct Inst {
    const GpuMesh *mesh;
    u32            pose;   // NO_POSE — статика
} Inst;

typedef struct Frustum {
    V4 p[6];   // плоскости: нормаль внутрь xyz, смещение w
} Frustum;

// Проход отрисовки: что видно и в каком размере на экране
typedef struct PassView {
    Frustum fr;
    V3      eye, fwd;   // для теней fwd нулевой: размер не зависит от дальности
    f32     px_k;       // пикселей на метр на единичной дальности (или просто на метр для теней)
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
    u32    prog_scene, prog_shadow, prog_rays, prog_atmos, prog_style, prog_upscale;
    u32    empty_vao;
    u32    shadow_fbo, shadow_tex;
    Target scene, rays, atmos, ldr;
    Target portrait;  // атлас портретов: ячейки RENDER_PORTRAIT_PX в ряд, HDR
    u32    timer[TIMER_FRAMES][TIMER_MARKS];
    bool   timer_pending[TIMER_FRAMES];
    u32    timer_slot;
    f64    stat_ms[PASS_COUNT];
    u32    stat_samples;
    u32    bone_buf[STREAM_FRAMES], inst_buf[STREAM_FRAMES];
    u32    stream;
    u32    used_poses, used_insts;   // занято в буферах текущего кадра (кадр, затем портреты)
    f64    stat_draws[2], stat_tris[2], stat_visible[2];
    u32    stat_frames, stat_items;
    bool   warned_items, warned_poses;
} R;

// Данные кадра на CPU: размер постоянный, выделяются один раз
static V4   g_sphere[RENDER_MAX_ITEMS];
static u32  g_pose[RENDER_MAX_ITEMS];
static Inst g_inst[2][RENDER_MAX_ITEMS];
static u32  g_inst_pose[2 * RENDER_MAX_ITEMS];
static M4   g_bones[RENDER_MAX_POSED * RENDER_MAX_BONES];

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
    const char *atmos_fs[] = { "common.glsl", "atmos.frag" };
    const char *style_fs[] = { "common.glsl", "style.frag" };
    const char *upscale_fs[] = { "upscale.frag" };

    u32 scene = link(compile(GL_VERTEX_SHADER, NULL, scene_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, scene_fs, 2));
    u32 shadow = link(compile(GL_VERTEX_SHADER, "#define SHADOW_PASS\n", scene_vs, 1),
                      compile(GL_FRAGMENT_SHADER, NULL, shadow_fs, 1));
    u32 rays = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, rays_fs, 2));
    u32 atmos = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, atmos_fs, 2));
    u32 style = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, style_fs, 2));
    u32 upscale = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, upscale_fs, 1));
    R.scratch->used = mark;

    u32 progs[] = { scene, shadow, rays, atmos, style, upscale };
    u32 *slots[] = { &R.prog_scene, &R.prog_shadow, &R.prog_rays, &R.prog_atmos, &R.prog_style, &R.prog_upscale };
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
    if (!render_reload_shaders()) return false;

    glCreateVertexArrays(1, &R.empty_vao);
    for (u32 i = 0; i < STREAM_FRAMES; i++) {
        glCreateBuffers(1, &R.bone_buf[i]);
        glNamedBufferStorage(R.bone_buf[i], (GLsizeiptr)sizeof g_bones, NULL, GL_DYNAMIC_STORAGE_BIT);
        glCreateBuffers(1, &R.inst_buf[i]);
        glNamedBufferStorage(R.inst_buf[i], (GLsizeiptr)sizeof g_inst_pose, NULL, GL_DYNAMIC_STORAGE_BIT);
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
    target_free(&R.atmos);
    target_free(&R.ldr);
    target_free(&R.portrait);
    if (R.shadow_fbo) glDeleteFramebuffers(1, &R.shadow_fbo);
    if (R.shadow_tex) glDeleteTextures(1, &R.shadow_tex);
    if (R.empty_vao) glDeleteVertexArrays(1, &R.empty_vao);
    for (u32 i = 0; i < STREAM_FRAMES; i++) {
        if (R.bone_buf[i]) glDeleteBuffers(1, &R.bone_buf[i]);
        if (R.inst_buf[i]) glDeleteBuffers(1, &R.inst_buf[i]);
    }
    for (u32 i = 0; i < TIMER_FRAMES; i++)
        if (R.timer[i][0]) glDeleteQueries(TIMER_MARKS, R.timer[i]);
    u32 progs[] = { R.prog_scene, R.prog_shadow, R.prog_rays, R.prog_atmos, R.prog_style, R.prog_upscale };
    for (u32 i = 0; i < ARRAY_COUNT(progs); i++)
        if (progs[i]) glDeleteProgram(progs[i]);
    memset(&R, 0, sizeof R);
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
    glVertexArrayAttribIFormat(gm->vao, 3, 2, GL_UNSIGNED_BYTE, offsetof(Vertex, mat));
    for (u32 i = 0; i < 4; i++) glVertexArrayAttribBinding(gm->vao, i, 0);
    gm->index_count = mb->icount;
    gm->lod_next = NULL;
    gm->lod_px = 0.0f;

    // Границы: сфера вокруг рамки вершин, для всей сетки и для каждой кости
    V3 lo[RENDER_MAX_BONES + 1], hi[RENDER_MAX_BONES + 1];
    for (u32 b = 0; b <= RENDER_MAX_BONES; b++) {
        lo[b] = v3(FLT_MAX, FLT_MAX, FLT_MAX);
        hi[b] = v3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    }
    gm->bone_mask = 0;
    for (u32 i = 0; i < mb->vcount; i++) {
        const Vertex *v = &mb->v[i];
        V3 p = v3(v->pos[0], v->pos[1], v->pos[2]);
        u32 bone = MIN((u32)v->bone, RENDER_MAX_BONES - 1u);  // как в scene.vert
        u32 slots[2] = { bone, RENDER_MAX_BONES };
        for (u32 k = 0; k < 2; k++) {
            u32 b = slots[k];
            lo[b] = v3(fminf(lo[b].x, p.x), fminf(lo[b].y, p.y), fminf(lo[b].z, p.z));
            hi[b] = v3(fmaxf(hi[b].x, p.x), fmaxf(hi[b].y, p.y), fmaxf(hi[b].z, p.z));
        }
        gm->bone_mask |= 1u << bone;
    }
    V4 sph[RENDER_MAX_BONES + 1];
    for (u32 b = 0; b <= RENDER_MAX_BONES; b++) {
        V3 c = lo[b].x <= hi[b].x ? v3_mul(v3_add(lo[b], hi[b]), 0.5f) : v3(0, 0, 0);
        sph[b] = (V4){ c.x, c.y, c.z, 0.0f };
    }
    for (u32 i = 0; i < mb->vcount; i++) {
        const Vertex *v = &mb->v[i];
        V3 p = v3(v->pos[0], v->pos[1], v->pos[2]);
        u32 slots[2] = { MIN((u32)v->bone, RENDER_MAX_BONES - 1u), RENDER_MAX_BONES };
        for (u32 k = 0; k < 2; k++) {
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

// Мировая сфера элемента: у скелетной сетки — вокруг сфер её костей в текущей позе
static V4 item_sphere(const DrawItem *it)
{
    const GpuMesh *m = it->mesh;
    if (!it->bones) return m->bound;
    V4 bs[RENDER_MAX_BONES];
    u32 n = 0;
    V3 lo = v3(FLT_MAX, FLT_MAX, FLT_MAX), hi = v3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (u32 b = 0; b < RENDER_MAX_BONES; b++) {
        if (!(m->bone_mask & (1u << b))) continue;
        const M4 *x = &it->bones[b];
        V4 l = m->bone_bound[b];
        V3 c = m4_point(*x, v3(l.x, l.y, l.z));
        f32 sx = v3_len(v3(x->m[0], x->m[1], x->m[2]));
        f32 sy = v3_len(v3(x->m[4], x->m[5], x->m[6]));
        f32 sz = v3_len(v3(x->m[8], x->m[9], x->m[10]));
        f32 r = l.w * fmaxf(sx, fmaxf(sy, sz));
        bs[n++] = (V4){ c.x, c.y, c.z, r };
        lo = v3(fminf(lo.x, c.x - r), fminf(lo.y, c.y - r), fminf(lo.z, c.z - r));
        hi = v3(fmaxf(hi.x, c.x + r), fmaxf(hi.y, c.y + r), fmaxf(hi.z, c.z + r));
    }
    if (!n) return (V4){ 0, 0, 0, 0 };
    V3 c = v3_mul(v3_add(lo, hi), 0.5f);
    f32 r = 0.0f;
    for (u32 i = 0; i < n; i++) r = fmaxf(r, v3_len(v3_sub(v3(bs[i].x, bs[i].y, bs[i].z), c)) + bs[i].w);
    return (V4){ c.x, c.y, c.z, r };
}

// Уровень детализации по размеру сферы в пикселях прохода
static const GpuMesh *pick_lod(const GpuMesh *m, V4 s, const PassView *pv)
{
    f32 depth = v3_dot(v3_sub(v3(s.x, s.y, s.z), pv->eye), pv->fwd);
    bool ortho = pv->fwd.x == 0.0f && pv->fwd.y == 0.0f && pv->fwd.z == 0.0f;
    f32 px = 2.0f * s.w * pv->px_k / (ortho ? 1.0f : fmaxf(depth, s.w));
    while (!pv->full && m->lod_next && px < m->lod_px) m = m->lod_next;
    return m;
}

static int inst_cmp(const void *pa, const void *pb)
{
    const Inst *a = pa, *b = pb;
    if (a->mesh != b->mesh) return (uintptr_t)a->mesh < (uintptr_t)b->mesh ? -1 : 1;
    return a->pose < b->pose ? -1 : (a->pose > b->pose);
}

// Видимые экземпляры прохода, сгруппированные по сетке
static u32 gather(const DrawItem *items, u32 count, const PassView *pv, bool shadow, Inst *out)
{
    u32 n = 0;
    for (u32 i = 0; i < count; i++) {
        const DrawItem *it = &items[i];
        if (shadow && it->no_shadow) continue;
        if (it->bones && g_pose[i] == NO_POSE) continue;
        if (!sphere_visible(&pv->fr, g_sphere[i])) continue;
        out[n++] = (Inst){ pick_lod(it->mesh, g_sphere[i], pv), it->bones ? g_pose[i] : NO_POSE };
    }
    qsort(out, n, sizeof *out, inst_cmp);
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
            glUniform1i(U_SKINNED, 0);
            glDrawElements(GL_TRIANGLES, (GLsizei)m->index_count, GL_UNSIGNED_INT, NULL);
        } else {
            glUniform1i(U_SKINNED, 1);
            glUniform1i(U_INST_BASE, (GLint)(base + i));
            glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)m->index_count, GL_UNSIGNED_INT, NULL, (GLsizei)(j - i));
        }
        draws++;
        tris += (f64)(m->index_count / 3) * (f64)(j - i);
        i = j;
    }
    if (pass >= 2) return;  // портреты в счётчики кадра не идут
    R.stat_draws[pass] += draws;
    R.stat_tris[pass] += tris;
    R.stat_visible[pass] += n;
}

// Наборы костей элементов — в буфер кадра после уже занятых; одинаковые подряд — один набор
static void stream_poses(const DrawItem *items, u32 count)
{
    u32 first = R.used_poses, poses = first;
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

void render_frame(const FrameView *fv, const DrawItem *items, u32 count, const RenderSettings *rs, i32 win_w, i32 win_h)
{
    if (win_w <= 0 || win_h <= 0 || !R.prog_scene) return;
    i32 ih = CLAMP(rs->internal_h, 120, win_h);
    i32 iw = (i32)((f32)ih * (f32)win_w / (f32)win_h + 0.5f);
    if (R.scene.w != iw || R.scene.h != ih) {
        target_make(&R.scene, iw, ih, GL_RGBA16F, true);
        target_make(&R.rays, (iw + 1) / 2, (ih + 1) / 2, GL_RG16F, false);
        target_make(&R.atmos, iw, ih, GL_RGBA16F, false);
        target_make(&R.ldr, iw, ih, GL_RGBA8, false);
        glTextureParameteri(R.ldr.color, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(R.ldr.color, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        log_info("РЕНДЕР: внутреннее разрешение %dx%d -> окно %dx%d", iw, ih, win_w, win_h);
    }

    // Слот занят непрочитанным кадром: забираем или пропускаем замер
    timer_collect(R.timer_slot);
    bool timing = !R.timer_pending[R.timer_slot];
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
    R.stream = (R.stream + 1) % STREAM_FRAMES;
    R.used_poses = R.used_insts = 0;
    stream_poses(items, count);
    PassView shadow_view = { .fr = frustum_from(light_vp), .px_k = SHADOW_SIZE / (2.0f * SHADOW_EXTENT) };
    PassView scene_view = {
        .fr = frustum_from(vp), .eye = fv->cam_pos,
        .fwd = v3(-fv->view.m[2], -fv->view.m[6], -fv->view.m[10]),
        .px_k = fv->proj.m[5] * 0.5f * (f32)ih,
    };
    u32 n_shadow = gather(items, count, &shadow_view, true, g_inst[0]);
    u32 n_scene = gather(items, count, &scene_view, false, g_inst[1]);
    u32 base_shadow = stream_insts(g_inst[0], &n_shadow);
    u32 base_scene = stream_insts(g_inst[1], &n_scene);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SB_BONES, R.bone_buf[R.stream]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SB_INST, R.inst_buf[R.stream]);
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
    draw_insts(g_inst[0], n_shadow, base_shadow, 0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    if (timing) mark(1);

    // 2. Сцена в низком разрешении
    glBindFramebuffer(GL_FRAMEBUFFER, R.scene.fbo);
    glViewport(0, 0, iw, ih);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(R.prog_scene);
    glUniformMatrix4fv(U_VIEW_PROJ, 1, GL_FALSE, vp.m);
    glUniformMatrix4fv(U_LIGHT_VP, 1, GL_FALSE, light_vp.m);
    glUniform3f(U_CAM_POS, fv->cam_pos.x, fv->cam_pos.y, fv->cam_pos.z);
    glUniform3f(U_SUN_DIR, L.x, L.y, L.z);
    glUniform3f(U_SUN_COLOR, fv->sun_color.x, fv->sun_color.y, fv->sun_color.z);
    glUniform1f(U_TIME, fv->time);
    glUniform3f(U_SKY_AMB, fv->sky_amb.x, fv->sky_amb.y, fv->sky_amb.z);
    glUniform3f(U_GROUND_AMB, fv->ground_amb.x, fv->ground_amb.y, fv->ground_amb.z);
    glBindTextureUnit(0, R.shadow_tex);
    draw_insts(g_inst[1], n_scene, base_scene, 1);
    if (timing) mark(2);

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

    glBindFramebuffer(GL_FRAMEBUFFER, R.atmos.fbo);
    glViewport(0, 0, iw, ih);
    glUseProgram(R.prog_atmos);
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
    glBindTextureUnit(3, R.rays.color);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (timing) mark(3);

    // 4. Фильтр: виньетка, зерно, сканлайны, аберрация
    glBindFramebuffer(GL_FRAMEBUFFER, R.ldr.fbo);
    glUseProgram(R.prog_style);
    glUniform1i(U_S_POST, rs->post ? 1 : 0);
    glUniform1i(U_S_FRAME, (GLint)fv->frame);
    glBindTextureUnit(0, R.atmos.color);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (timing) mark(4);

    // 5. Апскейл до окна: крупный ровный пиксель
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, win_w, win_h);
    glUseProgram(R.prog_upscale);
    glUniform2f(U_U_OUT, (f32)win_w, (f32)win_h);
    glBindTextureUnit(0, R.ldr.color);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (timing) {
        mark(5);
        R.timer_pending[R.timer_slot] = true;
    }
    R.timer_slot = (R.timer_slot + 1) % TIMER_FRAMES;
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
    glUseProgram(R.prog_scene);
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
