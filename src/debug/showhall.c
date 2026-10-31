#include "showhall.h"
#include "../content/props.h"

#include <stdio.h>
#include <string.h>

#include "../sim/look.h"
#include "../content/ostov.h"
#include "../core/log.h"
#include "../defs/defs.h"
#include "../render/ui.h"

// Цвета — материалы data/defs/materials.txt: свои у цеха краска полос, железо, натрий ламп

// Коробка цеха снаружи и внутри (стены WT), уровни: пол, низ и верх плиты галереи, низ и верх крыши
#define X0  -22.0f
#define X1   22.0f
#define Z0  -96.0f
#define Z1  -68.0f
#define WT   0.6f
#define IX0 (X0 + WT)
#define IX1 (X1 - WT)
#define IZ0 (Z0 + WT)
#define IZ1 (Z1 - WT)
#define FY   0.1f
#define F2B  4.55f
#define F2   5.0f
#define RB   9.6f
#define RT  10.3f
// Галерея второго этажа над глухим складом: от южной стены до MZ
#define MZ  -84.0f
// Лестница вдоль западной стены: нижняя ступень у ST_Z1, верхняя у MZ
#define ST_X1 (IX0 + 2.2f)
#define ST_Z1 -72.0f
#define ST_N  24
// Вход в северной стене у восточного угла; тамбур — стена TB_Z от восточной стены до TB_X: свет
// из двери упирается в неё, в цех заходят в обход её западного края
#define DOOR_X0 17.4f
#define DOOR_X1 19.6f
#define DOOR_H  2.9f
#define TB_X 15.4f
#define TB_Z -71.2f
#define TB_H 3.6f

// Круги витрины, с. Поза отдыха в игре держится долго (g_rest), здесь — SH_HOLD, чтобы видеть вход и выход
#define SH_PRE    3.0f   // стоит перед позой
#define SH_HOLD   5.0f
#define SH_MOVE_PRE 2.5f // стоит перед движением
#define SH_WAIT   1.5f   // ходок стоит у края галереи
#define SH_NEAR   60.0f  // фигуры считаются, если глаз ближе, м
// Сроки клипов всем телом — те же, что в бою: падение KNOCKED_S, подъём со спины GETUP_BACK_S и ничком
// GETUP_FRONT_S (sim/combat.c), оседание COLLAPSE_S (fight_anim.c)
#define SH_FALL_S     1.1f
#define SH_GETUP_S    2.2f
#define SH_GETUP_FRONT_S 2.0f
#define SH_COLLAPSE_S 1.1f
#define SH_LIE_S      3.0f
#define SH_STAND_S    3.0f
// Смерть DIE_S, истёк кровью BLEED_S, оседает SINK_S, переворот TURN_S, сдача KNEEL_S (fight_anim.c),
// подбор оружия PICKUP_S (sim/combat.c)
#define SH_DIE_S      0.8f
#define SH_BLEED_S    2.0f
#define SH_SINK_S     1.1f
#define SH_TURN_S     1.2f
#define SH_KNEEL_S    1.6f
#define SH_PICKUP_S   0.9f

// Ряды первого этажа: A — позы отдыха, B — движения стоя, вразбежку, чтобы задний ряд был виден
#define ROW_A_X(k) (-15.0f + 5.0f * (f32)(k))
#define ROW_A_Z    -76.5f
#define ROW_B_X(k) (-12.5f + 5.0f * (f32)(k))
#define ROW_B_Z    -81.6f

typedef enum ExKind { EX_REST, EX_REST_BACK, EX_MOVE, EX_WALK, EX_CLIP } ExKind;
// Круги клипов: у восточной стены — сбит с ног, обморок ничком и на спину (CL_EAST); вдоль северной
// стены — смерть по направлению удара, истёк кровью, ноги не держат, переворот, подбор оружия, сдача
enum { CL_KNOCK, CL_FAINT_DOWN, CL_FAINT_UP, CL_EAST,
       CL_DIE_BACK = CL_EAST, CL_DIE_FRONT, CL_DIE_SIDE, CL_BLEED, CL_SINK, CL_TURN, CL_PICKUP, CL_KNEEL, CL_COUNT };
// от входа на запад: стоя лицом к стене, читаются слева направо
#define CL_NORTH_X(k) (12.0f - 4.0f * (f32)(k))
#define CL_NORTH_Z    -71.4f

// Лампы: колпак на тросе от балки; первый этаж — над рядами, галерея — под крышей
#define LAMP_N 14
typedef struct Lamp {
    V3  pos;
    f32 power, radius;
} Lamp;
static const Lamp g_lamp[LAMP_N] = {
    { { -15.0f, 4.3f, -74.0f }, 1.0f, 11.0f }, { { -5.0f, 4.3f, -74.0f }, 1.0f, 11.0f },
    { { 5.0f, 4.3f, -74.0f }, 1.0f, 11.0f },   { { 15.0f, 4.3f, -79.5f }, 1.0f, 11.0f },
    { { -15.0f, 4.3f, -79.5f }, 1.0f, 11.0f }, { { -5.0f, 4.3f, -79.5f }, 1.0f, 11.0f },
    { { 5.0f, 4.3f, -79.5f }, 1.0f, 11.0f },   { { 18.5f, 3.2f, -69.9f }, 0.35f, 6.0f },
    { { -15.0f, 8.3f, -89.5f }, 0.9f, 10.0f }, { { -5.0f, 8.3f, -89.5f }, 0.9f, 10.0f },
    { { 5.0f, 8.3f, -89.5f }, 0.9f, 10.0f },   { { 15.0f, 8.3f, -89.5f }, 0.9f, 10.0f },
    { { -20.3f, 7.4f, -78.0f }, 0.6f, 8.0f },  { { 12.5f, 4.3f, -74.0f }, 0.8f, 9.0f },
};
#define LAMP_FLICKER 12   // над лестницей лампа гудит и мигает
#define BEAM_Y (RB - 0.8f)

