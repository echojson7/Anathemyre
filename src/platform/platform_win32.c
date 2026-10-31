// Windows: окно Win32 для Vulkan. vulkan-1.dll подгружается через LoadLibrary, заголовков нет
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "platform.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../core/log.h"

#ifndef GET_WHEEL_DELTA_WPARAM
#define GET_WHEEL_DELTA_WPARAM(w) ((short)HIWORD(w))
#endif

static struct {
    HINSTANCE inst;
    HWND      hwnd;
    HMODULE   vulkan_dll;
    i32       width, height;
    Input    *in;          // куда оконная процедура пишет события во время poll
    bool      have_mouse;
    bool      quit;
    bool      fullscreen;
    WINDOWPLACEMENT placement;  // окно до перехода на весь экран
} g_w;

static Key map_key(WPARAM vk)
{
    switch (vk) {
    case 'W': return KEY_W;
    case 'A': return KEY_A;
    case 'S': return KEY_S;
    case 'D': return KEY_D;
    case 'Q': return KEY_Q;
    case 'E': return KEY_E;
    case 'R': return KEY_R;
    case 'F': return KEY_F;
    case 'I': return KEY_I;
    case 'Z': return KEY_Z;
    case 'X': return KEY_X;
    case 'C': return KEY_C;
    case 'G': return KEY_G;
    case 'P': return KEY_P;
    case 'H': return KEY_H;
    case 'K': return KEY_K;
    case VK_UP: return KEY_UP;
    case VK_DOWN: return KEY_DOWN;
    case VK_LEFT: return KEY_LEFT;
    case VK_RIGHT: return KEY_RIGHT;
    case VK_SHIFT: return KEY_SHIFT;
    case VK_SPACE: return KEY_SPACE;
    case VK_ESCAPE: return KEY_ESCAPE;
    default: return KEY_NONE;
    }
}

