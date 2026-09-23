// chrma terminal, default chrmaWM application

#include "cwm_client.h"
#include "cwm_protocol.h"
#include "rendering.h"
#include "utils/font.h"

#include <errno.h>
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

#define CT_FONT_PATH "ctfont.qoi"
#define CT_FONT_GLYPHS \
    " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`" \
    "abcdefghijklmnopqrstuvwxyz{|}~"

#define CT_COLS 80
#define CT_ROWS 24
#define CSI_MAX 16

enum {
    ST_GROUND = 0,
    ST_ESC,
    ST_CSI,
    ST_OSC,
};

struct vt_cell {
    uint8_t glyph;
    uint8_t foreground;
    uint8_t background;
};

/* palette.bmp, mapped onto ANSI 0-15. */
static const color palette[16] = {
    {28, 21, 41, 255},     /* 0  black    */
    {231, 167, 142, 255},  /* 1  red      */
    {161, 213, 180, 255},  /* 2  green    */
    {239, 202, 167, 255},  /* 3  yellow   */
    {95, 101, 181, 255},   /* 4  blue     */
    {225, 155, 217, 255},  /* 5  magenta  */
    {132, 181, 229, 255},  /* 6  cyan     */
    {231, 224, 228, 255},  /* 7  white    */
    {196, 189, 201, 255},  /* 8  gray     */
    {231, 167, 142, 255},  /* 9  br red   */
    {161, 213, 180, 255},  /* 10 br green */
    {239, 202, 167, 255},  /* 11 br yellow*/
    {132, 181, 229, 255},  /* 12 br blue  */
    {151, 121, 209, 255},  /* 13 br magenta */
    {132, 181, 229, 255},  /* 14 br cyan  */
    {231, 224, 228, 255},  /* 15 br white */
};

static const uint8_t def_fg = 7;
static const uint8_t def_bg = 0;

static struct {
    pid_t pid;
    int master_fd;
    int window_id;
    int cols;
    int rows;
    int cell_width;
    int cell_height;
    int cursor_col;
    int cursor_row;
    int cursor_visible;
    int scroll_top;
    int scroll_bot;
    int sgr_fg;
    int sgr_bg;
    int bold;
    int reverse;
    int graph;
    int st;
    int csi_priv;
    int csi_i;
    int csi_p[CSI_MAX];
    struct vt_cell *cells;
    pixmap canvas;
    font f;
    struct xkb_context *xkb_ctx;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
} vt = {
    .master_fd = -1,
    .cursor_visible = 1,
    .sgr_fg = 7,
};

static void vt_paint(struct vt_cell *cell)
{
    uint8_t fg = (uint8_t)vt.sgr_fg;
    uint8_t bg = (uint8_t)vt.sgr_bg;

    if (vt.bold && fg < 8)
        fg += 8;
    if (vt.reverse) {
        uint8_t t = fg;
        fg = bg;
        bg = t;
    }
    cell->foreground = fg;
    cell->background = bg;
}

static void vt_blank_cell(struct vt_cell *cell)
{
    cell->glyph = ' ';
    vt_paint(cell);
}

static void vt_clear_row(int row)
{
    int i;
    struct vt_cell *cells = vt.cells + row * vt.cols;

    for (i = 0; i < vt.cols; i++)
        vt_blank_cell(&cells[i]);
}

static void vt_clamp_cursor(void)
{
    if (vt.cursor_col < 0)
        vt.cursor_col = 0;
    if (vt.cursor_row < 0)
        vt.cursor_row = 0;
    if (vt.cursor_col >= vt.cols)
        vt.cursor_col = vt.cols - 1;
    if (vt.cursor_row >= vt.rows)
        vt.cursor_row = vt.rows - 1;
}

static void vt_scroll_up(int n)
{
    int top = vt.scroll_top;
    int bot = vt.scroll_bot;
    int rows = bot - top + 1;
    int i;

    if (n < 1 || rows < 1)
        return;
    if (n > rows)
        n = rows;
    memmove(vt.cells + top * vt.cols,
            vt.cells + (top + n) * vt.cols,
            (size_t)(rows - n) * (size_t)vt.cols * sizeof(*vt.cells));
    for (i = 0; i < n; i++)
        vt_clear_row(bot - i);
}

static void vt_scroll_down(int n)
{
    int top = vt.scroll_top;
    int bot = vt.scroll_bot;
    int rows = bot - top + 1;
    int i;

    if (n < 1 || rows < 1)
        return;
    if (n > rows)
        n = rows;
    memmove(vt.cells + (top + n) * vt.cols,
            vt.cells + top * vt.cols,
            (size_t)(rows - n) * (size_t)vt.cols * sizeof(*vt.cells));
    for (i = 0; i < n; i++)
        vt_clear_row(top + i);
}

static void vt_newline(void)
{
    vt.cursor_col = 0;
    if (vt.cursor_row == vt.scroll_bot)
        vt_scroll_up(1);
    else if (vt.cursor_row < vt.rows - 1)
        vt.cursor_row++;
}

