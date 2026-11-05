// Регистр float (MXCSR) для шага мира: округление к ближайшему, без FTZ/DAZ, все исключения замаскированы.
// Драйверы и библиотеки могут его менять, поэтому шаг ставит его сам в начале и проверяет в отладке в конце.
#pragma once
#include "types.h"
#include <immintrin.h>

constexpr u32 MXCSR_SIM = 0x1F80u;

inline void fpu_enter_sim() { _mm_setcsr(MXCSR_SIM); }
inline bool fpu_sim_ok() { return (_mm_getcsr() & 0xFFC0u) == (MXCSR_SIM & 0xFFC0u); }
