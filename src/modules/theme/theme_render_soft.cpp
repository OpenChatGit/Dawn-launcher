#include "theme_desc.h"
#include "shared/api.h"
#include "shared/draw.h"
#include "shared/svg_rast.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846f
#define DEG (PI / 180.0f)
#define MAX_ARC 65

typedef struct Pt {
    float x;
    float y;
} Pt;

typedef struct Pix {
    uint32_t *px;
    int w;
    int h;
} Pix;

typedef struct EmblemSoft {
    unsigned char *rgba;
    int dim;
    uint32_t color;
    char path[THEME_PATH_MAX];
} EmblemSoft;

static float g_audio_level;
static uint32_t *g_base;
static int g_base_w;
static int g_base_h;
static const ThemeDesc *g_base_desc;
static EmblemSoft g_emblem;

extern "C" void
theme_render_set_audio(float level)
{
    g_audio_level = level;
}

extern "C" float
theme_render_audio(void)
{
    return g_audio_level;
}

static void
free_emblem(void)
{
    free(g_emblem.rgba);
    memset(&g_emblem, 0, sizeof(g_emblem));
}

static void
free_base(void)
{
    free(g_base);
    g_base = NULL;
    g_base_w = 0;
    g_base_h = 0;
    g_base_desc = NULL;
}

extern "C" void
theme_render_init(void)
{
}

extern "C" void
theme_render_shutdown(void)
{
    free_base();
    free_emblem();
}

extern "C" void
theme_render_reset(void)
{
    free_base();
    free_emblem();
}

static void
pix_put(Pix *dst, int x, int y, uint32_t rgb, int alpha)
{
    uint32_t *dest;
    uint32_t d;
    int dr;
    int dg;
    int db;
    int sr;
    int sg;
    int sb;
    int inv;

    if (!dst || !dst->px || alpha <= 0) {
        return;
    }
    if (x < 0 || y < 0 || x >= dst->w || y >= dst->h) {
        return;
    }
    dest = &dst->px[y * dst->w + x];
    if (alpha >= 255) {
        *dest = 0xff000000u | (rgb & 0x00ffffffu);
        return;
    }
    d = *dest;
    dr = (int)((d >> 16) & 0xff);
    dg = (int)((d >> 8) & 0xff);
    db = (int)(d & 0xff);
    sr = (int)((rgb >> 16) & 0xff);
    sg = (int)((rgb >> 8) & 0xff);
    sb = (int)(rgb & 0xff);
    inv = 255 - alpha;
    *dest = 0xff000000u | ((uint32_t)((sr * alpha + dr * inv) / 255) << 16) |
        ((uint32_t)((sg * alpha + dg * inv) / 255) << 8) |
        (uint32_t)((sb * alpha + db * inv) / 255);
}

static void
pix_clear(Pix *dst, uint32_t rgb)
{
    int n;
    int i;
    uint32_t packed;

    if (!dst || !dst->px) {
        return;
    }
    n = dst->w * dst->h;
    packed = 0xff000000u | (rgb & 0x00ffffffu);
    for (i = 0; i < n; i++) {
        dst->px[i] = packed;
    }
}

