// chrma terminal, default chrmaWM application

#include "cwm_client.h"
#include "cwm_protocol.h"
#include "rendering.h"
#include "utils/font.h"
#include "utils/executable_path.h"
#include "terminal.h"

#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <pty.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define CT_FONT_PATH "assets/ct_font.qoi"
#define CT_FONT_GLYPHS \
    " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`" \
    "abcdefghijklmnopqrstuvwxyz{|}~"

static struct {
    pid_t pid;
    int master_fd;
    int window_id;
    int cell_width;
    int cell_height;
    pixmap canvas;
    font f;
    struct xkb_context *xkb_ctx;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
} vt = {.master_fd = -1};

static void vt_write(const char *s, size_t n)
{
    if (vt.master_fd >= 0)
        write(vt.master_fd, s, n);
}

static void handle_key(uint32_t keycode, uint8_t down)
{
    xkb_keysym_t sym;
    char buf[16];
    int n;

    if (!vt.xkb_state)
        return;
    xkb_state_update_key(vt.xkb_state, (xkb_keycode_t)keycode,
                         down ? XKB_KEY_DOWN : XKB_KEY_UP);
    if (!down)
        return;

    sym = xkb_state_key_get_one_sym(vt.xkb_state, (xkb_keycode_t)keycode);
    switch (sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        vt_write("\r", 1);
        return;
    case XKB_KEY_BackSpace:
        vt_write("\x7f", 1);
        return;
    case XKB_KEY_Tab:
        vt_write("\t", 1);
        return;
    case XKB_KEY_Up:
        vt_write("\033[A", 3);
        return;
    case XKB_KEY_Down:
        vt_write("\033[B", 3);
        return;
    case XKB_KEY_Right:
        vt_write("\033[C", 3);
        return;
    case XKB_KEY_Left:
        vt_write("\033[D", 3);
        return;
    case XKB_KEY_Home:
        vt_write("\033[1~", 4);
        return;
    case XKB_KEY_End:
        vt_write("\033[4~", 4);
        return;
    case XKB_KEY_Delete:
        vt_write("\033[3~", 4);
        return;
    case XKB_KEY_Page_Up:
        vt_write("\033[5~", 4);
        return;
    case XKB_KEY_Page_Down:
        vt_write("\033[6~", 4);
        return;
    default:
        n = xkb_state_key_get_utf8(vt.xkb_state, (xkb_keycode_t)keycode,
                                   buf, sizeof(buf));
        if (n > 0 && n < (int)sizeof(buf))
            vt_write(buf, (size_t)n);
    }
}

static int vt_xkb_init(void)
{
    vt.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!vt.xkb_ctx)
        return -1;
    vt.xkb_keymap = xkb_keymap_new_from_names(vt.xkb_ctx, NULL,
                                              XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!vt.xkb_keymap)
        return -1;
    vt.xkb_state = xkb_state_new(vt.xkb_keymap);
    if (!vt.xkb_state)
        return -1;
    return 0;
}

static void set_winsize(void)
{
    struct winsize ws;

    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)term.cols;
    ws.ws_row = (unsigned short)term.rows;
    ioctl(vt.master_fd, TIOCSWINSZ, &ws);
}

static void vt_redraw(void)
{
    int r, c;
    char s[2];

    if (!vt.canvas.data || !term.cells)
        return;

    draw_rect(&vt.canvas,
              (rect2d){{0, 0}, {(int32_t)vt.canvas.width, (int32_t)vt.canvas.height}},
              terminal_palette[0]);

    s[1] = '\0';
    for (r = 0; r < term.rows; r++) {
        for (c = 0; c < term.cols; c++) {
            vt_cell *cell = &term.cells[r * term.cols + c];
            color fg = cell->foreground;
            color bg = cell->background;
            int cursor = term.cursor_visible
                && r == term.cursor_row && c == term.cursor_col;

            s[0] = cell->continuation ? ' ' : terminal_display_glyph(cell->glyph);
            font_draw(&vt.canvas, &vt.f, s,
                      c * vt.cell_width, r * vt.cell_height,
                      cursor ? bg : fg, cursor ? fg : bg);
        }
    }
    cwm_request_commit_window(vt.window_id);
}