static void set_button(int b, bool down)
{
    if (g_w.in) {
        if (down && !g_w.in->mouse_down[b]) g_w.in->mouse_pressed[b] = true;
        g_w.in->mouse_down[b] = down;
    }
    if (down) SetCapture(g_w.hwnd);
    else if (g_w.in && !g_w.in->mouse_down[0] && !g_w.in->mouse_down[1] && !g_w.in->mouse_down[2]) ReleaseCapture();
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Input *in = g_w.in;
    switch (msg) {
    case WM_CLOSE:
        g_w.quit = true;
        return 0;
    case WM_SIZE:
        g_w.width = LOWORD(lp);
        g_w.height = HIWORD(lp);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        Key k = map_key(wp);
        if (k != KEY_NONE && in) {
            bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
            if (down && !in->key_down[k]) in->key_pressed[k] = true;
            in->key_down[k] = down;
            return 0;
        }
    } break;
    case WM_LBUTTONDOWN: set_button(MOUSE_LEFT, true); return 0;
    case WM_LBUTTONUP: set_button(MOUSE_LEFT, false); return 0;
    case WM_MBUTTONDOWN: set_button(MOUSE_MIDDLE, true); return 0;
    case WM_MBUTTONUP: set_button(MOUSE_MIDDLE, false); return 0;
    case WM_RBUTTONDOWN: set_button(MOUSE_RIGHT, true); return 0;
    case WM_RBUTTONUP: set_button(MOUSE_RIGHT, false); return 0;
    case WM_MOUSEWHEEL:
        if (in) in->wheel += GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        return 0;
    case WM_MOUSEMOVE:
        if (in) {
            i32 x = (i32)(short)LOWORD(lp), y = (i32)(short)HIWORD(lp);
            if (g_w.have_mouse) {
                in->mouse_dx += x - in->mouse_x;
                in->mouse_dy += y - in->mouse_y;
            }
            in->mouse_x = x;
            in->mouse_y = y;
            g_w.have_mouse = true;
        }
        return 0;
    case WM_KILLFOCUS:
        if (in) {
            memset(in->key_down, 0, sizeof in->key_down);
            memset(in->mouse_down, 0, sizeof in->mouse_down);
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void platform_preinit(void)
{
    // Кириллица в консоли
    SetConsoleOutputCP(CP_UTF8);
}

bool platform_init(const char *title, i32 width, i32 height)
{
    SetProcessDPIAware();
    g_w.inst = GetModuleHandleW(NULL);

    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = g_w.inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"anathemyre";
    if (!RegisterClassW(&wc)) {
        log_error("Win32: класс окна не зарегистрирован");
        return false;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT r = { 0, 0, width, height };
    AdjustWindowRect(&r, style, FALSE);
    wchar_t wtitle[128];
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, (int)ARRAY_COUNT(wtitle));
    g_w.hwnd = CreateWindowExW(0, wc.lpszClassName, wtitle, style, CW_USEDEFAULT, CW_USEDEFAULT,
                               r.right - r.left, r.bottom - r.top, NULL, NULL, g_w.inst, NULL);
    if (!g_w.hwnd) {
        log_error("Win32: окно не создано");
        return false;
    }
    g_w.width = width;
    g_w.height = height;
    ShowWindow(g_w.hwnd, SW_SHOW);
    return true;
}

void platform_shutdown(void)
{
    if (g_w.hwnd) DestroyWindow(g_w.hwnd);
    if (g_w.vulkan_dll) FreeLibrary(g_w.vulkan_dll);
    memset(&g_w, 0, sizeof g_w);
}

void platform_poll(Input *in)
{
    memset(in->key_pressed, 0, sizeof in->key_pressed);
    memset(in->mouse_pressed, 0, sizeof in->mouse_pressed);
    in->mouse_dx = in->mouse_dy = 0;
    in->wheel = 0;
    g_w.in = in;
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) g_w.quit = true;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_w.in = NULL;
    if (g_w.quit) in->quit = true;
}

void platform_set_fullscreen(bool on)
{
    if (on == g_w.fullscreen) return;
    g_w.fullscreen = on;
    if (on) {
        // Без рамки ровно по монитору: драйвер может показывать кадр мимо композитора
        MONITORINFO mi = { .cbSize = sizeof mi };
        g_w.placement.length = sizeof g_w.placement;
        GetWindowPlacement(g_w.hwnd, &g_w.placement);
        GetMonitorInfoW(MonitorFromWindow(g_w.hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(g_w.hwnd, GWL_STYLE, (LONG_PTR)(WS_POPUP | WS_VISIBLE));
        SetWindowPos(g_w.hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        SetWindowLongPtrW(g_w.hwnd, GWL_STYLE, (LONG_PTR)(WS_OVERLAPPEDWINDOW | WS_VISIBLE));
        SetWindowPlacement(g_w.hwnd, &g_w.placement);
        SetWindowPos(g_w.hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
}

void platform_window_size(i32 *w, i32 *h)
{
    *w = g_w.width;
    *h = g_w.height;
}

PlatformNative platform_native(void)
{
    return (PlatformNative){ .display = (void *)g_w.inst, .window = (u64)(uintptr_t)g_w.hwnd };
}

PlatformProc platform_vk_loader(void)
{
    if (!g_w.vulkan_dll) g_w.vulkan_dll = LoadLibraryA("vulkan-1.dll");
    if (!g_w.vulkan_dll) {
        log_error("vulkan-1.dll не загружена");
        return NULL;
    }
    FARPROC p = GetProcAddress(g_w.vulkan_dll, "vkGetInstanceProcAddr");
    PlatformProc f;
    memcpy(&f, &p, sizeof f);
    return f;
}

bool platform_exe_dir(char *buf, size_t size)
{
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)size);
    if (n == 0 || n >= size) return false;
    char *slash = strrchr(buf, '\\');
    char *fwd = strrchr(buf, '/');
    if (fwd > slash) slash = fwd;
    if (!slash) return false;
    *slash = 0;
    return true;
}

bool platform_make_dir(const char *path)
{
    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool platform_list_dir(const char *path, PlatformDirFn fn, void *user)
{
    char mask[1024];
    snprintf(mask, sizeof mask, "%s\\*", path);
    WIN32_FIND_DATAA e;
    HANDLE h = FindFirstFileA(mask, &e);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (!strcmp(e.cFileName, ".") || !strcmp(e.cFileName, "..")) continue;
        fn(e.cFileName, (e.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, user);
    } while (FindNextFileA(h, &e));
    FindClose(h);
    return true;
}

// ---------------------------------------------------------------- потоки

struct PlatformThread {
    HANDLE           handle;
    PlatformThreadFn fn;
    void            *arg;
};

u32 platform_cpu_count(void)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (u32)si.dwNumberOfProcessors : 1;
}

static DWORD WINAPI thread_entry(LPVOID p)
{
    PlatformThread *t = p;
    t->fn(t->arg);
    return 0;
}

PlatformThread *platform_thread_start(PlatformThreadFn fn, void *arg)
{
    PlatformThread *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *t);
    if (!t) return NULL;
    t->fn = fn;
    t->arg = arg;
    t->handle = CreateThread(NULL, 0, thread_entry, t, 0, NULL);
    if (!t->handle) {
        HeapFree(GetProcessHeap(), 0, t);
        return NULL;
    }
    return t;
}

void platform_thread_join(PlatformThread *t)
{
    if (!t) return;
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
    HeapFree(GetProcessHeap(), 0, t);
}

// Семафор — это сам HANDLE
PlatformSema *platform_sema_create(void)
{
    return (PlatformSema *)CreateSemaphoreA(NULL, 0, 0x7fffffff, NULL);
}

void platform_sema_destroy(PlatformSema *s)
{
    if (s) CloseHandle((HANDLE)s);
}

void platform_sema_post(PlatformSema *s, u32 count)
{
    if (count) ReleaseSemaphore((HANDLE)s, (LONG)count, NULL);
}

void platform_sema_wait(PlatformSema *s)
{
    WaitForSingleObject((HANDLE)s, INFINITE);
}

void platform_cpu_relax(void)
{
    YieldProcessor();
}
