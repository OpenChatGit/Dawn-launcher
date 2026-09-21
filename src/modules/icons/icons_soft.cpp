#include "shared/icons.h"
#include "shared/app_font.h"
#include "shared/draw.h"
#include "shared/draw_vg.h"
#include "shared/os.h"
#include "shared/soft_font.h"
#include "shared/svg_rast.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_FAILURE_STRINGS
#include "third_party/stb_image.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct SvgMask {
    unsigned char *rgba;
    int dim;
} SvgMask;

static char g_root[MAX_PATH];
static int g_icon_alpha = 255;
static SvgMask g_steam_src;
static SvgMask g_steam_fit;
static int g_steam_fit_dim;
static unsigned char *g_avatar_rgba;
static int g_avatar_w;
static int g_avatar_h;
static char g_avatar_loaded[MAX_PATH];

static void put(SoftDc *dc, int x, int y, uint32_t rgb, int alpha);

static int
has_inter(void)
{
    char path[MAX_PATH];
    return app_font_file(path, sizeof(path), 0) || app_font_file(path, sizeof(path), 1);
}

static void
plot_dc(void *user, int x, int y, uint32_t rgb, int alpha)
{
    SoftDc *dc = (SoftDc *)user;
    put(dc, x, y, rgb, alpha);
}

void
icon_set_root(const char *project_root)
{
    snprintf(g_root, sizeof(g_root), "%s", project_root ? project_root : ".");
    app_font_set_root(g_root);
    vg_reset();
    svg_rast_free(g_steam_src.rgba);
    svg_rast_free(g_steam_fit.rgba);
    g_steam_src.rgba = NULL;
    g_steam_src.dim = 0;
    g_steam_fit.rgba = NULL;
    g_steam_fit.dim = 0;
    g_steam_fit_dim = 0;
    if (g_avatar_rgba) {
        stbi_image_free(g_avatar_rgba);
        g_avatar_rgba = NULL;
    }
    g_avatar_w = 0;
    g_avatar_h = 0;
    g_avatar_loaded[0] = '\0';
}

void
icon_set_alpha(int alpha)
{
    if (alpha < 0) {
        alpha = 0;
    }
    if (alpha > 255) {
        alpha = 255;
    }
    g_icon_alpha = alpha;
}

static SoftDc *
as_dc(void *hdc)
{
    return (SoftDc *)hdc;
}

static int
bind_dc(SoftDc *dc)
{
    return dc && dc->pixels && vg_begin(dc->pixels, dc->width, dc->height);
}

static void
put(SoftDc *dc, int x, int y, uint32_t rgb, int alpha)
{
    if (!dc || !dc->pixels || alpha <= 0) {
        return;
    }
    if (x < 0 || y < 0 || x >= dc->width || y >= dc->height) {
        return;
    }
    uint32_t *dest = &dc->pixels[y * dc->width + x];
    if (alpha >= 255) {
        *dest = 0xff000000u | (rgb & 0x00ffffffu);
        return;
    }
    uint32_t d = *dest;
    int dr = (int)((d >> 16) & 0xff);
    int dg = (int)((d >> 8) & 0xff);
    int db = (int)(d & 0xff);
    int sr = (int)((rgb >> 16) & 0xff);
    int sg = (int)((rgb >> 8) & 0xff);
    int sb = (int)(rgb & 0xff);
    int inv = 255 - alpha;
    *dest = 0xff000000u | ((uint32_t)((sr * alpha + dr * inv) / 255) << 16) |
        ((uint32_t)((sg * alpha + dg * inv) / 255) << 8) |
        (uint32_t)((sb * alpha + db * inv) / 255);
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

static float
sd_round_rect(float px, float py, float x, float y, float w, float h, float r)
{
    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;
    float dx = fabsf(px - cx) - (w * 0.5f - r);
    float dy = fabsf(py - cy) - (h * 0.5f - r);
    float ox = dx > 0.0f ? dx : 0.0f;
    float oy = dy > 0.0f ? dy : 0.0f;
    float out = sqrtf(ox * ox + oy * oy);
    float in = (dx > dy ? dx : dy);
    if (in > 0.0f) {
        in = 0.0f;
    }
    return out + in - r;
}

void
icon_fill_rect(void *hdc, float x, float y, float w, float h, uint32_t rgb, int alpha)
{
    SoftDc *dc = as_dc(hdc);
    int x0;
    int y0;
    int x1;
    int y1;
    int px;
    int py;

    if (!dc || !dc->pixels || w < 1.0f || h < 1.0f || alpha <= 0) {
        return;
    }
    if (bind_dc(dc)) {
        vg_fill_rect(x, y, w, h, rgb, alpha);
        return;
    }
    x0 = (int)x;
    y0 = (int)y;
    x1 = (int)(x + w + 0.5f);
    y1 = (int)(y + h + 0.5f);
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > dc->width) {
        x1 = dc->width;
    }
    if (y1 > dc->height) {
        y1 = dc->height;
    }
    for (py = y0; py < y1; ++py) {
        for (px = x0; px < x1; ++px) {
            put(dc, px, py, rgb, alpha);
        }
    }
}

