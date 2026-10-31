// Linux: окно X11 для Vulkan. libvulkan подгружается через dlopen, без линковки и заголовков
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

static struct {
    Display *dpy;
    Window   win;
    Colormap cmap;
    Atom     wm_delete;
    void    *libvk;
    i32      width, height;
    bool     have_mouse;
} g_x;

static int x_error_handler(Display *d, XErrorEvent *e)
{
    (void)d;
    log_error("X11: ошибка %d, запрос %d", e->error_code, e->request_code);
    return 0;
}

void platform_preinit(void)
{
}

static bool init_window(const char *title, i32 width, i32 height, Visual *visual, int depth)
{
    Window root = RootWindow(g_x.dpy, DefaultScreen(g_x.dpy));
    g_x.cmap = XCreateColormap(g_x.dpy, root, visual, AllocNone);

    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof swa);
    swa.colormap = g_x.cmap;
    swa.background_pixel = 0;
    swa.border_pixel = 0;
    swa.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | StructureNotifyMask | FocusChangeMask;
    g_x.win = XCreateWindow(g_x.dpy, root, 0, 0, (unsigned)width, (unsigned)height, 0, depth,
                            InputOutput, visual,
                            CWColormap | CWBorderPixel | CWBackPixel | CWEventMask, &swa);
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
    return true;
}

bool platform_init(const char *title, i32 width, i32 height)
{
    g_x.dpy = XOpenDisplay(NULL);
    if (!g_x.dpy) {
        log_error("X11: нет дисплея");
        return false;
    }
    XSetErrorHandler(x_error_handler);
    int screen = DefaultScreen(g_x.dpy);
    return init_window(title, width, height, DefaultVisual(g_x.dpy, screen), DefaultDepth(g_x.dpy, screen));
}

void platform_shutdown(void)
{
    if (g_x.dpy) {
        if (g_x.win) XDestroyWindow(g_x.dpy, g_x.win);
        if (g_x.cmap) XFreeColormap(g_x.dpy, g_x.cmap);
        XCloseDisplay(g_x.dpy);
    }
    if (g_x.libvk) dlclose(g_x.libvk);
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
    case XK_z: case XK_Z: return KEY_Z;
    case XK_x: case XK_X: return KEY_X;
    case XK_c: case XK_C: return KEY_C;
    case XK_g: case XK_G: return KEY_G;
    case XK_p: case XK_P: return KEY_P;
    case XK_h: case XK_H: return KEY_H;
    case XK_k: case XK_K: return KEY_K;
    case XK_Up: return KEY_UP;
    case XK_Down: return KEY_DOWN;
    case XK_Left: return KEY_LEFT;
    case XK_Right: return KEY_RIGHT;
    case XK_Shift_L: case XK_Shift_R: return KEY_SHIFT;
    case XK_space: return KEY_SPACE;
    case XK_Escape: return KEY_ESCAPE;
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

void platform_set_fullscreen(bool on)
{
    // Просьба оконному менеджеру (EWMH): _NET_WM_STATE_FULLSCREEN
    XEvent e = { 0 };
    e.xclient.type = ClientMessage;
    e.xclient.window = g_x.win;
    e.xclient.message_type = XInternAtom(g_x.dpy, "_NET_WM_STATE", False);
    e.xclient.format = 32;
    e.xclient.data.l[0] = on ? 1 : 0;
    e.xclient.data.l[1] = (long)XInternAtom(g_x.dpy, "_NET_WM_STATE_FULLSCREEN", False);
    e.xclient.data.l[3] = 1;
    XSendEvent(g_x.dpy, DefaultRootWindow(g_x.dpy), False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XFlush(g_x.dpy);
}

void platform_window_size(i32 *w, i32 *h)
{
    *w = g_x.width;
    *h = g_x.height;
}

PlatformNative platform_native(void)
{
    return (PlatformNative){ .display = g_x.dpy, .window = (u64)g_x.win };
}

PlatformProc platform_vk_loader(void)
{
    if (!g_x.libvk) g_x.libvk = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!g_x.libvk) {
        log_error("libvulkan.so.1 не найдена: %s", dlerror());
        return NULL;
    }
    void *p = dlsym(g_x.libvk, "vkGetInstanceProcAddr");
    PlatformProc f;
    memcpy(&f, &p, sizeof f);
    return f;
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

#include <errno.h>
#include <sys/stat.h>

bool platform_make_dir(const char *path) { return mkdir(path, 0755) == 0 || errno == EEXIST; }

#include <dirent.h>
#include <stdio.h>

bool platform_list_dir(const char *path, PlatformDirFn fn, void *user)
{
    DIR *d = opendir(path);
    if (!d) return false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char full[1024];
        struct stat st;
        snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        bool dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        fn(e->d_name, dir, user);
    }
    closedir(d);
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
