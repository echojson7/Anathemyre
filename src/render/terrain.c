#include "terrain.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../core/log.h"
#include "gl.h"

// Сетка уровня: TERRAIN_CELLS клеток, G узлов на сторону; для отсечения — BLOCKS×BLOCKS кусков.
// Узел (0,0) уровня стоит на чётном узле его шага, поэтому уровень мельче лежит в дыре крупного
// со сдвигом HOLE_MIN..HOLE_MIN+2 клеток: на каждый сдвиг по X и Z свой список треугольников
#define G (TERRAIN_CELLS + 1)
#define BLOCKS 4
#define BC (TERRAIN_CELLS / BLOCKS)
#define HOLE (TERRAIN_CELLS / 2)
#define HOLE_MIN (TERRAIN_CELLS / 4 - 1)
#define VARIANTS 10          // 9 видов дыры и сетка целиком (уровень 0)
// Высоты и цвет уровня — в текстурах TEX×TEX по кругу: узел g лежит в (g & MASK). Заполнено окно
// WIN узлов: сетка и BORDER узлов за краем (нормаль крупного уровня — по соседям через один)
#define TEX 256
#define MASK (TEX - 1)
#define BORDER 2
#define WIN (G + 2 * BORDER)
// Центр колец не дальше этого от глаза: с далёкой камеры подробная земля под ногами у кадра
#define FOCUS_MAX 40.0f

// Расположения uniform (scene.vert); U_SKINNED — как в render.c
enum { U_SKINNED = 17, U_TERR_GRID = 49, U_TERR_VIEW = 50 };
#define SKIN_TERRAIN 3

static struct {
    TerrainFill fill;
    void *user;
    u32  tex_h, tex_c, ibo, vao;
    u32  off[VARIANTS][BLOCKS * BLOCKS], cnt[VARIANTS][BLOCKS * BLOCKS];  // куски в буфере индексов: байт, индексов
    f32  cx, cz;                         // центр колец
    i32  og[TERRAIN_LEVELS][2];          // узел (0,0) уровня в узлах уровня
    bool valid[TERRAIN_LEVELS];
    f32  lo[TERRAIN_LEVELS][BLOCKS * BLOCKS], hi[TERRAIN_LEVELS][BLOCKS * BLOCKS];  // высоты кусков
    bool warned;
} T;

static f32 g_h[TERRAIN_LEVELS][TEX * TEX];  // копия высот: границы кусков для отсечения
static f32 g_fill_h[WIN * WIN];
static u32 g_fill_c[WIN * WIN];

