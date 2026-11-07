// Штампы земли (docs/terrain.md, «Штампы»): правят готовые данные генератора — сетку 8 м, дюны 2 м и в узлах,
// маски — до хеша земли. Поэтому terrain_node() у шага мира и шейдер, нормали и тени картинки и дальние кольца
// видят одну и ту же площадку без отдельного кода. Только целые числа: та же земля на любой цели
#include "terrain.h"

// Доля штампа в точке, Q8 (256 — вся): 1 внутри прямоугольника [x0, x1]×[z0, z1], к краю перехода — 0,
// плавно (smoothstep). Расстояние — по большей из осей: углы площадки прямые
static i32 pad_weight(i32 gx, i32 gz, i32 x0, i32 z0, i32 x1, i32 z1, i32 margin) {
    i32 dx = gx < x0 ? x0 - gx : (gx > x1 ? gx - x1 : 0);
    i32 dz = gz < z0 ? z0 - gz : (gz > z1 ? gz - z1 : 0);
    i32 d = dx > dz ? dx : dz;
    if (d >= margin) return 0;
    i32 s = ((margin - d) * 256) / margin;
    return (s * s * (768 - 2 * s)) >> 16;
}

static i32 mix_q8(i32 a, i32 b, i32 w) {
    i64 v = (i64)a * (256 - w) + (i64)b * w;
    return (i32)((v + 128) >> 8);
}

// Выровнять площадку: высота — средняя по прямоугольнику до штампа (до 0.5 м); дюны карты 2 м гаснут, сетка 8 м встаёт
// на эту высоту, слой деталей (шероховатость) у площадки выключен
static void pad_apply(Terrain &t, const TerrainPad &p) {
    const i32 org_x = -t.chunk0 * 256, org_z = -t.chunk0 * 256;
    const i32 base = 1 << TERRAIN_BASE_SHIFT;
    // Прямоугольник в метрах от юго-западного угла, края — на сетке 8 м (наружу)
    i32 x0 = org_x + p.x - p.w / 2, x1 = org_x + p.x + (p.w - p.w / 2);
    i32 z0 = org_z + p.z - p.d / 2, z1 = org_z + p.z + (p.d - p.d / 2);
    x0 = (x0 >> TERRAIN_BASE_SHIFT) * base;
    z0 = (z0 >> TERRAIN_BASE_SHIFT) * base;
    x1 = ((x1 + base - 1) >> TERRAIN_BASE_SHIFT) * base;
    z1 = ((z1 + base - 1) >> TERRAIN_BASE_SHIFT) * base;
    const i32 m = p.margin;
    if (x1 + m < 0 || z1 + m < 0 || x0 - m > t.size_x || z0 - m > t.size_z) return;

    // Средняя высота по узлам 1 м, единицы 5 см
    i64 sum = 0, cnt = 0;
    for (i32 gz = z0; gz <= z1; ++gz)
        for (i32 gx = x0; gx <= x1; ++gx) {
            sum += terrain_node(t, gx, gz);
            cnt++;
        }
    // Высота кратна 0.5 м: ряды щитов опалубки в узоре бетона идут по мировой высоте и ложатся от пола
    const i32 target = (i32)(((sum / cnt + 32) / 64 + 5) / 10 * 10);

    // Дюны карты 2 м гаснут к площадке
    const i32 fs = 1 << TERRAIN_FINE_SHIFT;
    const i32 nloc = t.size_x / t.loc_m;
    for (i32 loc = 0; loc < nloc; ++loc) {
        if (t.fine_off[loc] < 0) continue;
        i32 lx0 = loc * t.loc_m;
        for (u32 j = 0; j < t.fine_n; ++j) {
            i32 gz = (i32)j * fs;
            if (gz < z0 - m || gz > z1 + m) continue;
            for (u32 i = 0; i < t.fine_n; ++i) {
                i32 gx = lx0 + (i32)i * fs;
                i32 w = pad_weight(gx, gz, x0, z0, x1, z1, m);
                if (!w) continue;
                u16 &f = t.fine[(usize)t.fine_off[loc] + (usize)j * t.fine_n + i];
                f = (u16)mix_q8(f, 0, w);
            }
        }
    }

    // Сетка 8 м: высота без дюн тянется к площадке, дюны в узле — заново из карты (или 0 у формулы)
    for (u32 j = 0; j < t.nz; ++j) {
        i32 gz = (i32)j * base;
        if (gz < z0 - m || gz > z1 + m) continue;
        for (u32 i = 0; i < t.nx; ++i) {
            i32 gx = (i32)i * base;
            i32 w = pad_weight(gx, gz, x0, z0, x1, z1, m);
            if (!w) continue;
            usize k = (usize)j * t.nx + i;
            i32 dn_old = (i32)t.dune[k] * 2;  // единицы 1/1280 м; младший бит потерян, но dn/64 от него не зависит
            i32 bare = (i32)t.height[k] - dn_old / 64;
            i32 loc = gx / t.loc_m;
            loc = loc >= nloc ? nloc - 1 : loc;
            i32 dn = 0;
            if (t.fine_off[loc] >= 0) {
                i32 li = (gx - loc * t.loc_m) >> TERRAIN_FINE_SHIFT, lj = gz >> TERRAIN_FINE_SHIFT;
                dn = (i32)t.fine[(usize)t.fine_off[loc] + (usize)lj * t.fine_n + (usize)li] * 2;
            } else {
                dn = mix_q8(dn_old, 0, w);
            }
            t.dune[k] = (u16)(dn >> 1);
            i32 u = mix_q8(bare, target, w) + dn / 64;
            t.height[k] = (u16)(u < 0 ? 0 : (u > 65535 ? 65535 : u));
        }
    }

    // Маски 32 м: шероховатость (слой деталей) — 0 у площадки; сила дюн-формулы — 0 там же (у карты дюн маска
    // только включает в шейдере точный наклон дюн, сама карта у площадки уже погашена)
    const i32 mc = 1 << TERRAIN_MASK_SHIFT;
    for (u32 j = 0; j < t.nmz; ++j) {
        i32 gz = (i32)j * mc;
        if (gz < z0 - mc || gz > z1 + mc) continue;
        for (u32 i = 0; i < t.nmx; ++i) {
            i32 gx = (i32)i * mc;
            if (gx < x0 - mc || gx > x1 + mc) continue;
            usize k = (usize)j * t.nmx + i;
            t.mask[k] &= ~0xFFu;
            i32 loc = gx / t.loc_m;
            loc = loc >= nloc ? nloc - 1 : loc;
            if (t.fine_off[loc] < 0) t.mask2[k] &= ~0xFFu;
        }
    }
}

void terrain_stamps_apply(Terrain &t, const TerrainDesc &d) {
    for (u32 i = 0; i < d.pad_count; ++i) pad_apply(t, d.pad[i]);
}
