// Реализация platform.h на SDL3
#include "platform.h"
#include "../core/log.h"
#include "../core/mem.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <execinfo.h>
#include <unistd.h>
#endif

static SDL_Window *g_window = nullptr;
static SDL_IOStream *g_log = nullptr;
static bool g_video = false;

static void crash_guard();

void platform_preinit() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    crash_guard();
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

// Журнал (docs/frame_time.md, «Журнал»): строки копируются в кольцо, а пишет их в stderr и файл свой поток.
// Запись в файл с SDL_FlushIO — это FlushFileBuffers на Windows и fdatasync на Linux, то есть ожидание диска,
// и консоль Windows тоже бывает медленной: в потоке кадра это рывок на каждой строке
static constexpr usize LOG_RING = 256u << 10;
static char g_log_ring[LOG_RING];
static char g_log_out[LOG_RING];  // то, что поток журнала пишет сейчас
static usize g_log_head = 0, g_log_tail = 0;  // байты: записано в кольцо и забрано потоком, растут без обёртки
static bool g_log_busy = false, g_log_quit = false;
static SDL_Mutex *g_log_mutex = nullptr;
static SDL_Condition *g_log_wake = nullptr, *g_log_idle = nullptr;
static SDL_Thread *g_log_thread = nullptr;

static void log_out(const char *s, usize n) {
    fwrite(s, 1, n, stderr);
    if (g_log) SDL_WriteIO(g_log, s, n);
}

static int SDLCALL log_thread(void *) {
    SDL_LockMutex(g_log_mutex);
    for (;;) {
        while (g_log_head == g_log_tail && !g_log_quit) SDL_WaitCondition(g_log_wake, g_log_mutex);
        if (g_log_head == g_log_tail) break;
        usize n = g_log_head - g_log_tail;
        for (usize i = 0; i < n; ++i) g_log_out[i] = g_log_ring[(g_log_tail + i) % LOG_RING];
        g_log_tail = g_log_head;
        g_log_busy = true;
        SDL_BroadcastCondition(g_log_idle);  // место в кольце освободилось
        SDL_UnlockMutex(g_log_mutex);
        log_out(g_log_out, n);
        fflush(stderr);
        if (g_log) SDL_FlushIO(g_log);  // файл цел, даже если игра упадёт следующей строкой
        SDL_LockMutex(g_log_mutex);
        g_log_busy = false;
        SDL_BroadcastCondition(g_log_idle);
    }
    SDL_UnlockMutex(g_log_mutex);
    return 0;
}

bool platform_log_open(const char *path) {
    g_log = SDL_IOFromFile(path, "wb");
    g_log_mutex = SDL_CreateMutex();
    g_log_wake = SDL_CreateCondition();
    g_log_idle = SDL_CreateCondition();
    if (g_log_mutex && g_log_wake && g_log_idle) g_log_thread = SDL_CreateThread(log_thread, "log", nullptr);
    return g_log_thread != nullptr;
}

void platform_log_append(const char *line, usize len) {
    if (!g_log_thread) {  // потока нет — сразу, как было до потока
        log_out(line, len);
        if (g_log) SDL_FlushIO(g_log);
        return;
    }
    if (len > LOG_RING) len = LOG_RING;
    SDL_LockMutex(g_log_mutex);
    // Кольцо полно — подождать поток (только при потоке строк, обычно в кольце пусто)
    while (g_log_head - g_log_tail + len > LOG_RING) SDL_WaitCondition(g_log_idle, g_log_mutex);
    for (usize i = 0; i < len; ++i) g_log_ring[(g_log_head + i) % LOG_RING] = line[i];
    g_log_head += len;
    SDL_SignalCondition(g_log_wake);
    SDL_UnlockMutex(g_log_mutex);
}

void platform_log_flush() {
    if (!g_log_thread) {
        fflush(stderr);
        return;
    }
    SDL_LockMutex(g_log_mutex);
    while (g_log_head != g_log_tail || g_log_busy) SDL_WaitCondition(g_log_idle, g_log_mutex);
    SDL_UnlockMutex(g_log_mutex);
}

static void log_close() {
    if (g_log_thread) {
        SDL_LockMutex(g_log_mutex);
        g_log_quit = true;
        SDL_SignalCondition(g_log_wake);
        SDL_UnlockMutex(g_log_mutex);
        SDL_WaitThread(g_log_thread, nullptr);  // поток дописывает всё, что в кольце, и выходит
        g_log_thread = nullptr;
    }
    if (g_log) SDL_CloseIO(g_log);
    g_log = nullptr;
    if (g_log_idle) SDL_DestroyCondition(g_log_idle);
    if (g_log_wake) SDL_DestroyCondition(g_log_wake);
    if (g_log_mutex) SDL_DestroyMutex(g_log_mutex);
    g_log_idle = g_log_wake = nullptr;
    g_log_mutex = nullptr;
}

