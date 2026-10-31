// Камера как в RTS: точка взгляда на земле, поворот, наклон, дистанция; WASD, колесо, средняя кнопка, Q/E
#ifndef ANA_CAMERA_H
#define ANA_CAMERA_H

#include "../core/math3d.h"
#include "../platform/platform.h"
#include "../sim/player.h"

#define CAM_FOV 0.95f  // по вертикали, рад

typedef struct RtsCamera {
    V3  target;
    f32 yaw, pitch, dist;
} RtsCamera;

void camera_update(RtsCamera *c, const Input *in, f32 dt);
V3   camera_eye(const RtsCamera *c);
// Крупный план (ряд людей): камера подходит ближе 4 м и кружит вокруг точки на метр выше цели
void camera_close(bool on);
// Луч из камеры через точку экрана до земли. nx, ny — координаты в -1..1
bool camera_pick_ground(M4 view_proj, V3 eye, f32 nx, f32 ny, GroundFn ground, V3 *out);

#endif
