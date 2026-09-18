#include "theme_desc.h"
#include "shared/api.h"
#include "shared/draw.h"

#include <math.h>

static float g_audio_level;

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

extern "C" void
theme_render_init(void)
{
}

extern "C" void
theme_render_shutdown(void)
{
}

extern "C" void
theme_render_reset(void)
{
}

static void
soft_put(SoftDc *dc, int x, int y, uint32_t rgb, int alpha)
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

extern "C" void
theme_render_draw(Platform *platform, const ThemeDesc *desc, float time)
{
    (void)time;
    if (!platform || !desc) {
        return;
    }
    platform->clear(desc->clear);
    SoftDc *dc = (SoftDc *)platform->hdc;
    if (!dc || !dc->pixels) {
        return;
    }
    int w = platform->width;
    int h = platform->height;
    for (int y = 0; y < h; ++y) {
        float v = (float)y / (float)(h > 1 ? h : 1);
        int a = (int)(v * v * 90.0f);
        for (int x = 0; x < w; ++x) {
            float u = (float)x / (float)(w > 1 ? w : 1);
            float d = (u - 0.5f) * (u - 0.5f) + (v - 0.45f) * (v - 0.45f);
            int glow = (int)((1.0f - d * 3.2f) * 40.0f);
            if (glow > 0) {
                soft_put(dc, x, y, desc->chrome.hover ? desc->chrome.hover : 0x66c0f4, glow);
            }
            if (a > 0) {
                soft_put(dc, x, y, 0x000000, a);
            }
        }
    }
}
