#include "install_button.h"
#include "login_modal.h"
#include "settings_modal.h"
#include "modal_skin.h"
#include "shared/icons.h"
#include "shared/os.h"
#include "shared/theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define BTN_FILL 0xffffff
#define BTN_FILL_HOVER 0xf3f5f8
#define BTN_PROGRESS 0xe4e9f0
#define BTN_TEXT 0x111827
#define MENU_W 236.0f
#define MENU_INSET 10.0f
#define MENU_ITEM_H 34.0f
#define MENU_ITEM_GAP 6.0f

typedef struct InstallLayout {
    float x;
    float y;
    float w;
    float h;
    float main_x;
    float main_w;
    float caret_x;
    float caret_w;
    float icon_x;
    float icon_y;
    float icon_s;
    float text_x;
    float text_y;
    float text_w;
    float text_h;
    float text_px;
    float pad;
    float caret_cx;
    float caret_cy;
    float caret_s;
    float caret_stroke;
    float menu_x;
    float menu_y;
    float menu_w;
    float menu_h;
    float item_x;
    float item_w;
    float verify_y;
    float uninstall_y;
    float item_h;
    float hint_x;
    float hint_y;
    float hint_w;
    float hint_h;
    float hint_px;
} InstallLayout;

static float g_hover_main;
static float g_hover_caret;
static float g_hover_verify;
static float g_hover_uninstall;
static float g_menu;
static float g_hint;
static float g_shimmer;
static int g_open;
static int g_confirm;
static char g_secret[160];
static int g_secret_len;
static float g_hover_secret;

static int
hit(int mx, int my, float x, float y, float w, float h)
{
    return (float)mx >= x && (float)my >= y && (float)mx < x + w && (float)my < y + h;
}

static float
approach(float current, float target, float dt)
{
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    float next = current + (target - current) * (1.0f - expf(-16.0f * dt));
    if (next < 0.001f) {
        return 0.0f;
    }
    if (next > 0.999f && target >= 1.0f) {
        return 1.0f;
    }
    return next;
}

static int
install_need(Platform *platform)
{
    return platform && platform->install_need ? platform->install_need() : 0;
}

static void
clear_secret(void)
{
    memset(g_secret, 0, sizeof(g_secret));
    g_secret_len = 0;
}