// Падение (docs/build.md, «Если игра не запускается»): процесс умирает, и строки, которые поток журнала не успел
// записать, пропали бы — а с ними и то, на чём игра упала. Обработчик сам дописывает хвост кольца, строку
// «падение: …» и адреса стека в stderr и anathemyre.log, на Windows ещё окно с сообщением. Без блокировок:
// упасть могли и под мьютексом журнала; лучше редкая повторная строка, чем зависание при падении
static SDL_AtomicInt g_crashing;  // второе падение (в самом обработчике) — сразу выход

static void crash_out(const char *s, usize n) {
    fwrite(s, 1, n, stderr);
    fflush(stderr);
    if (g_log) {
        SDL_WriteIO(g_log, s, n);
        SDL_FlushIO(g_log);
    }
}

static void crash_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void crash_line(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    usize len = (usize)n < sizeof line - 2 ? (usize)n : sizeof line - 2;
    line[len++] = '\n';
    crash_out(line, len);
}

// Хвост кольца, который поток журнала ещё не забрал. Сначала даём потоку дописать самому (до 0.3 с), чтобы
// строки не задвоились; не успел (упали под мьютексом журнала или в самом потоке) — пишем сами
static void crash_flush_ring() {
    auto pending = [] { return *(volatile usize *)&g_log_head != *(volatile usize *)&g_log_tail || *(volatile bool *)&g_log_busy; };
    for (int i = 0; i < 300 && g_log_thread && pending(); ++i) SDL_Delay(1);
    usize head = g_log_head, tail = g_log_tail;
    if (head - tail > LOG_RING) tail = head - LOG_RING;
    static char out[LOG_RING];
    usize n = head - tail;
    for (usize i = 0; i < n; ++i) out[i] = g_log_ring[(tail + i) % LOG_RING];
    if (n) crash_out(out, n);
}

#ifdef _WIN32
static const char *crash_code_name(DWORD c) {
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION: return "обращение к чужой памяти";
    case EXCEPTION_STACK_OVERFLOW: return "переполнение стека";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "недопустимая инструкция (процессор без AVX2?)";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "деление на ноль";
    case EXCEPTION_IN_PAGE_ERROR: return "файл не прочитан с диска";
    case EXCEPTION_PRIV_INSTRUCTION: return "привилегированная инструкция";
    default: return "исключение";
    }
}

// Адрес → «модуль+смещение»: смещение в anathemyre.exe находится по сборке того же коммита (llvm-addr2line)
static void crash_where(DWORD64 pc, char *out, usize cap) {
    HMODULE mod = nullptr;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)pc, &mod) && mod)
        GetModuleFileNameA(mod, path, sizeof path);
    const char *name = path;
    for (const char *c = path; *c; ++c)
        if (*c == '\\' || *c == '/') name = c + 1;
    snprintf(out, cap, "%s+0x%llx", name, mod ? (unsigned long long)(pc - (DWORD64)(uintptr_t)mod) : (unsigned long long)pc);
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep) {
    if (SDL_AddAtomicInt(&g_crashing, 1)) return EXCEPTION_CONTINUE_SEARCH;
    crash_flush_ring();
    const EXCEPTION_RECORD &er = *ep->ExceptionRecord;
    char where[MAX_PATH + 32];
    crash_where((DWORD64)(uintptr_t)er.ExceptionAddress, where, sizeof where);
    char msg[1024];
    int n = snprintf(msg, sizeof msg, "падение: %s, код 0x%08lx, адрес %s", crash_code_name(er.ExceptionCode),
                     (unsigned long)er.ExceptionCode, where);
    if (er.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er.NumberParameters >= 2)
        snprintf(msg + n, sizeof msg - (usize)n, " (%s 0x%llx)", er.ExceptionInformation[0] == 1 ? "запись" : er.ExceptionInformation[0] == 8 ? "исполнение" : "чтение",
                 (unsigned long long)er.ExceptionInformation[1]);
    crash_line("%s", msg);
    // Стек: раскрутка по таблицам исключений x64 (без dbghelp)
    CONTEXT ctx = *ep->ContextRecord;
    for (int i = 0; i < 24 && ctx.Rip; ++i) {
        crash_where(ctx.Rip, where, sizeof where);
        crash_line("  %2d %s", i, where);
        DWORD64 base = 0;
        RUNTIME_FUNCTION *fn = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
        if (!fn) {  // листовая функция: адрес возврата на вершине стека
            ctx.Rip = *(DWORD64 *)(uintptr_t)ctx.Rsp;
            ctx.Rsp += 8;
            continue;
        }
        void *handler_data = nullptr;
        DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, fn, &ctx, &handler_data, &frame, nullptr);
    }
    if (g_log) {
        SDL_CloseIO(g_log);  // поток журнала уже не пишет: процесс сейчас закроется
        g_log = nullptr;
    }
    // Окно: игру часто запускают не из терминала (и в WSL терминал прячется за окном игры). В CI окна нет:
    // модальное окно повесило бы задачу до таймаута
    char box[1400];
    snprintf(box, sizeof box, "%s\n\nПодробности и стек — в anathemyre.log рядом с игрой.", msg);
    wchar_t wbox[1400];
    if (!GetEnvironmentVariableA("CI", nullptr, 0) && MultiByteToWideChar(CP_UTF8, 0, box, -1, wbox, 1400) > 0)
        MessageBoxW(nullptr, wbox, L"Anathemyre упала", MB_OK | MB_ICONERROR | MB_TOPMOST);
    return EXCEPTION_EXECUTE_HANDLER;  // выход с кодом исключения, без окна отчёта Windows
}
#endif

