// Мир тестовой сцены: карта 28×28 км (map.h), в центре лагерь с залом, ареной и Скуделью, к востоку
// долина у реки, люди одной таблицей, свой отряд, поле боя, время суток. Мир идёт ровными шагами TICK_HZ раз в секунду
// игрового времени, время — целым счётчиком шагов. Шаг мира не знает о кадре: кадр только читает
// мир и сглаживает движение между двумя последними шагами (alpha)
#ifndef ANA_WORLD_H
#define ANA_WORLD_H

#include "../core/arena.h"
#include "../core/rng.h"
#include "../io/chronicle.h"
#include "../sim/bands.h"
#include "places.h"
#include "../sim/bands_near.h"
#include "../sim/field.h"
#include "../sim/loot.h"
#include "../sim/people.h"
#include "../sim/scrap.h"
#include "../sim/squad.h"
#include "town.h"

#define DAY_LENGTH_S 240.0f  // полные сутки, с; пока быстро, чтобы смену было видно
// Шаг мира: все — люди, отряд, бой, арена, сутки — на одном тике с боем
#define TICK_HZ COMBAT_HZ
#define TICK_DT COMBAT_DT
#define DAY_TICKS ((u32)DAY_LENGTH_S * TICK_HZ)  // шагов в сутках: время суток идёт по ним
#define TICKS_PER_FRAME_MAX 8  // больше за кадр не догоняет: игра замедляется, а не застывает
// Время анимаций и шейдеров идёт по кругу этого периода: f32 после часов игры теряет точность
#define TIME_WRAP_TICKS (TICK_HZ * 3600u)
#define WORLD_STRINGS KB(32)  // имена и роли людей, прочитанные из сохранения

// Крючок шага (World.hook): мир зовёт его раз за шаг, после боя поля и отрядов вдали, до вещей и физики, и
// ничего о нём не знает. Так отладка (debug/) меняет мир только внутри шага: пробные отряды, лом и вещи по кнопке
typedef struct World World;
typedef void (*WorldHook)(World *w, void *user);

struct World {
    u64           seed;
    Rng           rng, town_rng;
    GroundFn      floor;         // высота под ногами (app/ground.c): земля карты, полы построек
    People       *people;
    Squad         squad;
    Field        *field;
    V3            valley_spawn;  // где отряд появляется в долине у реки, и куда смотрит
    f32           valley_heading;
    Places        places;        // места и дороги: зал, Скудель, долина
    Bands        *bands;         // отряды вдали; в мире их нет, пробные — кнопкой в НАСТРОЙКАХ
    NearBands    *near;          // отряды вдали рядом: тела
    Chronicle    *chron;         // журнал событий мира
    Phys         *phys;          // вещи с физикой: лом, потом оружие на земле и тела (docs/physics.md)
    Ground       *ground;        // вещи на земле: брошенное, выроненное в бою (loot.c)
    GroundAsks    ground_ask;    // просьбы интерфейса бросить вещь из мешка: исполняет шаг мира
    char         *strings;       // пул строк сохранения: имена и роли после загрузки
    V3            focus;         // точка взгляда камеры: рядом с ней отряды становятся телами
    u64           tick;          // шагов мира с начала
    f32           acc;           // игровое время после последнего шага, с (< TICK_DT)
    f32           alpha;         // acc / TICK_DT: где кадр между прошлым и текущим шагом
    f32           tod;           // время суток после последнего шага: 0 полночь, 0.5 полдень
    u32           day;
    bool          day_run;
    WorldHook     hook;          // NULL — нет
    void         *hook_user;
};

// Карта (map_init) уже есть; town — места жителей Скудели из её постройки (view/scene.c)
bool world_init(World *w, Arena *perm, u64 seed, f32 tod, bool day_run, const Town *town, GroundFn ground);
// Прошло dt игрового времени (кадр с учётом скорости): сделать столько шагов мира, сколько набралось.
// Шаг: люди, отряд, поле боя, отряды вдали, вещи, физика, сутки
void world_update(World *w, f32 dt);
// Вид боя для кадра: сглаживание бойцов поля между шагами; dt — игровое время кадра
void world_present(World *w, f32 dt);
// Время анимаций и шейдеров, с: непрерывно между шагами, по кругу TIME_WRAP_TICKS
f32  world_time(const World *w);
// Время суток для кадра: между шагами плавно
f32  world_tod(const World *w);
// Главный в отряде стоит в долине
bool world_squad_in_valley(const World *w);
// Перенести отряд из лагеря в долину или обратно (отладка: пешком ~1.6 км). false — отряд в бою
bool world_travel(World *w);
// Отряд дерётся
bool world_squad_fighting(const World *w);
// Сохранение мира между шагами: люди, отряд, бой, отряды вдали, вещи, журнал событий, время. Карта, места,
// посёлок и вид строятся из зерна и в файл не идут, отладка (арена) — тоже (docs/save.md)
bool world_save(World *w, const char *path);
// Прочитать мир того же зерна и той же сборки. false — мир не тронут (или испорчен, если файл оборвался)
bool world_load(World *w, const char *path);
// Сумма всего, что идёт в сохранение: одинаковый мир — одинаковая сумма
u64  world_checksum(World *w);

#endif