static int
secret_tick(Platform *platform, float dt)
{
    int need = install_need(platform);
    float s = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float w = 360.0f * s;
    float h = 168.0f * s;
    float x = ((float)platform->width - w) * 0.5f;
    float y = ((float)platform->height - h) * 0.42f;
    float field_y = y + 78.0f * s;
    float btn_y = y + 122.0f * s;
    float inset = 18.0f * s;
    float btn_w = 120.0f * s;
    float btn_h = 36.0f * s;
    float btn_x = x + w - inset - btn_w;
    wchar_t mask[160];
    int i;
    int over_btn;

    if (need != 1 && need != 2) {
        if (g_secret_len) {
            clear_secret();
        }
        g_hover_secret = 0.0f;
        return 0;
    }

    if (platform->text_len > 0) {
        int n;
        for (n = 0; n < platform->text_len && g_secret_len < (int)sizeof(g_secret) - 1; n++) {
            char ch = platform->text[n];
            if (ch >= 32 && ch != 127) {
                g_secret[g_secret_len++] = ch;
                g_secret[g_secret_len] = '\0';
            }
        }
    }
    if (platform->key == PLATFORM_KEY_BACKSPACE && g_secret_len > 0) {
        g_secret[--g_secret_len] = '\0';
    }
    if (platform->key == PLATFORM_KEY_ENTER && g_secret_len > 0 && platform->install_submit_secret) {
        platform->install_submit_secret(g_secret);
        clear_secret();
    }

    over_btn = hit(platform->mouse_x, platform->mouse_y, btn_x, btn_y, btn_w, btn_h);
    g_hover_secret = approach(g_hover_secret, over_btn ? 1.0f : 0.0f, dt);

    modal_draw_overlay(platform->hdc, (float)platform->width, (float)platform->height, 1.0f);
    modal_draw_card(platform->hdc, x, y, w, h, 1.0f, s);
    modal_draw_title(
        platform->hdc,
        x + inset,
        y + 14.0f * s,
        w - inset * 2.0f,
        22.0f * s,
        need == 2 ? L"Steam Guard" : L"Steam password",
        s,
        1.0f
    );
    modal_draw_subtitle(
        platform->hdc,
        x + inset,
        y + 40.0f * s,
        w - inset * 2.0f,
        32.0f * s,
        need == 2 ? L"Enter the code from the Steam app." : L"Needed once so DepotDownloader can use this Steam login.",
        s,
        1.0f
    );
    for (i = 0; i < g_secret_len && i < 159; i++) {
        mask[i] = need == 2 ? (wchar_t)g_secret[i] : L'\u2022';
    }
    mask[i] = 0;
    if (need == 2) {
        os_utf8_to_wide(g_secret, mask, 160);
    }
    modal_draw_field(platform->hdc, x + inset, field_y, w - inset * 2.0f, 36.0f * s, mask[0] ? mask : L"", 1.0f, s);
    modal_draw_button(
        platform->hdc,
        btn_x,
        btn_y,
        btn_w,
        btn_h,
        L"Continue",
        ICON_LOG_IN,
        g_hover_secret,
        1.0f,
        g_secret_len <= 0,
        s
    );

    if (platform->mouse_pressed && over_btn && g_secret_len > 0 && platform->install_submit_secret) {
        platform->install_submit_secret(g_secret);
        clear_secret();
    }
    return 1;
}

static int
is_ready(Platform *platform)
{
    return platform->install_ready && platform->install_ready();
}

static int
is_busy(Platform *platform)
{
    return platform->install_busy && platform->install_busy();
}

static void
copy_label(Platform *platform, char *out, int max, IconId *icon)
{
    if (is_ready(platform)) {
        snprintf(out, (size_t)max, "Play");
        *icon = ICON_PLAY;
        return;
    }
    if (is_busy(platform)) {
        const char *status = platform->install_status ? platform->install_status() : "";
        if (status && strstr(status, "Connecting Steam")) {
            snprintf(out, (size_t)max, "...");
            *icon = ICON_DOWNLOAD;
            return;
        }
        float progress = platform->install_progress ? platform->install_progress() : 0.0f;
        if (progress < 0.0f) {
            progress = 0.0f;
        }
        if (progress > 1.0f) {
            progress = 1.0f;
        }
        snprintf(out, (size_t)max, "%d%%", (int)(progress * 100.0f + 0.5f));
        *icon = ICON_DOWNLOAD;
        return;
    }
    snprintf(out, (size_t)max, "Download");
    *icon = ICON_DOWNLOAD;
}

static int
status_is_quiet(const char *status)
{
    return !status ||
        status[0] == '\0' ||
        strcmp(status, "Ready") == 0 ||
        strcmp(status, "Dawn is ready") == 0 ||
        strcmp(status, "Ready to install Dawn") == 0 ||
        strcmp(status, "Steam is ready to download") == 0;
}

static void
copy_hint(Platform *platform, char *out, int max, int *shimmer)
{
    const char *status = platform->install_status ? platform->install_status() : "";
    int busy = is_busy(platform);
    float progress;

    *shimmer = 0;
    out[0] = '\0';
    if (!status) {
        status = "";
    }
    if (busy) {
        *shimmer = 1;
        progress = platform->install_progress ? platform->install_progress() : 0.0f;
        if (progress < 0.0f) {
            progress = 0.0f;
        }
        if (progress > 1.0f) {
            progress = 1.0f;
        }
        if (status[0] && progress > 0.01f && progress < 0.995f && !strchr(status, '%')) {
            snprintf(out, (size_t)max, "%s  ·  %d%%", status, (int)(progress * 100.0f + 0.5f));
        } else if (status[0]) {
            snprintf(out, (size_t)max, "%s", status);
        } else {
            snprintf(out, (size_t)max, "Working...");
        }
        return;
    }
    if (!status_is_quiet(status)) {
        snprintf(out, (size_t)max, "%s", status);
    }
}

