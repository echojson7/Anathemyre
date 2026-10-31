#include "figures.h"
#include "../content/weapon.h"
#include "../defs/defs.h"
#include "lights.h"

#include <stdio.h>
#include <string.h>

#include "../core/jobs.h"
#include "../core/prof.h"

// Пороги детализации фигуры: диаметр её сферы на экране, пикселей. Больше первого — уровень 0 (16 тыс. треугольников).
// При 1440 строк и CAM_FOV это примерно 6, 13, 38 и 90 м: вплотную и в редакторе — 0, у камеры — 1, обычный вид — 2,
// дальше — 3 и 4. На 1080 строк расстояния на четверть короче
static const f32 g_char_lod_px[CHAR_LODS - 1] = { 420.0f, 200.0f, 70.0f, 30.0f };

// Сетка и её грубые уровни из data/meshes/<stem>{,_lod1.._lod4}.mesh; пороги g_char_lod_px × scale
// Место в ряду сеток тела занято: вещь из описаний или причёска этого тела
static bool gear_slot_used(u32 body, u32 g)
{
    return g < GEAR_CAP ? g < g_gear_count : g_hair_file[body][g - GEAR_CAP] != NULL;
}

static bool load_lods(GpuMesh *full, GpuMesh *lods, MeshBuilder *mb, Arena *perm, const char *data_dir, const char *stem, f32 scale)
{
    size_t mark = perm->used;
    for (u32 l = 0; l < CHAR_LODS; l++) {
        char path[700], suf[16] = "";
        if (l) snprintf(suf, sizeof suf, "_lod%u", l);
        snprintf(path, sizeof path, "%s/meshes/%s%s.mesh", data_dir, stem, suf);
        if (!mb_load(mb, perm, path)) return false;
        GpuMesh *gm = l ? &lods[l - 1] : full;
        gpu_mesh_upload_pooled(gm, mb);
        if (l) gpu_mesh_set_lod(l == 1 ? full : &lods[l - 2], gm, g_char_lod_px[l - 1] * scale);
        perm->used = mark;
    }
    return true;
}

// Метки под ногами: бледно-зелёный крест цели приказа; кольца — свой выбранный бледно-зелёное,
// осматриваемый чужой — охра, цель атаки — красное
static void marks_build(CharMeshes *m, MeshBuilder *mb, Arena *perm)
{
    size_t mark = perm->used;
    mb_init(mb, perm, 200, 600);
    mb_set(mb, v3(0.35f, 0.9f, 0.45f), MAT_EMISSIVE, 0);
    mb_box(mb, m4_mul(m4_translate(v3(0, 0.02f, 0)), m4_mul(m4_rot_y(PI_F * 0.25f), m4_scale(v3(0.5f, 0.02f, 0.06f)))));
    mb_box(mb, m4_mul(m4_translate(v3(0, 0.02f, 0)), m4_mul(m4_rot_y(-PI_F * 0.25f), m4_scale(v3(0.5f, 0.02f, 0.06f)))));
    gpu_mesh_upload_pooled(&m->marker, mb);
    perm->used = mark;
    static const V3 ring_color[3] = { { 0.1f, 0.42f, 0.16f }, { 0.5f, 0.3f, 0.08f }, { 0.62f, 0.06f, 0.04f } };
    for (u32 k = 0; k < 3; k++) {
        mb_init(mb, perm, 600, 1800);
        mb_set(mb, ring_color[k], MAT_EMISSIVE, 0);
        for (u32 j = 0; j < 20; j++) {
            f32 a = (f32)j * (2.0f * PI_F / 20.0f);
            mb_box(mb, m4_mul(m4_translate(v3(sinf(a) * 0.48f, 0.03f, cosf(a) * 0.48f)),
                              m4_mul(m4_rot_y(a), m4_scale(v3(0.09f, 0.015f, 0.025f)))));
        }
        gpu_mesh_upload_pooled(&m->ring[k], mb);
        perm->used = mark;
    }
}

