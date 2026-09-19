#include "shared/theme.h"
#include "shared/icons.h"
#include "theme_desc.h"

#include <math.h>
#include <string.h>

static float g_hover[THEME_MAX_UI];

static float
approach(float current, float target, float dt)
{
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    float next = current + (target - current) * (1.0f - expf(-22.0f * dt));
    if (next < 0.001f) {
        return 0.0f;
    }
    if (next > 0.999f && target >= 1.0f) {
        return 1.0f;
    }
    return next;
}

static IconId
icon_from_name(const char *name)
{
    if (!name || !name[0]) {
        return ICON_PLAY;
    }
    if (strcmp(name, "play") == 0) {
        return ICON_PLAY;
    }
    if (strcmp(name, "pause") == 0) {
        return ICON_PAUSE;
    }
    if (strcmp(name, "x") == 0) {
        return ICON_X;
    }
    if (strcmp(name, "minus") == 0) {
        return ICON_MINUS;
    }
    if (strcmp(name, "square") == 0) {
        return ICON_SQUARE;
    }
    if (strcmp(name, "globe") == 0) {
        return ICON_GLOBE;
    }
    if (strcmp(name, "search") == 0) {
        return ICON_SEARCH;
    }
    if (strcmp(name, "settings") == 0) {
        return ICON_SETTINGS;
    }
    if (strcmp(name, "chevronDown") == 0) {
        return ICON_CHEVRON_DOWN;
    }
    if (strcmp(name, "chevronUp") == 0) {
        return ICON_CHEVRON_UP;
    }
    if (strcmp(name, "menu") == 0) {
        return ICON_MENU;
    }
    if (strcmp(name, "download") == 0) {
        return ICON_DOWNLOAD;
    }
    return ICON_PLAY;
}

static int
hit_rect(int mx, int my, float x, float y, float w, float h)
{
    return (float)mx >= x && (float)my >= y && (float)mx < x + w && (float)my < y + h;
}

static int
is_play_chip(const ThemeUiWidget *widget)
{
    return widget->action == THEME_UI_ACTION_TOGGLE_PLAY ||
        widget->action == THEME_UI_ACTION_PLAY ||
        widget->action == THEME_UI_ACTION_PAUSE;
}

static void
button_box(
    Platform *platform,
    const ThemeUiWidget *widget,
    float *x,
    float *y,
    float *w,
    float *h
)
{
    float dpi = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float height = widget->size * dpi;
    if (height < 28.0f * dpi) {
        height = 28.0f * dpi;
    }
    float width = height;
    if (is_play_chip(widget) || widget->text[0]) {
        width = height * 3.15f;
        if (widget->w > 2.0f) {
            width = widget->w * dpi;
        } else if (widget->w > 0.05f) {
            width = widget->w * (float)platform->width;
        }
    }
    float inset = 16.0f * dpi;
    if (platform->corner_radius > inset) {
        inset = platform->corner_radius * 0.55f + 10.0f * dpi;
    }

    float left = widget->x * (float)platform->width - width * 0.5f;
    float top = widget->y * (float)platform->height - height * 0.5f;
    if (widget->x <= 0.12f) {
        left = inset;
    } else if (widget->x >= 0.88f) {
        left = (float)platform->width - inset - width;
    }
    if (widget->y <= 0.12f) {
        top = inset;
    } else if (widget->y >= 0.88f) {
        top = (float)platform->height - inset - height;
    }
    if (left < inset) {
        left = inset;
    }
    if (top < inset) {
        top = inset;
    }
    if (left + width > (float)platform->width - inset) {
        left = (float)platform->width - inset - width;
    }
    if (top + height > (float)platform->height - inset) {
        top = (float)platform->height - inset - height;
    }
    *x = left;
    *y = top;
    *w = width;
    *h = height;
}

static void
draw_play_chip(
    Platform *platform,
    const ThemeUiWidget *widget,
    float x,
    float y,
    float w,
    float h,
    float hover,
    int playing
)
{
    float radius = widget->radius >= 0.0f
        ? widget->radius * (platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f)
        : h * 0.5f;
    int fill_alpha = (int)((188.0f + hover * 42.0f) * widget->opacity);
    uint32_t fill = hover > 0.45f ? widget->fill_hover : widget->fill;
    icon_round_rect(platform->hdc, x, y + 2.0f, w, h, radius, 0x000000, (int)(70.0f * widget->opacity));
    icon_round_rect(platform->hdc, x, y, w, h, radius, fill, fill_alpha);
    int stroke_alpha = (int)((28.0f + hover * 50.0f + (playing ? 18.0f : 0.0f)) * widget->opacity);
    icon_round_stroke(platform->hdc, x, y, w, h, radius, widget->hover, stroke_alpha, 1.1f);

    float pad = h * 0.16f;
    float glyph = h - pad * 2.0f;
    float icon_box = glyph;
    float ix = x + pad + icon_box * 0.5f;
    float iy = y + h * 0.5f;
    uint32_t fg = hover > 0.2f ? widget->hover : widget->color;
    const char *icon_name = widget->icon;
    if (playing && widget->icon_on[0]) {
        icon_name = widget->icon_on;
    }
    IconId icon = icon_from_name(icon_name);
    icon_round_rect(
        platform->hdc,
        x + pad,
        y + pad,
        icon_box,
        icon_box,
        icon_box * 0.5f,
        fg,
        (int)((18.0f + hover * 22.0f) * widget->opacity)
    );
    float icon_size = icon_box * 0.46f;
    if (icon == ICON_PLAY) {
        ix += icon_box * 0.04f;
    }
    icon_draw(platform->hdc, icon, ix, iy, icon_size, fg, 1.4f);

    const wchar_t *label = playing ? L"PAUSE" : L"PLAY";
    float text_x = x + pad + icon_box + h * 0.12f;
    float text_w = x + w - pad - text_x;
    float px = (float)((int)(h * 0.32f + 0.5f));
    if (px < 11.0f) {
        px = 11.0f;
    }
    icon_draw_label(platform->hdc, text_x, y, text_w, h, label, fg, px, 600);
}

