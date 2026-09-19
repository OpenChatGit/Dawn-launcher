#include "settings_modal.h"
#include "login_modal.h"
#include "modal_skin.h"
#include "shared/chrome.h"
#include "shared/icons.h"
#include "shared/os.h"
#include "shared/theme.h"

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
    float lang_x;
    float lang_y;
    float lang_w;
    float lang_h;
    float menu_x;
    float menu_y;
    float menu_w;
    float menu_h;
    float item_h;
    float track_x;
    float track_y;
    float track_w;
    float track_h;
    float thumb_y;
    float thumb_h;
    float ok_x;
    float ok_y;
    float ok_w;
    float ok_h;
    float status_x;
    float status_y;
    float status_w;
    float status_h;
} SettingsLayout;

static int g_open;
static int g_block_mouse;
static int g_arm_download;
static int g_lang_open;
static int g_lang_scroll;
static int g_lang_drag;
static int g_lang_drag_scroll;
static float g_lang_drag_y;
static float g_anim;
static float g_hover_close;
static float g_hover_browse;
static float g_hover_lang;
static float g_hover_ok;
static float g_hover_item;
static int g_over_item;
static wchar_t g_status_w[160];

static const char *k_lang_steam[] = {
    "english",
    "french",
    "german",
    "italian",
    "japanese",
    "brazilian",
    "spanish",
    "russian",
    "polish",
    "schinese",
    "tchinese",
    "latam",
    "koreana"
};

static const wchar_t *k_lang_label[] = {
    L"English",
    L"French",
    L"German",
    L"Italian",
    L"Japanese",
    L"Portuguese (Brazil)",
    L"Spanish",
    L"Russian",
    L"Polish",
    L"Chinese (Simplified)",
    L"Chinese (Traditional)",
    L"Spanish (Latam)",
    L"Korean"
};

#define LANG_OPTION_COUNT ((int)(sizeof(k_lang_steam) / sizeof(k_lang_steam[0])))
#define LANG_VISIBLE 5

