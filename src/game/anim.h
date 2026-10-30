// Процедурные анимации человека на стандартном скелете (sim/body.h): походка с IK ног,
// позы ожидания, хват и убирание оружия, бой, падение и подъём.
// Суставы берутся из g_body_bones, поэтому смена пропорций скелета анимаций не ломает.
// Без состояния: поза — функция параметров, считается в любом потоке.
#ifndef ANA_ANIM_H
#define ANA_ANIM_H

#include "../render/mesh.h"
#include "../sim/body.h"
#include "../sim/items.h"

// Слот скиннинга оружия: сетка оружия построена в своей системе (рукоять в начале координат),
// character_pose пишет сюда, где оно сейчас: в руке, на поясе, на спине
#define CHAR_BONE_WEAPON BONE_COUNT
// Слот факела: в левом кулаке, древко по +Y
#define CHAR_BONE_TORCH (BONE_COUNT + 1)
// Слот пламени: над ветошью факела, всегда стоит вертикально, куда бы ни клонилось древко
#define CHAR_BONE_FLAME (BONE_COUNT + 2)
#define CHAR_BONES (BONE_COUNT + 3)
#define TORCH_FLAME_BASE 0.47f  // низ пламени над кулаком по древку, м

_Static_assert(CHAR_BONES <= 24, "скелет с оружием не влезает в RENDER_MAX_BONES");

typedef struct CharPose {
    V3  pos;         // точка на земле
    f32 heading;     // поворот вокруг Y, 0 = смотрит в +Z
    f32 phase;       // фаза цикла шага, 0..1
    f32 walk;        // 0 стоит .. 1 идёт
    f32 run;         // 0 шаг .. 1 бег (0.5 — трусца)
    f32 speed;       // фактическая скорость, м/с: по ней длина шага
    f32 stride;      // множитель длины шага перед остановкой (player_stride), 0 = 1
    f32 time;
    f32 seed;
    f32 height;      // множитель роста, 0 = 1
    f32 width;       // множитель ширины, 0 = 1
    f32 move_dir;    // направление шага относительно взгляда, рад (0 — вперёд)
    f32 lean_fwd;    // наклон тела: > 0 вперёд, рад
    f32 lean_side;   // крен в поворот: > 0 влево, рад
    f32 look;        // поворот головы к цели относительно взгляда, рад
    f32 idle;        // сколько стоит без движения, с: позы покоя только у давно стоящих
    u8  weapon;      // W_* + 1; 0 — без оружия
    f32 torch;       // 0 рука опущена .. 1 факел поднят перед собой (левая рука)
    f32 slope_x, slope_z; // уклон земли в мире (dy/dx, dy/dz): лежащий и падающий ложатся по склону
    bool calm;       // без поз ожидания (портреты)
    const V3 *joints; // суставы этого тела в позе привязки, BONE_COUNT; NULL — g_body_bones
} CharPose;

// Действие всем телом поверх походки и боя
typedef enum CharClip {
    CLIP_NONE,
    CLIP_FALL,       // сбит с ног: валится туда, куда толкнул удар (fall_dir), ловит себя руками
    CLIP_COLLAPSE,   // теряет сознание стоя: колени подламываются, руками не ловит
    CLIP_LIE,        // лежит (side — на боку)
    CLIP_GETUP,      // встаёт: со спины через бок и колено, ничком — через руки
    CLIP_KNEEL,      // опускается на колени, руки поднимает (сдался)
    CLIP_CRAWL,      // ползёт ничком на руках
    CLIP_DIE,        // убит ударом стоя: обмякает и валится туда, куда толкнул удар
    CLIP_BLEED,      // истёк кровью стоя: шатается, на колено, на бок
    CLIP_SINK,       // ноги не держат: оседает в сознании, ловит себя руками, ложится ничком
    CLIP_TURN,       // переворачивается со спины на живот (дальше ползёт)
    CLIP_PICKUP,     // поднимает выроненное оружие
} CharClip;

// Куда валится тело, относительно взгляда (как FALL_* боя)
enum { CHAR_FALL_BACK, CHAR_FALL_FRONT, CHAR_FALL_LEFT, CHAR_FALL_RIGHT };

typedef enum DodgeKind { DODGE_BACK, DODGE_SIDE, DODGE_DUCK, DODGE_COUNT } DodgeKind;