void
icon_round_rect(void *hdc, float x, float y, float w, float h, float radius, uint32_t rgb, int alpha)
{
    if (radius <= 0.5f) {
        icon_fill_rect(hdc, x, y, w, h, rgb, alpha);
        return;
    }
    icon_round_rect_corners(hdc, x, y, w, h, radius, radius, radius, radius, rgb, alpha);
}

void
icon_round_rect_corners(
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
)
{
    (void)tr;
    (void)br;
    (void)bl;
    SoftDc *dc = as_dc(hdc);
    if (!dc || w < 1.0f || h < 1.0f || alpha <= 0) {
        return;
    }
    if (bind_dc(dc)) {
        vg_fill_round_rect(x, y, w, h, tl, tr, br, bl, rgb, alpha);
        return;
    }
    float r = tl;
    if (r < 0.0f) {
        r = 0.0f;
    }
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1 = (int)ceilf(x + w);
    int y1 = (int)ceilf(y + h);
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            float d = sd_round_rect(px + 0.5f, py + 0.5f, x, y, w, h, r);
            float cover = clamp01(0.5f - d);
            int a = (int)(alpha * cover + 0.5f);
            put(dc, px, py, rgb, a);
        }
    }
}

void
icon_round_stroke(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    float radius,
    uint32_t rgb,
    int alpha,
    float stroke
)
{
    SoftDc *dc = as_dc(hdc);
    if (!dc || w < 1.0f || h < 1.0f || alpha <= 0) {
        return;
    }
    if (bind_dc(dc)) {
        vg_stroke_round_rect(x, y, w, h, radius, stroke, rgb, alpha);
        return;
    }
    if (stroke < 1.0f) {
        stroke = 1.0f;
    }
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1 = (int)ceilf(x + w);
    int y1 = (int)ceilf(y + h);
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            float d = fabsf(sd_round_rect(px + 0.5f, py + 0.5f, x, y, w, h, radius)) - stroke * 0.5f;
            float cover = clamp01(0.5f - d);
            put(dc, px, py, rgb, (int)(alpha * cover + 0.5f));
        }
    }
}

static void
stroke_line(SoftDc *dc, float x0, float y0, float x1, float y1, float width, uint32_t rgb, int alpha)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) {
        return;
    }
    int minx = (int)floorf((x0 < x1 ? x0 : x1) - width - 1.0f);
    int miny = (int)floorf((y0 < y1 ? y0 : y1) - width - 1.0f);
    int maxx = (int)ceilf((x0 > x1 ? x0 : x1) + width + 1.0f);
    int maxy = (int)ceilf((y0 > y1 ? y0 : y1) + width + 1.0f);
    for (int y = miny; y <= maxy; ++y) {
        for (int x = minx; x <= maxx; ++x) {
            float px = x + 0.5f - x0;
            float py = y + 0.5f - y0;
            float t = (px * dx + py * dy) / (len * len);
            if (t < 0.0f) {
                t = 0.0f;
            }
            if (t > 1.0f) {
                t = 1.0f;
            }
            float lx = x0 + dx * t;
            float ly = y0 + dy * t;
            float dist = sqrtf((x + 0.5f - lx) * (x + 0.5f - lx) + (y + 0.5f - ly) * (y + 0.5f - ly));
            float cover = clamp01(width * 0.5f + 0.5f - dist);
            put(dc, x, y, rgb, (int)(alpha * cover + 0.5f));
        }
    }
}

static int
assets_file(char *out, int max, const char *name)
{
    char assets[MAX_PATH];

    if (g_root[0] && os_join(assets, sizeof(assets), g_root, "assets") &&
        os_join(out, max, assets, name) && os_file_exists(out)) {
        return 1;
    }
    snprintf(out, max, "assets/%s", name);
    return os_file_exists(out);
}

static void
load_steam_src(void)
{
    char path[MAX_PATH];

    if (g_steam_src.rgba) {
        return;
    }
    if (!assets_file(path, (int)sizeof(path), "steam.svg")) {
        return;
    }
    svg_rast_file(path, 512, &g_steam_src.rgba, &g_steam_src.dim);
}

