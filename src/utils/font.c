#ifndef LOADFONT_C
#define LOADFONT_C

#include "utils/font.h"
#include "utils/imgload.h"
#include <stdlib.h>
#include <string.h>

static int is_control(const pixmap *atlas, uint32_t x, color key) {
    const uint8_t *px = atlas->data + (size_t)x * atlas->pixel_stride;
    return px[0] == key.r && px[1] == key.g && px[2] == key.b && px[3] == key.a;
}

static void store_glyph(font *f, const char *array, uint32_t *j, int start, int end,
                        int *fallback_pending) {
    unsigned char ch;

    if (start < 0 || end <= start)
        return;
    if (*fallback_pending) {
        f->x[0] = start;
        f->width[0] = end - start;
        *fallback_pending = 0;
        return;
    }
    if (!array[*j])
        return;
    ch = (unsigned char)array[*j];
    f->x[ch] = start;
    f->width[ch] = end - start;
    (*j)++;
}

int font_create(const char *path, font *f, const char *array, int first_is_fallback) {
    color control_key;
    uint32_t i;
    uint32_t j = 0;
    int start = -1;
    int fallback_pending = first_is_fallback ? 1 : 0;
    if (!array || !load_qoi(path, &f->atlas))
        return -1;

    f->height = (int)f->atlas.height;
    memset(f->x, 0, sizeof(f->x));
    memset(f->width, 0, sizeof(f->width));

    control_key = (color){
        f->atlas.data[0],
        f->atlas.data[1],
        f->atlas.data[2],
        f->atlas.data[3],
    };

    for (i = 0; i < f->atlas.width; i++) {
        if (is_control(&f->atlas, i, control_key)) {
            store_glyph(f, array, &j, start, (int)i, &fallback_pending);
            start = -1;
        } else if (start < 0) {
            start = (int)i;
        }
    }
    store_glyph(f, array, &j, start, (int)f->atlas.width, &fallback_pending);
    return 0;
}

int font_destroy(font *f) {
    free(f->atlas.data);
    f->atlas.data = NULL;
    return 0;
}

void font_draw(pixmap *dst, font *f, const char *text, int x, int y, color fg, color bg) {
    int cx = x;
    int cy = y;
    int i;

    if (!dst || !f || !text)
        return;

    for (i = 0; text[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)text[i];
        int gw;
        rect2d src, dest;

        if (ch == '\n') {
            cx = x;
            cy += f->height;
            continue;
        }

        gw = f->width[ch];
        src.pos.x = f->x[ch];
        if (gw <= 0) {
            gw = f->width[0];
            src.pos.x = f->x[0];
            if (gw <= 0)
                continue;
        }

        dest.pos.x = cx;
        dest.pos.y = cy;
        dest.size.width = gw;
        dest.size.height = f->height;

        if (bg.a)
            draw_rect(dst, dest, bg);

        src.pos.y = 0;
        src.size.width = gw;
        src.size.height = f->height;
        draw_buffer_ex(dst, dest, &f->atlas, src, fg);

        cx += gw;
    }
}
#endif
