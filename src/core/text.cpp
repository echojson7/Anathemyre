#include "text.h"
#include "hash.h"

bool str_eq(Str s, const char *lit) {
    u32 n = (u32)strlen(lit);
    return s.n == n && memcmp(s.p, lit, n) == 0;
}

u64 str_hash(Str s) { return hash_bytes(s.p, s.n); }

static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

bool text_next(TextReader &r, TextLine &out) {
    while (r.p < r.end) {
        const char *eol = r.p;
        while (eol < r.end && *eol != '\n') eol++;
        r.line++;
        out = TextLine{};
        out.line = r.line;
        const char *q = r.p;
        r.p = eol < r.end ? eol + 1 : eol;
        while (q < eol) {
            while (q < eol && is_space(*q)) q++;
            if (q >= eol || *q == '#') break;
            const char *w = q;
            while (q < eol && !is_space(*q) && *q != '#') q++;
            if (out.count < TEXT_MAX_WORDS) out.w[out.count] = {w, (u32)(q - w)};
            if (out.count <= TEXT_MAX_WORDS) out.count++;
        }
        if (out.count) return true;
    }
    return false;
}

bool parse_f32(Str s, f32 &out) {
    static const f64 POW10[19] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
                                  1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18};
    u32 i = 0;
    bool neg = false;
    if (i < s.n && (s.p[i] == '-' || s.p[i] == '+')) { neg = s.p[i] == '-'; i++; }
    u64 mant = 0;
    u32 digits = 0, frac = 0;
    bool dot = false, any = false;
    for (; i < s.n; ++i) {
        char c = s.p[i];
        if (c == '.' && !dot) { dot = true; continue; }
        if (c < '0' || c > '9') return false;
        any = true;
        if (mant == 0 && c == '0') {  // ведущие нули не считаются значащими
            if (dot) frac++;
            continue;
        }
        if (digits >= 18) return false;
        mant = mant * 10 + (u64)(c - '0');
        digits++;
        if (dot) frac++;
    }
    if (!any || frac > 18) return false;
    // u64 до 1e18 и степени 10 до 1e18 точны в f64 (до 2^53 — мантисса, степени 10 до 1e22 точны);
    // деление IEEE округляет одинаково на всех x86-64
    f64 v = (f64)mant / POW10[frac];
    out = (f32)(neg ? -v : v);
    return true;
}

bool parse_i32(Str s, i32 &out) {
    u32 i = 0;
    bool neg = false;
    if (i < s.n && (s.p[i] == '-' || s.p[i] == '+')) { neg = s.p[i] == '-'; i++; }
    if (i >= s.n) return false;
    i64 v = 0;
    for (; i < s.n; ++i) {
        char c = s.p[i];
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
        if (v > 2147483648ll) return false;
    }
    if (neg) v = -v;
    if (v > 2147483647ll) return false;
    out = (i32)v;
    return true;
}

bool parse_u32(Str s, u32 &out) {
    if (s.n == 0) return false;
    u64 v = 0;
    for (u32 i = 0; i < s.n; ++i) {
        char c = s.p[i];
        if (c < '0' || c > '9') return false;
        v = v * 10 + (u64)(c - '0');
        if (v > 0xFFFFFFFFull) return false;
    }
    out = (u32)v;
    return true;
}

u64 text_hash(const u8 *data, usize size) {
    u64 h = 0x51ED;
    u8 buf[256];
    u32 n = 0;
    for (usize i = 0; i < size; ++i) {
        if (data[i] == '\r') continue;
        buf[n++] = data[i];
        if (n == sizeof buf) { h = hash_combine(h, hash_bytes(buf, n)); n = 0; }
    }
    if (n) h = hash_combine(h, hash_bytes(buf, n));
    return h;
}