static u32 g_seed;
static u32 next_seed(void) { return ++g_seed * 7919u; }

static void conc(MeshBuilder *mb, V3 lo, V3 hi, u8 hide)
{
    ostov_rough_box(mb, lo, hi, 1.3f, 0.12f, hide, next_seed());
}

// Стена с проёмом: брус lo..hi по X, проём x0..x1 до высоты h
static void wall_x(MeshBuilder *mb, f32 z0, f32 z1, f32 xa, f32 xb, f32 x0, f32 x1, f32 h, f32 top)
{
    conc(mb, v3(xa, FY, z0), v3(x0, top, z1), OSTOV_HIDE_BOT);
    conc(mb, v3(x1, FY, z0), v3(xb, top, z1), OSTOV_HIDE_BOT);
    conc(mb, v3(x0, FY + h, z0), v3(x1, top, z1), 0);
}

static void cable(MeshBuilder *mb, V3 a, V3 b)
{
    mb_set(mb, v3(0.04f, 0.04f, 0.04f), MAT_METAL, BONE_NONE);
    mb_cylinder(mb, a, b, 0.012f, 4);
}

// Колпак лампы: ржавая жесть конусом, под ней светящийся шар
static void lamp_mesh(MeshBuilder *mb, V3 p, f32 hang_to)
{
    cable(mb, v3_add(p, v3(0, 0.28f, 0)), v3(p.x, hang_to, p.z));
    static const V2 shade[] = { { 0.0f, 0.05f }, { 0.08f, 0.07f }, { 0.20f, 0.20f }, { 0.30f, 0.30f }, { 0.31f, 0.29f } };
    prop_mat(mb, g_mat.rust);
    mb_lathe(mb, v3_add(p, v3(0, 0.30f, 0)), v3_add(p, v3(0, -0.01f, 0)), shade, ARRAY_COUNT(shade), 12, 1.0f, 1.0f);
    mb_set(mb, v3(1.0f, 0.78f, 0.45f), MAT_EMISSIVE, BONE_NONE);
    mb_capsule(mb, v3_add(p, v3(0, 0.06f, 0)), v3_add(p, v3(0, 0.02f, 0)), 0.07f, 0.07f, 8, 1.0f, 1.0f);
}

static void glyph(MeshBuilder *mb, V3 c, V3 normal, f32 hw, f32 hh, V3 color)
{
    mb_set(mb, color, MAT_GLYPH, BONE_NONE);
    V3 right = v3_norm(v3_cross(v3(0, 1, 0), normal));
    mb_quad(mb, v3_add(c, v3_mul(normal, 0.03f)), v3_mul(right, hw), v3(0, hh, 0));
}

static void pipe_run(MeshBuilder *mb, V3 a, V3 b, f32 r)
{
    prop_mat(mb, g_mat.rust);
    mb_cylinder(mb, a, b, r, 10);
}

