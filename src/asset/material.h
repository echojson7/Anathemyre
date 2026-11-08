// Таблица материалов (data/materials.txt, docs/materials.md): имя, вид и параметры. Вид — файл
// data/shaders/materials/<вид>.glsl; номер вида — место в списке видов, который пишет сборка
// (build/<цель>/data/material_kinds.txt) по тем же файлам, что и switch в шейдере
#pragma once
#include "../core/mem.h"
#include "../core/text.h"

constexpr u32 MATERIAL_MAX = 1024;
constexpr u32 MATERIAL_MAX_KINDS = 64;

struct MaterialDesc {
    Str name;
    u64 hash;      // устойчивый хеш имени (содержимое по имени)
    u32 kind;      // номер вида = номер функции в surface()
    // Параметры: p[0..2] color, p[3] rough, p[4..6] color2, p[7] texel, p[8..11] extra (смысл — у вида)
    f32 p[12];
    f32 metal;     // 0 — диэлектрик, 1 — металл (у вида — где металл виден: ржавчина и краска — нет)
};

struct MaterialTable {
    MaterialDesc *items = nullptr;
    u32 count = 0;
    Str kinds[MATERIAL_MAX_KINDS];
    u32 kind_count = 0;
};

// Номер 0 — всегда «missing» (яркий, чтобы потерянный материал было видно). false — ошибка в err
bool material_table_parse(const u8 *kinds_text, usize kinds_size, const u8 *text, usize size, Arena &arena,
                          MaterialTable &out, char *err, usize err_cap);
// Номер материала по хешу имени; нет такого — 0 (missing)
u32 material_find(const MaterialTable &t, u64 hash);
