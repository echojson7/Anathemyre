// Текстура .tex версии 2 (docs/textures.md): сжатие BC, мипы от полного размера до 4 × 4. Пишет tools/texpack из
// PNG, игра только читает и отдаёт рендеру как есть (сжатые блоки идут в видеопамять без распаковки)
#pragma once
#include "../core/types.h"

// Формат блоков, как в tools/texpack
enum TexFormat : u32 {
    TEX_BC7_SRGB = 1,   // цвет sRGB + затенение в альфе
    TEX_BC7_UNORM = 2,  // линейные маски в четырёх каналах
    TEX_BC5_UNORM = 3,  // нормаль: x, y (z — в шейдере)
    TEX_BC4_UNORM = 4,  // одна маска
};

constexpr u32 TEX_MAX_MIPS = 12;

struct TexData {
    TexFormat format = TEX_BC7_SRGB;
    u32 width = 0, height = 0, mips = 0;
    const u8 *blocks = nullptr;      // мип 0, потом 1… — блоки строками (указывает в данные файла)
    usize mip_offset[TEX_MAX_MIPS];  // начало мипа в blocks
    usize bytes = 0;                 // всех мипов
};

// Байт в блоке 4 × 4
inline u32 tex_block_bytes(TexFormat f) { return f == TEX_BC4_UNORM ? 8u : 16u; }
// false — файл испорчен, причина в *err; data живёт дольше out
bool tex_parse(const u8 *data, usize size, TexData &out, const char **err);