static void build_mesh(MeshBuilder *mb)
{
    g_seed = 0;
    // Пол плитой, стены, крыша
    prop_mat(mb, g_mat.concrete_dark);
    conc(mb, v3(X0 - 0.3f, -0.5f, Z0 - 0.3f), v3(X1 + 0.3f, FY, Z1 + 0.3f), OSTOV_HIDE_BOT);
    prop_mat(mb, g_mat.concrete);
    wall_x(mb, Z1 - WT, Z1, X0, X1, DOOR_X0, DOOR_X1, DOOR_H, RB);
    conc(mb, v3(X0, FY, Z0), v3(X1, RB, Z0 + WT), OSTOV_HIDE_BOT);
    conc(mb, v3(X0, FY, IZ0), v3(X0 + WT, RB, IZ1), OSTOV_HIDE_BOT | OSTOV_HIDE_ZN | OSTOV_HIDE_ZP);
    conc(mb, v3(X1 - WT, FY, IZ0), v3(X1, RB, IZ1), OSTOV_HIDE_BOT | OSTOV_HIDE_ZN | OSTOV_HIDE_ZP);
    conc(mb, v3(X0 - 0.4f, RB, Z0 - 0.4f), v3(X1 + 0.4f, RT, Z1 + 0.4f), 0);
    // Снаружи контрфорсы через 7 м: глухая коробка не плоская, тени ложатся полосами
    for (u32 k = 0; k < 7; k++) {
        f32 x = -21.0f + 7.0f * (f32)k;
        if (x > DOOR_X0 - 1.0f && x < DOOR_X1 + 1.0f) continue;
        conc(mb, v3(x - 0.4f, FY - 0.6f, Z1), v3(x + 0.4f, RB - 0.3f, Z1 + 0.5f), OSTOV_HIDE_BOT | OSTOV_HIDE_ZN);
        conc(mb, v3(x - 0.4f, FY - 0.6f, Z0 - 0.5f), v3(x + 0.4f, RB - 0.3f, Z0), OSTOV_HIDE_BOT | OSTOV_HIDE_ZP);
    }
    for (u32 k = 0; k < 4; k++) {
        f32 z = -91.5f + 7.0f * (f32)k;
        conc(mb, v3(X1, FY - 0.6f, z - 0.4f), v3(X1 + 0.5f, RB - 0.3f, z + 0.4f), OSTOV_HIDE_BOT | OSTOV_HIDE_XN);
        conc(mb, v3(X0 - 0.5f, FY - 0.6f, z - 0.4f), v3(X0, RB - 0.3f, z + 0.4f), OSTOV_HIDE_BOT | OSTOV_HIDE_XP);
    }
    // На крыше две вытяжные шахты и ржавая труба
    conc(mb, v3(-12.0f, RT, -86.0f), v3(-9.5f, RT + 2.2f, -83.5f), OSTOV_HIDE_BOT);
    conc(mb, v3(6.0f, RT, -92.0f), v3(8.0f, RT + 1.6f, -90.0f), OSTOV_HIDE_BOT);
    pipe_run(mb, v3(14.0f, RT, -78.0f), v3(14.0f, RT + 4.5f, -78.0f), 0.35f);

    // Балки под крышей поперёк цеха; лампы висят на них
    prop_mat(mb, g_mat.concrete_dark);
    static const f32 beam_z[] = { -90.5f, -85.0f, -79.5f, -74.0f };
    for (u32 k = 0; k < ARRAY_COUNT(beam_z); k++)
        conc(mb, v3(IX0, BEAM_Y, beam_z[k] - 0.25f), v3(IX1, RB, beam_z[k] + 0.25f), OSTOV_HIDE_TOP | OSTOV_HIDE_XN | OSTOV_HIDE_XP);
    // Пилястры по длинным стенам: стена не плоская, тень ложится полосами
    for (u32 k = 0; k < 6; k++) {
        f32 x = -17.5f + 7.0f * (f32)k;
        if (x > TB_X - 1.0f) continue;
        conc(mb, v3(x - 0.35f, FY, IZ1 - 0.35f), v3(x + 0.35f, BEAM_Y, IZ1), OSTOV_HIDE_BOT | OSTOV_HIDE_ZP);
    }

    // Тамбур: стена напротив двери до TB_X, над ней пусто до балок
    prop_mat(mb, g_mat.concrete);
    conc(mb, v3(TB_X, FY, TB_Z - 0.35f), v3(IX1, TB_H, TB_Z), OSTOV_HIDE_BOT | OSTOV_HIDE_XP);
    // Перемычка над дверью снаружи и откинутая ржавая створка
    conc(mb, v3(DOOR_X0 - 0.5f, DOOR_H, Z1), v3(DOOR_X1 + 0.5f, DOOR_H + 0.45f, Z1 + 0.5f), 0);
    prop_mat(mb, g_mat.rust);
    mb_box(mb, m4_mul(m4_translate(v3(DOOR_X1 + 1.15f, FY + 1.35f, Z1 + 0.55f)), m4_mul(m4_rot_y(-0.35f), m4_scale(v3(2.1f, 2.7f, 0.06f)))));
    glyph(mb, v3(18.5f, DOOR_H + 0.9f, Z1), v3(0, 0, 1), 0.8f, 0.22f, mat_c(g_mat.glyph_green));

    // Склад под галереей: глухая стена с запертыми створками; сверху плита галереи с карнизом
    prop_mat(mb, g_mat.concrete);
    conc(mb, v3(IX0, FY, IZ0), v3(IX1, F2B, MZ), OSTOV_HIDE_BOT | OSTOV_HIDE_TOP | OSTOV_HIDE_ZN | OSTOV_HIDE_XN | OSTOV_HIDE_XP);
    conc(mb, v3(IX0, F2B, IZ0), v3(IX1, F2, MZ + 0.35f), OSTOV_HIDE_ZN | OSTOV_HIDE_XN | OSTOV_HIDE_XP);
    for (u32 k = 0; k < 3; k++) {
        f32 x = -9.0f + 9.0f * (f32)k;
        prop_mat(mb, g_mat.rust);
        mb_box(mb, m4_mul(m4_translate(v3(x, FY + 1.5f, MZ + 0.04f)), m4_scale(v3(2.6f, 2.8f, 0.08f))));
        prop_mat(mb, g_mat.iron_dark);
        mb_box(mb, m4_mul(m4_translate(v3(x, FY + 1.5f, MZ + 0.09f)), m4_scale(v3(0.06f, 2.8f, 0.04f))));
    }
    glyph(mb, v3(-13.5f, 3.4f, MZ), v3(0, 0, 1), 0.5f, 0.5f, mat_c(g_mat.glyph_red));

    // Лестница: ступени цельным бетоном до пола
    prop_mat(mb, g_mat.concrete_dark);
    f32 run = (ST_Z1 - MZ) / (f32)ST_N, rise = (F2 - FY) / (f32)ST_N;
    for (u32 i = 0; i < ST_N; i++) {
        f32 za = ST_Z1 - run * (f32)(i + 1), zb = ST_Z1 - run * (f32)i;
        ostov_rough_box(mb, v3(IX0, FY, za), v3(ST_X1, FY + rise * (f32)(i + 1), zb), 0.6f, 0.05f,
                        OSTOV_HIDE_BOT | OSTOV_HIDE_XN | OSTOV_HIDE_ZN, next_seed());
    }

    // Перила: стойки через 1.5 м и две трубы — по краю галереи и вдоль лестницы
    prop_mat(mb, g_mat.iron_dark);
    f32 rz = MZ + 0.2f;
    for (f32 x = ST_X1 + 0.1f; x <= IX1 - 0.1f; x += 1.5f) mb_cylinder(mb, v3(x, F2, rz), v3(x, F2 + 1.05f, rz), 0.025f, 6);
    mb_cylinder(mb, v3(ST_X1 + 0.1f, F2 + 1.05f, rz), v3(IX1, F2 + 1.05f, rz), 0.03f, 8);
    mb_cylinder(mb, v3(ST_X1 + 0.1f, F2 + 0.55f, rz), v3(IX1, F2 + 0.55f, rz), 0.022f, 8);
    f32 sx = ST_X1 - 0.1f;
    for (u32 i = 1; i < ST_N; i += 4) {
        f32 z = ST_Z1 - run * ((f32)i + 0.5f), y = FY + rise * (f32)(i + 1);
        mb_cylinder(mb, v3(sx, y, z), v3(sx, y + 1.0f, z), 0.025f, 6);
    }
    mb_cylinder(mb, v3(sx, FY + rise + 1.0f, ST_Z1 - run * 0.5f), v3(sx, F2 + 1.05f, MZ + 0.2f), 0.03f, 8);

    // Разметка дорожек галереи: полосы через метр, по ним видно, сколько проходит шаг
    prop_mat(mb, g_mat.paint_pale);
    static const f32 lane_z[] = { -86.6f, -89.4f, -92.2f };
    for (u32 k = 0; k < ARRAY_COUNT(lane_z); k++)
        for (i32 x = -17; x <= 17; x++) {
            f32 w = x % 5 == 0 ? 0.08f : 0.035f;
            mb_quad(mb, v3((f32)x, F2 + 0.004f, lane_z[k] + 1.1f), v3(w, 0, 0), v3(0, 0, -0.18f));
        }

    // Трубы по северной и восточной стенам, спуск в пол
    pipe_run(mb, v3(IX0 + 3.0f, 6.6f, IZ1 - 0.35f), v3(TB_X - 0.5f, 6.6f, IZ1 - 0.35f), 0.16f);
    pipe_run(mb, v3(IX0 + 3.0f, 6.15f, IZ1 - 0.30f), v3(IX1 - 0.3f, 6.15f, IZ1 - 0.30f), 0.09f);
    pipe_run(mb, v3(IX1 - 0.35f, 6.15f, IZ1 - 0.3f), v3(IX1 - 0.35f, 6.15f, MZ + 0.5f), 0.09f);
    pipe_run(mb, v3(IX1 - 0.35f, 6.15f, MZ + 0.5f), v3(IX1 - 0.35f, FY, MZ + 0.5f), 0.09f);
    // Кран-балка над первым этажом: рельс и тележка с крюком
    prop_mat(mb, g_mat.iron_dark);
    mb_box(mb, m4_mul(m4_translate(v3(0.0f, BEAM_Y - 0.35f, -76.8f)), m4_scale(v3(IX1 - IX0, 0.3f, 0.18f))));
    prop_mat(mb, g_mat.rust);
    mb_box(mb, m4_mul(m4_translate(v3(9.5f, BEAM_Y - 0.65f, -76.8f)), m4_scale(v3(0.8f, 0.35f, 0.5f))));
    cable(mb, v3(9.5f, BEAM_Y - 0.8f, -76.8f), v3(9.5f, 5.6f, -76.8f));
    prop_mat(mb, g_mat.iron_dark);
    mb_box(mb, m4_mul(m4_translate(v3(9.5f, 5.45f, -76.8f)), m4_scale(v3(0.12f, 0.3f, 0.05f))));
    // Знаки у выхода из тамбура и на стене галереи
    glyph(mb, v3(TB_X + 1.6f, 2.6f, TB_Z - 0.35f), v3(0, 0, -1), 0.7f, 0.18f, mat_c(g_mat.glyph_green));
    glyph(mb, v3(0.0f, 7.6f, IZ0), v3(0, 0, 1), 2.4f, 0.3f, mat_c(g_mat.glyph_green));

    // Площадки под фигурами первого этажа
    prop_mat(mb, g_mat.concrete_dark);
    for (u32 k = 0; k < 6; k++)
        for (u32 r = 0; r < 2; r++) {
            f32 x = r ? ROW_B_X(k) : ROW_A_X(k), z = r ? ROW_B_Z : ROW_A_Z;
            ostov_rough_box(mb, v3(x - 1.3f, FY, z - 1.3f), v3(x + 1.3f, FY + 0.12f, z + 1.3f), 0.8f, 0.04f, OSTOV_HIDE_BOT, next_seed());
        }
    for (u32 k = 0; k < CL_EAST; k++) {
        f32 z = -75.5f - 3.6f * (f32)k;
        ostov_rough_box(mb, v3(14.6f, FY, z - 1.4f), v3(20.4f, FY + 0.12f, z + 1.4f), 0.8f, 0.04f, OSTOV_HIDE_BOT, next_seed());
    }
    for (u32 k = 0; k < CL_COUNT - CL_EAST; k++) {
        f32 x = CL_NORTH_X(k);
        ostov_rough_box(mb, v3(x - 1.6f, FY, CL_NORTH_Z - 1.8f), v3(x + 1.6f, FY + 0.12f, CL_NORTH_Z + 1.8f), 0.8f, 0.04f,
                        OSTOV_HIDE_BOT, next_seed());
    }

    for (u32 k = 0; k < LAMP_N; k++) {
        V3 p = g_lamp[k].pos;
        f32 top = p.y < F2 ? BEAM_Y : RB;
        if (k == 7) top = TB_H + 2.0f;     // над тамбуром — к стене
        if (k == LAMP_FLICKER) top = RB;
        lamp_mesh(mb, p, top);
    }
}

