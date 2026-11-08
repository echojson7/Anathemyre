// Двоичная запись и чтение по полям, little-endian. Структуры целиком не пишутся (дыры выравнивания).
// Файлы — куски «тег, версия, длина»: незнакомый кусок читатель пропускает.
#pragma once
#include "mem.h"
#include "text.h"

struct Writer {
    u8 *data = nullptr;
    usize size = 0;
    usize cap = 0;
    bool overflow = false;
};

inline void writer_init(Writer &w, Arena &a, usize cap) {
    w.data = arena_push<u8>(a, cap);
    w.cap = cap;
    w.size = 0;
    w.overflow = false;
}
inline void put_bytes(Writer &w, const void *p, usize n) {
    if (w.size + n > w.cap) { w.overflow = true; return; }
    memcpy(w.data + w.size, p, n);
    w.size += n;
}
inline void put_u8(Writer &w, u8 v) { put_bytes(w, &v, 1); }
inline void put_u32(Writer &w, u32 v) { put_bytes(w, &v, 4); }
inline void put_u64(Writer &w, u64 v) { put_bytes(w, &v, 8); }
inline void put_i32(Writer &w, i32 v) { put_bytes(w, &v, 4); }
inline void put_f32(Writer &w, f32 v) { put_bytes(w, &v, 4); }

// Начать кусок: тег из 4 букв, версия; длина дописывается в chunk_end
inline usize chunk_begin(Writer &w, const char tag[4], u32 version) {
    put_bytes(w, tag, 4);
    put_u32(w, version);
    usize at = w.size;
    put_u64(w, 0);
    return at;
}
inline void chunk_end(Writer &w, usize at) {
    if (w.overflow) return;
    u64 len = (u64)(w.size - at - 8);
    memcpy(w.data + at, &len, 8);
}

struct Reader {
    const u8 *data = nullptr;
    usize size = 0;
    usize pos = 0;
    bool bad = false;
};

inline bool get_bytes(Reader &r, void *p, usize n) {
    if (r.bad || r.pos + n > r.size) { r.bad = true; memset(p, 0, n); return false; }
    memcpy(p, r.data + r.pos, n);
    r.pos += n;
    return true;
}
inline u8 get_u8(Reader &r) { u8 v; get_bytes(r, &v, 1); return v; }
inline u16 get_u16(Reader &r) { u16 v; get_bytes(r, &v, 2); return v; }
inline u32 get_u32(Reader &r) { u32 v; get_bytes(r, &v, 4); return v; }
inline u64 get_u64(Reader &r) { u64 v; get_bytes(r, &v, 8); return v; }
inline i32 get_i32(Reader &r) { i32 v; get_bytes(r, &v, 4); return v; }
inline f32 get_f32(Reader &r) { f32 v; get_bytes(r, &v, 4); return v; }

struct Chunk {
    char tag[4];
    u32 version;
    Reader body;
};

// Следующий кусок; false — конец данных или порча
inline bool next_chunk(Reader &r, Chunk &c) {
    if (r.bad || r.pos + 16 > r.size) return false;
    get_bytes(r, c.tag, 4);
    c.version = get_u32(r);
    u64 len = get_u64(r);
    if (len > r.size - r.pos) { r.bad = true; return false; }
    c.body = Reader{r.data + r.pos, (usize)len, 0, false};
    r.pos += (usize)len;
    return true;
}
inline bool chunk_is(const Chunk &c, const char tag[4]) { return memcmp(c.tag, tag, 4) == 0; }

// Имя (u8 длина + байты) → устойчивый хеш (содержимое по имени); 0 — файл испорчен
inline u64 get_name_hash(Reader &r) {
    char s[256];
    u8 n = get_u8(r);
    get_bytes(r, s, n);
    return r.bad ? 0 : str_hash(Str{s, n});
}
