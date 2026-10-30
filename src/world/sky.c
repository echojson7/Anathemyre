#include "sky.h"

#include <math.h>

#define ORBIT_TILT 0.70f  // наклон пути солнца к югу (-Z), рад

void sky_at(f32 tod, Sky *out)
{
    f32 a = (tod - 0.25f) * 2.0f * PI_F;
    V3 sun = v3(cosf(a), sinf(a) * cosf(ORBIT_TILT), -sinf(a) * sinf(ORBIT_TILT));
    f32 e = sun.y;  // синус высоты солнца

    // Над горизонтом светит солнце, под ним — луна с противоположной стороны.
    // На самом горизонте оба гаснут, поэтому смена направления не видна
    if (e >= 0.0f) {
        V3 low = v3(1.7f, 0.75f, 0.30f), high = v3(2.05f, 1.88f, 1.58f);
        out->light_dir = sun;
        out->light_color = v3_mul(v3_lerp(low, high, smoothstepf(0.0f, 0.6f, e)), smoothstepf(0.0f, 0.10f, e));
    } else {
        out->light_dir = v3_mul(sun, -1.0f);
        // Луна вдвое слабее прежнего: ночью светят фонари и факелы
        out->light_color = v3_mul(v3(0.08f, 0.105f, 0.16f), smoothstepf(0.0f, 0.12f, -e));
    }

    f32 day = smoothstepf(-0.22f, 0.12f, e);
    out->night = 1.0f - smoothstepf(-0.12f, 0.05f, e);
    f32 dusk = 1.0f - smoothstepf(0.0f, 0.28f, fabsf(e + 0.03f));  // сумерки: тёплая дымка у горизонта
    V3 warm = v3(1.30f, 0.90f, 0.62f);
    V3 one = v3(1, 1, 1);
    V3 tint = v3_lerp(one, warm, dusk * 0.6f);

    out->sky_amb = v3_hadamard(v3_lerp(v3(0.035f, 0.045f, 0.070f), v3(0.16f, 0.16f, 0.15f), day), tint);
    out->ground_amb = v3_hadamard(v3_lerp(v3(0.012f, 0.013f, 0.018f), v3(0.20f, 0.15f, 0.09f), day), tint);
    out->haze = v3_hadamard(v3_lerp(v3(0.035f, 0.042f, 0.060f), v3(0.31f, 0.28f, 0.22f), day), tint);
    out->zenith = v3_lerp(v3(0.006f, 0.008f, 0.015f), v3(0.10f, 0.10f, 0.10f), day);
}
