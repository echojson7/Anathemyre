#include "save.h"

#include <string.h>

#include "../core/log.h"

typedef struct SaveHeader {
    char magic[8];
    u32  version, pad;
    u64  layout, seed, tick, bytes, hash;
} SaveHeader;

static const char SAVE_MAGIC[8] = "ANASAVE";

bool save_write(const char *path, SaveInfo *info, SerFn fn, void *user)
{
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        log_error("СОХРАНЕНИЕ: файл %s не открыт для записи", tmp);
        return false;
    }
    SaveHeader h = { .version = SAVE_VERSION, .layout = info->layout, .seed = info->seed, .tick = info->tick };
    memcpy(h.magic, SAVE_MAGIC, sizeof h.magic);
    bool ok = fwrite(&h, sizeof h, 1, f) == 1;
    Ser s = { .mode = SER_SAVE, .f = f, .hash = SER_HASH_START };
    if (ok) fn(&s, user);
    ok = ok && !s.bad;
    h.bytes = info->bytes = s.bytes;
    h.hash = s.hash;
    ok = ok && fseek(f, 0, SEEK_SET) == 0 && fwrite(&h, sizeof h, 1, f) == 1;
    ok = (fclose(f) == 0) && ok;
    if (!ok) {
        log_error("СОХРАНЕНИЕ: запись в %s оборвалась", tmp);
        remove(tmp);
        return false;
    }
    // На Windows rename не заменяет существующий файл
    remove(path);
    if (rename(tmp, path) != 0) {
        log_error("СОХРАНЕНИЕ: %s не переименован в %s", tmp, path);
        return false;
    }
    return true;
}

bool save_read(const char *path, SaveInfo *info, SerFn fn, void *user, char *pool, u32 pool_cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        log_error("СОХРАНЕНИЕ: файла %s нет", path);
        return false;
    }
    SaveHeader h = { 0 };
    const char *why = NULL;
    char seed_why[96];
    if (fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, SAVE_MAGIC, sizeof h.magic)) why = "это не сохранение";
    else if (h.version != SAVE_VERSION) why = "сохранение другой версии";
    else if (h.layout != info->layout) why = "сохранение другой сборки игры";
    else if (h.seed != info->seed) {
        snprintf(seed_why, sizeof seed_why, "другое зерно мира, запуск нужен с --seed %llu", (unsigned long long)h.seed);
        why = seed_why;
    }
    // Первый проход: сумма тела. Мир трогается, только если файл цел
    if (!why) {
        u8  buf[16384];
        u64 sum = SER_HASH_START, n = 0;
        size_t got;
        while ((got = fread(buf, 1, sizeof buf, f)) > 0) {
            sum = ser_fnv(sum, buf, got);
            n += got;
        }
        if (n != h.bytes || sum != h.hash) why = "файл испорчен";
        else if (fseek(f, (long)sizeof h, SEEK_SET) != 0) why = "файл не читается";
    }
    if (why) {
        fclose(f);
        log_error("СОХРАНЕНИЕ: %s: %s", path, why);
        return false;
    }
    Ser s = { .mode = SER_LOAD, .f = f, .hash = SER_HASH_START, .pool = pool, .pool_cap = pool_cap };
    fn(&s, user);
    fclose(f);
    if (s.bad || s.bytes != h.bytes) {
        log_error("СОХРАНЕНИЕ: %s прочитан не до конца, мир испорчен", path);
        return false;
    }
    info->tick = h.tick;
    info->bytes = h.bytes;
    return true;
}
