// Земля мира (docs/terrain.md): сетка высот 8 м на всю карту, маски и уровень воды 32 м из зерна (шумы целые,
// эрозия — float по правилам шага мира), над ними слой деталей 1 м — одна целая функция terrain_node() у шага
// мира и шейдера
// (data/shaders/terrain.glsl). После создания земля не меняется (штампы локаций — этап 4, тоже при создании),
// поэтому кадр читает её напрямую, а в хеш шага она не входит: её хеш — в хеше описаний записи повтора
#pragma once
#include "../core/mem.h"

constexpr i32 TERRAIN_BASE_SHIFT = 3;   // сетка высот 8 м
constexpr i32 TERRAIN_MASK_SHIFT = 5;   // маски 32 м
// Единица высоты слоя деталей: 1/1280 м. Шаг сетки высот 5 см = 64 единицы
constexpr i32 TERRAIN_U_PER_M = 1280;
constexpr f32 TERRAIN_M_PER_U = 1.0f / 1280.0f;
constexpr u32 TERRAIN_DETAIL_OCTAVES = 5;

// Вид локации: долина (холмы, трава, протоки) или пустыня (дюны)
enum TerrainBiome : u32 { BIOME_VALLEY = 0, BIOME_DESERT = 1 };
constexpr u32 TERRAIN_MAX_LOCATIONS = 4;

// Описание земли (data/terrain.txt)
struct TerrainDesc {
    u32 seed = 1;
    i32 chunks = 40;           // сторона локации в чанках 256 м, чётная
    f32 ring = 3600.0f;        // от центра локации до подножия гор по её краю, м
    u32 locations = 1;         // локации с запада на восток, между соседними — хребет с перевалом
    u32 biome[TERRAIN_MAX_LOCATIONS] = {};
    // Карта дюн пустыни (файл .dun, src/sim/dune_map.h): путь из описания, данные кладёт app до генерации
    char dune_path[TERRAIN_MAX_LOCATIONS][64] = {};
    const u8 *dune_data[TERRAIN_MAX_LOCATIONS] = {};
    usize dune_size[TERRAIN_MAX_LOCATIONS] = {};
};
// Карта дюн: узлы 2 м на всю локацию (сторона loc_m / 2 + 1), единицы 1/640 м
constexpr i32 TERRAIN_FINE_SHIFT = 1;

struct Terrain {
    i32 chunks_x = 0, chunks_z = 0;  // карта в чанках: локации с запада на восток
    i32 chunk0 = 0;    // номер чанка у западного и южного края: карта — чанки [chunk0, chunk0 + chunks_x/z)
    i32 loc_m = 0;     // сторона локации, м
    i32 size_x = 0, size_z = 0;  // стороны карты, м
    u32 nx = 0, nz = 0;      // узлов сетки высот: size / 8 + 1
    u32 nmx = 0, nmz = 0;    // узлов масок: size / 32 + 1
    u16 *height = nullptr;  // nx·nz, строками с юга на север, единица 5 см (0 — 3276.75 м); дюны уже внутри
    u32 *mask = nullptr;    // nmx·nmz, байты: шероховатость (сила слоя деталей), песок, трава, сырость
    u32 *mask2 = nullptr;   // nmx·nmz, байты: сила дюн, пустыня (тёплый песок и камень), —, —
    u16 *water = nullptr;   // nmx·nmz: уровень воды, единица 5 см; 0 — воды нет (протоки в низинах)
    u16 *dune = nullptr;    // nx·nz: высота дюн в узле сетки 8 м, единицы 1/640 м (она же — внутри height)
    u32 detail_seed = 0;    // зерно слоя деталей
    u32 oct_seed[TERRAIN_DETAIL_OCTAVES] = {};  // зёрна октав деталей (их же получает шейдер)
    u32 dune_seed = 0;      // зерно дюн (его же получает шейдер)
    // Дюны из карты (лидар или симуляция ветра) вместо формулы: сетки 2 м локаций подряд, единицы 1/640 м,
    // уже умножены на силу дюн. fine_off[loc] — начало сетки локации, −1 — у локации карты нет
    u16 *fine = nullptr;
    u32 fine_n = 0;         // узлов на сторону сетки локации
    usize fine_count = 0;   // всего узлов
    i32 fine_off[TERRAIN_MAX_LOCATIONS] = {-1, -1, -1, -1};
    u64 hash = 0;           // хеш сетки, масок, воды и дюн
};

// Разобрать data/terrain.txt. false — ошибка в err
bool terrain_desc_parse(const u8 *text, usize size, TerrainDesc &out, char *err, usize err_cap);
// Построить землю: память сетки — из world, временная — из scratch (откатывается). Испорченная карта дюн
// (проверяет app, dune_map_check) — локация без дюн
void terrain_generate(Terrain &t, const TerrainDesc &d, Arena &world, Arena &scratch);

// Октава слоя деталей: период 2^lp м, размах amp единиц при шероховатости 255. Таблицу получает и шейдер
void terrain_detail_octave(u32 k, i32 *lp, i32 *amp);

// Дюны в узле решётки 1 м, единицы 1/1280 м, ≥ 0: целая функция, общая с шейдером (docs/terrain.md, «Дюны»).
// amp — сила дюн 0..255 (маска)
i32 terrain_dune(i32 gx, i32 gz, u32 amp, u32 seed);
// Высота узла решётки 1 м в единицах 1/1280 м. gx, gz — метры от юго-западного угла карты (за краем —
// высота края). Сетка 8 м + слой деталей + дюны с точностью 1 м (гребни острые, а не размытые сеткой)
i32 terrain_node(const Terrain &t, i32 gx, i32 gz);
// Наибольший наклон сетки 8 м в клетке узла, единицы 1/1280 м на метр (для вида слоя: гладкий, без кочек)
i32 terrain_base_slope(const Terrain &t, i32 gx, i32 gz);
// Маски в узле решётки 1 м (билинейно из 32 м), 0..255
struct TerrainMasks {
    u32 rough, sand, grass, wet;
};
TerrainMasks terrain_masks(const Terrain &t, i32 gx, i32 gz);
// Вторые маски в узле: сила дюн и пустыня, 0..255
struct TerrainMasks2 {
    u32 dune, desert;
};
TerrainMasks2 terrain_masks2(const Terrain &t, i32 gx, i32 gz);
// Уровень воды в узле решётки 1 м, единицы 1/1280 м; 0 — воды нет. Вода есть, где уровень выше terrain_node()
i32 terrain_water(const Terrain &t, i32 gx, i32 gz);
