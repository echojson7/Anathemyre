// Сутки: положение солнца и луны, цвет света, неба и дымки по времени дня
#ifndef ANA_SKY_H
#define ANA_SKY_H

#include "../core/math3d.h"

typedef struct Sky {
    V3 light_dir;    // направление НА главный свет (днём солнце, ночью луна)
    V3 light_color;
    V3 sky_amb;      // рассеянный свет сверху
    V3 ground_amb;   // отражённый от земли
    V3 haze;         // цвет тумана и горизонта
    V3 zenith;       // цвет неба в зените
} Sky;

// tod — доля суток: 0 полночь, 0.25 рассвет, 0.5 полдень, 0.75 закат
void sky_at(f32 tod, Sky *out);

#endif
