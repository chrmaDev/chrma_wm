#ifndef VECTOR_H
#define VECTOR_H

#include <stddef.h>

typedef struct {
    void *data;
    size_t size;
    size_t capacity;
    size_t element_size;
} Vector;

int vector_init(Vector *v, size_t element_size);
int vector_reserve(Vector *v, size_t capacity);
int vector_push(Vector *v, const void *item);

void *vector_get(Vector *v, size_t index);
void vector_set(Vector *v, size_t index, const void *item);
void *vector_back(Vector *v);

void vector_clear(Vector *v);
void vector_pop(Vector *v);
void vector_remove(Vector *v, size_t index);
void vector_swap(Vector *v, size_t index1, size_t index2);
void vector_free(Vector *v);
#endif