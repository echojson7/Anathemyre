// Проверка Vulkan-бэкенда через слой rb: окно, swapchain, кадры в полёте, проход в окно, очистка,
// конвейер из .spv по рецепту (data/shaders/vkcheck.*), константы вызова.
// vkcheck [--frames N] [--present fifo|mailbox|immediate] [--size W H] [--shot file.ppm] [--verify]
// --verify сверяет пиксели кадра с ожидаемыми и ошибки validation: код выхода 1 при сбое
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/core/arena.h"
#include "../../src/core/clock.h"
#include "../../src/core/log.h"
#include "../../src/platform/platform.h"
#include "../../src/render/rb_vk.h"

static bool write_ppm(const char *path, const u8 *rgb, i32 w, i32 h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    // Кадр снизу вверх, как в GL
    for (i32 y = h - 1; y >= 0; y--) fwrite(rgb + (size_t)y * (size_t)w * 3, 1, (size_t)w * 3, f);
    fclose(f);
    return true;
}

static const f32 k_clear[4] = { 0.05f, 0.05f, 0.05f, 1.0f };
static const f32 k_red[4] = { 0.6f, 0.1f, 0.08f, 1.0f };

static bool near(const u8 *px, i32 r, i32 g, i32 b, i32 tol)
{
    return abs(px[0] - r) <= tol && abs(px[1] - g) <= tol && abs(px[2] - b) <= tol;
}

int main(int argc, char **argv)
{
    i32 w = 640, h = 360, frames = 300;
    const char *shot = NULL;
    bool verify = false;
    RbPresent present = RB_PRESENT_FIFO;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(a, "--size") && i + 2 < argc) { w = atoi(argv[++i]); h = atoi(argv[++i]); }
        else if (!strcmp(a, "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(a, "--verify")) verify = true;
        else if (!strcmp(a, "--present") && i + 1 < argc) {
            const char *m = argv[++i];
            present = !strcmp(m, "mailbox") ? RB_PRESENT_MAILBOX : !strcmp(m, "immediate") ? RB_PRESENT_IMMEDIATE : RB_PRESENT_FIFO;
        } else {
            puts("vkcheck [--frames N] [--present fifo|mailbox|immediate] [--size W H] [--shot file.ppm] [--verify]");
            return 2;
        }
    }

    platform_preinit();
    log_set_echo(true);
    time_init();
    Arena scratch;
    if (!arena_init(&scratch, MB(4))) return 1;
    platform_use_vulkan(true);
    rb_select(RB_API_VULKAN);
    if (!platform_init("Anathemyre — Vulkan", w, h, false)) return 1;
    if (!rb_init(&scratch, "data")) {
        platform_shutdown();
        return 1;
    }
    rb_vk_set_present(present);
    log_stat("VK: %s", rb_vk_device_name());

    RbPipe pipe = rb_pipe_make(&(RbPipeDesc){ .vs = { "vkcheck.vert" }, .fs = { "vkcheck.frag" }, .name = "vkcheck", .blend = RB_BLEND_ALPHA });
    bool ok = pipe != 0;

    u8 *rgb = arena_push(&scratch, (size_t)w * (size_t)h * 3, 16);
    Input in;
    memset(&in, 0, sizeof in);
    f64 t0 = time_now();
    i32 n = 0;
    for (; n < frames && !in.quit; n++) {
        platform_poll(&in);
        if (in.key_pressed[KEY_ESCAPE]) break;
        i32 ww, wh;
        platform_window_size(&ww, &wh);
        rb_frame_begin(ww, wh);
        RbPass pass = { .clear_color = true, .clear_depth = true };
        memcpy(pass.clear, k_clear, sizeof pass.clear);
        rb_pass_begin(&pass);
        f32 push[4] = { (f32)ww, (f32)wh, (f32)(time_now() - t0), 0.0f };
        rb_bind_pipe(pipe);
        rb_push(0, sizeof push, push);
        rb_draw(3, 0, 1);
        // Красный квадрат у левого нижнего угла (y от низа, как в GL), ножницы его не режут
        rb_scissor(0, 0, ww / 2, wh / 2);
        rb_clear_rect(16, 32, 48, 48, k_red, false);
        rb_scissor_off();
        rb_pass_end();
        bool last = n + 1 == frames;
        if (last && (shot || verify)) {
            rb_read_window(rgb, ww, wh);
            if (shot) {
                if (write_ppm(shot, rgb, ww, wh)) log_stat("VK: кадр записан %s", shot);
                else log_error("VK: кадр не записан %s", shot);
            }
            if (verify) {
                const u8 *red = rgb + ((size_t)40 * (size_t)ww + 20) * 3;
                const u8 *band = rgb + ((size_t)4 * (size_t)ww + (size_t)(ww - 20)) * 3;
                const u8 *top = rgb + ((size_t)(wh - 4) * (size_t)ww + (size_t)(ww / 2)) * 3;
                bool r_ok = near(red, 153, 26, 20, 1);
                bool b_ok = band[1] > band[0] + 10 && band[1] > band[2] + 10;    // бледно-зелёная полоса внизу
                bool t_ok = abs(top[0] - top[2]) < 12 && top[0] > 40 && top[0] < 80;  // бетон наверху
                log_stat("VK: пиксели: квадрат %d,%d,%d %s; полоса %d,%d,%d %s; верх %d,%d,%d %s",
                         red[0], red[1], red[2], r_ok ? "да" : "НЕТ", band[0], band[1], band[2], b_ok ? "да" : "НЕТ",
                         top[0], top[1], top[2], t_ok ? "да" : "НЕТ");
                ok = ok && r_ok && b_ok && t_ok;
            }
        }
        rb_frame_end();
    }
    f64 dt = time_now() - t0;
    log_stat("VK: %d кадров за %.2f с, %.0f к/с, показ %s", n, dt, dt > 0 ? n / dt : 0.0,
             rb_vk_present() == RB_PRESENT_MAILBOX ? "MAILBOX" : rb_vk_present() == RB_PRESENT_IMMEDIATE ? "IMMEDIATE" : "FIFO");
    rb_pipe_free(&pipe);
    rb_shutdown();
    platform_shutdown();
    u32 errors = rb_vk_validation_errors();
    log_stat("VK: ошибок validation %u", errors);
    arena_free(&scratch);
    if (verify && (!ok || errors)) {
        log_error("VK: СБОЙ проверки");
        return 1;
    }
    return 0;
}
