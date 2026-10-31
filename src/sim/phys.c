#include "phys.h"

#include <string.h>

#include "../core/clock.h"

#define SUBSTEP_HZ   160.0f  // подшагов в секунду: при шаге мира 20 Гц их 8
#define GRAVITY      9.81f
#define SLEEP_V      0.04f   // м/с: медленнее — копит время сна
#define SLEEP_W      0.15f   // рад/с
#define SLEEP_S      0.5f
#define OUT_S        3.0f    // вышло из зоны на ходу: досчитывается не дольше
#define WAKE_DEPTH   0.004f  // спящего будит вхождение глубже, м
#define PAIR_MAX     8192
#define GROUND_EPS   0.10f   // шаг для наклона земли под телом, м
#define DEPEN_V      2.0f    // вдавленное в землю выходит не быстрее, м/с
#define PAIR_PUSH    0.01f   // вещи расходятся за подшаг не больше, м (3.6 м/с)
#define AIR_DAMP     0.5f    // гашение вращения в воздухе, 1/с
#define ROLL_DAMP    6.0f    // на земле
#define JOINT_ITERS  2       // проходов по суставам за подшаг: цепь из четырёх тел не провисает
#define JOINT_DAMP   3.0f    // трение в суставах, 1/с: мёртвое тело не болтается, как на верёвках

// ---------------------------------------------------------------- кватернионы xyzw

static V3 qv(V4 q) { return v3(q.x, q.y, q.z); }

static V4 qmul(V4 a, V4 b)
{
    return (V4){ a.w * b.x + b.w * a.x + a.y * b.z - a.z * b.y, a.w * b.y + b.w * a.y + a.z * b.x - a.x * b.z,
                 a.w * b.z + b.w * a.z + a.x * b.y - a.y * b.x, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}

static V4 qnorm(V4 q)
{
    f32 l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    f32 k = l > 1e-12f ? 1.0f / l : 0.0f;
    return l > 1e-12f ? (V4){ q.x * k, q.y * k, q.z * k, q.w * k } : (V4){ 0, 0, 0, 1 };
}

static V4 qconj(V4 q) { return (V4){ -q.x, -q.y, -q.z, q.w }; }

static V3 qrot(V4 q, V3 v)
{
    V3 u = qv(q), t = v3_mul(v3_cross(u, v), 2.0f);
    return v3_add(v3_add(v, v3_mul(t, q.w)), v3_cross(u, t));
}


// Поворот на малый вектор: q + ½·(θ, 0)·q
static V4 qturn(V4 q, V3 th)
{
    V4 d = qmul((V4){ th.x, th.y, th.z, 0.0f }, q);
    return qnorm((V4){ q.x + 0.5f * d.x, q.y + 0.5f * d.y, q.z + 0.5f * d.z, q.w + 0.5f * d.w });
}

static V4 q_from_m4(M4 m)
{
    // Столбцы матрицы — оси вещи; масштаб отбрасывается
    V3 c0 = v3_norm(v3(m.m[0], m.m[1], m.m[2])), c1 = v3_norm(v3(m.m[4], m.m[5], m.m[6])), c2 = v3_cross(c0, c1);
    c1 = v3_cross(c2, c0);
    f32 tr = c0.x + c1.y + c2.z;
    V4 q;
    if (tr > 0.0f) {
        f32 s = sqrtf(tr + 1.0f) * 2.0f;
        q = (V4){ (c1.z - c2.y) / s, (c2.x - c0.z) / s, (c0.y - c1.x) / s, 0.25f * s };
    } else if (c0.x > c1.y && c0.x > c2.z) {
        f32 s = sqrtf(1.0f + c0.x - c1.y - c2.z) * 2.0f;
        q = (V4){ 0.25f * s, (c1.x + c0.y) / s, (c2.x + c0.z) / s, (c1.z - c2.y) / s };
    } else if (c1.y > c2.z) {
        f32 s = sqrtf(1.0f + c1.y - c0.x - c2.z) * 2.0f;
        q = (V4){ (c1.x + c0.y) / s, 0.25f * s, (c2.y + c1.z) / s, (c2.x - c0.z) / s };
    } else {
        f32 s = sqrtf(1.0f + c2.z - c0.x - c1.y) * 2.0f;
        q = (V4){ (c2.x + c0.z) / s, (c2.y + c1.z) / s, 0.25f * s, (c0.y - c1.x) / s };
    }
    return qnorm(q);
}

V4 phys_quat(M4 m) { return q_from_m4(m); }

// atan2 многочленом, без libm (шаг детерминирован на любой машине): ошибка ~1e-5 рад
static f32 fatan2(f32 y, f32 x)
{
    f32 ax = fabsf(x), ay = fabsf(y), mx = fmaxf(ax, ay), mn = fminf(ax, ay);
    if (mx < 1e-20f) return 0.0f;
    f32 a = mn / mx, s = a * a;
    f32 r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0.0f) r = 3.14159274f - r;
    return y < 0.0f ? -r : r;
}

static M4 m4_from_q(V4 q, V3 t)
{
    V3 a = qrot(q, v3(1, 0, 0)), b = qrot(q, v3(0, 1, 0)), c = qrot(q, v3(0, 0, 1));
    return (M4){ { a.x, a.y, a.z, 0, b.x, b.y, b.z, 0, c.x, c.y, c.z, 0, t.x, t.y, t.z, 1 } };
}

// ---------------------------------------------------------------- тела

