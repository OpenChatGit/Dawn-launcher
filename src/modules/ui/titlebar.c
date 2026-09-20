#include "titlebar.h"
#include "login_modal.h"
#include "settings_modal.h"
#include "modal_skin.h"
#include "shared/chrome.h"
#include "shared/icons.h"
#include "shared/theme.h"
#include "shared/os.h"
#include "shared/user_id.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define ACCOUNT_MENU_W 220.0f
#define ACCOUNT_INSET 8.0f
#define ACCOUNT_ICON 24.0f
#define ACCOUNT_TEXT_GAP 8.0f
#define ACCOUNT_IDENTITY_H 32.0f
#define ACCOUNT_PILL_H 30.0f
#define ACCOUNT_ROW_GAP 4.0f
#define ACCOUNT_DIVIDER_GAP 6.0f
#define ACCOUNT_RADIUS 10.0f
#define ACCOUNT_ITEM_ICON 16.0f
#define UPDATE_PAD_X 10.0f
#define UPDATE_ICON_GAP 6.0f
#define UPDATE_TEXT_PX 12.0f
#define UPDATE_TIP_H 26.0f
#define UPDATE_TIP_PAD 12.0f
#define UPDATE_TIP_GAP 6.0f

typedef struct TitlebarLayout {
    int btn;
    int gap;
    int pad;
    int bar;
    int y;
    int close_x;
    int min_x;
    int avatar_x;
    int update_x;
    int update_w;
    int menu_x;
    int menu_y;
    int menu_w;
    int menu_h;
    int item_x;
    int item_y;
    int item_w;
    int item_h;
    int ident_x;
    int ident_y;
    int ident_h;
    int divider_y;
    int settings_y;
    int icon_s;
    int text_x;
    int tip_x;
    int tip_y;
    int tip_w;
    int tip_h;
    int copy_x;
    int copy_y;
    int copy_w;
    int copy_h;
} TitlebarLayout;

static float g_hover_min;
static float g_hover_close;
static float g_hover_avatar;
static float g_hover_update;
static float g_hover_update_ok;
static float g_hover_update_close;
static int g_update_open;
static float g_hover_signin;
static float g_hover_settings;
static float g_hover_copy;
static float g_hover_tip;
static float g_menu;
static int g_open;
static uint32_t g_copied_ms;

static int
hit(int mx, int my, int x, int y, int w, int h)
{
    return mx >= x && my >= y && mx < x + w && my < y + h;
}

static int
px(float value, float scale)
{
    return (int)(value * scale + 0.5f);
}

static float
approach_rate(float current, float target, float dt, float rate)
{
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    float next = current + (target - current) * (1.0f - expf(-rate * dt));
    if (next < 0.001f) {
        return 0.0f;
    }
    if (next > 0.999f && target >= 1.0f) {
        return 1.0f;
    }
    return next;
}

static float
approach(float current, float target, float dt)
{
    return approach_rate(current, target, dt, 22.0f);
}

