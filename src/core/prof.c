#include "prof.h"

#include <string.h>

#include "clock.h"

const char *const g_prof_name[PZ_COUNT] = {
    "ввод", "люди", "арена", "поле", "звук", "фигуры", "позы", "сцена", "отправка", "интерфейс", "swap",
};

static f64 g_sum[PZ_COUNT];
static f64 g_mark;
static ProfZone g_cur;

void prof_zone(ProfZone z)
{
    f64 t = time_now();
    if (g_mark > 0.0) g_sum[g_cur] += t - g_mark;  // первая зона — с этого вызова, не с запуска
    g_mark = t;
    g_cur = z;
}

void prof_take(f64 out[PZ_COUNT])
{
    memcpy(out, g_sum, sizeof g_sum);
    memset(g_sum, 0, sizeof g_sum);
}
