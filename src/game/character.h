// Процедурный персонаж: модель из деталей на стандартном скелете тела (sim/body.h), походка с IK ног,
// процедурные позы боя поверх походки
#ifndef ANA_CHARACTER_H
#define ANA_CHARACTER_H

#include "../core/rng.h"
#include "../render/mesh.h"
#include "../sim/body.h"
#include "../sim/items.h"

// Длина полного цикла шага (два шага), м
#define CHAR_STRIDE 1.15f

_Static_assert(BONE_COUNT <= 24, "скелет не влезает в RENDER_MAX_BONES");

typedef struct CharPose {
    V3  pos;         // точка на земле
    f32 heading;     // поворот вокруг Y, 0 = смотрит в +Z
    f32 phase;       // фаза цикла шага, 0..1
    f32 walk;        // 0 стоит .. 1 идёт
    f32 time;
    f32 seed;
    f32 height;      // множитель роста, 0 = 1
    f32 width;       // множитель ширины, 0 = 1
    f32 move_dir;    // направление шага относительно взгляда, рад (0 — вперёд)
} CharPose;

// Что делает тело поверх походки. Все доли 0..1
typedef struct CharAction {
    f32  guard;      // руки опущены .. боевая стойка
    u8   attack;     // AttackKind текущего удара
    f32  swing;      // 0 — нет удара; 0..1 замах, 1..2 удар, 2..3 возврат
    f32  block;
    f32  dodge;      // отшатнуться назад
    f32  stagger;    // ошеломлён
    f32  down;       // стоит .. лежит
    f32  down_dir;   // +1 падает на спину, −1 ничком
    f32  kneel;      // на коленях (сдался)
    f32  crawl;      // ползёт (лёжа ничком)
    f32  limp;       // руки безвольно (без сознания, мёртв)
    u8   severed;    // маска BodyPart: отсечённые конечности
    bool two_hand;   // древковое и тяжёлое держат двумя руками
} CharAction;

// Вариант 0..N задаёт одежду; armed — встроенное оружие тестовой комнаты
void character_build(MeshBuilder *mb, u32 variant, bool armed);
// Оружие отдельной сеткой в правой руке: рисуется с матрицами того же персонажа
void weapon_build(MeshBuilder *mb, u8 weapon);
// Пишет BONE_COUNT матриц скиннинга; act == NULL — только походка
void character_pose(const CharPose *p, const CharAction *act, M4 *out_bones);
// Матрицы для оружия, брошенного на землю: рукоять в точке pos
void weapon_ground_pose(V3 pos, f32 yaw, M4 *out_bones);

#endif
