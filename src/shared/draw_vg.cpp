#include "shared/draw_vg.h"
#include "shared/app_font.h"
#include "shared/os.h"

#include <math.h>
#include <string.h>
#include <wchar.h>

#include <plutovg.h>

#ifndef VG_PI
#define VG_PI 3.14159265358979323846f
#endif

static uint32_t *g_pixels;
static int g_w;
static int g_h;
static plutovg_surface_t *g_surf;
static plutovg_canvas_t *g_cv;
static plutovg_font_face_t *g_face_regular;
static plutovg_font_face_t *g_face_bold;

static void
vg_unbind(void)
{
    if (g_cv) {
        plutovg_canvas_destroy(g_cv);
        g_cv = NULL;
    }
    if (g_surf) {
        plutovg_surface_destroy(g_surf);
        g_surf = NULL;
    }
    g_pixels = NULL;
    g_w = 0;
    g_h = 0;
}

void
vg_reset(void)
{
    vg_unbind();
    if (g_face_regular) {
        plutovg_font_face_destroy(g_face_regular);
        g_face_regular = NULL;
    }
    if (g_face_bold) {
        plutovg_font_face_destroy(g_face_bold);
        g_face_bold = NULL;
    }
}

int
vg_begin(uint32_t *pixels, int width, int height)
{
    if (!pixels || width <= 0 || height <= 0) {
        return 0;
    }
    if (g_cv && g_surf && g_pixels == pixels && g_w == width && g_h == height) {
        return 1;
    }
    vg_unbind();
    g_surf = plutovg_surface_create_for_data(
        (unsigned char *)pixels,
        width,
        height,
        width * 4
    );
    if (!g_surf) {
        return 0;
    }
    g_cv = plutovg_canvas_create(g_surf);
    if (!g_cv) {
        plutovg_surface_destroy(g_surf);
        g_surf = NULL;
        return 0;
    }
    g_pixels = pixels;
    g_w = width;
    g_h = height;
    plutovg_canvas_set_line_cap(g_cv, PLUTOVG_LINE_CAP_ROUND);
    plutovg_canvas_set_line_join(g_cv, PLUTOVG_LINE_JOIN_ROUND);
    return 1;
}

void
vg_end(void)
{
    vg_unbind();
}

int
vg_ready(void)
{
    return g_cv != NULL;
}

static int
clamp_alpha(int alpha)
{
    if (alpha < 0) {
        return 0;
    }
    if (alpha > 255) {
        return 255;
    }
    return alpha;
}

static float
clamp_corner(float r, float w, float h)
{
    if (r < 0.0f) {
        r = 0.0f;
    }
    if (r * 2.0f > w) {
        r = w * 0.5f;
    }
    if (r * 2.0f > h) {
        r = h * 0.5f;
    }
    return r;
}

static void set_radial(
    float cx,
    float cy,
    float rx,
    float ry,
    const plutovg_gradient_stop_t *stops,
    int nstops
);

static void
set_rgb(uint32_t rgb, int alpha)
{
    alpha = clamp_alpha(alpha);
    plutovg_canvas_set_rgba(
        g_cv,
        (float)((rgb >> 16) & 0xff) / 255.0f,
        (float)((rgb >> 8) & 0xff) / 255.0f,
        (float)(rgb & 0xff) / 255.0f,
        (float)alpha / 255.0f
    );
}

