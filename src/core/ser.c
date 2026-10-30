#include "ser.h"

#include <string.h>

u64 ser_fnv(u64 h, const void *p, size_t n)
{
    const u8 *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

void ser_bytes(Ser *s, void *p, size_t n)
{
    if (s->bad || !n) return;
    if (s->mode == SER_SAVE && fwrite(p, 1, n, s->f) != n) s->bad = true;
    if (s->mode == SER_LOAD && fread(p, 1, n, s->f) != n) s->bad = true;
    if (s->bad) return;
    s->hash = ser_fnv(s->hash, p, n);
    s->bytes += n;
}

void ser_str(Ser *s, const char **str)
{
    u16 len = *str ? (u16)strlen(*str) : 0xFFFFu;
    SER(s, len);
    if (s->bad || len == 0xFFFFu) {
        if (s->mode == SER_LOAD && !s->bad) *str = NULL;
        return;
    }
    if (s->mode != SER_LOAD) {
        ser_bytes(s, (void *)*str, len);
        return;
    }
    if (s->pool_used + len + 1u > s->pool_cap) {
        s->bad = true;
        return;
    }
    char *dst = s->pool + s->pool_used;
    ser_bytes(s, dst, len);
    dst[len] = 0;
    s->pool_used += len + 1u;
    *str = dst;
}

void ser_count(Ser *s, u32 *n, u32 cap)
{
    u32 v = *n;
    SER(s, v);
    if (s->mode != SER_LOAD || s->bad) return;
    if (v > cap) s->bad = true;
    else *n = v;
}

void ser_check(Ser *s, bool ok)
{
    if (s->mode == SER_LOAD && !ok) s->bad = true;
}