static int
steam_fit(int dim)
{
    int sw;
    int sh;
    int y;
    unsigned char *out;

    load_steam_src();
    if (!g_steam_src.rgba || g_steam_src.dim < 2 || dim < 8) {
        return 0;
    }
    if (g_steam_fit.rgba && g_steam_fit_dim == dim) {
        return 1;
    }
    svg_rast_free(g_steam_fit.rgba);
    g_steam_fit.rgba = NULL;
    g_steam_fit.dim = 0;
    g_steam_fit_dim = 0;
    sw = g_steam_src.dim;
    sh = g_steam_src.dim;
    out = (unsigned char *)calloc((size_t)dim * (size_t)dim * 4u, 1);
    if (!out) {
        return 0;
    }
    for (y = 0; y < dim; y++) {
        int x;
        int sy0 = y * sh / dim;
        int sy1 = (y + 1) * sh / dim;
        if (sy1 <= sy0) {
            sy1 = sy0 + 1;
        }
        if (sy1 > sh) {
            sy1 = sh;
        }
        for (x = 0; x < dim; x++) {
            int sx0 = x * sw / dim;
            int sx1 = (x + 1) * sw / dim;
            unsigned int a = 0;
            unsigned int n = 0;
            int sy;
            if (sx1 <= sx0) {
                sx1 = sx0 + 1;
            }
            if (sx1 > sw) {
                sx1 = sw;
            }
            for (sy = sy0; sy < sy1; sy++) {
                int sx;
                const unsigned char *row = g_steam_src.rgba + (size_t)sy * (size_t)sw * 4u;
                for (sx = sx0; sx < sx1; sx++) {
                    a += row[sx * 4 + 3];
                    n += 1;
                }
            }
            if (n == 0) {
                n = 1;
            }
            out[(y * dim + x) * 4 + 3] = (unsigned char)(a / n);
        }
    }
    g_steam_fit.rgba = out;
    g_steam_fit.dim = dim;
    g_steam_fit_dim = dim;
    return 1;
}

static int
draw_steam_svg(SoftDc *dc, float cx, float cy, float size, uint32_t rgb, int alpha)
{
    int dim;
    float left;
    float top;
    int x;
    int y;

    dim = (int)floorf(size + 0.5f);
    if (dim < 16) {
        dim = 16;
    }
    if (!steam_fit(dim)) {
        return 0;
    }
    left = floorf(cx - (float)dim * 0.5f + 0.5f);
    top = floorf(cy - (float)dim * 0.5f + 0.5f);
    for (y = 0; y < dim; y++) {
        for (x = 0; x < dim; x++) {
            int cover = g_steam_fit.rgba[(y * dim + x) * 4 + 3];
            int a;
            if (cover <= 0) {
                continue;
            }
            a = (cover * alpha) / 255;
            if (a > 0) {
                put(dc, (int)left + x, (int)top + y, rgb, a);
            }
        }
    }
    return 1;
}

typedef struct IconXf {
    float left;
    float top;
    float s;
} IconXf;

static IconXf
icon_xf(float cx, float cy, float size)
{
    IconXf xf;
    float dest = floorf(size + 0.5f);
    if (dest < 16.0f) {
        dest = size;
    }
    xf.s = dest / 24.0f;
    xf.left = floorf(cx - dest * 0.5f + 0.5f);
    xf.top = floorf(cy - dest * 0.5f + 0.5f);
    return xf;
}

static float
ix(const IconXf *xf, float u)
{
    return xf->left + u * xf->s;
}

static float
iy(const IconXf *xf, float v)
{
    return xf->top + v * xf->s;
}

static float
icon_stroke(float size, float stroke)
{
    float dest = floorf(size + 0.5f);
    float sw;
    if (dest < 16.0f) {
        dest = size;
    }
    sw = 1.75f * (dest / 24.0f);
    if (stroke > 0.1f && stroke < 4.0f && stroke > sw * 1.35f) {
        sw = stroke;
    }
    if (sw < 1.25f) {
        sw = 1.25f;
    }
    return sw;
}

