#ifndef SHARED_APP_FONT_H
#define SHARED_APP_FONT_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_FONT_ALIGN_LEFT 0
#define APP_FONT_ALIGN_CENTER 1
#define APP_FONT_ALIGN_RIGHT 2

typedef void (*app_font_plot)(void *user, int x, int y, uint32_t rgb, int alpha);

void app_font_set_root(const char *root);
int app_font_file(char *out, size_t max, int bold);
int app_font_register(void);

float app_font_measure_wide(const wchar_t *text, float px, int weight);
float app_font_measure_utf8(const char *text, float px, int weight);
void app_font_draw_wide(
    app_font_plot plot,
    void *user,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    float px,
    int weight,
    uint32_t rgb,
    int alpha,
    int align
);
void app_font_draw_utf8(
    app_font_plot plot,
    void *user,
    float x,
    float y,
    const char *text,
    float px,
    int weight,
    uint32_t rgb,
    int alpha,
    int tracking
);

#ifdef __cplusplus
}
#endif

#endif
