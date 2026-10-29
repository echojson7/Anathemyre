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
#define RENDER_MAX_VIS_INST 131072  // видимых экземпляров (трава, деревья, камни) за проход
#define RENDER_MAX_LIGHTS 16        // точечных источников в кадре: ближайшие из видимых (common.glsl)

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
    // Для экземпляров: редеет с дальности fade[0] и пропадает к fade[1], м (0 — не редеет)
    f32  fade[2];
    bool two_sided;     // без отсечения задних граней (листья, трава)
} GpuMesh;

// Экземпляр статической сетки (32 байта, InstRec в scene.vert): деревья, трава, камни
typedef struct RInst {
    f32 pos[3];
    f32 yaw;
    f32 scale, hscale;  // по ширине и по высоте
    f32 lean;           // наклон оси вверх, рад; в сторону локальной X после поворота
    u32 tint;           // RGB — множитель цвета (128 = 1.0), старший байт — порядок прореживания 0..255
} RInst;

// Группа экземпляров одной сетки, подряд в постоянном буфере. Рендер отсекает группу, затем каждый
// экземпляр (bulk — без проверки по одному, для травы), выбирает LOD и сводит одинаковые сетки в один вызов.
// У редеющих сеток экземпляры в группе идут по возрастанию порядка прореживания
typedef struct InstGroup {
    const GpuMesh *mesh;
    u32  first, count;
    V4   bound;         // сфера всей группы
    bool bulk, no_shadow;
} InstGroup;

// Участок: группы first..first+count подряд и общая сфера. Невидимый участок отбрасывается целиком,
// у целиком видимого группы и экземпляры не проверяются по одному
typedef struct InstCluster {
    V4  bound;
    u32 first, count;
} InstCluster;

// Что рисовать; bones == NULL для статики (вершины уже в мировых координатах).
// Видимость и уровень детализации рендер выбирает сам по границам сетки
typedef struct DrawItem {
    const GpuMesh *mesh;
    const M4      *bones;
    bool           no_shadow;  // не отбрасывает тень (рельеф, дальние мегаструктуры)
} DrawItem;

// Точечный свет без теней: факел, фонарь, прожектор (конусом)
typedef struct PointLight {
    V3  pos;
    f32 radius;     // дальше не светит, м
    V3  color;      // цвет с яркостью: на 1 м освещённость color / 2
    f32 cone;       // косинус половины угла конуса; -1 — во все стороны
    V3  dir;        // ось конуса
    f32 glow;       // свечение пыли вокруг источника, доля от color
} PointLight;

typedef struct FrameView {
    M4  view, proj;
    V3  cam_pos;
    V3  sun_dir;        // направление НА главный свет (солнце или луна)
    V3  sun_color;
    V3  sky_amb, ground_amb;  // рассеянный свет неба и отражённый от земли
    V3  haze, zenith;         // цвет дымки и неба в зените
    V3  shadow_center;  // центр области теней
    const PointLight *lights;  // рендер сам берёт видимые, не больше RENDER_MAX_LIGHTS
    u32 light_count;
    f32 time;
    u32 frame;
} FrameView;

typedef struct RenderSettings {
    i32  internal_h;    // высота внутреннего разрешения
    bool post;          // фильтр кадра вкл/выкл
    bool pixel;         // пиксельность (квадраты на поверхностях и блоки вблизи) вкл/выкл
    i32  pixel_div;     // 1 — размер пикселя по глубине; 2, 3 — весь кадр в 1/2, 1/3 окна (internal_h), ровные пиксели
    bool vrs;           // шейдинг блоками там, где фильтр сливает пиксели (если есть у видеокарты)
    bool ao_half;       // AO в половинном разрешении с апскейлом по глубине (сравнение)
    bool pixel_int;     // пиксели только целых размеров 1, 2, 4, 8 по вложенным сеткам (сравнение)
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
// Постоянный буфер экземпляров (заменяет прежний). Массив inst должен жить, пока он нужен рендеру:
// по нему отсекаются экземпляры
bool render_upload_instances(const RInst *inst, u32 count);
// Группы экземпляров по участкам для следующих кадров (NULL — нет); массивы должны жить, пока используются
void render_set_groups(const InstGroup *groups, const InstCluster *clusters, u32 cluster_count);
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