static void
fill_circle(SoftDc *dc, float cx, float cy, float r, uint32_t rgb, int alpha)
{
    int x0 = (int)floorf(cx - r - 1.0f);
    int y0 = (int)floorf(cy - r - 1.0f);
    int x1 = (int)ceilf(cx + r + 1.0f);
    int y1 = (int)ceilf(cy + r + 1.0f);
    int y;
    for (y = y0; y <= y1; y++) {
        int x;
        for (x = x0; x <= x1; x++) {
            float d = sqrtf(((float)x + 0.5f - cx) * ((float)x + 0.5f - cx) +
                ((float)y + 0.5f - cy) * ((float)y + 0.5f - cy));
            float cover = clamp01(r + 0.5f - d);
            if (cover > 0.0f) {
                put(dc, x, y, rgb, (int)(alpha * cover + 0.5f));
            }
        }
    }
}

void
icon_draw(void *hdc, IconId id, float cx, float cy, float size, uint32_t rgb, float stroke)
{
    SoftDc *dc = as_dc(hdc);
    IconXf xf;
    int a = g_icon_alpha;
    float sw;
    if (!dc || size < 1.0f || a <= 0) {
        return;
    }
    if (id == ICON_STEAM) {
        if (!draw_steam_svg(dc, cx, cy, size, rgb, a)) {
            fill_circle(dc, cx, cy, size * 0.42f, rgb, a);
        }
        return;
    }
    xf = icon_xf(cx, cy, size);
    sw = icon_stroke(size, stroke);
    if (bind_dc(dc)) {
        switch (id) {
        case ICON_X:
            vg_line(ix(&xf, 6.0f), iy(&xf, 6.0f), ix(&xf, 18.0f), iy(&xf, 18.0f), sw, rgb, a);
            vg_line(ix(&xf, 18.0f), iy(&xf, 6.0f), ix(&xf, 6.0f), iy(&xf, 18.0f), sw, rgb, a);
            break;
        case ICON_MINUS:
            vg_line(ix(&xf, 5.0f), iy(&xf, 12.0f), ix(&xf, 19.0f), iy(&xf, 12.0f), sw, rgb, a);
            break;
        case ICON_SQUARE:
            vg_stroke_round_rect(ix(&xf, 6.0f), iy(&xf, 6.0f), 12.0f * xf.s, 12.0f * xf.s, 1.6f * xf.s, sw, rgb, a);
            break;
        case ICON_GLOBE:
            vg_circle_stroke(ix(&xf, 12.0f), iy(&xf, 12.0f), 8.0f * xf.s, sw, rgb, a);
            vg_ellipse_stroke(ix(&xf, 12.0f), iy(&xf, 12.0f), 3.0f * xf.s, 8.0f * xf.s, sw, rgb, a);
            vg_line(ix(&xf, 4.0f), iy(&xf, 12.0f), ix(&xf, 20.0f), iy(&xf, 12.0f), sw, rgb, a);
            break;
        case ICON_SEARCH:
            vg_circle_stroke(ix(&xf, 10.25f), iy(&xf, 10.25f), 5.25f * xf.s, sw, rgb, a);
            vg_line(ix(&xf, 14.8f), iy(&xf, 14.8f), ix(&xf, 19.5f), iy(&xf, 19.5f), sw, rgb, a);
            break;
        case ICON_SETTINGS: {
            VgPt gear[25];
            const float step = 1.04719755f;
            const float half = 0.32f;
            int tooth;
            for (tooth = 0; tooth < 6; tooth++) {
                float mid = (float)tooth * step - 1.57079633f;
                int i = tooth * 4;
                gear[i + 0].x = ix(&xf, 12.0f + cosf(mid - half) * 6.7f);
                gear[i + 0].y = iy(&xf, 12.0f + sinf(mid - half) * 6.7f);
                gear[i + 1].x = ix(&xf, 12.0f + cosf(mid - half) * 10.6f);
                gear[i + 1].y = iy(&xf, 12.0f + sinf(mid - half) * 10.6f);
                gear[i + 2].x = ix(&xf, 12.0f + cosf(mid + half) * 10.6f);
                gear[i + 2].y = iy(&xf, 12.0f + sinf(mid + half) * 10.6f);
                gear[i + 3].x = ix(&xf, 12.0f + cosf(mid + half) * 6.7f);
                gear[i + 3].y = iy(&xf, 12.0f + sinf(mid + half) * 6.7f);
            }
            gear[24] = gear[0];
            vg_stroke_poly(gear, 25, sw, rgb, a);
            vg_circle_stroke(ix(&xf, 12.0f), iy(&xf, 12.0f), 3.0f * xf.s, sw, rgb, a);
            break;
        }
        case ICON_CHEVRON_DOWN:
            vg_line(ix(&xf, 6.0f), iy(&xf, 9.0f), ix(&xf, 12.0f), iy(&xf, 15.0f), sw, rgb, a);
            vg_line(ix(&xf, 12.0f), iy(&xf, 15.0f), ix(&xf, 18.0f), iy(&xf, 9.0f), sw, rgb, a);
            break;
        case ICON_CHEVRON_UP:
            vg_line(ix(&xf, 6.0f), iy(&xf, 15.0f), ix(&xf, 12.0f), iy(&xf, 9.0f), sw, rgb, a);
            vg_line(ix(&xf, 12.0f), iy(&xf, 9.0f), ix(&xf, 18.0f), iy(&xf, 15.0f), sw, rgb, a);
            break;
        case ICON_MENU:
            vg_line(ix(&xf, 4.5f), iy(&xf, 8.0f), ix(&xf, 19.5f), iy(&xf, 8.0f), sw, rgb, a);
            vg_line(ix(&xf, 4.5f), iy(&xf, 12.0f), ix(&xf, 19.5f), iy(&xf, 12.0f), sw, rgb, a);
            vg_line(ix(&xf, 4.5f), iy(&xf, 16.0f), ix(&xf, 19.5f), iy(&xf, 16.0f), sw, rgb, a);
            break;
        case ICON_PLAY:
            vg_triangle(
                ix(&xf, 7.0f),
                iy(&xf, 4.5f),
                ix(&xf, 19.5f),
                iy(&xf, 12.0f),
                ix(&xf, 7.0f),
                iy(&xf, 19.5f),
                rgb,
                a
            );
            break;
        case ICON_PAUSE:
            vg_fill_round_rect(ix(&xf, 6.0f), iy(&xf, 4.5f), 4.0f * xf.s, 15.0f * xf.s, 1.1f * xf.s, 1.1f * xf.s, 1.1f * xf.s, 1.1f * xf.s, rgb, a);
            vg_fill_round_rect(ix(&xf, 14.0f), iy(&xf, 4.5f), 4.0f * xf.s, 15.0f * xf.s, 1.1f * xf.s, 1.1f * xf.s, 1.1f * xf.s, 1.1f * xf.s, rgb, a);
            break;
        case ICON_USER:
            vg_circle_stroke(ix(&xf, 12.0f), iy(&xf, 8.0f), 4.0f * xf.s, sw, rgb, a);
            vg_arc(ix(&xf, 12.0f), iy(&xf, 21.0f), 8.0f * xf.s, 0.0f, 3.14159265f, 1, sw, rgb, a);
            break;
        case ICON_LOG_IN:
            vg_line(ix(&xf, 10.0f), iy(&xf, 7.0f), ix(&xf, 15.0f), iy(&xf, 12.0f), sw, rgb, a);
            vg_line(ix(&xf, 15.0f), iy(&xf, 12.0f), ix(&xf, 10.0f), iy(&xf, 17.0f), sw, rgb, a);
            vg_line(ix(&xf, 3.0f), iy(&xf, 12.0f), ix(&xf, 15.0f), iy(&xf, 12.0f), sw, rgb, a);
            vg_line(ix(&xf, 15.0f), iy(&xf, 3.0f), ix(&xf, 19.0f), iy(&xf, 3.0f), sw, rgb, a);
            vg_arc(ix(&xf, 19.0f), iy(&xf, 5.0f), 2.0f * xf.s, -1.5707963f, 0.0f, 0, sw, rgb, a);
            vg_line(ix(&xf, 21.0f), iy(&xf, 5.0f), ix(&xf, 21.0f), iy(&xf, 19.0f), sw, rgb, a);
            vg_arc(ix(&xf, 19.0f), iy(&xf, 19.0f), 2.0f * xf.s, 0.0f, 1.5707963f, 0, sw, rgb, a);
            vg_line(ix(&xf, 19.0f), iy(&xf, 21.0f), ix(&xf, 15.0f), iy(&xf, 21.0f), sw, rgb, a);
            break;
        case ICON_DOWNLOAD:
            vg_line(ix(&xf, 12.0f), iy(&xf, 3.0f), ix(&xf, 12.0f), iy(&xf, 15.0f), sw, rgb, a);
            vg_line(ix(&xf, 7.0f), iy(&xf, 10.0f), ix(&xf, 12.0f), iy(&xf, 15.0f), sw, rgb, a);
            vg_line(ix(&xf, 12.0f), iy(&xf, 15.0f), ix(&xf, 17.0f), iy(&xf, 10.0f), sw, rgb, a);
            vg_line(ix(&xf, 4.0f), iy(&xf, 15.0f), ix(&xf, 4.0f), iy(&xf, 19.0f), sw, rgb, a);
            vg_line(ix(&xf, 4.0f), iy(&xf, 19.0f), ix(&xf, 20.0f), iy(&xf, 19.0f), sw, rgb, a);
            vg_line(ix(&xf, 20.0f), iy(&xf, 19.0f), ix(&xf, 20.0f), iy(&xf, 15.0f), sw, rgb, a);
            break;
        default:
            vg_fill_round_rect(cx - size * 0.2f, cy - size * 0.2f, size * 0.4f, size * 0.4f, size * 0.06f, size * 0.06f, size * 0.06f, size * 0.06f, rgb, a);
            break;
        }
        return;
    }
    switch (id) {
    case ICON_X:
        stroke_line(dc, ix(&xf, 6.0f), iy(&xf, 6.0f), ix(&xf, 18.0f), iy(&xf, 18.0f), sw, rgb, a);
        stroke_line(dc, ix(&xf, 18.0f), iy(&xf, 6.0f), ix(&xf, 6.0f), iy(&xf, 18.0f), sw, rgb, a);
        break;
    default:
        icon_round_rect(hdc, cx - size * 0.2f, cy - size * 0.2f, size * 0.4f, size * 0.4f, size * 0.06f, rgb, a);
        break;
    }
}

