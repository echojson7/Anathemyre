// Люди в шаге мира (docs/people.md): таблица SoA на 1024 места, номер с поколением. Шаг ведёт корень тела —
// опору, место, курс, скорость, фазу шага и путь; позу костей считает вид. Сейчас (этап 9а) группы «место»,
// «движение», «путь» и зерно внешности; действие, тело и бой добавят этапы 10–12
#pragma once
#include "../core/mem.h"
#include "../core/wpos.h"

constexpr u32 PEOPLE_CAPACITY = 1024;
constexpr u32 PATH_MAX = 32;          // узлов пути на человека; длиннее — перепрокладка у последнего
constexpr u32 PATHS_PER_STEP = 4;     // поисков пути за шаг (остальные ждут в очереди)
constexpr u32 PATH_STEP_BUDGET = 12000;  // раскрытых узлов A* за шаг, дальше очередь ждёт: пик шага ≲ 4 мс

enum Gait : u8 { GAIT_WALK = 0, GAIT_JOG = 1, GAIT_RUN = 2, GAIT_COUNT = 3 };
enum MoveState : u8 { MOVE_STAND = 0, MOVE_WAIT_PATH = 1, MOVE_FOLLOW = 2 };
constexpr u8 PATH_DIRECT = 0xFF;      // path_area: вне сетки ходимости — прямо к цели

// Длина цикла шага (два шага) из дорожек корня клипов ходьбы и бега: фаза растёт на v / длина за секунду
struct GaitTable {
    f32 speed[GAIT_COUNT] = {1.6f, 3.2f, 4.8f};  // м/с
    f32 walk_v = 1.4f, walk_len = 1.4f;   // естественная скорость клипа walk и длина его цикла, м
    f32 run_v = 3.6f, run_len = 2.4f;
    u64 hash = 0;                          // хеш чисел — в хеше описаний
};

struct People {
    u16 *gen;
    u8 *alive;
    // Место: опора (0 — земля, k + 1 — коробка k), чанк + смещение, высота ступней, вертикальная скорость
    u32 *support;
    i32 *cx, *cz;
    f32 *x, *z, *y;
    f32 *vy;
    // Движение: курс тела, курс скорости, скорость, аллюр, фаза шага (u32 — доля цикла), состояние
    u32 *heading;
    u32 *move_yaw;
    f32 *speed;
    u8 *gait;
    u32 *phase;
    u8 *state;
    u16 *stuck;      // тиков почти без хода к цели
    // Путь: цель, узлы сетки ходимости (область path_area), следующий узел, обрезан ли путь
    i32 *gcx, *gcz;
    f32 *gx, *gz, *gy;
    u32 *path;       // PEOPLE_CAPACITY · PATH_MAX
    u8 *path_n, *path_i, *path_area, *path_more;
    u8 *queued;      // стоит в очереди поиска пути
    // Внешность
    u32 *seed;
    // Места и очередь поиска пути (кольцо, по порядку приказов)
    u16 *free;
    u32 free_count;
    u32 live;
    u16 *queue;
    u32 q_head, q_count;
};

struct World;
struct StepCtx;
void people_init(People &p, Arena &arena);
// Родить человека на ходимом месте у at (RNG мира); false — мест нет
bool people_spawn(World &w, const WPos &at, f32 radius);
void people_kill(World &w, u32 slot);
// Приказ идти: человек по номеру с поколением (чужое поколение — приказ пропадает)
void people_order_go(World &w, u32 id, const WPos &target, u32 gait);

// Системы шага (порядок — в sim_step): пути из очереди → руление и ход → расталкивание → опора и фаза
void people_paths(World &w, StepCtx &ctx);
void people_steer(World &w, StepCtx &ctx);
void people_separate(World &w, StepCtx &ctx);
void people_settle(World &w, StepCtx &ctx);

struct Column;
u32 people_columns(const People &p, Column *out);
