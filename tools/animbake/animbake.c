// animbake — клипы в data/anims/*.anim (docs/animations.md, «Клипы в файлах»). Офлайн, без рендера.
//   animbake write [data]   записать все клипы из keys.c
//   animbake check [data]   файлы совпадают с keys.c байт в байт, хеш поз всех клипов равен эталону
//                           tools/animbake/poses.hash (правка позы — пересчитать: animbake hash > poses.hash)
//   animbake hash [data]    хеш поз: все клипы × оружие × варианты действия × 16 точек по времени
#include <stdio.h>
#include <string.h>

#include "../../src/core/log.h"
#include "../../src/defs/defs.h"
#include "../../src/sim/anim.h"
#include "keys.h"

static bool write_clips(const char *data)
{
    for (u32 c = 0; c < AC_COUNT; c++) {
        char path[512];
        snprintf(path, sizeof path, "%s/anims/%s.anim", data, g_anim_clip_file[c]);
        FILE *f = fopen(path, "wb");
        if (!f) {
            fprintf(stderr, "animbake: не открыть %s (есть ли папка %s/anims?)\n", path, data);
            return false;
        }
        u32 kind = g_anim_clip_kind[c];
        u32 head[4] = { ANIM_FILE_VERSION, kind, g_anim_key_floats[kind], g_bake_clip[c].n };
        bool ok = fwrite("AANM", 4, 1, f) == 1 && fwrite(head, sizeof head, 1, f) == 1 &&
                  fwrite(g_bake_clip[c].key, sizeof(f32) * head[2], head[3], f) == head[3];
        ok = fclose(f) == 0 && ok;
        if (!ok) {
            fprintf(stderr, "animbake: не записан %s\n", path);
            return false;
        }
    }
    printf("animbake: клипов %u записано в %s/anims\n", AC_COUNT, data);
    return true;
}

// FNV-1a по байтам
static u64 g_h = 1469598103934665603ull;
static void mix(const void *p, size_t n)
{
    const u8 *c = p;
    for (size_t i = 0; i < n; i++) g_h = (g_h ^ c[i]) * 1099511628211ull;
}

// Позы всех клипов: оружие нет и три первых, 8 вариантов действия, 16 точек по времени; плюс капсулы стойки
static u64 pose_hash(void)
{
    g_h = 1469598103934665603ull;
    anim_bake_hit_guard();
    mix(g_body_hit_guard, sizeof g_body_hit_guard);
    M4 out[CHAR_BONES];
    for (int wpn = 0; wpn <= 3; wpn++)
        for (int clip = 0; clip <= CLIP_PICKUP; clip++)
            for (int var = 0; var < 8; var++)
                for (int k = 0; k < 16; k++) {
                    f32 t = (f32)k / 15.0f;
                    CharPose p = { .pos = v3(1, 0, 2), .heading = 0.3f, .phase = t, .walk = (var & 1) ? 1.0f : 0.0f,
                                   .run = (var & 2) ? 1.0f : 0.0f, .speed = (var & 1) ? 1.4f + 3.0f * (f32)((var >> 1) & 1) : 0.0f,
                                   .time = 3.0f + 7.0f * t, .seed = 0.37f, .height = 1.03f, .weapon = (u8)wpn,
                                   .torch = (var & 4) ? 1.0f : 0.0f, .rest = (u8)(var % REST_COUNT), .rest_t = t * 2.0f };
                    CharAction act = { .guard = (var & 1) ? 1.0f : 0.0f, .attack = (u8)(var % 3), .swing = (var & 4) ? 3.0f * t : 0.0f,
                                       .block = var == 2 ? t : 0.0f, .dodge = var == 3 ? t : 0.0f, .dodge_kind = (u8)(var % 3),
                                       .dodge_side = 1.0f, .stagger = var == 5 ? t : 0.0f,
                                       .wield = var == 6 ? t : (wpn ? 1.0f : 0.0f), .clip = (u8)clip,
                                       .clip_t = clip == CLIP_LIE || clip == CLIP_CRAWL ? 3.0f * t : t, .prone = var & 1,
                                       .side = (i8)((var >> 1) % 3 - 1), .fall_dir = (u8)(var % 4), .fade = 1.0f,
                                       .limp = var == 7 ? 1.0f : 0.0f };
                    character_pose(&p, clip || wpn || var ? &act : NULL, out);
                    mix(out, sizeof out);
                }
    return g_h;
}

int main(int argc, char **argv)
{
    const char *cmd = argc > 1 ? argv[1] : "", *data = argc > 2 ? argv[2] : "data";
    if (!strcmp(cmd, "write")) return write_clips(data) ? 0 : 1;
    bool check = !strcmp(cmd, "check");
    if (!check && strcmp(cmd, "hash")) {
        fprintf(stderr, "animbake write|check|hash [data]\n");
        return 2;
    }
    // Позы считает код игры: описания (оружие), тело, клипы из файлов
    static Arena defs, clips;
    char defs_dir[600], none[600];
    snprintf(defs_dir, sizeof defs_dir, "%s/defs", data);
    snprintf(none, sizeof none, "%s/anims/none", data);  // без модов
    if (!arena_init(&defs, DEFS_MEM) || !arena_init(&clips, ANIM_MEM) || !defs_load(&defs, defs_dir, none) ||
        !body_load(data) || !anim_clips_load(&clips, data))
        return 1;
    if (check)
        for (u32 c = 0; c < AC_COUNT; c++) {
            u32 n = g_anim_key_floats[g_anim_clip_kind[c]] * g_bake_clip[c].n;
            if (g_anim_clip[c].n != g_bake_clip[c].n || memcmp(g_anim_clip[c].key, g_bake_clip[c].key, sizeof(f32) * n)) {
                fprintf(stderr, "animbake: %s.anim не совпадает с keys.c — make anims\n", g_anim_clip_file[c]);
                return 1;
            }
        }
    u64 h = pose_hash();
    if (!check) {
        printf("%016llx\n", (unsigned long long)h);
        return 0;
    }
    char want[32] = { 0 };
    FILE *f = fopen("tools/animbake/poses.hash", "rb");
    if (!f || !fgets(want, sizeof want, f)) {
        fprintf(stderr, "animbake: нет tools/animbake/poses.hash\n");
        if (f) fclose(f);
        return 1;
    }
    fclose(f);
    char got[32];
    snprintf(got, sizeof got, "%016llx", (unsigned long long)h);
    if (strncmp(want, got, 16)) {
        fprintf(stderr, "animbake: хеш поз %s, эталон %.16s — позы изменились\n", got, want);
        return 1;
    }
    printf("animbake: клипов %u, файлы совпадают с keys.c, хеш поз %s как эталон\n", AC_COUNT, got);
    return 0;
}
