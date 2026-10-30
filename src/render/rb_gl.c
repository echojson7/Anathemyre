// Бэкенд слоя rb на OpenGL 4.5 core. Состояние GL кешируется: лишние вызовы не уходят в драйвер
#include "rb.h"

#include <stdio.h>
#include <string.h>

#include "../core/log.h"
#include "gl.h"

#define MAX_BUFS 4096
#define MAX_TEXS 256
#define MAX_PIPES 128
#define MAX_PROGS 64
#define MAX_LAYOUTS 16
#define MAX_FBOS 64
#define MAX_UNITS 64
#define MAX_BINDS 64
#define PUSH_VEC4 (RB_PUSH_BYTES / 16)

typedef struct GlBuf {
    u32    name;
    size_t size;
} GlBuf;

typedef struct GlTex {
    u32       name;
    RbTexDesc d;
    bool      rate_dirty;   // писалась целью после последнего барьера карты частоты
} GlTex;

// Программа: общая у конвейеров с теми же файлами и определениями
typedef struct GlProg {
    u32  name, refs, gen;
    char key[768];
    i32  push_n;                   // активных элементов u_push
    u32  push[PUSH_VEC4 * 4];      // что уже лежит в программе
    bool push_valid[PUSH_VEC4];
} GlProg;

// Раскладка вершин: VAO и привязанные к нему потоки
typedef struct GlLayout {
    RbLayout l;
    u32      vao, refs, streams;  // streams — маска используемых потоков
    u32      buf[RB_MAX_STREAMS], stride[RB_MAX_STREAMS];
    size_t   off[RB_MAX_STREAMS];
    u32      ibuf;
} GlLayout;

typedef struct GlPipe {
    bool       used, compute;
    u32        prog, layout;   // номера в G.prog и G.lay
    RbPipeDesc d;
} GlPipe;

typedef struct GlFbo {
    u32      name;
    RbAttach color[RB_MAX_COLORS], depth;
} GlFbo;

static struct {
    Arena  *scratch;
    char    data_dir[512];
    RbCaps  caps;
    GlBuf   buf[MAX_BUFS];
    GlTex   tex[MAX_TEXS];
    GlPipe  pipe[MAX_PIPES];
    GlProg  prog[MAX_PROGS];
    GlLayout lay[MAX_LAYOUTS];
    GlFbo   fbo[MAX_FBOS];
    u32     prog_gen;
    u32     samplers[RB_SMP_COUNT];
    u32     zero_buf;       // поток нулей
    u32     frame;
    i32     win_w, win_h;
    // Текущее
    RbPipe  cur_pipe;
    u32     vb[RB_MAX_STREAMS];
    size_t  vb_off[RB_MAX_STREAMS];
    u32     ib;
    size_t  ib_off;
    bool    ib16;
    u32     push[PUSH_VEC4 * 4];
    bool    pass_images;    // в проходе привязаны текстуры для записи
    bool    rate_on;
    i32     pass_w, pass_h;
    // Кеш состояния GL (-1 — неизвестно)
    i32     gl_prog, gl_vao, gl_fbo;
    i32     vp[4], sc[4];
    i32     scissor_on, cull_on, depth_on, depth_func, depth_mask, color_mask, blend, offset_on, clamp_on;
    f32     offset[2];
    u32     unit_tex[MAX_UNITS], unit_smp[MAX_UNITS];
    u32     ubo[MAX_BINDS], ssbo[MAX_BINDS];
    size_t  ubo_off[MAX_BINDS], ubo_size[MAX_BINDS], ssbo_off[MAX_BINDS], ssbo_size[MAX_BINDS];
    // Метки времени
    u32    *timer;
    u32     timer_sets, timer_marks;
} G;

static u32 timers_store[16 * 16];

static const struct { GLenum ifmt, fmt, type; u32 bytes; } g_fmt[RB_FMT_COUNT] = {
    [RB_FMT_R8] = { GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1 },
    [RB_FMT_R16] = { GL_R16, GL_RED, GL_UNSIGNED_SHORT, 2 },
    [RB_FMT_RGBA8] = { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4 },
    [RB_FMT_R8UI] = { GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, 1 },
    [RB_FMT_RG16UI] = { GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT, 4 },
    [RB_FMT_R32UI] = { GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, 4 },
    [RB_FMT_R16F] = { GL_R16F, GL_RED, GL_HALF_FLOAT, 2 },
    [RB_FMT_RG16F] = { GL_RG16F, GL_RG, GL_HALF_FLOAT, 4 },
    [RB_FMT_RGBA16F] = { GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8 },
    [RB_FMT_R11G11B10F] = { GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, 4 },
    [RB_FMT_R32F] = { GL_R32F, GL_RED, GL_FLOAT, 4 },
    [RB_FMT_D24] = { GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 4 },
    [RB_FMT_D32F] = { GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 4 },
};


static void GLAPIENTRY gl_debug_cb(GLenum source, GLenum type, GLuint id, GLenum severity,
                                   GLsizei length, const GLchar *message, const void *user)
{
    (void)source; (void)type; (void)id; (void)length; (void)user;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    log_error("GL: %s", message);
}