void figures_bones(Figures *f, Arena *perm)
{
    memset(f, 0, sizeof *f);
    for (u32 i = 0; i < PERSONS_MAX; i++) f->age[i] = -1.0f;
    f->bones = ARENA_PUSH_ARRAY(perm, M4, (size_t)PERSONS_MAX * RENDER_MAX_BONES);
    f->arena = ARENA_PUSH_ARRAY(perm, M4, (size_t)RING_BONE_SETS * RENDER_MAX_BONES);
    f->marks = ARENA_PUSH_ARRAY(perm, M4, (size_t)MARK_SETS * RENDER_MAX_BONES);
    f->portrait = ARENA_PUSH_ARRAY(perm, M4, 2 * RENDER_MAX_BONES);
}

bool figures_init(Figures *f, CharMeshes *m, MeshBuilder *mb, Arena *perm, const char *data_dir)
{
    figures_bones(f, perm);

    // Одежда и причёски, затем тела; грубые уровни лежат отдельно: остальной код видит только полный уровень.
    // Порядок загрузки — порядок отрисовки: одежда раньше тел (одежда после тел — проход сцены на ~13 % дольше)
    size_t mark = perm->used;
    for (u32 k = 0; k < BODY_KIND_COUNT; k++)
        for (u32 g = 0; g < GEAR_ROW; g++) {
            if (!gear_slot_used(k, g)) continue;
            char stem[96];
            if (g < GEAR_CAP) snprintf(stem, sizeof stem, "gear/%s_%s", g_body_file[k], g_gear_file[g]);
            else snprintf(stem, sizeof stem, "hair/%s_%s", g_body_file[k], g_hair_file[k][g - GEAR_CAP]);
            if (!load_lods(&m->gear[k * GEAR_ROW + g], &m->gear_lod[(k * GEAR_ROW + g) * (CHAR_LODS - 1)], mb, perm,
                           data_dir, stem, 0.0f))
                return false;
        }
    for (u32 k = 0; k < BODY_KIND_COUNT; k++) {
        if (!load_lods(&m->body[k], m->body_lod[k], mb, perm, data_dir, g_body_file[k], 1.0f)) return false;
        // Вещь переключает LOD на той же дальности, что и тело: порог по отношению сфер
        for (u32 g = 0; g < GEAR_ROW; g++) {
            if (!gear_slot_used(k, g)) continue;
            GpuMesh *gm = &m->gear[k * GEAR_ROW + g], *gl = &m->gear_lod[(k * GEAR_ROW + g) * (CHAR_LODS - 1)];
            f32 ratio = gm->bound.w / m->body[k].bound.w;
            for (u32 l = 0; l + 1 < CHAR_LODS; l++) (l ? &gl[l - 1] : gm)->lod_px = g_char_lod_px[l] * ratio;
        }
    }
    for (u32 k = 0; k < g_weapon_count * WEAPON_LOOKS; k++) {
        mb_init(mb, perm, 8000, 24000);
        weapon_build(mb, (u8)(k / WEAPON_LOOKS), (u8)(k % WEAPON_LOOKS), CHAR_BONE_WEAPON);
        gpu_mesh_upload_pooled(&m->weapon[k], mb);
        perm->used = mark;
    }
    // Факел: древко и пламя отдельно, пламя рисуется, только пока горит
    for (u32 k = 0; k < 2; k++) {
        mb_init(mb, perm, 2000, 6000);
        torch_build(mb, k == 1);
        gpu_mesh_upload_pooled(&m->torch[k], mb);
        perm->used = mark;
    }
    marks_build(m, mb, perm);
    render_pool_log();
    return true;
}

// Кому в этом кадре считать позу. Фигура не нужна кадру, если ни она, ни её тень на земле не задевают
// пирамиду видимости (капсула от тела до конца тени головы): тогда ни позы, ни отрисовки.
// Видимым вдали вне боя поза обновляется реже: от 20 м 120 раз в секунду, от 45 м 60 — глазу не видно
typedef struct PoseCull {
    V4  plane[6];   // нормаль внутрь xyz, смещение w (нормированы)
    V3  eye;
    V3  shadow;     // от ног до конца тени головы по земле
    f32 dt;         // реальное время кадра
} PoseCull;

