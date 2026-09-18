#ifndef SHARED_DRAW_H
#define SHARED_DRAW_H

#include <stdint.h>

typedef struct SoftDc {
    uint32_t *pixels;
    int width;
    int height;
} SoftDc;

#endif
