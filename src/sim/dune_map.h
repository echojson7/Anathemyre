// Карта дюн .dun (docs/terrain.md, «Дюны из карты»; пишет tools/dunes/dune_pack.py): сетка высот дюн над дном,
// единица 5 см, сжатая кодом Райса по остатку предсказания плоскостью. Только целые операции
#pragma once
#include "../core/types.h"

// Заголовок верен и сетка w×h. Проверяет только заголовок и размер потока
bool dune_map_check(const u8 *data, usize size, u32 w, u32 h);
// Распаковать w×h значений (строками с юга на север) в out. false — поток кончился раньше
bool dune_map_decode(const u8 *data, usize size, u16 *out, u32 w, u32 h);