static void
draw_char(SoftDc *dc, int x, int y, int scale, wchar_t ch, uint32_t rgb, int alpha)
{
    int row;
    int col;

    if (scale < 1) {
        scale = 1;
    }
    for (row = 0; row < SOFT_FONT_ROWS; ++row) {
        for (col = 0; col < SOFT_FONT_COLS; ++col) {
            if (!soft_font_pixel((int)ch, col, row)) {
                continue;
            }
            {
                int sy;
                int sx;
                for (sy = 0; sy < scale; ++sy) {
                    for (sx = 0; sx < scale; ++sx) {
                        put(dc, x + col * scale + sx, y + row * scale + sy, rgb, alpha);
                    }
                }
            }
        }
    }
}

float
icon_measure_label(void *hdc, const wchar_t *text, float px, int weight)
{
    SoftDc *dc = as_dc(hdc);
    float tw;

    if (!text || !text[0] || px < 1.0f) {
        return 0.0f;
    }
    if (dc) {
        bind_dc(dc);
    }
    tw = vg_text_width(text, px, weight);
    if (tw > 0.0f) {
        return tw;
    }
    if (has_inter()) {
        tw = app_font_measure_wide(text, px, weight);
        if (tw > 0.0f) {
            return tw;
        }
    }
    {
        int scale = (int)(px / 8.0f);
        int n = 0;
        if (scale < 1) {
            scale = 1;
        }
        while (text[n]) {
            n++;
        }
        return (float)(n * SOFT_FONT_ADVANCE * scale);
    }
}