static float
clamp01(float v)
{
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

static uint32_t
lerp_rgb(uint32_t a, uint32_t b, float t)
{
    int ar;
    int ag;
    int ab;
    int br;
    int bg;
    int bb;

    t = clamp01(t);
    ar = (int)((a >> 16) & 0xff);
    ag = (int)((a >> 8) & 0xff);
    ab = (int)(a & 0xff);
    br = (int)((b >> 16) & 0xff);
    bg = (int)((b >> 8) & 0xff);
    bb = (int)(b & 0xff);
    return ((uint32_t)(ar + (int)((float)(br - ar) * t)) << 16) |
        ((uint32_t)(ag + (int)((float)(bg - ag) * t)) << 8) |
        (uint32_t)(ab + (int)((float)(bb - ab) * t));
}

static float
design_px(const ThemeLayer *layer, int height, float dpi)
{
    float design = 620.0f;
    float size = layer->size * ((float)height / design);
    if (dpi > 1.0f && size < layer->size * dpi * 0.85f) {
        size = layer->size * dpi;
    }
    return size;
}

static float
layer_audio_drive(const ThemeLayer *layer, float time)
{
    float drive = 0.0f;
    if (layer->audio) {
        drive = g_audio_level;
        if (drive < 0.0f) {
            drive = 0.0f;
        }
        if (drive > 1.0f) {
            drive = 1.0f;
        }
    }
    if (layer->animate) {
        float speed = layer->flow_speed > 0.01f ? layer->flow_speed : 0.4f;
        float idle = 0.5f + 0.5f * sinf(time * speed * 6.2831853f + layer->phase);
        if (drive < idle * 0.22f) {
            drive = idle * 0.22f;
        }
    }
    return drive;
}

static void
fill_radial_ellipse(
    Pix *dst,
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
    int x0;
    int y0;
    int x1;
    int y1;
    int y;
    float mid_t;

    if (!dst || rx < 1.0f || ry < 1.0f) {
        return;
    }
    mid_t = mid_stop > 0.05f && mid_stop < 0.95f ? mid_stop : 0.42f;
    x0 = (int)floorf(cx - rx);
    y0 = (int)floorf(cy - ry);
    x1 = (int)ceilf(cx + rx);
    y1 = (int)ceilf(cy + ry);
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > dst->w) {
        x1 = dst->w;
    }
    if (y1 > dst->h) {
        y1 = dst->h;
    }
    for (y = y0; y < y1; y++) {
        int x;
        float v = ((float)y + 0.5f - cy) / ry;
        for (x = x0; x < x1; x++) {
            float u = ((float)x + 0.5f - cx) / rx;
            float t = sqrtf(u * u + v * v);
            uint32_t rgb;
            if (t > 1.0f) {
                continue;
            }
            if (t <= mid_t) {
                rgb = lerp_rgb(inner, mid, t / mid_t);
            } else {
                rgb = lerp_rgb(mid, outer, (t - mid_t) / (1.0f - mid_t));
            }
            pix_put(dst, x, y, rgb, 255);
        }
    }
}

static void
draw_layer_radial(Pix *dst, const ThemeLayer *layer, int width, int height)
{
    fill_radial_ellipse(
        dst,
        layer->x * (float)width,
        layer->y * (float)height,
        (float)width * layer->w * 0.5f,
        (float)height * layer->h * 0.5f,
        layer->inner,
        layer->mid,
        layer->outer,
        layer->mid_stop > 0.0f ? layer->mid_stop : 0.42f
    );
}

static void
draw_layer_vignette(Pix *dst, const ThemeLayer *layer, int width, int height)
{
    float cx = layer->x * (float)width;
    float cy = layer->y * (float)height;
    float rx = (float)width * (layer->w > 0.1f ? layer->w : 1.2f) * 0.5f;
    float ry = (float)height * (layer->h > 0.1f ? layer->h : 1.2f) * 0.5f;
    int y;
    int amax;

    if (!dst || rx < 1.0f || ry < 1.0f) {
        return;
    }
    amax = (int)(layer->opacity * 255.0f + 0.5f);
    if (amax <= 0) {
        return;
    }
    for (y = 0; y < height; y++) {
        int x;
        float v = ((float)y + 0.5f - cy) / ry;
        for (x = 0; x < width; x++) {
            float u = ((float)x + 0.5f - cx) / rx;
            float t = sqrtf(u * u + v * v);
            int a;
            if (t < 1.0f) {
                continue;
            }
            a = (int)((clamp01(t - 1.0f) + 0.15f) * (float)amax);
            if (a > amax) {
                a = amax;
            }
            if (a > 0) {
                pix_put(dst, x, y, layer->outer, a);
            }
        }
    }
}

static void
make_arc_points(const ThemeLayer *layer, float cx, float cy, float radius, float time, Pt *pts, int seg)
{
    float start_a = layer->start_deg * DEG;
    float end_a = layer->end_deg * DEG;
    float phase = layer->phase;
    int i;

    for (i = 0; i <= seg; i++) {
        float t = (float)i / (float)seg;
        float a = start_a + (end_a - start_a) * t;
        float envelope = 0.4f + 0.6f * sinf(t * PI);
        float w = 0.0f;
        if (layer->animate) {
            w = (sinf(time * 1.3f + t * 4.2f + phase) * layer->flow_amp +
                cosf(time * 0.9f - t * 2.8f + phase * 1.5f) * (layer->flow_amp * 0.45f)) *
                envelope;
        }
        pts[i].x = cx + cosf(a) * (radius + w);
        pts[i].y = cy + sinf(a) * (radius + w);
    }
}

