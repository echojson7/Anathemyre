// Зоны профайлера кадра: кадр идёт участками подряд, prof_zone закрывает текущий участок и открывает
// следующий — время кадра делится между зонами без щелей. Копится до prof_take (строки КАДР и ЗАМЕР)
#ifndef ANA_PROF_H
#define ANA_PROF_H

#include "types.h"

typedef enum ProfZone {
    PZ_INPUT,     // окно и ввод, камера, меню, интерфейс, приказы
    PZ_PEOPLE,    // шаг людей и своего отряда
    PZ_ARENA,     // бои арены
    PZ_FIELD,     // поле боя
    PZ_PHYS,      // физика вещей
    PZ_SOUND,     // звуки мира
    PZ_FIGURES,   // кому из людей нужна поза, их элементы отрисовки
    PZ_POSES,     // позы в пуле потоков, свет факелов
    PZ_SCENE,     // остальной список кадра: статика, метки, свет арены
    PZ_SUBMIT,    // render_frame: сбор и проходы GL
    PZ_UI,        // портреты, интерфейс, конец кадра
    PZ_SWAP,      // показ кадра
    PZ_COUNT
} ProfZone;

extern const char *const g_prof_name[PZ_COUNT];

void prof_zone(ProfZone z);
// Накопленное по зонам с прошлого вызова, секунды; счётчики обнуляются
void prof_take(f64 out[PZ_COUNT]);

#endif