static char vt_acs(uint8_t ch)
{
    switch (ch) {
    case 'j':
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 't':
    case 'u':
    case 'v':
    case 'w':
        return '+';
    case 'q':
        return '-';
    case 'x':
        return '|';
    default:
        return (char)ch;
    }
}

static void vt_put(uint8_t ch)
{
    struct vt_cell *cell;

    if (vt.graph)
        ch = (uint8_t)vt_acs(ch);
    cell = &vt.cells[vt.cursor_row * vt.cols + vt.cursor_col];
    cell->glyph = ch;
    vt_paint(cell);
    vt.cursor_col++;
    if (vt.cursor_col >= vt.cols)
        vt_newline();
}

static int csi_num(int i, int def)
{
    int v;

    if (i > vt.csi_i)
        return def;
    v = vt.csi_p[i];
    return v > 0 ? v : def;
}

static void vt_reset_sgr(void)
{
    vt.sgr_fg = def_fg;
    vt.sgr_bg = def_bg;
    vt.bold = 0;
    vt.reverse = 0;
}

static void vt_sgr(void)
{
    int i, n = vt.csi_i + 1;

    for (i = 0; i < n; i++) {
        int p = vt.csi_p[i];

        if (p == 0)
            vt_reset_sgr();
        else if (p == 1)
            vt.bold = 1;
        else if (p == 7)
            vt.reverse = 1;
        else if (p == 22)
            vt.bold = 0;
        else if (p == 27)
            vt.reverse = 0;
        else if (p >= 30 && p <= 37)
            vt.sgr_fg = p - 30;
        else if (p >= 90 && p <= 97)
            vt.sgr_fg = p - 90 + 8;
        else if (p == 39)
            vt.sgr_fg = def_fg;
        else if (p >= 40 && p <= 47)
            vt.sgr_bg = p - 40;
        else if (p >= 100 && p <= 107)
            vt.sgr_bg = p - 100 + 8;
        else if (p == 49)
            vt.sgr_bg = def_bg;
    }
}

static void vt_ed(int mode)
{
    int i, n = vt.cols * vt.rows;
    int cur = vt.cursor_row * vt.cols + vt.cursor_col;

    if (mode == 2)
        cur = 0, mode = 0;
    if (mode == 0) {
        for (i = cur; i < n; i++)
            vt_blank_cell(&vt.cells[i]);
    } else if (mode == 1) {
        for (i = 0; i <= cur; i++)
            vt_blank_cell(&vt.cells[i]);
    }
}

static void vt_el(int mode)
{
    int i;
    int row = vt.cursor_row * vt.cols;

    if (mode == 0) {
        for (i = vt.cursor_col; i < vt.cols; i++)
            vt_blank_cell(&vt.cells[row + i]);
    } else if (mode == 1) {
        for (i = 0; i <= vt.cursor_col; i++)
            vt_blank_cell(&vt.cells[row + i]);
    } else if (mode == 2) {
        vt_clear_row(vt.cursor_row);
    }
}

static void vt_csi_final(uint8_t cmd)
{
    int n;

    if (vt.csi_priv) {
        if (cmd == 'h' || cmd == 'l') {
            if (vt.csi_p[0] == 25)
                vt.cursor_visible = (cmd == 'h');
        }
        return;
    }

    switch (cmd) {
    case 'A':
        vt.cursor_row -= csi_num(0, 1);
        break;
    case 'B':
        vt.cursor_row += csi_num(0, 1);
        break;
    case 'C':
        vt.cursor_col += csi_num(0, 1);
        break;
    case 'D':
        vt.cursor_col -= csi_num(0, 1);
        break;
    case 'H':
    case 'f':
        vt.cursor_row = csi_num(0, 1) - 1;
        vt.cursor_col = csi_num(1, 1) - 1;
        break;
    case 'G':
        vt.cursor_col = csi_num(0, 1) - 1;
        break;
    case 'd':
        vt.cursor_row = csi_num(0, 1) - 1;
        break;
    case 'J':
        vt_ed(vt.csi_p[0]);
        break;
    case 'K':
        vt_el(vt.csi_p[0]);
        break;
    case 'm':
        vt_sgr();
        break;
    case 'r':
        vt.scroll_top = csi_num(0, 1) - 1;
        vt.scroll_bot = csi_num(1, vt.rows) - 1;
        if (vt.scroll_top < 0)
            vt.scroll_top = 0;
        if (vt.scroll_bot >= vt.rows)
            vt.scroll_bot = vt.rows - 1;
        if (vt.scroll_top > vt.scroll_bot)
            vt.scroll_top = 0, vt.scroll_bot = vt.rows - 1;
        break;
    case 'L':
        n = csi_num(0, 1);
        {
            int save_top = vt.scroll_top;
            vt.scroll_top = vt.cursor_row;
            vt_scroll_down(n);
            vt.scroll_top = save_top;
        }
        break;
    case 'M':
        n = csi_num(0, 1);
        {
            int save_top = vt.scroll_top;
            vt.scroll_top = vt.cursor_row;
            vt_scroll_up(n);
            vt.scroll_top = save_top;
        }
        break;
    default:
        break;
    }
    vt_clamp_cursor();
}