void
icon_draw_label_alpha(
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
)
{
    SoftDc *dc = as_dc(hdc);
    if (!dc || !text || w < 4.0f || h < 4.0f || alpha <= 0) {
        return;
    }
    if (bind_dc(dc) && vg_text_width(text, px, weight) > 0.0f) {
        vg_text(x, y, w, h, text, px, weight, rgb, alpha, VG_ALIGN_LEFT);
        return;
    }
    if (has_inter()) {
        app_font_draw_wide(
            plot_dc,
            dc,
            x,
            y,
            w,
            h,
            text,
            px,
            weight,
            rgb,
            alpha,
            APP_FONT_ALIGN_LEFT
        );
        return;
    }
    {
        int scale = (int)(px / 8.0f);
        int glyph_w;
        int glyph_h;
        int max_chars;
        int cy;
        int cx;
        int i;
        if (scale < 1) {
            scale = 1;
        }
        glyph_w = SOFT_FONT_ADVANCE * scale;
        glyph_h = SOFT_FONT_ROWS * scale;
        max_chars = (int)(w / (float)glyph_w);
        cy = (int)(y + (h - (float)glyph_h) * 0.5f);
        cx = (int)x;
        for (i = 0; text[i] && i < max_chars; ++i) {
            draw_char(dc, cx + i * glyph_w, cy, scale, text[i], rgb, alpha);
        }
    }
}

void
icon_draw_label_end_alpha(
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
)
{
    SoftDc *dc = as_dc(hdc);
    float tw;
    if (!dc || !text || w < 4.0f || h < 4.0f || alpha <= 0) {
        return;
    }
    if (bind_dc(dc) && vg_text_width(text, px, weight) > 0.0f) {
        vg_text(x, y, w, h, text, px, weight, rgb, alpha, VG_ALIGN_RIGHT);
        return;
    }
    if (has_inter()) {
        app_font_draw_wide(
            plot_dc,
            dc,
            x,
            y,
            w,
            h,
            text,
            px,
            weight,
            rgb,
            alpha,
            APP_FONT_ALIGN_RIGHT
        );
        return;
    }
    tw = icon_measure_label(hdc, text, px, weight);
    icon_draw_label_alpha(hdc, x + w - tw, y, tw > w ? w : tw, h, text, rgb, px, weight, alpha);
}

