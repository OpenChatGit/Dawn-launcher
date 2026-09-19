#include "shared/app_font.h"
#include "shared/os.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#ifdef _WIN32
#ifndef FR_PRIVATE
#define FR_PRIVATE 0x10
#endif
#endif

#define FONT_CACHE 512

typedef struct Face {
    unsigned char *data;
    size_t len;
    stbtt_fontinfo info;
    int ok;
} Face;

typedef struct Glyph {
    int used;
    int cp;
    int pxi;
    int bold;
    int sub;
    int w;
    int h;
    float xoff;
    float yoff;
    float adv;
    unsigned char *cover;
} Glyph;

#define FONT_SUB 4

static char g_root[MAX_PATH];
static Face g_regular;
static Face g_bold;
static Glyph g_cache[FONT_CACHE];
static int g_ready;

static void
trim_parent(char *path)
{
    size_t n;

    if (!path) {
        return;
    }
    n = strlen(path);
    while (n > 1 && (path[n - 1] == '/' || path[n - 1] == '\\')) {
        path[--n] = '\0';
    }
    while (n > 0 && path[n - 1] != '/' && path[n - 1] != '\\') {
        path[--n] = '\0';
    }
    if (n > 1 && (path[n - 1] == '/' || path[n - 1] == '\\')) {
        path[n - 1] = '\0';
    }
}

static int
try_font(char *out, size_t max, const char *dir, const char *name)
{
    char fonts[MAX_PATH];

    if (!dir || !dir[0] || !os_join(fonts, sizeof(fonts), dir, "assets")) {
        return 0;
    }
    if (!os_join(fonts, sizeof(fonts), fonts, "fonts")) {
        return 0;
    }
    if (!os_join(out, max, fonts, name)) {
        return 0;
    }
    return os_file_exists(out);
}

int
app_font_file(char *out, size_t max, int bold)
{
    const char *name = bold ? "Inter-SemiBold.ttf" : "Inter-Regular.ttf";
    char exe[MAX_PATH];
    char parent[MAX_PATH];
    char app[MAX_PATH];

    if (!out || max < 8) {
        return 0;
    }
    if (try_font(out, max, g_root, name)) {
        return 1;
    }
    if (os_exe_dir(exe, sizeof(exe)) && try_font(out, max, exe, name)) {
        return 1;
    }
    if (os_app_root(app, sizeof(app), NULL) && try_font(out, max, app, name)) {
        return 1;
    }
    if (exe[0]) {
        snprintf(parent, sizeof(parent), "%s", exe);
        trim_parent(parent);
        if (try_font(out, max, parent, name)) {
            return 1;
        }
    }
#ifndef _WIN32
    {
        const char *sys[] = {
            "/usr/share/fonts/truetype/inter",
            "/usr/share/fonts/opentype/inter",
            "/usr/local/share/fonts",
            NULL
        };
        int i;
        for (i = 0; sys[i]; i++) {
            snprintf(out, max, "%s/%s", sys[i], name);
            if (os_file_exists(out)) {
                return 1;
            }
        }
    }
#endif
    return 0;
}

static void
free_face(Face *face)
{
    if (!face) {
        return;
    }
    free(face->data);
    memset(face, 0, sizeof(*face));
}

static void
clear_cache(void)
{
    int i;

    for (i = 0; i < FONT_CACHE; i++) {
        free(g_cache[i].cover);
        memset(&g_cache[i], 0, sizeof(g_cache[i]));
    }
}

