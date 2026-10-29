// Монотонное время
#ifndef ANA_CLOCK_H
#define ANA_CLOCK_H

#include "types.h"

void time_init(void);
f64  time_now(void); // секунды с time_init
void time_wait_until(f64 t); // ждать момента t по time_now, точно до ~0.1 мс

#endif