void phys_init(Phys *p, Arena *perm)
{
    memset(p, 0, sizeof *p);
    u32 n = PHYS_MAX;
    p->x = ARENA_PUSH_ARRAY(perm, V3, n);
    p->v = ARENA_PUSH_ARRAY(perm, V3, n);
    p->w = ARENA_PUSH_ARRAY(perm, V3, n);
    p->x_tick = ARENA_PUSH_ARRAY(perm, V3, n);
    p->q = ARENA_PUSH_ARRAY(perm, V4, n);
    p->q_tick = ARENA_PUSH_ARRAY(perm, V4, n);
    p->com = ARENA_PUSH_ARRAY(perm, V3, n);
    p->inv_i = ARENA_PUSH_ARRAY(perm, V3, n);
    p->inv_m = ARENA_PUSH_ARRAY(perm, f32, n);
    p->bound = ARENA_PUSH_ARRAY(perm, f32, n);
    p->sleep_t = ARENA_PUSH_ARRAY(perm, f32, n);
    p->out_t = ARENA_PUSH_ARRAY(perm, f32, n);
    p->friction = ARENA_PUSH_ARRAY(perm, f32, n);
    p->bounce = ARENA_PUSH_ARRAY(perm, f32, n);
    p->sph = ARENA_PUSH_ARRAY(perm, V4, (size_t)n * PHYS_SPHERES);
    p->ns = ARENA_PUSH_ARRAY(perm, u8, n);
    p->state = ARENA_PUSH_ARRAY(perm, u8, n);
    p->mode = ARENA_PUSH_ARRAY(perm, u8, n);
    p->settled = ARENA_PUSH_ARRAY(perm, u8, n);
    p->wake = ARENA_PUSH_ARRAY(perm, u8, n);
    p->touch = ARENA_PUSH_ARRAY(perm, u8, n);
    p->user = ARENA_PUSH_ARRAY(perm, u16, n);
    p->group = ARENA_PUSH_ARRAY(perm, u16, n);
    p->ja = ARENA_PUSH_ARRAY(perm, u16, PHYS_JOINTS);
    p->jb = ARENA_PUSH_ARRAY(perm, u16, PHYS_JOINTS);
    p->jkind = ARENA_PUSH_ARRAY(perm, u8, PHYS_JOINTS);
    p->jra = ARENA_PUSH_ARRAY(perm, V3, PHYS_JOINTS);
    p->jrb = ARENA_PUSH_ARRAY(perm, V3, PHYS_JOINTS);
    p->jqa = ARENA_PUSH_ARRAY(perm, V4, PHYS_JOINTS);
    p->jqb = ARENA_PUSH_ARRAY(perm, V4, PHYS_JOINTS);
    p->jlim = ARENA_PUSH_ARRAY(perm, V4, PHYS_JOINTS);
    p->zone_ix = ARENA_PUSH_ARRAY(perm, u32, n);
    p->zone_x = ARENA_PUSH_ARRAY(perm, f32, n);
    p->zone_z = ARENA_PUSH_ARRAY(perm, f32, n);
    p->pair = ARENA_PUSH_ARRAY(perm, u32, PAIR_MAX);
    grid_init(&p->grid, perm, n, 1u << 16, 1.0f);
    p->on = true;
}

// Шары формы в осях вещи; вес шара — его объём
static u32 shape_spheres(const PhysDesc *d, V4 *out)
{
    u32 n = 0;
    f32 gap = d->gap > 0.0f ? d->gap : 1.5f;
    for (u32 k = 0; k < d->parts && n < PHYS_SPHERES; k++) {
        const PhysPart *pt = &d->part[k];
        u32 left = PHYS_SPHERES - n - (d->parts - 1 - k);  // хотя бы по шару на оставшиеся части
        if (pt->kind == PP_CAPSULE) {
            f32 len = v3_len(v3_sub(pt->b, pt->a));
            u32 m = (u32)(len / (pt->r * gap)) + 2;
            if (len < 1e-4f) m = 1;
            if (m > left) m = left;
            for (u32 i = 0; i < m; i++) {
                V3 c = m > 1 ? v3_lerp(pt->a, pt->b, (f32)i / (f32)(m - 1)) : pt->a;
                out[n++] = (V4){ c.x, c.y, c.z, pt->r };
            }
        } else {
            // Коробка — шары в углах; радиус — тонкая сторона, но не больше трети длинной, иначе куб станет шаром.
            // По тонкой оси углы сливаются (пластина — четыре шара)
            V3 h = pt->b;
            f32 r = fminf(fminf(h.x, fminf(h.y, h.z)), 0.35f * fmaxf(h.x, fmaxf(h.y, h.z)));
            V3 e = v3(h.x - r, h.y - r, h.z - r);
            u32 nx = e.x > 1e-4f ? 2 : 1, ny = e.y > 1e-4f ? 2 : 1, nz = e.z > 1e-4f ? 2 : 1;
            for (u32 i = 0; i < nx * ny * nz && n < PHYS_SPHERES && left; i++, left--) {
                f32 sx = nx > 1 ? ((i & 1) ? 1.0f : -1.0f) : 0.0f;
                f32 sy = ny > 1 ? (((i / nx) & 1) ? 1.0f : -1.0f) : 0.0f;
                f32 sz = nz > 1 ? (((i / nx / ny) & 1) ? 1.0f : -1.0f) : 0.0f;
                out[n++] = (V4){ pt->a.x + sx * e.x, pt->a.y + sy * e.y, pt->a.z + sz * e.z, r };
            }
        }
    }
    return n;
}

