#include "settings_modal.h"
#include "login_modal.h"
#include "modal_skin.h"
#include "shared/icons.h"
#include "shared/os.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef struct SettingsLayout {
    float overlay_w;
    float overlay_h;
    float x;
    float y;
    float w;
    float h;
    float close_x;
    float close_y;
    float close_s;
    float path_x;
    float path_y;
    float path_w;
    float path_h;
    float browse_x;
    float browse_y;
    float browse_w;
    float browse_h;
} SettingsLayout;

static int g_open;
static int g_block_mouse;
static float g_anim;
static float g_hover_close;
static float g_hover_browse;
static wchar_t g_status_w[160];

static void
layout_modal(Platform *platform, SettingsLayout *out)
{
    float s = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float inset = (float)modal_px(MODAL_INSET, s);
    out->overlay_w = (float)platform->width;
    out->overlay_h = (float)platform->height;
    out->w = (float)modal_px(360, s);
    out->h = (float)modal_px(196, s);
    if (out->w > (float)platform->width - 32.0f) {
        out->w = (float)platform->width - 32.0f;
    }
    out->x = ((float)platform->width - out->w) * 0.5f;
    out->y = ((float)platform->height - out->h) * 0.5f;
    modal_place_close(out->x, out->y, out->w, s, &out->close_x, &out->close_y, &out->close_s);
    out->browse_w = (float)modal_px(112, s);
    out->browse_h = (float)modal_px(MODAL_BTN_H, s);
    out->path_h = (float)modal_px(MODAL_FIELD_H, s);
    out->path_x = out->x + inset;
    out->path_y = out->y + (float)modal_px(100, s);
    out->browse_y = out->path_y;
    out->browse_x = out->x + out->w - inset - out->browse_w;
    out->path_w = out->browse_x - out->path_x - (float)modal_px(MODAL_GAP, s);
    if (out->path_w < (float)modal_px(120, s)) {
        out->path_w = out->w - inset * 2.0f;
        out->browse_x = out->path_x;
        out->browse_y = out->path_y + out->path_h + (float)modal_px(MODAL_GAP, s);
        out->browse_w = out->path_w;
        out->h = (float)modal_px(244, s);
        out->y = ((float)platform->height - out->h) * 0.5f;
        modal_place_close(out->x, out->y, out->w, s, &out->close_x, &out->close_y, &out->close_s);
    }
}

static const char *
current_dir(Platform *platform)
{
    const char *dir = platform->install_dir ? platform->install_dir() : "";
    return dir ? dir : "";
}

static void
path_label(const char *utf8, wchar_t *out, int max)
{
    wchar_t wide[512];
    int n;
    int keep;

    if (!out || max < 5) {
        return;
    }
    out[0] = 0;
    if (!utf8 || !utf8[0]) {
        wcscpy(out, L"Not set");
        return;
    }
    os_utf8_to_wide(utf8, wide, 512);
    n = (int)wcslen(wide);
    if (n < max - 1) {
        wcsncpy(out, wide, (size_t)max);
        out[max - 1] = 0;
        return;
    }
    keep = max - 5;
    if (keep < 8) {
        keep = 8;
    }
    out[0] = L'.';
    out[1] = L'.';
    out[2] = L'.';
    wcsncpy(out + 3, wide + (n - keep), (size_t)keep);
    out[3 + keep] = 0;
}

static int
is_busy(Platform *platform)
{
    return platform->install_busy && platform->install_busy();
}

static int
is_ready(Platform *platform)
{
    return platform->install_ready && platform->install_ready();
}

static void
choose_folder(Platform *platform)
{
    char picked[MAX_PATH];

    if (!platform->pick_folder || is_busy(platform)) {
        return;
    }
    picked[0] = '\0';
    if (!platform->pick_folder(picked, (int)sizeof(picked)) || picked[0] == '\0') {
        return;
    }
    if (platform->install_set_dir) {
        platform->install_set_dir(picked);
    }
}

void
settings_modal_open(void)
{
    login_modal_hide();
    g_open = 1;
    g_block_mouse = 1;
}

void
settings_modal_close(void)
{
    g_open = 0;
    g_block_mouse = 0;
}

void
settings_modal_hide(void)
{
    g_open = 0;
    g_anim = 0.0f;
    g_block_mouse = 0;
    g_hover_close = 0.0f;
    g_hover_browse = 0.0f;
}

