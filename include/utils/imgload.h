#ifndef IMGLOAD_H
#define IMGLOAD_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "rendering.h"

int load_qoi(const char *filename, pixmap *image);
void decompress_qoi(FILE *file, size_t size, pixmap *image);

#endif