// Что делает тело поверх походки. Доли 0..1
typedef struct CharAction {
    f32  guard;      // руки опущены .. боевая стойка
    u8   attack;     // AttackKind текущего удара
    f32  swing;      // 0 — нет удара; 0..1 замах, 1..2 удар, 2..3 возврат
    bool aim_on;     // удар идёт в точку aim: оружие ведётся через неё
    bool aim_stop;   // удар приняли на блок: клинок останавливается перед целью
    V3   aim;        // куда идёт удар, пространство модели (без роста и ширины)
    f32  aim_s;      // наклон следа: размах — вниз на метр влево, сверху — влево на метр вниз
    f32  block;
    f32  dodge;      // уворот 0..1..0
    u8   dodge_kind;
    f32  dodge_side; // ±1: куда уходит вбок
    f32  stagger;    // ошеломлён
    f32  flinch;     // отдача от попадания, гаснет к 0
    u8   flinch_part;
    f32  wield;      // 0 — оружие убрано .. 1 в руке; между — достаёт или убирает
    u8   clip;       // CharClip
    f32  clip_t;     // доля клипа 0..1 (LIE, CRAWL — время, с)
    bool prone;      // лежит ничком
    i8   side;       // лежит на боку: 1 — на левом, -1 — на правом
    u8   fall_dir;   // CHAR_FALL_*: куда валит удар
    f32  limp;       // руки безвольно (без сознания, мёртв)
    bool dead;       // мёртв: не дышит
    f32  still;      // сколько мёртв, с: первые секунды — последние движения
    u8   prev_clip;  // клип до смены: из него переход за fade
    f32  prev_t;
    bool prev_prone;
    i8   prev_side;
    f32  fade;       // 0 — ещё прежний клип .. 1 — уже новый
    u8   severed;    // маска BodyPart: отсечённые конечности
} CharAction;

// Циклов шага (по два шага) в секунду. Шагом темп растёт быстро (0.8 м/с — 92 шага в минуту,
// 1.6 — 118), бегом почти стоит, растёт длина шага (3.2 — 161, 4.8 — 172). Между 1.9 и 2.8 — переход
static inline f32 char_cadence(f32 v)
{
    f32 t = clampf((v - 1.9f) / 0.9f, 0.0f, 1.0f);
    t = t * t * (3.0f - 2.0f * t);
    return (0.533f + 0.283f * v) * (1.0f - t) + (1.167f + 0.056f * v) * t;
}
// Длина полного цикла шага (два шага), м
static inline f32 char_stride(f32 v) { return fmaxf(v, 0.5f) / char_cadence(fmaxf(v, 0.5f)); }
// То же с походкой run: бегом темп не ниже 150 шагов в минуту, даже на малой скорости (разгон с места и
// торможение — короткими частыми беговыми шагами). При походке по скорости совпадает с char_stride
static inline f32 char_stride_run(f32 v, f32 run)
{
    v = fmaxf(v, 0.5f);
    f32 c = char_cadence(v), w = clampf((run - 0.25f) / 0.25f, 0.0f, 1.0f);
    return v / (c + (fmaxf(c, 1.25f) - c) * w);
}
// Фаза шага, где левая стопа в середине опоры под телом (правая — +0.5): с неё трогаются и на ней встают
#define CHAR_REST_PHASE 0.22f
// Походка под скорость, м/с: до 1.6 шаг, к 4.8 полный бег
static inline f32 char_run_from_speed(f32 v) { return clampf((v - 1.6f) / 3.2f, 0.0f, 1.0f); }

// Пишет CHAR_BONES матриц скиннинга; act == NULL — только походка и позы ожидания
void character_pose(const CharPose *p, const CharAction *act, M4 *out_bones);
// Матрицы для оружия, брошенного на землю: рукоять в точке pos
void weapon_ground_pose(V3 pos, f32 yaw, M4 *out_bones);

// Оружие отдельной сеткой; кости — слот CHAR_BONE_WEAPON, рукоять в начале координат, острие по +Y
void weapon_build(MeshBuilder *mb, u8 weapon);
// Факел: древко с обмоткой (flame = false) или пламя отдельной сеткой; слот CHAR_BONE_TORCH
void torch_build(MeshBuilder *mb, bool flame);
// Где пламя факела в мире по костям позы
V3   torch_flame_pos(const M4 *bones);

#endif