static void
layout(Platform *platform, TitlebarLayout *out)
{
    float s = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    out->btn = px(CHROME_BTN, s);
    out->gap = px(CHROME_GAP, s);
    out->pad = px(CHROME_PAD, s);
    out->bar = px(CHROME_TITLEBAR, s);
    out->y = (out->bar - out->btn) / 2;
    out->close_x = platform->width - out->pad - out->btn;
    out->min_x = out->close_x - out->gap - out->btn;
    out->avatar_x = out->min_x - out->gap - out->btn;
    {
        float icon_s = (float)out->btn * 0.42f;
        float text_px = (float)px(UPDATE_TEXT_PX, s);
        float pad = UPDATE_PAD_X * s;
        float gap = UPDATE_ICON_GAP * s;
        float tw = 0.0f;
        if (platform->hdc) {
            tw = icon_measure_label(platform->hdc, L"New Version", text_px, 600);
        }
        if (tw < 8.0f) {
            tw = 11.0f * text_px * 0.72f;
        }
        out->update_w = (int)(pad + icon_s + gap + tw + pad + 0.5f);
    }
    out->update_x = out->avatar_x - out->gap - out->update_w;
    {
        const char *ver = platform->update_version ? platform->update_version() : "";
        float tip_pad = UPDATE_TIP_PAD * s;
        float tw = 0.0f;
        wchar_t tip[48];

        tip[0] = 0;
        if (ver && ver[0]) {
            char line[48];
            snprintf(line, sizeof(line), "v%s", ver);
            os_utf8_to_wide(line, tip, 48);
        }
        if (tip[0] && platform->hdc) {
            tw = icon_measure_label(platform->hdc, tip, (float)px(11, s), 600);
        }
        if (tip[0] && tw < 8.0f) {
            tw = (float)wcslen(tip) * 11.0f * s * 0.62f;
        }
        out->tip_h = px(UPDATE_TIP_H, s);
        out->tip_w = tip[0] ? (int)(tip_pad + tw + tip_pad + 0.5f) : 0;
        out->tip_x = out->update_x - px(UPDATE_TIP_GAP, s) - out->tip_w;
        if (out->tip_x < out->pad) {
            out->tip_x = out->pad;
        }
        out->tip_y = out->y + (out->btn - out->tip_h) / 2;
    }

    int inset = px(ACCOUNT_INSET, s);
    int div = px(ACCOUNT_DIVIDER_GAP, s);
    out->icon_s = px(ACCOUNT_ICON, s);
    out->ident_h = px(ACCOUNT_IDENTITY_H, s);
    out->item_h = px(ACCOUNT_PILL_H, s);
    out->menu_w = px(ACCOUNT_MENU_W, s);
    out->menu_h = inset + out->ident_h + div + 1 + div +
        out->item_h + px(ACCOUNT_ROW_GAP, s) + out->item_h + inset;
    out->menu_x = out->avatar_x + out->btn - out->menu_w;
    if (out->menu_x < out->pad) {
        out->menu_x = out->pad;
    }
    out->menu_y = out->bar + px(8, s);
    out->ident_x = out->menu_x + inset;
    out->ident_y = out->menu_y + inset;
    out->divider_y = out->ident_y + out->ident_h + div;
    out->item_x = out->menu_x + inset;
    out->settings_y = out->divider_y + 1 + div;
    out->item_y = out->settings_y + out->item_h + px(ACCOUNT_ROW_GAP, s);
    out->item_w = out->menu_w - inset * 2;
    out->text_x = out->ident_x + out->icon_s + px(ACCOUNT_TEXT_GAP, s);
    {
        float copy_px = (float)px(11, s);
        float tw = 0.0f;
        float pad = 8.0f * s;

        if (platform->hdc) {
            tw = icon_measure_label(platform->hdc, L"Copy ID", copy_px, 600);
        }
        if (tw < 8.0f) {
            tw = 7.0f * copy_px * 0.62f;
        }
    out->copy_h = px(20, s);
    out->copy_w = (int)(pad + tw + pad + 0.5f);
    out->copy_x = out->menu_x + out->menu_w - inset - out->copy_w;
    out->copy_y = out->ident_y + (out->ident_h - out->copy_h) / 2;
        if (out->copy_y < out->ident_y) {
            out->copy_y = out->ident_y;
        }
    }
}

static void
control_button(
    Platform *platform,
    float x,
    float y,
    float size,
    float hover,
    int pressed,
    int is_close,
    IconId icon
)
{
    const ThemeChrome *chrome = theme_chrome();
    uint32_t bg = is_close ? chrome->close : chrome->hover;
    int alpha = is_close
        ? (int)(hover * (pressed ? 240 : 215))
        : (int)(hover * (pressed ? 48 : 30));

    if (alpha > 0) {
        icon_round_rect(platform->hdc, x, y, size, size, size * 0.38f, bg, alpha);
    }

    uint32_t idle = chrome->muted;
    uint32_t fg = idle;
    if (hover > 0.2f) {
        fg = chrome->hover;
    }

    float icon_size = size * 0.42f;
    float stroke = icon_size * (1.75f / 24.0f);
    if (stroke < 1.0f) {
        stroke = 1.0f;
    }
    icon_draw(platform->hdc, icon, x + size * 0.5f, y + size * 0.5f, icon_size, fg, stroke);
}

static int
update_ready(Platform *platform)
{
    return platform && platform->update_available && platform->update_available();
}

