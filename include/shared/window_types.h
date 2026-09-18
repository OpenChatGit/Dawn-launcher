#ifndef SHARED_WINDOW_TYPES_H
#define SHARED_WINDOW_TYPES_H

#ifdef _WIN32
#include <windows.h>
#endif

#include "shared/draw.h"

typedef struct HostWindow {
#ifdef _WIN32
    HWND hwnd;
    HDC hdc;
    HDC back_dc;
    HBITMAP back_bitmap;
    HBITMAP old_bitmap;
    HFONT font;
    HFONT old_font;
#else
    void *display;
    unsigned long xwindow;
    SoftDc soft;
    void *ximage;
    void *gc;
#endif
    uint32_t *pixels;
    int width;
    int height;
    float dpi_scale;
    float corner_radius;
    int running;
    int ready;
    int shown;
    int mouse_x;
    int mouse_y;
    int mouse_down;
    int mouse_pressed;
    int mouse_released;
    char text[32];
    int text_len;
    int key;
} HostWindow;

#endif
