// Слой рендера: всё, что render.c, terrain.c и ui.c говорят видеокарте, идёт через эти функции.
// Под слоем бэкенд: OpenGL 4.5 (rb_gl.c) или Vulkan 1.3 (rb_vk.c), выбор — rb_select, вызовы — через rb.c.
// Шейдеры общие (GLSL 450, rb.glsl); для Vulkan собраны офлайн в data/spv (tools/spv).
// Правила:
// - Привязки (rb_bind_*) живут до перепривязки, как блоки текстур в GL; смена конвейера их не сбрасывает.
// - Буфер RB_BUF_DYNAMIC пишется с CPU каждый кадр: кусок, который читает кадр, не переписывается раньше
//   чем через RB_FRAMES кадров (кольцо по rb_frame_slot), иначе Vulkan прочтёт уже новые данные.
//   В остальные буферы rb_buf_write — редкая загрузка (сетки), она встаёт перед следующими командами.
// - Барьеры между проходами ставит слой: он знает, что проход пишет (цели, rb_bind_image) и что читает.
// - Кадр лежит в памяти как в GL: строка 0 внизу картинки, gl_FragCoord и uv от неё же.
#ifndef ANA_RB_H
#define ANA_RB_H

#include "../core/arena.h"
#include "../core/types.h"

#define RB_FRAMES 3        // кадров в полёте: кольца динамических буферов
#define RB_PUSH_BYTES 128  // константы вызова (rb_push), uvec4 u_push[8] в rb.glsl
#define RB_MAX_COLORS 4
#define RB_MAX_ATTRS 8
#define RB_MAX_STREAMS 4

typedef u32 RbBuf;   // 0 — нет
typedef u32 RbTex;
typedef u32 RbPipe;

typedef enum RbFmt {
    RB_FMT_NONE,
    RB_FMT_R8, RB_FMT_R16, RB_FMT_RGBA8,
    RB_FMT_R8UI, RB_FMT_RG16UI, RB_FMT_R32UI,
    RB_FMT_R16F, RB_FMT_RG16F, RB_FMT_RGBA16F, RB_FMT_R11G11B10F, RB_FMT_R32F,
    RB_FMT_D24, RB_FMT_D32F,
    RB_FMT_COUNT
} RbFmt;

// Назначение буфера; DYNAMIC — пишется с CPU каждый кадр
enum {
    RB_BUF_VERTEX = 1, RB_BUF_INDEX = 2, RB_BUF_UNIFORM = 4, RB_BUF_STORAGE = 8, RB_BUF_INDIRECT = 16,
    RB_BUF_DYNAMIC = 32,
};

typedef enum RbTexKind { RB_TEX_2D, RB_TEX_2D_ARRAY, RB_TEX_3D } RbTexKind;

// Назначение текстуры
enum { RB_TEX_SAMPLED = 1, RB_TEX_TARGET = 2, RB_TEX_STORAGE = 4 };

typedef struct RbTexDesc {
    RbTexKind kind;
    RbFmt     fmt;
    i32       w, h, d;   // d — слоёв у массива, глубина у 3D; 0 считается за 1
    u32       mips;      // 0 считается за 1
    u32       usage;
} RbTexDesc;

typedef enum RbSampler {
    RB_SMP_NEAREST,         // ближайший, край прижат
    RB_SMP_LINEAR,
    RB_SMP_NEAREST_REPEAT,
    RB_SMP_LINEAR_REPEAT,
    RB_SMP_SHADOW,          // линейный со сравнением LEQUAL: sampler2DShadow, sampler2DArrayShadow
    RB_SMP_COUNT
} RbSampler;

// Формат вершинного атрибута
typedef enum RbAttrFmt { RB_ATTR_F32x2, RB_ATTR_F32x3, RB_ATTR_UN8x4, RB_ATTR_U8x4, RB_ATTR_U32x4 } RbAttrFmt;

typedef struct RbAttr {
    u8  location, stream;
    u8  fmt;     // RbAttrFmt
    u16 offset;
} RbAttr;

// Раскладка вершин: атрибуты и потоки (шаг, по экземплярам). Пустая — вершины без буферов (номер вершины)
typedef struct RbLayout {
    RbAttr attr[RB_MAX_ATTRS];
    u32    attrs;
    u32    stride[RB_MAX_STREAMS];
    bool   per_instance[RB_MAX_STREAMS];
} RbLayout;

typedef enum RbCull { RB_CULL_NONE, RB_CULL_BACK } RbCull;
typedef enum RbDepth { RB_DEPTH_OFF, RB_DEPTH_LESS, RB_DEPTH_LEQUAL } RbDepth;
typedef enum RbBlend {
    RB_BLEND_NONE,
    RB_BLEND_ALPHA,      // цвет по альфе, альфа копится умноженной: слой интерфейса
    RB_BLEND_PREMUL,     // цвет уже умножен на альфу
    RB_BLEND_ADD,        // сложение: bloom, рассеяние
} RbBlend;

// Шейдер — список файлов из data/shaders, склеиваются по порядку после rb.glsl
#define RB_SHADER_FILES 6