int
settings_modal_visible(void)
{
    return g_open || g_anim > 0.0f;
}

int
settings_modal_wants_mouse(Platform *platform)
{
    (void)platform;
    return g_open;
}

void
settings_modal_tick(Platform *platform, float dt)
{
    SettingsLayout L;
    float s;
    float x;
    float y;
    float inset;
    int mx;
    int my;
    int over_close;
    int over_browse;
    int over_card;
    int busy;
    wchar_t path_w[96];

    if (!platform) {
        return;
    }
    g_anim = modal_approach(g_anim, g_open ? 1.0f : 0.0f, dt);
    if (g_anim <= 0.0f) {
        return;
    }

    layout_modal(platform, &L);
    s = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    mx = platform->mouse_x;
    my = platform->mouse_y;
    busy = is_busy(platform);
    x = L.x;
    y = L.y;
    over_close = modal_hit(mx, my, L.close_x, L.close_y, L.close_s, L.close_s);
    over_browse = !busy && modal_hit(mx, my, L.browse_x, L.browse_y, L.browse_w, L.browse_h);
    over_card = modal_hit(mx, my, x, y, L.w, L.h);
    g_hover_close = modal_approach(g_hover_close, (g_open && over_close) ? 1.0f : 0.0f, dt);
    g_hover_browse = modal_approach(g_hover_browse, (g_open && over_browse) ? 1.0f : 0.0f, dt);

    modal_draw_overlay(platform->hdc, L.overlay_w, L.overlay_h, g_anim);
    inset = (float)modal_px(MODAL_INSET, s);
    modal_draw_card(platform->hdc, x, y, L.w, L.h, g_anim, s);
    modal_draw_title(
        platform->hdc,
        x + inset,
        y + (float)modal_px(14, s),
        L.w - inset * 2.0f - L.close_s,
        (float)modal_px(20, s),
        L"Settings",
        s,
        g_anim
    );
    modal_draw_subtitle(
        platform->hdc,
        x + inset,
        y + (float)modal_px(36, s),
        L.w - inset * 2.0f,
        (float)modal_px(18, s),
        L"Dawn folder",
        s,
        g_anim
    );
    modal_draw_subtitle(
        platform->hdc,
        x + inset,
        y + (float)modal_px(54, s),
        L.w - inset * 2.0f,
        (float)modal_px(36, s),
        L"Existing copy or new download location.",
        s,
        g_anim
    );
    modal_draw_close(platform->hdc, L.close_x, L.close_y, L.close_s, g_hover_close, g_anim);

    path_label(current_dir(platform), path_w, 96);
    modal_draw_field(
        platform->hdc,
        L.path_x,
        L.path_y,
        L.path_w,
        L.path_h,
        path_w,
        g_anim,
        s
    );
    modal_draw_button(
        platform->hdc,
        L.browse_x,
        L.browse_y,
        L.browse_w,
        L.browse_h,
        busy ? L"Locked" : L"Browse",
        ICON_SEARCH,
        g_hover_browse,
        g_anim,
        busy,
        s
    );

    if (g_open) {
        if (busy) {
            wcscpy(g_status_w, L"Folder is locked while downloading.");
        } else if (is_ready(platform)) {
            wcscpy(g_status_w, L"Dawn is installed in this folder.");
        } else {
            wcscpy(g_status_w, L"New downloads will go here.");
        }
    }
    modal_draw_subtitle(
        platform->hdc,
        x + inset,
        y + L.h - (float)modal_px(28, s),
        L.w - inset * 2.0f,
        (float)modal_px(16, s),
        g_status_w[0] ? g_status_w : L"New downloads will go here.",
        s,
        g_anim
    );

    if (!g_open) {
        return;
    }
    if (platform->key == PLATFORM_KEY_ESCAPE) {
        settings_modal_close();
    }
    if (g_block_mouse) {
        if (!platform->mouse_down && !platform->mouse_pressed) {
            g_block_mouse = 0;
        }
        platform->mouse_pressed = 0;
        return;
    }
    if (platform->mouse_pressed) {
        if (over_close || !over_card) {
            settings_modal_close();
        } else if (over_browse) {
            choose_folder(platform);
        }
        platform->mouse_pressed = 0;
    }
}