i32 phys_spawn(Phys *p, const PhysDesc *d, M4 xf, V3 vel, V3 angvel, u8 mode, u16 user)
{
    u32 h = 0;
    while (h < p->count && p->state[h] != PS_FREE) h++;
    if (h == PHYS_MAX) return -1;
    if (h == p->count) p->count++;
    V4 *s = p->sph + (size_t)h * PHYS_SPHERES;
    u32 n = shape_spheres(d, s);
    if (!n) s[n++] = (V4){ 0, 0, 0, 0.05f };
    // Центр масс и моменты: шары как сплошные, масса по объёму
    f32 vol = 0.0f;
    V3 c = v3(0, 0, 0);
    for (u32 i = 0; i < n; i++) {
        f32 k = s[i].w * s[i].w * s[i].w;
        vol += k;
        c = v3_add(c, v3_mul(v3(s[i].x, s[i].y, s[i].z), k));
    }
    c = v3_mul(c, 1.0f / vol);
    f32 mass = fmaxf(d->mass, 0.05f);
    V3 inertia = v3(0, 0, 0);
    f32 bound = 0.0f;
    for (u32 i = 0; i < n; i++) {
        V3 r = v3_sub(v3(s[i].x, s[i].y, s[i].z), c);
        s[i] = (V4){ r.x, r.y, r.z, s[i].w };
        f32 m = mass * s[i].w * s[i].w * s[i].w / vol, own = 0.4f * m * s[i].w * s[i].w;
        inertia = v3_add(inertia, v3(m * (r.y * r.y + r.z * r.z) + own, m * (r.x * r.x + r.z * r.z) + own,
                                     m * (r.x * r.x + r.y * r.y) + own));
        bound = fmaxf(bound, v3_len(r) + s[i].w);
    }
    // Тонкий прут вокруг своей оси не крутится как волчок: момент не меньше, чем у прута 4.5 см
    f32 lo = mass * 0.002f;
    p->inv_i[h] = v3(1.0f / fmaxf(inertia.x, lo), 1.0f / fmaxf(inertia.y, lo), 1.0f / fmaxf(inertia.z, lo));
    p->ns[h] = (u8)n;
    p->com[h] = c;
    p->q[h] = p->q_tick[h] = q_from_m4(xf);
    p->x[h] = p->x_tick[h] = m4_point(xf, c);
    p->v[h] = vel;
    p->w[h] = angvel;
    p->inv_m[h] = mode == PHYS_FIXED ? 0.0f : 1.0f / mass;
    if (mode == PHYS_FIXED) p->inv_i[h] = v3(0, 0, 0);
    p->bound[h] = bound;
    p->friction[h] = d->friction;
    p->bounce[h] = d->bounce;
    p->mode[h] = mode;
    p->user[h] = user;
    p->group[h] = 0;
    p->sleep_t[h] = p->out_t[h] = 0.0f;
    p->wake[h] = 0;
    // Новое тело ждёт зону: застывшим; если оно уже в зоне, первый же шаг решит, что с ним
    p->settled[h] = mode != PHYS_SETTLE;
    p->state[h] = PS_FROZEN;
    // Брошенное на ходу сразу считается
    if (v3_dot(vel, vel) + v3_dot(angvel, angvel) > 0.0f && mode != PHYS_FIXED) p->state[h] = PS_ACTIVE;
    return (i32)h;
}

void phys_free(Phys *p, u32 h)
{
    if (h >= p->count) return;
    // Суставы с ним уходят вместе с телом
    for (u32 j = 0; j < p->jcount; j++)
        if (p->jkind[j] != PJ_FREE && (p->ja[j] == h || p->jb[j] == h)) p->jkind[j] = PJ_FREE;
    while (p->jcount && p->jkind[p->jcount - 1] == PJ_FREE) p->jcount--;
    p->group[h] = 0;
    p->state[h] = PS_FREE;
    while (p->count && p->state[p->count - 1] == PS_FREE) p->count--;
}

i32 phys_joint(Phys *p, const PhysJointDesc *d)
{
    u32 j = 0;
    while (j < p->jcount && p->jkind[j] != PJ_FREE) j++;
    if (j == PHYS_JOINTS || d->a >= p->count || d->b >= p->count) return -1;
    if (j == p->jcount) p->jcount++;
    p->ja[j] = (u16)d->a;
    p->jb[j] = (u16)d->b;
    p->jkind[j] = d->kind;
    p->jra[j] = qrot(qconj(p->q[d->a]), v3_sub(d->at, p->x[d->a]));
    p->jrb[j] = qrot(qconj(p->q[d->b]), v3_sub(d->at, p->x[d->b]));
    p->jqa[j] = qnorm(d->fa);
    p->jqb[j] = qnorm(d->fb);
    p->jlim[j] = (V4){ d->lo, d->hi, d->cone, 0.0f };
    return (i32)j;
}

void phys_joint_free(Phys *p, u32 j)
{
    if (j >= p->jcount) return;
    p->jkind[j] = PJ_FREE;
    while (p->jcount && p->jkind[p->jcount - 1] == PJ_FREE) p->jcount--;
}

M4 phys_pose(const Phys *p, u32 h, f32 alpha)
{
    V4 qa = p->q_tick[h], qb = p->q[h];
    // Короткий путь между кватернионами
    if (qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w < 0.0f) qb = (V4){ -qb.x, -qb.y, -qb.z, -qb.w };
    V4 q = qnorm((V4){ lerpf(qa.x, qb.x, alpha), lerpf(qa.y, qb.y, alpha), lerpf(qa.z, qb.z, alpha), lerpf(qa.w, qb.w, alpha) });
    V3 x = v3_lerp(p->x_tick[h], p->x[h], alpha);
    return m4_from_q(q, v3_sub(x, qrot(q, p->com[h])));
}

void phys_wake(Phys *p, V3 c, f32 r)
{
    for (u32 h = 0; h < p->count; h++) {
        if (p->state[h] != PS_ASLEEP || p->mode[h] == PHYS_FIXED) continue;
        V3 d = v3_sub(p->x[h], c);
        f32 rr = r + p->bound[h];
        if (v3_dot(d, d) < rr * rr) p->wake[h] = 1;
    }
}

void phys_zone(Phys *p, const V3 *pts, u32 n)
{
    p->npts = MIN(n, PHYS_ZONE_PTS);
    for (u32 i = 0; i < p->npts; i++) p->pts[i] = pts[i];
}

// ---------------------------------------------------------------- шаг

// Обратный момент инерции в осях мира (симметричная 3×3: xx yy zz xy xz yz), на подшаг: поворот за подшаг мал,
// а без неё каждый толчок крутил бы кватернион туда и обратно
typedef struct InvI {
    f32 xx, yy, zz, xy, xz, yz;
} InvI;

static InvI g_iw[PHYS_MAX];

