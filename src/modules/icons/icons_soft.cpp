#include "shared/icons.h"
#include "shared/draw.h"
#include "shared/os.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static char g_root[MAX_PATH];
static int g_icon_alpha = 255;

void
icon_set_root(const char *project_root)
{
    snprintf(g_root, sizeof(g_root), "%s", project_root ? project_root : ".");
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

void
icon_draw(void *hdc, IconId id, float cx, float cy, float size, uint32_t rgb, float stroke)
{
    SoftDc *dc = as_dc(hdc);
    int a = g_icon_alpha;
    if (!dc || size < 4.0f || a <= 0) {
        return;
    }
    float s = stroke > 0.1f ? stroke : size * (1.75f / 24.0f);
    if (s < 1.0f) {
        s = 1.0f;
    }
    float h = size * 0.32f;
    switch (id) {
    case ICON_X:
        stroke_line(dc, cx - h, cy - h, cx + h, cy + h, s, rgb, a);
        stroke_line(dc, cx + h, cy - h, cx - h, cy + h, s, rgb, a);
        break;
    case ICON_MINUS:
        stroke_line(dc, cx - h, cy, cx + h, cy, s, rgb, a);
        break;
    case ICON_USER:
        icon_round_rect(hdc, cx - h * 0.55f, cy - h * 1.05f, h * 1.1f, h * 1.1f, h * 0.55f, rgb, a);
        icon_round_rect(hdc, cx - h, cy + h * 0.15f, h * 2.0f, h * 1.15f, h * 0.55f, rgb, a);
        break;
    case ICON_LOG_IN:
        stroke_line(dc, cx - h, cy - h, cx + h * 0.2f, cy - h, s, rgb, a);
        stroke_line(dc, cx + h * 0.2f, cy - h, cx + h * 0.2f, cy + h, s, rgb, a);
        stroke_line(dc, cx + h * 0.2f, cy + h, cx - h, cy + h, s, rgb, a);
        stroke_line(dc, cx - h * 0.15f, cy, cx + h, cy, s, rgb, a);
        break;
    case ICON_STEAM:
        icon_round_rect(hdc, cx - size * 0.42f, cy - size * 0.42f, size * 0.84f, size * 0.84f, size * 0.42f, rgb, a);
        break;
    case ICON_DOWNLOAD:
        stroke_line(dc, cx, cy - h, cx, cy + h * 0.35f, s, rgb, a);
        stroke_line(dc, cx - h * 0.55f, cy, cx, cy + h * 0.45f, s, rgb, a);
        stroke_line(dc, cx + h * 0.55f, cy, cx, cy + h * 0.45f, s, rgb, a);
        stroke_line(dc, cx - h * 0.85f, cy + h * 0.35f, cx - h * 0.85f, cy + h, s, rgb, a);
        stroke_line(dc, cx - h * 0.85f, cy + h, cx + h * 0.85f, cy + h, s, rgb, a);
        stroke_line(dc, cx + h * 0.85f, cy + h, cx + h * 0.85f, cy + h * 0.35f, s, rgb, a);
        break;
    case ICON_SEARCH:
        icon_round_rect(hdc, cx - h * 0.45f, cy - h * 0.55f, h * 0.9f, h * 0.9f, h * 0.45f, rgb, a);
        stroke_line(dc, cx + h * 0.25f, cy + h * 0.25f, cx + h, cy + h, s, rgb, a);
        break;
    case ICON_SETTINGS:
        icon_round_rect(hdc, cx - h * 0.32f, cy - h * 0.32f, h * 0.64f, h * 0.64f, h * 0.32f, rgb, a);
        stroke_line(dc, cx, cy - h, cx, cy - h * 0.5f, s, rgb, a);
        stroke_line(dc, cx, cy + h * 0.5f, cx, cy + h, s, rgb, a);
        stroke_line(dc, cx - h, cy, cx - h * 0.5f, cy, s, rgb, a);
        stroke_line(dc, cx + h * 0.5f, cy, cx + h, cy, s, rgb, a);
        break;
    case ICON_PLAY:
        stroke_line(dc, cx - h * 0.45f, cy - h, cx + h * 0.75f, cy, s, rgb, a);
        stroke_line(dc, cx + h * 0.75f, cy, cx - h * 0.45f, cy + h, s, rgb, a);
        stroke_line(dc, cx - h * 0.45f, cy + h, cx - h * 0.45f, cy - h, s, rgb, a);
        break;
    case ICON_CHEVRON_UP:
        stroke_line(dc, cx - h * 0.9f, cy + h * 0.4f, cx, cy - h * 0.4f, s, rgb, a);
        stroke_line(dc, cx + h * 0.9f, cy + h * 0.4f, cx, cy - h * 0.4f, s, rgb, a);
        break;
    case ICON_CHEVRON_DOWN:
        stroke_line(dc, cx - h * 0.9f, cy - h * 0.4f, cx, cy + h * 0.4f, s, rgb, a);
        stroke_line(dc, cx + h * 0.9f, cy - h * 0.4f, cx, cy + h * 0.4f, s, rgb, a);
        break;
    default:
        icon_round_rect(hdc, cx - h, cy - h, h * 2.0f, h * 2.0f, h * 0.25f, rgb, a);
        break;
    }
}

static void
draw_char(SoftDc *dc, int x, int y, int scale, wchar_t ch, uint32_t rgb, int alpha)
{
    /* 5x7 subset for launcher labels */
    static const unsigned char font[96][7] = {
        {0,0,0,0,0,0,0},
    };
    (void)font;
    if (ch < 32 || ch > 126) {
        ch = '?';
    }
    /* tiny procedural glyphs */
    int col = (int)(ch - 32);
    unsigned pattern = (unsigned)(col * 17u + 13u);
    for (int row = 0; row < 7; ++row) {
        unsigned bits = (pattern + (unsigned)row * 3u) & 0x1f;
        if (ch == ' ') {
            bits = 0;
        }
        for (int c = 0; c < 5; ++c) {
            int on = 0;
            if (ch >= 'A' && ch <= 'Z') {
                on = ((row == 0 || row == 3) && c < 5) || c == 0 || (c == 4 && row < 4);
                if (ch == 'I') {
                    on = c == 2 || row == 0 || row == 6;
                }
            } else if (ch >= 'a' && ch <= 'z') {
                on = (row >= 2 && (c == 0 || c == 4 || row == 2 || row == 6));
            } else if (ch >= '0' && ch <= '9') {
                on = (row == 0 || row == 6 || c == 0 || c == 4);
            } else if (ch == '.' || ch == ':') {
                on = (row == 5 || row == 6) && c == 2;
            } else if (ch == '-') {
                on = row == 3;
            } else if (ch == '/') {
                on = (6 - row) == c;
            } else {
                on = bits & (1u << c);
            }
            if (on) {
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        put(dc, x + c * scale + sx, y + row * scale + sy, rgb, alpha);
                    }
                }
            }
        }
    }
}

