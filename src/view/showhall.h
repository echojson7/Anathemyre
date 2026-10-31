// Мерильня — глухой бетонный цех к югу от зала, где движения людей идут по кругу в живом темпе: позы
// отдыха (вход, держит, выход), движения стоя, шаг, трусца и бег по галерее второго этажа, падение и
// подъём. Вход один, через тамбур: солнце внутрь не попадает, свет — от ламп. Над каждым подпись: что
// показано, какая фаза и сколько она длится в игре. Подробно — docs/showhall.md
#ifndef ANA_SHOWHALL_H
#define ANA_SHOWHALL_H

#include "figures.h"
#include "../sim/player.h"
#include "../render/render.h"

#define SHOWHALL_MAX 28

// Камера на первом этаже у тамбура, лицом к рядам и галерее (кнопка КАМЕРА: В МЕРИЛЬНЮ)
#define SHOWHALL_CAM ((RtsCamera){ .target = { 0.0f, 0.0f, -79.0f }, .yaw = 0.0f, .pitch = 0.45f, .dist = 9.0f })

typedef struct Exhibit {
    u8     kind;      // EX_*
    u8     what;      // RestKind, IdleMove, Gait или номер круга клипов
    u8     body, hair, weapon;
    V3     pos;
    f32    heading;
    f32    shift;     // сдвиг круга, с: соседи не в такт
    f32    wait;      // ходок: сколько ещё стоит у края
    Player walker;    // ходок идёт настоящим телом (player.c)
    char   line[2][64];
} Exhibit;

typedef struct ShowHall {
    GpuMesh mesh;
    Exhibit ex[SHOWHALL_MAX];
    u32     count;
    M4     *bones;
    bool    posed;    // камера рядом: фигуры этого кадра посчитаны
    f32     t_off;    // сдвиг часов витрины (--hall-t): кадр нужной фазы без долгого прогона
} ShowHall;

void showhall_init(ShowHall *s, MeshBuilder *mb, Arena *perm);
// Пол цеха: первый этаж, лестница, галерея; вне цеха — base
f32  showhall_ground(f32 x, f32 z, f32 base);
// Сетка цеха всегда, фигуры — когда глаз ближе ~60 м. dt — шаг мира (пауза останавливает витрину)
u32  showhall_draw(ShowHall *s, const CharMeshes *m, V3 eye, f32 time, f32 dt, DrawItem *items);
// Лампы цеха в out (не больше max); свет внутри — только от них
u32  showhall_lights(f32 time, PointLight *out, u32 max);
// Коробка помещения для рендера: неба нет нигде, кроме тамбура
void showhall_room(RoomDark *r);
// Глаз камеры не выходит сквозь стены и крышу, пока точка взгляда внутри
V3   showhall_clamp_eye(V3 target, V3 eye);
// Подписи над фигурами (текст интерфейса), если камера внутри
void showhall_labels(const ShowHall *s, M4 view_proj, V3 eye);

#endif
