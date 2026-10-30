#include "stand.h"

#include "../core/log.h"

// Место стенда: пустошь к западу от зала, ряды друг за другом, каждый со своей доской
#define STAND_X  -42.0f
#define STAND_Z  40.0f
#define ROW_DZ   10.0f  // дальше камеры: соседний ряд позади неё
#define REST_DX  1.0f   // шаг фигур в покое
#define T_DX     2.1f   // шаг фигур в T-позе: руки в стороны
#define ROW_W    (STAND_VIEWS * (REST_DX + T_DX))
#define HAIR_DX  0.55f  // шаг голов в ряду причёсок

// Спереди, слева, сзади, в 3/4
static const f32 g_view_yaw[STAND_VIEWS] = { 0.0f, 1.5707963f, 3.1415927f, -0.7853982f };
static const f32 g_heights[STAND_HEIGHTS] = { 0.85f, 1.15f };

void stand_init(Stand *s, Arena *perm)
{
    *s = (Stand){ 0 };
    s->bones = ARENA_PUSH_ARRAY(perm, M4, (size_t)STAND_N * RENDER_MAX_BONES);
}

// T-поза: поза привязки, руки от плеча повёрнуты в стороны горизонтально
static void tpose(M4 *out, V3 pos, f32 yaw, const V3 *J)
{
    M4 root = m4_mul(m4_translate(pos), m4_rot_y(yaw));
    for (u32 b = 0; b < RENDER_MAX_BONES; b++) out[b] = root;
    static const u8 arm[2][3] = { { BONE_UPPER_ARM_L, BONE_FOREARM_L, BONE_HAND_L },
                                  { BONE_UPPER_ARM_R, BONE_FOREARM_R, BONE_HAND_R } };
    for (u32 sd = 0; sd < 2; sd++) {
        V3 sh = J[arm[sd][0]], d = v3_sub(J[arm[sd][2]], sh);
        f32 a = (sd ? PI_F : 0.0f) - atan2f(d.y, d.x);
        a = atan2f(sinf(a), cosf(a));
        M4 r = m4_mul(m4_translate(sh), m4_mul(m4_rot_z(a), m4_translate(v3_mul(sh, -1.0f))));
        for (u32 k = 0; k < 3; k++) out[arm[sd][k]] = m4_mul(root, r);
    }
    out[RENDER_LOOK_SLOT] = (M4){ 0 };
}

static void rest(M4 *out, V3 pos, f32 yaw, f32 height, u32 body)
{
    CharPose p = { .pos = pos, .heading = yaw, .seed = 1.0f, .height = height, .width = 1.0f, .calm = true,
                   .joints = g_body_joints[body] };
    character_pose(&p, NULL, out);
    out[RENDER_LOOK_SLOT] = (M4){ 0 };
}

// Пол ряда: плита на высоте самой высокой точки земли под рядом, чтобы все стояли на одном уровне;
// за ней мерная доска
static f32 row_base(MeshBuilder *mb, f32 z, f32 row_w, GroundFn ground)
{
    f32 x0 = STAND_X - 0.3f, x1 = STAND_X + row_w + 0.3f, y = -1e9f;
    for (f32 x = x0; x <= x1; x += 0.5f)
        for (f32 dz = -1.2f; dz <= 1.0f; dz += 0.5f) y = maxf(y, ground(x, z + dz));
    y += 0.03f;
    f32 w = x1 - x0, cx = (x0 + x1) * 0.5f;
    mb_set(mb, v3(0.30f, 0.29f, 0.27f), MAT_CONCRETE, 0);
    mb_box(mb, m4_mul(m4_translate(v3(cx, y - 0.75f, z - 0.1f)), m4_scale(v3(w, 1.5f, 2.2f))));
    mb_set(mb, v3(0.11f, 0.11f, 0.10f), MAT_METAL, 0);
    mb_box(mb, m4_mul(m4_translate(v3(cx, y + 1.1f, z - 1.0f)), m4_scale(v3(w, 2.2f, 0.06f))));
    // Линии через 1/8 роста 1.80 м, толще — на середине и на макушке
    mb_set(mb, v3(0.25f, 0.42f, 0.30f), MAT_EMISSIVE, 0);
    for (u32 k = 1; k <= 8; k++) {
        f32 t = (k == 4 || k == 8) ? 0.024f : 0.012f;
        mb_box(mb, m4_mul(m4_translate(v3(cx, y + 0.225f * (f32)k, z - 0.968f)), m4_scale(v3(w, t, 0.004f))));
    }
    return y;
}

