// Платформа: окно, ввод, время, файлы, память, Vulkan-поверхность. Единственный шов к ОС (docs/invariants.md, шов 1).
// Реализация — platform_sdl.cpp на SDL3; остальная игра SDL не включает.
#pragma once
#include "../core/types.h"

enum Key : u8 {
    KEY_NONE = 0,
    KEY_ESCAPE, KEY_SPACE, KEY_PERIOD, KEY_MINUS, KEY_EQUALS, KEY_BACKSPACE, KEY_TAB,
    KEY_W, KEY_A, KEY_S, KEY_D, KEY_Q, KEY_E, KEY_F, KEY_L, KEY_P, KEY_R, KEY_T, KEY_B, KEY_C,
    KEY_LEFTBRACKET, KEY_RIGHTBRACKET,
    KEY_SHIFT, KEY_CTRL, KEY_ALT,
    KEY_F1, KEY_F2, KEY_F3, KEY_F11,
    KEY_COUNT
};

enum MouseButton : u8 { MOUSE_LEFT, MOUSE_MIDDLE, MOUSE_RIGHT, MOUSE_COUNT };

struct Input {
    bool key_down[KEY_COUNT];
    bool key_pressed[KEY_COUNT];   // нажата в этом кадре (с повтором клавиши)
    bool mouse_down[MOUSE_COUNT];
    bool mouse_pressed[MOUSE_COUNT];
    f32 mouse_x, mouse_y;          // пиксели окна, от левого верхнего угла
    f32 mouse_dx, mouse_dy;        // сдвиг мыши за кадр, пиксели
    f32 wheel;                     // щелчки колеса за кадр
    bool quit;
    bool resized;
};

// До всего остального: консоль Windows в UTF-8 (журнал по-русски)
void platform_preinit();

// Время и память. Работают и без окна (--sim)
u64 platform_time_ns();
void platform_sleep_ms(u32 ms);
// Страницы под арены при старте, нулями. Освобождается при выходе ОС
void *platform_alloc(usize size);
void platform_free(void *p);

// Файлы: прочитать целиком в память из арены-приёмника вызывающего
struct Arena;
bool platform_read_file(const char *path, Arena &arena, u8 **data, usize *size);
bool platform_write_file(const char *path, const void *data, usize size);
// Журнал: строка уходит в stderr и файл своим потоком, вызывающий не ждёт диска и консоли (docs/frame_time.md).
// open — один раз при запуске (false — потока нет, строки пишутся сразу); flush ждёт, пока всё записано
bool platform_log_open(const char *path);
void platform_log_append(const char *line, usize len);
void platform_log_flush();
// Перед выходом из программы: дописать журнал, закрыть файл и SDL
void platform_shutdown();
// Папка исполняемого файла, с завершающим разделителем
const char *platform_exe_dir();
// Имена файлов в папке (без пути) с окончанием suffix, по одному в cb. false — папки нет
bool platform_list_dir(const char *dir, const char *suffix, void (*cb)(const char *name, void *user), void *user);

// Окно
bool platform_window_open(const char *title, i32 width, i32 height);
void platform_window_close();
void platform_poll(Input &in);
void platform_window_size(i32 *w, i32 *h);   // в пикселях кадра
void platform_set_title(const char *title);
void platform_toggle_fullscreen();
void platform_set_fullscreen(bool on);
bool platform_is_fullscreen();

// Vulkan: функция загрузки, нужные расширения instance, поверхность окна.
// Типы спрятаны: VkInstance — указатель, VkSurfaceKHR — u64
typedef void (*PlatformProc)();
PlatformProc platform_vk_get_instance_proc_addr();
const char *const *platform_vk_instance_extensions(u32 *count);
u64 platform_vk_create_surface(void *vk_instance);
