// Linux: X11 + GLX. libGL подгружается через dlopen, без линковки и заголовков GL
#define _GNU_SOURCE
#include "platform.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>

#include "../core/log.h"

// Минимальные объявления GLX
typedef struct __GLXFBConfigRec *GLXFBConfig;
typedef struct __GLXcontextRec *GLXContext;
typedef XID GLXDrawable;

#define GLX_DOUBLEBUFFER 5
#define GLX_RED_SIZE 8
#define GLX_GREEN_SIZE 9
#define GLX_BLUE_SIZE 10
#define GLX_ALPHA_SIZE 11
#define GLX_DEPTH_SIZE 12
#define GLX_STENCIL_SIZE 13
#define GLX_X_VISUAL_TYPE 0x22
#define GLX_TRUE_COLOR 0x8002
#define GLX_DRAWABLE_TYPE 0x8010
#define GLX_RENDER_TYPE 0x8011
#define GLX_X_RENDERABLE 0x8012
#define GLX_WINDOW_BIT 0x1
#define GLX_RGBA_BIT 0x1
#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#define GLX_CONTEXT_FLAGS_ARB 0x2094
#define GLX_CONTEXT_PROFILE_MASK_ARB 0x9126
#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x1
#define GLX_CONTEXT_DEBUG_BIT_ARB 0x1

typedef PlatformProc (*PFN_glXGetProcAddressARB)(const unsigned char *);
typedef GLXFBConfig *(*PFN_glXChooseFBConfig)(Display *, int, const int *, int *);
typedef XVisualInfo *(*PFN_glXGetVisualFromFBConfig)(Display *, GLXFBConfig);
typedef Bool (*PFN_glXMakeCurrent)(Display *, GLXDrawable, GLXContext);
typedef void (*PFN_glXSwapBuffers)(Display *, GLXDrawable);
typedef void (*PFN_glXDestroyContext)(Display *, GLXContext);
typedef GLXContext (*PFN_glXCreateContextAttribsARB)(Display *, GLXFBConfig, GLXContext, Bool, const int *);
typedef void (*PFN_glXSwapIntervalEXT)(Display *, GLXDrawable, int);

static struct {
    Display *dpy;
    Window   win;
    Colormap cmap;
    Atom     wm_delete;
    void    *libgl;
    GLXContext ctx;
    i32      width, height;
    bool     have_mouse;

    PFN_glXGetProcAddressARB    GetProcAddress;
    PFN_glXMakeCurrent          MakeCurrent;
    PFN_glXSwapBuffers          SwapBuffers;
    PFN_glXDestroyContext       DestroyContext;
} g_x;

// dlsym отдаёт void*, а ISO C запрещает прямое приведение к указателю на функцию
static PlatformProc sym(const char *name)
{
    void *p = dlsym(g_x.libgl, name);
    PlatformProc f;
    memcpy(&f, &p, sizeof f);
    return f;
}

static int x_error_handler(Display *d, XErrorEvent *e)
{
    (void)d;
    log_error("X11: ошибка %d, запрос %d", e->error_code, e->request_code);
    return 0;
}

void platform_preinit(void)
{
}

