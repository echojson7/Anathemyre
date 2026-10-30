// Тело на земле: идёт в указанную точку (ПКМ по земле, как в Kenshi).
// Движение с инерцией: скорость и её направление меняются с ограниченным ускорением,
// поворот на ходу идёт дугой, разворот назад — со сбросом темпа, без остановки.
// Препятствия пока не обходит: поиск пути отдельным этапом.
#ifndef ANA_PLAYER_H
#define ANA_PLAYER_H

#include "../core/math3d.h"

typedef f32 (*GroundFn)(f32 x, f32 z);

// Режим передвижения, выбирается игроком для каждого
typedef enum Gait { GAIT_WALK, GAIT_JOG, GAIT_RUN, GAIT_COUNT } Gait;

// Кто правит телом: ходьба сама, поле боя гонит к цели, поле боя ведёт бой (ходьба стоит)
enum { CTRL_FREE, CTRL_CHASE, CTRL_FIGHT };

// Скорость режима для здорового среднего человека, м/с
extern const f32 g_gait_speed[GAIT_COUNT];

typedef struct Player {
    V3   pos;
    f32  heading;    // куда смотрит тело
    f32  move_yaw;   // куда направлена скорость
    f32  speed;      // м/с
    f32  turn_rate;  // поворот тела, рад/с (для переступания на месте)
    f32  lean_fwd;   // наклон тела вперёд (разгон) и назад (торможение), рад
    f32  lean_side;  // крен в поворот, рад; > 0 — влево
    f32  look;       // голова к цели раньше тела, рад относительно heading
    f32  idle;       // сколько стоит без движения, с
    f32  phase, walk;
    f32  run;        // походка: 0 шаг .. 1 бег, следует за фактической скоростью
    f32  vmax;       // предел скорости на этот кадр, ставит отряд
    u8   gait;
    u16  group;      // номер приказа: кто идёт вместе, держит общий темп
    u8   ctrl;       // CTRL_*
    i8   stride;     // длина шага перед остановкой: 1 + stride/400 (в байтах выравнивания, размер прежний)
    i32  slot;       // слот на поле боя, -1 — нет
    V3   target;
    bool moving;
    f32  marker;     // сколько ещё секунд видна метка цели
} Player;

void player_init(Player *p, V3 pos, f32 heading);
void player_order_move(Player *p, V3 target);
void player_update(Player *p, f32 dt, GroundFn ground);
// Множитель длины шага (1 — обычный): перед целью шаги подгоняются, чтобы встать на ней, когда опорная
// стопа под телом. Тот же множитель идёт в позу (CharPose.stride), иначе стопа поедет
f32  player_stride(const Player *p);
// Тело для кадра между двумя шагами мира: a — прошлый шаг, b — текущий, t — доля шага 0..1
void player_lerp(const Player *a, const Player *b, f32 t, Player *out);
// Проверка разворотов без окна: шаг, трусца, бег; 90°, 135°, 180°; trace_path — CSV траекторий или NULL
int  player_bench(const char *trace_path);

#endif