static void
layout(Platform *platform, InstallLayout *out, const char *label)
{
    float dpi = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float inset = 20.0f * dpi;
    float gap;
    if (platform->corner_radius > inset) {
        inset = platform->corner_radius * 0.55f + 14.0f * dpi;
    }
    out->h = 36.0f * dpi;
    out->pad = 14.0f * dpi;
    out->icon_s = 16.0f * dpi;
    if (out->icon_s > out->h * 0.48f) {
        out->icon_s = out->h * 0.48f;
    }
    out->text_px = 13.0f * dpi;
    int tw = 0;
    int th = 0;
    if (platform->measure_label) {
        platform->measure_label(label ? label : "Download", (int)(out->text_px + 0.5f), 600, &tw, &th, NULL);
    }
    if (tw <= 0) {
        const char *use = label && label[0] ? label : "Download";
        tw = (int)((float)strlen(use) * out->text_px * 0.58f + 0.5f);
    }
    gap = 6.0f * dpi;
    out->main_w = out->pad + out->icon_s + gap + (float)tw + out->pad;
    out->caret_w = 32.0f * dpi;
    out->w = out->main_w + out->caret_w;
    out->x = (float)platform->width - inset - out->w;
    out->y = (float)platform->height - inset - out->h;
    out->main_x = out->x;
    out->caret_x = out->x + out->main_w;
    out->icon_x = out->main_x + out->pad + out->icon_s * 0.5f;
    out->icon_y = out->y + out->h * 0.5f;
    out->caret_s = 18.0f * dpi;
    if (out->caret_s < 16.0f) {
        out->caret_s = 16.0f;
    }
    if (out->caret_s > out->h * 0.56f) {
        out->caret_s = out->h * 0.56f;
    }
    out->caret_stroke = out->caret_s * (2.35f / 24.0f);
    if (out->caret_stroke < 1.9f) {
        out->caret_stroke = 1.9f;
    }
    if (out->caret_stroke > 3.6f) {
        out->caret_stroke = 3.6f;
    }
    out->caret_cx = out->caret_x + 1.0f + (out->caret_w - 1.0f) * 0.5f;
    out->caret_cy = out->y + out->h * 0.5f;
    out->text_x = out->main_x + out->pad + out->icon_s + gap;
    out->text_y = out->y;
    out->text_w = out->main_x + out->main_w - out->pad - out->text_x;
    out->text_h = out->h;

    out->menu_w = MENU_W * dpi;
    out->item_h = MENU_ITEM_H * dpi;
    out->menu_h = MENU_INSET * dpi + out->item_h + MENU_ITEM_GAP * dpi + out->item_h + MENU_INSET * dpi;
    out->menu_x = out->caret_x + out->caret_w - out->menu_w;
    if (out->menu_x < inset) {
        out->menu_x = inset;
    }
    out->menu_y = out->y - 8.0f * dpi - out->menu_h;
    out->item_x = out->menu_x + MENU_INSET * dpi;
    out->item_w = out->menu_w - MENU_INSET * dpi * 2.0f;
    out->verify_y = out->menu_y + MENU_INSET * dpi;
    out->uninstall_y = out->verify_y + out->item_h + MENU_ITEM_GAP * dpi;
}