bool platform_init(const char *title, i32 width, i32 height, bool gl_debug)
{
    g_x.libgl = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!g_x.libgl) {
        log_error("libGL.so.1 не найдена: %s", dlerror());
        return false;
    }
    g_x.GetProcAddress = (PFN_glXGetProcAddressARB)sym("glXGetProcAddressARB");
    PFN_glXChooseFBConfig ChooseFBConfig = (PFN_glXChooseFBConfig)sym("glXChooseFBConfig");
    PFN_glXGetVisualFromFBConfig GetVisual = (PFN_glXGetVisualFromFBConfig)sym("glXGetVisualFromFBConfig");
    g_x.MakeCurrent = (PFN_glXMakeCurrent)sym("glXMakeCurrent");
    g_x.SwapBuffers = (PFN_glXSwapBuffers)sym("glXSwapBuffers");
    g_x.DestroyContext = (PFN_glXDestroyContext)sym("glXDestroyContext");
    if (!g_x.GetProcAddress || !ChooseFBConfig || !GetVisual || !g_x.MakeCurrent || !g_x.SwapBuffers) {
        log_error("GLX: не хватает функций");
        return false;
    }

    g_x.dpy = XOpenDisplay(NULL);
    if (!g_x.dpy) {
        log_error("X11: нет дисплея");
        return false;
    }
    XSetErrorHandler(x_error_handler);
    int screen = DefaultScreen(g_x.dpy);

    const int fb_attr[] = {
        GLX_X_RENDERABLE, True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
        GLX_DEPTH_SIZE, 24,
        GLX_DOUBLEBUFFER, True,
        None
    };
    int count = 0;
    GLXFBConfig *configs = ChooseFBConfig(g_x.dpy, screen, fb_attr, &count);
    if (!configs || count == 0) {
        log_error("GLX: подходящий FBConfig не найден");
        return false;
    }
    GLXFBConfig fbc = configs[0];
    XFree(configs);

    XVisualInfo *vi = GetVisual(g_x.dpy, fbc);
    if (!vi) {
        log_error("GLX: нет visual");
        return false;
    }
    Window root = RootWindow(g_x.dpy, screen);
    g_x.cmap = XCreateColormap(g_x.dpy, root, vi->visual, AllocNone);

    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof swa);
    swa.colormap = g_x.cmap;
    swa.background_pixel = 0;
    swa.border_pixel = 0;
    swa.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | StructureNotifyMask | FocusChangeMask;
    g_x.win = XCreateWindow(g_x.dpy, root, 0, 0, (unsigned)width, (unsigned)height, 0, vi->depth,
                            InputOutput, vi->visual,
                            CWColormap | CWBorderPixel | CWBackPixel | CWEventMask, &swa);
    XFree(vi);
    if (!g_x.win) {
        log_error("X11: окно не создано");
        return false;
    }
    XStoreName(g_x.dpy, g_x.win, title);
    g_x.wm_delete = XInternAtom(g_x.dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_x.dpy, g_x.win, &g_x.wm_delete, 1);
    XkbSetDetectableAutoRepeat(g_x.dpy, True, NULL);
    XMapWindow(g_x.dpy, g_x.win);
    g_x.width = width;
    g_x.height = height;

    PFN_glXCreateContextAttribsARB CreateContextAttribs =
        (PFN_glXCreateContextAttribsARB)g_x.GetProcAddress((const unsigned char *)"glXCreateContextAttribsARB");
    if (!CreateContextAttribs) {
        log_error("GLX: нет glXCreateContextAttribsARB");
        return false;
    }
    const int ctx_attr[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 4,
        GLX_CONTEXT_MINOR_VERSION_ARB, 5,
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        GLX_CONTEXT_FLAGS_ARB, gl_debug ? GLX_CONTEXT_DEBUG_BIT_ARB : 0,
        None
    };
    g_x.ctx = CreateContextAttribs(g_x.dpy, fbc, NULL, True, ctx_attr);
    XSync(g_x.dpy, False);
    if (!g_x.ctx) {
        log_error("GLX: контекст OpenGL 4.5 core не создан");
        return false;
    }
    if (!g_x.MakeCurrent(g_x.dpy, g_x.win, g_x.ctx)) {
        log_error("GLX: glXMakeCurrent провален");
        return false;
    }
    return true;
}

void platform_shutdown(void)
{
    if (g_x.dpy) {
        if (g_x.ctx) {
            g_x.MakeCurrent(g_x.dpy, None, NULL);
            g_x.DestroyContext(g_x.dpy, g_x.ctx);
        }
        if (g_x.win) XDestroyWindow(g_x.dpy, g_x.win);
        if (g_x.cmap) XFreeColormap(g_x.dpy, g_x.cmap);
        XCloseDisplay(g_x.dpy);
    }
    if (g_x.libgl) dlclose(g_x.libgl);
    memset(&g_x, 0, sizeof g_x);
}

static Key map_key(KeySym ks)
{
    switch (ks) {
    case XK_w: case XK_W: return KEY_W;
    case XK_a: case XK_A: return KEY_A;
    case XK_s: case XK_S: return KEY_S;
    case XK_d: case XK_D: return KEY_D;
    case XK_q: case XK_Q: return KEY_Q;
    case XK_e: case XK_E: return KEY_E;
    case XK_r: case XK_R: return KEY_R;
    case XK_f: case XK_F: return KEY_F;
    case XK_i: case XK_I: return KEY_I;
    case XK_Up: return KEY_UP;
    case XK_Down: return KEY_DOWN;
    case XK_Left: return KEY_LEFT;
    case XK_Right: return KEY_RIGHT;
    case XK_Shift_L: case XK_Shift_R: return KEY_SHIFT;
    case XK_space: return KEY_SPACE;
    case XK_Escape: return KEY_ESCAPE;
    case XK_F1: return KEY_F1;
    case XK_F2: return KEY_F2;
    case XK_F3: return KEY_F3;
    case XK_F4: return KEY_F4;
    case XK_F5: return KEY_F5;
    default: return KEY_NONE;
    }
}

