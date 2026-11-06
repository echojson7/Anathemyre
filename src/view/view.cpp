#include "view.h"

static void rel(const Camera &cam, i32 cx, i32 cz, f32 x, f32 z, f32 &rx, f32 &ry) {
    Vec2 d = wpos_delta_xz(cam.center.cx, cam.center.cz, cam.center.x, cam.center.z, cx, cz, x, z);
    rx = d.x;
    ry = d.y;
}

void view_build(const Camera &cam, const ViewInput &in, Arena &frame, RenderFrame &out) {
    out = RenderFrame{};
    out.clear[0] = 0.085f;
    out.clear[1] = 0.078f;
    out.clear[2] = 0.070f;
    f32 aspect = in.height > 0 ? (f32)in.width / (f32)in.height : 1.0f;
    out.meters_to_ndc[0] = 1.0f / (cam.half_height * aspect);
    out.meters_to_ndc[1] = 1.0f / cam.half_height;
    out.triangle = true;

    const Extract &p = *in.prev;
    const Extract &c = *in.cur;
    RenderPoint *pts = arena_push<RenderPoint>(frame, RENDER_MAX_POINTS);
    u32 np = 0;
    f32 a = in.alpha;
    for (u32 i = 0; i < DOTS_CAPACITY && np < RENDER_MAX_POINTS; ++i) {
        if (!c.gen[i]) continue;
        f32 x1, y1;
        rel(cam, c.cx[i], c.cz[i], c.x[i], c.z[i], x1, y1);
        if (p.gen[i] == c.gen[i]) {
            f32 x0, y0;
            rel(cam, p.cx[i], p.cz[i], p.x[i], p.z[i], x0, y0);
            x1 = x0 + (x1 - x0) * a;
            y1 = y0 + (y1 - y0) * a;
        }
        pts[np++] = {x1, y1, 4.0f, rgba8(196, 178, 140)};
    }
    out.points = {pts, np};

    if (in.dbg && in.dbg->count) {
        const DebugDraw &d = *in.dbg;
        u32 nv = d.count * 2 < RENDER_MAX_LINE_VERTICES ? d.count * 2 : RENDER_MAX_LINE_VERTICES;
        RenderLineVertex *v = arena_push<RenderLineVertex>(frame, nv);
        for (u32 i = 0; i < nv / 2; ++i) {
            const DebugLine &l = d.lines[i];
            RenderLineVertex &va = v[2 * i], &vb = v[2 * i + 1];
            rel(cam, l.a.cx, l.a.cz, l.a.x, l.a.z, va.x, va.y);
            rel(cam, l.b.cx, l.b.cz, l.b.x, l.b.z, vb.x, vb.y);
            va.rgba = vb.rgba = l.rgba;
        }
        out.lines = {v, nv};
    }
}

WPos view_screen_to_world(const Camera &cam, i32 width, i32 height, f32 px, f32 py) {
    f32 aspect = height > 0 ? (f32)width / (f32)height : 1.0f;
    f32 nx = (px / (f32)width) * 2.0f - 1.0f;
    f32 ny = 1.0f - (py / (f32)height) * 2.0f;
    WPos w = cam.center;
    w.x += nx * cam.half_height * aspect;
    w.z += ny * cam.half_height;
    w.y = 0;
    wpos_rebase(w);
    return w;
}

void view_zoom(Camera &cam, f32 wheel) {
    for (; wheel > 0.5f; wheel -= 1.0f) cam.half_height *= 0.85f;
    for (; wheel < -0.5f; wheel += 1.0f) cam.half_height /= 0.85f;
    cam.half_height = f_clamp(cam.half_height, 5.0f, 2000.0f);
}
