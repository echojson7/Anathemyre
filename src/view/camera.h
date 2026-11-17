// Камера (docs/camera.md): опора, взгляд и штанга. Игрок задаёт цель (взгляд, длину штанги, место опоры), кадр
// идёт к ней плавно; наклон и высота опоры следуют за длиной штанги (вблизи — вдоль земли, издалека — сверху);
// земля поднимает глаз, стены укорачивают штангу, взгляд не трогает никто. Камера — часть вида, не мира: на шаг
// мира и повтор не влияет (docs/invariants.md)
#pragma once
#include "../core/math3.h"
#include "../core/wpos.h"
#include "../platform/platform.h"

struct Terrain;
struct Colliders;

// Настройки камеры, которые меняет игрок (меню HUD пишет их сюда раз в кадр)
struct CameraSettings {
    f32 mouse_sens = 1.0f;  // множитель к 0.005 рад на пиксель
    bool invert_y = false;  // false: мышь вверх — взгляд вверх
    f32 fov_deg = 60.0f;    // поле зрения по вертикали, градусы
    bool invert_x = false;  // false: мышь вправо — взгляд вправо
    f32 smooth = 1.0f;      // плавность: 0 — без сглаживания, 1 — обычная, 2 — тягучая
    bool tilt_zoom = true;  // наклон от расстояния: колесо ведёт и наклон (вблизи вдоль земли, издалека сверху)
    bool zoom_cursor = true;  // колесо приближает к точке под курсором
    bool edge_pan = false;  // курсор у края экрана двигает камеру
    f32 pan_speed = 1.0f;   // множитель скорости WASD и края экрана
};

constexpr f32 CAM_PIVOT_UNSET = -1e9f;

struct Camera {
    // Цель: что задал игрок
    bool free = false;      // полёт (F): штанги нет, опора — сам глаз
    bool follow = false;    // опора — голова выбранного человека (view_camera_follow); WASD и сдвиг опоры снимают
    WPos pivot;             // опора: x, z — от игрока, y — сглаженная высота над землёй или полом
    f32 yaw = 0.55f;        // радианы, 0 — глаз на юге от опоры (−z), смотрит на север
    f32 pitch = 0.42f;      // радианы: > 0 — глаз выше опоры, смотрит вниз; < 0 — смотрит вверх. Итог: кривая
                            // наклона по длине штанги + tilt (свой наклон игрока поверх кривой)
    f32 dist = 24.0f;       // длина штанги, метров (0 — вид от глаз)
    f32 pivot_above = CAM_PIVOT_UNSET;  // опора орбиты от земли, м (--pivot для крупных планов, ниже нуля — под землёй)
    bool exact = false;     // камера снимка: без сглаживания, опора ровно над землёй (кадры CI не зависят от времени кадра)
    CameraSettings settings;

    // Кадр: сглаженное состояние. Считает view_camera_place / control / follow, снаружи только читать
    bool placed = false;
    f32 yaw_now = 0.0f, pitch_now = 0.0f, dist_now = 0.0f;  // взгляд и штанга в кадре
    f32 tilt = 0.0f, tilt_now = 0.0f;  // наклон игрока поверх кривой: цель и в кадре
    bool tilt_on = false;   // кривая наклона была включена в прошлом кадре (переключили — наклон сохраняется)
    f32 rise = 1.7f;        // опора над полом: вблизи — грудь и голова, издалека — у земли
    f32 fov_add = 0.0f;     // взгляд снизу вверх: поле зрения шире, радианы
    f32 yaw_rate = 0.0f;    // поворот клавишами Q/E, рад/с (с разгоном)
    f32 zoom_v = 0.0f;      // скорость штанги (пружина колеса), логарифм в секунду
    Vec3 zoom_pan{};        // колесо к курсору: сколько ещё сдвинуть опору, м (идёт вместе со штангой)
    Vec3 zoom_pan_v{};      // и его скорость
    bool zoom_anchor = false;  // точка под курсором при приближении взята
    WPos zoom_at;           // она
    f32 zoom_px = 0.0f, zoom_py = 0.0f;  // и пиксель курсора
    bool panning = false;   // Shift + средняя: тащим землю
    f32 pan_px = 0.0f, pan_py = 0.0f;  // где на экране точка, за которую взяли землю (курсор спрятан — считаем сами)
    WPos pan_at;            // точка земли, за которую взяли
    Vec3 follow_vel{};      // скорость догона опоры за человеком
    f32 arm = -1.0f;        // длина штанги после стен (< 0 — ещё не считали)
    f32 arm_want = 0.0f;    // dist_now прошлого кадра
    f32 arm_hold = 0.0f;    // секунды до того, как штанга начнёт расти обратно
    f32 lift = 0.0f;        // подъём глаза землёй, м
    f32 feet = 0.0f;        // пол под опорой без сглаживания (земля или ступень постройки)
    Vec3 vel{};             // скорость опоры, м/с
    f32 dolly = 0.0f;       // полёт: сколько ещё проехать вперёд от колеса, м
    Vec3 ease{};            // остаток перехода между режимами (глаз не прыгает), м
    bool blend = false;     // в этом кадре опора прыгнула: переход начать с прежнего глаза
    bool followed = false;  // прошлый кадр считал view_camera_follow
    bool grab = false;      // игрок крутит или тащит камеру мышью: окну спрятать курсор
    WPos eye;               // глаз в кадре
};

// Поставить камеру по цели сразу, без перехода (старт, --cam)
void view_camera_place(Camera &cam, const Terrain &ground, const Colliders *coll);
// Мышь, колесо и клавиши → камера. dt — секунды кадра (камера движется по времени кадра, не мира). width, height —
// окно (колесо к курсору, захват земли, край экрана); edge — мышь у края можно считать (окно в фокусе, меню
// закрыто). С коллайдерами coll опора спускается в проём земли и ходит по полу подземной постройки (docs/camera.md)
void view_camera_control(Camera &cam, const Input &in, f32 dt, i32 width, i32 height, bool edge, const Terrain &ground,
                         const Colliders *coll = nullptr);
// Камера за человеком: опора догоняет его голову мягко (на rise над ногами), штангу укорачивают постройки. Звать
// после control, пока cam.follow
void view_camera_follow(Camera &cam, const WPos &head, f32 dt, const Terrain &ground, const Colliders *coll);
// Глаз камеры (чанк + смещение)
WPos view_eye(const Camera &cam);
// Оси взгляда и поле зрения по вертикали, радианы
struct CameraBasis {
    Vec3 right, up, forward;
};
CameraBasis view_camera_basis(const Camera &cam);
f32 view_camera_fov(const Camera &cam);
// Наклон по кривой «длина штанги → наклон» без своего наклона игрока, радианы (старт окна)
f32 view_camera_auto_pitch(f32 dist);
// Луч из пикселя окна: глаз и направление
bool view_screen_ray(const Camera &cam, i32 width, i32 height, f32 px, f32 py, WPos &eye, Vec3 &dir);
// Луч из пикселя окна до земли (по ground_at, шагами с уточнением). false — луч не попал в землю
bool view_screen_to_ground(const Camera &cam, i32 width, i32 height, f32 px, f32 py, const Terrain &ground, WPos &out);
// Луч до земли или коробки постройки (пол, плита, ступень): точка приказа «идти»
bool view_screen_to_support(const Camera &cam, i32 width, i32 height, f32 px, f32 py, const Terrain &ground,
                            const Colliders *coll, WPos &out);