void
icon_draw_label(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    float px,
    int weight
)
{
    icon_draw_label_alpha(hdc, x, y, w, h, text, rgb, px, weight, 255);
}

void
icon_draw_label_full(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    float px,
    int weight
)
{
    if (bind_dc(as_dc(hdc)) && vg_text_width(text, px, weight) > 0.0f) {
        vg_text(x, y, w > 4.0f ? w : 4096.0f, h, text, px, weight, rgb, 255, VG_ALIGN_LEFT);
        return;
    }
    if (has_inter()) {
        app_font_draw_wide(
            plot_dc,
            as_dc(hdc),
            x,
            y,
            w > 4.0f ? w : 4096.0f,
            h,
            text,
            px,
            weight,
            rgb,
            255,
            APP_FONT_ALIGN_LEFT
        );
        return;
    }
    {
        SoftDc *dc = as_dc(hdc);
        int scale = (int)(px / 8.0f);
        int glyph_w;
        int glyph_h;
        int cy;
        int cx;
        int i;
        if (!dc || !text || h < 4.0f) {
            return;
        }
        if (scale < 1) {
            scale = 1;
        }
        glyph_w = SOFT_FONT_ADVANCE * scale;
        glyph_h = SOFT_FONT_ROWS * scale;
        cy = (int)(y + (h - (float)glyph_h) * 0.5f);
        cx = (int)x;
        (void)w;
        for (i = 0; text[i]; ++i) {
            draw_char(dc, cx + i * glyph_w, cy, scale, text[i], rgb, 255);
        }
    }
}

void
icon_draw_label_center_alpha(
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
)
{
    SoftDc *dc = as_dc(hdc);
    if (!dc || !text || alpha <= 0) {
        return;
    }
    if (bind_dc(dc) && vg_text_width(text, px, weight) > 0.0f) {
        vg_text(x, y, w, h, text, px, weight, rgb, alpha, VG_ALIGN_CENTER);
        return;
    }
    if (has_inter()) {
        app_font_draw_wide(
            plot_dc,
            dc,
            x,
            y,
            w,
            h,
            text,
            px,
            weight,
            rgb,
            alpha,
            APP_FONT_ALIGN_CENTER
        );
        return;
    }
    {
        int scale = (int)(px / 8.0f);
        int n = 0;
        float tw;
        if (scale < 1) {
            scale = 1;
        }
        while (text[n]) {
            n++;
        }
        tw = (float)(n * SOFT_FONT_ADVANCE * scale);
        icon_draw_label_alpha(hdc, x + (w - tw) * 0.5f, y, w, h, text, rgb, px, weight, alpha);
    }
}

void
icon_draw_label_shimmer(
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
)
{
    SoftDc *dc = as_dc(hdc);
    if (!dc || !text || w < 4.0f || h < 4.0f || alpha <= 0) {
        return;
    }
    if (bind_dc(dc) && vg_text_width(text, px, weight) > 0.0f) {
        (void)shine;
        (void)phase;
        vg_text(x, y, w, h, text, px, weight, rgb, alpha, VG_ALIGN_RIGHT);
        return;
    }
    if (has_inter()) {
        (void)shine;
        (void)phase;
        app_font_draw_wide(
            plot_dc,
            dc,
            x,
            y,
            w,
            h,
            text,
            px,
            weight,
            rgb,
            alpha,
            APP_FONT_ALIGN_RIGHT
        );
        return;
    }
    if (phase < 0.0f) {
        phase = 0.0f;
    }
    if (phase > 1.0f) {
        phase -= (float)((int)phase);
    }
    int scale = (int)(px / 8.0f);
    if (scale < 1) {
        scale = 1;
    }
    int glyph_w = SOFT_FONT_ADVANCE * scale;
    int glyph_h = SOFT_FONT_ROWS * scale;
    int n = 0;
    while (text[n]) {
        n++;
    }
    float tw = (float)(n * glyph_w);
    int cy = (int)(y + (h - (float)glyph_h) * 0.5f);
    int cx = (int)(x + w - tw);
    for (int i = 0; i < n; ++i) {
        float t = n > 1 ? (float)i / (float)(n - 1) : 0.0f;
        float d = t - phase;
        if (d < 0.0f) {
            d = -d;
        }
        if (d > 0.5f) {
            d = 1.0f - d;
        }
        int use = alpha;
        uint32_t color = rgb;
        if (d < 0.22f) {
            int mix = (int)((1.0f - d / 0.22f) * 255.0f);
            color = shine;
            use = alpha;
            (void)mix;
        }
        draw_char(dc, cx + i * glyph_w, cy, scale, text[i], color, use);
    }
    (void)weight;
}

