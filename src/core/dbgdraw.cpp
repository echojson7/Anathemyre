#include "dbgdraw.h"

void dbg_init(DebugDraw &d, Arena &a, u32 capacity) {
    d.lines = arena_push<DebugLine>(a, capacity);
    d.capacity = capacity;
    d.count = 0;
}

void dbg_line(DebugDraw &d, const WPos &a, const WPos &b, u32 rgba) {
    if (!d.enabled) return;
    if (d.count >= d.capacity) { d.dropped++; return; }
    d.lines[d.count++] = {a, b, rgba};
}

void dbg_cross(DebugDraw &d, const WPos &p, f32 r, u32 rgba) {
    WPos a = p, b = p;
    a.x -= r; b.x += r;
    wpos_rebase(a); wpos_rebase(b);
    dbg_line(d, a, b, rgba);
    a = p; b = p;
    a.z -= r; b.z += r;
    wpos_rebase(a); wpos_rebase(b);
    dbg_line(d, a, b, rgba);
}
