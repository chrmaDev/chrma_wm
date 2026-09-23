#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "cwm_protocol.h"

#include "windows.h"
#include "utils/vector.h"
#include "cwm_server.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define WINDOW_UNFOCUSED_ATLAS_Y 24
#define WINDOW_MIN_SIZE (2 * WINDOW_BORDER + 16)

enum {
    GRAB_NONE = 0,
    GRAB_MOVE,
    GRAB_CLIENT,
    GRAB_RESIZE,
};

typedef struct {
    pixmap *canvas;
    int shared_fd;
    size_t map_size;
    rect2d bounds;
    int fd;
    char *title;
    uint32_t id;
    uint8_t minimized;
} window;

static struct {
    Vector windows; /* back → front */
    uint32_t grabbed; /* window id, 0 = none */
    int grab_kind;
    point2d grab_offset;
    rect2d resize_preview;
    uint32_t next_id;
    uint32_t focused; /* window id, 0 = none */
} wm = { .next_id = 1 };

static void put_u32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static rect2d atlas_frame(rect2d src, int focused)
{
    if (!focused)
        src.pos.y += WINDOW_UNFOCUSED_ATLAS_Y;
    return src;
}

static int rect_contains(rect2d r, point2d p)
{
    return p.x >= r.pos.x && p.x < r.pos.x + r.size.width
        && p.y >= r.pos.y && p.y < r.pos.y + r.size.height;
}

static window *window_at(size_t i)
{
    return vector_get(&wm.windows, i);
}

static window *window_by_id(uint32_t id)
{
    size_t i;

    if (!id)
        return NULL;
    for (i = 0; i < wm.windows.size; i++) {
        window *w = window_at(i);

        if (w->id == id)
            return w;
    }
    return NULL;
}

static window *window_at_point(point2d p, size_t *index)
{
    size_t i;

    for (i = wm.windows.size; i > 0; i--) {
        window *w = window_at(i - 1);
        rect2d hit = w->bounds;

        if (w->minimized)
            hit.size.height = WINDOW_BORDER;
        if (!rect_contains(hit, p))
            continue;
        if (index)
            *index = i - 1;
        return w;
    }
    return NULL;
}

static void windows_raise(size_t i)
{
    window w;

    if (i + 1 == wm.windows.size)
        return;
    w = *window_at(i);
    memmove(window_at(i), window_at(i + 1),
            (wm.windows.size - i - 1) * sizeof(window));
    *window_at(wm.windows.size - 1) = w;
}

static void windows_on_removed(size_t i, uint32_t id)
{
    (void)i;
    if (wm.grabbed == id) {
        wm.grabbed = 0;
        wm.grab_kind = GRAB_NONE;
    }
    if (wm.focused == id)
        wm.focused = 0;
}

static void window_release(window *w)
{
    if (w->canvas) {
        if (w->canvas->data && w->canvas->data != MAP_FAILED)
            munmap(w->canvas->data, w->map_size);
        free(w->canvas);
        w->canvas = NULL;
    }
    if (w->shared_fd >= 0) {
        close(w->shared_fd);
        w->shared_fd = -1;
    }
    free(w->title);
    w->title = NULL;
}

int windows_init(void)
{
    wm.grabbed = 0;
    wm.grab_kind = GRAB_NONE;
    wm.next_id = 1;
    wm.focused = 0;
    return vector_init(&wm.windows, sizeof(window));
}

void windows_cleanup(void)
{
    size_t i;

    for (i = 0; i < wm.windows.size; i++)
        window_release(window_at(i));
    vector_free(&wm.windows);
    wm.grabbed = 0;
    wm.grab_kind = GRAB_NONE;
    wm.focused = 0;
}

static int create_graphics_buffer(uint32_t width, uint32_t height, uint8_t color_depth)
{
    int shared_fd;
    size_t size;

    if (!width || !height || color_depth < 8)
        return -1;
    shared_fd = memfd_create("cwm_graphics_buffer", 0);
    if (shared_fd < 0)
        return -1;
    size = (size_t)width * height * (color_depth / 8);
    if (ftruncate(shared_fd, (off_t)size) < 0) {
        close(shared_fd);
        return -1;
    }
    return shared_fd;
}

