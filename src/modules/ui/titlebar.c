#include "titlebar.h"
#include "login_modal.h"
#include "settings_modal.h"
#include "modal_skin.h"
#include "shared/chrome.h"
#include "shared/icons.h"
#include "shared/theme.h"
#include "shared/os.h"

#include <math.h>

#define ACCOUNT_MENU_W 248.0f
#define ACCOUNT_INSET 12.0f
#define ACCOUNT_ICON 32.0f
#define ACCOUNT_TEXT_GAP 12.0f
#define ACCOUNT_IDENTITY_H 40.0f
#define ACCOUNT_DLC_H 36.0f
#define ACCOUNT_PILL_H 34.0f
#define ACCOUNT_ROW_GAP 10.0f
#define ACCOUNT_DIVIDER_GAP 10.0f

typedef struct TitlebarLayout {
    int btn;
    int gap;
    int pad;
    int bar;
    int y;
    int close_x;
    int min_x;
    int avatar_x;
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
    int dlc_y;
    int dlc_h;
    int divider_y;
    int settings_y;
    int icon_s;
    int text_x;
} TitlebarLayout;

static float g_hover_min;
static float g_hover_close;
static float g_hover_avatar;
static float g_hover_signin;
static float g_hover_settings;
static float g_menu;
static int g_open;

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

    int inset = px(ACCOUNT_INSET, s);
    int div = px(ACCOUNT_DIVIDER_GAP, s);
    out->icon_s = px(ACCOUNT_ICON, s);
    out->ident_h = px(ACCOUNT_IDENTITY_H, s);
    out->dlc_h = login_modal_signed_in() ? px(ACCOUNT_DLC_H, s) : 0;
    out->item_h = px(ACCOUNT_PILL_H, s);
    out->menu_w = px(ACCOUNT_MENU_W, s);
    out->menu_h = inset + out->ident_h + (out->dlc_h ? div + out->dlc_h : 0) + div + 1 + div +
        out->item_h + px(ACCOUNT_ROW_GAP, s) + out->item_h + inset;
    out->menu_x = out->avatar_x + out->btn - out->menu_w;
    if (out->menu_x < out->pad) {
        out->menu_x = out->pad;
    }
    out->menu_y = out->bar + px(8, s);
    out->ident_x = out->menu_x + inset;
    out->ident_y = out->menu_y + inset;
    out->dlc_y = out->ident_y + out->ident_h + (out->dlc_h ? div : 0);
    out->divider_y = out->dlc_y + out->dlc_h + div;
    out->item_x = out->menu_x + inset;
    out->settings_y = out->divider_y + 1 + div;
    out->item_y = out->settings_y + out->item_h + px(ACCOUNT_ROW_GAP, s);
    out->item_w = out->menu_w - inset * 2;
    out->text_x = out->ident_x + out->icon_s + px(ACCOUNT_TEXT_GAP, s);
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
    float pill_r = (float)L->item_h * 0.28f;
    float icon_s = (float)L->icon_s;
    float icon_cx = (float)L->ident_x + icon_s * 0.5f;
    float text_x = (float)L->text_x;
    float text_w = (float)L->menu_x + (float)L->menu_w - text_x - (float)px(ACCOUNT_INSET, s);
    uint32_t fg = use_hover > 0.2f ? chrome->hover : chrome->title_color;

    icon_round_rect(
        platform->hdc,
        (float)L->item_x,
        y,
        (float)L->item_w,
        (float)L->item_h,
        pill_r,
        modal_button_fill(use_hover),
        (int)(open * 255.0f)
    );
    icon_round_stroke(
        platform->hdc,
        (float)L->item_x,
        y,
        (float)L->item_w,
        (float)L->item_h,
        pill_r,
        use_hover > 0.2f ? chrome->hover : chrome->muted,
        (int)(open * (28.0f + use_hover * 24.0f)),
        1.0f
    );
    draw_row_icon(platform->hdc, icon, icon_cx, y + (float)L->item_h * 0.5f, icon_s, fg);
    icon_draw_label_alpha(
        platform->hdc,
        text_x,
        y,
        text_w,
        (float)L->item_h,
        label,
        fg,
        (float)px(13, s),
        600,
        (int)(open * 255.0f)
    );
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

    modal_draw_card(platform->hdc, x, y, w, h, open, s);

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
    float text_w = x + w - text_x - (float)px(ACCOUNT_INSET, s);
    float name_h = (float)L->ident_h * 0.55f;
    icon_draw_label_alpha(
        platform->hdc,
        text_x,
        ident_y + 2.0f,
        text_w,
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
        text_w,
        (float)L->ident_h - name_h,
        signed_in ? L"Signed in" : L"Not signed in",
        chrome->muted,
        (float)px(11, s),
        400,
        (int)(open * 255.0f)
    );

    if (signed_in && L->dlc_h > 0) {
        float dlc_y = (float)L->dlc_y;
        float line_h = (float)L->dlc_h * 0.5f;
        int forsaken = platform->steam_owns_forsaken ? platform->steam_owns_forsaken() : -1;
        int shadowkeep = platform->steam_owns_shadowkeep ? platform->steam_owns_shadowkeep() : -1;
        icon_draw_label_alpha(
            platform->hdc,
            ident_x,
            dlc_y,
            (float)L->item_w,
            line_h,
            forsaken > 0 ? L"Forsaken Pack  ·  Owned" :
                forsaken == 0 ? L"Forsaken Pack  ·  Not owned" : L"Forsaken Pack  ·  Hidden",
            chrome->muted,
            (float)px(11, s),
            400,
            (int)(open * 255.0f)
        );
        icon_draw_label_alpha(
            platform->hdc,
            ident_x,
            dlc_y + line_h,
            (float)L->item_w,
            line_h,
            shadowkeep > 0 ? L"Shadowkeep Pack  ·  Owned" :
                shadowkeep == 0 ? L"Shadowkeep Pack  ·  Not owned" : L"Shadowkeep Pack  ·  Hidden",
            chrome->muted,
            (float)px(11, s),
            400,
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
titlebar_wants_mouse(Platform *platform)
{
    TitlebarLayout L;
    layout(platform, &L);
    int mx = platform->mouse_x;
    int my = platform->mouse_y;
    if (g_open || g_menu > 0.02f) {
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

    int modal_open = login_modal_visible() || settings_modal_visible();
    int mx = platform->mouse_x;
    int my = platform->mouse_y;
    int over_close = hit(mx, my, L.close_x, L.y, L.btn, L.btn);
    int over_min = hit(mx, my, L.min_x, L.y, L.btn, L.btn);
    int over_avatar = !modal_open && hit(mx, my, L.avatar_x, L.y, L.btn, L.btn);
    int over_menu = !modal_open && g_open && hit(mx, my, L.menu_x, L.menu_y, L.menu_w, L.menu_h);
    int over_settings = !modal_open && g_open && hit(mx, my, L.item_x, L.settings_y, L.item_w, L.item_h);
    int over_item = !modal_open && g_open && hit(mx, my, L.item_x, L.item_y, L.item_w, L.item_h);

    g_hover_close = approach(g_hover_close, over_close ? 1.0f : 0.0f, dt);
    g_hover_min = approach(g_hover_min, over_min ? 1.0f : 0.0f, dt);
    g_hover_avatar = approach(g_hover_avatar, (over_avatar || g_open) ? 1.0f : 0.0f, dt);
    g_hover_signin = approach(g_hover_signin, over_item ? 1.0f : 0.0f, dt);
    g_hover_settings = approach(g_hover_settings, over_settings ? 1.0f : 0.0f, dt);
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

    if (platform->mouse_pressed) {
        if (over_close) {
            g_open = 0;
            platform->close();
        } else if (over_min) {
            g_open = 0;
            platform->minimize();
        } else if (over_avatar) {
            g_open = !g_open;
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