static void state_reset(void)
{
    G.gl_prog = G.gl_vao = G.gl_fbo = -1;
    G.scissor_on = G.cull_on = G.depth_on = G.depth_func = G.depth_mask = G.color_mask = G.blend = -1;
    G.offset_on = G.clamp_on = -1;
    G.offset[0] = G.offset[1] = -1.0f;
    memset(G.vp, -1, sizeof G.vp);
    memset(G.sc, -1, sizeof G.sc);
    memset(G.unit_tex, 0xFF, sizeof G.unit_tex);
    memset(G.unit_smp, 0xFF, sizeof G.unit_smp);
    memset(G.ubo, 0xFF, sizeof G.ubo);
    memset(G.ssbo, 0xFF, sizeof G.ssbo);
}

static void set_cap(i32 *cache, GLenum cap, bool on)
{
    if (*cache == (i32)on) return;
    *cache = on;
    if (on) glEnable(cap);
    else glDisable(cap);
}

static void set_depth_mask(bool on)
{
    if (G.depth_mask == (i32)on) return;
    G.depth_mask = on;
    glDepthMask(on ? GL_TRUE : GL_FALSE);
}

static void set_color_mask(bool on)
{
    if (G.color_mask == (i32)on) return;
    G.color_mask = on;
    GLboolean b = on ? GL_TRUE : GL_FALSE;
    glColorMask(b, b, b, b);
}

static void bind_fbo(u32 fbo)
{
    if (G.gl_fbo == (i32)fbo) return;
    G.gl_fbo = (i32)fbo;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
}

void rb_viewport(i32 x, i32 y, i32 w, i32 h)
{
    i32 v[4] = { x, y, w, h };
    if (!memcmp(v, G.vp, sizeof v)) return;
    memcpy(G.vp, v, sizeof v);
    glViewport(x, y, w, h);
}

void rb_scissor(i32 x, i32 y, i32 w, i32 h)
{
    set_cap(&G.scissor_on, GL_SCISSOR_TEST, true);
    i32 s[4] = { x, y, w, h };
    if (!memcmp(s, G.sc, sizeof s)) return;
    memcpy(G.sc, s, sizeof s);
    glScissor(x, y, w, h);
}

void rb_scissor_off(void) { set_cap(&G.scissor_on, GL_SCISSOR_TEST, false); }

bool rb_init(Arena *scratch, const char *data_dir)
{
    G.scratch = scratch;
    snprintf(G.data_dir, sizeof G.data_dir, "%s", data_dir);
    if (!gl_load()) return false;
    log_info("GL: %s | %s", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
#ifndef NDEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(gl_debug_cb, NULL);
#else
    (void)gl_debug_cb;
#endif
    state_reset();
    GLint a = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &a);
    G.caps.ubo_align = a > 0 ? (u32)a : 256;
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &a);
    G.caps.ssbo_align = a > 0 ? (u32)a : 256;
    // Частота шейдинга по блокам: карта с клеткой vrs_tile, палитра 0 — пиксель, 1 — 2x2, 2 — 4x4
    bool vrs = gl_has_extension("GL_NV_shading_rate_image") && glBindShadingRateImageNV &&
               glShadingRateImagePaletteNV && glShadingRateImageBarrierNV;
    if (vrs) {
        glGetIntegerv(GL_SHADING_RATE_IMAGE_TEXEL_WIDTH_NV, &G.caps.vrs_tile[0]);
        glGetIntegerv(GL_SHADING_RATE_IMAGE_TEXEL_HEIGHT_NV, &G.caps.vrs_tile[1]);
        const GLenum rates[3] = { GL_SHADING_RATE_1_INVOCATION_PER_PIXEL_NV, GL_SHADING_RATE_1_INVOCATION_PER_2X2_PIXELS_NV,
                                  GL_SHADING_RATE_1_INVOCATION_PER_4X4_PIXELS_NV };
        glShadingRateImagePaletteNV(0, 0, 3, rates);
    }
    G.caps.vrs = vrs;
    // Строки данных текстур не выравниваются
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    static const struct { GLenum filter, wrap; bool cmp; } smp[RB_SMP_COUNT] = {
        [RB_SMP_NEAREST] = { GL_NEAREST, GL_CLAMP_TO_EDGE, false },
        [RB_SMP_LINEAR] = { GL_LINEAR, GL_CLAMP_TO_EDGE, false },
        [RB_SMP_NEAREST_REPEAT] = { GL_NEAREST, GL_REPEAT, false },
        [RB_SMP_LINEAR_REPEAT] = { GL_LINEAR, GL_REPEAT, false },
        [RB_SMP_SHADOW] = { GL_LINEAR, GL_CLAMP_TO_EDGE, true },
    };
    glCreateSamplers(RB_SMP_COUNT, G.samplers);
    for (u32 i = 0; i < RB_SMP_COUNT; i++) {
        u32 s = G.samplers[i];
        glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, (GLint)smp[i].filter);
        glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, (GLint)smp[i].filter);
        glSamplerParameteri(s, GL_TEXTURE_WRAP_S, (GLint)smp[i].wrap);
        glSamplerParameteri(s, GL_TEXTURE_WRAP_T, (GLint)smp[i].wrap);
        glSamplerParameteri(s, GL_TEXTURE_WRAP_R, (GLint)smp[i].wrap);
        if (smp[i].cmp) {
            glSamplerParameteri(s, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glSamplerParameteri(s, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        }
    }
    static const u8 zero[64];
    glCreateBuffers(1, &G.zero_buf);
    glNamedBufferStorage(G.zero_buf, sizeof zero, zero, 0);
    return true;
}

