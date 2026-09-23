#include "rendering.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static inline uint8_t blend_channel(uint8_t src, uint8_t dst, uint8_t alpha)
{
    return (uint8_t)((src * alpha + dst * (255 - alpha) + 127) / 255);
}

static inline color blend_over(color src, color dst)
{
    uint8_t a = src.a;

    if (a == 0)
        return dst;
    if (a == 255)
        return src;
    return (color){
        blend_channel(src.r, dst.r, a),
        blend_channel(src.g, dst.g, a),
        blend_channel(src.b, dst.b, a),
        (uint8_t)(a + (uint16_t)dst.a * (255 - a) / 255),
    };
}

static color read_pixel(const pixmap *p, const uint8_t *px)
{
    color c;

    if (p->bgr) {
        c.r = px[2];
        c.g = px[1];
        c.b = px[0];
    } else {
        c.r = px[0];
        c.g = px[1];
        c.b = px[2];
    }
    /* Canvases are XRGB; the fourth byte is unused padding. */
    c.a = 255;
    return c;
}

static void write_pixel_raw(const pixmap *p, uint8_t *px, color c)
{
    if (p->pixel_stride == 4) {
        uint32_t packed = p->bgr
            ? ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b
            : ((uint32_t)c.a << 24) | ((uint32_t)c.b << 16) | ((uint32_t)c.g << 8) | c.r;
        memcpy(px, &packed, 4);
        return;
    }
    if (p->bgr) {
        px[0] = c.b;
        px[1] = c.g;
        px[2] = c.r;
    } else {
        px[0] = c.r;
        px[1] = c.g;
        px[2] = c.b;
    }
}

int pixmap_create(pixmap *p, uint32_t width, uint32_t height, uint8_t bgr)
{
    if (!p || !width || !height)
        return -1;
    p->width = width;
    p->height = height;
    p->pixel_stride = 4;
    p->pitch = width * 4;
    p->bgr = bgr;
    p->data = malloc((size_t)p->pitch * height);
    if (!p->data) {
        memset(p, 0, sizeof(*p));
        return -1;
    }
    return 0;
}

void pixmap_destroy(pixmap *p)
{
    if (!p)
        return;
    free(p->data);
    memset(p, 0, sizeof(*p));
}

#define FONT_W 8
#define FONT_H 16

static int in_bounds(const pixmap *p, int x, int y)
{
    return p && p->data && x >= 0 && y >= 0
        && (uint32_t)x < p->width && (uint32_t)y < p->height;
}

static void put_pixel(pixmap *p, int x, int y, color c)
{
    uint8_t *px;
    color dst;

    if (!in_bounds(p, x, y) || c.a == 0)
        return;
    px = p->data + (size_t)y * p->pitch + (size_t)x * p->pixel_stride;
    if (c.a == 255) {
        write_pixel_raw(p, px, c);
        return;
    }
    dst = read_pixel(p, px);
    write_pixel_raw(p, px, blend_over(c, dst));
}

void draw_rect(pixmap *dst, rect2d rect, color fill)
{
    int32_t x, y;
    int32_t x0, y0, x1, y1;

    if (!dst || !dst->data || rect.size.width <= 0 || rect.size.height <= 0)
        return;

    x0 = rect.pos.x;
    y0 = rect.pos.y;
    x1 = rect.pos.x + rect.size.width;
    y1 = rect.pos.y + rect.size.height;

    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > (int32_t)dst->width)
        x1 = (int32_t)dst->width;
    if (y1 > (int32_t)dst->height)
        y1 = (int32_t)dst->height;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++)
            put_pixel(dst, x, y, fill);
    }
}

