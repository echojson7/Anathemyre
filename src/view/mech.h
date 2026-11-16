// «Вежа» в кадре (docs/vezha.md): шагающая турель — машина из жёстких частей на костях. Вся её жизнь — у вида:
// где стоит, куда идёт, как ставит лапы, куда смотрит. Шаг мира о ней пока не знает (таблица машин, приказы и
// попадания — следующий заход); место даёт строка сцены `prop vezha …`, вид перехватывает её и ведёт машину сам.
// Движение — расчётом, а не захватом: ноги решаются по опоре (IK), тело — пружинами, шаги — по правилу «лапа
// отстала — переставь», поэтому машина ходит по любому рельефу и в любую сторону
#pragma once
#include "../render/render.h"
#include "../sim/extract.h"

struct ViewAssets;
struct ViewInput;

constexpr u32 MECH_MAX = 4;     // машин в кадре
constexpr u32 MECH_LEGS = 4;

enum MechState : u8 { MECH_PACKED = 0, MECH_UNFOLD, MECH_STAND, MECH_WALK, MECH_FOLD };
// Принудительный режим (--mech): сама, лежит турелью, стоит на месте, обходит без складывания, показ — по порядку
// всё, что умеет (встала, привстала, прошла, присела, рывок с прыжком, домой, сложилась), по кругу
enum MechDemo : u8 { MECH_DEMO_AUTO = 0, MECH_DEMO_PACK, MECH_DEMO_STAND, MECH_DEMO_WALK, MECH_DEMO_SHOW };

struct MechLeg {
    WPos at;          // точка опоры лапы на земле
    WPos from, to;    // шаг: откуда и куда
    Vec3 n;           // нормаль опоры
    f32 t;            // < 0 — стоит; 0..1 — в шаге
    f32 dur, lift;    // длительность шага, с; высота дуги, м
    f32 fold;         // 1 — сложена пачкой у рамы, 0 — работает
    Vec3 pol;         // конец пути складывания у земли в осях рамы: поворот бедра, вынос от оси поворота, высота
};

struct Mech {
    bool live;
    u32 prop;         // место объекта сцены + 1
    u32 copy;         // копия сетки (render_skinned_create)
    u32 rng;
    WPos home;        // место турели
    f32 home_yaw;
    // Тело
    WPos at;          // середина; y — сглаженная опора под лапами
    f32 yaw, yaw_v;   // курс рамы, рад
    Vec3 vel, acc;    // скорость и сглаженное ускорение в мире (y = 0)
    f32 h, h_v, h_to; // оси бёдер над опорой, м
    f32 pitch, pitch_v, roll, roll_v;   // наклон рамы
    f32 hull, hull_v; // корпус относительно рамы
    f32 gun, gun_v;   // возвышение ствола
    f32 eye, eye_to, eye_wait;          // голова-прибор относительно корпуса
    f32 ant[2][2], ant_v[2][2];         // антенны: качание вокруг x и z
    MechLeg leg[MECH_LEGS];
    f32 lag;          // насколько самая отставшая лапа перебрала свой порог шага (1 — пора шагать)
    // Поведение
    u8 state;
    f32 state_t;      // секунд в состоянии
    f32 wait;         // сколько ещё стоять (лежать)
    u32 points_left;  // точек обхода до возвращения домой
    WPos goal;
    bool goal_home;
    f32 speed_max;
    f32 look, look_wait;   // куда смотрит без цели (курс в мире) и когда выберет заново
    f32 mood_t;       // сколько держать необычную высоту (привстала, присела)
    u8 jump;          // прыжок на рывке: 0 — нет, 1 — приседает, 2 — толчок (лапы ещё на земле), 3 — летит
    bool leap;        // на этом отрезке пути прыгнет
    f32 jump_t;       // сколько ещё приседать
    u8 show;          // шаг показа (--mech show)
    f32 clock;
};

struct MechView {
    bool bound, failed;
    u32 body;         // тело в ViewAssets::bodies
    // Геометрия ноги — из скелета (бинд-поза), см. tools/parts/vezha_leg.py
    f32 u, l1, l2, l3, a1, a2;   // вынос оси качания, длины звеньев, углы бедра и голени в бинде
    Vec3 corner[MECH_LEGS];      // оси поворота бёдер от оси машины
    Vec3 dir[MECH_LEGS];         // куда нога смотрит в бинде
    i32 chassis, hull, gun, eye, ant[2];
    i32 leg[MECH_LEGS][7];       // бедренный узел, бедро, голень, плюсна, лапа, цилиндр, шток
    Mech mech[MECH_MAX];
    u8 demo;
};

void mech_view_init(MechView &mv);
// Режим по слову ключа --mech (pack, stand, walk, show); false — слово не то
bool mech_view_demo(MechView &mv, const char *name);
// Объект сцены с этой сеткой — место машины, а не статичная деталь
bool mech_view_is(u64 mesh_name);
// Сообщить место (каждый кадр, из выписки объектов): prop — номер места объекта
void mech_view_place(MechView &mv, u32 prop, const WPos &at, Turn yaw);
// Машины кадра: экземпляры (inst, ni — дописывает, места — MECH_MAX), палитры и скиннинг — в out
void mech_view_build(MechView &mv, const ViewAssets &assets, const ViewInput &in, Arena &frame, RenderInstance *inst,
                     u32 &ni, RenderFrame &out);
// След движения без окна (--mech-trace): кадр жизни первой машины на dt секунд и строка чисел — состояние, место,
// углы и точки костей в метрах от дома. Скелет даёт вызывающий (тела и GPU не нужны)
struct Skel;
bool mech_view_trace(MechView &mv, const Skel &s, const Extract &c, f32 dt, char *line, usize cap);