static f32 plinth(void) { return FY + 0.12f; }

static void add(ShowHall *s, u8 kind, u8 what, V3 pos, f32 heading, f32 shift)
{
    if (s->count >= SHOWHALL_MAX) return;
    u32 i = s->count++;
    Exhibit *e = &s->ex[i];
    *e = (Exhibit){ .kind = kind, .what = what, .pos = pos, .heading = heading, .shift = shift };
    e->body = (u8)(i % BODY_KIND_COUNT);
    u32 cuts = look_cuts(e->body);
    e->hair = cuts ? (u8)(GEAR_CAP + (i * 7u + 3u) % cuts) : 0;
}

void showhall_init(ShowHall *s, MeshBuilder *mb, Arena *perm)
{
    memset(s, 0, sizeof *s);
    size_t mark = perm->used;
    mb_init(mb, perm, 200000, 600000);
    build_mesh(mb);
    gpu_mesh_upload(&s->mesh, mb);
    log_info("МЕРИЛЬНЯ: %u треугольников", mb->icount / 3);
    perm->used = mark;
    s->bones = ARENA_PUSH_ARRAY(perm, M4, (size_t)SHOWHALL_MAX * RENDER_MAX_BONES);

    // Ряд A — позы отдыха и одна, брошенная на полпути; в три четверти, чтобы виден был профиль
    for (u32 k = REST_SQUAT; k < REST_COUNT; k++) add(s, EX_REST, (u8)k, v3(ROW_A_X(k - 1), plinth(), ROW_A_Z), 0.6f, 1.3f * (f32)k);
    add(s, EX_REST_BACK, REST_SIT, v3(ROW_A_X(5), plinth(), ROW_A_Z), 0.6f, 0.0f);
    // Ряд B — движения стоя лицом к входу
    for (u32 k = MOVE_NECK; k < MOVE_COUNT; k++) {
        add(s, EX_MOVE, (u8)k, v3(ROW_B_X(k - 1), plinth(), ROW_B_Z), 0.25f, 0.9f * (f32)k);
        if (k == MOVE_HILT) s->ex[s->count - 1].weapon = (u8)(def_weapon("sekach") + 1);
    }
    // Клипы всем телом у восточной стены, падают вдоль площадки
    for (u32 k = 0; k < CL_EAST; k++) add(s, EX_CLIP, (u8)k, v3(17.5f, plinth(), -75.5f - 3.6f * (f32)k), -1.5708f, 1.1f * (f32)k);
    // Вдоль северной стены лицом в цех: на спину валятся к стене, ничком — в цех, набок — вдоль ряда
    for (u32 k = CL_EAST; k < CL_COUNT; k++)
        add(s, EX_CLIP, (u8)k, v3(CL_NORTH_X(k - CL_EAST), plinth(), CL_NORTH_Z), 3.1416f, 0.7f * (f32)k);
    // Галерея: шаг, трусца, бег туда и обратно
    static const f32 lane_z[GAIT_COUNT] = { -87.0f, -89.8f, -92.6f };
    for (u32 g = 0; g < GAIT_COUNT; g++) {
        add(s, EX_WALK, (u8)g, v3(-16.0f, F2, lane_z[g]), 1.5708f, 0.0f);
        Exhibit *e = &s->ex[s->count - 1];
        player_init(&e->walker, e->pos, 1.5708f);
        e->walker.gait = (u8)g;
        e->walker.vmax = g_gait_speed[g];
        e->wait = 0.6f * (f32)g;
    }
}

