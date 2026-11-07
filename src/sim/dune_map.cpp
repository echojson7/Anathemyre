#include "dune_map.h"
#include <string.h>

static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

bool dune_map_check(const u8 *data, usize size, u32 w, u32 h) {
    if (!data || size < 20 || memcmp(data, "DUN1", 4) != 0) return false;
    return rd32(data + 4) == w && rd32(data + 8) == h && rd32(data + 12) < 16 && 20 + (usize)rd32(data + 16) <= size;
}

struct BitReader {
    const u8 *p, *end;
    u64 acc = 0;
    u32 n = 0;
    bool over = false;
    u32 get(u32 bits) {  // bits ≤ 32
        while (n < bits) {
            if (p < end) acc |= (u64)*p++ << n;
            else over = true;
            n += 8;
        }
        u32 v = (u32)(acc & ((bits == 32) ? 0xFFFFFFFFull : ((1ull << bits) - 1)));
        acc >>= bits;
        n -= bits;
        return v;
    }
};

bool dune_map_decode(const u8 *data, usize size, u16 *out, u32 w, u32 h) {
    if (!dune_map_check(data, size, w, h)) return false;
    u32 k = rd32(data + 12);
    BitReader br{data + 20, data + 20 + rd32(data + 16)};
    for (u32 j = 0; j < h; ++j) {
        for (u32 i = 0; i < w; ++i) {
            u32 q = 0;
            while (q < 32 && br.get(1)) ++q;
            u32 z = q < 32 ? (q << k) | (k ? br.get(k) : 0) : br.get(32);
            i32 r = (z & 1) ? -(i32)((z + 1) >> 1) : (i32)(z >> 1);
            usize c = (usize)j * w + i;
            i32 pr;
            if (i && j) pr = (i32)out[c - 1] + (i32)out[c - w] - (i32)out[c - w - 1];
            else if (i) pr = out[c - 1];
            else if (j) pr = out[c - w];
            else pr = 0;
            i32 v = pr + r;
            out[c] = (u16)(v < 0 ? 0 : (v > 65535 ? 65535 : v));
        }
    }
    return !br.over;
}