static void
add_round_rect(float x, float y, float w, float h, float tl, float tr, float br, float bl)
{
    tl = clamp_corner(tl, w, h);
    tr = clamp_corner(tr, w, h);
    br = clamp_corner(br, w, h);
    bl = clamp_corner(bl, w, h);
    plutovg_canvas_new_path(g_cv);
    if (tl < 0.5f && tr < 0.5f && br < 0.5f && bl < 0.5f) {
        plutovg_canvas_rect(g_cv, x, y, w, h);
        return;
    }
    plutovg_canvas_move_to(g_cv, x + tl, y);
    plutovg_canvas_line_to(g_cv, x + w - tr, y);
    if (tr >= 0.5f) {
        plutovg_canvas_arc(g_cv, x + w - tr, y + tr, tr, -VG_PI * 0.5f, 0.0f, false);
    } else {
        plutovg_canvas_line_to(g_cv, x + w, y);
    }
    plutovg_canvas_line_to(g_cv, x + w, y + h - br);
    if (br >= 0.5f) {
        plutovg_canvas_arc(g_cv, x + w - br, y + h - br, br, 0.0f, VG_PI * 0.5f, false);
    } else {
        plutovg_canvas_line_to(g_cv, x + w, y + h);
    }
    plutovg_canvas_line_to(g_cv, x + bl, y + h);
    if (bl >= 0.5f) {
        plutovg_canvas_arc(g_cv, x + bl, y + h - bl, bl, VG_PI * 0.5f, VG_PI, false);
    } else {
        plutovg_canvas_line_to(g_cv, x, y + h);
    }
    plutovg_canvas_line_to(g_cv, x, y + tl);
    if (tl >= 0.5f) {
        plutovg_canvas_arc(g_cv, x + tl, y + tl, tl, VG_PI, VG_PI * 1.5f, false);
    } else {
        plutovg_canvas_line_to(g_cv, x, y);
    }
    plutovg_canvas_close_path(g_cv);
}

void
vg_fill_rect(float x, float y, float w, float h, uint32_t rgb, int alpha)
{
    if (!g_cv || w < 0.5f || h < 0.5f || alpha <= 0) {
        return;
    }
    set_rgb(rgb, alpha);
    plutovg_canvas_fill_rect(g_cv, x, y, w, h);
}

void
vg_fill_round_rect(
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
)
{
    if (!g_cv || w < 0.5f || h < 0.5f || alpha <= 0) {
        return;
    }
    set_rgb(rgb, alpha);
    add_round_rect(x, y, w, h, tl, tr, br, bl);
    plutovg_canvas_fill(g_cv);
}

void
vg_stroke_round_rect(
    float x,
    float y,
    float w,
    float h,
    float radius,
    float stroke,
    uint32_t rgb,
    int alpha
)
{
    if (!g_cv || w < 0.5f || h < 0.5f || alpha <= 0) {
        return;
    }
    if (stroke < 1.0f) {
        stroke = 1.0f;
    }
    set_rgb(rgb, alpha);
    plutovg_canvas_set_line_width(g_cv, stroke);
    add_round_rect(x, y, w, h, radius, radius, radius, radius);
    plutovg_canvas_stroke(g_cv);
}

void
vg_line(float x0, float y0, float x1, float y1, float width, uint32_t rgb, int alpha)
{
    if (!g_cv || alpha <= 0 || width <= 0.0f) {
        return;
    }
    set_rgb(rgb, alpha);
    plutovg_canvas_set_line_width(g_cv, width);
    plutovg_canvas_new_path(g_cv);
    plutovg_canvas_move_to(g_cv, x0, y0);
    plutovg_canvas_line_to(g_cv, x1, y1);
    plutovg_canvas_stroke(g_cv);
}

void
vg_circle(float cx, float cy, float r, uint32_t rgb, int alpha)
{
    if (!g_cv || r <= 0.0f || alpha <= 0) {
        return;
    }
    set_rgb(rgb, alpha);
    plutovg_canvas_new_path(g_cv);
    plutovg_canvas_circle(g_cv, cx, cy, r);
    plutovg_canvas_fill(g_cv);
}

void
vg_circle_stroke(float cx, float cy, float r, float width, uint32_t rgb, int alpha)
{
    if (!g_cv || r <= 0.0f || alpha <= 0 || width <= 0.0f) {
        return;
    }
    set_rgb(rgb, alpha);
    plutovg_canvas_set_line_width(g_cv, width);
    plutovg_canvas_new_path(g_cv);
    plutovg_canvas_circle(g_cv, cx, cy, r);
    plutovg_canvas_stroke(g_cv);
}

void
vg_ellipse_stroke(float cx, float cy, float rx, float ry, float width, uint32_t rgb, int alpha)
{
    if (!g_cv || rx <= 0.0f || ry <= 0.0f || alpha <= 0 || width <= 0.0f) {
        return;
    }
    set_rgb(rgb, alpha);
    plutovg_canvas_set_line_width(g_cv, width);
    plutovg_canvas_new_path(g_cv);
    plutovg_canvas_ellipse(g_cv, cx, cy, rx, ry);
    plutovg_canvas_stroke(g_cv);
}