static float
dist_seg(float px, float py, float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    float t = 0.0f;
    float lx;
    float ly;

    if (len2 > 1e-6f) {
        t = ((px - x0) * dx + (py - y0) * dy) / len2;
        if (t < 0.0f) {
            t = 0.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }
    }
    lx = x0 + dx * t;
    ly = y0 + dy * t;
    return sqrtf((px - lx) * (px - lx) + (py - ly) * (py - ly));
}

static float
dist_poly(float px, float py, const Pt *pts, int n)
{
    float best = 1.0e9f;
    int i;

    for (i = 0; i < n - 1; i++) {
        float d = dist_seg(px, py, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y);
        if (d < best) {
            best = d;
        }
    }
    return best;
}

static int
point_in_poly(float x, float y, const Pt *pts, int n)
{
    int c = 0;
    int i;
    int j = n - 1;

    for (i = 0; i < n; j = i++) {
        float yi = pts[i].y;
        float yj = pts[j].y;
        if ((yi > y) != (yj > y)) {
            float xi = pts[i].x;
            float xj = pts[j].x;
            if (x < (xj - xi) * (y - yi) / (yj - yi + 1e-8f) + xi) {
                c = !c;
            }
        }
    }
    return c;
}

static void
draw_layer_arc(Pix *dst, const ThemeLayer *layer, int width, int height, float time, float dpi)
{
    float cx = layer->x * (float)width;
    float cy = layer->y * (float)height;
    float scale = width < 500.0f * dpi ? layer->narrow_scale : layer->scale;
    float min_side = (float)(width < height ? width : height);
    float radius = min_side * layer->radius_ratio * scale;
    float px = dpi < 0.75f ? 1.0f : dpi;
    float anim = layer->animate ? time * layer->flow_speed : 0.0f;
    int seg = layer->segments < 8 ? 8 : layer->segments;
    Pt pts[MAX_ARC];
    Pt cap[MAX_ARC + 1];
    float stroke_w;
    float core_w;
    float sigma_o;
    float sigma_i;
    float reach;
    float x_min;
    float y_min;
    float x_max;
    float y_max;
    int i;
    int x0;
    int y0;
    int x1;
    int y1;
    int y;

    if (!dst) {
        return;
    }
    if (seg > 64) {
        seg = 64;
    }
    make_arc_points(layer, cx, cy, radius, anim, pts, seg);

    if (layer->cap) {
        cap[0].x = cx;
        cap[0].y = cy;
        for (i = 0; i <= seg; i++) {
            cap[i + 1] = pts[i];
        }
        x_min = cx;
        y_min = cy;
        x_max = cx;
        y_max = cy;
        for (i = 0; i <= seg; i++) {
            if (pts[i].x < x_min) {
                x_min = pts[i].x;
            }
            if (pts[i].y < y_min) {
                y_min = pts[i].y;
            }
            if (pts[i].x > x_max) {
                x_max = pts[i].x;
            }
            if (pts[i].y > y_max) {
                y_max = pts[i].y;
            }
        }
        x0 = (int)floorf(x_min) - 1;
        y0 = (int)floorf(y_min) - 1;
        x1 = (int)ceilf(x_max) + 1;
        y1 = (int)ceilf(y_max) + 1;
        if (x0 < 0) {
            x0 = 0;
        }
        if (y0 < 0) {
            y0 = 0;
        }
        if (x1 > dst->w) {
            x1 = dst->w;
        }
        if (y1 > dst->h) {
            y1 = dst->h;
        }
        for (y = y0; y < y1; y++) {
            int x;
            for (x = x0; x < x1; x++) {
                float pxp = (float)x + 0.5f;
                float pyp = (float)y + 0.5f;
                float t;
                if (!point_in_poly(pxp, pyp, cap, seg + 2)) {
                    continue;
                }
                t = sqrtf((pxp - cx) * (pxp - cx) + (pyp - cy) * (pyp - cy)) / (radius * 1.15f);
                pix_put(dst, x, y, lerp_rgb(layer->cap_inner, layer->cap_outer, t), 255);
            }
        }
    }

    stroke_w = layer->stroke_width * px;
    core_w = layer->core_width * px;
    sigma_o = layer->blur_outer * px * 0.85f;
    sigma_i = layer->blur_inner * px * 0.70f;
    if (sigma_o < 1.0f) {
        sigma_o = 1.0f;
    }
    if (sigma_i < 0.6f) {
        sigma_i = 0.6f;
    }
    reach = sigma_o * 3.2f + stroke_w;
    x_min = pts[0].x;
    y_min = pts[0].y;
    x_max = pts[0].x;
    y_max = pts[0].y;
    for (i = 1; i <= seg; i++) {
        if (pts[i].x < x_min) {
            x_min = pts[i].x;
        }
        if (pts[i].y < y_min) {
            y_min = pts[i].y;
        }
        if (pts[i].x > x_max) {
            x_max = pts[i].x;
        }
        if (pts[i].y > y_max) {
            y_max = pts[i].y;
        }
    }
    x0 = (int)floorf(x_min - reach);
    y0 = (int)floorf(y_min - reach);
    x1 = (int)ceilf(x_max + reach);
    y1 = (int)ceilf(y_max + reach);
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > dst->w) {
        x1 = dst->w;
    }
    if (y1 > dst->h) {
        y1 = dst->h;
    }

    for (y = y0; y < y1; y++) {
        int x;
        for (x = x0; x < x1; x++) {
            float pxp = (float)x + 0.5f;
            float pyp = (float)y + 0.5f;
            float d = dist_poly(pxp, pyp, pts, seg + 1);
            float ao;
            float ai;
            float as;
            float ac;
            if (d > reach) {
                continue;
            }
            ao = expf(-(d * d) / (2.0f * sigma_o * sigma_o)) * layer->stroke_alpha * layer->opacity * 0.55f;
            ai = expf(-(d * d) / (2.0f * sigma_i * sigma_i)) * layer->stroke_alpha * layer->opacity * 0.80f;
            as = clamp01(stroke_w * 0.5f + 0.55f - d) * layer->stroke_alpha * layer->opacity;
            ac = clamp01(core_w * 0.5f + 0.55f - d) * layer->opacity;
            if (ao > 0.002f) {
                pix_put(dst, x, y, layer->glow, (int)(ao * 255.0f + 0.5f));
            }
            if (ai > 0.002f) {
                pix_put(dst, x, y, layer->glow, (int)(ai * 255.0f + 0.5f));
            }
            if (as > 0.002f) {
                pix_put(dst, x, y, layer->glow, (int)(as * 255.0f + 0.5f));
            }
            if (ac > 0.002f && layer->core) {
                pix_put(dst, x, y, layer->core, (int)(ac * 255.0f + 0.5f));
            }
        }
    }
}

