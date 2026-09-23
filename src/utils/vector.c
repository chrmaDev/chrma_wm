#ifndef VECTOR_C
#define VECTOR_C

#include "utils/vector.h"
#include <stdlib.h>
#include <string.h>

static void *vector_at(const Vector *v, size_t index) {
    return (char *)v->data + index * v->element_size;
}

int vector_init(Vector *v, size_t element_size) {
    if (element_size == 0) {
        return -1;
    }
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
    v->element_size = element_size;
    return 0;
}

int vector_reserve(Vector *v, size_t capacity) {
    if (capacity > v->capacity) {
        void *data = realloc(v->data, capacity * v->element_size);
        if (data == NULL) {
            return -1;
        }
        v->data = data;
        v->capacity = capacity;
    }
    return 0;
}

int vector_push(Vector *v, const void *item) {
    if (v->size == v->capacity) {
        size_t capacity = v->capacity ? v->capacity * 2 : 1;
        if (vector_reserve(v, capacity) < 0) {
            return -1;
        }
    }
    memcpy(vector_at(v, v->size), item, v->element_size);
    v->size++;
    return 0;
}

void *vector_get(Vector *v, size_t index) {
    return vector_at(v, index);
}

void vector_set(Vector *v, size_t index, const void *item) {
    memcpy(vector_at(v, index), item, v->element_size);
}

void *vector_back(Vector *v) {
    if (v->size == 0) {
        return NULL;
    }
    return vector_at(v, v->size - 1);
}

void vector_pop(Vector *v) {
    if (v->size > 0) {
        v->size--;
    }
}

void vector_remove(Vector *v, size_t index) {
    if (index >= v->size) {
        return;
    }
    if (index + 1 < v->size) {
        memmove(vector_at(v, index), vector_at(v, index + 1),
                (v->size - index - 1) * v->element_size);
    }
    v->size--;
}

void vector_free(Vector *v) {
    free(v->data);
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
    v->element_size = 0;
}

void vector_swap(Vector *v, size_t index1, size_t index2) {
    char temp[v->element_size];
    void *a = vector_at(v, index1);
    void *b = vector_at(v, index2);
    memcpy(temp, a, v->element_size);
    memcpy(a, b, v->element_size);
    memcpy(b, temp, v->element_size);
}
#endif

void vector_clear(Vector *v) {
    v->size = 0;
}