static void
update_chip(Platform *platform, const TitlebarLayout *L, float hover, int pressed)
{
    const ThemeChrome *chrome = theme_chrome();
    float s = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float x = (float)L->update_x;
    float y = (float)L->y;
    float w = (float)L->update_w;
    float h = (float)L->btn;
    float pad = UPDATE_PAD_X * s;
    float icon_s = h * 0.42f;
    float gap = UPDATE_ICON_GAP * s;
    float use = pressed ? 1.0f : hover;
    uint32_t fg = use > 0.15f ? chrome->hover : chrome->muted;
    float stroke = icon_s * (1.75f / 24.0f);
    int fill = (int)(use * (pressed ? 48.0f : 28.0f));
    float text_x = x + pad + icon_s + gap;
    float text_w = w - pad * 2.0f - icon_s - gap;

    if (stroke < 1.0f) {
        stroke = 1.0f;
    }
    if (fill > 0) {
        float inset = 3.0f * s;
        icon_round_rect(
            platform->hdc,
            x,
            y + inset,
            w,
            h - inset * 2.0f,
            (h - inset * 2.0f) * 0.5f,
            chrome->hover,
            fill
        );
    }
    icon_draw(platform->hdc, ICON_DOWNLOAD, x + pad + icon_s * 0.5f, y + h * 0.5f, icon_s, fg, stroke);
    icon_draw_label_full(
        platform->hdc,
        text_x,
        y,
        text_w,
        h,
        L"New Version",
        fg,
        (float)px(UPDATE_TEXT_PX, s),
        600
    );
}

static void
update_tooltip(Platform *platform, const TitlebarLayout *L, float open)
{
    const ThemeChrome *chrome = theme_chrome();
    const char *ver = platform->update_version ? platform->update_version() : "";
    float s = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float x = (float)L->tip_x;
    float y = (float)L->tip_y;
    float w = (float)L->tip_w;
    float h = (float)L->tip_h;
    float pad = UPDATE_TIP_PAD * s;
    int alpha = (int)(open * 255.0f + 0.5f);
    char line[48];
    wchar_t tip[48];

    if (open < 0.02f || w < 8.0f || !ver || !ver[0]) {
        return;
    }
    snprintf(line, sizeof(line), "v%s", ver);
    os_utf8_to_wide(line, tip, 48);
    if (alpha > 255) {
        alpha = 255;
    }
    icon_round_rect(platform->hdc, x, y + 3.0f * s, w, h, h * 0.5f, 0x000000, alpha * 40 / 255);
    icon_round_rect(platform->hdc, x, y, w, h, h * 0.5f, modal_panel_color(), alpha * 252 / 255);
    icon_round_stroke(platform->hdc, x, y, w, h, h * 0.5f, chrome->muted, alpha * 40 / 255, 1.0f);
    icon_draw_label_full(
        platform->hdc,
        x + pad,
        y,
        w - pad * 2.0f,
        h,
        tip,
        chrome->title_color,
        (float)px(11, s),
        600
    );
}

static void
avatar_button(Platform *platform, const TitlebarLayout *L, float hover, int pressed)
{
    const ThemeChrome *chrome = theme_chrome();
    float x = (float)L->avatar_x;
    float y = (float)L->y;
    float size = (float)L->btn;
    int fill = (int)(hover * (pressed ? 48.0f : 30.0f));
    if (fill > 0) {
        icon_round_rect(platform->hdc, x, y, size, size, size * 0.5f, chrome->hover, fill);
    }

    const char *avatar = login_modal_avatar_path();
    if (login_modal_signed_in() && avatar && avatar[0]) {
        float photo = size * 0.68f;
        icon_draw_avatar(platform->hdc, avatar, x + size * 0.5f, y + size * 0.5f, photo);
        return;
    }
    uint32_t fg = hover > 0.2f || g_open ? chrome->hover : chrome->muted;
    float icon_size = size * 0.50f;
    float stroke = icon_size * (1.75f / 24.0f);
    if (stroke < 1.0f) {
        stroke = 1.0f;
    }
    icon_draw(platform->hdc, ICON_USER, x + size * 0.5f, y + size * 0.5f, icon_size, fg, stroke);
}

static void
draw_row_icon(void *hdc, IconId id, float cx, float cy, float box, uint32_t rgb)
{
    float size = box * 0.46f;
    float stroke = size * (1.75f / 24.0f);
    if (stroke < 1.0f) {
        stroke = 1.0f;
    }
    icon_draw(hdc, id, cx, cy, size, rgb, stroke);
}