static int vt_resize(uint8_t *px, int w, int h, int stride)
{
    int cols, rows;

    (void)stride;
    if (!px || vt.cell_width <= 0 || vt.cell_height <= 0)
        return -1;
    cols = w / vt.cell_width;
    rows = h / vt.cell_height;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (terminal_resize(cols, rows) < 0)
        return -1;
    cwm_client_pixmap(&vt.canvas);
    set_winsize();
    vt_redraw();
    return 0;
}

static void vt_shutdown(void)
{
    if (vt.window_id > 0)
        cwm_request_destroy_window(vt.window_id);
    cwm_client_disconnect();
    if (vt.master_fd >= 0)
        close(vt.master_fd);
    if (vt.pid > 0)
        waitpid(vt.pid, NULL, WNOHANG);
    terminal_free();
    font_destroy(&vt.f);
    if (vt.xkb_state)
        xkb_state_unref(vt.xkb_state);
    if (vt.xkb_keymap)
        xkb_keymap_unref(vt.xkb_keymap);
    if (vt.xkb_ctx)
        xkb_context_unref(vt.xkb_ctx);
    vt.xkb_state = NULL;
    vt.xkb_keymap = NULL;
    vt.xkb_ctx = NULL;
}

int main(void)
{
    uint8_t *px;
    int w, h, stride;
    struct pollfd pfds[2];
    char font_path[4096];

    setlocale(LC_CTYPE, "");

    if (executable_path(font_path, sizeof(font_path), CT_FONT_PATH) < 0 ||
        font_create(font_path, &vt.f, CT_FONT_GLYPHS, 1) < 0)
        return 1;

    vt.cell_height = vt.f.height;
    vt.cell_width = vt.f.width[' '];
    if (vt.cell_width <= 0)
        vt.cell_width = vt.f.width[0];
    if (vt.cell_width <= 0 || vt.cell_height <= 0) {
        font_destroy(&vt.f);
        return 1;
    }

    vt.pid = forkpty(&vt.master_fd, NULL, NULL, NULL);
    if (vt.pid < 0) {
        font_destroy(&vt.f);
        return 1;
    }
    if (vt.pid == 0) {
        const char *home = getenv("HOME");

        if (home)
            chdir(home);
        setenv("TERM", "linux", 1);
        execl("/bin/sh", "sh", (char *)NULL);
        _exit(1);
    }

    if (vt_xkb_init() < 0) {
        vt_shutdown();
        return 1;
    }

    terminal_set_reply(vt_write);

    if (cwm_client_connect() < 0) {
        vt_shutdown();
        return 1;
    }

    vt.window_id = cwm_request_create_window("ChrmaTerm",
                                            CT_COLS * vt.cell_width,
                                            CT_ROWS * vt.cell_height);
    if (vt.window_id < 0) {
        vt.window_id = 0;
        vt_shutdown();
        return 1;
    }

    px = cwm_client_pixels();
    w = cwm_client_width();
    h = cwm_client_height();
    stride = cwm_client_stride();
    if (vt_resize(px, w, h, stride) < 0) {
        vt_shutdown();
        return 1;
    }

    pfds[0].fd = cwm_client_fd();
    pfds[0].events = POLLIN;
    pfds[1].fd = vt.master_fd;
    pfds[1].events = POLLIN;

    for (;;) {
        int n = poll(pfds, 2, -1);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (pfds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
            struct input_event ev;
            int got;

            while ((got = cwm_client_poll_event(&ev)) > 0) {
                switch (ev.type) {
                case CWM_PRT_TYPE_CLOSE_WINDOW:
                    goto done;
                case CWM_INPUT_EVENT_TYPE_RESIZE:
                    px = cwm_client_pixels();
                    w = cwm_client_width();
                    h = cwm_client_height();
                    stride = cwm_client_stride();
                    if (vt_resize(px, w, h, stride) < 0)
                        goto done;
                    break;
                case CWM_INPUT_EVENT_TYPE_KEYBOARD:
                    handle_key(ev.keycode, ev.down);
                    break;
                default:
                    break;
                }
            }
            if (got < 0)
                goto done;
        }

        if (pfds[1].revents & (POLLIN | POLLERR | POLLHUP)) {
            uint8_t buf[512];
            ssize_t nread = read(vt.master_fd, buf, sizeof(buf));

            if (nread <= 0)
                goto done;
            terminal_feed(buf, (size_t)nread);
            vt_redraw();
        }
    }

done:
    vt_shutdown();
    return 0;
}
