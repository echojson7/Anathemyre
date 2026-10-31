// Клипы: ключи поз в файлах data/anims/*.anim (пишет tools/animbake). Процедурные слои anim.c
// (походка от скорости, IK рук и ног, наклоны, дыхание) остаются кодом и берут ключи отсюда.
// Формат и список клипов — docs/animations.md, раздел «Клипы в файлах»
#ifndef ANA_ANIM_KEYS_H
#define ANA_ANIM_KEYS_H

#include "../core/arena.h"
#include "../core/math3d.h"

// Рука в зеркальных углах: ux < 0 — вперёд и вверх, ac > 0 — к середине тела, out > 0 — в сторону,
// el < 0 — сгиб локтя, wx — сгиб кисти, wz — отведение кисти
typedef struct ArmK {
    f32 ux, ac, out, el, wx, wz;
} ArmK;

// Куда ставить кисть: опора (сустав модели), запястье от неё в осях модели, куда смотрит предплечье
enum { HA_ROOT, HA_KNEE_L, HA_KNEE_R, HA_PELVIS, HA_CHEST, HA_HEAD, HA_SHOULDER_L, HA_SHOULDER_R };
typedef struct HandK {
    f32 w;      // 0 — рука по углам (ArmK), 1 — запястье в точке
    f32 anchor; // опора HA_*: число, чтобы ключ в файле был ровным рядом f32
    V3  at;     // запястье от опоры, м
    V3  dir;    // куда смотрит предплечье
    f32 roll;   // поворот кисти вокруг предплечья, рад
    V3  bend;   // сгиб запястья после постановки, оси кисти (x, z)
} HandK;

// Ключ позы отдыха. Таз — от стойки (drop вниз, pz вперёд, px влево), стопы — лодыжки в осях модели,
// высота над своей лодыжкой стоя. live — поза стоя этого кадра: с неё вход начинается и ей выход кончается
typedef struct RestK {
    f32  t;              // доля трека
    f32  live;           // 1 — поза стоя этого кадра (ключ без чисел)
    f32  drop, pz, px;
    V3   pel;            // таз: наклон вперёд, разворот влево, крен влево
    f32  sp, ch, nk, hd; // наклон вперёд: поясница, грудь, шея, голова
    f32  yaw, roll;      // корпус: разворот и крен влево
    ArmK al, ar;
    V3   fl, fr;
    f32  tl, tr;         // наклон стоп, > 0 носок вниз
    f32  kout;           // колени врозь
    HandK hl, hr;
} RestK;

// Ключ боевой позы: руки, корпус, таз и стопы (левая впереди, правая сзади)
typedef struct FightK {
    ArmK r, l;
    f32 lean, twist, roll;   // корпус: наклон вперёд, скрутка груди влево, крен
    f32 pyaw, drop, pz;      // таз: разворот влево, присед, вынос вперёд
    f32 front, back;         // вынос передней и задней стопы вперёд, м
} FightK;

// Ключ позы всего тела: таз (высота над землёй, вперёд, вбок, повороты), корпус, руки, ноги.
// Ноги углами (ik = 0) или стопами на земле (ik = 1).
// Темп по тяжести: таз нигде не опускается быстрее свободного падения, обмякшее тело падает с ним
// почти вровень, живой притормаживает ногами и руками. Подъём через силу — вдвое дольше падения
typedef struct BodyK {
    f32 t;
    f32 py, pz, px, ptx, pty, ptz;
    f32 sp, ch, hd, tw;
    ArmK al, ar;
    f32 lt[4], rt[4];        // бедро вперёд(<0), бедро в сторону, колено, стопа
    V3 fl, fr;               // цели стоп при ik
    f32 ik;
} BodyK;

// Стойка и блок по типу оружия: рукоять и оси оружия относительно груди (поза привязки, от сустава груди)
typedef struct GuardK {
    V3 pos, tip, edge;     // стойка
    V3 bpos, btip, bedge;  // блок
} GuardK;

#define HK(w_, an, ax, ay, az, dx, dy, dz, rl, bx, bz) { w_, an, { ax, ay, az }, { dx, dy, dz }, rl, { bx, 0.0f, bz } }
// Та же рука зеркально для правой стороны (опору подставляет вызывающий)
#define HKM(w_, an, ax, ay, az, dx, dy, dz, rl, bx, bz) HK(w_, an, -(ax), ay, az, -(dx), dy, dz, -(rl), bx, -(bz))

