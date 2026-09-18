#ifndef SHARED_TYPES_H
#define SHARED_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_KILOBYTES(n) ((n) * 1024u)
#define APP_MEGABYTES(n) ((n) * 1024u * 1024u)

#define APP_RGB(r, g, b) \
    ((uint32_t)(((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))

#endif