static void
account_menu_card(Platform *platform, float x, float y, float w, float h, float open)
{
    const ThemeChrome *chrome = theme_chrome();
    float s = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float radius = (float)px(ACCOUNT_RADIUS, s);
    int alpha = (int)(open * 255.0f);

    icon_round_rect(platform->hdc, x, y + 2.0f * s, w, h, radius, 0x000000, alpha * 40 / 255);
    icon_round_rect(platform->hdc, x, y, w, h, radius, modal_panel_color(), alpha);
    icon_round_stroke(platform->hdc, x, y, w, h, radius, chrome->muted, alpha * 28 / 255, 1.0f);
}

static void
menu_pill(
    Platform *platform,
    const TitlebarLayout *L,
    float y,
    float open,
    float hover,
    int pressed,
    IconId icon,
    const wchar_t *label
)
{
    const ThemeChrome *chrome = theme_chrome();
    float s = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float use_hover = pressed ? 1.0f : hover;
    float x = (float)L->item_x;
    float w = (float)L->item_w;
    float h = (float)L->item_h;
    float pad = (float)px(10, s);
    float icon_s = (float)px(ACCOUNT_ITEM_ICON, s);
    float icon_cx = x + pad + icon_s * 0.5f;
    float text_x = x + pad + icon_s + (float)px(8, s);
    float text_w = x + w - text_x - pad;
    float radius = (float)px(7, s);
    uint32_t fg = chrome->title_color;
    int alpha = open >= 0.98f ? 255 : (int)(open * 255.0f);

    if (use_hover > 0.01f) {
        uint32_t fill = modal_mix(modal_panel_color(), 0xffffff, 10 + (int)(use_hover * 16.0f));
        icon_round_rect(platform->hdc, x, y, w, h, radius, fill, (int)(open * use_hover * 255.0f));
        fg = use_hover > 0.2f ? chrome->hover : chrome->title_color;
    }

    icon_set_alpha(alpha);
    icon_draw(platform->hdc, icon, icon_cx, y + h * 0.5f, icon_s, fg, 0.0f);
    icon_set_alpha(255);
    icon_draw_label_alpha(platform->hdc, text_x, y, text_w, h, label, fg, (float)px(13, s), 600, alpha);
}

