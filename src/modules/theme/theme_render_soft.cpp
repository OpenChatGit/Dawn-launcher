#include "theme_desc.h"
#include "shared/api.h"
#include "shared/draw.h"
#include "shared/draw_vg.h"
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
    vg_reset();
    free_base();
    free_emblem();
}

extern "C" void
theme_render_reset(void)
{
    vg_reset();
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

static int
bind_pix(Pix *dst)
{
    return dst && dst->px && vg_begin(dst->px, dst->w, dst->h);
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
    if (!bind_pix(dst) || rx < 1.0f || ry < 1.0f) {
        return;
    }
    vg_radial_ellipse(cx, cy, rx, ry, inner, mid, outer, mid_stop);
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

    if (!bind_pix(dst) || rx < 1.0f || ry < 1.0f) {
        return;
    }
    vg_vignette(cx, cy, rx, ry, width, height, layer->inner, layer->outer, layer->opacity);
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
    int glow_a;
    int core_a;
    int i;

    if (!bind_pix(dst)) {
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
        vg_fill_poly_radial(
            (const VgPt *)cap,
            seg + 2,
            cx,
            cy,
            radius * 1.15f,
            layer->cap_inner,
            layer->cap_outer
        );
    }
    stroke_w = layer->stroke_width * px;
    core_w = layer->core_width * px;
    glow_a = (int)(layer->stroke_alpha * layer->opacity * 255.0f + 0.5f);
    core_a = (int)(layer->opacity * 255.0f + 0.5f);
    vg_glow_poly(
        (const VgPt *)pts,
        seg + 1,
        stroke_w,
        layer->blur_outer * px,
        layer->blur_inner * px,
        layer->glow,
        glow_a,
        layer->core,
        core_a,
        core_w
    );
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

static float
wrap01(float t)
{
    t -= floorf(t);
    if (t < 0.0f) {
        t += 1.0f;
    }
    return t;
}

static void
blit_emblem(Pix *dst, const ThemeLayer *layer, float left, float top, float size, int opacity)
{
    int x0;
    int y0;
    int x1;
    int y1;
    int y;

    if (!dst || size < 4.0f || opacity <= 0 || !g_emblem.rgba) {
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
draw_layer_drift(Pix *dst, const ThemeLayer *layer, int width, int height, float dpi, float time)
{
    int count;
    float speed;
    float design = 620.0f;
    float base;
    int i;

    ensure_emblem(layer);
    if (!g_emblem.rgba || g_emblem.dim <= 0) {
        return;
    }
    count = layer->count > 0 ? layer->count : 12;
    if (count > 24) {
        count = 24;
    }
    speed = layer->flow_speed > 0.01f ? layer->flow_speed : 0.07f;
    base = layer->size * ((float)height / design);
    if (dpi > 1.0f && base < layer->size * dpi * 0.35f) {
        base = layer->size * dpi * 0.55f;
    }
    if (base < 18.0f) {
        base = 18.0f;
    }
    for (i = 0; i < count; i++) {
        float seed = (float)i * 0.618033988f + layer->phase;
        float t = wrap01(time * speed + seed);
        float lane = wrap01(seed * 3.17f) * 2.0f - 1.0f;
        float nx = -0.2f + t * 1.4f + lane * 0.4f;
        float ny = -0.2f + t * 1.4f - lane * 0.22f;
        float sz = base;
        int opacity;
        if (layer->animate) {
            nx += 0.03f * sinf(time * 0.45f + seed * 6.2f);
            ny += 0.025f * cosf(time * 0.38f + seed * 5.1f);
        }
        opacity = (int)(layer->opacity * 255.0f + 0.5f);
        blit_emblem(dst, layer, nx * (float)width - sz * 0.5f, ny * (float)height - sz * 0.5f, sz, opacity);
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
    uint32_t glow;
    uint32_t core;
    int glow_a;
    int core_a;

    if (!bind_pix(dst)) {
        return;
    }
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
    glow = layer->glow ? layer->glow : layer->color;
    core = layer->core ? layer->core : layer->color;
    glow_a = (int)(layer->stroke_alpha * alpha * 255.0f + 0.5f);
    core_a = (int)(alpha * 255.0f + 0.5f);
    if (glow_a > 0 && glow_w > 0.2f) {
        vg_ellipse_stroke(cx, cy, rx, ry, glow_w + layer->blur_outer * px, glow, (glow_a * 55) / 255);
        vg_ellipse_stroke(cx, cy, rx, ry, glow_w, glow, glow_a);
    }
    if (core_a > 0 && stroke > 0.2f) {
        vg_ellipse_stroke(cx, cy, rx, ry, stroke, core, core_a);
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
    case THEME_LAYER_DRIFT:
        draw_layer_drift(dst, layer, width, height, dpi, time);
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
