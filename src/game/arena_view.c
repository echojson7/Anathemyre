#include "arena_view.h"

#include <string.h>

#define C_CONCRETE v3(0.42f, 0.40f, 0.37f)
#define C_CONCRETE_DARK v3(0.30f, 0.29f, 0.27f)
#define C_RUST v3(0.42f, 0.24f, 0.13f)
#define C_RUST_DARK v3(0.28f, 0.17f, 0.10f)
#define C_FLOOR v3(0.36f, 0.31f, 0.24f)
#define C_GLYPH v3(0.16f, 0.62f, 0.28f)
#define C_GLYPH_RED v3(0.95f, 0.18f, 0.10f)
#define C_LAMP v3(0.95f, 0.82f, 0.45f)

#define SLAB_R 8.6f     // вписанный радиус бетонной плиты
#define FLOOR_R 6.9f    // песчаный круг боя
#define FENCE_R 7.15f   // ограда: восьмиугольник, внутрь влезает круг ARENA_RADIUS
#define FLOOR_TOP 0.125f

static f32 hashf(u32 x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (f32)(x & 0xFFFFFF) / 16777215.0f;
}

static f32 wrap_angle(f32 a)
{
    while (a > PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

static M4 xf_box(V3 c, V3 size, f32 rx, f32 ry, f32 rz)
{
    return m4_mul(m4_translate(c), m4_mul(m4_rot_y(ry), m4_mul(m4_rot_x(rx), m4_mul(m4_rot_z(rz), m4_scale(size)))));
}

// Восьмиугольная плита из четырёх полос: их объединение и есть восьмиугольник, грани плоские
static void oct_slab(MeshBuilder *mb, V3 c, f32 r_in, f32 y0, f32 y1)
{
    f32 side = 2.0f * r_in * tanf(PI_F / 8.0f);
    for (int k = 0; k < 4; k++)
        mb_box(mb, xf_box(v3(c.x, 0.5f * (y0 + y1), c.z), v3(side, y1 - y0, 2.0f * r_in), 0, (f32)k * PI_F * 0.25f, 0));
}

// Угол восьмиугольника с вписанным радиусом r_in; k = 0..7, отсчёт от +Z к +X
static V3 oct_corner(V3 c, f32 r_in, int k, f32 y)
{
    f32 a = PI_F / 8.0f + (f32)k * PI_F * 0.25f;
    f32 rc = r_in / cosf(PI_F / 8.0f);
    return v3(c.x + sinf(a) * rc, y, c.z + cosf(a) * rc);
}

// Плоский знак на полу, светится снизу вверх
static void floor_glyph(MeshBuilder *mb, V3 c, V3 radial, f32 half_len, f32 half_w)
{
    V3 tangent = v3(-radial.z, 0.0f, radial.x);
    mb_quad(mb, c, v3_mul(tangent, half_len), v3_mul(radial, half_w));
}

void arena_view_build(MeshBuilder *mb, V3 center)
{
    // Плита и песок
    mb_set(mb, C_CONCRETE, MAT_CONCRETE, BONE_NONE);
    oct_slab(mb, center, SLAB_R, -0.3f, 0.12f);
    mb_set(mb, C_FLOOR, MAT_SAND, BONE_NONE);
    oct_slab(mb, center, FLOOR_R, 0.0f, FLOOR_TOP);

    // Круг знаков на песке: им размечено место боя, смысла никто не помнит
    mb_set(mb, C_GLYPH, MAT_GLYPH, BONE_NONE);
    for (int k = 0; k < 28; k++) {
        f32 a = (f32)k * 2.0f * PI_F / 28.0f;
        V3 radial = v3(sinf(a), 0.0f, cosf(a));
        floor_glyph(mb, v3(center.x + radial.x * 6.1f, FLOOR_TOP + 0.004f, center.z + radial.z * 6.1f), radial, 0.55f, 0.16f);
    }

    // Ограда: стойки из двутавра по углам, три трубы, частые прутья арматуры.
    // Сторона к залу (−Z) открыта: ворота с перекладиной
    const int gate = 4;  // сторона между углами 3 и 4 смотрит на −Z
    for (int k = 0; k < 8; k++) {
        V3 p = oct_corner(center, FENCE_R, k, 0.0f);
        f32 a = PI_F / 8.0f + (f32)k * PI_F * 0.25f;
        bool gate_post = k == gate - 1 || k == gate;
        f32 h = gate_post ? 3.1f : 2.3f;
        mb_set(mb, C_RUST_DARK, MAT_RUST, BONE_NONE);
        mb_box(mb, xf_box(v3(p.x, h * 0.5f, p.z), v3(0.22f, h, 0.06f), 0, a, 0));
        mb_box(mb, xf_box(v3(p.x, h * 0.5f, p.z), v3(0.05f, h, 0.2f), 0, a, 0));
        mb_set(mb, C_CONCRETE_DARK, MAT_CONCRETE, BONE_NONE);
        mb_box(mb, xf_box(v3(p.x, 0.25f, p.z), v3(0.5f, 0.3f, 0.5f), 0, a, 0));
    }
    for (int k = 0; k < 8; k++) {
        int k1 = (k + 1) % 8;
        if (k1 == gate) continue;
        V3 a = oct_corner(center, FENCE_R, k, 0.0f), b = oct_corner(center, FENCE_R, k1, 0.0f);
        mb_set(mb, C_RUST, MAT_RUST, BONE_NONE);
        static const f32 rails[3] = { 0.55f, 1.15f, 1.85f };
        for (int r = 0; r < 3; r++)
            mb_cylinder(mb, v3(a.x, rails[r], a.z), v3(b.x, rails[r] - (r == 2 ? 0.06f : 0.0f), b.z), 0.04f, 8);
        mb_set(mb, C_RUST_DARK, MAT_RUST, BONE_NONE);
        for (int i = 1; i < 12; i++) {
            f32 t = (f32)i / 12.0f;
            V3 q = v3_lerp(a, b, t);
            // Прутья кривые и разной высоты: ограду латали чем было
            f32 top = 1.85f + 0.25f * hashf((u32)(k * 31 + i));
            f32 lean = (hashf((u32)(k * 57 + i)) - 0.5f) * 0.12f;
            mb_cylinder(mb, v3(q.x, 0.12f, q.z), v3(q.x + lean, top, q.z + lean * 0.5f), 0.016f, 6);
        }
    }
    {
        // Перекладина ворот и знак над ними
        V3 a = oct_corner(center, FENCE_R, gate - 1, 2.95f), b = oct_corner(center, FENCE_R, gate, 2.95f);
        mb_set(mb, C_RUST_DARK, MAT_RUST, BONE_NONE);
        mb_cylinder(mb, a, b, 0.09f, 10);
        V3 m = v3_lerp(a, b, 0.5f);
        mb_set(mb, C_CONCRETE_DARK, MAT_CONCRETE, BONE_NONE);
        mb_box(mb, xf_box(v3(m.x, 2.45f, m.z - 0.02f), v3(1.6f, 0.75f, 0.08f), 0, 0, 0));
        mb_set(mb, C_GLYPH_RED, MAT_GLYPH, BONE_NONE);
        mb_quad(mb, v3(m.x, 2.45f, m.z - 0.07f), v3(-0.7f, 0, 0), v3(0, 0.3f, 0));
    }

    // Прожекторные мачты по диагонали: свет болезненно-жёлтый
    for (int side = 0; side < 2; side++) {
        f32 a = PI_F * (side ? 1.375f : 0.375f);
        V3 dir = v3(sinf(a), 0.0f, cosf(a));
        V3 base = v3(center.x + dir.x * 10.2f, 0.0f, center.z + dir.z * 10.2f);
        mb_set(mb, C_RUST_DARK, MAT_RUST, BONE_NONE);
        mb_box(mb, xf_box(v3(base.x, 5.2f, base.z), v3(0.32f, 10.4f, 0.32f), 0, a, 0));
        for (int i = 0; i < 6; i++)
            mb_box(mb, xf_box(v3(base.x, 1.2f + (f32)i * 1.6f, base.z), v3(0.5f, 0.06f, 0.5f), 0, a, 0));
        mb_set(mb, C_CONCRETE_DARK, MAT_CONCRETE, BONE_NONE);
        mb_box(mb, xf_box(v3(base.x, 0.3f, base.z), v3(1.1f, 0.6f, 1.1f), 0, a, 0));
        // Корпус фонаря смотрит в центр и вниз
        V3 lamp = v3(base.x - dir.x * 0.5f, 10.2f, base.z - dir.z * 0.5f);
        f32 yaw = atan2f(-dir.x, -dir.z);
        mb_set(mb, v3(0.16f, 0.16f, 0.17f), MAT_METAL, BONE_NONE);
        mb_box(mb, xf_box(lamp, v3(1.2f, 0.7f, 0.5f), 0.55f, yaw, 0));
        mb_set(mb, C_LAMP, MAT_EMISSIVE, BONE_NONE);
        V3 fwd = v3(sinf(yaw), 0.0f, cosf(yaw));
        V3 lens = v3_add(lamp, v3_add(v3_mul(fwd, 0.22f), v3(0, -0.14f, 0)));
        V3 right = v3(cosf(yaw), 0.0f, -sinf(yaw));
        V3 up = v3_norm(v3_add(v3(0, 1, 0), v3_mul(fwd, 0.6f)));
        mb_quad(mb, lens, v3_mul(right, -0.5f), v3_mul(up, 0.25f));
    }

    // Бетонные блоки, на которых стоят и сидят зрители
    mb_set(mb, C_CONCRETE_DARK, MAT_CONCRETE, BONE_NONE);
    for (int i = 0; i < 9; i++) {
        f32 a = 0.5f + (f32)i * 0.62f;
        if (fabsf(wrap_angle(a - PI_F)) < 0.6f) continue;  // проход к воротам
        f32 r = 9.4f + 0.5f * hashf((u32)i * 13u);
        mb_box(mb, xf_box(v3(center.x + sinf(a) * r, 0.25f, center.z + cosf(a) * r), v3(1.6f, 0.5f, 0.6f), 0, a, 0.03f));
    }
}

void arena_view_init(ArenaView *v, V3 center, f32 floor_y)
{
    memset(v, 0, sizeof *v);
    v->center = center;
    v->floor_y = floor_y;
    // Зрители полукругом за оградой, кроме прохода к воротам
    u32 n = 0;
    for (u32 k = 0; n < AV_SPECTATORS && k < 64; k++) {
        f32 a = 0.35f + (f32)k * 0.41f + 0.12f * hashf(k * 7u);
        if (fabsf(wrap_angle(a - PI_F)) < 0.55f) continue;
        f32 r = 8.0f + 0.5f * hashf(k * 11u + 3u);
        v->sx[n] = sinf(a) * r;
        v->sz[n] = cosf(a) * r;
        v->sh[n] = atan2f(-v->sx[n], -v->sz[n]) + (hashf(k * 5u) - 0.5f) * 0.5f;
        v->sseed[n] = 100.0f * hashf(k * 3u + 1u);
        n++;
    }
}

static void bout_started(ArenaView *v, const ArenaSession *s)
{
    const CombatWorld *w = &s->w;
    for (u32 i = 0; i < w->count && i < ARENA_MAX_BOUT; i++) {
        v->prev_x[i] = w->x[i];
        v->prev_z[i] = w->z[i];
        v->prev_h[i] = w->heading[i];
        v->phase[i] = hashf(i * 17u + s->bout);
        v->walk[i] = v->move_dir[i] = 0.0f;
        v->guard[i] = v->block[i] = v->dodge[i] = v->stagger[i] = 0.0f;
        v->down[i] = v->kneel[i] = v->limp[i] = v->crawl[i] = 0.0f;
        v->down_dir[i] = 1.0f;
        v->dropped[i] = false;
    }
}

static f32 approach(f32 x, f32 target, f32 rate, f32 dt)
{
    return x + (target - x) * fminf(1.0f, rate * dt);
}

// Доля пройденного состояния с учётом времени между тиками
static f32 state_progress(const CombatWorld *w, u32 i, f32 alpha)
{
    if (w->dur[i] <= 0.0f) return 1.0f;
    return clampf(1.0f - (w->timer[i] - alpha * COMBAT_DT) / w->dur[i], 0.0f, 1.0f);
}

void arena_view_update(ArenaView *v, ArenaSession *s, f32 dt)
{
    CombatWorld *w = &s->w;
    v->acc += dt;
    for (u32 steps = 0; v->acc >= COMBAT_DT && steps < 8; steps++) {
        v->acc -= COMBAT_DT;
        if (!v->in_bout) {
            // Все отдыхают: время идёт, пока кто-то не встанет
            for (u32 tries = 0; tries < 64 && !v->in_bout; tries++) v->in_bout = arena_bout_begin(s);
            if (v->in_bout) bout_started(v, s);
            continue;
        }
        for (u32 i = 0; i < w->count && i < ARENA_MAX_BOUT; i++) {
            v->prev_x[i] = w->x[i];
            v->prev_z[i] = w->z[i];
            v->prev_h[i] = w->heading[i];
        }
        if (arena_bout_tick(s)) {
            arena_bout_end(s);
            v->in_bout = false;
        }
    }
    if (v->acc > COMBAT_DT) v->acc = COMBAT_DT;  // кадр слишком долгий: не догонять
    if (!v->in_bout) return;

    f32 alpha = v->acc / COMBAT_DT;
    for (u32 i = 0; i < w->count && i < ARENA_MAX_BOUT; i++) {
        u8 st = w->state[i];
        // Шаг по смещению за тик
        f32 vx = (w->x[i] - v->prev_x[i]) / COMBAT_DT, vz = (w->z[i] - v->prev_z[i]) / COMBAT_DT;
        f32 speed = sqrtf(vx * vx + vz * vz);
        bool moving = speed > 0.15f && !fs_lying(st);
        v->walk[i] = approach(v->walk[i], moving ? clampf(speed / 1.4f, 0.4f, 1.0f) : 0.0f, 6.0f, dt);
        if (speed > 0.15f) v->move_dir[i] = wrap_angle(atan2f(vx, vz) - w->heading[i]);
        f32 stride = st == FS_CRAWL ? 0.5f : CHAR_STRIDE;
        v->phase[i] += (moving || st == FS_CRAWL ? speed : 0.0f) * dt / stride;
        v->phase[i] -= floorf(v->phase[i]);

        bool armed = !w->disarmed[i];
        bool fighting = st == FS_READY || st == FS_WINDUP || st == FS_STRIKE || st == FS_RECOVER || st == FS_BLOCK ||
                        st == FS_DODGE || st == FS_STAGGER;
        f32 guard_t = !armed ? 0.0f : (fighting ? 1.0f : (st == FS_RETREAT ? 0.6f : 0.0f));
        if (w->halted && st == FS_READY) guard_t = 0.35f;  // бой окончен: оружие опущено
        v->guard[i] = approach(v->guard[i], guard_t, 5.0f, dt);
        v->block[i] = approach(v->block[i], st == FS_BLOCK ? 1.0f : 0.0f, 14.0f, dt);
        v->dodge[i] = approach(v->dodge[i], st == FS_DODGE ? 1.0f : 0.0f, 10.0f, dt);
        v->stagger[i] = approach(v->stagger[i], st == FS_STAGGER ? 1.0f : 0.0f, 10.0f, dt);
        v->kneel[i] = approach(v->kneel[i], st == FS_SURRENDER ? 1.0f : 0.0f, 3.0f, dt);
        v->limp[i] = approach(v->limp[i], st == FS_UNCONSCIOUS || st == FS_DEAD ? 1.0f : 0.0f, 4.0f, dt);
        v->crawl[i] = approach(v->crawl[i], st == FS_CRAWL ? 1.0f : 0.0f, 4.0f, dt);

        // Падение: направление выбирается в начале; ползут ничком
        bool lying = st == FS_KNOCKED || st == FS_DOWN || st == FS_UNCONSCIOUS || st == FS_CRAWL || st == FS_DEAD;
        if (lying && v->down[i] < 0.05f)
            v->down_dir[i] = st == FS_KNOCKED ? 1.0f : (hashf(i * 131u + s->bout) < 0.5f ? 1.0f : -1.0f);
        if (st == FS_CRAWL) v->down_dir[i] = -1.0f;
        if (st == FS_GETUP) v->down[i] = 1.0f - state_progress(w, i, alpha);
        else v->down[i] = approach(v->down[i], lying ? 1.0f : 0.0f, 5.0f, dt);

        // Оружие выпадает из рук у выбывших и обезоруженных
        if (!v->dropped[i] && (!armed || st == FS_UNCONSCIOUS || st == FS_DEAD || st == FS_SURRENDER)) {
            v->dropped[i] = true;
            f32 h = w->heading[i] + (hashf(i * 7u + s->bout) - 0.5f) * 1.5f;
            v->drop_pos[i] = v3(w->x[i] + sinf(h) * 0.7f - cosf(h) * 0.3f, 0.0f, w->z[i] + cosf(h) * 0.7f + sinf(h) * 0.3f);
            v->drop_yaw[i] = h + PI_F * 0.5f;
        }
    }
}

u32 arena_view_draw(const ArenaView *v, const ArenaSession *s, f32 time, const GpuMesh *body, u32 variants,
                    const GpuMesh *weapons, M4 *bones, DrawItem *items)
{
    const CombatWorld *w = &s->w;
    f32 alpha = v->acc / COMBAT_DT;
    u32 n = 0, set = 0;
    if (v->in_bout) {
        for (u32 i = 0; i < w->count && i < ARENA_MAX_BOUT; i++) {
            u32 ri = s->idx[i];
            const FighterDesc *d = &s->roster[ri].d;
            f32 x = lerpf(v->prev_x[i], w->x[i], alpha), z = lerpf(v->prev_z[i], w->z[i], alpha);
            f32 h = v->prev_h[i] + wrap_angle(w->heading[i] - v->prev_h[i]) * alpha;
            f32 seed = (f32)ri * 13.7f + 3.0f;
            CharPose p = {
                .pos = v3(v->center.x + x, v->floor_y, v->center.z + z), .heading = h,
                .phase = v->phase[i], .walk = v->walk[i], .time = time, .seed = seed,
                .height = d->height / 1.8f, .width = 1.0f + 0.08f * sinf(seed * 3.17f + 1.0f),
                .move_dir = v->move_dir[i],
            };
            u8 st = w->state[i];
            f32 swing = 0.0f;
            if (st == FS_WINDUP) swing = state_progress(w, i, alpha);
            else if (st == FS_STRIKE) swing = 1.0f + state_progress(w, i, alpha);
            else if (st == FS_RECOVER) swing = 2.0f + state_progress(w, i, alpha);
            u8 wc = g_weapons[w->weapon[i]].wclass;
            CharAction act = {
                .guard = v->guard[i], .attack = w->attack[i], .swing = swing, .block = v->block[i],
                .dodge = v->dodge[i], .stagger = v->stagger[i], .down = v->down[i], .down_dir = v->down_dir[i],
                .kneel = v->kneel[i], .crawl = v->crawl[i], .limp = v->limp[i], .severed = w->severed[i],
                .two_hand = wc == WC_POLEARM || wc == WC_HEAVY,
            };
            M4 *b = &bones[(set++) * RENDER_MAX_BONES];
            character_pose(&p, &act, b);
            items[n++] = (DrawItem){ .mesh = &body[ri % variants], .bones = b };
            if (v->dropped[i]) {
                M4 *wb = &bones[(set++) * RENDER_MAX_BONES];
                V3 dp = v->drop_pos[i];
                weapon_ground_pose(v3(v->center.x + dp.x, v->floor_y + 0.03f, v->center.z + dp.z), v->drop_yaw[i], wb);
                items[n++] = (DrawItem){ .mesh = &weapons[w->weapon[i]], .bones = wb };
            } else {
                items[n++] = (DrawItem){ .mesh = &weapons[w->weapon[i]], .bones = b };
            }
        }
    }
    for (u32 k = 0; k < AV_SPECTATORS; k++) {
        CharPose p = {
            .pos = v3(v->center.x + v->sx[k], 0.12f, v->center.z + v->sz[k]), .heading = v->sh[k],
            .time = time, .seed = v->sseed[k],
            .height = 1.0f + 0.06f * sinf(v->sseed[k] * 7.31f), .width = 1.0f + 0.08f * sinf(v->sseed[k] * 3.17f + 1.0f),
        };
        M4 *b = &bones[(set++) * RENDER_MAX_BONES];
        character_pose(&p, NULL, b);
        items[n++] = (DrawItem){ .mesh = &body[(k + 2) % variants], .bones = b };
    }
    return n;
}