// Ориентированная площадь в узлах сетки (x — столбец, z — строка)
static i32 cross2(i32 ax, i32 az, i32 bx, i32 bz, i32 cx, i32 cz)
{
    return (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
}

static u32 emit_cell(u16 *out, u32 n, u32 i, u32 k)
{
    u16 p00 = (u16)(i * G + k), p01 = (u16)(p00 + 1), p10 = (u16)(p00 + G), p11 = (u16)(p10 + 1);
    u16 t[6] = { p00, p10, p11, p00, p11, p01 };
    memcpy(out + n, t, sizeof t);
    return n + 6;
}

// Вырожденный треугольник на краю (A, B, C подряд): закрывает щель, если B на экране чуть внутри
// от ребра AC уровня крупнее. Порядок вершин — чтобы при таком сдвиге он смотрел лицом, как сетка
static u32 emit_seam(u16 *out, u32 n, i32 ai, i32 ak, i32 di, i32 dk, i32 ni, i32 nk)
{
    i32 bi = ai + di, bk = ak + dk, ci = ai + 2 * di, ck = ak + 2 * dk;
    i32 face = cross2(0, 0, 0, 1, 1, 1);  // как (p00, p10, p11)
    bool acb = (cross2(ak, ai, ck, ci, bk + nk, bi + ni) > 0) == (face > 0);
    u16 a = (u16)(ai * G + ak), b = (u16)(bi * G + bk), c = (u16)(ci * G + ck);
    u16 t[3] = { a, acb ? c : b, acb ? b : c };
    memcpy(out + n, t, sizeof t);
    return n + 3;
}

bool terrain_init(Arena *scratch)
{
    size_t mark = scratch->used;
    u32 cap = VARIANTS * (TERRAIN_CELLS * TERRAIN_CELLS * 6 + 4 * TERRAIN_CELLS * 3);
    u16 *idx = arena_push(scratch, sizeof(u16) * cap, 4);
    if (!idx) {
        log_error("РЕЛЬЕФ: нет памяти на сетку");
        return false;
    }
    u32 n = 0;
    for (u32 v = 0; v < VARIANTS; v++) {
        u32 hx = HOLE_MIN + v % 3, hz = HOLE_MIN + v / 3;
        for (u32 b = 0; b < BLOCKS * BLOCKS; b++) {
            u32 i0 = (b / BLOCKS) * BC, k0 = (b % BLOCKS) * BC, start = n;
            for (u32 i = i0; i < i0 + BC; i++)
                for (u32 k = k0; k < k0 + BC; k++) {
                    bool hole = v < 9 && i >= hz && i < hz + HOLE && k >= hx && k < hx + HOLE;
                    if (!hole) n = emit_cell(idx, n, i, k);
                }
            // Швы по внешнему краю уровня
            for (u32 k = k0; k < k0 + BC; k += 2) {
                if (i0 == 0) n = emit_seam(idx, n, 0, (i32)k, 0, 1, 1, 0);
                if (i0 + BC == TERRAIN_CELLS) n = emit_seam(idx, n, TERRAIN_CELLS, (i32)k, 0, 1, -1, 0);
            }
            for (u32 i = i0; i < i0 + BC; i += 2) {
                if (k0 == 0) n = emit_seam(idx, n, (i32)i, 0, 1, 0, 0, 1);
                if (k0 + BC == TERRAIN_CELLS) n = emit_seam(idx, n, (i32)i, TERRAIN_CELLS, 1, 0, 0, -1);
            }
            T.off[v][b] = start * (u32)sizeof(u16);
            T.cnt[v][b] = n - start;
        }
    }
    glCreateBuffers(1, &T.ibo);
    glNamedBufferStorage(T.ibo, (GLsizeiptr)(sizeof(u16) * n), idx, 0);
    scratch->used = mark;
    glCreateVertexArrays(1, &T.vao);
    glVertexArrayElementBuffer(T.vao, T.ibo);
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &T.tex_h);
    glTextureStorage3D(T.tex_h, 1, GL_R32F, TEX, TEX, TERRAIN_LEVELS);
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &T.tex_c);
    glTextureStorage3D(T.tex_c, 1, GL_RGBA8, TEX, TEX, TERRAIN_LEVELS);
    u32 tex[2] = { T.tex_h, T.tex_c };
    for (u32 i = 0; i < 2; i++) {
        glTextureParameteri(tex[i], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(tex[i], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    // Блоки 8 и 9 больше никто не занимает (scene.vert)
    glBindTextureUnit(8, T.tex_h);
    glBindTextureUnit(9, T.tex_c);
    return true;
}

void terrain_shutdown(void)
{
    if (T.ibo) glDeleteBuffers(1, &T.ibo);
    if (T.vao) glDeleteVertexArrays(1, &T.vao);
    if (T.tex_h) glDeleteTextures(1, &T.tex_h);
    if (T.tex_c) glDeleteTextures(1, &T.tex_c);
    memset(&T, 0, sizeof T);
}

void terrain_source(TerrainFill fill, void *user)
{
    T.fill = fill;
    T.user = user;
    memset(T.valid, 0, sizeof T.valid);
}

// Узлы [gx, gx+w) × [gz, gz+h) уровня: у источника, в копию и в текстуры (по кругу — до четырёх кусков)
static void fill_rect(u32 l, i32 gx, i32 gz, u32 w, u32 h)
{
    f32 s = (f32)(1u << l);
    T.fill((f32)gx * s, (f32)gz * s, s, w, h, g_fill_h, g_fill_c, T.user);
    for (u32 i = 0; i < h; i++)
        for (u32 k = 0; k < w; k++)
            g_h[l][((u32)(gz + (i32)i) & MASK) * TEX + ((u32)(gx + (i32)k) & MASK)] = g_fill_h[i * w + k];
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)w);
    for (u32 z = 0; z < h;) {
        u32 tz = (u32)(gz + (i32)z) & MASK, hz = MIN(h - z, (u32)TEX - tz);
        for (u32 x = 0; x < w;) {
            u32 tx = (u32)(gx + (i32)x) & MASK, wx = MIN(w - x, (u32)TEX - tx);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, (GLint)x);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, (GLint)z);
            glTextureSubImage3D(T.tex_h, 0, (GLint)tx, (GLint)tz, (GLint)l, (GLsizei)wx, (GLsizei)hz, 1, GL_RED, GL_FLOAT, g_fill_h);
            glTextureSubImage3D(T.tex_c, 0, (GLint)tx, (GLint)tz, (GLint)l, (GLsizei)wx, (GLsizei)hz, 1, GL_RGBA, GL_UNSIGNED_BYTE, g_fill_c);
            x += wx;
        }
        z += hz;
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
}

static void level_bounds(u32 l)
{
    for (u32 b = 0; b < BLOCKS * BLOCKS; b++) {
        u32 i0 = (b / BLOCKS) * BC, k0 = (b % BLOCKS) * BC;
        f32 lo = 1e9f, hi = -1e9f;
        for (u32 i = i0; i <= i0 + BC; i++) {
            const f32 *row = g_h[l] + ((u32)(T.og[l][1] + (i32)i) & MASK) * TEX;
            for (u32 k = k0; k <= k0 + BC; k++) {
                f32 y = row[(u32)(T.og[l][0] + (i32)k) & MASK];
                lo = fminf(lo, y);
                hi = fmaxf(hi, y);
            }
        }
        T.lo[l][b] = lo;
        T.hi[l][b] = hi;
    }
}

