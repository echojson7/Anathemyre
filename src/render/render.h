// Рендерер: сцена в низком разрешении -> атмосфера -> стилизация -> апскейл
#ifndef ANA_RENDER_H
#define ANA_RENDER_H

#include "../core/arena.h"
#include "../core/math3d.h"
#include "mesh.h"

#define RENDER_MAX_BONES 24
// Последний слот набора — не кость, а внешность экземпляра (scene.vert): числа 0..7 — веса морфов,
// 8..10 и 12..14 — оттенок кожи и волос минус 1. Нулевая матрица — сетка как есть
#define RENDER_LOOK_SLOT 23
#define RENDER_MAX_ITEMS 4096    // элементов отрисовки за кадр
#define RENDER_MAX_POSED 1024    // разных наборов костей за кадр (персонаж с оружием — один набор)
#define RENDER_PORTRAIT_PX 96     // наибольший портрет, пикселей
#define RENDER_PORTRAIT_SLOTS 10

typedef struct GpuMesh {
    u32 vao, vbo, ibo, skin_vbo;  // skin_vbo — веса до 4 костей, 0 у жёстких сеток
    u32 morph_buf, morph_count;   // сдвиги морфов внешности (mesh.h), 0 — нет
    u32 index_count;
    V4  bound;                          // сфера всей сетки: центр xyz, радиус w
    V4  bone_bound[RENDER_MAX_BONES];   // сфера вершин каждой кости в позе привязки
    u32 bone_mask;                      // какие кости есть в сетке
    // Уровень детализации: если сфера на экране меньше lod_px пикселей, рисуется lod_next
    const struct GpuMesh *lod_next;
    f32 lod_px;
} GpuMesh;

// Что рисовать; bones == NULL для статики (вершины уже в мировых координатах).
// Видимость и уровень детализации рендер выбирает сам по границам сетки
typedef struct DrawItem {
    const GpuMesh *mesh;
    const M4      *bones;
    bool           no_shadow;  // не отбрасывает тень (рельеф, дальние мегаструктуры)
} DrawItem;

typedef struct FrameView {
    M4  view, proj;
    V3  cam_pos;
    V3  sun_dir;        // направление НА главный свет (солнце или луна)
    V3  sun_color;
    V3  sky_amb, ground_amb;  // рассеянный свет неба и отражённый от земли
    V3  haze, zenith;         // цвет дымки и неба в зените
    V3  shadow_center;  // центр области теней
    f32 time;
    u32 frame;
} FrameView;

typedef struct RenderSettings {
    i32  internal_h;    // высота внутреннего разрешения
    bool post;          // фильтр кадра вкл/выкл
    bool pixel;         // пиксельность (квадраты на поверхностях и блоки вблизи) вкл/выкл
    bool vrs;           // шейдинг блоками там, где фильтр сливает пиксели (если есть у видеокарты)
} RenderSettings;

// Время проходов на GPU, мс, среднее с прошлого render_take_stats
// глубина — проход глубины и карты пикселей; интерфейс — портреты и HUD после render_frame
enum { PASS_SHADOW, PASS_DEPTH, PASS_SCENE, PASS_ATMOS, PASS_STYLE, PASS_BLIT, PASS_UI, PASS_COUNT };
typedef struct RenderStats {
    f64 gpu_ms[PASS_COUNT];
    f64 gpu_total_ms;
    u32 samples;
    // Среднее за кадр: вызовы отрисовки и треугольники в проходах теней и сцены, видимые элементы
    f64 draws[2], tris[2], visible[2];
    u32 items;          // элементов в последнем кадре
    bool vrs;           // последний кадр шейдился блоками вблизи
} RenderStats;

bool render_init(Arena *scratch, const char *data_dir);
void render_take_stats(RenderStats *out);
void render_shutdown(void);
bool render_reload_shaders(void);
bool gpu_mesh_upload(GpuMesh *gm, const MeshBuilder *mb);
// Сделать coarse следующим уровнем детализации fine: fine рисуется, пока сфера не меньше min_px пикселей
void gpu_mesh_set_lod(GpuMesh *fine, const GpuMesh *coarse, f32 min_px);
void render_frame(const FrameView *fv, const DrawItem *items, u32 count, const RenderSettings *rs, i32 win_w, i32 win_h);
// Конец кадра GPU: после портретов и интерфейса, перед swap
void render_frame_end(void);
// Портрет в ячейку slot атласа (size x size от левого нижнего угла ячейки), после render_frame:
// тот же свет и тени, фон прозрачный. cam_pos во fv — глаз портретной камеры
void render_portrait(u32 slot, i32 size, const FrameView *fv, M4 view_proj, const DrawItem *items, u32 count);
u32  render_portrait_texture(void);  // HDR, ячейки в ряд, RENDER_PORTRAIT_PX
// Программа из двух файлов data/shaders (для слоёв поверх сцены: интерфейс)
u32  render_program(const char *vs_file, const char *fs_file);
// RGB, снизу вверх, размер окна
void render_read_pixels(u8 *rgb, i32 w, i32 h);

#endif