// abort() из ana_fail и CRT: строка «проверка не прошла» уже в журнале, здесь только дописать кольцо
static void crash_signal(int sig) {
    if (SDL_AddAtomicInt(&g_crashing, 1)) _exit(128 + sig);
    crash_flush_ring();
    const char *what = sig == SIGSEGV ? "обращение к чужой памяти" : sig == SIGABRT ? "аварийный выход (abort)"
                     : sig == SIGFPE ? "ошибка арифметики" : sig == SIGILL ? "недопустимая инструкция (процессор без AVX2?)" : "сигнал";
    crash_line("падение: %s, сигнал %d", what, sig);
#ifndef _WIN32
    void *pcs[32];
    int n = backtrace(pcs, 32);
    char **names = n > 0 ? backtrace_symbols(pcs, n) : nullptr;  // malloc в умирающем процессе: худшее — без стека
    for (int i = 0; names && i < n; ++i) crash_line("  %2d %s", i, names[i]);
#endif
    _exit(128 + sig);
}

static void crash_guard() {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
    return;  // у ASan свой обработчик падений, и его отчёт подробнее
#endif
#endif
#ifdef _WIN32
    ULONG guarantee = 64u << 10;  // при переполнении стека обработчику нужен свой запас
    SetThreadStackGuarantee(&guarantee);
    SetUnhandledExceptionFilter(crash_filter);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    signal(SIGABRT, crash_signal);
#else
    static char alt[64u << 10];  // свой стек обработчика: падение может быть от переполнения стека
    stack_t ss{};
    ss.ss_sp = alt;
    ss.ss_size = sizeof alt;
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_handler = crash_signal;
    sa.sa_flags = SA_ONSTACK | SA_RESETHAND;
    const int sigs[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
    for (int sig : sigs) sigaction(sig, &sa, nullptr);
#endif
}

void platform_shutdown() {
    log_close();
    SDL_Quit();
}

const char *platform_exe_dir() {
    const char *p = SDL_GetBasePath();
    return p ? p : "./";
}

struct ListCtx {
    const char *suffix;
    void (*cb)(const char *, void *);
    void *user;
};

static SDL_EnumerationResult SDLCALL on_dir_entry(void *u, const char *, const char *name) {
    ListCtx &c = *(ListCtx *)u;
    usize n = SDL_strlen(name), k = SDL_strlen(c.suffix);
    if (n >= k && SDL_strcmp(name + n - k, c.suffix) == 0) c.cb(name, c.user);
    return SDL_ENUM_CONTINUE;
}

bool platform_list_dir(const char *dir, const char *suffix, void (*cb)(const char *name, void *user), void *user) {
    ListCtx c{suffix, cb, user};
    return SDL_EnumerateDirectory(dir, on_dir_entry, &c);
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
    // Только видео и события: SDL_Quit объявляет недействительными все объекты SDL, и поток журнала потом не
    // дождаться (SDL_WaitThread молча ничего не делает). Всё SDL закрывает platform_shutdown
    if (g_video) SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
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
    case SDLK_Q: return KEY_Q;
    case SDLK_E: return KEY_E;
    case SDLK_F: return KEY_F;
    case SDLK_L: return KEY_L;
    case SDLK_P: return KEY_P;
    case SDLK_R: return KEY_R;
    case SDLK_T: return KEY_T;
    case SDLK_B: return KEY_B;
    case SDLK_C: return KEY_C;
    case SDLK_LEFTBRACKET: return KEY_LEFTBRACKET;
    case SDLK_RIGHTBRACKET: return KEY_RIGHTBRACKET;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return KEY_SHIFT;
    case SDLK_LCTRL: case SDLK_RCTRL: return KEY_CTRL;
    case SDLK_LALT: case SDLK_RALT: return KEY_ALT;
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
    in.mouse_dx = in.mouse_dy = 0;
    in.resized = false;
    f32 density = g_window ? SDL_GetWindowPixelDensity(g_window) : 1.0f;
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
        case SDL_EVENT_MOUSE_MOTION:
            in.mouse_dx += e.motion.xrel * density;
            in.mouse_dy += e.motion.yrel * density;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            // Отпущенные вне окна клавиши иначе «залипают»
            for (bool &b : in.key_down) b = false;
            for (bool &b : in.mouse_down) b = false;
            break;
        default: break;
        }
    }
    // Мышь — в пикселях кадра, как и размер окна
    f32 mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    in.mouse_x = mx * density;
    in.mouse_y = my * density;
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

void platform_set_fullscreen(bool on) {
    if (g_window && platform_is_fullscreen() != on) SDL_SetWindowFullscreen(g_window, on);
}

bool platform_is_fullscreen() {
    return g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;
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