const RbCaps *rb_caps(void) { return &G.caps; }

void rb_shaders_changed(void) { G.prog_gen++; }

void rb_frame_begin(i32 win_w, i32 win_h)
{
    G.win_w = win_w;
    G.win_h = win_h;
}

u32 rb_frame_slot(void) { return G.frame % RB_FRAMES; }

void rb_frame_end(void)
{
    glFlush();
    G.frame++;
}

// --- Буферы

RbBuf rb_buf_make(size_t size, u32 usage, const void *data)
{
    for (u32 i = 1; i < MAX_BUFS; i++) {
        if (G.buf[i].name) continue;
        u32 name;
        glCreateBuffers(1, &name);
        // Без начальных данных или с записью каждый кадр — дописываемый
        glNamedBufferStorage(name, (GLsizeiptr)size, data, (usage & RB_BUF_DYNAMIC) || !data ? GL_DYNAMIC_STORAGE_BIT : 0);
        G.buf[i] = (GlBuf){ name, size };
        return i;
    }
    log_error("RB: буферов больше %u", MAX_BUFS);
    return 0;
}

void rb_buf_write(RbBuf b, size_t offset, size_t size, const void *data)
{
    if (b && size) glNamedBufferSubData(G.buf[b].name, (GLintptr)offset, (GLsizeiptr)size, data);
}

void rb_buf_free(RbBuf *b)
{
    if (!*b) return;
    u32 name = G.buf[*b].name;
    // Имя освобождается: кеши, где оно записано, больше не верны
    for (u32 i = 0; i < MAX_BINDS; i++) {
        if (G.ubo[i] == name) G.ubo[i] = ~0u;
        if (G.ssbo[i] == name) G.ssbo[i] = ~0u;
    }
    for (u32 l = 0; l < MAX_LAYOUTS; l++) {
        for (u32 s = 0; s < RB_MAX_STREAMS; s++)
            if (G.lay[l].buf[s] == name) G.lay[l].buf[s] = ~0u;
        if (G.lay[l].ibuf == name) G.lay[l].ibuf = ~0u;
    }
    for (u32 s = 0; s < RB_MAX_STREAMS; s++)
        if (G.vb[s] == *b) G.vb[s] = 0;
    if (G.ib == *b) G.ib = 0;
    glDeleteBuffers(1, &name);
    memset(&G.buf[*b], 0, sizeof G.buf[*b]);
    *b = 0;
}

// --- Текстуры

RbTex rb_tex_make(const RbTexDesc *d)
{
    for (u32 i = 1; i < MAX_TEXS; i++) {
        if (G.tex[i].name) continue;
        GlTex *t = &G.tex[i];
        t->d = *d;
        if (t->d.d < 1) t->d.d = 1;
        if (t->d.mips < 1) t->d.mips = 1;
        GLenum target = d->kind == RB_TEX_3D ? GL_TEXTURE_3D : d->kind == RB_TEX_2D_ARRAY ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
        glCreateTextures(target, 1, &t->name);
        if (d->kind == RB_TEX_2D) glTextureStorage2D(t->name, (GLsizei)t->d.mips, g_fmt[d->fmt].ifmt, d->w, d->h);
        else glTextureStorage3D(t->name, (GLsizei)t->d.mips, g_fmt[d->fmt].ifmt, d->w, d->h, t->d.d);
        return i;
    }
    log_error("RB: текстур больше %u", MAX_TEXS);
    return 0;
}

void rb_tex_write(RbTex t, u32 mip, i32 x, i32 y, i32 z, i32 w, i32 h, i32 d, const void *data, u32 row_len)
{
    if (!t) return;
    const GlTex *tx = &G.tex[t];
    if (row_len && (i32)row_len != w) glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)row_len);
    GLenum fmt = g_fmt[tx->d.fmt].fmt, type = g_fmt[tx->d.fmt].type;
    if (tx->d.kind == RB_TEX_2D) glTextureSubImage2D(tx->name, (GLint)mip, x, y, w, h, fmt, type, data);
    else glTextureSubImage3D(tx->name, (GLint)mip, x, y, z, w, h, d, fmt, type, data);
    if (row_len && (i32)row_len != w) glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void rb_tex_size(RbTex t, i32 *w, i32 *h)
{
    *w = t ? G.tex[t].d.w : G.win_w;
    *h = t ? G.tex[t].d.h : G.win_h;
}