f32 showhall_ground(f32 x, f32 z, f32 base)
{
    if (x < X0 || x > X1 || z < Z0 || z > Z1) return base;
    if (z < MZ) return F2;
    if (x < ST_X1 && z < ST_Z1) return FY + (ST_Z1 - z) / (ST_Z1 - MZ) * (F2 - FY);
    return FY;
}

// ---------------------------------------------------------------- круги

static f32 wrapf(f32 t, f32 c) { return t - floorf(t / c) * c; }

static void rest_cycle(Exhibit *e, f32 T, CharPose *p)
{
    static const char *ph[4] = { "СТОИТ", "ВХОД", "ДЕРЖИТ", "ВЫХОД" };
    static const u8 rp[4] = { RP_ENTER, RP_ENTER, RP_HOLD, RP_EXIT };
    const RestDef *d = &g_rest[e->what];
    f32 seg[4] = { SH_PRE, d->enter_s, SH_HOLD, d->exit_s };
    f32 lt = wrapf(T + e->shift, seg[0] + seg[1] + seg[2] + seg[3]);
    u32 k = 0;
    while (k < 3 && lt >= seg[k]) lt -= seg[k++];
    p->rest = k ? e->what : REST_NONE;
    p->rest_ph = rp[k];
    p->rest_t = lt;
    snprintf(e->line[0], sizeof e->line[0], "%s", d->name);
    if (k == 2) snprintf(e->line[1], sizeof e->line[1], "ДЕРЖИТ %.1f С (В ИГРЕ %.0f-%.0f С)", (f64)lt, (f64)d->hold_min, (f64)d->hold_max);
    else snprintf(e->line[1], sizeof e->line[1], "%s %.1f / %.1f С", ph[k], (f64)lt, (f64)seg[k]);
}

