// Реализация platform.h на SDL3
#include "platform.h"
#include "../core/log.h"
#include "../core/mem.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static SDL_Window *g_window = nullptr;
static SDL_IOStream *g_log = nullptr;
static bool g_video = false;

void platform_preinit() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
}

u64 platform_time_ns() { return SDL_GetTicksNS(); }
void platform_sleep_ms(u32 ms) { SDL_Delay(ms); }

void *platform_alloc(usize size) { return SDL_calloc(1, size); }
void platform_free(void *p) { SDL_free(p); }

bool platform_read_file(const char *path, Arena &arena, u8 **data, usize *size) {
    SDL_IOStream *f = SDL_IOFromFile(path, "rb");
    if (!f) return false;
    Sint64 n = SDL_GetIOSize(f);
    if (n < 0) { SDL_CloseIO(f); return false; }
    u8 *p = (u8 *)arena_push_bytes(arena, (usize)n + 1, 16);
    usize got = SDL_ReadIO(f, p, (usize)n);
    SDL_CloseIO(f);
    if (got != (usize)n) return false;
    p[n] = 0;
    *data = p;
    *size = (usize)n;
    return true;
}

bool platform_write_file(const char *path, const void *data, usize size) {
    SDL_IOStream *f = SDL_IOFromFile(path, "wb");
    if (!f) return false;
    bool ok = SDL_WriteIO(f, data, size) == size;
    ok = SDL_CloseIO(f) && ok;
    return ok;
}

bool platform_log_open(const char *path) {
    g_log = SDL_IOFromFile(path, "wb");
    return g_log != nullptr;
}

void platform_log_append(const char *line, usize len) {
    if (g_log) {
        SDL_WriteIO(g_log, line, len);
        SDL_FlushIO(g_log);
    }
}

const char *platform_exe_dir() {
    const char *p = SDL_GetBasePath();
    return p ? p : "./";
}

bool platform_window_open(const char *title, i32 width, i32 height) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        log_error("SDL_Init: %s", SDL_GetError());
        return false;
    }
    g_video = true;
    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        log_error("Vulkan не найден в системе: %s", SDL_GetError());
        return false;
    }
    g_window = SDL_CreateWindow(title, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!g_window) {
        log_error("SDL_CreateWindow: %s", SDL_GetError());
        return false;
    }
    return true;
}

void platform_window_close() {
    if (g_window) SDL_DestroyWindow(g_window);
    g_window = nullptr;
    // Библиотеку Vulkan не выгружаем: драйвер уходит вместе с процессом, а отладочные утечки драйвера
    // остаются с именами модулей (их глушит список в app/main.cpp)
    if (g_video) SDL_Quit();
    g_video = false;
}

static Key map_key(SDL_Keycode k) {
    switch (k) {
    case SDLK_ESCAPE: return KEY_ESCAPE;
    case SDLK_SPACE: return KEY_SPACE;
    case SDLK_PERIOD: return KEY_PERIOD;
    case SDLK_MINUS: return KEY_MINUS;
    case SDLK_EQUALS: return KEY_EQUALS;
    case SDLK_BACKSPACE: return KEY_BACKSPACE;
    case SDLK_TAB: return KEY_TAB;
    case SDLK_W: return KEY_W;
    case SDLK_A: return KEY_A;
    case SDLK_S: return KEY_S;
    case SDLK_D: return KEY_D;
    case SDLK_L: return KEY_L;
    case SDLK_F1: return KEY_F1;
    case SDLK_F2: return KEY_F2;
    case SDLK_F3: return KEY_F3;
    case SDLK_F11: return KEY_F11;
    default: return KEY_NONE;
    }
}

static int map_button(u8 b) {
    switch (b) {
    case SDL_BUTTON_LEFT: return MOUSE_LEFT;
    case SDL_BUTTON_MIDDLE: return MOUSE_MIDDLE;
    case SDL_BUTTON_RIGHT: return MOUSE_RIGHT;
    default: return -1;
    }
}

void platform_poll(Input &in) {
    for (bool &b : in.key_pressed) b = false;
    for (bool &b : in.mouse_pressed) b = false;
    in.wheel = 0;
    in.resized = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: in.quit = true; break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: in.resized = true; break;
        case SDL_EVENT_KEY_DOWN: {
            Key k = map_key(e.key.key);
            if (k) { in.key_down[k] = true; in.key_pressed[k] = true; }
        } break;
        case SDL_EVENT_KEY_UP: {
            Key k = map_key(e.key.key);
            if (k) in.key_down[k] = false;
        } break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            int b = map_button(e.button.button);
            if (b >= 0) { in.mouse_down[b] = true; in.mouse_pressed[b] = true; }
        } break;
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int b = map_button(e.button.button);
            if (b >= 0) in.mouse_down[b] = false;
        } break;
        case SDL_EVENT_MOUSE_WHEEL: in.wheel += e.wheel.y; break;
        default: break;
        }
    }
    // Мышь — в пикселях кадра, как и размер окна
    f32 mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    f32 scale = g_window ? SDL_GetWindowPixelDensity(g_window) : 1.0f;
    in.mouse_x = mx * scale;
    in.mouse_y = my * scale;
}

void platform_window_size(i32 *w, i32 *h) {
    *w = *h = 0;
    if (g_window) SDL_GetWindowSizeInPixels(g_window, w, h);
}

void platform_set_title(const char *title) {
    if (g_window) SDL_SetWindowTitle(g_window, title);
}

void platform_toggle_fullscreen() {
    if (!g_window) return;
    bool full = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(g_window, !full);
}

PlatformProc platform_vk_get_instance_proc_addr() {
    return (PlatformProc)SDL_Vulkan_GetVkGetInstanceProcAddr();
}

const char *const *platform_vk_instance_extensions(u32 *count) {
    Uint32 n = 0;
    const char *const *ext = SDL_Vulkan_GetInstanceExtensions(&n);
    *count = n;
    return ext;
}

u64 platform_vk_create_surface(void *vk_instance) {
    VkSurfaceKHR s = 0;
    if (!SDL_Vulkan_CreateSurface(g_window, (VkInstance)vk_instance, nullptr, &s)) {
        log_error("SDL_Vulkan_CreateSurface: %s", SDL_GetError());
        return 0;
    }
    return (u64)s;
}