static void
layout_hint(Platform *platform, InstallLayout *out, const char *hint)
{
    float dpi = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float inset = 20.0f * dpi;
    float gap = 14.0f * dpi;
    int tw = 0;
    int th = 0;
    float max_w;

    if (platform->corner_radius > inset) {
        inset = platform->corner_radius * 0.55f + 14.0f * dpi;
    }
    out->hint_px = 12.0f * dpi;
    out->hint_y = out->y;
    out->hint_h = out->h;
    max_w = (float)platform->width * 0.42f;
    if (max_w < 160.0f * dpi) {
        max_w = 160.0f * dpi;
    }
    if (max_w > 420.0f * dpi) {
        max_w = 420.0f * dpi;
    }
    if (out->x - gap - inset < max_w) {
        max_w = out->x - gap - inset;
    }
    if (max_w < 8.0f) {
        out->hint_x = inset;
        out->hint_w = 0.0f;
        return;
    }
    if (platform->measure_label && hint && hint[0]) {
        platform->measure_label(hint, (int)(out->hint_px + 0.5f), 500, &tw, &th, NULL);
    }
    if (tw <= 0 && hint && hint[0]) {
        tw = (int)((float)strlen(hint) * out->hint_px * 0.52f + 0.5f);
    }
    out->hint_w = (float)tw + 4.0f * dpi;
    if (out->hint_w > max_w) {
        out->hint_w = max_w;
    }
    out->hint_x = out->x - gap - out->hint_w;
    if (out->hint_x < inset) {
        out->hint_x = inset;
        out->hint_w = out->x - gap - inset;
        if (out->hint_w < 0.0f) {
            out->hint_w = 0.0f;
        }
    }
}

static void
draw_menu_item(
    Platform *platform,
    float x,
    float y,
    float w,
    float h,
    float open,
    float hover,
    int pressed,
    int disabled,
    int danger,
    IconId icon,
    const wchar_t *label
)
{
    const ThemeChrome *chrome = theme_chrome();
    float dpi = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float use_hover = disabled ? 0.0f : (pressed ? 1.0f : hover);
    float icon_s = 16.0f * dpi;
    float icon_cx = x + 12.0f * dpi + icon_s * 0.5f;
    float text_x = x + 12.0f * dpi + icon_s + 8.0f * dpi;
    float text_w = x + w - text_x - 10.0f * dpi;
    uint32_t fill = modal_button_fill(use_hover);
    uint32_t stroke = use_hover > 0.2f ? chrome->hover : chrome->muted;
    uint32_t fg = disabled ? chrome->muted : (use_hover > 0.2f ? chrome->hover : chrome->title_color);
    int alpha = (int)(open * (disabled ? 140.0f : 255.0f));

    if (danger && !disabled && use_hover > 0.01f) {
        uint32_t red = chrome->close ? chrome->close : 0xe81123;
        int tint = (int)(use_hover * 255.0f);
        fill = modal_mix(fill, red, 48 + tint * 72 / 255);
        stroke = modal_mix(stroke, red, tint);
        fg = modal_mix(fg, red, tint);
    }

    icon_round_rect(platform->hdc, x, y, w, h, h * 0.28f, fill, (int)(open * 255.0f));
    icon_round_stroke(
        platform->hdc,
        x,
        y,
        w,
        h,
        h * 0.28f,
        stroke,
        (int)(open * (28.0f + use_hover * 36.0f)),
        1.0f
    );
    icon_set_alpha(alpha);
    icon_draw(platform->hdc, icon, icon_cx, y + h * 0.5f, icon_s, fg, 0.0f);
    icon_set_alpha(255);
    icon_draw_label_alpha(platform->hdc, text_x, y, text_w, h, label, fg, 13.0f * dpi, 600, alpha);
}

int
install_button_wants_mouse(Platform *platform)
{
    InstallLayout L;
    char label[32];
    IconId icon = ICON_DOWNLOAD;
    if (!platform) {
        return 0;
    }
    if (g_open || g_menu > 0.02f || install_need(platform)) {
        return 1;
    }
    copy_label(platform, label, (int)sizeof(label), &icon);
    layout(platform, &L, label);
    return hit(platform->mouse_x, platform->mouse_y, L.x, L.y, L.w, L.h);
}