// Садится, на полпути его зовут — встаёт тем же путём назад быстрее (REST_HURRY в player.c)
#define SH_BACK_HURRY 1.3f
static void back_cycle(Exhibit *e, f32 T, CharPose *p)
{
    const RestDef *d = &g_rest[e->what];
    f32 half = 0.5f * d->enter_s, back = half / SH_BACK_HURRY;
    f32 cyc = SH_PRE + half + back, lt = wrapf(T + e->shift, cyc);
    snprintf(e->line[0], sizeof e->line[0], "%s: ПОЗВАЛИ НА ПОЛПУТИ", d->name);
    if (lt < SH_PRE) {
        snprintf(e->line[1], sizeof e->line[1], "СТОИТ");
        return;
    }
    lt -= SH_PRE;
    p->rest = e->what;
    if (lt < half) {
        p->rest_ph = RP_ENTER;
        p->rest_t = lt;
        snprintf(e->line[1], sizeof e->line[1], "ВХОД %.1f / %.1f С", (f64)lt, (f64)d->enter_s);
    } else {
        p->rest_ph = RP_BACK;
        p->rest_t = half - (lt - half) * SH_BACK_HURRY;
        snprintf(e->line[1], sizeof e->line[1], "НАЗАД %.1f / %.1f С", (f64)(lt - half), (f64)back);
    }
}

static void move_cycle(Exhibit *e, f32 T, CharPose *p)
{
    f32 dur = g_move_s[e->what], cyc = SH_MOVE_PRE + dur, lt = wrapf(T + e->shift, cyc) - SH_MOVE_PRE;
    snprintf(e->line[0], sizeof e->line[0], "%s", g_move_name[e->what]);
    p->move = MOVE_NONE + 1;
    if (lt < 0.0f) {
        snprintf(e->line[1], sizeof e->line[1], "СТОИТ");
        return;
    }
    p->move = (u8)(e->what + 1);
    p->move_t = lt;
    snprintf(e->line[1], sizeof e->line[1], "%.1f / %.1f С", (f64)lt, (f64)dur);
}

// Круг клипа из четырёх шагов: имя, клип, длительность; rev — тот же клип обратно в том же темпе
typedef struct Step {
    const char *name;
    u8 clip;
    f32 s;
    bool rev;
} Step;
typedef struct ClipLoop {
    const char *title;
    Step st[4];
    u8 fall;      // CHAR_FALL_*: куда валится
    i8 side;      // лежит на боку: 1 — на левом
    bool prone;
    bool dead;    // мёртвый: обмяк, не дышит
    bool faint;   // без сознания: обмяк, пока оседает и лежит; встаёт уже сам
} ClipLoop;

static const ClipLoop g_loops[CL_COUNT] = {
    [CL_KNOCK] = { "СБИТ С НОГ И ВСТАЁТ",
                   { { "ЛЕЖИТ", CLIP_LIE, SH_LIE_S, 0 }, { "ВСТАЁТ", CLIP_GETUP, SH_GETUP_S, 0 },
                     { "СТОИТ", CLIP_NONE, SH_STAND_S, 0 }, { "ПАДАЕТ", CLIP_FALL, SH_FALL_S, 0 } },
                   .fall = CHAR_FALL_BACK },
    [CL_FAINT_DOWN] = { "ОБМОРОК НИЧКОМ",
                        { { "СТОИТ", CLIP_NONE, SH_STAND_S, 0 }, { "ОСЕДАЕТ", CLIP_COLLAPSE, SH_COLLAPSE_S, 0 },
                          { "ЛЕЖИТ", CLIP_LIE, SH_LIE_S, 0 }, { "ВСТАЁТ", CLIP_GETUP, SH_GETUP_FRONT_S, 0 } },
                        .prone = true, .faint = true },
    [CL_FAINT_UP] = { "ОБМОРОК НА СПИНУ",
                      { { "СТОИТ", CLIP_NONE, SH_STAND_S, 0 }, { "ОСЕДАЕТ", CLIP_COLLAPSE, SH_COLLAPSE_S, 0 },
                        { "ЛЕЖИТ", CLIP_LIE, SH_LIE_S, 0 }, { "ВСТАЁТ", CLIP_GETUP, SH_GETUP_S, 0 } },
                      .faint = true },
    [CL_DIE_BACK] = { "УБИТ СПЕРЕДИ",
                      { { "СТОИТ", CLIP_NONE, SH_STAND_S, 0 }, { "ПАДАЕТ", CLIP_DIE, SH_DIE_S, 0 },
                        { "ЛЕЖИТ", CLIP_LIE, SH_LIE_S, 0 }, { "ОБРАТНО", CLIP_DIE, SH_DIE_S, 1 } },
                      .fall = CHAR_FALL_BACK, .dead = true },
    [CL_DIE_FRONT] = { "УБИТ В СПИНУ",
                       { { "СТОИТ", CLIP_NONE, SH_STAND_S, 0 }, { "ПАДАЕТ", CLIP_DIE, SH_DIE_S, 0 },
                         { "ЛЕЖИТ", CLIP_LIE, SH_LIE_S, 0 }, { "ОБРАТНО", CLIP_DIE, SH_DIE_S, 1 } },
                       .fall = CHAR_FALL_FRONT, .prone = true, .dead = true },
    [CL_DIE_SIDE] = { "УБИТ СБОКУ",
                      { { "СТОИТ", CLIP_NONE, SH_STAND_S, 0 }, { "ПАДАЕТ", CLIP_DIE, SH_DIE_S, 0 },
                        { "ЛЕЖИТ", CLIP_LIE, SH_LIE_S, 0 }, { "ОБРАТНО", CLIP_DIE, SH_DIE_S, 1 } },
                      .fall = CHAR_FALL_LEFT, .side = 1, .dead = true },
    [CL_BLEED] = { "ИСТЁК КРОВЬЮ",
                   { { "СТОИТ", CLIP_NONE, SH_STAND_S, 0 }, { "ОСЕДАЕТ", CLIP_BLEED, SH_BLEED_S, 0 },
                     { "ЛЕЖИТ", CLIP_LIE, SH_LIE_S, 0 }, { "ОБРАТНО", CLIP_BLEED, SH_BLEED_S, 1 } },
                   .side = -1, .dead = true },
    [CL_SINK] = { "НОГИ НЕ ДЕРЖАТ",
                  { { "СТОИТ", CLIP_NONE, SH_STAND_S, 0 }, { "ОСЕДАЕТ", CLIP_SINK, SH_SINK_S, 0 },
                    { "ПОЛЗЁТ", CLIP_CRAWL, SH_LIE_S, 0 }, { "ОБРАТНО", CLIP_SINK, SH_SINK_S, 1 } },
                  .prone = true },
    [CL_TURN] = { "СО СПИНЫ НА ЖИВОТ",
                  { { "ЛЕЖИТ", CLIP_LIE, SH_LIE_S, 0 }, { "ПЕРЕВОРАЧИВАЕТСЯ", CLIP_TURN, SH_TURN_S, 0 },
                    { "ПОЛЗЁТ", CLIP_CRAWL, SH_LIE_S, 0 }, { "ОБРАТНО", CLIP_TURN, SH_TURN_S, 1 } },
                   .fall = CHAR_FALL_BACK },
    [CL_PICKUP] = { "ПОДНИМАЕТ ОРУЖИЕ",
                    { { "СТОИТ", CLIP_NONE, SH_STAND_S, 0 }, { "ПОДНИМАЕТ", CLIP_PICKUP, SH_PICKUP_S, 0 },
                      { "СТОИТ", CLIP_NONE, SH_STAND_S, 0 }, { "ОБРАТНО", CLIP_PICKUP, SH_PICKUP_S, 1 } },
                   .fall = CHAR_FALL_BACK },
    [CL_KNEEL] = { "СДАЁТСЯ",
                   { { "СТОИТ", CLIP_NONE, SH_STAND_S, 0 }, { "НА КОЛЕНИ", CLIP_KNEEL, SH_KNEEL_S, 0 },
                     { "НА КОЛЕНЯХ", CLIP_KNEEL, SH_LIE_S, 0 }, { "ОБРАТНО", CLIP_KNEEL, SH_KNEEL_S, 1 } },
                   .fall = CHAR_FALL_BACK },
};

