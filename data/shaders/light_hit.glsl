// Попадание пикселя и его поверхность (docs/perf_arch.md, шаг 4): общее у освещения (light_cells.comp) и прохода
// материала (surf_cells.comp). До включения: resolve.glsl, gcache.glsl, shade.glsl, константы RESOLVE_GROUND,
// RESOLVE_MESH и GC_KIND вида плиток
#ifndef LIGHT_HIT_GLSL
#define LIGHT_HIT_GLSL

const uint HIT_GCACHE = 3u;  // земля из кеша: rel — точка пикселя, level — уровень, m0, m1 — номер текселя

// Попадание пикселя: из кеша земли (тексель записал разбор плиток в gc_cells, точка — на луче пикселя на его
// глубине; на пиксель — нижний из четырёх смешиваемых текселей, доля между ними — от этой точки) или разбор
// visibility buffer. id — номер пикселя; у земли из кеша — 0xFFFFFFFE (все пиксели текселя — одна поверхность,
// какой бы треугольник ни был). c — тексель кеша пикселя (kt = GC_NONE — мимо кеша)
Hit pixel_hit(ivec2 px, bool inside, inout uint id, out GcCell c) {
    FrameData f = pc.frame;
    bool ground = inside && id != 0u && (id & VIS_GROUND) != 0u;
    c.kt = GC_NONE;
    if (ground && f.gc_on != 0u && GC_KIND) c = f.gc_cells.v[px.y * int(f.viewport.x) + px.x];
    Hit h;
    if (c.kt != GC_NONE) {
        h.kind = HIT_GCACHE;
        h.t = c.depth;
        h.rel = pixel_ray(px) * c.depth;
        vec2 fr;
        ivec2 tx = f.pixel != 0u ? gc_cell_texel(c) : gc_bilinear(h.rel, gc_cell_level(c), fr);
        h.n = h.ng = vec3(0.0, 1.0, 0.0);
        h.blend = h.ao = 0.0;
        h.level = uint(gc_cell_level(c));
        h.m0 = uint(tx.x);
        h.m1 = uint(tx.y);
        h.seed = 0u;
        id = 0xFFFFFFFEu;
    } else {
        h = resolve(px, inside ? id : 0u);
    }
    return h;
}

// Поверхность для света. Земля: тексель из кеша; у смеси и земли на пиксель — тексель без страницы (той же
// функцией, что запекание, и через ту же упаковку — картинка та же) или крутой склон на пиксель. Земля и сетки
// зовут материалы одним surface_uv() в цикле: код слоёв и материалов в конвейере — одна копия
Surface hit_surface(Hit h, int phys) {
    Surface s;
    g_gcl_ok = false;  // свет из кеша — только у пути gc_surface_px
    bool gc = GC_KIND && h.kind == HIT_GCACHE;
    int k = int(h.level);
    ivec2 t = ivec2(int(h.m0), int(h.m1));
    if (gc && phys >= 0) {
        if (pc.frame.pixel != 0u) {
            s = gc_surface(gc_read(phys, t), k, t, h.rel);
        } else {
            vec2 fr;
            gc_bilinear(h.rel, k, fr);
            s = gc_surface_px(k, t, fr, h.rel, phys);
        }
    } else if (RESOLVE_GROUND || RESOLVE_MESH) {
        // Земля (пиксель или тексель без страницы) или сетка — одним циклом с одним вызовом surface_uv(): смесь
        // видит оба пути, и два вызова встраивали бы все материалы дважды (сборка конвейера на AMD — минуты)
        bool gr = RESOLVE_GROUND && (gc || !RESOLVE_MESH || h.kind == HIT_GROUND);
        GroundIn g;
        MeshCall mc;
        if (gr) g = gc ? ground_texel_in(k, t) : ground_pixel_in(h.rel, normalize(h.n), h.level, h.blend, h.ng);
        else mc = mesh_call_in(h.rel, normalize(h.n), h.m0, h.m1, h.blend, h.ao, h.seed, h.dyn, h.obj, h.obj_n,
                               h.has_uv, h.uv, h.duv, h.tan);
        ivec2 saved = ORIGIN_CHUNK;
        ORIGIN_CHUNK = gr ? g.origin : mesh_origin(mc);
        Surface sv[2], cur;
        GroundPlan pl;
        int v = 0, i = 0;
        [[dont_unroll]] for (;;) {
            // Земля: варианты v (у текселя — два следа), в каждом — слои i плана
            if (gr && i == 0) pl = ground_plan(g.p, g.n, g.wdepth);
            uint id = mc.mat, seed = mc.seed;
            vec3 sp = mc.ps, sn = mc.ns;
            float cell = mc.cell, foot = mc.foot;
            SurfUv tu = mc.t;
            if (gr) {
                id = pc.frame.ground_layers[pl.lays[i]];
                seed = GROUND_SEED;
                sp = g.p;
                sn = g.n;
                cell = g.cell;
                foot = g.foot[v];
                tu.on = false;
                tu.uv = vec2(0.0);
                tu.duv = tu.tan = vec4(0.0);
            }
            Surface r = surface_uv(id, sp, sn, cell, foot, seed, tu);
            if (!gr) {
                cur = r;
                break;
            }
            ground_feed(pl, i, r, cur);
            if (++i < pl.nl) continue;
            ground_finish(pl, cur);
            sv[v] = cur;
            i = 0;
            if (++v >= g.nv) break;
        }
        ORIGIN_CHUNK = saved;
        if (!gr) {
            s = mesh_finish(mc, cur);
        } else if (gc) {
            s = gc_surface(uvec4(gc_pack(sv[0]), gc_pack(sv[1])), k, t, h.rel);
        } else {
            s = sv[0];
            s.pos = g.p - pc.frame.cam_off;
        }
    } else {
        s = gc_unpack(uvec2(0u));  // вид «земля»: других попаданий нет
        s.pos = h.rel;
    }
    return s;
}
#endif