static void vt_csi_reset(void)
{
    memset(vt.csi_p, 0, sizeof(vt.csi_p));
    vt.csi_i = 0;
    vt.csi_priv = 0;
}

static void parseVT(const uint8_t *buf, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        uint8_t ch = buf[i];

        if (vt.st == ST_ESC) {
            if (ch == '[') {
                vt.st = ST_CSI;
                vt_csi_reset();
                continue;
            }
            if (ch == ']') {
                vt.st = ST_OSC;
                continue;
            }
            if (ch == 'M')
                vt_scroll_down(1);
            vt.st = ST_GROUND;
            continue;
        }

        if (vt.st == ST_OSC) {
            if (ch == 0x07 || ch == 0x1b) {
                vt.st = (ch == 0x1b) ? ST_ESC : ST_GROUND;
                if (ch == 0x1b)
                    i--;
            }
            continue;
        }

        if (vt.st == ST_CSI) {
            if (ch == '?' && vt.csi_i == 0 && vt.csi_p[0] == 0) {
                vt.csi_priv = 1;
                continue;
            }
            if (ch >= '0' && ch <= '9') {
                vt.csi_p[vt.csi_i] = vt.csi_p[vt.csi_i] * 10 + (ch - '0');
                continue;
            }
            if (ch == ';') {
                if (vt.csi_i + 1 < CSI_MAX)
                    vt.csi_i++;
                continue;
            }
            if (ch >= 0x40 && ch <= 0x7e) {
                vt_csi_final(ch);
                vt.st = ST_GROUND;
                continue;
            }
            vt.st = ST_GROUND;
            continue;
        }

        if (ch == 0x1b) {
            vt.st = ST_ESC;
            continue;
        }
        if (ch == 0x0e) {
            vt.graph = 1;
            continue;
        }
        if (ch == 0x0f) {
            vt.graph = 0;
            continue;
        }
        if (ch == '\n') {
            vt_newline();
            continue;
        }
        if (ch == '\r') {
            vt.cursor_col = 0;
            continue;
        }
        if (ch == '\b') {
            if (vt.cursor_col > 0)
                vt.cursor_col--;
            continue;
        }
        if (ch == '\t') {
            vt.cursor_col = (vt.cursor_col + 8) & ~7;
            if (vt.cursor_col >= vt.cols)
                vt_newline();
            continue;
        }
        if (ch >= 32)
            vt_put(ch);
    }
}

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
    ws.ws_col = (unsigned short)vt.cols;
    ws.ws_row = (unsigned short)vt.rows;
    ioctl(vt.master_fd, TIOCSWINSZ, &ws);
}

static void vt_redraw(void)
{
    int r, c;
    char s[2];

    if (!vt.canvas.data || !vt.cells)
        return;

    draw_rect(&vt.canvas,
              (rect2d){{0, 0}, {(int32_t)vt.canvas.width, (int32_t)vt.canvas.height}},
              palette[def_bg]);

    s[1] = '\0';
    for (r = 0; r < vt.rows; r++) {
        for (c = 0; c < vt.cols; c++) {
            struct vt_cell *cell = &vt.cells[r * vt.cols + c];
            color fg = palette[cell->foreground & 15];
            color bg = palette[cell->background & 15];
            int cursor = vt.cursor_visible
                && r == vt.cursor_row && c == vt.cursor_col;

            s[0] = (char)cell->glyph;
            font_draw(&vt.canvas, &vt.f, s,
                      c * vt.cell_width, r * vt.cell_height,
                      cursor ? bg : fg, cursor ? fg : bg);
        }
    }
    cwm_request_commit_window(vt.window_id);
}

static int vt_resize(uint8_t *px, int w, int h, int stride)
{
    int cols, rows, i, n;
    struct vt_cell *cells;

    (void)stride;

    if (!px || vt.cell_width <= 0 || vt.cell_height <= 0)
        return -1;

    cols = w / vt.cell_width;
    rows = h / vt.cell_height;
    if (cols < 1)
        cols = 1;
    if (rows < 1)
        rows = 1;

    n = cols * rows;
    cells = realloc(vt.cells, (size_t)n * sizeof(*cells));
    if (!cells)
        return -1;

    vt.cells = cells;
    vt.cols = cols;
    vt.rows = rows;
    vt.cursor_col = 0;
    vt.cursor_row = 0;
    vt.scroll_top = 0;
    vt.scroll_bot = rows - 1;
    vt_reset_sgr();
    for (i = 0; i < n; i++)
        vt_blank_cell(&vt.cells[i]);

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
    free(vt.cells);
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

    if (font_create(CT_FONT_PATH, &vt.f, CT_FONT_GLYPHS, 1) < 0)
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
            parseVT(buf, (size_t)nread);
            vt_redraw();
        }
    }

done:
    vt_shutdown();
    return 0;
}