static void world_inv_i(const Phys *p, u32 h)
{
    V3 a = qrot(p->q[h], v3(1, 0, 0)), b = qrot(p->q[h], v3(0, 1, 0)), c = qrot(p->q[h], v3(0, 0, 1)), d = p->inv_i[h];
    // R·diag(d)·Rᵀ: столбцы R — оси вещи в мире
    g_iw[h] = (InvI){ d.x * a.x * a.x + d.y * b.x * b.x + d.z * c.x * c.x, d.x * a.y * a.y + d.y * b.y * b.y + d.z * c.y * c.y,
                      d.x * a.z * a.z + d.y * b.z * b.z + d.z * c.z * c.z, d.x * a.x * a.y + d.y * b.x * b.y + d.z * c.x * c.y,
                      d.x * a.x * a.z + d.y * b.x * b.z + d.z * c.x * c.z, d.x * a.y * a.z + d.y * b.y * b.z + d.z * c.y * c.z };
}

static V3 iw_mul(u32 h, V3 v)
{
    const InvI *m = &g_iw[h];
    return v3(m->xx * v.x + m->xy * v.y + m->xz * v.z, m->xy * v.x + m->yy * v.y + m->yz * v.z,
              m->xz * v.x + m->yz * v.y + m->zz * v.z);
}

// Обобщённая обратная масса точки r (от центра масс, мир) вдоль n
static f32 gen_w(const Phys *p, u32 h, V3 r, V3 n)
{
    if (p->inv_m[h] == 0.0f || p->state[h] != PS_ACTIVE) return 0.0f;
    V3 u = v3_cross(r, n);
    return p->inv_m[h] + v3_dot(u, iw_mul(h, u));
}

// Сдвиг позиции на импульс положения j в точке r
static void push(Phys *p, u32 h, V3 r, V3 j)
{
    if (p->inv_m[h] == 0.0f || p->state[h] != PS_ACTIVE) return;
    p->x[h] = v3_add(p->x[h], v3_mul(j, p->inv_m[h]));
    p->q[h] = qturn(p->q[h], iw_mul(h, v3_cross(r, j)));
}

// Скорость точки r
static V3 point_v(const Phys *p, u32 h, V3 r) { return v3_add(p->v[h], v3_cross(p->w[h], r)); }

static void push_v(Phys *p, u32 h, V3 r, V3 j)
{
    if (p->inv_m[h] == 0.0f || p->state[h] != PS_ACTIVE) return;
    p->v[h] = v3_add(p->v[h], v3_mul(j, p->inv_m[h]));
    p->w[h] = v3_add(p->w[h], iw_mul(h, v3_cross(r, j)));
}

typedef struct Plane {
    V3  n;
    f32 d;  // точка p под землёй, если dot(n, p) < d
} Plane;

// Земля под телом — плоскость по высоте в центре и наклону вокруг. Один замер на шаг, а не на шар
static Plane ground_plane(PhysGround g, V3 c)
{
    f32 h = g(c.x, c.z);
    f32 gx = (g(c.x + GROUND_EPS, c.z) - h) / GROUND_EPS, gz = (g(c.x, c.z + GROUND_EPS) - h) / GROUND_EPS;
    V3 n = v3_norm(v3(-gx, 1.0f, -gz));
    return (Plane){ n, v3_dot(n, v3(c.x, h, c.z)) };
}

static bool in_zone(const Phys *p, V3 x, f32 r)
{
    for (u32 i = 0; i < p->npts; i++) {
        f32 dx = x.x - p->pts[i].x, dz = x.z - p->pts[i].z;
        if (dx * dx + dz * dz < r * r) return true;
    }
    return false;
}

// Зона: застывшие в зоне просыпаются (осесть — считается, остальные спят), спящие вне зоны застывают,
// считающиеся вне зоны досчитываются не дольше OUT_S
static void zone_pass(Phys *p, f32 dt)
{
    for (u32 h = 0; h < p->count; h++) {
        u8 s = p->state[h];
        if (s == PS_FREE) continue;
        // Намертво: в зоне — стена для остальных, вне зоны — ничто
        if (p->mode[h] == PHYS_FIXED) {
            p->state[h] = in_zone(p, p->x[h], PHYS_ZONE_R) ? PS_ASLEEP : PS_FROZEN;
            continue;
        }
        if (s == PS_FROZEN) {
            if (!in_zone(p, p->x[h], PHYS_ZONE_R)) continue;
            bool fall = p->mode[h] == PHYS_SETTLE && !p->settled[h];
            p->state[h] = fall ? PS_ACTIVE : PS_ASLEEP;
            p->sleep_t[h] = p->out_t[h] = 0.0f;
        } else if (!in_zone(p, p->x[h], PHYS_ZONE_R + PHYS_ZONE_OUT)) {
            if (s == PS_ASLEEP) p->state[h] = PS_FROZEN;
            else if ((p->out_t[h] += dt) >= OUT_S) {
                p->state[h] = PS_FROZEN;
                p->v[h] = p->w[h] = v3(0, 0, 0);
            }
        } else
            p->out_t[h] = 0.0f;
    }
}

// Пары тел, чьи сферы охвата пересекаются; хотя бы одно считается
static void find_pairs(Phys *p, f32 dt)
{
    u32 nz = 0;
    for (u32 h = 0; h < p->count; h++) {
        u8 s = p->state[h];
        if (s != PS_ACTIVE && s != PS_ASLEEP) continue;
        p->zone_ix[nz] = h;
        p->zone_x[nz] = p->x[h].x;
        p->zone_z[nz] = p->x[h].z;
        nz++;
    }
    p->npair = 0;
    if (nz < 2) return;
    grid_build(&p->grid, p->zone_x, p->zone_z, NULL, 1, nz);
    u32 near[256];
    for (u32 k = 0; k < nz; k++) {
        u32 a = p->zone_ix[k];
        if (p->state[a] != PS_ACTIVE) continue;
        // Запас на путь за шаг
        f32 reach = p->bound[a] + 1.0f + v3_len(p->v[a]) * dt;
        u32 nn = grid_query(&p->grid, p->x[a].x, p->x[a].z, reach, GRID_ALL_TAGS, near, 256);
        for (u32 q = 0; q < nn; q++) {
            u32 b = p->zone_ix[near[q]];
            if (b == a || (p->state[b] == PS_ACTIVE && b < a)) continue;
            if (p->group[a] && p->group[a] == p->group[b]) continue;  // части одного тела держат суставы
            V3 d = v3_sub(p->x[a], p->x[b]);
            f32 r = p->bound[a] + p->bound[b] + v3_len(p->v[a]) * dt + v3_len(p->v[b]) * dt;
            if (v3_dot(d, d) >= r * r) continue;
            if (p->npair == PAIR_MAX) return;
            p->pair[p->npair++] = (MIN(a, b) << 16) | MAX(a, b);
        }
    }
}

