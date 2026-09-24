#ifndef CT_TERMINAL_H
#define CT_TERMINAL_H

#include <stddef.h>
#include <stdint.h>

#include "types.h"

#define CT_COLS 80
#define CT_ROWS 24

typedef struct {
    uint32_t glyph;
    color foreground;
    color background;
    uint8_t continuation;
} vt_cell;

typedef struct {
    vt_cell *cells;
    int cursor_col, cursor_row, cursor_visible;
    int saved_col, saved_row;
    int scroll_top, scroll_bot;
    int sgr_fg, sgr_bg, bold, reverse, graph;
    color sgr_fg_color, sgr_bg_color;
    int sgr_fg_custom, sgr_bg_custom;
    int wrap_pending;
    int history_offset;
} terminal_screen;

typedef struct {
    int cols, rows;
    int cursor_col, cursor_row, cursor_visible;
    int saved_col, saved_row;
    int scroll_top, scroll_bot;
    int sgr_fg, sgr_bg, bold, reverse, graph;
    color sgr_fg_color, sgr_bg_color;
    int sgr_fg_custom, sgr_bg_custom;
    int wrap_pending, autowrap, alt_active;
    int mouse_mode, mouse_sgr;
    int st, csi_priv, csi_i, csi_p[16];
    uint32_t utf_codepoint, utf_min;
    int utf_remaining;
    vt_cell *cells;
    terminal_screen primary;
    vt_cell *history;
    int history_capacity, history_count, history_head, history_offset;
} terminal;

extern terminal term;
extern const color terminal_palette[16];

int terminal_resize(int cols, int rows);
int terminal_enable_history(int lines);
int terminal_scroll(int lines);
int terminal_scroll_bottom(void);
const vt_cell *terminal_row(int row);
char terminal_display_glyph(uint32_t codepoint);
void terminal_set_reply(void (*write_reply)(const char *, size_t));
void terminal_feed(const uint8_t *buf, size_t n);
void terminal_free(void);

#endif
