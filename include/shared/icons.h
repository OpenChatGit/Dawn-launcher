#ifndef SHARED_ICONS_H
#define SHARED_ICONS_H

#include "shared/types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void icon_set_root(const char *project_root);
void icon_set_alpha(int alpha);

/*
 * Stroke icons use the Lucide 24x24 grid.
 * Download and Play load official Lucide SVGs from assets/icons.
 */

typedef enum IconId {
    ICON_X = 0,
    ICON_MINUS,
    ICON_SQUARE,
    ICON_GLOBE,
    ICON_SEARCH,
    ICON_SETTINGS,
    ICON_CHEVRON_DOWN,
    ICON_CHEVRON_UP,
    ICON_MENU,
    ICON_PLAY,
    ICON_PAUSE,
    ICON_USER,
    ICON_LOG_IN,
    ICON_STEAM,
    ICON_DOWNLOAD,
    ICON_COUNT
} IconId;

void icon_draw(
    void *hdc,
    IconId id,
    float cx,
    float cy,
    float size,
    uint32_t rgb,
    float stroke
);

void icon_fill_rect(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    uint32_t rgb,
    int alpha
);

void icon_round_rect(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    float radius,
    uint32_t rgb,
    int alpha
);

void icon_round_rect_corners(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    float tl,
    float tr,
    float br,
    float bl,
    uint32_t rgb,
    int alpha
);

void icon_round_stroke(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    float radius,
    uint32_t rgb,
    int alpha,
    float stroke
);

void icon_draw_label(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    float px,
    int weight
);

void icon_draw_label_alpha(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    float px,
    int weight,
    int alpha
);
void icon_draw_label_end_alpha(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    float px,
    int weight,
    int alpha
);
float icon_measure_label(void *hdc, const wchar_t *text, float px, int weight);

void icon_draw_label_shimmer(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    uint32_t shine,
    float px,
    int weight,
    int alpha,
    float phase
);

void icon_draw_label_center(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    float px,
    int weight
);

void icon_draw_label_center_alpha(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    float px,
    int weight,
    int alpha
);

void icon_draw_avatar(
    void *hdc,
    const char *path,
    float cx,
    float cy,
    float size
);

#ifdef __cplusplus
}
#endif

#endif
