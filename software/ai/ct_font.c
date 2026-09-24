#include "ct_font.h"

#include <string.h>

static const char glyphs[] =
    " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
    "abcdefghijklmnopqrstuvwxyz{|}~";

static int same_color(Color a, Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

Font LoadCtFont(const char *path)
{
    Font font = {0};
    Image image = LoadImage(path);
    Color *pixels;
    Color separator;
    int start = -1;
    int count = 0;
    const int expected = sizeof(glyphs);

    if (!image.data || image.width <= 0 || image.height <= 0)
        return font;

    pixels = LoadImageColors(image);
    if (!pixels) {
        UnloadImage(image);
        return font;
    }
    separator = pixels[0];

    font.recs = MemAlloc((size_t)expected * sizeof(*font.recs));
    font.glyphs = MemAlloc((size_t)expected * sizeof(*font.glyphs));
    if (!font.recs || !font.glyphs)
        goto failed;
    memset(font.glyphs, 0, (size_t)expected * sizeof(*font.glyphs));

    for (int x = 0; x <= image.width; x++) {
        int is_separator = x == image.width || same_color(pixels[x], separator);
        if (is_separator) {
            if (start >= 0) {
                if (count >= expected)
                    goto failed;
                font.recs[count] = (Rectangle){(float)start, 0, (float)(x - start), (float)image.height};
                font.glyphs[count].value = count == 0 ? 0 : (unsigned char)glyphs[count - 1];
                font.glyphs[count].advanceX = x - start;
                count++;
                start = -1;
            }
        } else if (start < 0) {
            start = x;
        }
    }
    if (count != expected)
        goto failed;

    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            Color *pixel = &pixels[(size_t)y * image.width + x];
            if (same_color(*pixel, separator))
                *pixel = BLANK;
        }
    }

    Image atlas = {pixels, image.width, image.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    font.texture = LoadTextureFromImage(atlas);
    if (font.texture.id == 0)
        goto failed;
    font.baseSize = image.height;
    font.glyphCount = count;
    SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);
    UnloadImageColors(pixels);
    UnloadImage(image);
    return font;

failed:
    if (font.recs) MemFree(font.recs);
    if (font.glyphs) MemFree(font.glyphs);
    UnloadImageColors(pixels);
    UnloadImage(image);
    return (Font){0};
}