void
install_button_tick(Platform *platform, float dt)
{
    InstallLayout L;
    char label[32];
    char hint[192];
    wchar_t wide[32];
    wchar_t hint_w[192];
    IconId icon = ICON_DOWNLOAD;
    int shimmer = 0;

    if (!platform) {
        return;
    }
    if (secret_tick(platform, dt)) {
        return;
    }
    copy_label(platform, label, (int)sizeof(label), &icon);
    copy_hint(platform, hint, (int)sizeof(hint), &shimmer);
    layout(platform, &L, label);
    layout_hint(platform, &L, hint);

    int blocked = login_modal_visible() || settings_modal_visible();
    int ready = is_ready(platform);
    int busy = is_busy(platform);
    int mx = platform->mouse_x;
    int my = platform->mouse_y;
    int over_main = !blocked && hit(mx, my, L.main_x, L.y, L.main_w, L.h);
    int over_caret = !blocked && hit(mx, my, L.caret_x, L.y, L.caret_w, L.h);
    int over_menu = !blocked && (g_open || g_menu > 0.02f) &&
        hit(mx, my, L.menu_x, L.menu_y, L.menu_w, L.menu_h);
    int over_verify = !blocked && g_open && hit(mx, my, L.item_x, L.verify_y, L.item_w, L.item_h);
    int over_uninstall = !blocked && g_open && hit(mx, my, L.item_x, L.uninstall_y, L.item_w, L.item_h);

    if (blocked && g_open) {
        g_open = 0;
        g_confirm = 0;
    }

    g_hover_main = approach(g_hover_main, over_main ? 1.0f : 0.0f, dt);
    g_hover_caret = approach(g_hover_caret, (over_caret || g_open) ? 1.0f : 0.0f, dt);
    g_hover_verify = approach(g_hover_verify, over_verify ? 1.0f : 0.0f, dt);
    g_hover_uninstall = approach(g_hover_uninstall, over_uninstall ? 1.0f : 0.0f, dt);
    g_menu = modal_approach(g_menu, g_open ? 1.0f : 0.0f, dt);
    g_hint = approach(g_hint, hint[0] && L.hint_w > 8.0f ? 1.0f : 0.0f, dt);
    if (shimmer && g_hint > 0.02f) {
        g_shimmer += dt / 1.7f;
        if (g_shimmer > 1.0f) {
            g_shimmer -= (float)((int)g_shimmer);
        }
    } else {
        g_shimmer = 0.0f;
    }

    uint32_t fg = BTN_TEXT;
    if (g_hint > 0.02f && L.hint_w > 8.0f) {
        const ThemeChrome *chrome = theme_chrome();
        uint32_t dim = modal_mix(chrome->muted, 0x000000, 72);
        uint32_t shine = modal_mix(dim, 0xffffff, 110);
        int alpha = (int)(g_hint * 210.0f);
        os_utf8_to_wide(hint, hint_w, 192);
        if (shimmer) {
            icon_draw_label_shimmer(
                platform->hdc,
                L.hint_x,
                L.hint_y,
                L.hint_w,
                L.hint_h,
                hint_w,
                dim,
                shine,
                L.hint_px,
                500,
                alpha,
                g_shimmer
            );
        } else {
            icon_draw_label_alpha(
                platform->hdc,
                L.hint_x,
                L.hint_y,
                L.hint_w,
                L.hint_h,
                hint_w,
                dim,
                L.hint_px,
                500,
                alpha
            );
        }
    }
    float radius = L.h * 0.5f;
    float progress = busy && platform->install_progress ? platform->install_progress() : 0.0f;
    if (progress < 0.0f) {
        progress = 0.0f;
    }
    if (progress > 1.0f) {
        progress = 1.0f;
    }

    icon_round_rect(platform->hdc, L.x, L.y + 1.5f, L.w, L.h, radius, 0x000000, 28);
    icon_round_rect(platform->hdc, L.x, L.y, L.w, L.h, radius, BTN_FILL, 255);
    if (g_hover_main > 0.02f) {
        icon_round_rect_corners(
            platform->hdc,
            L.main_x,
            L.y,
            L.main_w,
            L.h,
            radius,
            0.0f,
            0.0f,
            radius,
            BTN_FILL_HOVER,
            (int)(g_hover_main * 255.0f)
        );
    }
    if (g_hover_caret > 0.02f) {
        icon_round_rect_corners(
            platform->hdc,
            L.caret_x,
            L.y,
            L.caret_w,
            L.h,
            0.0f,
            radius,
            radius,
            0.0f,
            BTN_FILL_HOVER,
            (int)(g_hover_caret * 255.0f)
        );
    }
    if (busy && progress > 0.01f) {
        icon_round_rect_corners(
            platform->hdc,
            L.main_x,
            L.y,
            L.main_w * progress,
            L.h,
            radius,
            0.0f,
            0.0f,
            radius,
            BTN_PROGRESS,
            255
        );
    }

    float div_h = L.h * 0.46f;
    icon_fill_rect(
        platform->hdc,
        L.caret_x,
        L.y + (L.h - div_h) * 0.5f,
        1.0f,
        div_h,
        0x111827,
        28
    );

    float ix = L.icon_x;
    if (icon == ICON_PLAY) {
        ix += L.icon_s * 0.06f;
    }
    icon_draw(platform->hdc, icon, ix, L.icon_y, L.icon_s, fg, 0.0f);
    os_utf8_to_wide(label, wide, 32);
    icon_draw_label(platform->hdc, L.text_x, L.text_y, L.text_w, L.text_h, wide, fg, L.text_px, 600);

    icon_draw(
        platform->hdc,
        g_open ? ICON_CHEVRON_DOWN : ICON_CHEVRON_UP,
        L.caret_cx,
        L.caret_cy + (g_open ? -0.5f : 0.5f) * (platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f),
        L.caret_s,
        fg,
        L.caret_stroke
    );

    if (g_menu > 0.02f) {
        modal_draw_card(platform->hdc, L.menu_x, L.menu_y, L.menu_w, L.menu_h, g_menu, platform->dpi_scale);
        draw_menu_item(
            platform,
            L.item_x,
            L.verify_y,
            L.item_w,
            L.item_h,
            g_menu,
            g_hover_verify,
            platform->mouse_down && over_verify,
            busy,
            0,
            ICON_SEARCH,
            L"Check Game Integrity"
        );
        draw_menu_item(
            platform,
            L.item_x,
            L.uninstall_y,
            L.item_w,
            L.item_h,
            g_menu,
            g_confirm && !busy && ready && g_hover_uninstall < 0.5f ? 0.5f : g_hover_uninstall,
            platform->mouse_down && over_uninstall,
            busy || !ready,
            1,
            ICON_X,
            g_confirm ? L"Confirm uninstall" : L"Uninstall"
        );
    }

    if (!platform->mouse_pressed || blocked) {
        return;
    }
    if (over_main) {
        g_open = 0;
        g_confirm = 0;
        if (ready) {
            if (platform->install_launch) {
                platform->install_launch();
            }
        } else if (!busy) {
            if (!login_modal_signed_in()) {
                login_modal_open();
            } else if (platform->install_start) {
                platform->install_start();
            }
        }
        return;
    }
    if (over_caret) {
        g_open = !g_open;
        if (!g_open) {
            g_confirm = 0;
        }
        return;
    }
    if (over_verify && !busy) {
        g_open = 0;
        g_confirm = 0;
        if (!login_modal_signed_in()) {
            login_modal_open();
        } else if (platform->install_verify) {
            platform->install_verify();
        }
        return;
    }
    if (over_uninstall && !busy && ready) {
        if (!g_confirm) {
            g_confirm = 1;
            return;
        }
        g_open = 0;
        g_confirm = 0;
        if (platform->install_uninstall) {
            platform->install_uninstall();
        }
        return;
    }
    if (!over_menu) {
        g_open = 0;
        g_confirm = 0;
    }
}
