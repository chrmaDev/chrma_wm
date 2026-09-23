#include "kms.h"
#include "input.h"
#include "rendering.h"
#include "utils/imgload.h"
#include "utils/font.h"
#include "utils/executable_path.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>
#include "windows.h"
#include "cwm_server.h"

static struct {
    struct pollfd pfds[CWM_MAX_PFDS];
    int npfds;
    pixmap canvas;
    pixmap image;
    pixmap window_atlas;
    font f;
    point2d pos;
    int dirty;
    int running;
    int screenshot;
} app;

static volatile sig_atomic_t g_stop;

static void on_stop(int sig)
{
    (void)sig;
    g_stop = 1;
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static int rebuild_pollfds(void)
{
    int n = 0;

    app.pfds[n].fd = input_fd();
    app.pfds[n].events = POLLIN;
    app.pfds[n].revents = 0;
    n++;
    app.pfds[n].fd = kms_fd();
    app.pfds[n].events = POLLIN;
    app.pfds[n].revents = 0;
    n++;
    n += protocol_add_pollfds(app.pfds + n, CWM_MAX_PFDS - n);
    app.npfds = n;
    return n;
}

static void put_u32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void png_chunk(FILE *f, const char *type, const void *data, uint32_t len)
{
    uint8_t buf[8];
    uLong crc;

    put_u32be(buf, len);
    memcpy(buf + 4, type, 4);
    fwrite(buf, 1, 8, f);
    if (len)
        fwrite(data, 1, len, f);
    crc = crc32(0, (const Bytef *)type, 4);
    if (len)
        crc = crc32(crc, data, len);
    put_u32be(buf, (uint32_t)crc);
    fwrite(buf, 1, 4, f);
}

static void dump_screenshot(const pixmap *p)
{
    static const uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
    FILE *f;
    uint8_t ihdr[13];
    uint8_t *raw, *zbuf;
    uLong raw_n, z_n;
    uint32_t x, y;

    if (!p || !p->data || !p->width || !p->height)
        return;

    raw_n = (uLong)p->height * (1 + (uLong)p->width * 3);
    raw = malloc(raw_n);
    z_n = compressBound(raw_n);
    zbuf = malloc(z_n);
    if (!raw || !zbuf) {
        free(raw);
        free(zbuf);
        return;
    }

    for (y = 0; y < p->height; y++) {
        const uint8_t *src = p->data + (size_t)y * p->pitch;
        uint8_t *dst = raw + (size_t)y * (1 + (size_t)p->width * 3);

        *dst++ = 0;
        for (x = 0; x < p->width; x++) {
            const uint8_t *px = src + (size_t)x * p->pixel_stride;
            if (p->bgr) {
                dst[0] = px[2];
                dst[1] = px[1];
                dst[2] = px[0];
            } else {
                dst[0] = px[0];
                dst[1] = px[1];
                dst[2] = px[2];
            }
            dst += 3;
        }
    }

    if (compress(zbuf, &z_n, raw, raw_n) != Z_OK) {
        free(raw);
        free(zbuf);
        return;
    }
    free(raw);

    f = fopen("cwm.png", "wb");
    if (!f) {
        perror("cwm.png");
        free(zbuf);
        return;
    }
    fwrite(sig, 1, 8, f);
    put_u32be(ihdr, p->width);
    put_u32be(ihdr + 4, p->height);
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", zbuf, (uint32_t)z_n);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(zbuf);
}

static void spawn_terminal(void)
{
    pid_t pid;
    int fd;
    char path[4096];

    if (executable_path(path, sizeof(path), "ct") < 0) {
        fprintf(stderr, "cannot locate ct beside the window manager\n");
        return;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }
    if (pid > 0)
        return;

    for (fd = 3; fd < 256; fd++)
        close(fd);
    execl(path, "ct", (char *)NULL);
    _exit(1);
}

int init() {

    pixmap *screen;
    char path[4096];

    if (windows_init() < 0)
        return 1;

    if (executable_path(path, sizeof(path), "assets/window_atlas.qoi") < 0 ||
        !load_qoi(path, &app.window_atlas)) {
        printf("Failed to load window atlas\n");
        return 1;
    }

    if (executable_path(path, sizeof(path), "assets/background.qoi") < 0 ||
        !load_qoi(path, &app.image)) {
        printf("Failed to load image\n");
        return 1;
    }

    if (executable_path(path, sizeof(path), "assets/ui_font.qoi") < 0 ||
        font_create(path, &app.f, " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 0) < 0) {
        fprintf(stderr, "Failed to load UI font\n");
        return 1;
    }

    if (startServer() < 0)
        return 1;
    if (kms_init() < 0) {
        protocol_cleanup();
        return 1;
    }
    if (input_init() < 0) {
        kms_cleanup();
        protocol_cleanup();
        return 1;
    }

    signal(SIGINT, on_stop);
    signal(SIGTERM, on_stop);
    signal(SIGCHLD, SIG_IGN);

    screen = kms_buffer();
    if (pixmap_create(&app.canvas, screen->width / get_render_scale(),
                      screen->height / get_render_scale(), screen->bgr) < 0) {
        fprintf(stderr, "canvas alloc failed\n");
        input_cleanup();
        kms_cleanup();
        protocol_cleanup();
        return 1;
    }

    app.dirty = 1;
    app.running = 1;

    rebuild_pollfds();

    return 0;
}

static void update(void)
{
    pointer_state ptr;
    int timeout = (app.dirty && !kms_flip_pending()) ? 0 : -1;

    rebuild_pollfds();
    if (poll(app.pfds, app.npfds, timeout) < 0) {
        if (errno != EINTR) {
            perror("poll");
            app.running = 0;
            return;
        }
        if (g_stop)
            app.running = 0;
        return;
    }
    if (g_stop) {
        app.running = 0;
        return;
    }

    if (app.pfds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
        input_dispatch();
        app.dirty = 1;
    }
    if (app.pfds[1].revents & (POLLIN | POLLERR | POLLHUP))
        kms_dispatch();
    
    if (input_quit_requested()) {
        app.running = 0;
        return;
    }
    if (input_spawn_requested())
        spawn_terminal();

    ptr = input_pointer();
    {
        int scale = get_render_scale();
        int32_t maxx, maxy;
        static int rem_x, rem_y;

        rem_x += ptr.dx;
        rem_y += ptr.dy;
        app.pos.x += rem_x / scale;
        rem_x %= scale;
        app.pos.y += rem_y / scale;
        rem_y %= scale;
        maxx = (int32_t)app.canvas.width - 1;
        maxy = (int32_t)app.canvas.height - 1;
        if (maxx < 0)
            maxx = 0;
        if (maxy < 0)
            maxy = 0;
        app.pos.x = clampi(app.pos.x, 0, maxx);
        app.pos.y = clampi(app.pos.y, 0, maxy);
    }
    windows_handle_pointer(app.pos,
                           button_pressed(BUTTON_LEFT),
                           button_released(BUTTON_LEFT));

    {
        uint32_t key;
        uint8_t down;

        while (input_next_key_event(&key, &down))
            windows_handle_keyboard(key, down);
    }
    input_consume_buttons();
    input_consume_motion();

    process_new_connections();
    if (process_connections())
        app.dirty = 1;

    if (input_screenshot_requested()) {
        app.screenshot = 1;
        app.dirty = 1;
    }

    if (!app.dirty || kms_flip_pending())
        return;
}

static void draw(void)
{
    pixmap *c = &app.canvas;
    rect2d dst_rect, src_rect;

    dst_rect.pos.x = 0;
    dst_rect.pos.y = 0;
    dst_rect.size.width = (int32_t)c->width;
    dst_rect.size.height = (int32_t)c->height;
    src_rect.pos.x = 0;
    src_rect.pos.y = 0;
    src_rect.size.width = (int32_t)app.image.width;
    src_rect.size.height = (int32_t)app.image.height;
    draw_buffer_ex(c, dst_rect, &app.image, src_rect, IDENT_COLOR);

    windows_draw(c, &app.f, &app.window_atlas);
    draw_buffer_ex(c, (rect2d){{app.pos.x, app.pos.y}, {8, 8}}, &app.window_atlas, MOUSE_CURSOR, (color){255, 255, 255, 255});
}

int main(void)
{
    if (init() != 0) {
        printf("Failed to initialize\n");
        return 1;
    }


    for (;;) {
        update();
        if (!app.running)
            break;
        if (!app.dirty || kms_flip_pending())
            continue;

        draw();
        draw_integer_scale(kms_buffer(), &app.canvas, get_render_scale());
        if (app.screenshot) {
            dump_screenshot(kms_buffer());
            app.screenshot = 0;
        }
        if (kms_present() < 0)
            break;
        app.dirty = 0;
    }

    pixmap_destroy(&app.canvas);
    free(app.image.data);
    font_destroy(&app.f);
    windows_cleanup();
    protocol_cleanup();
    input_cleanup();
    kms_cleanup();
    return 0;
}