static void build(Stand *s, MeshBuilder *mb, Arena *perm, GroundFn ground)
{
    size_t mark = perm->used;
    mb_init(mb, perm, 1200, 1800);
    u32 n = 0;
    for (u32 b = 0; b < BODY_KIND_COUNT; b++) {
        f32 z = STAND_Z + ROW_DZ * (f32)b, y = s->floor[b] = row_base(mb, z, ROW_W, ground);
        for (u32 v = 0; v < STAND_VIEWS; v++) {
            f32 x = STAND_X + REST_DX * ((f32)v + 0.5f);
            rest(s->bones + (size_t)n * RENDER_MAX_BONES, v3(x, y, z), g_view_yaw[v], 1.0f, b);
            s->body[n++] = (u8)b;
        }
        for (u32 v = 0; v < STAND_VIEWS; v++) {
            f32 x = STAND_X + REST_DX * STAND_VIEWS + T_DX * ((f32)v + 0.5f);
            tpose(s->bones + (size_t)n * RENDER_MAX_BONES, v3(x, y, z), g_view_yaw[v], g_body_joints[b]);
            s->body[n++] = (u8)b;
        }
    }
    // Рост без ширины: каждое тело низким и высоким, спереди и сбоку
    f32 z = STAND_Z + ROW_DZ * (f32)BODY_KIND_COUNT, y = s->floor[BODY_KIND_COUNT] = row_base(mb, z, ROW_W, ground);
    for (u32 b = 0; b < BODY_KIND_COUNT; b++)
        for (u32 h = 0; h < STAND_HEIGHTS; h++)
            for (u32 v = 0; v < 2; v++) {
                f32 x = STAND_X + REST_DX * ((f32)((b * STAND_HEIGHTS + h) * 2 + v) + 0.5f);
                rest(s->bones + (size_t)n * RENDER_MAX_BONES, v3(x, y, z), g_view_yaw[v], g_heights[h], b);
                s->body[n++] = (u8)b;
            }
    // Причёски: каждый вид своего тела спереди, сбоку, сзади
    z = STAND_Z + ROW_DZ * (f32)(BODY_KIND_COUNT + 1);
    y = s->floor[BODY_KIND_COUNT + 1] = row_base(mb, z, HAIR_DX * STAND_HAIR_VIEWS * (f32)(look_cuts(BODY_MALE) + look_cuts(BODY_FEMALE)), ground);
    u32 h = 0;
    for (u32 b = BODY_MALE; b <= BODY_FEMALE; b++)
        for (u32 c = 0; c < look_cuts(b); c++, h++)
            for (u32 v = 0; v < STAND_HAIR_VIEWS; v++) {
                f32 x = STAND_X + HAIR_DX * ((f32)(h * STAND_HAIR_VIEWS + v) + 0.5f);
                rest(s->bones + (size_t)n * RENDER_MAX_BONES, v3(x, y, z), g_view_yaw[v], 1.0f, b);
                s->hair[n] = (u8)(GEAR_COUNT + c);
                s->body[n++] = (u8)b;
            }
    gpu_mesh_upload(&s->board, mb);
    perm->used = mark;
    s->count = n;
    s->built = true;
    log_info("СТЕНД ФИГУР: %u фигур, %u рядов", n, STAND_ROWS);
}

void stand_show(Stand *s, bool on, u32 row, MeshBuilder *mb, Arena *perm, GroundFn ground, RtsCamera *cam)
{
    s->on = on;
    camera_close(on);
    if (!on) return;
    if (!s->built) build(s, mb, perm, ground);
    row %= STAND_ROWS;
    f32 x = STAND_X + ROW_W * 0.5f, z = STAND_Z + ROW_DZ * (f32)row;
    *cam = (RtsCamera){ .target = { x, s->floor[row] + 0.35f, z }, .yaw = 0.0f, .pitch = 0.08f, .dist = 7.4f };
}

u32 stand_draw(const Stand *s, const CharMeshes *m, DrawItem *items)
{
    if (!s->on) return 0;
    u32 n = 0;
    items[n++] = (DrawItem){ .mesh = &s->board };
    for (u32 k = 0; k < s->count; k++) {
        const M4 *b = s->bones + (size_t)k * RENDER_MAX_BONES;
        items[n++] = (DrawItem){ .mesh = &m->body[s->body[k]], .bones = b };
        if (s->hair[k]) items[n++] = (DrawItem){ .mesh = &m->gear[s->body[k] * GEAR_ROW + s->hair[k]], .bones = b };
    }
    return n;
}