typedef struct RbPipeDesc {
    const char *vs[RB_SHADER_FILES], *fs[RB_SHADER_FILES], *cs[RB_SHADER_FILES];  // cs — вычислительный
    const char *defines;    // строки #define, общие для стадий
    const char *name;       // для журнала
    RbLayout    layout;
    RbCull      cull;
    RbDepth     depth;
    bool        depth_write;
    bool        no_color;   // не писать цвет (проход глубины в цель с цветом)
    bool        depth_clamp;
    f32         bias_slope, bias_const;  // сдвиг глубины (тени); 0 и 0 — без сдвига
    RbBlend     blend;
    RbFmt       color[RB_MAX_COLORS];    // форматы целей прохода: окно — RB_FMT_NONE в color[0] и depth
    RbFmt       depth_fmt;
    bool        vrs;        // рисуется в проходе с картой частоты шейдинга
    bool        frag_size;  // фрагментный шейдер читает размер блока шейдинга: RB_FRAG_SIZE (если vrs есть)
} RbPipeDesc;

// Цель прохода: текстура, её мип и слой
typedef struct RbAttach {
    RbTex tex;
    u16   mip, layer;
} RbAttach;

typedef struct RbPass {
    RbAttach color[RB_MAX_COLORS];   // нет ни цвета, ни глубины — окно
    RbAttach depth;
    bool     clear_color, clear_depth;
    f32      clear[4];
    RbTex    rate;                   // карта частоты шейдинга (R8UI, значения 0..2), 0 — нет
} RbPass;

typedef struct RbCaps {
    bool vrs;           // карта частоты шейдинга
    i32  vrs_tile[2];   // клетка карты в пикселях
    u32  ubo_align;     // выравнивание смещения rb_bind_ubo
    u32  ssbo_align;
} RbCaps;

// Бэкенд: выбирается до rb_init (по умолчанию GL)
typedef enum RbApi { RB_API_GL, RB_API_VULKAN } RbApi;
void  rb_select(RbApi api);
RbApi rb_api(void);

bool rb_init(Arena *scratch, const char *data_dir);
// Шейдеры перечитываются с диска: следующие rb_pipe_make собирают заново, а не берут собранное
void rb_shaders_changed(void);
void rb_shutdown(void);
const RbCaps *rb_caps(void);

// Кадр: размер окна (проход без целей рисует в него) и номер слота колец 0..RB_FRAMES-1
void rb_frame_begin(i32 win_w, i32 win_h);
u32  rb_frame_slot(void);
// Отправить накопленное на GPU и перейти к следующему слоту
void rb_frame_end(void);

RbBuf rb_buf_make(size_t size, u32 usage, const void *data);
void  rb_buf_write(RbBuf b, size_t offset, size_t size, const void *data);
void  rb_buf_free(RbBuf *b);

RbTex rb_tex_make(const RbTexDesc *d);
// Кусок w×h×d мипа mip с (x, y, z); строки данных по row_len текселей (0 — w), слои по row_len·h
void  rb_tex_write(RbTex t, u32 mip, i32 x, i32 y, i32 z, i32 w, i32 h, i32 d, const void *data, u32 row_len);
void  rb_tex_free(RbTex *t);
void  rb_tex_size(RbTex t, i32 *w, i32 *h);

// 0 — шейдер не собрался (причина в журнале)
RbPipe rb_pipe_make(const RbPipeDesc *d);
void   rb_pipe_free(RbPipe *p);

void rb_pass_begin(const RbPass *p);
void rb_pass_end(void);
// По умолчанию область — вся цель прохода, ножницы выключены
void rb_viewport(i32 x, i32 y, i32 w, i32 h);
void rb_scissor(i32 x, i32 y, i32 w, i32 h);
void rb_scissor_off(void);
// Очистить прямоугольник целей прохода: цвет (color не NULL) и/или глубину в 1
void rb_clear_rect(i32 x, i32 y, i32 w, i32 h, const f32 *color, bool depth);

void rb_bind_pipe(RbPipe p);
// b = 0 — поток нулей (шаг 0): сетка без весов костей в раскладке с ними
void rb_bind_vertex(u32 stream, RbBuf b, size_t offset);
void rb_bind_index(RbBuf b, size_t offset, bool idx16);
// size 0 — весь буфер от offset
void rb_bind_ubo(u32 binding, RbBuf b, size_t offset, size_t size);
void rb_bind_ssbo(u32 binding, RbBuf b, size_t offset, size_t size);
void rb_bind_tex(u32 binding, RbTex t, RbSampler s);
// Текстура для imageLoad/imageStore (мип mip, все слои)
void rb_bind_image(u32 binding, RbTex t, u32 mip);
// Константы вызова: кусок u_push с offset байт, offset и size кратны 16. Живут до перезаписи, как привязки
void rb_push(u32 offset, u32 size, const void *data);

void rb_draw(u32 verts, u32 first, u32 instances);
void rb_draw_indexed(u32 count, u32 first_index, i32 base_vertex, u32 instances, u32 base_instance);
// Несколько кусков индексов подряд одним вызовом (экземпляр один, без сдвига вершин)
void rb_draw_indexed_multi(const u32 *counts, const u32 *first_indices, u32 n);
// n команд по 20 байт (число индексов, экземпляров, первый индекс, сдвиг вершин, первый экземпляр)
void rb_draw_indexed_indirect(RbBuf b, size_t offset, u32 n);
void rb_dispatch(u32 x, u32 y, u32 z);
// Команда из буфера: три u32
void rb_dispatch_indirect(RbBuf b, size_t offset);

// Метки времени GPU: sets наборов по marks меток, чтение без ожидания
bool rb_timer_init(u32 sets, u32 marks);
void rb_timer_mark(u32 set, u32 mark);
bool rb_timer_ready(u32 set, u32 mark);
u64  rb_timer_get(u32 set, u32 mark);   // нс

// Кадр в окне, RGB построчно снизу вверх
void rb_read_window(u8 *rgb, i32 w, i32 h);

#endif