static void
layout_modal(Platform *platform, SettingsLayout *out)
{
    float s = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float inset = (float)modal_px(MODAL_INSET, s);
    float gap = (float)modal_px(12, s);
    float inner = (float)modal_px(6, s);
    float browse_px = (float)modal_px(12, s);
    float tw = 0.0f;
    float title_y;
    float title_h;

    out->overlay_w = (float)platform->width;
    out->overlay_h = (float)platform->height;
    out->w = (float)modal_px(360, s);
    if (out->w > (float)platform->width - 32.0f) {
        out->w = (float)platform->width - 32.0f;
    }
    out->h = (float)modal_px(244, s);
    out->x = ((float)platform->width - out->w) * 0.5f;
    out->y = ((float)platform->height - out->h) * 0.5f;
    out->close_s = (float)modal_px(MODAL_CLOSE, s);
    title_y = out->y + (float)modal_px(16, s);
    title_h = (float)modal_px(20, s);
    out->close_x = out->x + out->w - inset - out->close_s;
    out->close_y = title_y + (title_h - out->close_s) * 0.5f;
    out->path_h = (float)modal_px(MODAL_FIELD_H, s);
    out->path_x = out->x + inset;
    out->path_y = out->y + (float)modal_px(78, s);
    out->path_w = out->w - inset * 2.0f;
    if (platform->hdc) {
        static float browse_cache_px;
        static float browse_cache_w;
        if (browse_cache_w > 0.0f && browse_cache_px == browse_px) {
            tw = browse_cache_w;
        } else {
            tw = icon_measure_label(platform->hdc, L"Browse", browse_px, 600);
            browse_cache_px = browse_px;
            browse_cache_w = tw;
        }
    }
    out->browse_h = out->path_h - inner * 2.0f;
    out->browse_w = tw + (float)modal_px(20, s);
    if (out->browse_w < (float)modal_px(58, s)) {
        out->browse_w = (float)modal_px(58, s);
    }
    if (out->browse_w > out->path_w * 0.38f) {
        out->browse_w = out->path_w * 0.38f;
    }
    out->browse_x = out->path_x + out->path_w - inner - out->browse_w;
    out->browse_y = out->path_y + (out->path_h - out->browse_h) * 0.5f;
    out->lang_x = out->path_x;
    out->lang_y = out->path_y + out->path_h + gap;
    out->lang_w = out->path_w;
    out->lang_h = out->path_h;
    out->item_h = (float)modal_px(26, s);
    out->menu_w = out->lang_w;
    out->menu_h = out->item_h * (float)LANG_VISIBLE + (float)modal_px(10, s);
    out->menu_x = out->lang_x;
    out->menu_y = out->lang_y - (float)modal_px(6, s) - out->menu_h;
    {
        float floor_y = (float)CHROME_TITLEBAR * s + 8.0f;
        if (out->menu_y < floor_y) {
            out->menu_y = floor_y;
        }
    }
    out->track_w = (float)modal_px(4, s);
    out->track_x = out->menu_x + out->menu_w - (float)modal_px(11, s);
    out->track_y = out->menu_y + (float)modal_px(8, s);
    out->track_h = out->menu_h - (float)modal_px(16, s);
    out->thumb_h = out->track_h * (float)LANG_VISIBLE / (float)LANG_OPTION_COUNT;
    if (out->thumb_h < (float)modal_px(16, s)) {
        out->thumb_h = (float)modal_px(16, s);
    }
    if (out->thumb_h > out->track_h) {
        out->thumb_h = out->track_h;
    }
    {
        int max = LANG_OPTION_COUNT - LANG_VISIBLE;
        float travel = out->track_h - out->thumb_h;
        if (max < 1) {
            max = 1;
        }
        if (g_lang_scroll < 0) {
            g_lang_scroll = 0;
        }
        if (g_lang_scroll > LANG_OPTION_COUNT - LANG_VISIBLE) {
            g_lang_scroll = LANG_OPTION_COUNT - LANG_VISIBLE;
        }
        out->thumb_y = out->track_y + travel * (float)g_lang_scroll / (float)max;
    }
    out->ok_w = (float)modal_px(76, s);
    out->ok_h = (float)modal_px(32, s);
    out->ok_x = out->path_x + out->path_w - out->ok_w;
    out->ok_y = out->lang_y + out->lang_h + gap;
    out->status_x = out->path_x;
    out->status_y = out->ok_y;
    out->status_w = out->ok_x - out->status_x - (float)modal_px(12, s);
    out->status_h = out->ok_h;
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

static int
has_dir(Platform *platform)
{
    const char *dir = current_dir(platform);
    return dir && dir[0];
}

static int
modal_alpha_local(float anim)
{
    int alpha = (int)(anim * 255.0f + 0.5f);
    if (alpha < 0) {
        return 0;
    }
    if (alpha > 255) {
        return 255;
    }
    return alpha;
}

static void
draw_path_field(
    Platform *platform,
    const SettingsLayout *L,
    const wchar_t *path,
    float hover_browse,
    int busy,
    float anim,
    float scale
)
{
    const ThemeChrome *chrome = theme_chrome();
    float radius = L->path_h * 0.30f;
    float pad = (float)modal_px(14, scale);
    float px = (float)modal_px(13, scale);
    float chip_r = L->browse_h * 0.5f;
    float text_w;
    int alpha = (int)(anim * 255.0f);
    uint32_t field = modal_field_color();
    uint32_t chip = modal_mix(field, 0xffffff, 18 + (int)(hover_browse * 36.0f));
    uint32_t stroke = hover_browse > 0.2f ? chrome->hover : chrome->muted;
    uint32_t fg = (path && path[0] && wcscmp(path, L"Not set") != 0) ? chrome->title_color : chrome->muted;
    uint32_t chip_fg = busy ? chrome->muted : (hover_browse > 0.2f ? chrome->hover : chrome->title_color);

    icon_round_rect(platform->hdc, L->path_x, L->path_y, L->path_w, L->path_h, radius, field, alpha);
    icon_round_stroke(
        platform->hdc,
        L->path_x,
        L->path_y,
        L->path_w,
        L->path_h,
        radius,
        stroke,
        (int)(anim * (hover_browse > 0.2f ? 70.0f : 34.0f)),
        hover_browse > 0.2f ? 1.2f : 1.0f
    );
    text_w = L->browse_x - L->path_x - pad - (float)modal_px(6, scale);
    if (text_w < 8.0f) {
        text_w = 8.0f;
    }
    icon_draw_label_alpha(
        platform->hdc,
        L->path_x + pad,
        L->path_y,
        text_w,
        L->path_h,
        path,
        fg,
        px,
        400,
        modal_alpha_local(anim)
    );
    icon_round_rect(
        platform->hdc,
        L->browse_x,
        L->browse_y,
        L->browse_w,
        L->browse_h,
        chip_r,
        chip,
        (int)(anim * (busy ? 140.0f : 255.0f))
    );
    icon_draw_label_center_alpha(
        platform->hdc,
        L->browse_x,
        L->browse_y,
        L->browse_w,
        L->browse_h,
        L"Browse",
        chip_fg,
        (float)modal_px(12, scale),
        600,
        (int)(anim * (busy ? 140.0f : 255.0f))
    );
}

static const wchar_t *
current_lang_label(Platform *platform)
{
    const char *steam = platform->install_language ? platform->install_language() : "";
    int i;

    for (i = 0; i < LANG_OPTION_COUNT; i++) {
        if (steam && os_stricmp(steam, k_lang_steam[i]) == 0) {
            return k_lang_label[i];
        }
    }
    if (platform->install_language_label) {
        static wchar_t wide[48];
        os_utf8_to_wide(platform->install_language_label(), wide, 48);
        if (wide[0]) {
            return wide;
        }
    }
    return L"English";
}

static int
lang_scroll_max(void)
{
    int max = LANG_OPTION_COUNT - LANG_VISIBLE;
    return max < 0 ? 0 : max;
}

static void
clamp_lang_scroll(void)
{
    int max = lang_scroll_max();
    if (g_lang_scroll < 0) {
        g_lang_scroll = 0;
    }
    if (g_lang_scroll > max) {
        g_lang_scroll = max;
    }
}

static void
reveal_lang(int index)
{
    if (index < g_lang_scroll) {
        g_lang_scroll = index;
    } else if (index >= g_lang_scroll + LANG_VISIBLE) {
        g_lang_scroll = index - LANG_VISIBLE + 1;
    }
    clamp_lang_scroll();
}

static int
over_lang_track(const SettingsLayout *L, int mx, int my)
{
    return modal_hit(
        mx,
        my,
        L->track_x - 4.0f,
        L->track_y,
        L->track_w + 8.0f,
        L->track_h
    );
}

static int
over_lang_thumb(const SettingsLayout *L, int mx, int my)
{
    return modal_hit(
        mx,
        my,
        L->track_x - 4.0f,
        L->thumb_y,
        L->track_w + 8.0f,
        L->thumb_h
    );
}

static int
lang_item_at(const SettingsLayout *L, int mx, int my)
{
    int i;
    float y = L->menu_y + 5.0f;
    float item_w = L->track_x - L->menu_x - 4.0f;

    if (!modal_hit(mx, my, L->menu_x, L->menu_y, L->menu_w, L->menu_h)) {
        return -1;
    }
    if (over_lang_track(L, mx, my)) {
        return -1;
    }
    if (item_w < 24.0f) {
        item_w = L->menu_w - 16.0f;
    }
    for (i = 0; i < LANG_VISIBLE; i++) {
        int idx = g_lang_scroll + i;
        if (idx >= LANG_OPTION_COUNT) {
            break;
        }
        if (modal_hit(mx, my, L->menu_x, y, item_w, L->item_h)) {
            return idx;
        }
        y += L->item_h;
    }
    return -1;
}

static void
draw_lang_field(
    Platform *platform,
    const SettingsLayout *L,
    const wchar_t *label,
    float hover,
    int open,
    int busy,
    float anim,
    float scale
)
{
    const ThemeChrome *chrome = theme_chrome();
    float radius = L->lang_h * 0.30f;
    float pad = (float)modal_px(14, scale);
    float px = (float)modal_px(13, scale);
    int alpha = (int)(anim * 255.0f);
    uint32_t field = modal_field_color();
    uint32_t stroke = (hover > 0.2f || open) ? chrome->hover : chrome->muted;
    uint32_t fg = busy ? chrome->muted : chrome->title_color;
    float chev = (float)modal_px(16, scale);

    icon_round_rect(platform->hdc, L->lang_x, L->lang_y, L->lang_w, L->lang_h, radius, field, alpha);
    icon_round_stroke(
        platform->hdc,
        L->lang_x,
        L->lang_y,
        L->lang_w,
        L->lang_h,
        radius,
        stroke,
        (int)(anim * ((hover > 0.2f || open) ? 70.0f : 34.0f)),
        (hover > 0.2f || open) ? 1.2f : 1.0f
    );
    icon_draw_label_alpha(
        platform->hdc,
        L->lang_x + pad,
        L->lang_y,
        L->lang_w - pad * 2.0f - chev,
        L->lang_h,
        label,
        fg,
        px,
        400,
        modal_alpha_local(anim)
    );
    icon_draw(
        platform->hdc,
        open ? ICON_CHEVRON_DOWN : ICON_CHEVRON_UP,
        L->lang_x + L->lang_w - pad - chev * 0.5f,
        L->lang_y + L->lang_h * 0.5f,
        chev,
        fg,
        0.0f
    );
}

static void
draw_lang_menu(Platform *platform, const SettingsLayout *L, int selected, int over, float anim, float scale)
{
    const ThemeChrome *chrome = theme_chrome();
    float y = L->menu_y + 5.0f;
    float inset = (float)modal_px(10, scale);
    float text_w = L->track_x - L->menu_x - inset - 4.0f;
    int i;
    int alpha = modal_alpha_local(anim);

    modal_draw_card(platform->hdc, L->menu_x, L->menu_y, L->menu_w, L->menu_h, anim, scale);
    if (text_w < 24.0f) {
        text_w = L->menu_w - inset * 2.0f;
    }
    for (i = 0; i < LANG_VISIBLE; i++) {
        int idx = g_lang_scroll + i;
        uint32_t fg;
        if (idx >= LANG_OPTION_COUNT) {
            break;
        }
        fg = (idx == selected) ? chrome->title_color : chrome->muted;
        if (idx == over) {
            icon_round_rect(
                platform->hdc,
                L->menu_x + 4.0f,
                y,
                L->track_x - L->menu_x - 8.0f,
                L->item_h,
                6.0f,
                modal_mix(modal_field_color(), 0xffffff, 22),
                alpha
            );
            fg = chrome->title_color;
        }
        icon_draw_label_alpha(
            platform->hdc,
            L->menu_x + inset,
            y,
            text_w,
            L->item_h,
            k_lang_label[idx],
            fg,
            (float)modal_px(12, scale),
            idx == selected ? 600 : 400,
            alpha
        );
        y += L->item_h;
    }
    icon_round_rect(
        platform->hdc,
        L->track_x,
        L->track_y,
        L->track_w,
        L->track_h,
        L->track_w * 0.5f,
        modal_mix(modal_field_color(), chrome->muted, 70),
        (int)(anim * 90.0f)
    );
    icon_round_rect(
        platform->hdc,
        L->track_x,
        L->thumb_y,
        L->track_w,
        L->thumb_h,
        L->track_w * 0.5f,
        modal_mix(chrome->title_color, 0xffffff, 40),
        (int)(anim * 200.0f)
    );
}

static void
draw_ok_pill(
    Platform *platform,
    const SettingsLayout *L,
    float hover,
    int disabled,
    float anim,
    float scale
)
{
    float radius = L->ok_h * 0.5f;
    float use = disabled ? 0.0f : hover;
    int alpha = (int)(anim * (disabled ? 150.0f : 255.0f));
    uint32_t fill = modal_mix(0xffffff, 0xf3f5f8, (int)(use * 255.0f));
    uint32_t fg = disabled ? 0x6b7280 : 0x111827;

    icon_round_rect(platform->hdc, L->ok_x, L->ok_y + 1.5f, L->ok_w, L->ok_h, radius, 0x000000, alpha * 28 / 255);
    icon_round_rect(platform->hdc, L->ok_x, L->ok_y, L->ok_w, L->ok_h, radius, fill, alpha);
    icon_draw_label_center_alpha(
        platform->hdc,
        L->ok_x,
        L->ok_y,
        L->ok_w,
        L->ok_h,
        L"OK",
        fg,
        (float)modal_px(13, scale),
        600,
        alpha
    );
}

static void
confirm_ok(Platform *platform)
{
    int start = g_arm_download;

    if (is_busy(platform)) {
        return;
    }
    if (start && !has_dir(platform)) {
        return;
    }
    g_arm_download = 0;
    settings_modal_close();
    if (start && platform->install_start) {
        platform->install_start();
    }
}

void
settings_modal_open(void)
{
    login_modal_hide();
    g_open = 1;
    g_block_mouse = 1;
    g_arm_download = 0;
}

void
settings_modal_open_for_download(void)
{
    login_modal_hide();
    g_open = 1;
    g_block_mouse = 1;
    g_arm_download = 1;
}

void
settings_modal_close(void)
{
    g_open = 0;
    g_anim = 0.0f;
    g_block_mouse = 0;
    g_arm_download = 0;
    g_lang_open = 0;
    g_lang_drag = 0;
}

void
settings_modal_hide(void)
{
    g_open = 0;
    g_anim = 0.0f;
    g_block_mouse = 0;
    g_arm_download = 0;
    g_lang_open = 0;
    g_lang_scroll = 0;
    g_lang_drag = 0;
    g_hover_close = 0.0f;
    g_hover_browse = 0.0f;
    g_hover_lang = 0.0f;
    g_hover_ok = 0.0f;
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
    int over_lang;
    int over_ok;
    int over_card;
    int over_menu;
    int busy;
    int can_ok;
    int item;
    int selected;
    wchar_t path_w[96];
    const wchar_t *lang_w;

    if (!platform) {
        return;
    }
    g_anim = g_open ? 1.0f : 0.0f;
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
    can_ok = !busy && (!g_arm_download || has_dir(platform));
    item = (g_open && g_lang_open) ? lang_item_at(&L, mx, my) : -1;
    over_close = modal_hit(mx, my, L.close_x, L.close_y, L.close_s, L.close_s);
    over_browse = !busy && modal_hit(mx, my, L.path_x, L.path_y, L.path_w, L.path_h);
    over_lang = !busy && modal_hit(mx, my, L.lang_x, L.lang_y, L.lang_w, L.lang_h);
    over_menu = g_lang_open && modal_hit(mx, my, L.menu_x, L.menu_y, L.menu_w, L.menu_h);
    if (g_lang_open && g_open && platform->mouse_wheel && (over_menu || over_lang)) {
        g_lang_scroll -= platform->mouse_wheel;
        clamp_lang_scroll();
        platform->mouse_wheel = 0;
        layout_modal(platform, &L);
        item = lang_item_at(&L, mx, my);
        over_menu = modal_hit(mx, my, L.menu_x, L.menu_y, L.menu_w, L.menu_h);
    }
    if (g_lang_drag) {
        if (!platform->mouse_down) {
            g_lang_drag = 0;
        } else {
            float travel = L.track_h - L.thumb_h;
            int max = lang_scroll_max();
            if (travel > 1.0f && max > 0) {
                float dy = (float)my - g_lang_drag_y;
                g_lang_scroll = g_lang_drag_scroll + (int)(dy * (float)max / travel + (dy >= 0.0f ? 0.5f : -0.5f));
                clamp_lang_scroll();
                layout_modal(platform, &L);
                item = lang_item_at(&L, mx, my);
            }
        }
    }
    over_ok = can_ok && !g_lang_open && modal_hit(mx, my, L.ok_x, L.ok_y, L.ok_w, L.ok_h);
    over_card = modal_hit(mx, my, x, y, L.w, L.h) || over_menu;
    g_hover_close = modal_approach(g_hover_close, (g_open && over_close) ? 1.0f : 0.0f, dt);
    g_hover_browse = modal_approach(g_hover_browse, (g_open && over_browse) ? 1.0f : 0.0f, dt);
    g_hover_lang = modal_approach(g_hover_lang, (g_open && (over_lang || g_lang_open)) ? 1.0f : 0.0f, dt);
    g_hover_ok = modal_approach(g_hover_ok, (g_open && over_ok) ? 1.0f : 0.0f, dt);
    g_over_item = item;
    g_hover_item = modal_approach(g_hover_item, item >= 0 ? 1.0f : 0.0f, dt);

    modal_draw_overlay(platform->hdc, L.overlay_w, L.overlay_h, g_anim);
    inset = (float)modal_px(MODAL_INSET, s);
    modal_draw_card(platform->hdc, x, y, L.w, L.h, g_anim, s);
    modal_draw_title(
        platform->hdc,
        x + inset,
        y + (float)modal_px(16, s),
        L.close_x - x - inset - (float)modal_px(8, s),
        (float)modal_px(20, s),
        g_arm_download ? L"Install folder" : L"Settings",
        s,
        g_anim
    );
    modal_draw_subtitle(
        platform->hdc,
        x + inset,
        y + (float)modal_px(40, s),
        L.w - inset * 2.0f,
        (float)modal_px(32, s),
        g_arm_download ? L"Choose folder and language, then press OK." : L"Install folder and game language.",
        s,
        g_anim
    );
    modal_draw_close(platform->hdc, L.close_x, L.close_y, L.close_s, g_hover_close, g_anim);

    {
        int max_chars = (int)((L.browse_x - L.path_x - (float)modal_px(20, s)) /
            ((float)modal_px(13, s) * 0.55f));
        if (max_chars < 12) {
            max_chars = 12;
        }
        if (max_chars > 95) {
            max_chars = 95;
        }
        path_label(current_dir(platform), path_w, max_chars);
    }
    draw_path_field(platform, &L, path_w, g_hover_browse, busy, g_anim, s);
    lang_w = current_lang_label(platform);
    draw_lang_field(platform, &L, lang_w, g_hover_lang, g_lang_open, busy, g_anim, s);
    selected = 0;
    {
        const char *cur = platform->install_language ? platform->install_language() : "";
        int i;
        for (i = 0; i < LANG_OPTION_COUNT; i++) {
            if (cur && os_stricmp(cur, k_lang_steam[i]) == 0) {
                selected = i;
                break;
            }
        }
    }
    if (g_lang_open && g_anim > 0.02f) {
        draw_lang_menu(platform, &L, selected, item, g_anim, s);
    }
    draw_ok_pill(platform, &L, g_hover_ok, !can_ok, g_anim, s);

    if (g_open) {
        if (busy) {
            wcscpy(g_status_w, L"Folder is locked while downloading.");
        } else if (g_arm_download && !has_dir(platform)) {
            wcscpy(g_status_w, L"Browse to a folder first.");
        } else if (g_arm_download) {
            wcscpy(g_status_w, L"OK starts the download here.");
        } else if (is_ready(platform)) {
            wcscpy(g_status_w, L"Dawn is installed in this folder.");
        } else {
            wcscpy(g_status_w, L"New downloads will go here.");
        }
    }
    icon_draw_label_alpha(
        platform->hdc,
        L.status_x,
        L.status_y,
        L.status_w > 0.0f ? L.status_w : L.path_w,
        L.status_h,
        g_status_w[0] ? g_status_w : L"New downloads will go here.",
        theme_chrome()->muted,
        (float)modal_px(MODAL_SUB_PX, s),
        400,
        modal_alpha_local(g_anim)
    );

    if (!g_open) {
        return;
    }
    if (platform->key == PLATFORM_KEY_ESCAPE) {
        if (g_lang_open) {
            g_lang_open = 0;
        } else {
            settings_modal_close();
        }
    } else if (platform->key == PLATFORM_KEY_ENTER && !g_lang_open) {
        confirm_ok(platform);
    }
    if (g_block_mouse) {
        if (!platform->mouse_down && !platform->mouse_pressed) {
            g_block_mouse = 0;
        }
        platform->mouse_pressed = 0;
        return;
    }
    if (platform->mouse_pressed) {
        if (g_lang_open && over_lang_thumb(&L, mx, my)) {
            g_lang_drag = 1;
            g_lang_drag_y = (float)my;
            g_lang_drag_scroll = g_lang_scroll;
        } else if (g_lang_open && over_lang_track(&L, mx, my)) {
            g_lang_scroll += (my < (int)L.thumb_y) ? -LANG_VISIBLE : LANG_VISIBLE;
            clamp_lang_scroll();
        } else if (g_lang_open && item >= 0) {
            if (platform->install_set_language) {
                platform->install_set_language(k_lang_steam[item]);
            }
            g_lang_open = 0;
            g_lang_drag = 0;
        } else if (over_lang) {
            g_lang_open = !g_lang_open;
            g_lang_drag = 0;
            if (g_lang_open) {
                const char *cur = platform->install_language ? platform->install_language() : "";
                int i;
                reveal_lang(0);
                for (i = 0; i < LANG_OPTION_COUNT; i++) {
                    if (cur && os_stricmp(cur, k_lang_steam[i]) == 0) {
                        reveal_lang(i);
                        break;
                    }
                }
            }
        } else if (g_lang_open) {
            g_lang_open = 0;
            g_lang_drag = 0;
        } else if (over_close || !over_card) {
            settings_modal_close();
        } else if (over_browse) {
            choose_folder(platform);
        } else if (over_ok) {
            confirm_ok(platform);
        }
        platform->mouse_pressed = 0;
    }
}
