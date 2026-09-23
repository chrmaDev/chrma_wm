#ifndef FONT_H
#define FONT_H

#include <stddef.h>
#include <stdint.h>
#include "types.h"
#include "rendering.h"

typedef struct { //spacing should be part of the atlas
    pixmap atlas;
    int height;
    int x[256];
    int width[256]; 
} font;

/* first_is_fallback: atlas strip 0 is the unknown glyph (mapped to '\0'). */
int font_create(const char *path, font *f, const char *array, int first_is_fallback);
int font_destroy(font *f);
void font_draw(pixmap *dst, font *f, const char *text, int x, int y, color fg, color bg);

#endif