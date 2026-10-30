#include "sfx.h"

#include <string.h>

#include "../audio/audio.h"
#include "../world/map.h"
#include "../world/valley.h"
#include "testroom.h"
#include "town.h"

#define STEP_MIN_SPEED 0.25f
// Пол зала (как в testroom_ground) и плита арены: бетон
#define HALL_X 32.0f
#define HALL_Z0 -28.0f
#define HALL_Z1 24.0f
#define ARENA_SLAB_R 8.5f

// Броня, по которой удар звенит железом
static bool armor_metal(u8 a) { return g_armors[a].metal; }

static bool in_hall(f32 x, f32 z) { return fabsf(x) <= HALL_X && z >= HALL_Z0 && z <= HALL_Z1; }

static u8 surface(const WorldSound *s, f32 x, f32 z)
{
    if (map_valley(x, z) > 0.5f) {
        f32 lx = x - VALLEY_ORIGIN.x, lz = z - VALLEY_ORIGIN.z;
        return valley_river_dist(lx, lz) < valley_river_w(lz) ? SFX_STEP_WATER : SFX_STEP_GRASS;
    }
    f32 dx = x - s->arena_center.x, dz = z - s->arena_center.z;
    if (in_hall(x, z) || dx * dx + dz * dz < ARENA_SLAB_R * ARENA_SLAB_R) return SFX_STEP_CONCRETE;
    // Мостовая посёлка
    if (town_ground(x, z, -1e9f) > -1e8f) return SFX_STEP_CONCRETE;
    return SFX_STEP_SAND;
}

static f32 dist2(V3 a, V3 b)
{
    V3 d = v3_sub(a, b);
    return v3_dot(d, d);
}

// Шаг — когда фаза проходит 0 (левая нога) или 0.5 (правая)
static void step(WorldSound *s, f32 *prev, f32 phase, f32 walk, f32 speed, V3 pos)
{
    f32 p0 = *prev;
    *prev = phase;
    if (walk < 0.3f || speed < STEP_MIN_SPEED) return;
    bool cross = phase < p0 || (p0 < 0.5f && phase >= 0.5f);
    if (!cross) return;
    if (dist2(pos, s->lis) > 22.0f * 22.0f) return;
    // Бег громче шага, тяжёлый шаг ниже
    f32 run = clampf((speed - 1.6f) / 3.2f, 0.0f, 1.0f);
    audio_play(surface(s, pos.x, pos.z), pos, 0.45f + 0.55f * run, 1.0f - 0.08f * run, 0.0f);
}

static void sfx_on_events(void *user, const CombatWorld *w);

void sfx_init(WorldSound *s, Field *field, ArenaSession *arena, V3 arena_center, GroundFn ground)
{
    memset(s, 0, sizeof *s);
    s->arena_center = arena_center;
    s->field.ground = ground;
    s->field.field = field;
    s->arena.origin = arena_center;
    s->arena_bout = 0xFFFFFFFFu;
    field->w.sink = sfx_on_events;
    field->w.sink_user = &s->field;
    arena->w.sink = sfx_on_events;
    arena->w.sink_user = &s->arena;
}

void sfx_listener(WorldSound *s, V3 eye, V3 target, f32 yaw)
{
    // Слушатель между точкой взгляда и камерой: приблизил — услышал ближе
    V3 lis = v3_add(target, v3_mul(v3_sub(eye, target), 0.35f));
    lis.y = fmaxf(lis.y, target.y + 1.0f);
    s->lis = lis;
    f32 zoom = clampf(v3_len(v3_sub(eye, target)) / 60.0f, 0.0f, 1.0f);
    AudioScene sc;
    if (map_valley(lis.x, lis.z) > 0.5f) {
        // Долина: ветер тише, гула почти нет, эхо слабое, вода рядом
        sc = (AudioScene){ .wind = 0.35f + 0.2f * zoom, .hum = 0.12f, .room = 0.18f, .far = 0.3f, .river = true, .green = true };
        V3 o = VALLEY_ORIGIN;
        f32 lz = lis.z - o.z, rx = valley_river_x(lz), w = valley_river_w(lz), dx = lis.x - o.x - rx;
        sc.river_pos = v3_add(o, v3(rx + (dx < 0.0f ? -1.0f : 1.0f) * fminf(w, fabsf(dx)), valley_water_y(lz), lz));
    } else if (in_hall(target.x, target.z)) {
        // Под сводом зала: ветер глуше, эхо от бетона сильнее
        sc = (AudioScene){ .wind = 0.5f + 0.3f * zoom, .hum = 1.0f, .room = 0.85f, .far = 0.8f };
    } else {
        sc = (AudioScene){ .wind = 0.85f + 0.35f * zoom, .hum = 1.0f, .room = 0.55f, .far = 1.0f };
    }
    audio_listener(lis, v3(cosf(yaw), 0.0f, -sinf(yaw)), &sc);
}