static void
ensure_emblem(const ThemeLayer *layer)
{
    int cache;
    int i;
    int n;

    if (!layer->path[0]) {
        return;
    }
    if (g_emblem.rgba &&
        strcmp(g_emblem.path, layer->path) == 0 &&
        g_emblem.color == layer->color) {
        return;
    }
    free_emblem();
    cache = layer->cache > 0 ? layer->cache : 512;
    if (cache > 512) {
        cache = 512;
    }
    if (!svg_rast_file(layer->path, cache, &g_emblem.rgba, &g_emblem.dim) || !g_emblem.rgba) {
        return;
    }
    n = g_emblem.dim * g_emblem.dim;
    for (i = 0; i < n; i++) {
        int a = g_emblem.rgba[i * 4 + 3];
        g_emblem.rgba[i * 4 + 0] = (unsigned char)((layer->color >> 16) & 0xff);
        g_emblem.rgba[i * 4 + 1] = (unsigned char)((layer->color >> 8) & 0xff);
        g_emblem.rgba[i * 4 + 2] = (unsigned char)(layer->color & 0xff);
        g_emblem.rgba[i * 4 + 3] = (unsigned char)a;
    }
    g_emblem.color = layer->color;
    snprintf(g_emblem.path, sizeof(g_emblem.path), "%s", layer->path);
}