typedef struct Contact {
    V3  r, n;
    f32 lam, vn0;  // толчок положения по нормали (м·кг), нормальная скорость до шага
} Contact;

// Подшаг одного тела о землю: позиции. Касания — в c (для скоростей)
static u32 ground_contacts(Phys *p, u32 h, Plane pl, const V3 *xp, const V4 *qp, const V3 *wr, f32 sh, Contact *c)
{
    // Всё тело выше земли — шары не проверяются
    if (v3_dot(pl.n, p->x[h]) - p->bound[h] > pl.d) return 0;
    u32 nc = 0;
    const V4 *s = p->sph + (size_t)h * PHYS_SPHERES;
    for (u32 i = 0; i < p->ns[h]; i++) {
        // Сначала по смещению с начала подшага: касание редко, точный поворот — только для него
        if (pl.d - (v3_dot(pl.n, v3_add(p->x[h], wr[i])) - s[i].w) <= -0.01f) continue;
        V3 r = qrot(p->q[h], v3(s[i].x, s[i].y, s[i].z));
        V3 wp = v3_add(p->x[h], r);
        f32 pen = pl.d - (v3_dot(pl.n, wp) - s[i].w);
        if (pen <= 0.0f) continue;
        // Вдавленность с прошлого подшага (вещь появилась в земле, земля под ней поднялась) выходит не быстрее
        // DEPEN_V: иначе вещь выстреливает вверх. Вдавленность от движения за подшаг снимается целиком
        V3 sl = v3(s[i].x, s[i].y, s[i].z);
        f32 was_in = fmaxf(pl.d - (v3_dot(pl.n, v3_add(*xp, qrot(*qp, sl))) - s[i].w), 0.0f);
        pen -= was_in - fminf(was_in, DEPEN_V * sh);
        if (pen <= 0.0f) continue;
        V3 rc = v3_sub(r, v3_mul(pl.n, s[i].w));  // точка касания на поверхности шара
        f32 wn = gen_w(p, h, rc, pl.n);
        if (wn <= 0.0f) continue;
        f32 vn0 = v3_dot(point_v(p, h, rc), pl.n);
        f32 lam = pen / wn;
        push(p, h, rc, v3_mul(pl.n, lam));
        // Трение покоя: точка не скользит, пока нужный для этого толчок меньше трения × толчок по нормали
        r = qrot(p->q[h], v3(s[i].x, s[i].y, s[i].z));
        rc = v3_sub(r, v3_mul(pl.n, s[i].w));
        // Сдвиг точки за подшаг: перенос центра плюс малый поворот от прошлой позы
        V4 dq = qmul(p->q[h], (V4){ -qp->x, -qp->y, -qp->z, qp->w });
        V3 th = v3_mul(qv(dq), dq.w < 0.0f ? -2.0f : 2.0f);
        V3 dp = v3_add(v3_sub(p->x[h], *xp), v3_cross(th, rc));
        V3 dt = v3_sub(dp, v3_mul(pl.n, v3_dot(dp, pl.n)));
        f32 lt = v3_len(dt);
        if (lt > 1e-7f) {
            V3 t = v3_mul(dt, 1.0f / lt);
            f32 wt = gen_w(p, h, rc, t);
            if (wt > 0.0f && lt / wt < p->friction[h] * lam) push(p, h, rc, v3_mul(t, -lt / wt));
        }
        if (nc < PHYS_SPHERES) c[nc++] = (Contact){ rc, pl.n, lam, vn0 };
    }
    return nc;
}

// Шары двух тел: выталкивание пополам по обобщённым массам. Спящее для считающегося — стена.
// Смещения шаров wr повёрнуты в начале подшага: за подшаг поворот мал
static void pair_contacts(Phys *p, u32 a, u32 b, const V3 *wr)
{
    V3 d = v3_sub(p->x[a], p->x[b]);
    f32 rr = p->bound[a] + p->bound[b];
    if (v3_dot(d, d) >= rr * rr) return;
    const V4 *sa = p->sph + (size_t)a * PHYS_SPHERES, *sb = p->sph + (size_t)b * PHYS_SPHERES;
    const V3 *ra = wr + (size_t)a * PHYS_SPHERES, *rb = wr + (size_t)b * PHYS_SPHERES;
    for (u32 i = 0; i < p->ns[a]; i++) {
        // Шар не достаёт до сферы охвата другого тела — его пары не проверяются
        V3 pa = v3_add(p->x[a], ra[i]), ob = v3_sub(pa, p->x[b]);
        f32 reach = p->bound[b] + sa[i].w;
        if (v3_dot(ob, ob) >= reach * reach) continue;
        for (u32 j = 0; j < p->ns[b]; j++) {
            V3 dd = v3_sub(pa, v3_add(p->x[b], rb[j]));
            f32 l2 = v3_dot(dd, dd), sr = sa[i].w + sb[j].w;
            if (l2 >= sr * sr) continue;
            f32 l = sqrtf(l2);
            V3 n = l > 1e-6f ? v3_mul(dd, 1.0f / l) : v3(0, 1, 0);
            f32 pen = sr - l;
            p->touch[a] = p->touch[b] = 1;
            pen = fminf(pen, PAIR_PUSH);
            if (p->state[b] == PS_ASLEEP && pen > WAKE_DEPTH) p->wake[b] = 1;
            if (p->state[a] == PS_ASLEEP && pen > WAKE_DEPTH) p->wake[a] = 1;
            V3 ca = v3_sub(ra[i], v3_mul(n, sa[i].w)), cb = v3_add(rb[j], v3_mul(n, sb[j].w));
            f32 wa = gen_w(p, a, ca, n), wb = gen_w(p, b, cb, n);
            if (wa + wb <= 0.0f) continue;
            f32 lam = pen / (wa + wb);
            push(p, a, ca, v3_mul(n, lam));
            push(p, b, cb, v3_mul(n, -lam));
        }
    }
}