static const WeaponDef *weapon_of(const CombatWorld *w, u32 i)
{
    return w->disarmed[i] || w->weapon[i] >= g_weapon_count ? NULL : &g_weapons[w->weapon[i]];
}

static bool weapon_heavy(const WeaponDef *d) { return d && (d->wclass == WC_HEAVY || d->wclass == WC_POLEARM || d->weight >= 6.5f); }

static u32 hash32(u32 x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static f32 roll01(SfxFight *f) { return (f32)(hash32(++f->roll * 0x9E3779B9u) >> 8) * (1.0f / 16777216.0f); }

// У каждого бойца своя высота оружия (±7%): два одинаковых секача звенят по-разному
static f32 own_pitch(const SfxFight *f, u32 i) { return 0.93f + 0.14f * (f32)(hash32((i + 1) * 0x85EBCA6Bu ^ f->salt) & 1023u) / 1023.0f; }

// Чем оружие отвечает на удар: чем закрылись — древко багра деревянное, а бьют им крюком или пикой
static u8 weapon_voice(u32 w, bool striking)
{
    const WeaponDef *d = &g_weapons[w];
    return (u8)(SFX_WPN_RESSORA + (striking ? d->voice_strike : d->voice));
}

static bool weapon_edged(const WeaponDef *d) { return d && (d->wclass == WC_BLADE || d->wclass == WC_CLEAVER); }

static V3 fighter_pos(const SfxFight *f, const CombatWorld *w, u32 i)
{
    f32 x = w->x[i] + f->origin.x, z = w->z[i] + f->origin.z;
    f32 y = f->ground ? f->ground(x, z) : f->origin.y;
    return v3(x, y + 1.0f, z);
}

static void fall(SfxFight *f, const CombatWorld *w, u32 i, f32 delay)
{
    if (i >= FIELD_MAX || f->down[i]) return;
    f->down[i] = 1;
    audio_play(SFX_BODYFALL, fighter_pos(f, w, i), 1.0f, clampf(75.0f / fmaxf(w->mass[i], 30.0f), 0.85f, 1.2f), delay);
}

// Журнал боя: замах, блок, попадание, падения. Вызывается до очистки журнала
static void sfx_on_events(void *user, const CombatWorld *w)
{
    SfxFight *f = user;
    for (u32 k = 0; k < w->ev_count; k++) {
        const CombatEvent *e = &w->ev[k];
        if (e->a >= w->count || e->b >= w->count) continue;
        const WeaponDef *wa = weapon_of(w, e->a), *wb = weapon_of(w, e->b);
        switch (e->kind) {
        case EV_ATTACK:
        case EV_COUNTER: {
            // Свист к концу замаха: громче всего в миг удара (пик звука ~0.2 с от начала)
            f32 delay = fmaxf(0.0f, w->timer[e->a] - 0.2f);
            f32 pitch = (wa ? clampf(1.2f - 0.03f * wa->weight, 0.8f, 1.15f) : 1.3f) * own_pitch(f, e->a);
            audio_play(weapon_heavy(wa) ? SFX_SWISH_HEAVY : SFX_SWISH, fighter_pos(f, w, e->a), wa ? 1.0f : 0.4f, pitch, delay);
            break;
        }
        case EV_HIT: {
            V3 p = fighter_pos(f, w, e->b);
            f32 dmg = e->cut + e->blunt + e->pierce;
            f32 g = clampf(0.55f + dmg / 40.0f, 0.55f, 1.3f);
            bool metal = false;
            for (u32 sl = 0; sl < SLOT_COUNT && !metal; sl++) {
                u8 a = w->armor[sl][e->b];
                metal = a != ARMOR_NONE && armor_metal(a) && e->part < PART_COUNT && g_armors[a].cover[e->part] >= 0.5f;
            }
            f32 pa = own_pitch(f, e->a);
            bool thrust = e->pierce > e->cut && e->pierce > e->blunt;
            if (metal) {
                // Броня звенит, и в ней слышно, чем ударили
                audio_play(SFX_HIT_ARMOR, p, g, wa ? clampf(1.15f - 0.02f * wa->weight, 0.85f, 1.1f) : 0.9f, 0.0f);
                if (wa) audio_play(weapon_voice(w->weapon[e->a], true), p, g * 0.35f, pa, 0.002f);
                audio_play(SFX_HIT_BLUNT, p, g * 0.4f, 1.0f, 0.0f);
            } else if (!wa) {
                audio_play(SFX_HIT_BLUNT, p, g * 0.6f, 1.15f, 0.0f);
            } else {
                switch (wa->wclass) {
                case WC_BLADE: audio_play(thrust ? SFX_HIT_PIERCE : SFX_HIT_CUT, p, g, pa, 0.0f); break;
                case WC_CLEAVER: audio_play(SFX_HIT_CHOP, p, g, pa, 0.0f); break;
                case WC_BLUNT:
                    // Арматура: тупой удар, и бетон с прутьями брякнули
                    audio_play(SFX_HIT_BLUNT, p, g, clampf(1.1f - 0.015f * wa->weight, 0.85f, 1.05f), 0.0f);
                    audio_play(SFX_WPN_ARMATURA, p, g * 0.3f, pa, 0.003f);
                    break;
                case WC_POLEARM:
                    // Пикой — укол, размахом — древком плашмя
                    if (thrust) audio_play(SFX_HIT_PIERCE, p, g, pa * 0.95f, 0.0f);
                    else audio_play(SFX_HIT_BLUNT, p, g * 0.85f, 1.1f * pa, 0.0f);
                    break;
                default: audio_play(SFX_HIT_CRUSH, p, g, pa, 0.0f); break;
                }
            }
            if (e->flags & EVF_FRACTURE) audio_play(SFX_CRACK, p, 1.0f, 1.0f, 0.01f);
            // Отсечённое упало на землю
            if (e->flags & EVF_SEVER) audio_play(SFX_BODYFALL, p, 0.35f, 1.6f, 0.4f);
            break;
        }
        case EV_BLOCK: {
            V3 p = fighter_pos(f, w, e->b);
            if (!wb) {
                // Закрылся руками
                audio_play(SFX_HIT_BLUNT, p, 0.5f, 1.2f, 0.0f);
                break;
            }
            // Два голоса: громче то, чем закрылись (по нему ударили), тише то, чем бьют.
            // Высота у каждого своя; сбили стойку — удар глубже и ниже
            bool stag = e->flags & EVF_STAGGER;
            f32 force = wa ? clampf(wa->weight / 8.0f, 0.3f, 1.4f) : 0.3f;
            f32 gb = (stag ? 1.15f : 0.85f) * (0.75f + 0.25f * force);
            audio_play(weapon_voice(w->weapon[e->b], false), p, gb, own_pitch(f, e->b) * (stag ? 0.96f : 1.0f), 0.0f);
            if (wa) {
                audio_play(weapon_voice(w->weapon[e->a], true), p, gb * 0.6f, own_pitch(f, e->a), 0.003f);
                // Лезвие по лезвию иногда скользит, а не просто стучит
                if (weapon_edged(wa) && weapon_edged(wb) && roll01(f) < 0.45f)
                    audio_play(SFX_SCRAPE, p, 0.8f, own_pitch(f, e->a), 0.012f);
            }
            break;
        }
        case EV_DODGE: audio_play(SFX_SCUFF, fighter_pos(f, w, e->b), 0.8f, 1.0f, 0.0f); break;
        // Удар тела о землю — в миг касания в клипе падения
        case EV_KNOCKDOWN: fall(f, w, e->b, fight_anim_contact(w, e->b, e->kind)); break;
        case EV_FALL:
        case EV_KO:
        case EV_DEATH: fall(f, w, e->a, fight_anim_contact(w, e->a, e->kind)); break;
        case EV_GETUP:
            if (e->a < FIELD_MAX) f->down[e->a] = 0;
            audio_play(SFX_SCUFF, fighter_pos(f, w, e->a), 0.7f, 0.9f, 0.0f);
            break;
        default: break;
        }
    }
}

// Боец в виде боя: шаги, достал или убрал оружие, выронил
// foot — где стоит (высота земли уже известна виду), без поиска высоты каждый кадр
static void fighter(WorldSound *s, SfxFight *f, u32 i, const FightAnim *a, V3 foot)
{
    V3 p = v3_add(foot, v3(0, 1.0f, 0));
    step(s, &f->phase[i], a->phase, a->walk, a->speed, foot);
    f32 w0 = f->wield[i];
    f->wield[i] = a->wield;
    bool near = dist2(p, s->lis) < 30.0f * 30.0f;
    if (a->dropped && !f->dropped[i]) {
        V3 dp = v3(a->drop_pos.x + f->origin.x, p.y - 1.0f, a->drop_pos.z + f->origin.z);
        audio_play(SFX_DROP_METAL, dp, 1.0f, 1.0f, 0.4f);  // падает из руки 0.4 с
    } else if (near && !a->dropped) {
        if (w0 < 0.3f && a->wield >= 0.3f) audio_play(SFX_DRAW, p, 1.0f, 1.0f, 0.0f);
        if (w0 > 0.7f && a->wield <= 0.7f) audio_play(SFX_SHEATH, p, 1.0f, 1.0f, 0.0f);
    }
    f->dropped[i] = a->dropped;
}

void sfx_field(WorldSound *s, const Field *f, const Player *view)
{
    const CombatWorld *w = &f->w;
    SfxFight *sf = &s->field;
    for (u32 i = 0; i < w->count && i < FIELD_MAX; i++) {
        const Player *p = &view[i];
        if (field_awake(f, i)) {
            if (!sf->awake[i]) {
                // Проснулся: оружие ещё в ножнах, падений не было
                sf->awake[i] = 1;
                sf->down[i] = fs_lying(w->state[i]);
                sf->dropped[i] = 0;
                sf->wield[i] = 0.0f;
                sf->phase[i] = f->anim[i].phase;
            }
            fighter(s, sf, i, &f->anim[i], p->pos);
            continue;
        }
        if (sf->awake[i]) {
            // Бой стих: убрал оружие
            sf->awake[i] = 0;
            if (sf->wield[i] > 0.7f && !sf->dropped[i] && w->state[i] != FS_DEAD && dist2(p->pos, s->lis) < 30.0f * 30.0f)
                audio_play(SFX_SHEATH, v3_add(p->pos, v3(0, 1, 0)), 1.0f, 1.0f, 0.3f);
            sf->phase[i] = p->phase;
        }
        step(s, &sf->phase[i], p->phase, p->walk, p->speed, p->pos);
    }
}

void sfx_arena(WorldSound *s, const ArenaView *v, const ArenaSession *a)
{
    SfxFight *f = &s->arena;
    const CombatWorld *w = &a->w;
    if (!v->in_bout) return;
    if (a->bout != s->arena_bout) {
        // Новый бой: в слотах другие люди
        s->arena_bout = a->bout;
        f->salt = hash32(a->bout + 1u);
        memset(f->down, 0, sizeof f->down);
        memset(f->dropped, 0, sizeof f->dropped);
        memset(f->wield, 0, sizeof f->wield);
        for (u32 i = 0; i < ARENA_MAX_BOUT; i++) f->phase[i] = v->fa[i].phase;
    }
    f->origin = v3(s->arena_center.x, v->floor_y, s->arena_center.z);
    for (u32 i = 0; i < w->count && i < ARENA_MAX_BOUT; i++)
        fighter(s, f, i, &v->fa[i], v3(v->fa[i].disp_x + f->origin.x, f->origin.y, v->fa[i].disp_z + f->origin.z));
}
