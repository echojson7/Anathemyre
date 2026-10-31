// Сборка без отладки (make NODEBUG=1): вместо src/debug/*.c. Арены, Мерильни, стенда, редактора и замеров нет,
// мир без крючка шага
#include "debug.h"

#include "../core/log.h"

const bool g_debug = false;

int debug_bench(const char *data_dir, const char *name, char **args, u32 nargs, u64 seed, u32 threads)
{
    (void)data_dir, (void)args, (void)nargs, (void)seed, (void)threads;
    log_error("ЗАМЕР: %s — замеров нет в сборке без отладки", name);
    return 1;
}

Debug *debug_init(Arena *perm, MeshBuilder *mb, const char *data_dir, World *w, const DebugStart *st)
{
    (void)perm, (void)mb, (void)data_dir, (void)w, (void)st;
    return NULL;
}

ArenaSession *debug_arena(Debug *d) { (void)d; return NULL; }
ArenaRing *debug_ring(Debug *d) { (void)d; return NULL; }
DebugStand debug_update(Debug *d, RtsCamera *cam) { (void)d, (void)cam; return DEBUG_STAND_SAME; }
void debug_menu(Debug *d, Hud *h, RtsCamera *cam) { (void)d, (void)h, (void)cam; }
void debug_menu_light(HudForm *f, RtsCamera *cam) { (void)f, (void)cam; }
void debug_input(Debug *d, Hud *h, Input *in, World *w, RtsCamera *cam) { (void)d, (void)h, (void)in, (void)w, (void)cam; }
i32 debug_follow(const Debug *d) { (void)d; return -1; }
void debug_present(Debug *d, const World *w, f32 dt) { (void)d, (void)w, (void)dt; }
f32 debug_ground(f32 x, f32 z, f32 base) { (void)x, (void)z; return base; }
V3 debug_clamp_eye(V3 target, V3 eye) { (void)target; return eye; }
void debug_room(RoomDark *r) { *r = (RoomDark){ 0 }; }
u32 debug_lights(f32 time, PointLight *out, u32 max) { (void)time, (void)out, (void)max; return 0; }

u32 debug_draw(Debug *d, const CharMeshes *cm, const World *w, V3 eye, f32 time, f32 dt, bool people, DrawItem *items)
{
    (void)d, (void)cm, (void)w, (void)eye, (void)time, (void)dt, (void)people, (void)items;
    return 0;
}

void debug_labels(const Debug *d, M4 view_proj, V3 eye) { (void)d, (void)view_proj, (void)eye; }
