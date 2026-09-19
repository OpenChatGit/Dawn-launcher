#ifndef SHARED_SOFT_FONT_H
#define SHARED_SOFT_FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#define SOFT_FONT_COLS 5
#define SOFT_FONT_ROWS 7
#define SOFT_FONT_ADVANCE 6

int soft_font_pixel(int ch, int col, int row);

#ifdef __cplusplus
}
#endif

#endif
