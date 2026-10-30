// Бэкенды слоя rb: таблица функций. rb.c выбирает бэкенд и зовёт его через таблицу,
// render.c, terrain.c и ui.c видят только rb.h. Внутри бэкенда функции зовутся glb_* (GL) и vkb_* (Vulkan)
#ifndef ANA_RB_BACKEND_H
#define ANA_RB_BACKEND_H

#include "rb.h"

// R — функция с результатом, V — без: тип, имя, параметры, аргументы
#define RB_FUNCS(R, V)                                                                                          \
    R(bool, init, (Arena *scratch, const char *data_dir), (scratch, data_dir))                                  \
    V(shaders_changed, (void), ())                                                                              \
    V(shutdown, (void), ())                                                                                     \
    R(const RbCaps *, caps, (void), ())                                                                         \
    V(frame_begin, (i32 win_w, i32 win_h), (win_w, win_h))                                                      \
    R(u32, frame_slot, (void), ())                                                                              \
    V(frame_end, (void), ())                                                                                    \
    R(RbBuf, buf_make, (size_t size, u32 usage, const void *data), (size, usage, data))                         \
    V(buf_write, (RbBuf b, size_t offset, size_t size, const void *data), (b, offset, size, data))              \
    V(buf_free, (RbBuf * b), (b))                                                                               \
    R(RbTex, tex_make, (const RbTexDesc *d), (d))                                                               \
    V(tex_write, (RbTex t, u32 mip, i32 x, i32 y, i32 z, i32 w, i32 h, i32 d, const void *data, u32 row_len),   \
      (t, mip, x, y, z, w, h, d, data, row_len))                                                                \
    V(tex_free, (RbTex * t), (t))                                                                               \
    V(tex_size, (RbTex t, i32 *w, i32 *h), (t, w, h))                                                           \
    R(RbPipe, pipe_make, (const RbPipeDesc *d), (d))                                                            \
    V(pipe_free, (RbPipe * p), (p))                                                                             \
    V(pass_begin, (const RbPass *p), (p))                                                                       \
    V(pass_end, (void), ())                                                                                     \
    V(viewport, (i32 x, i32 y, i32 w, i32 h), (x, y, w, h))                                                     \
    V(scissor, (i32 x, i32 y, i32 w, i32 h), (x, y, w, h))                                                      \
    V(scissor_off, (void), ())                                                                                  \
    V(clear_rect, (i32 x, i32 y, i32 w, i32 h, const f32 *color, bool depth), (x, y, w, h, color, depth))      \
    V(bind_pipe, (RbPipe p), (p))                                                                               \
    V(bind_vertex, (u32 stream, RbBuf b, size_t offset), (stream, b, offset))                                   \
    V(bind_index, (RbBuf b, size_t offset, bool idx16), (b, offset, idx16))                                     \
    V(bind_ubo, (u32 binding, RbBuf b, size_t offset, size_t size), (binding, b, offset, size))                 \
    V(bind_ssbo, (u32 binding, RbBuf b, size_t offset, size_t size), (binding, b, offset, size))                \
    V(bind_tex, (u32 binding, RbTex t, RbSampler s), (binding, t, s))                                           \
    V(bind_image, (u32 binding, RbTex t, u32 mip), (binding, t, mip))                                           \
    V(push, (u32 offset, u32 size, const void *data), (offset, size, data))                                     \
    V(draw, (u32 verts, u32 first, u32 instances), (verts, first, instances))                                   \
    V(draw_indexed, (u32 count, u32 first_index, i32 base_vertex, u32 instances, u32 base_instance),            \
      (count, first_index, base_vertex, instances, base_instance))                                              \
    V(draw_indexed_multi, (const u32 *counts, const u32 *first_indices, u32 n), (counts, first_indices, n))     \
    V(draw_indexed_indirect, (RbBuf b, size_t offset, u32 n), (b, offset, n))                                   \
    V(dispatch, (u32 x, u32 y, u32 z), (x, y, z))                                                               \
    V(dispatch_indirect, (RbBuf b, size_t offset), (b, offset))                                                 \
    R(bool, timer_init, (u32 sets, u32 marks), (sets, marks))                                                   \
    V(timer_mark, (u32 set, u32 mark), (set, mark))                                                             \
    R(bool, timer_ready, (u32 set, u32 mark), (set, mark))                                                      \
    R(u64, timer_get, (u32 set, u32 mark), (set, mark))                                                         \
    V(read_window, (u8 * rgb, i32 w, i32 h), (rgb, w, h))

typedef struct RbBackend {
#define RB_FIELD_R(ret, name, params, args) ret (*name) params;
#define RB_FIELD_V(name, params, args) void (*name) params;
    RB_FUNCS(RB_FIELD_R, RB_FIELD_V)
#undef RB_FIELD_R
#undef RB_FIELD_V
} RbBackend;

extern const RbBackend rb_gl_backend;
extern const RbBackend rb_vk_backend;

// Стадии шейдера в рецепте SPIR-V
typedef enum RbStage { RB_STAGE_VERT, RB_STAGE_FRAG, RB_STAGE_COMP } RbStage;

// Рецепт SPIR-V стадии: «стадия [-DИМЯ[=ЗНАЧ] ...] файлы» — строка tools/spv/list.txt.
// Имя .spv из рецепта: файлы через «+», определения через «~», стадия: frame.glsl+scene.vert~SHADOW_PASS.vert.spv
bool rb_spv_recipe(const RbPipeDesc *d, RbStage stage, char *line, size_t line_size, char *name, size_t name_size);

#endif