static int
sample_emblem(float u, float v)
{
    int dim = g_emblem.dim;
    float x = u * (float)(dim - 1);
    float y = v * (float)(dim - 1);
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1;
    int y1;
    float fx;
    float fy;
    float a00;
    float a10;
    float a01;
    float a11;

    if (!g_emblem.rgba || dim <= 1) {
        return 0;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    x1 = x0 + 1;
    y1 = y0 + 1;
    if (x1 >= dim) {
        x1 = dim - 1;
    }
    if (y1 >= dim) {
        y1 = dim - 1;
    }
    fx = x - (float)x0;
    fy = y - (float)y0;
    a00 = (float)g_emblem.rgba[(y0 * dim + x0) * 4 + 3];
    a10 = (float)g_emblem.rgba[(y0 * dim + x1) * 4 + 3];
    a01 = (float)g_emblem.rgba[(y1 * dim + x0) * 4 + 3];
    a11 = (float)g_emblem.rgba[(y1 * dim + x1) * 4 + 3];
    return (int)((a00 * (1.0f - fx) + a10 * fx) * (1.0f - fy) + (a01 * (1.0f - fx) + a11 * fx) * fy + 0.5f);
}

static void
draw_layer_emblem(Pix *dst, const ThemeLayer *layer, int width, int height, float dpi)
{
    float size;
    float left;
    float top;
    int x0;
    int y0;
    int x1;
    int y1;
    int y;
    int opacity;

    ensure_emblem(layer);
    if (!g_emblem.rgba || g_emblem.dim <= 0) {
        return;
    }
    size = design_px(layer, height, dpi);
    if (size < 4.0f) {
        return;
    }
    left = layer->x * (float)width - size * 0.5f;
    top = layer->y * (float)height - size * 0.5f;
    opacity = (int)(layer->opacity * 255.0f + 0.5f);
    if (opacity <= 0) {
        return;
    }
    x0 = (int)floorf(left);
    y0 = (int)floorf(top);
    x1 = (int)ceilf(left + size);
    y1 = (int)ceilf(top + size);
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > dst->w) {
        x1 = dst->w;
    }
    if (y1 > dst->h) {
        y1 = dst->h;
    }
    for (y = y0; y < y1; y++) {
        int x;
        float v = ((float)y + 0.5f - top) / size;
        if (v < 0.0f || v > 1.0f) {
            continue;
        }
        for (x = x0; x < x1; x++) {
            float u = ((float)x + 0.5f - left) / size;
            int cover;
            int a;
            if (u < 0.0f || u > 1.0f) {
                continue;
            }
            cover = sample_emblem(u, v);
            if (cover <= 0) {
                continue;
            }
            a = (cover * opacity) / 255;
            if (a > 0) {
                pix_put(dst, x, y, layer->color, a);
            }
        }
    }
}