void rb_tex_free(RbTex *t)
{
    if (!*t) return;
    u32 name = G.tex[*t].name;
    for (u32 i = 0; i < MAX_UNITS; i++)
        if (G.unit_tex[i] == name) G.unit_tex[i] = ~0u;
    // Кадровые буферы с этой текстурой больше не нужны
    for (u32 i = 0; i < MAX_FBOS; i++) {
        GlFbo *f = &G.fbo[i];
        if (!f->name) continue;
        bool uses = f->depth.tex == *t;
        for (u32 c = 0; c < RB_MAX_COLORS; c++) uses = uses || f->color[c].tex == *t;
        if (!uses) continue;
        if (G.gl_fbo == (i32)f->name) G.gl_fbo = -1;
        glDeleteFramebuffers(1, &f->name);
        memset(f, 0, sizeof *f);
    }
    glDeleteTextures(1, &name);
    memset(&G.tex[*t], 0, sizeof G.tex[*t]);
    *t = 0;
}

// --- Шейдеры и конвейеры

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
    char *buf = arena_push(G.scratch, (size_t)n + 1, 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return NULL;
    }
    buf[n] = 0;
    fclose(f);
    return buf;
}

static u32 file_count(const char *const *files)
{
    u32 n = 0;
    while (n < RB_SHADER_FILES && files[n]) n++;
    return n;
}

