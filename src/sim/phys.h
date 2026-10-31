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
#define PHYS_ACTIVE_MAX 256 // считается одновременно; сверх — самое медленное засыпает раньше
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
} PhysDesc;

// Трение и отскок одни на весь мир: у материала физики нет (решение пользователя, docs/physics.md)
#define PHYS_FRICTION 0.6f
#define PHYS_BOUNCE   0.15f

// Удар: тело коснулось земли или другого тела со скоростью сближения не меньше PHYS_HIT_V.
// По одному самому сильному на тело за шаг. Кольцо для вида (звук): мир его не читает, в сохранение не идёт
#define PHYS_HIT_V   0.4f   // м/с
#define PHYS_HITS    64     // кольцо, степень двойки
#define PHYS_GROUND  0xFFFFu
typedef struct PhysHit {
    V3  pos;
    f32 speed;       // м/с по нормали
    u16 user;        // кто ударился (Phys.user)
    u16 other;       // обо что: user другого тела или PHYS_GROUND
} PhysHit;

// Неподвижные коробки построек (стены, плиты, ящики): тела упираются в них, как в землю. Добавляются при
// старте из описаний объектов (world/objects.c), в сохранение не идут — строятся из зерна заново
#define PHYS_STATICS     16384
#define PHYS_STATIC_CELL 8.0f    // ячейка поиска, м
#define PHYS_STATIC_NEAR 16      // коробок у одного тела за шаг

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
    f32 *inv_m, *bound, *sleep_t, *out_t;
    V4  *sph;          // шары: центр от центра масс в осях вещи и радиус, PHYS_SPHERES на тело
    u8  *ns, *state, *mode, *settled, *wake, *touch;  // wake и touch — на один шаг
    u16 *user;         // кто это для вида: вид лома, номер вещи
    // Поиск соседей: тела в зоне, на каждый шаг
    Grid grid;
    u32 *zone_ix;
    f32 *zone_x, *zone_z;
    u32 *pair;         // пары (a << 16 | b), a < b
    u32  npair;
    V3   pts[PHYS_ZONE_PTS];
    u32  npts;
    PhysStats st;
    // Удары: hit[seq % PHYS_HITS], hit_seq — всего записано. Сильнейший удар тела за шаг копится в hit_v
    PhysHit hit[PHYS_HITS];
    u32  hit_seq;
    f32 *hit_v;
    V3  *hit_p;
    u16 *hit_o;
    bool on;           // выключено — всё стоит как есть
    // Коробки построек: середина, половины размеров, поворот вокруг вертикали. Поиск — ячейки по хешу
    u32  nstatic;
    V3  *st_c, *st_half;
    f32 *st_cos, *st_sin;
    u32 *st_start, *st_ref, nref;
    bool st_dirty;
    u16 *st_near;      // PHYS_STATIC_NEAR на тело: коробки рядом на этот шаг
    u8  *st_nnear;
} Phys;

void phys_init(Phys *p, Arena *perm);
// Новое тело: xf — поза вещи (начало осей вещи, например, кулак у оружия), скорости мира.
// Только из шага мира. -1 — мест нет
i32  phys_spawn(Phys *p, const PhysDesc *d, M4 xf, V3 vel, V3 angvel, u8 mode, u16 user);
void phys_free(Phys *p, u32 h);
// Неподвижная коробка: середина c, половины размеров half, поворот yaw вокруг вертикали. Только при старте.
// false — мест нет
bool phys_static_add(Phys *p, V3 c, V3 half, f32 yaw);
// Поза вещи: после шага (alpha 1) или между двумя шагами для кадра
M4   phys_pose(const Phys *p, u32 h, f32 alpha);
// Разбудить всё в круге (толчок, взрыв, кто-то прошёл)
void phys_wake(Phys *p, V3 c, f32 r);
// Точки, вокруг которых работает физика; до PHYS_ZONE_PTS
void phys_zone(Phys *p, const V3 *pts, u32 n);
// Шаг dt: зона, подшаги, сон
void phys_tick(Phys *p, f32 dt, PhysGround ground);
void phys_ser(Ser *s, Phys *p);

#endif
