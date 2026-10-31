#include "anim_keys.h"

#include <stdio.h>
#include <string.h>

#include "../core/log.h"
#include "action.h"
#include "items.h"

AnimClip g_anim_clip[AC_COUNT];

#define CLIP_FILE(id, file, kind, n) #file,
const char *const g_anim_clip_file[AC_COUNT] = { ANIM_CLIPS(CLIP_FILE) };
#undef CLIP_FILE
#define CLIP_KIND(id, file, kind, n) kind,
const u8 g_anim_clip_kind[AC_COUNT] = { ANIM_CLIPS(CLIP_KIND) };
#undef CLIP_KIND
#define CLIP_NEED(id, file, kind, n) n,
const u8 g_anim_clip_need[AC_COUNT] = { ANIM_CLIPS(CLIP_NEED) };
#undef CLIP_NEED
_Static_assert(ATK_COUNT == 3, "замах и удар — по ключу на AttackKind");

const u32 g_anim_key_floats[AK_COUNT] = {
    [AK_REST] = sizeof(RestK) / 4, [AK_FIGHT] = sizeof(FightK) / 4, [AK_BODY] = sizeof(BodyK) / 4, [AK_GUARD] = sizeof(GuardK) / 4,
};

#define CLIP_KEYS_MAX 64

bool anim_clips_load(Arena *mem, const char *data_dir)
{
    for (u32 c = 0; c < AC_COUNT; c++) {
        char path[512];
        snprintf(path, sizeof path, "%s/anims/%s.anim", data_dir, g_anim_clip_file[c]);
        FILE *f = fopen(path, "rb");
        if (!f) {
            log_error("КЛИПЫ: нет файла %s (собрать: make anims)", path);
            return false;
        }
        char magic[4];
        u32 head[4];  // версия, вид, f32 в ключе, ключей
        u32 kind = g_anim_clip_kind[c], fl = g_anim_key_floats[kind], need = g_anim_clip_need[c];
        bool ok = fread(magic, 4, 1, f) == 1 && fread(head, sizeof head, 1, f) == 1 && !memcmp(magic, "AANM", 4);
        if (ok && (head[0] != ANIM_FILE_VERSION || head[1] != kind || head[2] != fl || head[3] > CLIP_KEYS_MAX ||
                   (need ? head[3] != need : head[3] < 2))) {
            log_error("КЛИПЫ: %s: версия %u, вид %u, ключ %u f32, ключей %u — нужно %u, %u, %u, %s%u (пересобрать: make anims)",
                      path, head[0], head[1], head[2], head[3], ANIM_FILE_VERSION, kind, fl, need ? "" : "от ", need ? need : 2);
            fclose(f);
            return false;
        }
        f32 *key = ok ? arena_push(mem, sizeof(f32) * fl * head[3], 4) : NULL;
        ok = key && fread(key, sizeof(f32) * fl, head[3], f) == head[3] && fgetc(f) == EOF;
        fclose(f);
        if (!ok) {
            log_error("КЛИПЫ: файл %s повреждён", path);
            return false;
        }
        g_anim_clip[c] = (AnimClip){ key, head[3] };
    }
    // Стойки наборов действий — ключи клипа weapon_guard
    for (u32 s = 0; s < AS_COUNT; s++)
        if (g_actions[s][ACT_STANCE].key >= CLIP_N(AC_WEAPON_GUARD)) {
            log_error("КЛИПЫ: стойка набора действий %u — ключ %u, а в weapon_guard их %u", s, g_actions[s][ACT_STANCE].key,
                      CLIP_N(AC_WEAPON_GUARD));
            return false;
        }
    return true;
}
