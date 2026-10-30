// Мир тестовой сцены: карта 28×28 км (map.h), в центре лагерь с залом, ареной и Скуделью, к востоку
// долина у реки, люди одной таблицей, свой отряд, поле боя, арена, время суток. Мир идёт ровными шагами TICK_HZ раз в секунду
// игрового времени, время — целым счётчиком шагов. Шаг мира не знает о кадре: кадр только читает
// мир и сглаживает движение между двумя последними шагами (alpha)
#ifndef ANA_WORLD_H
#define ANA_WORLD_H

#include "../core/arena.h"
#include "../core/rng.h"
#include "../render/render.h"
#include "../sim/bands.h"
#include "../world/places.h"
#include "arena.h"
#include "arena_view.h"
#include "bands_near.h"
#include "field.h"
#include "people.h"
#include "squad.h"
#include "town.h"
#include "valley_view.h"

#define DAY_LENGTH_S 240.0f  // полные сутки, с; пока быстро, чтобы смену было видно
// Шаг мира: все — люди, отряд, бой, арена, сутки — на одном тике с боем
#define TICK_HZ COMBAT_HZ
#define TICK_DT COMBAT_DT
#define TICKS_PER_FRAME_MAX 8  // больше за кадр не догоняет: игра замедляется, а не застывает
// Время анимаций и шейдеров идёт по кругу этого периода: f32 после часов игры теряет точность
#define TIME_WRAP_TICKS (TICK_HZ * 3600u)

typedef struct World {
    u64           seed;
    Rng           rng, town_rng;
    Town          town;
    People       *people;
    Squad         squad;
    Field        *field;
    ArenaSession *arena;
    ArenaView     aview;
    ValleyView   *valley;        // долина у реки на карте: вода, руины, растения
    Places        places;        // места и дороги: зал, Скудель, долина
    Bands        *bands;         // отряды вдали; в мире их нет, пробные — кнопкой в НАСТРОЙКАХ
    NearBands    *near;          // отряды вдали рядом: тела
    V3            focus;         // точка взгляда камеры: рядом с ней отряды становятся телами
    GpuMesh       hall, far, town_mesh;  // статика лагеря: зал и обломки, мегаструктуры, посёлок
    u64           tick;          // шагов мира с начала
    f32           acc;           // игровое время после последнего шага, с (< TICK_DT)
    f32           alpha;         // acc / TICK_DT: где кадр между прошлым и текущим шагом
    f32           tod;           // время суток после последнего шага: 0 полночь, 0.5 полдень
    u32           day;
    bool          day_run;
} World;

bool world_init(World *w, Arena *perm, MeshBuilder *mb, u64 seed, f32 tod, bool day_run);
// Прошло dt игрового времени (кадр с учётом скорости): сделать столько шагов мира, сколько набралось.
// Шаг: люди, отряд, арена, поле боя, отряды вдали, сутки
void world_update(World *w, f32 dt);
// Вид боя для кадра: сглаживание бойцов поля и арены между шагами; dt — игровое время кадра
void world_present(World *w, f32 dt);
// Время анимаций и шейдеров, с: непрерывно между шагами, по кругу TIME_WRAP_TICKS
f32  world_time(const World *w);
// Время суток для кадра: между шагами плавно
f32  world_tod(const World *w);
// Высота под ногами: земля карты (map.h), пол зала, ярусы посёлка, плита арены
f32  world_ground(f32 x, f32 z);
// Главный в отряде стоит в долине
bool world_squad_in_valley(const World *w);
// Перенести отряд из лагеря в долину или обратно (отладка: пешком ~1.6 км). false — отряд в бою
bool world_travel(World *w);
// Отряд дерётся
bool world_squad_fighting(const World *w);
// Пробные отряды вдали (отладка): по одному у Скудели и в долине, враждуют и идут навстречу по одной дороге
void world_bands_test(World *w);

#endif