static int window_map_buffer(window *w, uint32_t width, uint32_t height)
{
    int fd;
    size_t size;
    void *map;
    pixmap *canvas;

    fd = create_graphics_buffer(width, height, 32);
    if (fd < 0)
        return -1;
    size = (size_t)width * height * 4;
    map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return -1;
    }
    memset(map, 0, size);

    canvas = w->canvas;
    if (!canvas) {
        canvas = malloc(sizeof(pixmap));
        if (!canvas) {
            munmap(map, size);
            close(fd);
            return -1;
        }
        w->canvas = canvas;
    } else if (canvas->data && canvas->data != MAP_FAILED) {
        munmap(canvas->data, w->map_size);
    }
    if (w->shared_fd >= 0)
        close(w->shared_fd);

    w->shared_fd = fd;
    w->map_size = size;
    canvas->data = map;
    canvas->width = width;
    canvas->height = height;
    canvas->pixel_stride = 4;
    canvas->pitch = width * 4;
    canvas->bgr = 1;
    return 0;
}

int window_create_for_client(int client_fd, rect2d bounds, char *title, uint32_t *window_id)
{
    window w;

    memset(&w, 0, sizeof(w));
    w.shared_fd = -1;
    w.bounds = bounds;
    w.fd = client_fd;
    if (bounds.size.width <= 0 || bounds.size.height <= 0)
        return -1;
    if (title) {
        w.title = malloc(strlen(title) + 1);
        if (w.title)
            memcpy(w.title, title, strlen(title) + 1);
    }

    w.bounds.size.width += 2 * WINDOW_BORDER;
    w.bounds.size.height += 2 * WINDOW_BORDER;
    if (window_map_buffer(&w, (uint32_t)bounds.size.width, (uint32_t)bounds.size.height) < 0) {
        window_release(&w);
        return -1;
    }
    w.id = wm.next_id++;

    if (vector_push(&wm.windows, &w) < 0) {
        window_release(&w);
        return -1;
    }
    wm.focused = w.id;
    if (window_id)
        *window_id = w.id;
    return w.shared_fd;
}

void window_destroy(int window_id)
{
    size_t i;

    if (window_id <= 0)
        return;

    for (i = 0; i < wm.windows.size; i++) {
        window *w = window_at(i);

        if (w->id != (uint32_t)window_id)
            continue;
        windows_on_removed(i, w->id);
        window_release(w);
        vector_remove(&wm.windows, i);
        return;
    }
}

void windows_destroy_all_for_client(int client_fd)
{
    size_t i = 0;

    while (i < wm.windows.size) {
        window *w = window_at(i);

        if (w->fd != client_fd) {
            i++;
            continue;
        }
        windows_on_removed(i, w->id);
        window_release(w);
        vector_remove(&wm.windows, i);
    }
}

static rect2d window_client_rect(const window *w)
{
    int b = WINDOW_BORDER;
    rect2d r;

    r.pos.x = w->bounds.pos.x + b;
    r.pos.y = w->bounds.pos.y + b;
    r.size.width = w->bounds.size.width - 2 * b;
    r.size.height = w->bounds.size.height - 2 * b;
    if (r.size.width < 0)
        r.size.width = 0;
    if (r.size.height < 0)
        r.size.height = 0;
    return r;
}

static rect2d window_close_rect(const window *w)
{
    return (rect2d){
        {w->bounds.pos.x + w->bounds.size.width - 10, w->bounds.pos.y},
        {WINDOW_BORDER, WINDOW_BORDER}
    };
}

static rect2d window_minimize_rect(const window *w)
{
    return (rect2d){
        {w->bounds.pos.x + w->bounds.size.width - 18, w->bounds.pos.y},
        {WINDOW_BORDER, WINDOW_BORDER}
    };
}

static rect2d window_resize_grip_rect(const window *w)
{
    int b = WINDOW_BORDER;

    return (rect2d){
        {w->bounds.pos.x + w->bounds.size.width - b,
         w->bounds.pos.y + w->bounds.size.height - b},
        {b, b}
    };
}

static void window_set_minimized(window *w, int minimized)
{
    w->minimized = minimized ? 1 : 0;
    if (!w->minimized)
        return;
    if (wm.focused == w->id)
        wm.focused = 0;
    if (wm.grabbed == w->id) {
        wm.grabbed = 0;
        wm.grab_kind = GRAB_NONE;
    }
}

