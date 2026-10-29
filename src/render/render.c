#include "render.h"

#include <stdio.h>
#include <string.h>

#include "../core/log.h"
#include "gl.h"

#define SHADOW_SIZE 2048
#define SHADOW_EXTENT 45.0f   // полуразмер области теней, м

// Расположения uniform задаются явно в GLSL (layout(location = N))
enum {
    U_VIEW_PROJ = 0, U_SKINNED = 17, U_LIGHT_VP = 18,
    U_CAM_POS = 20, U_SUN_DIR = 21, U_SUN_COLOR = 22, U_TIME = 23, U_SKY_AMB = 24, U_GROUND_AMB = 25,
    U_A_INV_VP = 30, U_A_CAM = 31, U_A_SUN_DIR = 32, U_A_SUN_COLOR = 33,
    U_A_LIGHT_VP = 34, U_A_TIME = 35, U_A_FRAME = 36, U_A_PROJ_SCALE = 37, U_A_HAZE = 38, U_A_ZENITH = 39,
    U_S_POST = 41, U_S_FRAME = 42, U_U_OUT = 43,
    U_BONES = 50,  // занимает 50..50+RENDER_MAX_BONES-1
};

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
    u32    prog_scene, prog_shadow, prog_atmos, prog_style, prog_upscale;
    u32    empty_vao;
    u32    shadow_fbo, shadow_tex;
    Target scene, atmos, ldr;
    Target portrait;  // атлас портретов: ячейки RENDER_PORTRAIT_PX в ряд, HDR
    u32    timer[TIMER_FRAMES][TIMER_MARKS];
    bool   timer_pending[TIMER_FRAMES];
    u32    timer_slot;
    f64    stat_ms[PASS_COUNT];
    u32    stat_samples;
} R;

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
    const char *atmos_fs[] = { "common.glsl", "atmos.frag" };
    const char *style_fs[] = { "common.glsl", "style.frag" };
    const char *upscale_fs[] = { "upscale.frag" };

    u32 scene = link(compile(GL_VERTEX_SHADER, NULL, scene_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, scene_fs, 2));
    u32 shadow = link(compile(GL_VERTEX_SHADER, "#define SHADOW_PASS\n", scene_vs, 1),
                      compile(GL_FRAGMENT_SHADER, NULL, shadow_fs, 1));
    u32 atmos = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, atmos_fs, 2));
    u32 style = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, style_fs, 2));
    u32 upscale = link(compile(GL_VERTEX_SHADER, NULL, full_vs, 1), compile(GL_FRAGMENT_SHADER, NULL, upscale_fs, 1));
    R.scratch->used = mark;

    u32 progs[] = { scene, shadow, atmos, style, upscale };
    u32 *slots[] = { &R.prog_scene, &R.prog_shadow, &R.prog_atmos, &R.prog_style, &R.prog_upscale };
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
    target_free(&R.atmos);
    target_free(&R.ldr);
    target_free(&R.portrait);
    if (R.shadow_fbo) glDeleteFramebuffers(1, &R.shadow_fbo);
    if (R.shadow_tex) glDeleteTextures(1, &R.shadow_tex);
    if (R.empty_vao) glDeleteVertexArrays(1, &R.empty_vao);
    for (u32 i = 0; i < TIMER_FRAMES; i++)
        if (R.timer[i][0]) glDeleteQueries(TIMER_MARKS, R.timer[i]);
    u32 progs[] = { R.prog_scene, R.prog_shadow, R.prog_atmos, R.prog_style, R.prog_upscale };
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
    return true;
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
}

static void draw_items(const DrawItem *items, u32 count, bool shadow_pass)
{
    for (u32 i = 0; i < count; i++) {
        const DrawItem *it = &items[i];
        if (shadow_pass && it->no_shadow) continue;
        if (it->bones) {
            glUniform1i(U_SKINNED, 1);
            glUniformMatrix4fv(U_BONES, RENDER_MAX_BONES, GL_FALSE, it->bones[0].m);
        } else {
            glUniform1i(U_SKINNED, 0);
        }
        glBindVertexArray(it->mesh->vao);
        glDrawElements(GL_TRIANGLES, (GLsizei)it->mesh->index_count, GL_UNSIGNED_INT, NULL);
    }
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
    draw_items(items, count, true);
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
    draw_items(items, count, false);
    if (timing) mark(2);

    // 3. Атмосфера: небо, туман, объёмный свет, AO
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(R.empty_vao);
    glBindFramebuffer(GL_FRAMEBUFFER, R.atmos.fbo);
    glUseProgram(R.prog_atmos);
    M4 inv_vp = m4_inverse(vp);
    glUniformMatrix4fv(U_A_INV_VP, 1, GL_FALSE, inv_vp.m);
    glUniform3f(U_A_CAM, fv->cam_pos.x, fv->cam_pos.y, fv->cam_pos.z);
    glUniform3f(U_A_SUN_DIR, L.x, L.y, L.z);
    glUniform3f(U_A_SUN_COLOR, fv->sun_color.x, fv->sun_color.y, fv->sun_color.z);
    glUniformMatrix4fv(U_A_LIGHT_VP, 1, GL_FALSE, light_vp.m);
    glUniform1f(U_A_TIME, fv->time);
    glUniform1i(U_A_FRAME, (GLint)fv->frame);
    glUniform1f(U_A_PROJ_SCALE, fv->proj.m[5] * 0.5f * (f32)ih);
    glUniform3f(U_A_HAZE, fv->haze.x, fv->haze.y, fv->haze.z);
    glUniform3f(U_A_ZENITH, fv->zenith.x, fv->zenith.y, fv->zenith.z);
    glBindTextureUnit(0, R.scene.color);
    glBindTextureUnit(1, R.scene.depth);
    glBindTextureUnit(2, R.shadow_tex);
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
    // Тот же шейдер и свет, что у сцены; тень берётся из карты этого кадра
    M4 light_vp = light_matrix(fv);
    V3 L = v3_norm(fv->sun_dir);
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
    glUniformMatrix4fv(U_LIGHT_VP, 1, GL_FALSE, light_vp.m);
    glUniform3f(U_CAM_POS, fv->cam_pos.x, fv->cam_pos.y, fv->cam_pos.z);
    glUniform3f(U_SUN_DIR, L.x, L.y, L.z);
    glUniform3f(U_SUN_COLOR, fv->sun_color.x, fv->sun_color.y, fv->sun_color.z);
    glUniform1f(U_TIME, fv->time);
    glUniform3f(U_SKY_AMB, fv->sky_amb.x, fv->sky_amb.y, fv->sky_amb.z);
    glUniform3f(U_GROUND_AMB, fv->ground_amb.x, fv->ground_amb.y, fv->ground_amb.z);
    glBindTextureUnit(0, R.shadow_tex);
    draw_items(items, count, false);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

u32 render_portrait_texture(void) { return R.portrait.color; }
