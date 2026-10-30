// Сетка видимости неба (СВЕТ: ОТРАЖЁННЫЙ — НЕБО ПО СЕТКЕ, docs/lighting.md): клетка 0.5 м, 256×64×256,
// в клетке — сколько из SKY_DIRS направлений неба из неё видно (байт, по четыре в uint вдоль x).
// Заголовок: xyz — угол сетки в мире, w — 1, если сетка готова
const ivec3 SKY_N = ivec3(256, 64, 256);
const float SKY_CELL = 0.5;
const float SKY_DIRS = 32.0;

uint sky_index(ivec3 c) { return (uint(c.z) * uint(SKY_N.y) + uint(c.y)) * uint(SKY_N.x / 4) + uint(c.x >> 2); }