float
icon_measure_label(void *hdc, const wchar_t *text, float px, int weight)
{
    (void)hdc;
    (void)weight;
    if (!text || !text[0] || px < 1.0f) {
        return 0.0f;
    }
    int scale = (int)(px / 8.0f);
    if (scale < 1) {
        scale = 1;
    }
    int n = 0;
    while (text[n]) {
        n++;
    }
    return (float)(n * 6 * scale);
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
    (void)weight;
    SoftDc *dc = as_dc(hdc);
    if (!dc || !text || w < 4.0f || h < 4.0f || alpha <= 0) {
        return;
    }
    int scale = (int)(px / 8.0f);
    if (scale < 1) {
        scale = 1;
    }
    int glyph_w = 6 * scale;
    int glyph_h = 7 * scale;
    int max_chars = (int)(w / (float)glyph_w);
    int cy = (int)(y + (h - (float)glyph_h) * 0.5f);
    int cx = (int)x;
    for (int i = 0; text[i] && i < max_chars; ++i) {
        draw_char(dc, cx + i * glyph_w, cy, scale, text[i], rgb, alpha);
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
    (void)weight;
    SoftDc *dc = as_dc(hdc);
    if (!dc || !text || w < 4.0f || h < 4.0f || alpha <= 0) {
        return;
    }
    int scale = (int)(px / 8.0f);
    if (scale < 1) {
        scale = 1;
    }
    int glyph_w = 6 * scale;
    int n = 0;
    while (text[n]) {
        n++;
    }
    float tw = (float)(n * glyph_w);
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
    (void)weight;
    SoftDc *dc = as_dc(hdc);
    if (!dc || !text || alpha <= 0) {
        return;
    }
    int scale = (int)(px / 8.0f);
    if (scale < 1) {
        scale = 1;
    }
    int n = 0;
    while (text[n]) {
        n++;
    }
    float tw = (float)(n * 6 * scale);
    icon_draw_label_alpha(hdc, x + (w - tw) * 0.5f, y, w, h, text, rgb, px, weight, alpha);
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
    int glyph_w = 6 * scale;
    int glyph_h = 7 * scale;
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
    (void)weight;
    SoftDc *dc = as_dc(hdc);
    if (!dc || !text) {
        return;
    }
    int scale = (int)(px / 8.0f);
    if (scale < 1) {
        scale = 1;
    }
    int n = 0;
    while (text[n]) {
        n++;
    }
    float tw = (float)(n * 6 * scale);
    icon_draw_label(hdc, x + (w - tw) * 0.5f, y, w, h, text, rgb, px, weight);
}

void
icon_draw_avatar(void *hdc, const char *path, float cx, float cy, float size)
{
    (void)path;
    icon_round_rect(hdc, cx - size * 0.5f, cy - size * 0.5f, size, size, size * 0.5f, 0x66c0f4, 255);
}
