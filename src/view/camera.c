#include "camera.h"

// Ближе 4 м камера подходит только к ряду людей (меню НАСТРОЙКИ, крупные планы)
static f32 g_cam_min_dist = 4.0f;

void camera_close(bool on) { g_cam_min_dist = on ? 0.8f : 4.0f; }

void camera_update(RtsCamera *c, const Input *in, f32 dt)
{
    f32 fast = in->key_down[KEY_SHIFT] ? 3.0f : 1.0f;
    f32 pan = (8.0f + c->dist * 0.9f) * dt * fast;
    V3 fwd = v3(-sinf(c->yaw), 0, -cosf(c->yaw));
    V3 right = v3(cosf(c->yaw), 0, -sinf(c->yaw));
    if (in->key_down[KEY_W] || in->key_down[KEY_UP]) c->target = v3_add(c->target, v3_mul(fwd, pan));
    if (in->key_down[KEY_S] || in->key_down[KEY_DOWN]) c->target = v3_sub(c->target, v3_mul(fwd, pan));
    if (in->key_down[KEY_D] || in->key_down[KEY_RIGHT]) c->target = v3_add(c->target, v3_mul(right, pan));
    if (in->key_down[KEY_A] || in->key_down[KEY_LEFT]) c->target = v3_sub(c->target, v3_mul(right, pan));
    if (in->key_down[KEY_Q]) c->yaw += 1.5f * dt;
    if (in->key_down[KEY_E]) c->yaw -= 1.5f * dt;
    // ПКМ отдан приказам, камеру крутит средняя кнопка (как в Kenshi)
    if (in->mouse_down[MOUSE_MIDDLE]) {
        c->yaw -= (f32)in->mouse_dx * 0.005f;
        c->pitch += (f32)in->mouse_dy * 0.004f;
    }
    c->dist *= powf(0.88f, (f32)in->wheel);
    c->dist = clampf(c->dist, g_cam_min_dist, 120.0f);
    c->pitch = clampf(c->pitch, 0.08f, 1.45f);
}

V3 camera_eye(const RtsCamera *c)
{
    V3 off = v3(sinf(c->yaw) * cosf(c->pitch), sinf(c->pitch), cosf(c->yaw) * cosf(c->pitch));
    V3 eye = v3_add(c->target, v3_mul(off, c->dist));
    // У ряда людей камера кружит вокруг точки взгляда (на 1 м выше цели), иначе вблизи смотрит снизу
    if (g_cam_min_dist < 4.0f) eye.y += 1.0f;
    return eye;
}

bool camera_pick_ground(M4 view_proj, V3 eye, f32 nx, f32 ny, GroundFn ground, V3 *out)
{
    M4 inv = m4_inverse(view_proj);
    const f32 *m = inv.m;
    f32 w = m[3] * nx + m[7] * ny + m[11] + m[15];
    if (fabsf(w) < 1e-9f) return false;
    V3 far_p = v3_mul(m4_point(inv, v3(nx, ny, 1.0f)), 1.0f / w);
    V3 dir = v3_norm(v3_sub(far_p, eye));
    // Шаг по лучу до пересечения с рельефом, затем уточнение делением пополам
    const f32 step = 0.5f, max_t = 1500.0f;
    f32 t0 = 0.0f;
    for (f32 t = step; t < max_t; t += step) {
        V3 p = v3_add(eye, v3_mul(dir, t));
        if (p.y > ground(p.x, p.z)) {
            t0 = t;
            continue;
        }
        f32 a = t0, b = t;
        for (int i = 0; i < 20; i++) {
            f32 c = 0.5f * (a + b);
            V3 q = v3_add(eye, v3_mul(dir, c));
            if (q.y > ground(q.x, q.z)) a = c;
            else b = c;
        }
        V3 hit = v3_add(eye, v3_mul(dir, b));
        *out = v3(hit.x, ground(hit.x, hit.z), hit.z);
        return true;
    }
    return false;
}
