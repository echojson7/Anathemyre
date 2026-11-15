// Пакет сканов CC0 (docs/scans.md): build/<цель>/data/scans.pak пишет tools/scans/pack.py из картинок в кеше.
// Слои одного размера, мипы от size до 1. Две картинки-массива: цвет sRGB + AO и нормаль (OpenGL) + шероховатость.
// Пакета нет — материалы остаются узором, игра от этого не зависит
#pragma once
#include "../core/text.h"

constexpr u32 SCANS_MAX_LAYERS = 16;

struct ScansPack {
    u32 layers = 0, size = 0, mips = 0;
    Str names[SCANS_MAX_LAYERS];
    const u8 *albedo = nullptr;  // мип 0 всех слоёв подряд, потом мип 1… — RGBA8
    const u8 *normal = nullptr;
    usize array_bytes = 0;       // байт одной картинки-массива со всеми мипами
};

// false — пакет испорчен (текст ошибки в err); data живёт дольше pack
bool scans_parse(const u8 *data, usize size, ScansPack &pack, char *err, usize err_cap);
// Слой по имени + 1; нет такого — 0
u32 scans_find(const ScansPack &pack, Str name);