static int
load_face(Face *face, int bold)
{
    char path[MAX_PATH];
    FILE *file;
    size_t n;

    if (face->ok) {
        return 1;
    }
    if (!app_font_file(path, sizeof(path), bold)) {
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    fseek(file, 0, SEEK_END);
    {
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        if (size <= 0 || size > 8 * 1024 * 1024) {
            fclose(file);
            return 0;
        }
        face->data = (unsigned char *)malloc((size_t)size);
        if (!face->data) {
            fclose(file);
            return 0;
        }
        n = fread(face->data, 1, (size_t)size, file);
        fclose(file);
        if (n != (size_t)size || !stbtt_InitFont(&face->info, face->data, 0)) {
            free(face->data);
            memset(face, 0, sizeof(*face));
            return 0;
        }
        face->len = n;
        face->ok = 1;
        return 1;
    }
}

static Face *
face_for(int weight)
{
    if (weight >= 600 && load_face(&g_bold, 1)) {
        return &g_bold;
    }
    if (load_face(&g_regular, 0)) {
        return &g_regular;
    }
    if (load_face(&g_bold, 1)) {
        return &g_bold;
    }
    return NULL;
}

void
app_font_set_root(const char *root)
{
    snprintf(g_root, sizeof(g_root), "%s", root ? root : ".");
    free_face(&g_regular);
    free_face(&g_bold);
    clear_cache();
    g_ready = 0;
}

int
app_font_register(void)
{
#ifdef _WIN32
    char path[MAX_PATH];
    wchar_t wide[MAX_PATH];
    int ok = 0;

    if (app_font_file(path, sizeof(path), 0) && os_utf8_to_wide(path, wide, MAX_PATH) > 0) {
        if (AddFontResourceExW(wide, FR_PRIVATE, 0) > 0) {
            ok = 1;
        }
    }
    if (app_font_file(path, sizeof(path), 1) && os_utf8_to_wide(path, wide, MAX_PATH) > 0) {
        AddFontResourceExW(wide, FR_PRIVATE, 0);
        ok = 1;
    }
    return ok;
#else
    return face_for(400) != NULL;
#endif
}

static Glyph *
glyph_get(Face *face, int cp, int pxi, int bold, int sub)
{
    Glyph *slot;
    unsigned char *bmp;
    int w = 0;
    int h = 0;
    int xoff = 0;
    int yoff = 0;
    int adv = 0;
    int lsb = 0;
    float scale;
    float shift;
    int i;

    if (pxi < 8) {
        pxi = 8;
    }
    if (sub < 0) {
        sub = 0;
    }
    if (sub >= FONT_SUB) {
        sub = FONT_SUB - 1;
    }
    for (i = 0; i < FONT_CACHE; i++) {
        if (g_cache[i].used && g_cache[i].cp == cp && g_cache[i].pxi == pxi &&
            g_cache[i].bold == bold && g_cache[i].sub == sub) {
            return &g_cache[i];
        }
    }
    scale = stbtt_ScaleForPixelHeight(&face->info, (float)pxi);
    shift = (float)sub / (float)FONT_SUB;
    stbtt_GetCodepointHMetrics(&face->info, cp, &adv, &lsb);
    bmp = stbtt_GetCodepointBitmapSubpixel(&face->info, scale, scale, shift, 0.0f, cp, &w, &h, &xoff, &yoff);
    slot = &g_cache[0];
    for (i = 0; i < FONT_CACHE; i++) {
        if (!g_cache[i].used) {
            slot = &g_cache[i];
            break;
        }
    }
    if (slot->used) {
        free(slot->cover);
        memset(slot, 0, sizeof(*slot));
    }
    slot->used = 1;
    slot->cp = cp;
    slot->pxi = pxi;
    slot->bold = bold;
    slot->sub = sub;
    slot->w = w;
    slot->h = h;
    slot->xoff = (float)xoff;
    slot->yoff = (float)yoff;
    slot->adv = (float)adv * scale;
    slot->cover = bmp;
    return slot;
}

static float
measure_cps(Face *face, const int *cps, int n, float px)
{
    float scale;
    float w = 0.0f;
    int i;
    int adv;
    int lsb;

    if (!face || n <= 0) {
        return 0.0f;
    }
    scale = stbtt_ScaleForPixelHeight(&face->info, px);
    for (i = 0; i < n; i++) {
        stbtt_GetCodepointHMetrics(&face->info, cps[i], &adv, &lsb);
        w += (float)adv * scale;
        if (i + 1 < n) {
            w += (float)stbtt_GetCodepointKernAdvance(&face->info, cps[i], cps[i + 1]) * scale;
        }
    }
    return w;
}

static int
wide_to_cps(const wchar_t *text, int *cps, int max)
{
    int n = 0;

    if (!text) {
        return 0;
    }
    while (text[n] && n < max) {
        cps[n] = (int)(unsigned)text[n];
        n++;
    }
    return n;
}

static int
utf8_to_cps(const char *text, int *cps, int max)
{
    const unsigned char *p = (const unsigned char *)text;
    int n = 0;

    if (!text) {
        return 0;
    }
    while (*p && n < max) {
        unsigned int cp = 0;
        if (*p < 0x80) {
            cp = *p++;
        } else if ((*p & 0xe0) == 0xc0 && p[1]) {
            cp = ((unsigned int)(p[0] & 0x1f) << 6) | (p[1] & 0x3f);
            p += 2;
        } else if ((*p & 0xf0) == 0xe0 && p[1] && p[2]) {
            cp = ((unsigned int)(p[0] & 0x0f) << 12) | ((unsigned int)(p[1] & 0x3f) << 6) | (p[2] & 0x3f);
            p += 3;
        } else {
            p++;
            continue;
        }
        cps[n++] = (int)cp;
    }
    return n;
}

float
app_font_measure_wide(const wchar_t *text, float px, int weight)
{
    int cps[256];
    Face *face = face_for(weight);
    if (!face || px < 1.0f) {
        return 0.0f;
    }
    return measure_cps(face, cps, wide_to_cps(text, cps, 256), px);
}

float
app_font_measure_utf8(const char *text, float px, int weight)
{
    int cps[256];
    Face *face = face_for(weight);
    if (!face || px < 1.0f) {
        return 0.0f;
    }
    return measure_cps(face, cps, utf8_to_cps(text, cps, 256), px);
}

static void
draw_cps(
    app_font_plot plot,
    void *user,
    float x,
    float baseline,
    float clip_r,
    const int *cps,
    int n,
    float px,
    int weight,
    uint32_t rgb,
    int alpha,
    float tracking
)
{
    Face *face = face_for(weight);
    float scale;
    float pen;
    int pxi;
    int bold;
    int i;

    if (!plot || !face || n <= 0 || alpha <= 0) {
        return;
    }
    scale = stbtt_ScaleForPixelHeight(&face->info, px);
    pxi = (int)(px + 0.5f);
    bold = weight >= 600;
    pen = x;
    for (i = 0; i < n; i++) {
        float frac = pen - floorf(pen);
        int sub = (int)(frac * (float)FONT_SUB);
        Glyph *g;
        int gx;
        int gy;
        int row;
        int col;
        if (sub >= FONT_SUB) {
            sub = FONT_SUB - 1;
        }
        g = glyph_get(face, cps[i], pxi, bold, sub);
        if (g && g->cover && g->w > 0 && g->h > 0) {
            gx = (int)floorf(pen + g->xoff);
            gy = (int)floorf(baseline + g->yoff);
            for (row = 0; row < g->h; row++) {
                for (col = 0; col < g->w; col++) {
                    int px0 = gx + col;
                    int cover;
                    int a;
                    if (clip_r > 0.0f && (float)px0 >= clip_r) {
                        continue;
                    }
                    cover = g->cover[row * g->w + col];
                    if (cover <= 0) {
                        continue;
                    }
                    a = (cover * alpha) / 255;
                    if (a > 0) {
                        plot(user, px0, gy + row, rgb, a);
                    }
                }
            }
        }
        if (g) {
            pen += g->adv;
        }
        if (i + 1 < n) {
            pen += (float)stbtt_GetCodepointKernAdvance(&face->info, cps[i], cps[i + 1]) * scale;
            if (tracking > 0.0f) {
                pen += tracking;
            }
        }
    }
}

void
app_font_draw_wide(
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
)
{
    Face *face = face_for(weight);
    int cps[256];
    int n;
    int ascent = 0;
    int descent = 0;
    int gap = 0;
    float scale;
    float tw;
    float pen_x;
    float baseline;

    if (!face || !text || w < 1.0f || h < 1.0f || px < 1.0f) {
        return;
    }
    n = wide_to_cps(text, cps, 256);
    tw = measure_cps(face, cps, n, px);
    stbtt_GetFontVMetrics(&face->info, &ascent, &descent, &gap);
    scale = stbtt_ScaleForPixelHeight(&face->info, px);
    baseline = y + (h - (float)(ascent - descent) * scale) * 0.5f + (float)ascent * scale;
    pen_x = x;
    if (align == APP_FONT_ALIGN_CENTER) {
        pen_x = x + (w - tw) * 0.5f;
    } else if (align == APP_FONT_ALIGN_RIGHT) {
        pen_x = x + w - tw;
    }
    draw_cps(plot, user, pen_x, baseline, x + w, cps, n, px, weight, rgb, alpha, 0.0f);
}

void
app_font_draw_utf8(
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
)
{
    Face *face = face_for(weight);
    int cps[256];
    int n;
    int ascent = 0;
    int descent = 0;
    int gap = 0;
    float scale;
    float baseline;

    if (!face || !text || px < 1.0f) {
        return;
    }
    n = utf8_to_cps(text, cps, 256);
    stbtt_GetFontVMetrics(&face->info, &ascent, &descent, &gap);
    scale = stbtt_ScaleForPixelHeight(&face->info, px);
    baseline = y + (float)ascent * scale;
    draw_cps(plot, user, x, baseline, 0.0f, cps, n, px, weight, rgb, alpha, (float)tracking);
}
