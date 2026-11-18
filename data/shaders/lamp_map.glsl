// Тени огней картами (docs/lamp_shadows.md, vk_lamp_shadow.cpp): куб глубины на огонь. Грань — LSM_N² текселей,
// значение — 1 − расстояние до огня / LSM_RANGE (обратная глубина, как у кадра: ближе — больше, пусто — 0).
// Здесь — то, что нужно и отрисовке граней, и отбору заслонок, и выборке
#ifndef LAMP_MAP_GLSL
#define LAMP_MAP_GLSL

const float LSM_RANGE = 48.0;   // м: как LSM_RANGE в vk_state.h и предел дальности ламп (lamps.cpp)
const float LSM_N = 256.0;      // текселей в стороне грани (LSM_N в vk_state.h)
const float LSM_NEAR = 0.01;    // м: ближе к огню по оси грани заслонок нет

const float LSM_BACK = 0.06;    // м за огнём (против face): глубже — стена, на которой он висит

// Глубина в карте для расстояния до огня, м
float lsm_depth(float dist) { return clamp(1.0 - dist / LSM_RANGE, 0.0, 1.0); }

// Точка v (от огня) — часть его светильника (корпус, патрон, решётка): ближе self_r к отрезку огня и не глубже
// LSM_BACK за ним. Заслонкой не считается: тень корпуса у самого тела света в жизни размыта телом и залита
// отражённым светом, а PCSS (круг не шире поиска) рисовал её резким тёмным ореолом на стене вокруг лампы и гасил
// лампы в решётках. Стена за огнём остаётся заслонкой — свет не уходит за неё
bool lsm_own(vec3 v, vec3 axis, vec3 face, float self_r) {
    float aa = dot(axis, axis);
    float t = aa > 0.0 ? clamp(dot(v, axis) / aa, -1.0, 1.0) : 0.0;
    return length(v - axis * t) < self_r && dot(v, face) > -LSM_BACK;
}

// Грань f (0..5: +X −X +Y −Y +Z −Z) и точка v от огня → (sc, tc, ma), как их берёт выборка куба (спецификация
// Vulkan, «Cube Map Face Selection»): s = (sc / ma + 1) / 2 — слева направо, t = (tc / ma + 1) / 2 — сверху вниз
vec3 lsm_face_coords(uint f, vec3 v) {
    switch (f) {
    case 0u: return vec3(-v.z, -v.y, v.x);
    case 1u: return vec3(v.z, -v.y, -v.x);
    case 2u: return vec3(v.x, v.z, v.y);
    case 3u: return vec3(v.x, -v.z, -v.y);
    case 4u: return vec3(v.x, -v.y, v.z);
    default: return vec3(-v.x, -v.y, -v.z);
    }
}

// Шар (центр v от огня, радиус r) задевает пирамиду грани f: |sc| ≤ ma и |tc| ≤ ma. Плоскости боков идут через
// огонь с нормалями (±1, 0, 1) / √2 в осях грани; переход к осям грани ортогонален — расстояния те же
bool lsm_face_sphere(uint f, vec3 v, float r) {
    vec3 q = lsm_face_coords(f, v);
    float rr = r * 1.41421356;
    return q.z + q.x >= -rr && q.z - q.x >= -rr && q.z + q.y >= -rr && q.z - q.y >= -rr;
}
#endif