// Скорости после позиций: отскок и трение скольжения о землю по касаниям подшага
static void ground_velocities(Phys *p, u32 h, const Contact *c, u32 nc, f32 sh)
{
    for (u32 k = 0; k < nc; k++) {
        V3 n = c[k].n, vr = point_v(p, h, c[k].r);
        f32 vn = v3_dot(vr, n);
        V3 vt = v3_sub(vr, v3_mul(n, vn));
        f32 lt = v3_len(vt);
        // Скольжение: трение гасит касательную скорость толчком не больше нормального × трение
        if (lt > 1e-6f) {
            V3 t = v3_mul(vt, 1.0f / lt);
            f32 wt = gen_w(p, h, c[k].r, t);
            if (wt > 0.0f) push_v(p, h, c[k].r, v3_mul(t, -fminf(lt / wt, p->friction[h] * c[k].lam / sh)));
        }
        // Отскок только от заметного удара: иначе дрожь на месте
        f32 e = fabsf(c[k].vn0) > 2.0f * GRAVITY * sh ? p->bounce[h] : 0.0f;
        f32 want = fmaxf(-e * c[k].vn0, 0.0f);
        vn = v3_dot(point_v(p, h, c[k].r), n);
        f32 wn = gen_w(p, h, c[k].r, n);
        if (wn > 0.0f && vn < want) push_v(p, h, c[k].r, v3_mul(n, (want - vn) / wn));
    }
}

static void turn_spheres(const Phys *p, u32 h, V3 *wr)
{
    const V4 *s = p->sph + (size_t)h * PHYS_SPHERES;
    for (u32 i = 0; i < p->ns[h]; i++) wr[(size_t)h * PHYS_SPHERES + i] = qrot(p->q[h], v3(s[i].x, s[i].y, s[i].z));
}

// ---------------------------------------------------------------- суставы

// Обобщённая обратная инерция тела по оси n (мир); застывшее и спящее — стена
static f32 ang_w(const Phys *p, u32 h, V3 n)
{
    if (p->inv_m[h] == 0.0f || p->state[h] != PS_ACTIVE) return 0.0f;
    return v3_dot(n, iw_mul(h, n));
}

// Повернуть b относительно a на угол th вокруг оси n (мир), поделив по инерции
static void rel_turn(Phys *p, u32 a, u32 b, V3 n, f32 th)
{
    f32 wa = ang_w(p, a, n), wb = ang_w(p, b, n);
    if (wa + wb <= 0.0f) return;
    V3 j = v3_mul(n, th / (wa + wb));
    if (wb > 0.0f) p->q[b] = qturn(p->q[b], iw_mul(b, j));
    if (wa > 0.0f) p->q[a] = qturn(p->q[a], iw_mul(a, v3_mul(j, -1.0f)));
}

static f32 wrap_pi(f32 a) { return a > 3.14159274f ? a - 6.28318548f : a < -3.14159274f ? a + 6.28318548f : a; }

// Сустав за проход подшага: сначала пределы поворота, потом общая точка (Müller и др., «Detailed Rigid Body
// Simulation with Extended Position Based Dynamics», 2020). Поворот b относительно a: R = (qa·fa)⁻¹·(qb·fb)
static void joint_solve(Phys *p, u32 j)
{
    u32 a = p->ja[j], b = p->jb[j];
    if (p->state[a] != PS_ACTIVE && p->state[b] != PS_ACTIVE) return;
    V4 lim = p->jlim[j];
    V4 fa = qmul(p->q[a], p->jqa[j]), fb = qmul(p->q[b], p->jqb[j]);
    if (p->jkind[j] == PJ_HINGE) {
        // Оси петли совпадают
        V3 za = qrot(fa, v3(0, 0, 1)), zb = qrot(fb, v3(0, 0, 1)), c = v3_cross(zb, za);
        f32 s = v3_len(c);
        if (s > 1e-6f) {
            rel_turn(p, a, b, v3_mul(c, 1.0f / s), fatan2(s, v3_dot(za, zb)));
            fa = qmul(p->q[a], p->jqa[j]);
            fb = qmul(p->q[b], p->jqb[j]);
        }
        V4 r = qmul(qconj(fa), fb);
        f32 phi = wrap_pi(2.0f * fatan2(r.z, r.w)), want = clampf(phi, lim.x, lim.y);
        if (want != phi) rel_turn(p, a, b, qrot(fa, v3(0, 0, 1)), want - phi);
    } else {
        // Разложение на отклонение (swing) и кручение вокруг X (twist): R = swing·twist
        V4 r = qmul(qconj(fa), fb);
        if (r.w < 0.0f) r = (V4){ -r.x, -r.y, -r.z, -r.w };
        f32 tl = sqrtf(r.x * r.x + r.w * r.w);
        V4 tw = tl > 1e-6f ? (V4){ r.x / tl, 0, 0, r.w / tl } : (V4){ 0, 0, 0, 1 };
        V4 sw = qmul(r, qconj(tw));  // sw.w = tl >= 0
        V3 sv = qv(sw);
        f32 sl = v3_len(sv), s = 2.0f * fatan2(sl, sw.w);
        if (s > lim.z && sl > 1e-6f) {
            rel_turn(p, a, b, qrot(fa, v3_mul(sv, 1.0f / sl)), lim.z - s);
            fb = qmul(p->q[b], p->jqb[j]);
        }
        f32 t = 2.0f * fatan2(tw.x, tw.w), want = clampf(t, lim.x, lim.y);
        if (want != t) rel_turn(p, a, b, qrot(fb, v3(1, 0, 0)), want - t);
    }
    // Общая точка
    V3 ra = qrot(p->q[a], p->jra[j]), rb = qrot(p->q[b], p->jrb[j]);
    V3 d = v3_sub(v3_add(p->x[b], rb), v3_add(p->x[a], ra));
    f32 c = v3_len(d);
    if (c < 1e-7f) return;
    V3 n = v3_mul(d, 1.0f / c);
    f32 wa = gen_w(p, a, ra, n), wb = gen_w(p, b, rb, n);
    if (wa + wb <= 0.0f) return;
    f32 lam = c / (wa + wb);
    push(p, a, ra, v3_mul(n, lam));
    push(p, b, rb, v3_mul(n, -lam));
}

