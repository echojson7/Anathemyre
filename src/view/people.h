// Тела людей в кадре (docs/people.md, «Ходьба»): из выписки шага мира (корень, курс, скорость, фаза шага) вид
// считает позу — клипы покоя, шага и бега по скорости синхронно по фазе, подгонка длины шага, IK стоп по
// support_at(), опускание таза, наклон от ускорения и поворота, перестановка стоп на месте — и отдаёт её
// скиннингу. Выбор человека, метка приказа и опора камеры «за человеком» — тоже здесь. Шаг мира и повтор
// этого не видят: состояние только у вида
#pragma once
#include "../anim/gait.h"
#include "../render/render.h"
#include "../sim/extract.h"

struct ViewAssets;
struct ViewInput;
struct Camera;

constexpr u32 VIEW_BODIES_MAX = 48;      // людей с телом в кадре (ближние к глазу); копий сетки столько же
constexpr f32 VIEW_BODY_FAR_M = 400.0f;  // дальше тела нет
constexpr f32 VIEW_IK_FAR_M = 60.0f;     // дальше — без IK и перестановки стоп
constexpr u32 VIEW_TRAIL = 48;           // след касаний стоп на человека (отладка)

struct PersonLook {
    u16 gen;            // 0 — вид этого человека ещё не видел
    bool drawn;         // тело было в прошлом кадре
    f32 y;              // сглаженная высота корня (ступени марша — скачки по 18 см)
    f32 drop;           // опускание таза, м (≤ 0)
    f32 acc;            // сглаженное ускорение вперёд, м/с²
    f32 side;           // сглаженное боковое ускорение, м/с²
    f32 foot_h[2];      // сглаженная опора под стопой, м
    // Стопы на месте: стоит — стопы держатся, где встали, и переставляются шагом, если тело ушло или повернулось
    bool lock;
    WPos lock_at[2];
    Turn lock_yaw[2];
    f32 step_t[2];      // < 0 — стопа стоит; 0..1 — переставляется
    WPos step_from[2];
    Turn step_yaw0[2];
    WPos root;          // корень в кадре (для выбора и камеры)
    Turn yaw;
    // След касаний стоп (отладка): точки стоп на опоре
    WPos trail[VIEW_TRAIL];
    u32 trail_n, trail_at;
};

struct PeopleView {
    PersonLook look[PEOPLE_CAPACITY];
    u32 copy[VIEW_BODIES_MAX];        // копии сетки (render_skinned_create)
    u16 copy_owner[VIEW_BODIES_MAX];  // место + 1, 0 — свободна
    u32 copy_count;
    bool bound, failed;
    u32 body, skin;                   // тело (ViewAssets::bodies) и его скелет game_engine
    GaitSet gait;
    i32 bones[10];                    // таз, позвонок 1, бёдра, голени, стопы, носки л/п
    Vec3 rest_foot[2];                // стопы в покое (оси тела, вдоль хода — от таза)
    // Выбор и приказ
    i32 sel;                          // место, −1 — никого
    u16 sel_gen;
    bool mark_on;
    WPos mark;
    f32 mark_fade;                    // 1 — видна; гаснет, когда человек встал
    u64 mark_step;                    // шаг мира приказа: метка гаснет только после него
};

void people_view_init(PeopleView &pv);
// Тела людей кадра: экземпляры (inst, ni — дописывает, места — VIEW_BODIES_MAX), палитры и скиннинг в out;
// линии метки и следа — в lines (ln — дописывает, места — VIEW_PEOPLE_LINES вершин)
constexpr u32 VIEW_PEOPLE_LINES = 4096;
void people_view_build(PeopleView &pv, const ViewAssets &assets, const ViewInput &in, const WPos &eye, Arena &frame,
                       RenderInstance *inst, u32 &ni, RenderFrame &out, RenderLineVertex *lines, u32 &ln, bool debug);
// Человек под пикселем окна (по телу из прошлого кадра): место, −1 — мимо
i32 people_view_pick(const PeopleView &pv, const Extract &cur, const Camera &cam, i32 width, i32 height, f32 px, f32 py);
// Голова выбранного человека на этот кадр (опора камеры, без покачивания шага). false — никого не выбрано
bool people_view_head(const PeopleView &pv, const Extract &prev, const Extract &cur, f32 alpha, WPos &out);