static void
run_action(const ThemeUiWidget *widget)
{
    ThemeUiAction action = widget->action;
    const char *url = widget->url[0] ? widget->url : NULL;
    if (action == THEME_UI_ACTION_TOGGLE_PLAY) {
        if (theme_playing()) {
            theme_set_playing(0);
        } else if (url) {
            theme_play_url(url);
        } else {
            theme_set_playing(1);
        }
    } else if (action == THEME_UI_ACTION_PLAY) {
        if (url) {
            theme_play_url(url);
        } else {
            theme_set_playing(1);
        }
    } else if (action == THEME_UI_ACTION_PAUSE) {
        theme_set_playing(0);
    }
}

static void
place_embed(Platform *platform, const ThemeUiWidget *widget)
{
    if (!platform->embed_set_view) {
        return;
    }
    float width = widget->w > 0.01f ? widget->w : 0.22f;
    float height = widget->h > 0.01f ? widget->h : 0.22f;
    int w = (int)(width * (float)platform->width + 0.5f);
    int h = (int)(height * (float)platform->height + 0.5f);
    int x = (int)(widget->x * (float)platform->width - (float)w * 0.5f + 0.5f);
    int y = (int)(widget->y * (float)platform->height - (float)h * 0.5f + 0.5f);
    if (w < 64) {
        w = 64;
    }
    if (h < 64) {
        h = 64;
    }
    platform->embed_set_view(x, y, w, h, widget->enabled);
}

void
theme_ui_reset(void)
{
    memset(g_hover, 0, sizeof(g_hover));
}

void
theme_ui_tick(Platform *platform, float dt)
{
    if (!platform) {
        return;
    }

    const ThemeDesc *desc = theme_desc_current();
    int placed_embed = 0;
    float dpi = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    int playing = theme_playing();

    if (desc) {
        for (int i = 0; i < desc->ui_count; ++i) {
            const ThemeUiWidget *widget = &desc->ui[i];
            if (!widget->enabled) {
                continue;
            }

            if (widget->kind == THEME_UI_EMBED) {
                place_embed(platform, widget);
                placed_embed = 1;
                continue;
            }

            if (widget->kind == THEME_UI_LABEL && widget->text[0] && platform->draw_label) {
                int px = (int)(widget->size * dpi + 0.5f);
                if (px < 8) {
                    px = 8;
                }
                int x = (int)(widget->x * (float)platform->width + 0.5f);
                int y = (int)(widget->y * (float)platform->height + 0.5f);
                platform->draw_label(x, y, widget->text, widget->color, px, 600, 0);
                continue;
            }

            if (widget->kind != THEME_UI_BUTTON) {
                continue;
            }

            float bx, by, bw, bh;
            button_box(platform, widget, &bx, &by, &bw, &bh);
            int over = hit_rect(platform->mouse_x, platform->mouse_y, bx, by, bw, bh);
            g_hover[i] = approach(g_hover[i], over ? 1.0f : 0.0f, dt);

            if (is_play_chip(widget)) {
                draw_play_chip(platform, widget, bx, by, bw, bh, g_hover[i], playing);
            } else {
                int fill_alpha = (int)((90.0f + g_hover[i] * 70.0f) * widget->opacity);
                uint32_t fill = g_hover[i] > 0.5f ? widget->fill_hover : widget->fill;
                float radius = widget->radius >= 0.0f ? widget->radius * dpi : bh * 0.5f;
                icon_round_rect(platform->hdc, bx, by, bw, bh, radius, fill, fill_alpha);

                const char *icon_name = widget->icon;
                if (playing && widget->icon_on[0]) {
                    icon_name = widget->icon_on;
                }
                uint32_t fg = widget->color;
                if (g_hover[i] > 0.2f) {
                    fg = widget->hover;
                }
                float icon_size = bh * 0.42f;
                float stroke = bh * 0.07f;
                if (stroke < 1.2f) {
                    stroke = 1.2f;
                }
                float ix = bx + bw * 0.5f;
                float iy = by + bh * 0.5f;
                if (icon_from_name(icon_name) == ICON_PLAY) {
                    ix += bw * 0.03f;
                }
                icon_draw(platform->hdc, icon_from_name(icon_name), ix, iy, icon_size, fg, stroke);
            }

            if (over && platform->mouse_pressed) {
                run_action(widget);
            }
        }
    }

    if (!placed_embed && platform->embed_set_view) {
        platform->embed_set_view(0, 0, 2, 2, 0);
    }
}