static PoseCull pose_cull_make(M4 vp, V3 eye, V3 light_dir, f32 dt)
{
    PoseCull pc = { .eye = eye, .dt = dt };
    const f32 *m = vp.m;
    for (int i = 0; i < 6; i++) {
        int row = i >> 1;
        f32 s = (i & 1) ? -1.0f : 1.0f;
        V4 p = { m[3] + s * m[row], m[7] + s * m[4 + row], m[11] + s * m[8 + row], m[15] + s * m[12 + row] };
        f32 k = 1.0f / sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
        pc.plane[i] = (V4){ p.x * k, p.y * k, p.z * k, p.w * k };
    }
    // Тень головы (1.8 м) против света; у низкого солнца — не дальше 40 м
    V3 L = v3_norm(light_dir);
    pc.shadow = v3_mul(v3(-L.x, 0.0f, -L.z), 1.8f / fmaxf(L.y, 0.02f));
    f32 len = v3_len(pc.shadow);
    if (len > 40.0f) pc.shadow = v3_mul(pc.shadow, 40.0f / len);
    return pc;
}

// false — фигура кадру не нужна. Иначе *pose — пересчитать позу сейчас. r — радиус тела с оружием
// (или света факела); every — поза каждый кадр (в бою); *age < 0 — не было в кадре
static bool pose_cull(const PoseCull *pc, V3 pos, f32 r, bool every, f32 *age, bool *pose)
{
    V3 a = v3(pos.x, pos.y + 0.9f, pos.z), b = v3_add(pos, pc->shadow);
    for (u32 i = 0; i < 6; i++) {
        V4 p = pc->plane[i];
        if (p.x * a.x + p.y * a.y + p.z * a.z + p.w < -r && p.x * b.x + p.y * b.y + p.z * b.z + p.w < -r) {
            *age = -1.0f;
            return false;
        }
    }
    f32 dist = v3_len(v3_sub(pos, pc->eye));
    f32 period = every || dist < 20.0f ? 0.0f : dist < 45.0f ? 1.0f / 120.0f : 1.0f / 60.0f;
    *pose = *age < 0.0f || (*age += pc->dt) >= period;
    if (*pose) *age = 0.0f;
    return true;
}

