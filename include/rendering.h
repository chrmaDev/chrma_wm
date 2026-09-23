#ifndef RENDERING_H
#define RENDERING_H

#include "types.h"
#include <stdint.h>

typedef struct {
    uint8_t *data;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t pixel_stride;
    uint8_t bgr; /* 1 = B,G,R in memory (KMS XRGB8888); 0 = R,G,B (QOI) */
} pixmap;

int pixmap_create(pixmap *p, uint32_t width, uint32_t height, uint8_t bgr);
void pixmap_destroy(pixmap *p);

void draw_char_stdfont(pixmap *dst, int x, int y, char c, color bg, color fg);
void draw_string_stdfont(pixmap *dst, int x, int y, const char *text, color bg, color fg);
void draw_rect(pixmap *dst, rect2d rect, color fill);

/* 1:1 blit of src at (x, y). */
void draw_buffer(pixmap *dst, int x, int y, const pixmap *src);

/*
 * Nearest-neighbour blit of src_rect onto dst_rect, then multiply by tint.
 * White tint {255,255,255,255} is identity. 9-slice = nine of these calls.
 */
void draw_buffer_ex(pixmap *dst, rect2d dst_rect, const pixmap *src, rect2d src_rect, color tint);

/* Nearest-neighbour integer upscale, centered. Uncovered dst pixels are black. */
void draw_integer_scale(pixmap *dst, const pixmap *src, int scale);

#endif