static void
account_menu(
    Platform *platform,
    const TitlebarLayout *L,
    float open,
    float settings_hover,
    float item_hover,
    int settings_pressed,
    int item_pressed,
    int show_content
)
{
    if (open < 0.02f) {
        return;
    }

    const ThemeChrome *chrome = theme_chrome();
    float s = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float x = (float)L->menu_x;
    float y = (float)L->menu_y;
    float w = (float)L->menu_w;
    float h = (float)L->menu_h;

    account_menu_card(platform, x, y, w, h, open);

    if (!show_content) {
        return;
    }

    float icon_s = (float)L->icon_s;
    float ident_y = (float)L->ident_y;
    float ident_x = (float)L->ident_x;
    float icon_cx = ident_x + icon_s * 0.5f;
    float ident_cy = ident_y + (float)L->ident_h * 0.5f;
    icon_round_rect(
        platform->hdc,
        ident_x,
        ident_cy - icon_s * 0.5f,
        icon_s,
        icon_s,
        icon_s * 0.5f,
        chrome->hover,
        (int)(open * 18.0f)
    );
    const char *avatar = login_modal_avatar_path();
    if (login_modal_signed_in() && avatar && avatar[0]) {
        icon_draw_avatar(platform->hdc, avatar, icon_cx, ident_cy, icon_s);
    } else {
        icon_round_stroke(
            platform->hdc,
            ident_x,
            ident_cy - icon_s * 0.5f,
            icon_s,
            icon_s,
            icon_s * 0.5f,
            chrome->muted,
            (int)(open * 40.0f),
            1.0f
        );
        draw_row_icon(platform->hdc, ICON_USER, icon_cx, ident_cy, icon_s, chrome->muted);
    }

    int signed_in = login_modal_signed_in();
    const char *user = login_modal_username();
    wchar_t name_w[64];
    name_w[0] = 0;
    if (signed_in && user && user[0]) {
        os_utf8_to_wide(user, name_w, 64);
    }

    float text_x = (float)L->text_x;
    float name_w_max = (float)L->copy_x - 8.0f * s - text_x;
    float status_w = x + w - text_x - (float)px(ACCOUNT_INSET, s);
    if (name_w_max < 24.0f) {
        name_w_max = 24.0f;
    }
    float name_h = (float)L->ident_h * 0.55f;
    icon_draw_label_alpha(
        platform->hdc,
        text_x,
        ident_y + 2.0f,
        name_w_max,
        name_h,
        signed_in && name_w[0] ? name_w : L"Guest",
        chrome->title_color,
        (float)px(13, s),
        600,
        (int)(open * 255.0f)
    );
    icon_draw_label_alpha(
        platform->hdc,
        text_x,
        ident_y + name_h - 1.0f,
        status_w,
        (float)L->ident_h - name_h,
        signed_in ? L"Signed in" : L"Not signed in",
        chrome->muted,
        (float)px(12, s),
        600,
        (int)(open * 255.0f)
    );

    {
        int copied = g_copied_ms && (os_tick_ms() - g_copied_ms) < 1400u;
        float use = platform->mouse_down &&
            hit(platform->mouse_x, platform->mouse_y, L->copy_x, L->copy_y, L->copy_w, L->copy_h)
            ? 1.0f : g_hover_copy;
        uint32_t fg = use > 0.2f || copied ? chrome->hover : chrome->muted;
        float cx = (float)L->copy_x;
        float cy = (float)L->copy_y;
        float cw = (float)L->copy_w;
        float ch = (float)L->copy_h;
        float cr = (float)px(6, s);

        if (use > 0.01f || copied) {
            uint32_t fill = modal_mix(modal_panel_color(), 0xffffff, copied ? 22 : 10 + (int)(use * 16.0f));
            icon_round_rect(platform->hdc, cx, cy, cw, ch, cr, fill, (int)(open * 255.0f));
        }
        icon_draw_label_center_alpha(
            platform->hdc,
            cx,
            cy,
            cw,
            ch,
            copied ? L"Copied" : L"Copy ID",
            fg,
            (float)px(12, s),
            600,
            (int)(open * 255.0f)
        );
    }

    float div_y = (float)L->divider_y;
    float div_x = ident_x;
    float div_w = (float)L->item_w;
    icon_round_rect(platform->hdc, div_x, div_y, div_w, 1.0f, 0.0f, chrome->hover, (int)(open * 28.0f));

    menu_pill(
        platform,
        L,
        (float)L->settings_y,
        open,
        settings_hover,
        settings_pressed,
        ICON_SETTINGS,
        L"Settings"
    );
    menu_pill(
        platform,
        L,
        (float)L->item_y,
        open,
        item_hover,
        item_pressed,
        ICON_LOG_IN,
        signed_in ? L"Sign out" : L"Sign in"
    );
}

int
titlebar_modal_visible(void)
{
    return g_update_open;
}