static void clip_cycle(Exhibit *e, f32 T, CharAction *a)
{
    const ClipLoop *c = &g_loops[e->what];
    const Step *st = c->st;
    snprintf(e->line[0], sizeof e->line[0], "%s", c->title);
    *a = (CharAction){ 0 };
    f32 cyc = st[0].s + st[1].s + st[2].s + st[3].s, lt = wrapf(T + e->shift, cyc);
    u32 k = 0;
    while (k < 3 && lt >= st[k].s) lt -= st[k++].s;
    a->clip = st[k].clip;
    f32 u = lt / st[k].s;
    // Лёжа и ползком клип идёт по времени; поза на коленях держит конец входа
    if (a->clip == CLIP_LIE || a->clip == CLIP_CRAWL) a->clip_t = lt;
    else if (a->clip == CLIP_KNEEL && k == 2) a->clip_t = 1.0f;
    else a->clip_t = st[k].rev ? 1.0f - u : u;
    // Переворот кончается ничком: дальше ползёт ничком, назад — со спины
    a->prone = c->prone || (e->what == CL_TURN && k == 2);
    a->side = (i8)(a->clip == CLIP_LIE || a->clip == CLIP_BLEED ? c->side : 0);
    a->fall_dir = c->fall;
    a->dead = c->dead;
    a->still = c->dead && a->clip == CLIP_LIE ? lt : 9.0f;
    if (c->dead || (c->faint && (a->clip == CLIP_COLLAPSE || a->clip == CLIP_LIE))) a->limp = 1.0f;
    snprintf(e->line[1], sizeof e->line[1], "%s %.1f / %.1f С", st[k].name, (f64)lt, (f64)st[k].s);
}

static f32 gallery_ground(f32 x, f32 z) { return showhall_ground(x, z, F2); }

// Ходок: идёт к другому краю, там стоит SH_WAIT и разворачивается
static void walk_step(Exhibit *e, f32 dt)
{
    static const char *gait[GAIT_COUNT] = { "ШАГ", "ТРУСЦА", "БЕГ" };
    Player *w = &e->walker;
    if (!w->moving) {
        e->wait -= dt;
        if (e->wait <= 0.0f) {
            f32 x = w->pos.x < 0.0f ? 16.0f : -16.0f;
            player_order_move(w, v3(x, F2, e->pos.z));
            e->wait = SH_WAIT;
        }
    }
    if (dt > 0.0f) {
        w->idle = 0.0f;   // у края не садится
        player_update(w, dt, gallery_ground);
    }
    snprintf(e->line[0], sizeof e->line[0], "%s %.1f М/С", gait[e->what], (f64)g_gait_speed[e->what]);
    snprintf(e->line[1], sizeof e->line[1], "СЕЙЧАС %.2f М/С", (f64)w->speed);
}

// ---------------------------------------------------------------- кадр