// Вид ключа клипа. Ключ в файле — ровный ряд f32 той же раскладки, что структура
typedef enum AnimKeyKind { AK_REST, AK_FIGHT, AK_BODY, AK_GUARD, AK_COUNT } AnimKeyKind;
_Static_assert(sizeof(RestK) % 4 == 0 && sizeof(FightK) % 4 == 0 && sizeof(BodyK) % 4 == 0 && sizeof(GuardK) % 4 == 0,
               "ключи клипов — только f32");

// Клипы: имя в коде, имя файла, вид ключа, ключей (0 — трек по времени, от двух ключей; иначе ровно столько:
// замах и удар — по AttackKind, стойки оружия — одноручное, древковое, тяжёлое). Новый клип — строка здесь и ключи в tools/animbake
#define ANIM_CLIPS(X) \
    X(SQUAT_IN, squat_in, AK_REST, 0) \
    X(SQUAT_OUT, squat_out, AK_REST, 0) \
    X(SIT_IN, sit_in, AK_REST, 0) \
    X(SIT_OUT, sit_out, AK_REST, 0) \
    X(KNEEL_IN, kneel_in, AK_REST, 0) \
    X(KNEEL_OUT, kneel_out, AK_REST, 0) \
    X(ARMS_IN, arms_in, AK_REST, 0) \
    X(ARMS_OUT, arms_out, AK_REST, 0) \
    X(STARE_IN, stare_in, AK_REST, 0) \
    X(STARE_OUT, stare_out, AK_REST, 0) \
    X(GUARD, fight_guard, AK_FIGHT, 1) \
    X(BLOCK, fight_block, AK_FIGHT, 1) \
    X(WINDUP, fight_windup, AK_FIGHT, 3) \
    X(STRIKE, fight_strike, AK_FIGHT, 3) \
    X(LIE_UP, lie_up, AK_BODY, 1) \
    X(LIE_DOWN, lie_down, AK_BODY, 1) \
    X(LIE_SIDE, lie_side, AK_BODY, 1) \
    X(FALL, fall, AK_BODY, 0) \
    X(FALL_FRONT, fall_front, AK_BODY, 0) \
    X(FALL_SIDE, fall_side, AK_BODY, 0) \
    X(DIE_BACK, die_back, AK_BODY, 0) \
    X(DIE_FRONT, die_front, AK_BODY, 0) \
    X(DIE_SIDE, die_side, AK_BODY, 0) \
    X(BLEED, bleed, AK_BODY, 0) \
    X(COLLAPSE_DOWN, collapse_down, AK_BODY, 0) \
    X(COLLAPSE_UP, collapse_up, AK_BODY, 0) \
    X(SINK, sink, AK_BODY, 0) \
    X(GETUP_UP, getup_up, AK_BODY, 0) \
    X(GETUP_DOWN, getup_down, AK_BODY, 0) \
    X(TURN_OVER, turn_over, AK_BODY, 0) \
    X(PICKUP, pickup, AK_BODY, 0) \
    X(KNEEL, kneel, AK_BODY, 0) \
    X(WEAPON_GUARD, weapon_guard, AK_GUARD, 3)

#define ANIM_CLIP_ID(id, file, kind, n) AC_##id,
typedef enum AnimClipId { ANIM_CLIPS(ANIM_CLIP_ID) AC_COUNT } AnimClipId;
#undef ANIM_CLIP_ID

typedef struct AnimClip {
    const f32 *key;  // n ключей подряд
    u32 n;
} AnimClip;

extern AnimClip g_anim_clip[AC_COUNT];
extern const char *const g_anim_clip_file[AC_COUNT];
extern const u8 g_anim_clip_kind[AC_COUNT];
extern const u8 g_anim_clip_need[AC_COUNT];   // ключей: 0 — от двух
extern const u32 g_anim_key_floats[AK_COUNT];  // f32 в ключе каждого вида

// Файл .anim: «AANM», версия, вид ключа, f32 в ключе, число ключей (все u32), затем ключи. Little-endian
#define ANIM_FILE_VERSION 1u
#define ANIM_MEM KB(256)  // ключи всех клипов (~60 КБ)

// Прочитать все клипы из <data>/anims в mem. false — нет файла, не та версия или раскладка; ошибка в журнале
bool anim_clips_load(Arena *mem, const char *data_dir);

#define CLIP_KEYS(T, id) ((const T *)g_anim_clip[id].key)
#define CLIP_N(id) g_anim_clip[id].n

#endif