// Факел зажигают с темнотой, каждый в свой момент: 0 погашен (рука опущена) .. 1 горит
static f32 torch_lit(bool has, f32 seed, f32 night)
{
    if (!has) return 0.0f;
    f32 th = 0.22f + 0.3f * (seed * 0.618034f - floorf(seed * 0.618034f));
    f32 t = clampf((night - th) / 0.1f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Вещи поверх тела: те же кости, что у тела
static u32 push_gear(DrawItem *items, const GpuMesh *row, const u8 *gear, u32 n, const M4 *bones)
{
    for (u32 i = 0; i < n; i++) items[i] = (DrawItem){ .mesh = &row[gear[i]], .bones = bones };
    return n;
}

// Поз на один кусок работы потока: поза ~1.5–3 мкс, разбудить поток стоит столько же, сколько несколько поз
#define POSE_GRAIN 8

// Позы пачкой: каждая пишет только свой набор костей, поэтому потоки друг другу не мешают
static void pose_job(void *ctx, u32 b, u32 e)
{
    PoseJob *job = ctx;
    for (u32 i = b; i < e; i++) {
        character_pose(&job[i].p, job[i].fight ? &job[i].act : NULL, job[i].bones);
        job[i].bones[RENDER_LOOK_SLOT] = job[i].look;
    }
}

// Человек i в бою: поза от поля боя, сложение и оружие — от владельца тела
static void fight_pose(const Field *fd, u32 i, PoseJob *j, const f32 slope[2])
{
    CharPose walk = j->p;
    fight_anim_pose(&fd->anim[i], &fd->w, i, fd->alpha, v3(0, 0, 0), walk.pos.y, walk.time, &j->p, &j->act);
    j->p.seed = walk.seed;
    j->p.height = walk.height;
    j->p.width = walk.width;
    j->p.joints = walk.joints;
    if (!walk.weapon) j->p.weapon = 0;
    j->p.slope_x = slope[0];
    j->p.slope_z = slope[1];
    j->fight = true;
}

// Бойцы текущего боя арены: позы от её боя, внешность из ростера
static u32 arena_figures(Figures *f, const CharMeshes *m, const ArenaRing *v, const ArenaSession *s, f32 time,
                         DrawItem *items)
{
    const CombatWorld *w = &s->w;
    f32 alpha = v->alpha;
    u32 n = 0, set = 0;
    if (!v->in_bout) return 0;
    for (u32 i = 0; i < w->count && i < ARENA_MAX_BOUT; i++) {
        u32 ri = s->idx[i], kind = ri % BODY_KIND_COUNT;
        PoseJob *j = &f->job[f->jobs++];
        bool dropped = fight_anim_pose(&v->fa[i], w, i, alpha, v->center, v->floor_y, time, &j->p, &j->act);
        j->p.joints = g_body_joints[kind];
        j->fight = true;
        M4 *b = &f->arena[(set++) * RENDER_MAX_BONES];
        j->bones = b;
        CharLook lk;
        j->look = arena_ring_look(s, ri, &lk);
        items[n++] = (DrawItem){ .mesh = &m->body[kind], .bones = b };
        u8 worn[GEAR_MAX_WORN];
        u32 nw = gear_from_armor(s->roster[ri].d.armor, worn);
        nw += look_hair(&lk, worn, nw);
        n += push_gear(items + n, m->gear + kind * GEAR_ROW, worn, nw, b);
        // Выроненное лежит вещью на земле: его рисует физика
        if (!dropped) items[n++] = (DrawItem){ .mesh = &m->weapon[weapon_mesh(w->weapon[i], w->weapon_q[i])], .bones = b };
    }
    return n;
}

void figures_view(Figures *f, const Persons *ps, const Field *fd, f32 alpha, GroundFn ground)
{
    for (u32 i = 0; i < ps->count; i++) {
        Player b = person_body(ps, i);
        if (field_awake(fd, i)) {
            // В бою: где его показывает вид боя (выпады, отскоки), поворот — сглаженный тик
            f32 x = fd->anim[i].disp_x, z = fd->anim[i].disp_z, h, px, pz;
            field_pos(fd, i, alpha, &px, &pz, &h);
            f->view[i] = b;
            f->view[i].pos = v3(x, ground(x, z), z);
            f->view[i].heading = f->view[i].move_yaw = h;
            f->slope[i][0] = (ground(x + 0.4f, z) - ground(x - 0.4f, z)) * 1.25f;
            f->slope[i][1] = (ground(x, z + 0.4f) - ground(x, z - 0.4f)) * 1.25f;
        } else {
            player_lerp(&ps->last[i], &b, alpha, &f->view[i]);
        }
    }
}

u32 figures_draw(Figures *f, const CharMeshes *m, const Persons *p, const Field *fd, const ArenaRing *av,
                 const ArenaSession *s, const FigureView *v, DrawItem *items, PointLight *lights, u32 *nl)
{
    PoseCull pc = pose_cull_make(v->view_proj, v->eye, v->light_dir, v->dt);
    u32 n = 0;
    f->jobs = 0;
    // Кому нужна поза и что рисовать; кости ещё не готовы, элементы ссылаются на их место
    bool lit_any = false;
    for (u32 i = 0; i < p->count; i++) {
        if (p->role[i] == ROLE_FREE) {
            f->age[i] = -1.0f;  // свет факела по нему не ищется
            continue;
        }
        const Player *b = &f->view[i];
        const CharSheet *cs = &p->sheet[i];
        f32 lit = torch_lit(p->torch[i], p->seed[i], v->night);
        bool fight = field_awake(fd, i), pose;
        // Свет факела достаёт дальше тела: с горящим факелом проверяем шире
        if (!pose_cull(&pc, b->pos, lit > 0.0f ? 8.0f : 2.5f, fight, &f->age[i], &pose)) continue;
        // Выроненное в бою лежит вещью на земле: его рисует физика
        bool armed = item_weapon(cs->equip[SLOT_WEAPON]) != COMP_NONE && !(fight && fd->w.gitem[i] != COMBAT_IN_HAND);
        u8 wid = item_weapon(cs->equip[SLOT_WEAPON]);
        u32 kind = p->variant[i];
        M4 *bones = &f->bones[(size_t)i * RENDER_MAX_BONES];
        if (pose) {
            PoseJob *j = &f->job[f->jobs++];
            j->p = (CharPose){
                .pos = b->pos, .heading = b->heading, .phase = b->phase, .walk = b->walk, .run = b->run,
                .speed = b->speed, .stride = player_stride(b), .time = v->time, .seed = p->seed[i],
                .lean_fwd = b->lean_fwd, .lean_side = b->lean_side, .look = b->look,
                .rest = b->rest, .rest_ph = b->rest_ph, .rest_t = b->rest_t,
                .height = p->height[i], .width = p->width[i], .joints = g_body_joints[kind],
                .weapon = armed ? (u8)(wid + 1) : 0, .torch = lit,
            };
            j->fight = false;
            j->bones = bones;
            j->look = p->look[i];
            if (fight) fight_pose(fd, i, j, f->slope[i]);
        }
        items[n++] = (DrawItem){ .mesh = &m->body[kind], .bones = bones };
        n += push_gear(items + n, m->gear + kind * GEAR_ROW, p->gear[i], p->ngear[i], bones);
        if (armed) items[n++] = (DrawItem){ .mesh = &m->weapon[weapon_mesh(wid, cs->equip[SLOT_WEAPON].quality)], .bones = bones };
        if (p->torch[i]) {
            items[n++] = (DrawItem){ .mesh = &m->torch[0], .bones = bones };
            if (lit >= 0.05f) {
                items[n++] = (DrawItem){ .mesh = &m->torch[1], .bones = bones, .no_shadow = true };
                lit_any = true;
            }
        }
    }
    f->posed = f->jobs;
    if (s) n += arena_figures(f, m, av, s, v->time, items + n);
    prof_zone(PZ_POSES);
    jobs_for(f->jobs, POSE_GRAIN, pose_job, f->job);
    // Свет факелов — компонент light вещи torch, по готовым костям, в том же порядке, что и люди
    u8 tl = g_items[def_item("torch")].light;
    for (u32 i = 0; lit_any && tl != COMP_NONE && i < p->count && *nl < TORCH_LIGHTS_MAX; i++) {
        f32 lit = torch_lit(p->torch[i], p->seed[i], v->night);
        if (lit < 0.05f || f->age[i] < 0.0f) continue;
        lights[(*nl)++] = lights_item(&g_lights[tl], torch_flame_pos(&f->bones[(size_t)i * RENDER_MAX_BONES]), lit, p->seed[i], v->time);
    }
    return n;
}

u32 figures_marks(Figures *f, const CharMeshes *m, const Squad *sq, const Field *fd, const V3 *look_ring, DrawItem *items)
{
    u32 n = 0, set = 0;
    for (u32 i = 0; i < sq->count; i++) {
        if (!sq->selected[i]) continue;
        const Player *pl = &f->view[squad_at(sq, i)];
        M4 *rb = &f->marks[(set++) * RENDER_MAX_BONES];
        rb[0] = m4_translate(pl->pos);
        items[n++] = (DrawItem){ .mesh = &m->ring[0], .bones = rb, .no_shadow = true };
        // Красное кольцо под целью выбранного; у нескольких одна цель — кольцо одно
        i32 t = field_target(fd, squad_at(sq, i));
        bool first = t >= 0;
        for (u32 j = 0; j < i && first; j++)
            if (sq->selected[j] && field_target(fd, squad_at(sq, j)) == t) first = false;
        if (first) {
            M4 *tb = &f->marks[(set++) * RENDER_MAX_BONES];
            tb[0] = m4_translate(f->view[t].pos);  // слот поля = место в реестре
            items[n++] = (DrawItem){ .mesh = &m->ring[2], .bones = tb, .no_shadow = true };
        }
    }
    if (sq->order_marker > 0.0f) {
        M4 *mk = &f->marks[(set++) * RENDER_MAX_BONES];
        mk[0] = m4_translate(sq->order_pos);
        items[n++] = (DrawItem){ .mesh = &m->marker, .bones = mk, .no_shadow = true };
    }
    if (look_ring) {
        M4 *rb = &f->marks[(set++) * RENDER_MAX_BONES];
        rb[0] = m4_translate(*look_ring);
        items[n++] = (DrawItem){ .mesh = &m->ring[1], .bones = rb, .no_shadow = true };
    }
    return n;
}

// ---------------------------------------------------------------- портреты

// Что сейчас нарисовано в ячейке атласа портретов: портрет неподвижен, перерисовка только при смене
typedef struct PortraitKey {
    const GpuMesh *body, *weapon;
    f32 seed, height, width;
    M4  look;
    i32 size;
    u8  gear[GEAR_MAX_WORN];
    u8  ngear;
} PortraitKey;
static PortraitKey g_portrait_key[RENDER_PORTRAIT_SLOTS];

void figures_portraits_reset(void) { memset(g_portrait_key, 0, sizeof g_portrait_key); }

// Неподвижная фигура в покое, камера по кости головы спереди, чуть сверху
static void portrait(const FrameView *base, u32 slot, i32 size, u32 kind, const GpuMesh *body, const GpuMesh *weapon, u8 wid,
                     f32 seed, f32 height, f32 width, M4 look, const GpuMesh *gear_row, const u8 *gear, u32 ngear, M4 *bones)
{
    PortraitKey key;
    memset(&key, 0, sizeof key);
    key = (PortraitKey){ body, weapon, seed, height, width, look, size, { 0 }, (u8)ngear };
    memcpy(key.gear, gear, ngear);
    if (slot >= RENDER_PORTRAIT_SLOTS || !memcmp(&g_portrait_key[slot], &key, sizeof key)) return;
    g_portrait_key[slot] = key;
    CharPose p = { .seed = seed, .height = height, .width = width, .calm = true, .weapon = weapon ? (u8)(wid + 1) : 0,
                   .joints = g_body_joints[kind] };
    character_pose(&p, NULL, bones);
    bones[RENDER_LOOK_SLOT] = look;
    DrawItem items[2 + GEAR_MAX_WORN] = { { .mesh = body, .bones = bones } };
    u32 n = 1 + push_gear(items + 1, gear_row, gear, ngear, bones);
    if (weapon) items[n++] = (DrawItem){ .mesh = weapon, .bones = bones };
    M4 hb = bones[BONE_HEAD];
    V3 head = m4_point(hb, v3(0, 1.70f, 0.03f));
    V3 fwd = v3_norm(m4_dir(hb, v3(0, 0, 1)));
    V3 up = v3_norm(m4_dir(hb, v3(0, 1, 0)));
    FrameView fv = *base;
    fv.cam_pos = v3_add(v3_add(head, v3_mul(fwd, 1.05f)), v3_mul(up, 0.1f));
    fv.view = m4_look_at(fv.cam_pos, v3_sub(head, v3_mul(up, 0.06f)), up);
    fv.proj = m4_perspective(0.55f, 1.0f, 0.05f, 20.0f);
    render_portrait(slot, size, &fv, m4_mul(fv.proj, fv.view), items, n);
}

void figures_portrait(Figures *f, const FrameView *base, const CharMeshes *m, const Persons *p, u32 i, u32 slot, i32 size,
                      bool focus)
{
    const CharSheet *cs = &p->sheet[i];
    u8 wid = item_weapon(cs->equip[SLOT_WEAPON]);
    const GpuMesh *wm = wid != COMP_NONE ? &m->weapon[weapon_mesh(wid, cs->equip[SLOT_WEAPON].quality)] : NULL;
    u32 kind = p->variant[i];
    portrait(base, slot, size, kind, &m->body[kind], wm, wid == COMP_NONE ? 0 : wid, p->seed[i], p->height[i], p->width[i],
             p->look[i], m->gear + kind * GEAR_ROW, p->gear[i], p->ngear[i], f->portrait + (focus ? RENDER_MAX_BONES : 0));
}

void figures_portrait_arena(Figures *f, const FrameView *base, const CharMeshes *m, const ArenaSession *s, u32 ri, u32 slot,
                            i32 size)
{
    const FighterDesc *d = &s->roster[ri].d;
    f32 seed = (f32)ri * 13.7f + 3.0f;
    u8 gear[GEAR_MAX_WORN];
    u32 ng = gear_from_armor(d->armor, gear), kind = ri % BODY_KIND_COUNT;
    CharLook lk;
    M4 lm = arena_ring_look(s, ri, &lk);
    ng += look_hair(&lk, gear, ng);
    portrait(base, slot, size, kind, &m->body[kind], &m->weapon[weapon_mesh(d->weapon, d->weapon_q)], d->weapon, seed,
             body_scale(d->height), 1.0f + 0.08f * sinf(seed * 3.17f + 1.0f), lm, m->gear + kind * GEAR_ROW, gear, ng,
             f->portrait + RENDER_MAX_BONES);
}
