// Земля мира (docs/terrain.md): сетка высот 8 м на всю карту и маски 32 м (шероховатость, песок) из зерна
// только целыми числами, над ними слой деталей 1 м — одна целая функция terrain_node() у шага мира и шейдера
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

// Описание земли (data/terrain.txt)
struct TerrainDesc {
    u32 seed = 1;
    i32 chunks = 110;          // сторона карты в чанках 256 м, чётная
    f32 start_plain = 1500.0f; // радиус равнины без гор у начала координат, м (временно, до штампов этапа 4)
};

struct Terrain {
    i32 chunks = 0;    // сторона карты в чанках
    i32 chunk0 = 0;    // номер чанка у западного и южного края: карта — чанки [chunk0, chunk0 + chunks)
    i32 size_m = 0;    // сторона в метрах
    u32 n = 0;         // узлов сетки высот на сторону: size / 8 + 1
    u32 nm = 0;        // узлов масок на сторону: size / 32 + 1
    u16 *height = nullptr;  // n·n, строками с юга на север, единица 5 см (0 — 3276.75 м)
    u16 *mask = nullptr;    // nm·nm: младший байт — шероховатость (сила слоя деталей), старший — песок
    u32 detail_seed = 0;    // зерно слоя деталей
    u32 oct_seed[TERRAIN_DETAIL_OCTAVES] = {};  // зёрна октав деталей (их же получает шейдер)
    u64 hash = 0;           // хеш сетки и масок
};

// Разобрать data/terrain.txt. false — ошибка в err
bool terrain_desc_parse(const u8 *text, usize size, TerrainDesc &out, char *err, usize err_cap);
// Построить землю: память сетки — из world, временная — из scratch (откатывается)
void terrain_generate(Terrain &t, const TerrainDesc &d, Arena &world, Arena &scratch);

// Октава слоя деталей: период 2^lp м, размах amp единиц при шероховатости 255. Таблицу получает и шейдер
void terrain_detail_octave(u32 k, i32 *lp, i32 *amp);

// Высота узла решётки 1 м в единицах 1/1280 м. gx, gz — метры от юго-западного угла карты (за краем —
// высота края)
i32 terrain_node(const Terrain &t, i32 gx, i32 gz);
// Наибольший наклон сетки 8 м в клетке узла, единицы 1/1280 м на метр (для вида слоя: гладкий, без кочек)
i32 terrain_base_slope(const Terrain &t, i32 gx, i32 gz);
// Маски в узле решётки 1 м (билинейно из 32 м): шероховатость и песок, 0..255
void terrain_masks(const Terrain &t, i32 gx, i32 gz, u32 *rough, u32 *sand);