void
vg_arc(
    float cx,
    float cy,
    float r,
    float a0,
    float a1,
    int ccw,
    float width,
    uint32_t rgb,
    int alpha
)
{
    if (!g_cv || r <= 0.0f || alpha <= 0 || width <= 0.0f) {
        return;
    }
    set_rgb(rgb, alpha);
    plutovg_canvas_set_line_width(g_cv, width);
    plutovg_canvas_new_path(g_cv);
    plutovg_canvas_arc(g_cv, cx, cy, r, a0, a1, ccw != 0);
    plutovg_canvas_stroke(g_cv);
}

void
vg_triangle(
    float x0,
    float y0,
    float x1,
    float y1,
    float x2,
    float y2,
    uint32_t rgb,
    int alpha
)
{
    if (!g_cv || alpha <= 0) {
        return;
    }
    set_rgb(rgb, alpha);
    plutovg_canvas_new_path(g_cv);
    plutovg_canvas_move_to(g_cv, x0, y0);
    plutovg_canvas_line_to(g_cv, x1, y1);
    plutovg_canvas_line_to(g_cv, x2, y2);
    plutovg_canvas_close_path(g_cv);
    plutovg_canvas_fill(g_cv);
}

void
vg_fill_poly(const VgPt *pts, int n, uint32_t rgb, int alpha)
{
    int i;

    if (!g_cv || !pts || n < 3 || alpha <= 0) {
        return;
    }
    set_rgb(rgb, alpha);
    plutovg_canvas_new_path(g_cv);
    plutovg_canvas_move_to(g_cv, pts[0].x, pts[0].y);
    for (i = 1; i < n; i++) {
        plutovg_canvas_line_to(g_cv, pts[i].x, pts[i].y);
    }
    plutovg_canvas_close_path(g_cv);
    plutovg_canvas_fill(g_cv);
}

void
vg_fill_poly_radial(
    const VgPt *pts,
    int n,
    float cx,
    float cy,
    float radius,
    uint32_t inner,
    uint32_t outer
)
{
    plutovg_gradient_stop_t stops[2];
    int i;

    if (!g_cv || !pts || n < 3 || radius < 1.0f) {
        return;
    }
    plutovg_color_init_rgb8(&stops[0].color, (int)((inner >> 16) & 0xff), (int)((inner >> 8) & 0xff), (int)(inner & 0xff));
    stops[0].offset = 0.0f;
    plutovg_color_init_rgb8(&stops[1].color, (int)((outer >> 16) & 0xff), (int)((outer >> 8) & 0xff), (int)(outer & 0xff));
    stops[1].offset = 1.0f;
    set_radial(cx, cy, radius, radius, stops, 2);
    plutovg_canvas_new_path(g_cv);
    plutovg_canvas_move_to(g_cv, pts[0].x, pts[0].y);
    for (i = 1; i < n; i++) {
        plutovg_canvas_line_to(g_cv, pts[i].x, pts[i].y);
    }
    plutovg_canvas_close_path(g_cv);
    plutovg_canvas_fill(g_cv);
}

void
vg_stroke_poly(const VgPt *pts, int n, float width, uint32_t rgb, int alpha)
{
    int i;

    if (!g_cv || !pts || n < 2 || alpha <= 0 || width <= 0.0f) {
        return;
    }
    set_rgb(rgb, alpha);
    plutovg_canvas_set_line_width(g_cv, width);
    plutovg_canvas_new_path(g_cv);
    plutovg_canvas_move_to(g_cv, pts[0].x, pts[0].y);
    for (i = 1; i < n; i++) {
        plutovg_canvas_line_to(g_cv, pts[i].x, pts[i].y);
    }
    plutovg_canvas_stroke(g_cv);
}

void
vg_glow_poly(
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
)
{
    if (!g_cv || !pts || n < 2) {
        return;
    }
    if (blur_outer < 1.0f) {
        blur_outer = 1.0f;
    }
    if (blur_inner < 0.6f) {
        blur_inner = 0.6f;
    }
    if (glow_alpha > 0) {
        vg_stroke_poly(pts, n, width + blur_outer * 2.2f, glow, (glow_alpha * 55) / 255);
        vg_stroke_poly(pts, n, width + blur_inner * 1.4f, glow, (glow_alpha * 80) / 255);
        vg_stroke_poly(pts, n, width, glow, glow_alpha);
    }
    if (core_alpha > 0 && core_width > 0.2f) {
        vg_stroke_poly(pts, n, core_width, core, core_alpha);
    }
}