static int
update_modal_tick(Platform *platform, float dt)
{
    float s = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float inset = (float)modal_px(MODAL_INSET, s);
    float w = (float)modal_px(360, s);
    float h = (float)modal_px(208, s);
    float x;
    float y;
    float close_x;
    float close_y;
    float close_s;
    float btn_w;
    float btn_h = (float)modal_px(MODAL_BTN_H, s);
    float btn_y;
    float cancel_x;
    float ok_x;
    int busy = platform->update_busy && platform->update_busy();
    int over_close;
    int over_ok;
    int over_cancel;
    float progress = (busy && platform->update_progress) ? platform->update_progress() : -1.0f;
    int pct;
    wchar_t title[64];
    wchar_t body[160];
    wchar_t status_w[160];
    wchar_t ok_label[24];
    const char *ver = platform->update_version ? platform->update_version() : "";
    const char *status = platform->update_status ? platform->update_status() : "";
    char line[160];

    if (w > (float)platform->width - 32.0f) {
        w = (float)platform->width - 32.0f;
    }
    x = ((float)platform->width - w) * 0.5f;
    y = ((float)platform->height - h) * 0.5f;
    modal_place_close(x, y, w, s, &close_x, &close_y, &close_s);
    btn_w = (w - inset * 2.0f - (float)modal_px(8, s)) * 0.5f;
    btn_y = y + h - inset - btn_h;
    cancel_x = x + inset;
    ok_x = cancel_x + btn_w + (float)modal_px(8, s);
    over_close = modal_hit(platform->mouse_x, platform->mouse_y, close_x, close_y, close_s, close_s);
    over_ok = modal_hit(platform->mouse_x, platform->mouse_y, ok_x, btn_y, btn_w, btn_h);
    over_cancel = modal_hit(platform->mouse_x, platform->mouse_y, cancel_x, btn_y, btn_w, btn_h);
    g_hover_update_ok = approach(g_hover_update_ok, over_ok ? 1.0f : 0.0f, dt);
    g_hover_update_close = approach(g_hover_update_close, (over_close || over_cancel) ? 1.0f : 0.0f, dt);

    if (ver && ver[0]) {
        snprintf(line, sizeof(line), "Dawn %s", ver);
    } else {
        snprintf(line, sizeof(line), "Update");
    }
    os_utf8_to_wide(line, title, 64);
    os_utf8_to_wide(
        busy ? "Downloading the new launcher and replacing this install." :
            "Download the official build, replace this launcher, and restart.",
        body,
        160
    );
    if (status && status[0]) {
        os_utf8_to_wide(status, status_w, 160);
    } else {
        status_w[0] = 0;
    }
    if (progress < 0.0f) {
        progress = 0.0f;
    }
    if (progress > 1.0f) {
        progress = 1.0f;
    }
    pct = (int)(progress * 100.0f + 0.5f);
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    if (!busy) {
        wcscpy(ok_label, L"Update");
    } else if (status && strstr(status, "Install")) {
        wcscpy(ok_label, L"Installing");
    } else if (pct < 1 || (status && strstr(status, "Starting"))) {
        wcscpy(ok_label, L"Starting");
    } else {
        snprintf(line, sizeof(line), "%d%%", pct);
        os_utf8_to_wide(line, ok_label, 24);
    }

    modal_draw_overlay(platform->hdc, (float)platform->width, (float)platform->height, 1.0f);
    modal_draw_card(platform->hdc, x, y, w, h, 1.0f, s);
    modal_draw_title(platform->hdc, x + inset, y + (float)modal_px(14, s), w - inset * 2.0f - close_s, (float)modal_px(20, s), title, s, 1.0f);
    modal_draw_close(platform->hdc, close_x, close_y, close_s, g_hover_update_close, 1.0f);
    modal_draw_subtitle(
        platform->hdc,
        x + inset,
        y + (float)modal_px(42, s),
        w - inset * 2.0f,
        (float)modal_px(40, s),
        body,
        s,
        1.0f
    );
    if (status_w[0]) {
        modal_draw_subtitle(
            platform->hdc,
            x + inset,
            y + (float)modal_px(88, s),
            w - inset * 2.0f,
            (float)modal_px(20, s),
            status_w,
            s,
            1.0f
        );
    }
    modal_draw_button(
        platform->hdc,
        cancel_x,
        btn_y,
        btn_w,
        btn_h,
        L"Not now",
        ICON_X,
        g_hover_update_close,
        1.0f,
        0,
        s,
        -1.0f
    );
    modal_draw_button(
        platform->hdc,
        ok_x,
        btn_y,
        btn_w,
        btn_h,
        ok_label,
        ICON_DOWNLOAD,
        g_hover_update_ok,
        1.0f,
        busy,
        s,
        busy ? progress : -1.0f
    );

    if (!platform->mouse_pressed) {
        return 1;
    }
    if (over_close || over_cancel) {
        if (platform->update_cancel) {
            platform->update_cancel();
        }
        g_update_open = 0;
        return 1;
    }
    if (over_ok && !busy && platform->update_begin) {
        platform->update_begin();
    }
    return 1;
}

int
titlebar_wants_mouse(Platform *platform)
{
    TitlebarLayout L;
    layout(platform, &L);
    int mx = platform->mouse_x;
    int my = platform->mouse_y;
    if (g_open || g_menu > 0.02f || g_update_open) {
        return 1;
    }
    if (update_ready(platform) && hit(mx, my, L.update_x, L.y, L.update_w, L.btn)) {
        return 1;
    }
    return hit(mx, my, L.close_x, L.y, L.btn, L.btn) ||
        hit(mx, my, L.min_x, L.y, L.btn, L.btn) ||
        hit(mx, my, L.avatar_x, L.y, L.btn, L.btn);
}

