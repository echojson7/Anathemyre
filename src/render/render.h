// Рендерер: сцена в низком разрешении -> атмосфера -> стилизация -> апскейл
#ifndef ANA_RENDER_H
#define ANA_RENDER_H

#include "../core/arena.h"
#include "../core/math3d.h"
#include "mesh.h"

#define RENDER_MAX_BONES 24

typedef struct GpuMesh {
    u32 vao, vbo, ibo;
    u32 index_count;
} GpuMesh;

// Один вызов отрисовки; bones == NULL для статики (вершины уже в мировых координатах)
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
    u32  filter;        // фильтр кадра: 0 — без фильтра, 1..RENDER_FILTERS-1 — пресеты
} RenderSettings;

#define RENDER_FILTERS 5

// Время проходов на GPU, мс, среднее с прошлого render_take_stats
enum { PASS_SHADOW, PASS_SCENE, PASS_ATMOS, PASS_STYLE, PASS_BLIT, PASS_COUNT };
typedef struct RenderStats {
    f64 gpu_ms[PASS_COUNT];
    f64 gpu_total_ms;
    u32 samples;
} RenderStats;

bool render_init(Arena *scratch, const char *data_dir);
void render_take_stats(RenderStats *out);
void render_shutdown(void);
bool render_reload_shaders(void);
bool gpu_mesh_upload(GpuMesh *gm, const MeshBuilder *mb);
void render_frame(const FrameView *fv, const DrawItem *items, u32 count, const RenderSettings *rs, i32 win_w, i32 win_h);
// RGB, снизу вверх, размер окна
void render_read_pixels(u8 *rgb, i32 w, i32 h);

#endif