// Исходник стадии: версия, расширения, определения, rb.glsl, файлы по порядку
static u32 compile(GLenum type, const RbPipeDesc *d, const char *const *files)
{
    const char *src[RB_SHADER_FILES + 5];
    u32 n = 0, nf = file_count(files);
    src[n++] = "#version 450 core\n";
    // Размер блока шейдинга во фрагментном шейдере (карта частоты GL_NV_shading_rate_image)
    if (type == GL_FRAGMENT_SHADER && d->frag_size && G.caps.vrs)
        src[n++] = "#extension GL_NV_shading_rate_image : require\n#define RB_FRAG_SIZE gl_FragmentSizeNV\n";
    src[n++] = d->defines ? d->defines : "";
    const char *const all[] = { "rb.glsl" };
    for (u32 i = 0; i < 1 + nf; i++) {
        char path[640];
        snprintf(path, sizeof path, "%s/shaders/%s", G.data_dir, i ? files[i - 1] : all[0]);
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
        log_error("ШЕЙДЕР %s: %s", nf ? files[nf - 1] : "?", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static u32 link(u32 a, u32 b)
{
    if (!a) return 0;
    u32 p = glCreateProgram();
    glAttachShader(p, a);
    if (b) glAttachShader(p, b);
    glLinkProgram(p);
    glDeleteShader(a);
    if (b) glDeleteShader(b);
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

static void key_files(char *k, size_t size, const char *const *files)
{
    for (u32 i = 0; i < RB_SHADER_FILES && files[i]; i++) {
        size_t n = strlen(k);
        snprintf(k + n, size - n, "%s,", files[i]);
    }
    size_t n = strlen(k);
    snprintf(k + n, size - n, "|");
}

// Программа по файлам и определениям: собранная этой же версией шейдеров — общая
static u32 prog_get(const RbPipeDesc *d)
{
    char key[768] = "";
    key_files(key, sizeof key, d->cs);
    key_files(key, sizeof key, d->vs);
    key_files(key, sizeof key, d->fs);
    size_t kn = strlen(key);
    snprintf(key + kn, sizeof key - kn, "%d|%s", d->frag_size && G.caps.vrs, d->defines ? d->defines : "");
    u32 free_slot = 0;
    for (u32 i = 1; i < MAX_PROGS; i++) {
        GlProg *p = &G.prog[i];
        if (!p->name) {
            if (!free_slot) free_slot = i;
            continue;
        }
        if (p->gen == G.prog_gen && !strcmp(p->key, key)) {
            p->refs++;
            return i;
        }
    }
    if (!free_slot) {
        log_error("RB: программ больше %u", MAX_PROGS);
        return 0;
    }
    size_t mark = G.scratch->used;
    u32 name;
    if (d->cs[0]) {
        name = link(compile(GL_COMPUTE_SHADER, d, d->cs), 0);
    } else {
        u32 vs = compile(GL_VERTEX_SHADER, d, d->vs), fs = compile(GL_FRAGMENT_SHADER, d, d->fs);
        if (!vs || !fs) {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            name = 0;
        } else {
            name = link(vs, fs);
        }
    }
    G.scratch->used = mark;
    if (!name) return 0;
    GlProg *p = &G.prog[free_slot];
    memset(p, 0, sizeof *p);
    p->name = name;
    p->refs = 1;
    p->gen = G.prog_gen;
    snprintf(p->key, sizeof p->key, "%s", key);
    // Константы вызова: сколько элементов u_push осталось после оптимизации
    for (i32 k = 0; k < PUSH_VEC4; k++) {
        char nm[16];
        snprintf(nm, sizeof nm, "u_push[%d]", k);
        if (glGetUniformLocation(name, nm) < 0) break;
        p->push_n = k + 1;
    }
    return free_slot;
}

static void prog_release(u32 i)
{
    GlProg *p = &G.prog[i];
    if (!i || !p->name || --p->refs) return;
    if (G.gl_prog == (i32)p->name) G.gl_prog = -1;
    glDeleteProgram(p->name);
    memset(p, 0, sizeof *p);
}

static const struct { GLint size; GLenum type; bool norm, integer; } g_attr[] = {
    [RB_ATTR_F32x2] = { 2, GL_FLOAT, false, false },
    [RB_ATTR_F32x3] = { 3, GL_FLOAT, false, false },
    [RB_ATTR_UN8x4] = { 4, GL_UNSIGNED_BYTE, true, false },
    [RB_ATTR_U8x4] = { 4, GL_UNSIGNED_BYTE, false, true },
    [RB_ATTR_U32x4] = { 4, GL_UNSIGNED_INT, false, true },
};

static u32 layout_get(const RbLayout *l)
{
    u32 free_slot = 0;
    for (u32 i = 1; i < MAX_LAYOUTS; i++) {
        if (!G.lay[i].vao) {
            if (!free_slot) free_slot = i;
            continue;
        }
        if (!memcmp(&G.lay[i].l, l, sizeof *l)) {
            G.lay[i].refs++;
            return i;
        }
    }
    if (!free_slot) {
        log_error("RB: раскладок больше %u", MAX_LAYOUTS);
        return 0;
    }
    GlLayout *g = &G.lay[free_slot];
    memset(g, 0, sizeof *g);
    g->l = *l;
    g->refs = 1;
    glCreateVertexArrays(1, &g->vao);
    for (u32 a = 0; a < l->attrs; a++) {
        const RbAttr *at = &l->attr[a];
        glEnableVertexArrayAttrib(g->vao, at->location);
        if (g_attr[at->fmt].integer)
            glVertexArrayAttribIFormat(g->vao, at->location, g_attr[at->fmt].size, g_attr[at->fmt].type, at->offset);
        else
            glVertexArrayAttribFormat(g->vao, at->location, g_attr[at->fmt].size, g_attr[at->fmt].type,
                                      g_attr[at->fmt].norm ? GL_TRUE : GL_FALSE, at->offset);
        glVertexArrayAttribBinding(g->vao, at->location, at->stream);
        g->streams |= 1u << at->stream;
    }
    for (u32 s = 0; s < RB_MAX_STREAMS; s++) {
        g->buf[s] = ~0u;
        if (l->per_instance[s]) glVertexArrayBindingDivisor(g->vao, s, 1);
    }
    g->ibuf = ~0u;
    return free_slot;
}

static void layout_release(u32 i)
{
    GlLayout *g = &G.lay[i];
    if (!i || !g->vao || --g->refs) return;
    if (G.gl_vao == (i32)g->vao) G.gl_vao = -1;
    glDeleteVertexArrays(1, &g->vao);
    memset(g, 0, sizeof *g);
}

RbPipe rb_pipe_make(const RbPipeDesc *d)
{
    u32 slot = 0;
    for (u32 i = 1; i < MAX_PIPES && !slot; i++)
        if (!G.pipe[i].used) slot = i;
    if (!slot) {
        log_error("RB: конвейеров больше %u", MAX_PIPES);
        return 0;
    }
    u32 prog = prog_get(d);
    if (!prog) return 0;
    GlPipe *p = &G.pipe[slot];
    memset(p, 0, sizeof *p);
    p->used = true;
    p->compute = d->cs[0] != NULL;
    p->prog = prog;
    p->layout = p->compute ? 0 : layout_get(&d->layout);
    p->d = *d;
    return slot;
}

void rb_pipe_free(RbPipe *pp)
{
    if (!*pp) return;
    GlPipe *p = &G.pipe[*pp];
    prog_release(p->prog);
    layout_release(p->layout);
    if (G.cur_pipe == *pp) G.cur_pipe = 0;
    memset(p, 0, sizeof *p);
    *pp = 0;
}

void rb_bind_pipe(RbPipe pp)
{
    G.cur_pipe = pp;
    const GlPipe *p = &G.pipe[pp];
    u32 prog = G.prog[p->prog].name;
    if (G.gl_prog != (i32)prog) {
        G.gl_prog = (i32)prog;
        glUseProgram(prog);
    }
    if (p->compute) return;
    u32 vao = G.lay[p->layout].vao;
    if (G.gl_vao != (i32)vao) {
        G.gl_vao = (i32)vao;
        glBindVertexArray(vao);
    }
    const RbPipeDesc *d = &p->d;
    set_cap(&G.cull_on, GL_CULL_FACE, d->cull == RB_CULL_BACK);
    set_cap(&G.depth_on, GL_DEPTH_TEST, d->depth != RB_DEPTH_OFF);
    if (d->depth != RB_DEPTH_OFF) {
        i32 f = d->depth == RB_DEPTH_LESS ? GL_LESS : GL_LEQUAL;
        if (G.depth_func != f) {
            G.depth_func = f;
            glDepthFunc((GLenum)f);
        }
    }
    set_depth_mask(d->depth_write);
    set_color_mask(!d->no_color);
    set_cap(&G.clamp_on, GL_DEPTH_CLAMP, d->depth_clamp);
    bool off = d->bias_slope != 0.0f || d->bias_const != 0.0f;
    set_cap(&G.offset_on, GL_POLYGON_OFFSET_FILL, off);
    if (off && (G.offset[0] != d->bias_slope || G.offset[1] != d->bias_const)) {
        G.offset[0] = d->bias_slope;
        G.offset[1] = d->bias_const;
        glPolygonOffset(d->bias_slope, d->bias_const);
    }
    if (G.blend != (i32)d->blend) {
        G.blend = (i32)d->blend;
        if (d->blend == RB_BLEND_NONE) glDisable(GL_BLEND);
        else glEnable(GL_BLEND);
        if (d->blend == RB_BLEND_ALPHA) glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        else if (d->blend == RB_BLEND_PREMUL) glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        else if (d->blend == RB_BLEND_ADD) glBlendFunc(GL_ONE, GL_ONE);
    }
}

// --- Проходы

static bool attach_eq(const RbAttach *a, const RbAttach *b)
{
    return a->tex == b->tex && a->mip == b->mip && a->layer == b->layer;
}

static void fbo_attach(u32 fbo, GLenum point, const RbAttach *a)
{
    const GlTex *t = &G.tex[a->tex];
    if (t->d.kind == RB_TEX_2D) glNamedFramebufferTexture(fbo, point, t->name, a->mip);
    else glNamedFramebufferTextureLayer(fbo, point, t->name, a->mip, a->layer);
}

static u32 fbo_get(const RbPass *p)
{
    u32 free_slot = 0;
    for (u32 i = 0; i < MAX_FBOS; i++) {
        GlFbo *f = &G.fbo[i];
        if (!f->name) {
            if (!free_slot) free_slot = i + 1;
            continue;
        }
        bool same = attach_eq(&f->depth, &p->depth);
        for (u32 c = 0; c < RB_MAX_COLORS && same; c++) same = attach_eq(&f->color[c], &p->color[c]);
        if (same) return f->name;
    }
    if (!free_slot) {
        log_error("RB: кадровых буферов больше %u", MAX_FBOS);
        return 0;
    }
    GlFbo *f = &G.fbo[free_slot - 1];
    glCreateFramebuffers(1, &f->name);
    GLenum bufs[RB_MAX_COLORS];
    u32 n = 0;
    for (u32 c = 0; c < RB_MAX_COLORS && p->color[c].tex; c++) {
        fbo_attach(f->name, GL_COLOR_ATTACHMENT0 + c, &p->color[c]);
        f->color[c] = p->color[c];
        bufs[n++] = GL_COLOR_ATTACHMENT0 + c;
    }
    if (p->depth.tex) {
        fbo_attach(f->name, GL_DEPTH_ATTACHMENT, &p->depth);
        f->depth = p->depth;
    }
    if (n) glNamedFramebufferDrawBuffers(f->name, (GLsizei)n, bufs);
    else glNamedFramebufferDrawBuffer(f->name, GL_NONE);
    if (glCheckNamedFramebufferStatus(f->name, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        log_error("RB: кадровый буфер неполон");
    return f->name;
}

void rb_pass_begin(const RbPass *p)
{
    bool window = !p->color[0].tex && !p->depth.tex;
    bind_fbo(window ? 0 : fbo_get(p));
    const RbAttach *a = p->color[0].tex ? &p->color[0] : &p->depth;
    if (window) {
        G.pass_w = G.win_w;
        G.pass_h = G.win_h;
    } else {
        G.pass_w = MAX(G.tex[a->tex].d.w >> a->mip, 1);
        G.pass_h = MAX(G.tex[a->tex].d.h >> a->mip, 1);
    }
    rb_viewport(0, 0, G.pass_w, G.pass_h);
    rb_scissor_off();
    if (p->clear_color || p->clear_depth) {
        GLbitfield mask = 0;
        if (p->clear_color) {
            set_color_mask(true);
            glClearColor(p->clear[0], p->clear[1], p->clear[2], p->clear[3]);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if (p->clear_depth) {
            set_depth_mask(true);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        glClear(mask);
    }
    if (p->rate && G.caps.vrs) {
        GlTex *r = &G.tex[p->rate];
        // Карту только что рисовали: GPU должен дописать её до того, как по ней шейдить
        if (r->rate_dirty) {
            glShadingRateImageBarrierNV(GL_TRUE);
            r->rate_dirty = false;
        }
        glBindShadingRateImageNV(r->name);
        glEnable(GL_SHADING_RATE_IMAGE_NV);
        G.rate_on = true;
    }
    for (u32 c = 0; c < RB_MAX_COLORS && p->color[c].tex; c++) G.tex[p->color[c].tex].rate_dirty = true;
    G.pass_images = false;
}

void rb_pass_end(void)
{
    if (G.rate_on) {
        glDisable(GL_SHADING_RATE_IMAGE_NV);
        G.rate_on = false;
    }
    // Запись в текстуры из фрагментного шейдера видна следующим проходам
    if (G.pass_images) glMemoryBarrier(GL_ALL_BARRIER_BITS);
    G.pass_images = false;
}

void rb_clear_rect(i32 x, i32 y, i32 w, i32 h, const f32 *color, bool depth)
{
    if (w <= 0 || h <= 0 || (!color && !depth)) return;
    // Ножницы прохода восстанавливаются после очистки
    i32 sc_on = G.scissor_on, sc[4];
    memcpy(sc, G.sc, sizeof sc);
    rb_scissor(x, y, w, h);
    GLbitfield mask = 0;
    if (color) {
        set_color_mask(true);
        glClearColor(color[0], color[1], color[2], color[3]);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (depth) {
        set_depth_mask(true);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    glClear(mask);
    if (sc_on == 1) rb_scissor(sc[0], sc[1], sc[2], sc[3]);
    else rb_scissor_off();
}

// --- Привязки

void rb_bind_vertex(u32 stream, RbBuf b, size_t offset)
{
    G.vb[stream] = b;
    G.vb_off[stream] = offset;
}

void rb_bind_index(RbBuf b, size_t offset, bool idx16)
{
    G.ib = b;
    G.ib_off = offset;
    G.ib16 = idx16;
}

static void bind_range(GLenum target, u32 *cache, size_t *coff, size_t *csize, u32 binding, RbBuf b, size_t offset, size_t size)
{
    u32 name = G.buf[b].name;
    if (!size) size = G.buf[b].size - offset;
    if (cache[binding] == name && coff[binding] == offset && csize[binding] == size) return;
    cache[binding] = name;
    coff[binding] = offset;
    csize[binding] = size;
    if (!offset && size == G.buf[b].size) glBindBufferBase(target, binding, name);
    else glBindBufferRange(target, binding, name, (GLintptr)offset, (GLsizeiptr)size);
}

void rb_bind_ubo(u32 binding, RbBuf b, size_t offset, size_t size)
{
    bind_range(GL_UNIFORM_BUFFER, G.ubo, G.ubo_off, G.ubo_size, binding, b, offset, size);
}

void rb_bind_ssbo(u32 binding, RbBuf b, size_t offset, size_t size)
{
    bind_range(GL_SHADER_STORAGE_BUFFER, G.ssbo, G.ssbo_off, G.ssbo_size, binding, b, offset, size);
}

void rb_bind_tex(u32 binding, RbTex t, RbSampler s)
{
    u32 name = G.tex[t].name;
    if (G.unit_tex[binding] != name) {
        G.unit_tex[binding] = name;
        glBindTextureUnit(binding, name);
    }
    if (G.unit_smp[binding] != G.samplers[s]) {
        G.unit_smp[binding] = G.samplers[s];
        glBindSampler(binding, G.samplers[s]);
    }
}

void rb_bind_image(u32 binding, RbTex t, u32 mip)
{
    const GlTex *tx = &G.tex[t];
    glBindImageTexture(binding, tx->name, (GLint)mip, tx->d.kind != RB_TEX_2D, 0, GL_READ_WRITE, g_fmt[tx->d.fmt].ifmt);
    G.pass_images = true;
}

void rb_push(u32 offset, u32 size, const void *data)
{
    memcpy((u8 *)G.push + offset, data, size);
}

// Перед вызовом: потоки и индексы в VAO конвейера, константы в программу
static void draw_prep(void)
{
    const GlPipe *p = &G.pipe[G.cur_pipe];
    GlProg *pr = &G.prog[p->prog];
    // Константы: залить изменившиеся элементы u_push одним куском
    i32 lo = -1, hi = -1;
    for (i32 k = 0; k < pr->push_n; k++) {
        if (pr->push_valid[k] && !memcmp(&pr->push[k * 4], &G.push[k * 4], 16)) continue;
        if (lo < 0) lo = k;
        hi = k;
    }
    if (lo >= 0) {
        memcpy(&pr->push[lo * 4], &G.push[lo * 4], (size_t)(hi - lo + 1) * 16);
        for (i32 k = lo; k <= hi; k++) pr->push_valid[k] = true;
        glUniform4uiv(lo, hi - lo + 1, &G.push[lo * 4]);
    }
    if (p->compute) return;
    GlLayout *l = &G.lay[p->layout];
    for (u32 s = 0; s < RB_MAX_STREAMS; s++) {
        if (!(l->streams & (1u << s))) continue;
        u32 name = G.vb[s] ? G.buf[G.vb[s]].name : G.zero_buf;
        size_t off = G.vb[s] ? G.vb_off[s] : 0;
        u32 stride = G.vb[s] ? l->l.stride[s] : 0;
        if (l->buf[s] == name && l->off[s] == off && l->stride[s] == stride) continue;
        l->buf[s] = name;
        l->off[s] = off;
        l->stride[s] = stride;
        glVertexArrayVertexBuffer(l->vao, s, name, (GLintptr)off, (GLsizei)stride);
    }
    u32 iname = G.ib ? G.buf[G.ib].name : 0;
    if (l->ibuf != iname) {
        l->ibuf = iname;
        glVertexArrayElementBuffer(l->vao, iname);
    }
}

// --- Вызовы

void rb_draw(u32 verts, u32 first, u32 instances)
{
    draw_prep();
    if (instances == 1) glDrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)verts);
    else glDrawArraysInstanced(GL_TRIANGLES, (GLint)first, (GLsizei)verts, (GLsizei)instances);
}

static const void *index_ptr(u32 first)
{
    return (const void *)(G.ib_off + (size_t)first * (G.ib16 ? 2u : 4u));
}

void rb_draw_indexed(u32 count, u32 first_index, i32 base_vertex, u32 instances, u32 base_instance)
{
    draw_prep();
    GLenum type = G.ib16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    if (instances == 1 && !base_instance) {
        if (base_vertex) glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)count, type, index_ptr(first_index), base_vertex);
        else glDrawElements(GL_TRIANGLES, (GLsizei)count, type, index_ptr(first_index));
    } else if (!base_vertex && !base_instance) {
        glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)count, type, index_ptr(first_index), (GLsizei)instances);
    } else {
        glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, (GLsizei)count, type, index_ptr(first_index),
                                                      (GLsizei)instances, base_vertex, base_instance);
    }
}

void rb_draw_indexed_multi(const u32 *counts, const u32 *first_indices, u32 n)
{
    if (!n) return;
    draw_prep();
    GLsizei c[64];
    const void *o[64];
    for (u32 i = 0; i < n;) {
        u32 k = MIN(n - i, 64u);
        for (u32 j = 0; j < k; j++) {
            c[j] = (GLsizei)counts[i + j];
            o[j] = index_ptr(first_indices[i + j]);
        }
        glMultiDrawElements(GL_TRIANGLES, c, G.ib16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, o, (GLsizei)k);
        i += k;
    }
}

void rb_draw_indexed_indirect(RbBuf b, size_t offset, u32 n)
{
    draw_prep();
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, G.buf[b].name);
    glMultiDrawElementsIndirect(GL_TRIANGLES, G.ib16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (const void *)offset, (GLsizei)n, 0);
}

void rb_dispatch(u32 x, u32 y, u32 z)
{
    draw_prep();
    glDispatchCompute(x, y, z);
    // Что посчитано, видно всем следующим чтениям: буферы, текстуры, команды
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
}

void rb_dispatch_indirect(RbBuf b, size_t offset)
{
    draw_prep();
    glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, G.buf[b].name);
    glDispatchComputeIndirect((GLintptr)offset);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
}

// --- Метки времени

bool rb_timer_init(u32 sets, u32 marks)
{
    if (sets * marks > ARRAY_COUNT(timers_store)) return false;
    G.timer = timers_store;
    G.timer_sets = sets;
    G.timer_marks = marks;
    glCreateQueries(GL_TIMESTAMP, (GLsizei)(sets * marks), G.timer);
    return true;
}

void rb_timer_mark(u32 set, u32 mark) { glQueryCounter(G.timer[set * G.timer_marks + mark], GL_TIMESTAMP); }

bool rb_timer_ready(u32 set, u32 mark)
{
    GLuint ready = 0;
    glGetQueryObjectuiv(G.timer[set * G.timer_marks + mark], GL_QUERY_RESULT_AVAILABLE, &ready);
    return ready != 0;
}

u64 rb_timer_get(u32 set, u32 mark)
{
    GLuint64 t = 0;
    glGetQueryObjectui64v(G.timer[set * G.timer_marks + mark], GL_QUERY_RESULT, &t);
    return t;
}

void rb_read_window(u8 *rgb, i32 w, i32 h)
{
    bind_fbo(0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
}

void rb_shutdown(void)
{
    for (u32 i = 1; i < MAX_PIPES; i++)
        if (G.pipe[i].used) {
            RbPipe p = i;
            rb_pipe_free(&p);
        }
    for (u32 i = 1; i < MAX_TEXS; i++)
        if (G.tex[i].name) {
            RbTex t = i;
            rb_tex_free(&t);
        }
    for (u32 i = 1; i < MAX_BUFS; i++)
        if (G.buf[i].name) {
            RbBuf b = i;
            rb_buf_free(&b);
        }
    for (u32 i = 0; i < MAX_FBOS; i++)
        if (G.fbo[i].name) glDeleteFramebuffers(1, &G.fbo[i].name);
    if (G.timer) glDeleteQueries((GLsizei)(G.timer_sets * G.timer_marks), G.timer);
    if (G.samplers[0]) glDeleteSamplers(RB_SMP_COUNT, G.samplers);
    if (G.zero_buf) glDeleteBuffers(1, &G.zero_buf);
    memset(&G, 0, sizeof G);
}