u32 showhall_draw(ShowHall *s, const CharMeshes *m, V3 eye, f32 time, f32 dt, DrawItem *items)
{
    u32 n = 0;
    items[n++] = (DrawItem){ .mesh = &s->mesh };
    f32 dx = eye.x - 0.5f * (X0 + X1), dz = eye.z - 0.5f * (Z0 + Z1);
    s->posed = dx * dx + dz * dz < SH_NEAR * SH_NEAR;
    if (!s->posed) return n;
    f32 T = time + s->t_off;
    for (u32 i = 0; i < s->count; i++) {
        Exhibit *e = &s->ex[i];
        CharPose p = { .pos = e->pos, .heading = e->heading, .time = T, .seed = 0.37f + 0.61f * (f32)i, .height = 1.0f,
                       .width = 1.0f, .joints = g_body_joints[e->body], .weapon = e->weapon, .move = MOVE_NONE + 1 };
        CharAction act;
        bool clip = false;
        switch (e->kind) {
        case EX_REST: rest_cycle(e, T, &p); break;
        case EX_REST_BACK: back_cycle(e, T, &p); break;
        case EX_MOVE: move_cycle(e, T, &p); break;
        case EX_CLIP:
            clip_cycle(e, T, &act);
            clip = true;
            break;
        default: {
            walk_step(e, dt);
            const Player *w = &e->walker;
            p.pos = w->pos;
            p.heading = w->heading;
            p.phase = w->phase;
            p.walk = w->walk;
            p.run = w->run;
            p.speed = w->speed;
            p.lean_fwd = w->lean_fwd;
            p.lean_side = w->lean_side;
            p.look = w->look;
            break;
        }
        }
        M4 *b = s->bones + (size_t)i * RENDER_MAX_BONES;
        character_pose(&p, clip ? &act : NULL, b);
        b[RENDER_LOOK_SLOT] = (M4){ 0 };
        items[n++] = (DrawItem){ .mesh = &m->body[e->body], .bones = b };
        if (e->hair) items[n++] = (DrawItem){ .mesh = &m->gear[e->body * GEAR_ROW + e->hair], .bones = b };
        if (e->weapon) items[n++] = (DrawItem){ .mesh = &m->weapon[weapon_mesh((u8)(e->weapon - 1), Q_SOUND)], .bones = b };
    }
    return n;
}

u32 showhall_lights(f32 time, PointLight *out, u32 max)
{
    u32 n = 0;
    for (u32 k = 0; k < LAMP_N && n < max; k++) {
        const Lamp *l = &g_lamp[k];
        f32 on = 1.0f;
        // Гудит и временами гаснет на долю секунды
        if (k == LAMP_FLICKER) {
            f32 f = fmodf(time * 0.37f, 1.0f);
            on = f < 0.04f ? 0.15f : (f > 0.52f && f < 0.55f ? 0.4f : 0.92f + 0.08f * sinf(time * 50.0f));
        }
        out[n++] = (PointLight){ .pos = v3_add(l->pos, v3(0, -0.05f, 0)), .radius = l->radius,
                                 .color = v3_mul(mat_c(g_mat.lamp_sodium), 7.0f * l->power * on), .cone = 0.35f,
                                 .dir = v3(0, -1, 0), .glow = 0.0009f };
    }
    return n;
}

void showhall_room(RoomDark *r)
{
    *r = (RoomDark){ .on = true, .lo = v3(IX0, -0.5f, IZ0), .hi = v3(IX1, RB, IZ1),
                     .door = v3(0.5f * (DOOR_X0 + DOOR_X1), 1.4f, IZ1), .door_reach = 2.2f,
                     .fill = v3(0.016f, 0.012f, 0.0075f) };
}

V3 showhall_clamp_eye(V3 target, V3 eye)
{
    if (target.x < IX0 || target.x > IX1 || target.z < IZ0 || target.z > IZ1) return eye;
    const f32 m = 0.35f;
    eye.x = clampf(eye.x, IX0 + m, IX1 - m);
    eye.z = clampf(eye.z, IZ0 + m, IZ1 - m);
    eye.y = fminf(eye.y, BEAM_Y - m);
    return eye;
}

static bool project(M4 vp, V3 p, f32 *sx, f32 *sy)
{
    const f32 *m = vp.m;
    f32 x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    f32 y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    f32 w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w < 0.1f) return false;
    *sx = (x / w * 0.5f + 0.5f) * ui_width();
    *sy = (0.5f - y / w * 0.5f) * ui_height();
    return true;
}

void showhall_labels(const ShowHall *s, M4 view_proj, V3 eye)
{
    if (!s->posed || eye.x < X0 || eye.x > X1 || eye.z < Z0 || eye.z > Z1) return;
    for (u32 i = 0; i < s->count; i++) {
        const Exhibit *e = &s->ex[i];
        V3 at = e->kind == EX_WALK ? e->walker.pos : e->pos;
        V3 d = v3_sub(at, eye);
        // Дальние и с другого этажа не подписываются: подписи не лезут друг на друга
        if (v3_dot(d, d) > 16.0f * 16.0f || fabsf(at.y - fmaxf(eye.y - 2.5f, FY)) > 3.5f) continue;
        f32 sx, sy;
        if (!project(view_proj, v3_add(at, v3(0, 2.15f, 0)), &sx, &sy)) continue;
        f32 w = fmaxf(ui_text_width(e->line[0]), ui_text_width(e->line[1])), lh = ui_line();
        f32 x = floorf(sx - w * 0.5f), y = floorf(sy - 2.0f * lh);
        ui_rect(x - 4, y - 3, w + 8, 2.0f * lh + 5, UI_RGBA(10, 10, 9, 190));
        ui_text(x, y, UI_RGBA(214, 200, 150, 255), e->line[0]);
        ui_text(x, y + lh, UI_RGBA(142, 214, 150, 255), e->line[1]);
    }
}