// Трение в суставе: гасит разность угловых скоростей
static void joint_damp(Phys *p, u32 j, f32 sh)
{
    u32 a = p->ja[j], b = p->jb[j];
    V3 dw = v3_sub(p->w[b], p->w[a]);
    f32 l = v3_len(dw);
    if (l < 1e-6f) return;
    V3 n = v3_mul(dw, 1.0f / l);
    f32 wa = ang_w(p, a, n), wb = ang_w(p, b, n);
    if (wa + wb <= 0.0f) return;
    V3 L = v3_mul(n, l * fminf(1.0f, JOINT_DAMP * sh) / (wa + wb));
    if (wa > 0.0f) p->w[a] = v3_add(p->w[a], iw_mul(a, L));
    if (wb > 0.0f) p->w[b] = v3_sub(p->w[b], iw_mul(b, L));
}

static void substep(Phys *p, f32 sh, const Plane *pl, V3 *xp, V4 *qp, V3 *wr, Contact *cs, u32 *ncs)
{
    for (u32 h = 0; h < p->count; h++) {
        if (p->state[h] != PS_ACTIVE) continue;
        xp[h] = p->x[h];
        qp[h] = p->q[h];
        p->v[h].y -= GRAVITY * sh;
        p->x[h] = v3_add(p->x[h], v3_mul(p->v[h], sh));
        p->q[h] = qturn(p->q[h], v3_mul(p->w[h], sh));
        turn_spheres(p, h, wr);
        world_inv_i(p, h);
    }
    for (u32 it = 0; it < JOINT_ITERS; it++)
        for (u32 j = 0; j < p->jcount; j++)
            if (p->jkind[j] != PJ_FREE) joint_solve(p, j);
    for (u32 k = 0; k < p->npair; k++) pair_contacts(p, p->pair[k] >> 16, p->pair[k] & 0xFFFFu, wr);
    for (u32 h = 0; h < p->count; h++) {
        if (p->state[h] != PS_ACTIVE) continue;
        ncs[h] = ground_contacts(p, h, pl[h], &xp[h], &qp[h], wr + (size_t)h * PHYS_SPHERES, sh, cs + (size_t)h * PHYS_SPHERES);
    }
    for (u32 h = 0; h < p->count; h++) {
        if (p->state[h] != PS_ACTIVE) continue;
        p->v[h] = v3_mul(v3_sub(p->x[h], xp[h]), 1.0f / sh);
        V4 dq = qmul(p->q[h], (V4){ -qp[h].x, -qp[h].y, -qp[h].z, qp[h].w });
        p->w[h] = v3_mul(qv(dq), (dq.w < 0.0f ? -2.0f : 2.0f) / sh);
        ground_velocities(p, h, cs + (size_t)h * PHYS_SPHERES, ncs[h], sh);
        // Воздух и внутреннее трение: без него вещь долго раскачивается на ребре. На земле ещё сопротивление
        // качению: труба по ровному катится и останавливается, а не катится вечно
        f32 k = ncs[h] ? ROLL_DAMP : AIR_DAMP;
        p->w[h] = v3_mul(p->w[h], 1.0f / (1.0f + k * sh));
    }
    for (u32 j = 0; j < p->jcount; j++)
        if (p->jkind[j] != PJ_FREE) joint_damp(p, j, sh);
}

// Тела одной группы живут вместе: застыло одно — застывают все, считается одно — считаются все.
// Иначе сустав между считающимся и застывшим тянул бы живое к неподвижному
static void group_sync(Phys *p)
{
    static u8 fl[PHYS_GROUPS];
    memset(fl, 0, sizeof fl);
    for (u32 h = 0; h < p->count; h++) {
        u16 g = p->group[h];
        if (g && p->state[h] != PS_FREE) fl[g] |= p->state[h] == PS_FROZEN ? 1 : p->state[h] == PS_ACTIVE ? 2 : 0;
    }
    for (u32 h = 0; h < p->count; h++) {
        u16 g = p->group[h];
        if (!g || p->state[h] == PS_FREE) continue;
        if (fl[g] & 1) {
            if (p->state[h] != PS_FROZEN) p->v[h] = p->w[h] = v3(0, 0, 0);
            p->state[h] = PS_FROZEN;
        } else if ((fl[g] & 2) && p->state[h] == PS_ASLEEP) {
            p->state[h] = PS_ACTIVE;
            p->sleep_t[h] = 0.0f;
        }
    }
}

static void put_asleep(Phys *p, u32 h)
{
    p->state[h] = PS_ASLEEP;
    p->settled[h] = 1;
    p->v[h] = p->w[h] = v3(0, 0, 0);
}