static void emit_mouse(window *w, point2d pos, uint8_t down)
{
    rect2d c = window_client_rect(w);
    int32_t x = pos.x - c.pos.x;
    int32_t y = pos.y - c.pos.y;

    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (c.size.width > 0 && x >= c.size.width)
        x = c.size.width - 1;
    if (c.size.height > 0 && y >= c.size.height)
        y = c.size.height - 1;
    cwm_emit_input_event(w->fd, CWM_INPUT_EVENT_TYPE_MOUSE, 0, down, 0,
                         (uint16_t)x, (uint16_t)y);
}

static void resize_preview_from_point(window *w, point2d pos)
{
    int32_t width = pos.x - w->bounds.pos.x;
    int32_t height = pos.y - w->bounds.pos.y;

    if (width < WINDOW_MIN_SIZE)
        width = WINDOW_MIN_SIZE;
    if (height < WINDOW_MIN_SIZE)
        height = WINDOW_MIN_SIZE;
    wm.resize_preview.pos = w->bounds.pos;
    wm.resize_preview.size.width = width;
    wm.resize_preview.size.height = height;
}

void windows_handle_resize(int window_id, rect2d bounds)
{
    window *w = window_by_id((uint32_t)window_id);
    uint8_t payload[12];
    int32_t width = bounds.size.width;
    int32_t height = bounds.size.height;

    if (!w || window_id <= 0)
        return;
    if (width < WINDOW_MIN_SIZE)
        width = WINDOW_MIN_SIZE;
    if (height < WINDOW_MIN_SIZE)
        height = WINDOW_MIN_SIZE;
    if (w->bounds.size.width == width && w->bounds.size.height == height)
        return;

    w->bounds.size.width = width;
    w->bounds.size.height = height;
    {
        rect2d inner = window_client_rect(w);
        uint32_t buf_w = inner.size.width > 0 ? (uint32_t)inner.size.width : 1;
        uint32_t buf_h = inner.size.height > 0 ? (uint32_t)inner.size.height : 1;

        if (window_map_buffer(w, buf_w, buf_h) < 0)
            return;

        put_u32be(payload, w->id);
        put_u32be(payload + 4, buf_w);
        put_u32be(payload + 8, buf_h);
    }
    cwm_send_fd(w->fd, CWM_PRT_TYPE_RESIZE_WINDOW, payload, CWM_RESIZE_PAYLOAD, w->shared_fd);
}

void windows_handle_pointer(point2d pos, int pressed, int released)
{
    window *w;

    if (pressed) {
        size_t i;

        wm.grabbed = 0;
        wm.grab_kind = GRAB_NONE;
        w = window_at_point(pos, &i);
        if (!w) {
            wm.focused = 0;
            return;
        }
        wm.focused = w->id;
        windows_raise(i);
        w = window_by_id(wm.focused);
        if (!w)
            return;
        if (rect_contains(window_close_rect(w), pos)) {
            cwm_send(w->fd, CWM_PRT_TYPE_CLOSE_WINDOW, NULL, 0);
            window_destroy((int)w->id);
            return;
        }
        if (rect_contains(window_minimize_rect(w), pos)) {
            window_set_minimized(w, !w->minimized);
            return;
        }
        if (w->minimized)
            window_set_minimized(w, 0);
        if (rect_contains(window_resize_grip_rect(w), pos)) {
            wm.grabbed = w->id;
            wm.grab_kind = GRAB_RESIZE;
            resize_preview_from_point(w, pos);
        } else if (rect_contains(window_client_rect(w), pos)) {
            wm.grabbed = w->id;
            wm.grab_kind = GRAB_CLIENT;
            emit_mouse(w, pos, 1);
        } else {
            wm.grabbed = w->id;
            wm.grab_kind = GRAB_MOVE;
            wm.grab_offset.x = pos.x - w->bounds.pos.x;
            wm.grab_offset.y = pos.y - w->bounds.pos.y;
        }
    }

    if (released) {
        if (wm.grab_kind == GRAB_CLIENT) {
            w = window_by_id(wm.grabbed);
            if (w)
                emit_mouse(w, pos, 0);
        } else if (wm.grab_kind == GRAB_RESIZE) {
            w = window_by_id(wm.grabbed);
            if (w) {
                resize_preview_from_point(w, pos);
                windows_handle_resize((int)w->id, wm.resize_preview);
            }
        }
        wm.grabbed = 0;
        wm.grab_kind = GRAB_NONE;
    }

    if (wm.grab_kind == GRAB_MOVE) {
        w = window_by_id(wm.grabbed);
        if (w) {
            w->bounds.pos.x = pos.x - wm.grab_offset.x;
            w->bounds.pos.y = pos.y - wm.grab_offset.y;
        }
    } else if (wm.grab_kind == GRAB_RESIZE) {
        w = window_by_id(wm.grabbed);
        if (w)
            resize_preview_from_point(w, pos);
    } else if (wm.grab_kind == GRAB_CLIENT && !pressed) {
        w = window_by_id(wm.grabbed);
        if (w)
            emit_mouse(w, pos, 1);
    }
}

