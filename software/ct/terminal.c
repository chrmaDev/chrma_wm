#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define CSI_MAX 16

enum {
    ST_GROUND = 0,
    ST_ESC,
    ST_CSI,
    ST_OSC,
    ST_OSC_ESC,
    ST_STRING,
    ST_STRING_ESC,
    ST_CHARSET,
};

/* palette.bmp, mapped onto ANSI 0-15. */
const color terminal_palette[16] = {
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

terminal term = {.cursor_visible = 1, .autowrap = 1};
static void (*reply_writer)(const char *, size_t);

void terminal_set_reply(void (*write_reply)(const char *, size_t))
{
    reply_writer = write_reply;
}

static void vt_paint(vt_cell *cell)
{
    int fg = term.sgr_fg;
    color fg_color, bg_color;

    if (term.bold && !term.sgr_fg_custom && fg < 8)
        fg += 8;
    fg_color = term.sgr_fg_custom ? term.sgr_fg_color : terminal_palette[fg & 15];
    bg_color = term.sgr_bg_custom ? term.sgr_bg_color : terminal_palette[term.sgr_bg & 15];
    if (term.reverse) {
        color swap = fg_color;
        fg_color = bg_color;
        bg_color = swap;
    }
    cell->foreground = fg_color;
    cell->background = bg_color;
}

static void vt_blank_cell(vt_cell *cell)
{
    cell->glyph = ' ';
    cell->continuation = 0;
    vt_paint(cell);
}

static void vt_clear_row(int row)
{
    int i;
    vt_cell *cells = term.cells + row * term.cols;

    for (i = 0; i < term.cols; i++)
        vt_blank_cell(&cells[i]);
}

static void vt_clamp_cursor(void)
{
    if (term.cursor_col < 0)
        term.cursor_col = 0;
    if (term.cursor_row < 0)
        term.cursor_row = 0;
    if (term.cursor_col >= term.cols)
        term.cursor_col = term.cols - 1;
    if (term.cursor_row >= term.rows)
        term.cursor_row = term.rows - 1;
}

static void vt_scroll_up(int n, int save_history)
{
    int top = term.scroll_top;
    int bot = term.scroll_bot;
    int rows = bot - top + 1;
    int i;

    if (n < 1 || rows < 1)
        return;
    if (n > rows)
        n = rows;
    if (save_history && term.history && !term.alt_active &&
        top == 0 && bot == term.rows - 1) {
        for (i = 0; i < n; i++) {
            memcpy(term.history + (size_t)term.history_head * term.cols,
                   term.cells + (size_t)i * term.cols,
                   (size_t)term.cols * sizeof(*term.cells));
            term.history_head = (term.history_head + 1) % term.history_capacity;
            if (term.history_count < term.history_capacity)
                term.history_count++;
        }
        if (term.history_offset > 0) {
            term.history_offset += n;
            if (term.history_offset > term.history_count)
                term.history_offset = term.history_count;
        }
    }
    memmove(term.cells + top * term.cols,
            term.cells + (top + n) * term.cols,
            (size_t)(rows - n) * (size_t)term.cols * sizeof(*term.cells));
    for (i = 0; i < n; i++)
        vt_clear_row(bot - i);
}

static void vt_scroll_down(int n)
{
    int top = term.scroll_top;
    int bot = term.scroll_bot;
    int rows = bot - top + 1;
    int i;

    if (n < 1 || rows < 1)
        return;
    if (n > rows)
        n = rows;
    memmove(term.cells + (top + n) * term.cols,
            term.cells + top * term.cols,
            (size_t)(rows - n) * (size_t)term.cols * sizeof(*term.cells));
    for (i = 0; i < n; i++)
        vt_clear_row(top + i);
}

static void vt_newline(void)
{
    term.wrap_pending = 0;
    if (term.cursor_row == term.scroll_bot)
        vt_scroll_up(1, 1);
    else if (term.cursor_row < term.rows - 1)
        term.cursor_row++;
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

char terminal_display_glyph(uint32_t ch)
{
    if (ch >= 32 && ch <= 126) return (char)ch;
    if (ch >= 0x2500 && ch <= 0x2501) return '-';
    if (ch >= 0x2502 && ch <= 0x2503) return '|';
    if (ch >= 0x250c && ch <= 0x254b) return '+';
    if (ch >= 0x2580 && ch <= 0x259f) return '#';
    return '?';
}

static void vt_put(uint32_t ch)
{
    vt_cell *cell;
    int width = wcwidth((wchar_t)ch);

    if (width == 0) return;
    if (width < 0 || width > 2) width = 1;

    if (term.wrap_pending || (width == 2 && term.cursor_col == term.cols - 1 && term.autowrap)) {
        term.cursor_col = 0;
        vt_newline();
    }
    if (term.graph && ch < 128)
        ch = (uint8_t)vt_acs((uint8_t)ch);
    cell = &term.cells[term.cursor_row * term.cols + term.cursor_col];
    cell->glyph = ch;
    cell->continuation = 0;
    vt_paint(cell);
    if (width == 2 && term.cursor_col + 1 < term.cols) {
        cell[1].glyph = 0;
        cell[1].continuation = 1;
        vt_paint(&cell[1]);
    }
    if (term.cursor_col + width >= term.cols)
        term.wrap_pending = term.autowrap;
    else
        term.cursor_col += width;
}

static int csi_num(int i, int def)
{
    int v;

    if (i > term.csi_i)
        return def;
    v = term.csi_p[i];
    return v > 0 ? v : def;
}

static void vt_reset_sgr(void)
{
    term.sgr_fg = def_fg;
    term.sgr_bg = def_bg;
    term.sgr_fg_custom = 0;
    term.sgr_bg_custom = 0;
    term.bold = 0;
    term.reverse = 0;
}

static color vt_palette_256(int index)
{
    static const uint8_t cube[6] = {0, 95, 135, 175, 215, 255};

    if (index < 16) return terminal_palette[index];
    if (index < 232) {
        index -= 16;
        return (color){cube[index / 36], cube[(index / 6) % 6], cube[index % 6], 255};
    }
    index = 8 + (index - 232) * 10;
    return (color){index, index, index, 255};
}

static void vt_enter_alt(void)
{
    vt_cell *cells;

    if (term.alt_active) return;
    cells = malloc((size_t)term.cols * term.rows * sizeof(*cells));
    if (!cells) return;
    term.primary = (terminal_screen){
        .cells = term.cells,
        .cursor_col = term.cursor_col, .cursor_row = term.cursor_row,
        .cursor_visible = term.cursor_visible,
        .saved_col = term.saved_col, .saved_row = term.saved_row,
        .scroll_top = term.scroll_top, .scroll_bot = term.scroll_bot,
        .sgr_fg = term.sgr_fg, .sgr_bg = term.sgr_bg,
        .sgr_fg_color = term.sgr_fg_color, .sgr_bg_color = term.sgr_bg_color,
        .sgr_fg_custom = term.sgr_fg_custom, .sgr_bg_custom = term.sgr_bg_custom,
        .bold = term.bold, .reverse = term.reverse, .graph = term.graph,
        .wrap_pending = term.wrap_pending,
        .history_offset = term.history_offset,
    };
    term.cells = cells;
    term.alt_active = 1;
    term.cursor_col = term.cursor_row = 0;
    term.saved_col = term.saved_row = 0;
    term.scroll_top = 0;
    term.scroll_bot = term.rows - 1;
    term.cursor_visible = 1;
    term.wrap_pending = 0;
    term.history_offset = 0;
    vt_reset_sgr();
    for (int i = 0; i < term.cols * term.rows; i++)
        vt_blank_cell(&term.cells[i]);
}

static void vt_leave_alt(void)
{
    terminal_screen screen;

    if (!term.alt_active) return;
    screen = term.primary;
    free(term.cells);
    term.cells = screen.cells;
    term.cursor_col = screen.cursor_col;
    term.cursor_row = screen.cursor_row;
    term.cursor_visible = screen.cursor_visible;
    term.saved_col = screen.saved_col;
    term.saved_row = screen.saved_row;
    term.scroll_top = screen.scroll_top;
    term.scroll_bot = screen.scroll_bot;
    term.sgr_fg = screen.sgr_fg;
    term.sgr_bg = screen.sgr_bg;
    term.sgr_fg_color = screen.sgr_fg_color;
    term.sgr_bg_color = screen.sgr_bg_color;
    term.sgr_fg_custom = screen.sgr_fg_custom;
    term.sgr_bg_custom = screen.sgr_bg_custom;
    term.bold = screen.bold;
    term.reverse = screen.reverse;
    term.graph = screen.graph;
    term.wrap_pending = screen.wrap_pending;
    term.history_offset = screen.history_offset;
    term.alt_active = 0;
    term.primary = (terminal_screen){0};
}

static void vt_sgr(void)
{
    int i, n = term.csi_i + 1;

    for (i = 0; i < n; i++) {
        int p = term.csi_p[i];

        if (p == 0)
            vt_reset_sgr();
        else if (p == 1)
            term.bold = 1;
        else if (p == 7)
            term.reverse = 1;
        else if (p == 22)
            term.bold = 0;
        else if (p == 27)
            term.reverse = 0;
        else if (p >= 30 && p <= 37)
            term.sgr_fg = p - 30, term.sgr_fg_custom = 0;
        else if (p >= 90 && p <= 97)
            term.sgr_fg = p - 90 + 8, term.sgr_fg_custom = 0;
        else if (p == 39)
            term.sgr_fg = def_fg, term.sgr_fg_custom = 0;
        else if (p >= 40 && p <= 47)
            term.sgr_bg = p - 40, term.sgr_bg_custom = 0;
        else if (p >= 100 && p <= 107)
            term.sgr_bg = p - 100 + 8, term.sgr_bg_custom = 0;
        else if (p == 49)
            term.sgr_bg = def_bg, term.sgr_bg_custom = 0;
        else if (p == 38 || p == 48) {
            color *selected = p == 38 ? &term.sgr_fg_color : &term.sgr_bg_color;
            int *custom = p == 38 ? &term.sgr_fg_custom : &term.sgr_bg_custom;

            if (i + 2 < n && term.csi_p[i + 1] == 5) {
                int index = term.csi_p[i + 2];
                if (index >= 0 && index <= 255) {
                    *selected = vt_palette_256(index);
                    *custom = 1;
                }
                i += 2;
            } else if (i + 4 < n && term.csi_p[i + 1] == 2) {
                int r = term.csi_p[i + 2], g = term.csi_p[i + 3], b = term.csi_p[i + 4];
                if (r <= 255 && g <= 255 && b <= 255) {
                    *selected = (color){r, g, b, 255};
                    *custom = 1;
                }
                i += 4;
            }
        }
    }
}

static void vt_ed(int mode)
{
    int i, n = term.cols * term.rows;
    int cur = term.cursor_row * term.cols + term.cursor_col;

    if (mode == 3) {
        term.history_count = 0;
        term.history_head = 0;
        term.history_offset = 0;
        return;
    }
    if (mode == 2)
        cur = 0, mode = 0;
    if (mode == 0) {
        for (i = cur; i < n; i++)
            vt_blank_cell(&term.cells[i]);
    } else if (mode == 1) {
        for (i = 0; i <= cur; i++)
            vt_blank_cell(&term.cells[i]);
    }
}

static void vt_el(int mode)
{
    int i;
    int row = term.cursor_row * term.cols;

    if (mode == 0) {
        for (i = term.cursor_col; i < term.cols; i++)
            vt_blank_cell(&term.cells[row + i]);
    } else if (mode == 1) {
        for (i = 0; i <= term.cursor_col; i++)
            vt_blank_cell(&term.cells[row + i]);
    } else if (mode == 2) {
        vt_clear_row(term.cursor_row);
    }
}

static void vt_edit_chars(uint8_t cmd, int count)
{
    vt_cell *line = term.cells + (size_t)term.cursor_row * term.cols;
    int col = term.cursor_col;
    int remaining = term.cols - col;

    if (count > remaining) count = remaining;
    if (count < 1) return;
    if (cmd == '@') {
        memmove(line + col + count, line + col,
                (size_t)(remaining - count) * sizeof(*line));
    } else if (cmd == 'P') {
        memmove(line + col, line + col + count,
                (size_t)(remaining - count) * sizeof(*line));
        col += remaining - count;
    }
    for (int i = 0; i < count; i++)
        vt_blank_cell(&line[col + i]);
}

static void vt_csi_final(uint8_t cmd)
{
    int n;

    if (term.csi_priv) {
        if (cmd == 'h' || cmd == 'l') {
            for (int i = 0; i <= term.csi_i; i++) {
                switch (term.csi_p[i]) {
                case 7:
                    term.autowrap = (cmd == 'h');
                    if (!term.autowrap) term.wrap_pending = 0;
                    break;
                case 25: term.cursor_visible = (cmd == 'h'); break;
                case 1000: case 1002: case 1003:
                    if (cmd == 'h') term.mouse_mode = term.csi_p[i];
                    else if (term.mouse_mode == term.csi_p[i]) term.mouse_mode = 0;
                    break;
                case 1006: term.mouse_sgr = (cmd == 'h'); break;
                case 47: case 1047: case 1049:
                    if (cmd == 'h') vt_enter_alt();
                    else vt_leave_alt();
                    break;
                case 1048:
                    if (cmd == 'h') {
                        term.saved_col = term.cursor_col;
                        term.saved_row = term.cursor_row;
                    } else {
                        term.cursor_col = term.saved_col;
                        term.cursor_row = term.saved_row;
                        vt_clamp_cursor();
                    }
                    break;
                }
            }
        }
        return;
    }

    switch (cmd) {
    case 'A':
        term.wrap_pending = 0;
        term.cursor_row -= csi_num(0, 1);
        break;
    case 'B':
        term.wrap_pending = 0;
        term.cursor_row += csi_num(0, 1);
        break;
    case 'C':
        term.wrap_pending = 0;
        term.cursor_col += csi_num(0, 1);
        break;
    case 'D':
        term.wrap_pending = 0;
        term.cursor_col -= csi_num(0, 1);
        break;
    case 'H':
    case 'f':
        term.wrap_pending = 0;
        term.cursor_row = csi_num(0, 1) - 1;
        term.cursor_col = csi_num(1, 1) - 1;
        break;
    case 'G':
        term.wrap_pending = 0;
        term.cursor_col = csi_num(0, 1) - 1;
        break;
    case 'd':
        term.wrap_pending = 0;
        term.cursor_row = csi_num(0, 1) - 1;
        break;
    case 'E':
        term.wrap_pending = 0;
        term.cursor_row += csi_num(0, 1);
        term.cursor_col = 0;
        break;
    case 'F':
        term.wrap_pending = 0;
        term.cursor_row -= csi_num(0, 1);
        term.cursor_col = 0;
        break;
    case '@': case 'P': case 'X':
        term.wrap_pending = 0;
        vt_edit_chars(cmd, csi_num(0, 1));
        break;
    case 'J':
        vt_ed(term.csi_p[0]);
        break;
    case 'K':
        vt_el(term.csi_p[0]);
        break;
    case 'm':
        vt_sgr();
        break;
    case 'r':
        term.wrap_pending = 0;
        term.scroll_top = csi_num(0, 1) - 1;
        term.scroll_bot = csi_num(1, term.rows) - 1;
        if (term.scroll_top < 0)
            term.scroll_top = 0;
        if (term.scroll_bot >= term.rows)
            term.scroll_bot = term.rows - 1;
        if (term.scroll_top > term.scroll_bot)
            term.scroll_top = 0, term.scroll_bot = term.rows - 1;
        term.cursor_col = term.cursor_row = 0;
        break;
    case 'n':
        if (reply_writer) {
            if (term.csi_p[0] == 5)
                reply_writer("\033[0n", 4);
            else if (term.csi_p[0] == 6) {
                char reply[32];
                int len = snprintf(reply, sizeof(reply), "\033[%d;%dR",
                                   term.cursor_row + 1, term.cursor_col + 1);
                if (len > 0 && len < (int)sizeof(reply))
                    reply_writer(reply, (size_t)len);
            }
        }
        break;
    case 'c':
        if (reply_writer && term.csi_p[0] == 0)
            reply_writer("\033[?6c", 5);
        break;
    case 's':
        term.saved_col = term.cursor_col;
        term.saved_row = term.cursor_row;
        break;
    case 'u':
        term.wrap_pending = 0;
        term.cursor_col = term.saved_col;
        term.cursor_row = term.saved_row;
        break;
    case 'L':
        n = csi_num(0, 1);
        {
            int save_top = term.scroll_top;
            term.scroll_top = term.cursor_row;
            vt_scroll_down(n);
            term.scroll_top = save_top;
        }
        break;
    case 'M':
        n = csi_num(0, 1);
        {
            int save_top = term.scroll_top;
            term.scroll_top = term.cursor_row;
            vt_scroll_up(n, 0);
            term.scroll_top = save_top;
        }
        break;
    default:
        break;
    }
    vt_clamp_cursor();
}

static void vt_csi_reset(void)
{
    memset(term.csi_p, 0, sizeof(term.csi_p));
    term.csi_i = 0;
    term.csi_priv = 0;
}

void terminal_feed(const uint8_t *buf, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        uint8_t ch = buf[i];

        if (term.st == ST_GROUND && term.utf_remaining) {
            if ((ch & 0xc0) == 0x80) {
                term.utf_codepoint = (term.utf_codepoint << 6) | (ch & 0x3f);
                if (--term.utf_remaining == 0) {
                    uint32_t cp = term.utf_codepoint;
                    vt_put(cp >= term.utf_min && cp <= 0x10ffff &&
                           !(cp >= 0xd800 && cp <= 0xdfff) ? cp : 0xfffd);
                }
                continue;
            }
            vt_put(0xfffd);
            term.utf_remaining = 0;
        }
        if (term.st == ST_CHARSET) {
            term.st = ST_GROUND;
            continue;
        }
        if (term.st == ST_OSC || term.st == ST_STRING) {
            if (ch == 0x9c || (ch == 0x07 && term.st == ST_OSC))
                term.st = ST_GROUND;
            else if (ch == 0x1b)
                term.st = term.st == ST_OSC ? ST_OSC_ESC : ST_STRING_ESC;
            continue;
        }
        if (term.st == ST_OSC_ESC || term.st == ST_STRING_ESC) {
            if (ch == '\\')
                term.st = ST_GROUND;
            else if (ch != 0x1b)
                term.st = term.st == ST_OSC_ESC ? ST_OSC : ST_STRING;
            continue;
        }
        if (term.st == ST_ESC) {
            if (ch == '[') {
                term.st = ST_CSI;
                vt_csi_reset();
                continue;
            }
            if (ch == ']') {
                term.st = ST_OSC;
                continue;
            }
            if (ch == 'P' || ch == 'X' || ch == '^' || ch == '_') {
                term.st = ST_STRING;
                continue;
            }
            if (ch == '(' || ch == ')') {
                term.st = ST_CHARSET;
                continue;
            }
            if (ch == 'M') {
                term.wrap_pending = 0;
                vt_scroll_down(1);
            } else if (ch == 'D')
                vt_newline();
            else if (ch == 'E') {
                term.cursor_col = 0;
                vt_newline();
            }
            else if (ch == '7') {
                term.saved_col = term.cursor_col;
                term.saved_row = term.cursor_row;
            } else if (ch == '8') {
                term.cursor_col = term.saved_col;
                term.cursor_row = term.saved_row;
                term.wrap_pending = 0;
                vt_clamp_cursor();
            }
            term.st = ST_GROUND;
            continue;
        }

        if (term.st == ST_CSI) {
            if (ch == '?' && term.csi_i == 0 && term.csi_p[0] == 0) {
                term.csi_priv = 1;
                continue;
            }
            if (ch >= '0' && ch <= '9') {
                term.csi_p[term.csi_i] = term.csi_p[term.csi_i] * 10 + (ch - '0');
                continue;
            }
            if (ch == ';') {
                if (term.csi_i + 1 < CSI_MAX)
                    term.csi_i++;
                continue;
            }
            if (ch >= 0x40 && ch <= 0x7e) {
                vt_csi_final(ch);
                term.st = ST_GROUND;
                continue;
            }
            term.st = ST_GROUND;
            continue;
        }

        if (ch == 0x1b) {
            term.st = ST_ESC;
            continue;
        }
        if (ch == 0x0e) {
            term.graph = 1;
            continue;
        }
        if (ch == 0x0f) {
            term.graph = 0;
            continue;
        }
        if (ch == '\n') {
            vt_newline();
            continue;
        }
        if (ch == '\r') {
            term.cursor_col = 0;
            term.wrap_pending = 0;
            continue;
        }
        if (ch == '\b') {
            term.wrap_pending = 0;
            if (term.cursor_col > 0)
                term.cursor_col--;
            continue;
        }
        if (ch == '\t') {
            term.wrap_pending = 0;
            term.cursor_col = (term.cursor_col + 8) & ~7;
            if (term.cursor_col >= term.cols)
                term.cursor_col = term.cols - 1;
            continue;
        }
        if (ch >= 0xc2 && ch <= 0xdf) {
            term.utf_codepoint = ch & 0x1f;
            term.utf_min = 0x80;
            term.utf_remaining = 1;
        } else if (ch >= 0xe0 && ch <= 0xef) {
            term.utf_codepoint = ch & 0x0f;
            term.utf_min = 0x800;
            term.utf_remaining = 2;
        } else if (ch >= 0xf0 && ch <= 0xf4) {
            term.utf_codepoint = ch & 0x07;
            term.utf_min = 0x10000;
            term.utf_remaining = 3;
        } else if (ch >= 0x80) {
            vt_put(0xfffd);
        } else if (ch >= 32)
            vt_put(ch);
    }
}

int terminal_resize(int cols, int rows)
{
    vt_cell *cells;
    vt_cell *primary = NULL;
    vt_cell *history = NULL;
    size_t n;

    if (cols < 1 || rows < 1)
        return -1;
    n = (size_t)cols * rows;
    cells = malloc(n * sizeof(*cells));
    if (!cells) return -1;
    if (term.alt_active) {
        primary = malloc(n * sizeof(*primary));
        if (!primary) {
            free(cells);
            return -1;
        }
    }
    if (term.history_capacity) {
        history = calloc((size_t)term.history_capacity * cols, sizeof(*history));
        if (!history) {
            free(primary);
            free(cells);
            return -1;
        }
    }
    free(term.cells);
    if (term.alt_active) free(term.primary.cells);
    free(term.history);
    term.history = history;
    term.history_count = term.history_head = term.history_offset = 0;
    term.cells = cells;
    if (term.alt_active) {
        term.primary = (terminal_screen){ .cells = primary, .cursor_visible = 1,
                                            .scroll_bot = rows - 1, .sgr_fg = def_fg };
        for (size_t i = 0; i < n; i++)
            primary[i] = (vt_cell){ ' ', terminal_palette[def_fg],
                                    terminal_palette[def_bg], 0 };
    }
    term.cols = cols;
    term.rows = rows;
    term.cursor_col = term.cursor_row = 0;
    term.saved_col = term.saved_row = 0;
    term.scroll_top = 0;
    term.scroll_bot = rows - 1;
    term.wrap_pending = 0;
    term.utf_remaining = 0;
    vt_reset_sgr();
    for (size_t i = 0; i < n; i++)
        vt_blank_cell(&term.cells[i]);
    return 0;
}

int terminal_enable_history(int lines)
{
    vt_cell *history;

    if (lines <= 0 || term.cols <= 0)
        return -1;
    history = calloc((size_t)lines * term.cols, sizeof(*history));
    if (!history) return -1;
    free(term.history);
    term.history = history;
    term.history_capacity = lines;
    term.history_count = term.history_head = term.history_offset = 0;
    return 0;
}

int terminal_scroll(int lines)
{
    int old = term.history_offset;

    if (term.alt_active) return 0;
    term.history_offset += lines;
    if (term.history_offset < 0) term.history_offset = 0;
    if (term.history_offset > term.history_count)
        term.history_offset = term.history_count;
    return term.history_offset != old;
}

int terminal_scroll_bottom(void)
{
    return terminal_scroll(-term.history_offset);
}

const vt_cell *terminal_row(int row)
{
    int index;

    if (row < 0 || row >= term.rows) return NULL;
    if (term.alt_active) return term.cells + (size_t)row * term.cols;
    index = term.history_count - term.history_offset + row;
    if (index >= term.history_count)
        return term.cells + (size_t)(index - term.history_count) * term.cols;
    index = (term.history_head - term.history_count + index + term.history_capacity)
          % term.history_capacity;
    return term.history + (size_t)index * term.cols;
}

void terminal_free(void)
{
    free(term.cells);
    term.cells = NULL;
    if (term.alt_active) free(term.primary.cells);
    term.primary = (terminal_screen){0};
    term.alt_active = 0;
    free(term.history);
    term.history = NULL;
    term.history_capacity = term.history_count = term.history_head = term.history_offset = 0;
    reply_writer = NULL;
}
