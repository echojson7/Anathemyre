// Память: арены (линейное выделение с откатом), Span и FixedArray над ареной.
// В игровом цикле нет malloc/free: всё берётся из арен, выделенных при старте.
// Сроки жизни: арена мира (всю игру), шага (сброс каждый шаг), кадра (сброс каждый кадр), загрузки.
#pragma once
#include "types.h"
#include <string.h>

struct Arena {
    u8 *base = nullptr;
    usize size = 0;
    usize used = 0;
    const char *name = "";
};

void arena_init(Arena &a, void *memory, usize size, const char *name);
// Выделить n байт с выравниванием align; нет места — ana_fail с именем арены
void *arena_push_bytes(Arena &a, usize n, usize align);
inline void arena_reset(Arena &a) { a.used = 0; }
inline usize arena_mark(const Arena &a) { return a.used; }
inline void arena_restore(Arena &a, usize mark) { a.used = mark; }

// Массив из n элементов, заполнен нулями
template <typename T>
T *arena_push(Arena &a, usize n) {
    T *p = (T *)arena_push_bytes(a, sizeof(T) * n, alignof(T) > 16 ? alignof(T) : 16);
    memset((void *)p, 0, sizeof(T) * n);
    return p;
}

template <typename T>
struct Span {
    T *data = nullptr;
    usize count = 0;

    Span() = default;
    Span(T *d, usize n) : data(d), count(n) {}
    // Span<T> → Span<const T>
    template <typename U>
        requires requires(U *p) { static_cast<T *>(p); }
    Span(const Span<U> &o) : data(o.data), count(o.count) {}
    T &operator[](usize i) const { ANA_ASSERT(i < count); return data[i]; }
    T *begin() const { return data; }
    T *end() const { return data + count; }
};

// Массив фиксированной ёмкости: память из арены, push_back до ёмкости
template <typename T>
struct FixedArray {
    T *data = nullptr;
    usize count = 0;
    usize capacity = 0;

    void init(Arena &a, usize cap) { data = arena_push<T>(a, cap); count = 0; capacity = cap; }
    bool push_back(const T &v) {
        if (count >= capacity) return false;
        data[count++] = v;
        return true;
    }
    void clear() { count = 0; }
    T &operator[](usize i) { ANA_ASSERT(i < count); return data[i]; }
    const T &operator[](usize i) const { ANA_ASSERT(i < count); return data[i]; }
    Span<T> span() { return {data, count}; }
    Span<const T> span() const { return {data, count}; }
};
