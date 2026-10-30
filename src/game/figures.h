// Фигуры кадра: люди из таблицы (People) и бойцы арены. Кто нужен кадру (сам или его тень в пирамиде
// видимости), позы — пачкой в пуле потоков, элементы отрисовки, факелы и их свет, портреты, метки приказов.
// Только читает мир: ни люди, ни поле боя отсюда не меняются
#ifndef ANA_FIGURES_H
#define ANA_FIGURES_H

#include "../core/arena.h"
#include "../render/render.h"
#include "anim.h"
#include "arena_view.h"
#include "field.h"
#include "look.h"
#include "people.h"
#include "squad.h"

#define TORCH_LIGHTS_MAX 128

// Сетки людей: тела с грубыми уровнями, одежда и причёски по GEAR_ROW на тело, оружие, факел;
// метки под ногами: крест цели приказа, кольца (свой выбранный, осматриваемый чужой, цель атаки)
#define CHAR_GEAR_MESHES (BODY_KIND_COUNT * GEAR_ROW)

typedef struct CharMeshes {
    GpuMesh  body[BODY_KIND_COUNT];
    GpuMesh  body_lod[BODY_KIND_COUNT][CHAR_LODS - 1];
    GpuMesh *gear;       // CHAR_GEAR_MESHES; выделяет вызывающий: место в памяти задаёт порядок отрисовки
    GpuMesh *gear_lod;   // по CHAR_LODS - 1 на вещь
    GpuMesh  weapon[WEAPON_CAP * WEAPON_LOOKS];  // по weapon_mesh: вещь × вид качества  // внутри: рендер раскладывает по адресам сеток
    GpuMesh  torch[2];   // древко, пламя
    GpuMesh  marker;
    GpuMesh  ring[3];
} CharMeshes;

// Поза к расчёту: вход, действие в бою (если fight) и куда писать кости; look — в слот RENDER_LOOK_SLOT
typedef struct PoseJob {
    CharPose   p;
    CharAction act;
    bool       fight;
    M4        *bones;
    M4         look;
} PoseJob;

#define FIGURES_MAX_JOBS (PEOPLE_MAX + ARENA_MAX_BOUT)
#define MARK_SETS (SQUAD_MAX * 2 + 2)

typedef struct Figures {
    // Люди для кадра (figures_view): между двумя шагами мира, бойцы — как их показывает вид боя.
    // По номеру в таблице, он же слот поля. Читают позы, метки, звук шагов, выбор мышью, камера
    Player  view[PEOPLE_MAX];
    f32     slope[PEOPLE_MAX][2]; // уклон земли под бойцом (dy/dx, dy/dz): лежащие ложатся по склону
    f32     age[PEOPLE_MAX];  // сколько секунд поза не пересчитывалась; < 0 — не было в кадре, считать сразу
    M4     *bones;            // PEOPLE_MAX наборов костей
    M4     *arena;            // AV_BONE_SETS наборов: бойцы арены
    M4     *marks;            // MARK_SETS наборов: метки под ногами (одна матрица)
    M4     *portrait;         // 2 набора: миниатюры отряда и крупный портрет
    PoseJob job[FIGURES_MAX_JOBS];
    u32     jobs, posed;      // поз в последнем кадре: всего и людей
} Figures;

// Что видит кадр: пирамида видимости, глаз, свет (для тени), время
typedef struct FigureView {
    M4  view_proj;
    V3  eye, light_dir;
    f32 dt;         // реальное время кадра: по нему реже позы вдали
    f32 time;       // время симуляции: по нему анимации
    f32 night;      // Sky.night: когда зажигают факелы
} FigureView;

// Сетки людей в m (m->gear и m->gear_lod уже выделены), кости фигур
bool figures_init(Figures *f, CharMeshes *m, MeshBuilder *mb, Arena *perm, const char *data_dir);
// Люди для кадра: alpha — доля шага мира после последнего (World.alpha), после field_present
void figures_view(Figures *f, const People *p, const Field *fd, f32 alpha, GroundFn ground);
// Люди и бойцы арены (s == NULL — арены нет): элементы в items, свет факелов в lights после *nl.
// Позы считаются здесь же, в пуле потоков. Возвращает число элементов
u32  figures_draw(Figures *f, const CharMeshes *m, const People *p, const Field *fd, const ArenaView *av,
                  const ArenaSession *s, const FigureView *v, DrawItem *items, PointLight *lights, u32 *nl);
// Метки под ногами: кольца выбранных и их целей, крест приказа, кольцо осматриваемого (look_ring)
u32  figures_marks(Figures *f, const CharMeshes *m, const Squad *sq, const Field *fd, const V3 *look_ring, DrawItem *items);
// Портрет человека из таблицы в ячейку атласа slot (size пикселей); перерисовка только при смене
void figures_portrait(Figures *f, const FrameView *base, const CharMeshes *m, const People *p, u32 i, u32 slot, i32 size,
                      bool focus);
// Портрет бойца арены по номеру в ростере
void figures_portrait_arena(Figures *f, const FrameView *base, const CharMeshes *m, const ArenaSession *s, u32 ri, u32 slot,
                            i32 size);
// Забыть нарисованные портреты (перезагрузка шейдеров)
void figures_portraits_reset(void);

#endif
