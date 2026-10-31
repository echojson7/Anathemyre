// Физика вещей: твёрдые тела из шаров (капсулы и коробки раскладываются на шары), земля — плоскость под
// телом из высоты мира, решатель XPBD (позиции, потом скорости) с подшагами. Тело в одном из состояний:
// считается, спит (лежит, не стоит ничего), застыло (вне зоны физики: стоит как было, хоть в воздухе).
// Зона — круги вокруг точек (свои, точка взгляда камеры), а не то, что видно на экране: поворот камеры
// не должен менять мир. Шаг детерминирован: один поток, порядок по номеру тела, без libm кроме sqrtf
// (docs/physics.md)
#ifndef ANA_PHYS_H
#define ANA_PHYS_H

#include "../core/arena.h"
#include "../core/grid.h"
#include "../core/math3d.h"
#include "../core/ser.h"

#define PHYS_MAX     2048   // тел всего: и считающихся, и застывших
#define PHYS_SPHERES 8      // шаров на тело
#define PHYS_PARTS   3      // частей в описании формы
#define PHYS_ACTIVE_MAX 768 // считается одновременно; сверх — самое медленное засыпает раньше (ragdoll — 11 тел)
#define PHYS_JOINTS  1024   // суставов: ragdoll — 10 на тело
#define PHYS_GROUPS  1024   // группы тел (ragdoll): друг с другом не сталкиваются, спят и застывают вместе
#define PHYS_ZONE_R  150.0f // радиус зоны физики, м
#define PHYS_ZONE_OUT 20.0f // выходит из зоны на столько дальше, чем входит
#define PHYS_ZONE_PTS 4

typedef f32 (*PhysGround)(f32 x, f32 z);

typedef enum PhysPartKind { PP_CAPSULE, PP_BOX } PhysPartKind;

// Часть формы в осях вещи: капсула a..b радиуса r или коробка с центром a и полуразмерами b
typedef struct PhysPart {
    u8  kind;
    V3  a, b;
    f32 r;
} PhysPart;

typedef struct PhysDesc {
    PhysPart part[PHYS_PARTS];
    u8  parts;
    f32 mass;      // кг
    f32 friction;  // трение покоя о землю: 0.6 бетон, 0.9 песок
    f32 bounce;    // отскок 0..1
    f32 gap;       // шаг шаров капсулы в радиусах; 0 — 1.5 (вещь), у частей тела реже
} PhysDesc;

// Сустав: точка, общая для двух тел, и пределы поворота тела b относительно a в осях сустава.
// Шар (плечо, бедро, шея, поясница): ось X тела b отклоняется от оси X тела a не больше cone, кручение вокруг X — lo..hi.
// Петля (колено, локоть): только вокруг Z, угол lo..hi; 0 — как в позе привязки
typedef enum PhysJointKind { PJ_FREE, PJ_BALL, PJ_HINGE } PhysJointKind;

typedef struct PhysJointDesc {
    u32 a, b;
    V3  at;        // точка сустава в мире, тела уже стоят
    V4  fa, fb;    // оси сустава в осях вещей a и b (кватернионы xyzw)
    u8  kind;
    f32 lo, hi, cone;
} PhysJointDesc;

// Как стоит расставленная вещь: осесть при первом попадании в зону, висеть, пока не тронут, или намертво
typedef enum PhysMode { PHYS_SETTLE, PHYS_HANG, PHYS_FIXED } PhysMode;

typedef enum PhysState { PS_FREE, PS_ACTIVE, PS_ASLEEP, PS_FROZEN } PhysState;

typedef struct PhysStats {
    u32 active, asleep, frozen, pairs;
    f32 ms;  // последний шаг
} PhysStats;

typedef struct Phys {
    u32 count;  // верхняя граница занятых номеров
    // Состояние тела: центр масс и поворот, скорости, поза прошлого шага (кадр сглаживает между ними)
    V3  *x, *v, *w, *x_tick;
    V4  *q, *q_tick;   // кватернион xyzw
    V3  *com;          // центр масс в осях вещи: поза вещи = поворот, затем сдвиг x - R·com
    V3  *inv_i;        // обратный момент инерции по осям вещи
    f32 *inv_m, *bound, *sleep_t, *out_t, *friction, *bounce;
    V4  *sph;          // шары: центр от центра масс в осях вещи и радиус, PHYS_SPHERES на тело
    u8  *ns, *state, *mode, *settled, *wake, *touch;  // wake и touch — на один шаг
    u16 *user;         // кто это для вида: вид лома, номер вещи
    u16 *group;        // 0 — само по себе; иначе группа (ragdoll): внутри не сталкиваются, спят и застывают вместе
    // Суставы (SoA): тела, точка в осях вещи от центра масс, оси сустава в осях вещи, пределы
    u32  jcount;
    u16 *ja, *jb;
    u8  *jkind;
    V3  *jra, *jrb;
    V4  *jqa, *jqb;
    V4  *jlim;         // lo, hi, cone
    // Поиск соседей: тела в зоне, на каждый шаг
    Grid grid;
    u32 *zone_ix;
    f32 *zone_x, *zone_z;
    u32 *pair;         // пары (a << 16 | b), a < b
    u32  npair;
    V3   pts[PHYS_ZONE_PTS];
    u32  npts;
    PhysStats st;
    bool on;           // выключено — всё стоит как есть
} Phys;

void phys_init(Phys *p, Arena *perm);
// Новое тело: xf — поза вещи (начало осей вещи, например, кулак у оружия), скорости мира.
// Только из шага мира. -1 — мест нет
i32  phys_spawn(Phys *p, const PhysDesc *d, M4 xf, V3 vel, V3 angvel, u8 mode, u16 user);
void phys_free(Phys *p, u32 h);
// Сустав между двумя телами (оба уже стоят). -1 — мест нет
i32  phys_joint(Phys *p, const PhysJointDesc *d);
void phys_joint_free(Phys *p, u32 j);
// Поворот матрицы (её столбцы — оси) кватернионом; масштаб отбрасывается
V4   phys_quat(M4 m);
// Поза вещи: после шага (alpha 1) или между двумя шагами для кадра
M4   phys_pose(const Phys *p, u32 h, f32 alpha);
// Разбудить всё в круге (толчок, взрыв, кто-то прошёл)
void phys_wake(Phys *p, V3 c, f32 r);
// Точки, вокруг которых работает физика; до PHYS_ZONE_PTS
void phys_zone(Phys *p, const V3 *pts, u32 n);
// Шаг dt: зона, подшаги (суставы, касания), сон
void phys_tick(Phys *p, f32 dt, PhysGround ground);
void phys_ser(Ser *s, Phys *p);

#endif