// Уровень за центром: узел (0,0) на чётном узле; дописать узлы, которых в окне ещё не было
static void level_update(u32 l)
{
    f32 s = (f32)(1u << l);
    i32 ox = 2 * (i32)floorf((T.cx / s - (f32)(TERRAIN_CELLS / 2)) * 0.5f);
    i32 oz = 2 * (i32)floorf((T.cz / s - (f32)(TERRAIN_CELLS / 2)) * 0.5f);
    if (T.valid[l] && ox == T.og[l][0] && oz == T.og[l][1]) return;
    i32 ax = T.og[l][0] - BORDER, az = T.og[l][1] - BORDER, bx = ox - BORDER, bz = oz - BORDER;
    if (!T.valid[l] || abs(bx - ax) >= WIN || abs(bz - az) >= WIN) {
        fill_rect(l, bx, bz, WIN, WIN);
    } else {
        if (bx > ax) fill_rect(l, ax + WIN, bz, (u32)(bx - ax), WIN);
        else if (bx < ax) fill_rect(l, bx, bz, (u32)(ax - bx), WIN);
        i32 x0 = MAX(ax, bx), x1 = MIN(ax, bx) + WIN;
        if (bz > az) fill_rect(l, x0, az + WIN, (u32)(x1 - x0), (u32)(bz - az));
        else if (bz < az) fill_rect(l, x0, bz, (u32)(x1 - x0), (u32)(az - bz));
    }
    T.og[l][0] = ox;
    T.og[l][1] = oz;
    T.valid[l] = true;
    level_bounds(l);
}

void terrain_update(V3 eye, V3 target)
{
    if (!T.fill) return;
    f32 dx = target.x - eye.x, dz = target.z - eye.z, d = sqrtf(dx * dx + dz * dz);
    f32 k = d > FOCUS_MAX ? FOCUS_MAX / d : 1.0f;
    T.cx = eye.x + dx * k;
    T.cz = eye.z + dz * k;
    for (u32 l = 0; l < TERRAIN_LEVELS; l++) level_update(l);
}

static bool box_visible(const V4 p[6], V3 lo, V3 hi)
{
    for (u32 i = 0; i < 6; i++) {
        V3 v = v3(p[i].x > 0.0f ? hi.x : lo.x, p[i].y > 0.0f ? hi.y : lo.y, p[i].z > 0.0f ? hi.z : lo.z);
        if (p[i].x * v.x + p[i].y * v.y + p[i].z * v.z + p[i].w < 0.0f) return false;
    }
    return true;
}

void terrain_draw(const V4 planes[6], u32 levels, f32 min_relief, u32 *draws, f64 *tris)
{
    if (!T.fill || !T.valid[0]) return;
    glBindVertexArray(T.vao);
    glUniform1i(U_SKINNED, SKIN_TERRAIN);
    GLsizei counts[BLOCKS * BLOCKS];
    const void *offs[BLOCKS * BLOCKS];
    for (u32 l = 0; l < MIN(levels, (u32)TERRAIN_LEVELS); l++) {
        u32 v = 9;
        if (l > 0) {
            // Где в этом уровне лежит уровень мельче
            i32 hx = T.og[l - 1][0] / 2 - T.og[l][0] - HOLE_MIN, hz = T.og[l - 1][1] / 2 - T.og[l][1] - HOLE_MIN;
            if (hx < 0 || hx > 2 || hz < 0 || hz > 2) {
                if (!T.warned) log_error("РЕЛЬЕФ: уровень %u не в дыре уровня крупнее (%d, %d)", l - 1, hx, hz);
                T.warned = true;
                continue;
            }
            v = (u32)(hx + 3 * hz);
        }
        f32 s = (f32)(1u << l);
        u32 n = 0;
        f64 t = 0;
        for (u32 b = 0; b < BLOCKS * BLOCKS; b++) {
            if (!T.cnt[v][b] || T.hi[l][b] - T.lo[l][b] < min_relief) continue;
            f32 x0 = (f32)(T.og[l][0] + (i32)((b % BLOCKS) * BC)) * s, z0 = (f32)(T.og[l][1] + (i32)((b / BLOCKS) * BC)) * s;
            if (!box_visible(planes, v3(x0, T.lo[l][b], z0), v3(x0 + BC * s, T.hi[l][b], z0 + BC * s))) continue;
            counts[n] = (GLsizei)T.cnt[v][b];
            offs[n++] = (const void *)(uintptr_t)T.off[v][b];
            t += T.cnt[v][b] / 3;
        }
        if (!n) continue;
        glUniform4i(U_TERR_GRID, T.og[l][0], T.og[l][1], (GLint)l, 0);
        glUniform4f(U_TERR_VIEW, s, T.cx, T.cz, 0.0f);
        glMultiDrawElements(GL_TRIANGLES, counts, GL_UNSIGNED_SHORT, offs, (GLsizei)n);
        *draws += 1;
        *tris += t;
    }
}