void
icon_draw_label_center(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    uint32_t rgb,
    float px,
    int weight
)
{
    icon_draw_label_center_alpha(hdc, x, y, w, h, text, rgb, px, weight, 255);
}

static int
avatar_load(const char *path)
{
    unsigned char *rgba;
    int w;
    int h;
    int n;

    if (!path || !path[0]) {
        return 0;
    }
    if (g_avatar_rgba && strcmp(g_avatar_loaded, path) == 0) {
        return 1;
    }
    if (g_avatar_rgba) {
        stbi_image_free(g_avatar_rgba);
        g_avatar_rgba = NULL;
    }
    g_avatar_w = 0;
    g_avatar_h = 0;
    g_avatar_loaded[0] = '\0';
    rgba = stbi_load(path, &w, &h, &n, 4);
    if (!rgba || w < 8 || h < 8) {
        if (rgba) {
            stbi_image_free(rgba);
        }
        return 0;
    }
    g_avatar_rgba = rgba;
    g_avatar_w = w;
    g_avatar_h = h;
    snprintf(g_avatar_loaded, sizeof(g_avatar_loaded), "%s", path);
    return 1;
}

void
icon_draw_avatar(void *hdc, const char *path, float cx, float cy, float size)
{
    SoftDc *dc = as_dc(hdc);
    int dim;
    int x;
    int y;
    float x0;
    float y0;
    float mid;
    float radius;

    if (!dc || !dc->pixels || size < 4.0f) {
        return;
    }
    if (!avatar_load(path)) {
        icon_round_rect(hdc, cx - size * 0.5f, cy - size * 0.5f, size, size, size * 0.5f, 0x66c0f4, 255);
        return;
    }
    dim = (int)(size + 0.5f);
    if (dim < 8) {
        dim = 8;
    }
    x0 = cx - (float)dim * 0.5f;
    y0 = cy - (float)dim * 0.5f;
    mid = (float)dim * 0.5f;
    radius = mid - 0.35f;
    for (y = 0; y < dim; y++) {
        int sy0 = y * g_avatar_h / dim;
        int sy1 = (y + 1) * g_avatar_h / dim;
        if (sy1 <= sy0) {
            sy1 = sy0 + 1;
        }
        if (sy1 > g_avatar_h) {
            sy1 = g_avatar_h;
        }
        for (x = 0; x < dim; x++) {
            float dx = ((float)x + 0.5f) - mid;
            float dy = ((float)y + 0.5f) - mid;
            float cover = radius - sqrtf(dx * dx + dy * dy);
            int sx0;
            int sx1;
            int sy;
            unsigned r = 0;
            unsigned g = 0;
            unsigned b = 0;
            unsigned a = 0;
            unsigned n = 0;
            int alpha;
            uint32_t rgb;

            if (cover <= 0.0f) {
                continue;
            }
            if (cover > 1.0f) {
                cover = 1.0f;
            }
            sx0 = x * g_avatar_w / dim;
            sx1 = (x + 1) * g_avatar_w / dim;
            if (sx1 <= sx0) {
                sx1 = sx0 + 1;
            }
            if (sx1 > g_avatar_w) {
                sx1 = g_avatar_w;
            }
            for (sy = sy0; sy < sy1; sy++) {
                const unsigned char *row = g_avatar_rgba + (size_t)sy * (size_t)g_avatar_w * 4u;
                int sx;
                for (sx = sx0; sx < sx1; sx++) {
                    const unsigned char *p = row + (size_t)sx * 4u;
                    r += p[0];
                    g += p[1];
                    b += p[2];
                    a += p[3];
                    n += 1;
                }
            }
            if (n == 0) {
                n = 1;
            }
            alpha = (int)(((float)(a / n) * cover) + 0.5f);
            if (alpha <= 0) {
                continue;
            }
            rgb = ((r / n) << 16) | ((g / n) << 8) | (b / n);
            put(dc, (int)(x0 + (float)x + 0.5f), (int)(y0 + (float)y + 0.5f), rgb, alpha);
        }
    }
}
