// Платформенный слой: окно для Vulkan, ввод, потоки, звук. Реализация на каждую ОС отдельно
#ifndef ANA_PLATFORM_H
#define ANA_PLATFORM_H

#include "../core/types.h"

typedef enum Key {
    KEY_NONE = 0,
    KEY_W, KEY_A, KEY_S, KEY_D, KEY_Q, KEY_E, KEY_R, KEY_F, KEY_I, KEY_Z, KEY_X, KEY_C, KEY_G, KEY_P, KEY_H, KEY_K,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_SHIFT, KEY_SPACE, KEY_ESCAPE,
    KEY_COUNT
} Key;

enum { MOUSE_LEFT, MOUSE_MIDDLE, MOUSE_RIGHT, MOUSE_COUNT };

typedef struct Input {
    bool key_down[KEY_COUNT];
    bool key_pressed[KEY_COUNT];   // нажата в этом кадре
    bool mouse_down[MOUSE_COUNT];
    bool mouse_pressed[MOUSE_COUNT];  // нажата в этом кадре
    i32  mouse_x, mouse_y;
    i32  mouse_dx, mouse_dy;
    i32  wheel;                    // щелчки колеса за кадр
    bool quit;
} Input;

typedef void (*PlatformProc)(void);

// До всего остального: настройка консоли и т.п.
void         platform_preinit(void);
bool         platform_init(const char *title, i32 width, i32 height);
void         platform_shutdown(void);
void         platform_poll(Input *in);
// Окно без рамки на весь монитор и обратно
void         platform_set_fullscreen(bool on);
void         platform_window_size(i32 *w, i32 *h);

// Показ кадра и vsync делает rb_vk.c
// Хэндлы окна для поверхности: Display* и Window на X11, HINSTANCE и HWND на Windows
typedef struct PlatformNative { void *display; u64 window; } PlatformNative;
PlatformNative platform_native(void);
// vkGetInstanceProcAddr из libvulkan.so.1 / vulkan-1.dll; NULL — Vulkan в системе нет
PlatformProc platform_vk_loader(void);
// Папка исполняемого файла, без завершающего '/'
bool         platform_exe_dir(char *buf, size_t size);
// Создать папку (одну, без родителей); true — есть или создана
bool         platform_make_dir(const char *path);
// Обойти папку: имена без "." и "..", dir — подпапка. Порядок любой. false — папки нет
typedef void (*PlatformDirFn)(const char *name, bool dir, void *user);
bool         platform_list_dir(const char *path, PlatformDirFn fn, void *user);

// Потоки и семафор для рабочих потоков. Создаются один раз при старте
typedef struct PlatformThread PlatformThread;
typedef struct PlatformSema PlatformSema;
typedef void (*PlatformThreadFn)(void *arg);

u32             platform_cpu_count(void);
PlatformThread *platform_thread_start(PlatformThreadFn fn, void *arg);
void            platform_thread_join(PlatformThread *t);
PlatformSema   *platform_sema_create(void);
void            platform_sema_destroy(PlatformSema *s);
void            platform_sema_post(PlatformSema *s, u32 count);
void            platform_sema_wait(PlatformSema *s);
void            platform_cpu_relax(void);

// Звук: свой поток просит у fn frames кадров стерео (L, R по очереди, -1..1).
// Устройства нет — false, игра идёт без звука. Реализация в audio_<ОС>.c
typedef void (*PlatformAudioFn)(f32 *out, u32 frames, void *user);
bool platform_audio_start(u32 rate, PlatformAudioFn fn, void *user);
void platform_audio_stop(void);

#endif