void draw_buffer_ex(pixmap *dst, rect2d dst_rect, const pixmap *src, rect2d src_rect, color tint)
{
    int32_t x, y;
    int32_t dw, dh, sw, sh;
    int32_t dx, dy, x0, y0, x1, y1;
    int ident;
    uint32_t sstride;

    if (!dst || !src || !dst->data || !src->data)
        return;

    dw = dst_rect.size.width;
    dh = dst_rect.size.height;
    sw = src_rect.size.width;
    sh = src_rect.size.height;
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return;

    dx = dst_rect.pos.x;
    dy = dst_rect.pos.y;
    x0 = dx;
    y0 = dy;
    x1 = dx + dw;
    y1 = dy + dh;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > (int32_t)dst->width)
        x1 = (int32_t)dst->width;
    if (y1 > (int32_t)dst->height)
        y1 = (int32_t)dst->height;
    if (x0 >= x1 || y0 >= y1)
        return;

    ident = tint.r == 255 && tint.g == 255 && tint.b == 255 && tint.a == 255;
    sstride = src->pixel_stride;

    for (y = y0; y < y1; y++) {
        int32_t sy = src_rect.pos.y + (int32_t)((int64_t)(y - dy) * sh / dh);
        const uint8_t *srow;

        if (sy < 0 || sy >= (int32_t)src->height)
            continue;
        srow = src->data + (size_t)sy * src->pitch;

        for (x = x0; x < x1; x++) {
            int32_t sx = src_rect.pos.x + (int32_t)((int64_t)(x - dx) * sw / dw);
            const uint8_t *s;
            uint8_t r, g, b, a;
            color out;

            if (sx < 0 || sx >= (int32_t)src->width)
                continue;
            s = srow + (size_t)sx * sstride;

            if (src->bgr) {
                b = s[0];
                g = s[1];
                r = s[2];
            } else {
                r = s[0];
                g = s[1];
                b = s[2];
            }
            a = sstride > 3 ? s[3] : 255;
            if (!ident) {
                r = (uint8_t)((r * tint.r) / 255);
                g = (uint8_t)((g * tint.g) / 255);
                b = (uint8_t)((b * tint.b) / 255);
                a = (uint8_t)((a * tint.a) / 255);
            }
            out = (color){r, g, b, a};
            put_pixel(dst, x, y, out);
        }
    }
}
void render_line_dotted(pixmap *dst, point2d p1, point2d p2, uint8_t period, uint8_t freq, color color) {
    //bresenham's line algorithm

}
void draw_buffer(pixmap *dst, int x, int y, const pixmap *src)
{
    rect2d d, s;
    color ident = {255, 255, 255, 255};

    if (!src)
        return;

    d.pos.x = x;
    d.pos.y = y;
    d.size.width = (int32_t)src->width;
    d.size.height = (int32_t)src->height;
    s.pos.x = 0;
    s.pos.y = 0;
    s.size.width = (int32_t)src->width;
    s.size.height = (int32_t)src->height;
    draw_buffer_ex(dst, d, src, s, ident);
}

void draw_integer_scale(pixmap *dst, const pixmap *src, int scale)
{
    rect2d d, s;
    color ident = {255, 255, 255, 255};
    color black = {0, 0, 0, 255};
    int32_t dw, dh;

    if (!dst || !src || !dst->data || !src->data || scale < 1)
        return;

    dw = (int32_t)src->width * scale;
    dh = (int32_t)src->height * scale;
    d.pos.x = ((int32_t)dst->width - dw) / 2;
    d.pos.y = ((int32_t)dst->height - dh) / 2;
    d.size.width = dw;
    d.size.height = dh;
    s.pos.x = 0;
    s.pos.y = 0;
    s.size.width = (int32_t)src->width;
    s.size.height = (int32_t)src->height;

    if (d.pos.x != 0 || d.pos.y != 0
        || dw != (int32_t)dst->width || dh != (int32_t)dst->height)
        draw_rect(dst, (rect2d){{0, 0}, {(int32_t)dst->width, (int32_t)dst->height}}, black);
    draw_buffer_ex(dst, d, src, s, ident);
}