void
titlebar_tick(Platform *platform, float dt)
{
    TitlebarLayout L;
    layout(platform, &L);

    int modal_open = login_modal_visible() || settings_modal_visible() || g_update_open;
    int show_update = update_ready(platform);
    int mx = platform->mouse_x;
    int my = platform->mouse_y;
    int over_close = hit(mx, my, L.close_x, L.y, L.btn, L.btn);
    int over_min = hit(mx, my, L.min_x, L.y, L.btn, L.btn);
    int over_update = show_update && !login_modal_visible() && !settings_modal_visible() &&
        hit(mx, my, L.update_x, L.y, L.update_w, L.btn);
    int over_avatar = !modal_open && hit(mx, my, L.avatar_x, L.y, L.btn, L.btn);
    int over_menu = !modal_open && g_open && hit(mx, my, L.menu_x, L.menu_y, L.menu_w, L.menu_h);
    int over_settings = !modal_open && g_open && hit(mx, my, L.item_x, L.settings_y, L.item_w, L.item_h);
    int over_item = !modal_open && g_open && hit(mx, my, L.item_x, L.item_y, L.item_w, L.item_h);
    int over_copy = !modal_open && g_open && hit(mx, my, L.copy_x, L.copy_y, L.copy_w, L.copy_h);

    g_hover_close = approach(g_hover_close, over_close ? 1.0f : 0.0f, dt);
    g_hover_min = approach(g_hover_min, over_min ? 1.0f : 0.0f, dt);
    if (over_update || g_update_open) {
        g_hover_update = approach(g_hover_update, 1.0f, dt);
    } else {
        g_hover_update = 0.0f;
    }
    g_hover_tip = over_update && !g_update_open ? 1.0f : 0.0f;
    g_hover_avatar = approach(g_hover_avatar, (over_avatar || g_open) ? 1.0f : 0.0f, dt);
    g_hover_signin = approach(g_hover_signin, over_item ? 1.0f : 0.0f, dt);
    g_hover_settings = approach(g_hover_settings, over_settings ? 1.0f : 0.0f, dt);
    g_hover_copy = approach(g_hover_copy, over_copy ? 1.0f : 0.0f, dt);
    if (g_copied_ms && (os_tick_ms() - g_copied_ms) > 1400u) {
        g_copied_ms = 0;
    }
    user_id_get();
    g_menu = g_open ? 1.0f : 0.0f;

    control_button(
        platform,
        (float)L.min_x,
        (float)L.y,
        (float)L.btn,
        g_hover_min,
        platform->mouse_down && over_min,
        0,
        ICON_MINUS
    );
    control_button(
        platform,
        (float)L.close_x,
        (float)L.y,
        (float)L.btn,
        g_hover_close,
        platform->mouse_down && over_close,
        1,
        ICON_X
    );
    if (show_update) {
        update_chip(platform, &L, g_hover_update, platform->mouse_down && over_update);
        update_tooltip(platform, &L, g_hover_tip);
    }
    avatar_button(platform, &L, g_hover_avatar, platform->mouse_down && over_avatar);
    account_menu(
        platform,
        &L,
        g_menu,
        g_hover_settings,
        g_hover_signin,
        platform->mouse_down && over_settings,
        platform->mouse_down && over_item,
        g_menu > 0.02f
    );

    const ThemeChrome *chrome = theme_chrome();
    platform->draw_label(
        L.pad + px(4, platform->dpi_scale),
        px(16, platform->dpi_scale),
        chrome->title,
        chrome->title_color,
        px(11, platform->dpi_scale),
        600,
        px(3, platform->dpi_scale)
    );

    if (g_update_open) {
        update_modal_tick(platform, dt);
        return;
    }

    if (platform->mouse_pressed) {
        if (over_close) {
            g_open = 0;
            platform->close();
        } else if (over_min) {
            g_open = 0;
            platform->minimize();
        } else if (over_update) {
            g_open = 0;
            g_update_open = 1;
        } else if (over_avatar) {
            g_open = !g_open;
        } else if (over_copy) {
            if (user_id_copy()) {
                g_copied_ms = os_tick_ms();
                if (g_copied_ms == 0) {
                    g_copied_ms = 1;
                }
            }
        } else if (over_settings) {
            g_open = 0;
            settings_modal_open();
            platform->mouse_pressed = 0;
        } else if (over_item) {
            g_open = 0;
            if (login_modal_signed_in()) {
                login_modal_sign_out();
                if (platform->steam_sign_out) {
                    platform->steam_sign_out();
                }
                if (platform->log) {
                    platform->log("account: signed out");
                }
            } else {
                login_modal_open();
                platform->mouse_pressed = 0;
            }
        } else if (!over_menu) {
            g_open = 0;
        }
    }
}