void windows_handle_keyboard(uint32_t keycode, uint8_t down)
{
    window *w = window_by_id(wm.focused);

    if (!w || w->minimized)
        return;
    cwm_emit_input_event(w->fd, CWM_INPUT_EVENT_TYPE_KEYBOARD, keycode, down, 0, 0, 0);
}

static void draw_outline(pixmap *dst, rect2d r, color c)
{
    if (r.size.width <= 0 || r.size.height <= 0)
        return;
    draw_rect(dst, (rect2d){r.pos, {r.size.width, 1}}, c);
    draw_rect(dst, (rect2d){{r.pos.x, r.pos.y + r.size.height - 1}, {r.size.width, 1}}, c);
    draw_rect(dst, (rect2d){r.pos, {1, r.size.height}}, c);
    draw_rect(dst, (rect2d){{r.pos.x + r.size.width - 1, r.pos.y}, {1, r.size.height}}, c);
}

static void blit_slice(pixmap *dst, pixmap *atlas, int x, int y, int w, int h, rect2d src)
{
    draw_buffer_ex(dst, (rect2d){{x, y}, {w, h}}, atlas, src, (color){255, 255, 255, 255});
}

static void window_draw(pixmap *dst, window *w, font *f, pixmap *atlas)
{
    int x = w->bounds.pos.x;
    int y = w->bounds.pos.y;
    int bw = w->bounds.size.width;
    int bh = w->minimized ? WINDOW_BORDER : w->bounds.size.height;
    int b = WINDOW_BORDER;
    int focused = (w->id == wm.focused);

    blit_slice(dst, atlas, x, y, b, b, atlas_frame(WINDOW_TOP_LEFT, focused));
    blit_slice(dst, atlas, x + bw - b, y, b, b, atlas_frame(WINDOW_TOP_RIGHT, focused));
    blit_slice(dst, atlas, x + b, y, bw - 2 * b, b, atlas_frame(WINDOW_TOP_MIDDLE, focused));

    if (!w->minimized) {
        blit_slice(dst, atlas, x, y + bh - b, b, b, atlas_frame(WINDOW_BOTTOM_LEFT, focused));
        blit_slice(dst, atlas, x + bw - b, y + bh - b, b, b, atlas_frame(WINDOW_BOTTOM_RIGHT, focused));
        blit_slice(dst, atlas, x + b, y + bh - b, bw - 2 * b, b, atlas_frame(WINDOW_BOTTOM_MIDDLE, focused));
        blit_slice(dst, atlas, x, y + b, b, bh - 2 * b, atlas_frame(WINDOW_MIDDLE_LEFT, focused));
        blit_slice(dst, atlas, x + bw - b, y + b, b, bh - 2 * b, atlas_frame(WINDOW_MIDDLE_RIGHT, focused));
        blit_slice(dst, atlas, x + b, y + b, bw - 2 * b, bh - 2 * b, atlas_frame(WINDOW_MIDDLE_MIDDLE, focused));
        if (w->canvas && w->canvas->data)
            draw_buffer(dst, x + b, y + b, w->canvas);
    }

    blit_slice(dst, atlas, x + bw - 10, y, b, b, WINDOW_CLOSE_BUTTON);
    blit_slice(dst, atlas, x + bw - 18, y, b, b, WINDOW_MINIMIZE_BUTTON);
    font_draw(dst, f, w->title, x + 4, y + 1, (color){255, 255, 255, 255}, (color){0, 0, 0, 20});
}

void windows_draw(pixmap *dst, font *f, pixmap *atlas)
{
    size_t i;

    for (i = 0; i < wm.windows.size; i++)
        window_draw(dst, window_at(i), f, atlas);
    if (wm.grab_kind == GRAB_RESIZE)
        draw_outline(dst, wm.resize_preview, (color){255, 255, 255, 255});
}
