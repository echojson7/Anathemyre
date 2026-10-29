// Windows: Win32 + WGL. opengl32.dll подгружается через LoadLibrary, заголовков GL нет
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "platform.h"

#include <windows.h>
#include <string.h>

#include "../core/log.h"

#ifndef GET_WHEEL_DELTA_WPARAM
#define GET_WHEEL_DELTA_WPARAM(w) ((short)HIWORD(w))
#endif

#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_FLAGS_ARB 0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x1
#define WGL_CONTEXT_DEBUG_BIT_ARB 0x1

typedef HGLRC (WINAPI *PFN_wglCreateContext)(HDC);
typedef BOOL (WINAPI *PFN_wglDeleteContext)(HGLRC);
typedef BOOL (WINAPI *PFN_wglMakeCurrent)(HDC, HGLRC);
typedef PROC (WINAPI *PFN_wglGetProcAddress)(LPCSTR);
typedef HGLRC (WINAPI *PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int *);
typedef BOOL (WINAPI *PFN_wglSwapIntervalEXT)(int);

static struct {
    HINSTANCE inst;
    HWND      hwnd;
    HDC       hdc;
    HGLRC     ctx;
    HMODULE   opengl;
    i32       width, height;
    Input    *in;          // куда оконная процедура пишет события во время poll
    bool      have_mouse;
    bool      quit;

    PFN_wglDeleteContext  DeleteContext;
    PFN_wglMakeCurrent    MakeCurrent;
    PFN_wglGetProcAddress GetProcAddress;
} g_w;

// GetProcAddress отдаёт FARPROC; приводим через memcpy, как и на Linux
static PlatformProc sym(const char *name)
{
    FARPROC p = GetProcAddress(g_w.opengl, name);
    PlatformProc f;
    memcpy(&f, &p, sizeof f);
    return f;
}

// wglGetProcAddress отдаёт PROC; через void(*)(void) приведение к нужному типу допустимо
static PlatformProc wgl_proc(const char *name)
{
    PROC p = g_w.GetProcAddress(name);
    PlatformProc f;
    memcpy(&f, &p, sizeof f);
    return f;
}

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

bool platform_init(const char *title, i32 width, i32 height, bool gl_debug)
{
    SetProcessDPIAware();
    g_w.inst = GetModuleHandleW(NULL);
    g_w.opengl = LoadLibraryA("opengl32.dll");
    if (!g_w.opengl) {
        log_error("opengl32.dll не загружена");
        return false;
    }
    PFN_wglCreateContext CreateContext = (PFN_wglCreateContext)sym("wglCreateContext");
    g_w.DeleteContext = (PFN_wglDeleteContext)sym("wglDeleteContext");
    g_w.MakeCurrent = (PFN_wglMakeCurrent)sym("wglMakeCurrent");
    g_w.GetProcAddress = (PFN_wglGetProcAddress)sym("wglGetProcAddress");
    if (!CreateContext || !g_w.DeleteContext || !g_w.MakeCurrent || !g_w.GetProcAddress) {
        log_error("WGL: не хватает функций");
        return false;
    }

    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
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
    g_w.hdc = GetDC(g_w.hwnd);
    g_w.width = width;
    g_w.height = height;

    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int pf = ChoosePixelFormat(g_w.hdc, &pfd);
    if (!pf || !SetPixelFormat(g_w.hdc, pf, &pfd)) {
        log_error("WGL: формат пикселей не установлен");
        return false;
    }

    // Старый контекст нужен только чтобы достать wglCreateContextAttribsARB
    HGLRC legacy = CreateContext(g_w.hdc);
    if (!legacy || !g_w.MakeCurrent(g_w.hdc, legacy)) {
        log_error("WGL: временный контекст не создан");
        return false;
    }
    PFN_wglCreateContextAttribsARB CreateContextAttribs =
        (PFN_wglCreateContextAttribsARB)wgl_proc("wglCreateContextAttribsARB");
    if (!CreateContextAttribs) {
        log_error("WGL: нет wglCreateContextAttribsARB");
        return false;
    }
    const int attr[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
        WGL_CONTEXT_MINOR_VERSION_ARB, 5,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        WGL_CONTEXT_FLAGS_ARB, gl_debug ? WGL_CONTEXT_DEBUG_BIT_ARB : 0,
        0
    };
    g_w.ctx = CreateContextAttribs(g_w.hdc, NULL, attr);
    g_w.MakeCurrent(g_w.hdc, NULL);
    g_w.DeleteContext(legacy);
    if (!g_w.ctx || !g_w.MakeCurrent(g_w.hdc, g_w.ctx)) {
        log_error("WGL: контекст OpenGL 4.5 core не создан");
        return false;
    }
    ShowWindow(g_w.hwnd, SW_SHOW);
    return true;
}

void platform_shutdown(void)
{
    if (g_w.ctx) {
        g_w.MakeCurrent(NULL, NULL);
        g_w.DeleteContext(g_w.ctx);
    }
    if (g_w.hwnd) {
        ReleaseDC(g_w.hwnd, g_w.hdc);
        DestroyWindow(g_w.hwnd);
    }
    if (g_w.opengl) FreeLibrary(g_w.opengl);
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

void platform_swap(void)
{
    SwapBuffers(g_w.hdc);
}

void platform_set_vsync(bool on)
{
    PFN_wglSwapIntervalEXT f = (PFN_wglSwapIntervalEXT)wgl_proc("wglSwapIntervalEXT");
    if (f) f(on ? 1 : 0);
}

void platform_window_size(i32 *w, i32 *h)
{
    *w = g_w.width;
    *h = g_w.height;
}

PlatformProc platform_gl_proc(const char *name)
{
    // wglGetProcAddress не отдаёт функции OpenGL 1.1 и может вернуть 1, 2, 3 или -1
    PlatformProc f = wgl_proc(name);
    intptr_t v;
    memcpy(&v, &f, sizeof v);
    if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1) return sym(name);
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