static void
set_radial(
    float cx,
    float cy,
    float rx,
    float ry,
    const plutovg_gradient_stop_t *stops,
    int nstops
)
{
    plutovg_matrix_t matrix;

    if (rx < 1.0f) {
        rx = 1.0f;
    }
    if (ry < 1.0f) {
        ry = 1.0f;
    }
    plutovg_matrix_init_translate(&matrix, cx, cy);
    plutovg_matrix_scale(&matrix, rx, ry);
    plutovg_canvas_set_radial_gradient(
        g_cv,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        PLUTOVG_SPREAD_METHOD_PAD,
        stops,
        nstops,
        &matrix
    );
}

void
vg_radial_ellipse(
    float cx,
    float cy,
    float rx,
    float ry,
    uint32_t inner,
    uint32_t mid,
    uint32_t outer,
    float mid_stop
)
{
    plutovg_gradient_stop_t stops[3];

    if (!g_cv || rx < 1.0f || ry < 1.0f) {
        return;
    }
    if (mid_stop <= 0.05f || mid_stop >= 0.95f) {
        mid_stop = 0.42f;
    }
    plutovg_color_init_rgb8(&stops[0].color, (int)((inner >> 16) & 0xff), (int)((inner >> 8) & 0xff), (int)(inner & 0xff));
    stops[0].offset = 0.0f;
    plutovg_color_init_rgb8(&stops[1].color, (int)((mid >> 16) & 0xff), (int)((mid >> 8) & 0xff), (int)(mid & 0xff));
    stops[1].offset = mid_stop;
    plutovg_color_init_rgb8(&stops[2].color, (int)((outer >> 16) & 0xff), (int)((outer >> 8) & 0xff), (int)(outer & 0xff));
    stops[2].offset = 1.0f;
    set_radial(cx, cy, rx, ry, stops, 3);
    plutovg_canvas_new_path(g_cv);
    plutovg_canvas_ellipse(g_cv, cx, cy, rx, ry);
    plutovg_canvas_fill(g_cv);
}

void
vg_vignette(
    float cx,
    float cy,
    float rx,
    float ry,
    int width,
    int height,
    uint32_t inner,
    uint32_t outer,
    float opacity
)
{
    plutovg_gradient_stop_t stops[2];

    if (!g_cv || width <= 0 || height <= 0) {
        return;
    }
    if (opacity < 0.0f) {
        opacity = 0.0f;
    }
    if (opacity > 1.0f) {
        opacity = 1.0f;
    }
    plutovg_color_init_rgba8(
        &stops[0].color,
        (int)((inner >> 16) & 0xff),
        (int)((inner >> 8) & 0xff),
        (int)(inner & 0xff),
        0
    );
    stops[0].offset = 0.0f;
    plutovg_color_init_rgba8(
        &stops[1].color,
        (int)((outer >> 16) & 0xff),
        (int)((outer >> 8) & 0xff),
        (int)(outer & 0xff),
        (int)(opacity * 255.0f + 0.5f)
    );
    stops[1].offset = 1.0f;
    set_radial(cx, cy, rx, ry, stops, 2);
    plutovg_canvas_fill_rect(g_cv, 0.0f, 0.0f, (float)width, (float)height);
}

