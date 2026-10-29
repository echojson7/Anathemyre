// Пул рабочих потоков: параллельный цикл по диапазону номеров.
// Задача обязана писать только в свои номера — тогда результат не зависит от числа потоков.
#ifndef ANA_JOBS_H
#define ANA_JOBS_H

#include "types.h"

#define JOBS_MAX_THREADS 64

typedef void (*JobFn)(void *ctx, u32 begin, u32 end);

// threads — всего потоков вместе с главным; 0 — по числу ядер
bool jobs_init(u32 threads);
void jobs_shutdown(void);
u32  jobs_threads(void);
// fn вызывается кусками по grain номеров; возврат — когда весь диапазон пройден
void jobs_for(u32 count, u32 grain, JobFn fn, void *ctx);

#endif