static void
draw_layer_ring(Pix *dst, const ThemeLayer *layer, int width, int height, float dpi, float time)
{
    float cx = layer->x * (float)width;
    float cy = layer->y * (float)height;
    float px = dpi < 0.75f ? 1.0f : dpi;
    float drive = layer_audio_drive(layer, time);
    float amp = layer->scale;
    float pulse;
    float rx;
    float ry;
    float stroke;
    float glow_w;
    float alpha;
    int x0;
    int y0;
    int x1;
    int y1;
    int y;

    if (amp > 0.6f) {
        amp = 0.16f;
    }
    pulse = 1.0f + amp * drive;
    if (layer->size > 1.0f) {
        float radius = design_px(layer, height, dpi) * 0.5f * pulse;
        rx = radius;
        ry = radius;
    } else {
        rx = (float)width * layer->w * 0.5f * pulse;
        ry = (float)height * layer->h * 0.5f * pulse;
    }
    stroke = layer->core_width * px * (1.0f + drive * 0.45f);
    glow_w = layer->stroke_width * px * (1.0f + drive * 0.35f);
    alpha = layer->opacity * (0.72f + 0.28f * drive);
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    x0 = (int)floorf(cx - rx - glow_w * 3.0f);
    y0 = (int)floorf(cy - ry - glow_w * 3.0f);
    x1 = (int)ceilf(cx + rx + glow_w * 3.0f);
    y1 = (int)ceilf(cy + ry + glow_w * 3.0f);
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > dst->w) {
        x1 = dst->w;
    }
    if (y1 > dst->h) {
        y1 = dst->h;
    }
    for (y = y0; y < y1; y++) {
        int x;
        for (x = x0; x < x1; x++) {
            float u = ((float)x + 0.5f - cx) / rx;
            float v = ((float)y + 0.5f - cy) / ry;
            float d = (sqrtf(u * u + v * v) - 1.0f) * ((rx + ry) * 0.5f);
            float ad = fabsf(d);
            float ag = clamp01(glow_w * 0.5f + 1.2f - ad) * layer->stroke_alpha * alpha * 0.55f;
            float ac = clamp01(stroke * 0.5f + 0.55f - ad) * alpha;
            if (ag > 0.002f) {
                pix_put(dst, x, y, layer->glow ? layer->glow : layer->color, (int)(ag * 255.0f + 0.5f));
            }
            if (ac > 0.002f) {
                pix_put(dst, x, y, layer->core ? layer->core : layer->color, (int)(ac * 255.0f + 0.5f));
            }
        }
    }
}

static void
draw_static_layer(Pix *dst, const ThemeLayer *layer, int width, int height, float dpi)
{
    switch (layer->kind) {
    case THEME_LAYER_RADIAL:
        draw_layer_radial(dst, layer, width, height);
        break;
    case THEME_LAYER_VIGNETTE:
        draw_layer_vignette(dst, layer, width, height);
        break;
    case THEME_LAYER_EMBLEM:
        draw_layer_emblem(dst, layer, width, height, dpi);
        break;
    default:
        break;
    }
}

static void
draw_live_layer(Pix *dst, const ThemeLayer *layer, int width, int height, float time, float dpi)
{
    switch (layer->kind) {
    case THEME_LAYER_ARC:
        draw_layer_arc(dst, layer, width, height, time, dpi);
        break;
    case THEME_LAYER_RING:
        draw_layer_ring(dst, layer, width, height, dpi, time);
        break;
    default:
        break;
    }
}

static int
rebuild_base(const ThemeDesc *desc, int width, int height, float dpi)
{
    Pix dst;
    int i;

    free_base();
    g_base = (uint32_t *)malloc((size_t)width * (size_t)height * sizeof(uint32_t));
    if (!g_base) {
        return 0;
    }
    g_base_w = width;
    g_base_h = height;
    g_base_desc = desc;
    dst.px = g_base;
    dst.w = width;
    dst.h = height;
    pix_clear(&dst, desc->clear);
    for (i = 0; i < desc->layer_count; i++) {
        const ThemeLayer *layer = &desc->layers[i];
        if (!layer->enabled) {
            continue;
        }
        draw_static_layer(&dst, layer, width, height, dpi);
    }
    return 1;
}

extern "C" void
theme_render_draw(Platform *platform, const ThemeDesc *desc, float time)
{
    SoftDc *dc;
    Pix dst;
    float dpi;
    int width;
    int height;
    int i;

    if (!platform || !desc) {
        return;
    }
    dc = (SoftDc *)platform->hdc;
    width = platform->width;
    height = platform->height;
    if (!dc || !dc->pixels || width <= 0 || height <= 0) {
        if (platform->clear) {
            platform->clear(desc->clear);
        }
        return;
    }
    dpi = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    if (!g_base || g_base_w != width || g_base_h != height || g_base_desc != desc) {
        if (!rebuild_base(desc, width, height, dpi)) {
            platform->clear(desc->clear);
            return;
        }
    }
    memcpy(dc->pixels, g_base, (size_t)width * (size_t)height * sizeof(uint32_t));
    dst.px = dc->pixels;
    dst.w = width;
    dst.h = height;
    for (i = 0; i < desc->layer_count; i++) {
        const ThemeLayer *layer = &desc->layers[i];
        if (!layer->enabled) {
            continue;
        }
        draw_live_layer(&dst, layer, width, height, time, dpi);
    }
}