static int
wide_to_utf8(const wchar_t *text, char *out, int max)
{
    int n = 0;

    if (!text || !out || max < 2) {
        return 0;
    }
    while (*text && n + 5 < max) {
        unsigned int cp = (unsigned int)*text++;
        if (cp < 0x80u) {
            out[n++] = (char)cp;
        } else if (cp < 0x800u) {
            out[n++] = (char)(0xc0u | (cp >> 6));
            out[n++] = (char)(0x80u | (cp & 0x3fu));
        } else if (cp < 0x10000u) {
            out[n++] = (char)(0xe0u | (cp >> 12));
            out[n++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
            out[n++] = (char)(0x80u | (cp & 0x3fu));
        } else {
            if (cp > 0x10ffffu) {
                cp = 0xfffd;
            }
            out[n++] = (char)(0xf0u | (cp >> 18));
            out[n++] = (char)(0x80u | ((cp >> 12) & 0x3fu));
            out[n++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
            out[n++] = (char)(0x80u | (cp & 0x3fu));
        }
    }
    out[n] = '\0';
    return n;
}

static plutovg_font_face_t *
load_face(int bold)
{
    char path[MAX_PATH];

    if (bold && g_face_bold) {
        return g_face_bold;
    }
    if (!bold && g_face_regular) {
        return g_face_regular;
    }
    if (!app_font_file(path, sizeof(path), bold)) {
        return NULL;
    }
    if (bold) {
        g_face_bold = plutovg_font_face_load_from_file(path, 0);
        return g_face_bold;
    }
    g_face_regular = plutovg_font_face_load_from_file(path, 0);
    return g_face_regular;
}

static int
set_font(float px, int weight)
{
    plutovg_font_face_t *face;

    if (!g_cv || px < 1.0f) {
        return 0;
    }
    face = load_face(weight >= 600);
    if (!face) {
        face = load_face(0);
    }
    if (!face) {
        return 0;
    }
    plutovg_canvas_set_font(g_cv, face, px);
    return 1;
}

float
vg_text_width(const wchar_t *text, float px, int weight)
{
    char utf8[1024];
    plutovg_rect_t extents;
    uint32_t scratch_px = 0;
    int scratch = 0;
    int n;

    if (!text || !text[0] || px < 1.0f) {
        return 0.0f;
    }
    px = floorf(px + 0.5f);
    if (px < 1.0f) {
        px = 1.0f;
    }
    if (!g_cv) {
        if (!vg_begin(&scratch_px, 1, 1)) {
            return 0.0f;
        }
        scratch = 1;
    }
    if (!set_font(px, weight)) {
        if (scratch) {
            vg_end();
        }
        return 0.0f;
    }
    n = wide_to_utf8(text, utf8, (int)sizeof(utf8));
    if (n <= 0) {
        if (scratch) {
            vg_end();
        }
        return 0.0f;
    }
    memset(&extents, 0, sizeof(extents));
    {
        float adv = plutovg_canvas_text_extents(g_cv, utf8, n, PLUTOVG_TEXT_ENCODING_UTF8, &extents);
        float ink = extents.x + extents.w;
        if (scratch) {
            vg_end();
        }
        if (ink > adv) {
            adv = ink;
        }
        return adv > 0.0f ? adv : 0.0f;
    }
}

void
vg_text(
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
)
{
    char utf8[1024];
    plutovg_rect_t extents;
    float ascent = 0.0f;
    float descent = 0.0f;
    float gap = 0.0f;
    float tw;
    float tx;
    float baseline;
    int n;

    if (!g_cv || !text || !text[0] || alpha <= 0 || px < 1.0f) {
        return;
    }
    x = floorf(x + 0.5f);
    y = floorf(y + 0.5f);
    w = floorf(w + 0.5f);
    h = floorf(h + 0.5f);
    px = floorf(px + 0.5f);
    if (px < 1.0f) {
        px = 1.0f;
    }
    if (!set_font(px, weight)) {
        return;
    }
    n = wide_to_utf8(text, utf8, (int)sizeof(utf8));
    if (n <= 0) {
        return;
    }
    memset(&extents, 0, sizeof(extents));
    tw = plutovg_canvas_text_extents(g_cv, utf8, n, PLUTOVG_TEXT_ENCODING_UTF8, &extents);
    {
        float ink = extents.x + extents.w;
        if (ink > tw) {
            tw = ink;
        }
    }
    plutovg_canvas_font_metrics(g_cv, &ascent, &descent, &gap, NULL);
    tx = x;
    if (align == VG_ALIGN_CENTER) {
        tx = x + (w - tw) * 0.5f;
    } else if (align == VG_ALIGN_RIGHT) {
        tx = x + w - tw;
    }
    baseline = floorf(y + (h - (ascent - descent)) * 0.5f + ascent + 0.5f);
    tx = floorf(tx + 0.5f);
    plutovg_canvas_save(g_cv);
    if (w > 1.0f && h > 1.0f) {
        plutovg_canvas_clip_rect(g_cv, x, y, w, h);
    }
    set_rgb(rgb, alpha);
    plutovg_canvas_fill_text(g_cv, utf8, n, PLUTOVG_TEXT_ENCODING_UTF8, tx, baseline);
    plutovg_canvas_restore(g_cv);
}
