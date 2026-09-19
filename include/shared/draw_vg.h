#ifndef SHARED_DRAW_VG_H
#define SHARED_DRAW_VG_H

#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VG_ALIGN_LEFT 0
#define VG_ALIGN_CENTER 1
#define VG_ALIGN_RIGHT 2

typedef struct VgPt {
    float x;
    float y;
} VgPt;

int vg_begin(uint32_t *pixels, int width, int height);
void vg_end(void);
void vg_reset(void);
int vg_ready(void);

void vg_fill_rect(float x, float y, float w, float h, uint32_t rgb, int alpha);
void vg_fill_round_rect(
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
void vg_stroke_round_rect(
    float x,
    float y,
    float w,
    float h,
    float radius,
    float stroke,
    uint32_t rgb,
    int alpha
);
void vg_line(float x0, float y0, float x1, float y1, float width, uint32_t rgb, int alpha);
void vg_circle(float cx, float cy, float r, uint32_t rgb, int alpha);
void vg_circle_stroke(float cx, float cy, float r, float width, uint32_t rgb, int alpha);
void vg_ellipse_stroke(float cx, float cy, float rx, float ry, float width, uint32_t rgb, int alpha);
void vg_arc(
    float cx,
    float cy,
    float r,
    float a0,
    float a1,
    int ccw,
    float width,
    uint32_t rgb,
    int alpha
);
void vg_triangle(
    float x0,
    float y0,
    float x1,
    float y1,
    float x2,
    float y2,
    uint32_t rgb,
    int alpha
);
void vg_fill_poly(const VgPt *pts, int n, uint32_t rgb, int alpha);
void vg_fill_poly_radial(
    const VgPt *pts,
    int n,
    float cx,
    float cy,
    float radius,
    uint32_t inner,
    uint32_t outer
);
void vg_stroke_poly(const VgPt *pts, int n, float width, uint32_t rgb, int alpha);
void vg_glow_poly(
    const VgPt *pts,
    int n,
    float width,
    float blur_outer,
    float blur_inner,
    uint32_t glow,
    int glow_alpha,
    uint32_t core,
    int core_alpha,
    float core_width
);
void vg_radial_ellipse(
    float cx,
    float cy,
    float rx,
    float ry,
    uint32_t inner,
    uint32_t mid,
    uint32_t outer,
    float mid_stop
);
void vg_vignette(
    float cx,
    float cy,
    float rx,
    float ry,
    int width,
    int height,
    uint32_t inner,
    uint32_t outer,
    float opacity
);

float vg_text_width(const wchar_t *text, float px, int weight);
void vg_text(
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

#ifdef __cplusplus
}
#endif

#endif
