// Эрозия водой крупного слоя земли (docs/terrain.md, «Эрозия»): капли по одной стекают по склону, смывают
// грунт там, где разгоняются, и откладывают его, где тормозят. Так появляются долины, промоины, отроги и
// конусы выноса — то, чего нет у шума. Float без FMA, только + − × / и sqrt (docs/invariants.md): капли идут
// строго по порядку, поэтому сетка одна и та же на любой цели
#include "terrain_erode.h"
#include "../core/mathf.h"

static inline u32 eh(u32 v) {
    u32 s = v * 747796405u + 2891336453u;
    u32 w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
static inline f32 eh01(u32 v) { return (f32)(eh(v) >> 8) * (1.0f / 16777216.0f); }

// Высота и наклон билинейно в точке (x, z) в клетках
static inline void sample(const f32 *h, u32 n, f32 x, f32 z, f32 *hgt, f32 *gx, f32 *gz) {
    i32 ix = (i32)x, iz = (i32)z;
    f32 u = x - (f32)ix, v = z - (f32)iz;
    const f32 *p = h + (usize)iz * n + (usize)ix;
    f32 h00 = p[0], h10 = p[1], h01 = p[n], h11 = p[n + 1];
    *gx = (h10 - h00) * (1.0f - v) + (h11 - h01) * v;
    *gz = (h01 - h00) * (1.0f - u) + (h11 - h10) * u;
    *hgt = (h00 * (1.0f - u) + h10 * u) * (1.0f - v) + (h01 * (1.0f - u) + h11 * u) * v;
}

void terrain_erode(f32 *h, u32 nx, u32 nz, const ErodeParams &prm, f32 *deposit, f32 *flow, Arena &scratch) {
    usize mark = arena_mark(scratch);
    const u32 n = nx;  // шаг строки
    // Кисть смыва: клетки в радиусе r с весом (r − d), сумма весов 1
    const i32 r = prm.radius;
    const i32 side = 2 * r + 1;
    i32 *bo = arena_push<i32>(scratch, (usize)(side * side));
    i32 *bx = arena_push<i32>(scratch, (usize)(side * side));
    i32 *bz = arena_push<i32>(scratch, (usize)(side * side));
    f32 *bw = arena_push<f32>(scratch, (usize)(side * side));
    i32 nb = 0;
    f32 wsum = 0.0f;
    for (i32 dz = -r; dz <= r; ++dz)
        for (i32 dx = -r; dx <= r; ++dx) {
            f32 d = f_sqrt((f32)(dx * dx + dz * dz));
            if (d > (f32)r) continue;
            bx[nb] = dx;
            bz[nb] = dz;
            bo[nb] = dz * (i32)n + dx;
            bw[nb] = (f32)r - d + 0.5f;
            wsum += bw[nb];
            nb++;
        }
    for (i32 k = 0; k < nb; ++k) bw[k] /= wsum;

    const f32 limx = (f32)(nx - 2), limz = (f32)(nz - 2);
    for (u32 drop = 0; drop < prm.droplets; ++drop) {
        f32 x = 1.0f + eh01(drop * 2u + prm.seed) * (limx - 1.0f);
        f32 z = 1.0f + eh01(drop * 2u + 1u + prm.seed * 3u) * (limz - 1.0f);
        f32 dx = 0.0f, dz = 0.0f, speed = prm.init_speed, water = 1.0f, sed = 0.0f;
        for (u32 life = 0; life < prm.lifetime; ++life) {
            i32 ix = (i32)x, iz = (i32)z;
            f32 u = x - (f32)ix, v = z - (f32)iz;
            usize node = (usize)iz * n + (usize)ix;
            f32 hh, gx, gz;
            sample(h, n, x, z, &hh, &gx, &gz);
            if (flow) flow[node] += water;
            // Направление: инерция + вниз по склону, шаг — одна клетка
            dx = dx * prm.inertia - gx * (1.0f - prm.inertia);
            dz = dz * prm.inertia - gz * (1.0f - prm.inertia);
            f32 len = f_sqrt(dx * dx + dz * dz);
            if (len < 1e-6f) break;
            dx /= len;
            dz /= len;
            f32 px = x + dx, pz = z + dz;
            if (px < 1.0f || pz < 1.0f || px >= limx || pz >= limz) break;
            f32 nh, ngx, ngz;
            sample(h, n, px, pz, &nh, &ngx, &ngz);
            f32 dh = nh - hh;
            f32 cap = f_max(-dh, prm.min_slope) * speed * water * prm.capacity;
            if (sed > cap || dh > 0.0f) {
                // Откладывает: в яму — до её края, иначе излишек
                f32 put = dh > 0.0f ? f_min(dh, sed) : (sed - cap) * prm.deposit;
                sed -= put;
                f32 w00 = (1.0f - u) * (1.0f - v), w10 = u * (1.0f - v), w01 = (1.0f - u) * v, w11 = u * v;
                h[node] += put * w00;
                h[node + 1] += put * w10;
                h[node + n] += put * w01;
                h[node + n + 1] += put * w11;
                if (deposit) {
                    deposit[node] += put * w00;
                    deposit[node + 1] += put * w10;
                    deposit[node + n] += put * w01;
                    deposit[node + n + 1] += put * w11;
                }
            } else {
                // Смывает кистью, не глубже перепада (иначе роет ямы)
                f32 take = f_min((cap - sed) * prm.erode, -dh);
                for (i32 k = 0; k < nb; ++k) {
                    i32 cx = ix + bx[k], cz = iz + bz[k];
                    if (cx < 0 || cz < 0 || cx >= (i32)nx || cz >= (i32)nz) continue;
                    f32 *c = h + (i64)node + bo[k];
                    f32 dw = take * bw[k];
                    f32 got = *c < dw ? *c : dw;
                    *c -= got;
                    sed += got;
                }
            }
            f32 s2 = speed * speed - dh * prm.gravity;
            speed = s2 > 0.0f ? f_sqrt(s2) : 0.0f;
            if (speed > prm.max_speed) speed = prm.max_speed;
            water *= 1.0f - prm.evaporate;
            x = px;
            z = pz;
        }
    }
    arena_restore(scratch, mark);
}
