#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

typedef struct {
    int32_t x;
    int32_t y;
} point2d;

typedef struct {
    int32_t width;
    int32_t height;
} size2d;

typedef struct {
    point2d pos;
    size2d size;
} rect2d;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} color;

#define IDENT_COLOR (color){255, 255, 255, 255}
#define TRANSPARENT_COLOR (color){0, 0, 0, 0}
#define BLACK_COLOR (color){0, 0, 0, 255}


#endif