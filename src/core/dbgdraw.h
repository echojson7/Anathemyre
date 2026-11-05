// Отладочное рисование: шаг мира и отладка пишут отрезки в буфер, кадр их рисует.
// Буфер не часть состояния мира (не в хеше и не в сохранении). Прямых вызовов рендера из симуляции нет
#pragma once
#include "mem.h"
#include "wpos.h"

struct DebugLine {
    WPos a, b;
    u32 rgba;
};

struct DebugDraw {
    DebugLine *lines = nullptr;
    u32 count = 0;
    u32 capacity = 0;
    u32 dropped = 0;
    bool enabled = true;
};

void dbg_init(DebugDraw &d, Arena &a, u32 capacity);
inline void dbg_clear(DebugDraw &d) { d.count = 0; d.dropped = 0; }
void dbg_line(DebugDraw &d, const WPos &a, const WPos &b, u32 rgba);
// Крест в плоскости XZ с полуразмером r метров
void dbg_cross(DebugDraw &d, const WPos &p, f32 r, u32 rgba);

constexpr u32 rgba8(u32 r, u32 g, u32 b, u32 a = 255) { return r | (g << 8) | (b << 16) | (a << 24); }
