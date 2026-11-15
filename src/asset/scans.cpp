#include "scans.h"
#include <stdio.h>
#include <string.h>

// Заголовок: "ASCN", версия 1, слоёв, размер, мипов (u32, младший байт первым), имена слоёв по 16 байт
// (нули в хвосте), потом цвет и нормаль — по мипам, внутри мипа слои подряд
static u32 rd_u32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

bool scans_parse(const u8 *data, usize size, ScansPack &pack, char *err, usize err_cap) {
    pack = ScansPack{};
    if (size < 20 || memcmp(data, "ASCN", 4) != 0 || rd_u32(data + 4) != 1) {
        snprintf(err, err_cap, "не пакет сканов версии 1");
        return false;
    }
    u32 layers = rd_u32(data + 8), sz = rd_u32(data + 12), mips = rd_u32(data + 16);
    if (!layers || layers > SCANS_MAX_LAYERS || sz < 1 || sz > 4096 || (sz & (sz - 1)) || mips < 1 || mips > 13 ||
        (sz >> (mips - 1)) < 1) {
        snprintf(err, err_cap, "заголовок испорчен: слоёв %u, размер %u, мипов %u", layers, sz, mips);
        return false;
    }
    usize bytes = 0;
    for (u32 m = 0; m < mips; ++m) {
        usize s = sz >> m;
        bytes += s * s * 4 * layers;
    }
    usize head = 20 + 16 * (usize)layers;
    if (size != head + 2 * bytes) {
        snprintf(err, err_cap, "размер %zu, ждали %zu", size, head + 2 * bytes);
        return false;
    }
    for (u32 l = 0; l < layers; ++l) {
        const char *n = (const char *)data + 20 + 16 * l;
        pack.names[l] = {n, (u32)strnlen(n, 16)};
    }
    pack.layers = layers;
    pack.size = sz;
    pack.mips = mips;
    pack.albedo = data + head;
    pack.normal = data + head + bytes;
    pack.array_bytes = bytes;
    return true;
}

u32 scans_find(const ScansPack &pack, Str name) {
    for (u32 l = 0; l < pack.layers; ++l)
        if (pack.names[l].n == name.n && memcmp(pack.names[l].p, name.p, name.n) == 0) return l + 1;
    return 0;
}