void platform_poll(Input *in)
{
    memset(in->key_pressed, 0, sizeof in->key_pressed);
    memset(in->mouse_pressed, 0, sizeof in->mouse_pressed);
    in->mouse_dx = in->mouse_dy = 0;
    in->wheel = 0;

    while (XPending(g_x.dpy)) {
        XEvent ev;
        XNextEvent(g_x.dpy, &ev);
        switch (ev.type) {
        case KeyPress:
        case KeyRelease: {
            Key k = map_key(XLookupKeysym(&ev.xkey, 0));
            if (k == KEY_NONE) break;
            bool down = ev.type == KeyPress;
            if (down && !in->key_down[k]) in->key_pressed[k] = true;
            in->key_down[k] = down;
        } break;
        case ButtonPress:
        case ButtonRelease: {
            bool down = ev.type == ButtonPress;
            unsigned b = ev.xbutton.button;
            if (b >= 1 && b <= 3) {
                if (down && !in->mouse_down[b - 1]) in->mouse_pressed[b - 1] = true;
                in->mouse_down[b - 1] = down;
                in->mouse_x = ev.xbutton.x;
                in->mouse_y = ev.xbutton.y;
            }
            else if (down && b == 4) in->wheel++;
            else if (down && b == 5) in->wheel--;
        } break;
        case MotionNotify:
            if (g_x.have_mouse) {
                in->mouse_dx += ev.xmotion.x - in->mouse_x;
                in->mouse_dy += ev.xmotion.y - in->mouse_y;
            }
            in->mouse_x = ev.xmotion.x;
            in->mouse_y = ev.xmotion.y;
            g_x.have_mouse = true;
            break;
        case ConfigureNotify:
            g_x.width = ev.xconfigure.width;
            g_x.height = ev.xconfigure.height;
            break;
        case FocusOut:
            memset(in->key_down, 0, sizeof in->key_down);
            memset(in->mouse_down, 0, sizeof in->mouse_down);
            break;
        case ClientMessage:
            if ((Atom)ev.xclient.data.l[0] == g_x.wm_delete) in->quit = true;
            break;
        default:
            break;
        }
    }
}

void platform_swap(void)
{
    g_x.SwapBuffers(g_x.dpy, g_x.win);
}

void platform_set_vsync(bool on)
{
    PFN_glXSwapIntervalEXT f =
        (PFN_glXSwapIntervalEXT)g_x.GetProcAddress((const unsigned char *)"glXSwapIntervalEXT");
    if (f) f(g_x.dpy, g_x.win, on ? 1 : 0);
}

void platform_window_size(i32 *w, i32 *h)
{
    *w = g_x.width;
    *h = g_x.height;
}

PlatformProc platform_gl_proc(const char *name)
{
    PlatformProc p = g_x.GetProcAddress((const unsigned char *)name);
    return p ? p : sym(name);
}

bool platform_exe_dir(char *buf, size_t size)
{
    ssize_t n = readlink("/proc/self/exe", buf, size - 1);
    if (n <= 0) return false;
    buf[n] = 0;
    char *slash = strrchr(buf, '/');
    if (!slash) return false;
    *slash = 0;
    return true;
}

// ---------------------------------------------------------------- потоки

#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>

struct PlatformThread {
    pthread_t        handle;
    PlatformThreadFn fn;
    void            *arg;
};

struct PlatformSema {
    sem_t sem;
};

u32 platform_cpu_count(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (u32)n : 1;
}

static void *thread_entry(void *p)
{
    PlatformThread *t = p;
    t->fn(t->arg);
    return NULL;
}

PlatformThread *platform_thread_start(PlatformThreadFn fn, void *arg)
{
    PlatformThread *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->fn = fn;
    t->arg = arg;
    if (pthread_create(&t->handle, NULL, thread_entry, t) != 0) {
        free(t);
        return NULL;
    }
    return t;
}

void platform_thread_join(PlatformThread *t)
{
    if (!t) return;
    pthread_join(t->handle, NULL);
    free(t);
}

PlatformSema *platform_sema_create(void)
{
    PlatformSema *s = calloc(1, sizeof *s);
    if (s && sem_init(&s->sem, 0, 0) != 0) {
        free(s);
        return NULL;
    }
    return s;
}

void platform_sema_destroy(PlatformSema *s)
{
    if (!s) return;
    sem_destroy(&s->sem);
    free(s);
}

void platform_sema_post(PlatformSema *s, u32 count)
{
    while (count--) sem_post(&s->sem);
}

void platform_sema_wait(PlatformSema *s)
{
    while (sem_wait(&s->sem) != 0) {}  // EINTR — ждать дальше
}

void platform_cpu_relax(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
}
