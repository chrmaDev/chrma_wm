//QOI image loader because I'm a degen

#include "utils/imgload.h"


static inline uint8_t hash_color(color pixel) {
    return (uint8_t)((pixel.r * 3 + pixel.g * 5 + pixel.b * 7 + pixel.a * 11) % 64);
}

static inline int read_u8(FILE *file, uint8_t *out) {
    int c = fgetc(file);
    if (c == EOF)
        return 0;
    *out = (uint8_t)c;
    return 1;
}

static inline void emit_pixel(uint8_t **out, uint8_t *end, color pixel, color seen_pixels[64], color *previous) {
    if (*out + sizeof(color) > end)
        return;
    seen_pixels[hash_color(pixel)] = pixel;
    memcpy(*out, &pixel, sizeof(color));
    *out += sizeof(color);
    *previous = pixel;
}

void decompress_qoi(FILE *file, size_t size, pixmap *image) {
    uint8_t *out = image->data;
    uint8_t *end = image->data + size;
    color seen_pixels[64];
    color previous_pixel = {0, 0, 0, 255};
    uint8_t byte;

    memset(seen_pixels, 0, sizeof(seen_pixels));
    fseek(file, 14, SEEK_SET);

    while (out + sizeof(color) <= end) {
        if (!read_u8(file, &byte))
            break;

        if (byte == 0xFE) { /* QOI_OP_RGB */
            color pixel = previous_pixel;
            if (!read_u8(file, &pixel.r) || !read_u8(file, &pixel.g) || !read_u8(file, &pixel.b))
                break;
            emit_pixel(&out, end, pixel, seen_pixels, &previous_pixel);
        } else if (byte == 0xFF) { /* QOI_OP_RGBA */
            color pixel;
            if (!read_u8(file, &pixel.r) || !read_u8(file, &pixel.g)
                || !read_u8(file, &pixel.b) || !read_u8(file, &pixel.a))
                break;
            emit_pixel(&out, end, pixel, seen_pixels, &previous_pixel);
        } else if ((byte & 0xC0) == 0x00) { /* QOI_OP_INDEX */
            emit_pixel(&out, end, seen_pixels[byte & 0x3F], seen_pixels, &previous_pixel);
        } else if ((byte & 0xC0) == 0x40) { /* QOI_OP_DIFF */
            color pixel = previous_pixel;
            pixel.r += (uint8_t)(((byte >> 4) & 0x03) - 2);
            pixel.g += (uint8_t)(((byte >> 2) & 0x03) - 2);
            pixel.b += (uint8_t)((byte & 0x03) - 2);
            emit_pixel(&out, end, pixel, seen_pixels, &previous_pixel);
        } else if ((byte & 0xC0) == 0x80) { /* QOI_OP_LUMA */
            uint8_t second;
            int dg;
            color pixel = previous_pixel;

            if (!read_u8(file, &second))
                break;
            dg = (int)(byte & 0x3F) - 32;
            pixel.r += (uint8_t)(dg + ((int)(second >> 4) - 8));
            pixel.g += (uint8_t)dg;
            pixel.b += (uint8_t)(dg + ((int)(second & 0x0F) - 8));
            emit_pixel(&out, end, pixel, seen_pixels, &previous_pixel);
        } else { /* QOI_OP_RUN */
            int run = (int)(byte & 0x3F) + 1;
            int i;
            for (i = 0; i < run && out + sizeof(color) <= end; i++)
                emit_pixel(&out, end, previous_pixel, seen_pixels, &previous_pixel);
        }
    }
}

int load_qoi(const char *filename, pixmap *image)
{
    FILE *file = fopen(filename, "rb");
    uint8_t header[14];

    if (!file)
        return 0;

    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return 0;
    }

    if (header[0] != 'q' || header[1] != 'o' || header[2] != 'i' || header[3] != 'f') {
        fclose(file);
        return 0;
    }

    image->width = ((uint32_t)header[4] << 24) | ((uint32_t)header[5] << 16)
        | ((uint32_t)header[6] << 8) | (uint32_t)header[7];
    image->height = ((uint32_t)header[8] << 24) | ((uint32_t)header[9] << 16)
        | ((uint32_t)header[10] << 8) | (uint32_t)header[11];
    image->pixel_stride = 4;
    image->pitch = image->width * image->pixel_stride;
    image->bgr = 0;
    image->data = malloc((size_t)image->width * image->height * image->pixel_stride);
    if (!image->data) {
        fclose(file);
        return 0;
    }

    decompress_qoi(file, (size_t)image->width * image->height * image->pixel_stride, image);
    fclose(file);

    /* Match KMS XRGB8888 (B,G,R,X) once, so blits don't swizzle every frame. */
    {
        uint32_t y, x;
        for (y = 0; y < image->height; y++) {
            uint8_t *row = image->data + (size_t)y * image->pitch;
            for (x = 0; x < image->width; x++) {
                uint8_t *px = row + (size_t)x * image->pixel_stride;
                uint8_t t = px[0];
                px[0] = px[2];
                px[2] = t;
            }
        }
        image->bgr = 1;
    }
    return 1;
}