void phys_tick(Phys *p, f32 dt, PhysGround ground)
{
    f64 t0 = time_now();
    memcpy(p->x_tick, p->x, p->count * sizeof *p->x);
    memcpy(p->q_tick, p->q, p->count * sizeof *p->q);
    if (!p->on) return;
    zone_pass(p, dt);
    group_sync(p);
    // Сверх предела считающихся засыпают самые медленные; тело из группы — всей группой
    u32 na = 0;
    for (u32 h = 0; h < p->count; h++) na += p->state[h] == PS_ACTIVE;
    while (na > PHYS_ACTIVE_MAX) {
        u32 slow = 0;
        f32 best = 1e30f;
        for (u32 h = 0; h < p->count; h++) {
            f32 e = v3_dot(p->v[h], p->v[h]);
            if (p->state[h] == PS_ACTIVE && e < best) best = e, slow = h;
        }
        u16 g = p->group[slow];
        for (u32 h = 0; h < p->count; h++) {
            if (h != slow && (!g || p->group[h] != g || p->state[h] != PS_ACTIVE)) continue;
            p->state[h] = PS_ASLEEP;
            p->v[h] = p->w[h] = v3(0, 0, 0);
            na--;
        }
    }
    find_pairs(p, dt);
    memset(p->touch, 0, p->count);
    // Рабочие массивы на стеке шага: по одному на тело
    static V3 xp[PHYS_MAX];
    static V4 qp[PHYS_MAX];
    static Plane pl[PHYS_MAX];
    static Contact cs[PHYS_MAX * PHYS_SPHERES];
    static u32 ncs[PHYS_MAX];
    static V3 wr[PHYS_MAX * PHYS_SPHERES];
    for (u32 h = 0; h < p->count; h++) {
        if (p->state[h] == PS_ACTIVE) pl[h] = ground_plane(ground, p->x[h]);
        if (p->state[h] == PS_ASLEEP) turn_spheres(p, h, wr);
        ncs[h] = 0;
    }
    u32 nsub = (u32)(dt * SUBSTEP_HZ + 0.5f);
    if (nsub < 1) nsub = 1;
    f32 sh = dt / (f32)nsub;
    for (u32 k = 0; k < nsub; k++) substep(p, sh, pl, xp, qp, wr, cs, ncs);
    // Сон и пробуждение
    p->st = (PhysStats){ .pairs = p->npair };
    for (u32 h = 0; h < p->count; h++) {
        if (p->wake[h]) {
            p->wake[h] = 0;
            if (p->state[h] == PS_ASLEEP) {
                p->state[h] = PS_ACTIVE;
                p->sleep_t[h] = 0.0f;
            }
        }
        if (p->state[h] == PS_ACTIVE) {
            bool still = v3_dot(p->v[h], p->v[h]) < SLEEP_V * SLEEP_V && v3_dot(p->w[h], p->w[h]) < SLEEP_W * SLEEP_W;
            // В воздухе не засыпает: лежать надо на чём-то. Часть тела может висеть на суставе (голова над землёй)
            p->sleep_t[h] = still && (ncs[h] || p->touch[h] || p->group[h]) ? p->sleep_t[h] + dt : 0.0f;
        }
    }
    // Группа засыпает, только когда замерли все её тела
    static u8 busy[PHYS_GROUPS];
    memset(busy, 0, sizeof busy);
    for (u32 h = 0; h < p->count; h++)
        if (p->group[h] && p->state[h] == PS_ACTIVE && p->sleep_t[h] < SLEEP_S) busy[p->group[h]] = 1;
    for (u32 h = 0; h < p->count; h++) {
        if (p->state[h] == PS_ACTIVE && (p->group[h] ? !busy[p->group[h]] : p->sleep_t[h] >= SLEEP_S)) put_asleep(p, h);
        p->st.active += p->state[h] == PS_ACTIVE;
        p->st.asleep += p->state[h] == PS_ASLEEP;
        p->st.frozen += p->state[h] == PS_FROZEN;
    }
    p->st.ms = (f32)((time_now() - t0) * 1000.0);
}

void phys_ser(Ser *s, Phys *p)
{
    ser_count(s, &p->count, PHYS_MAX);
    if (s->bad) return;
    u32 n = p->count;
    SER_N(s, p->x, n);
    SER_N(s, p->v, n);
    SER_N(s, p->w, n);
    SER_N(s, p->x_tick, n);
    SER_N(s, p->q, n);
    SER_N(s, p->q_tick, n);
    SER_N(s, p->com, n);
    SER_N(s, p->inv_i, n);
    SER_N(s, p->inv_m, n);
    SER_N(s, p->bound, n);
    SER_N(s, p->sleep_t, n);
    SER_N(s, p->out_t, n);
    SER_N(s, p->friction, n);
    SER_N(s, p->bounce, n);
    SER_N(s, p->sph, (size_t)n * PHYS_SPHERES);
    SER_N(s, p->ns, n);
    SER_N(s, p->state, n);
    SER_N(s, p->mode, n);
    SER_N(s, p->settled, n);
    SER_N(s, p->user, n);
    SER_N(s, p->group, n);
    ser_count(s, &p->jcount, PHYS_JOINTS);
    if (s->bad) return;
    u32 nj = p->jcount;
    SER_N(s, p->ja, nj);
    SER_N(s, p->jb, nj);
    SER_N(s, p->jkind, nj);
    SER_N(s, p->jra, nj);
    SER_N(s, p->jrb, nj);
    SER_N(s, p->jqa, nj);
    SER_N(s, p->jqb, nj);
    SER_N(s, p->jlim, nj);
    SER(s, p->on);
    for (u32 j = 0; j < nj && s->mode == SER_LOAD; j++)
        ser_check(s, p->jkind[j] <= PJ_HINGE && (p->jkind[j] == PJ_FREE || (p->ja[j] < n && p->jb[j] < n)));
    for (u32 h = 0; h < n && s->mode == SER_LOAD; h++) {
        ser_check(s, p->state[h] <= PS_FROZEN && p->ns[h] <= PHYS_SPHERES && p->group[h] < PHYS_GROUPS);
        p->wake[h] = 0;
    }
}
