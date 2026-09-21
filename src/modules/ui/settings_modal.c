#include "settings_modal.h"
#include "login_modal.h"
#include "modal_skin.h"
#include "i18n/i18n.h"
#include "shared/chrome.h"
#include "shared/icons.h"
#include "shared/os.h"
#include "shared/theme.h"
#include "shared/user_id.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

enum {
    SETTINGS_PAGE_USER = 0,
    SETTINGS_PAGE_GENERAL,
    SETTINGS_PAGE_INSTALL,
    SETTINGS_PAGE_COUNT
};

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
    float side_x;
    float side_y;
    float side_w;
    float side_h;
    float nav_x;
    float nav_y;
    float nav_w;
    float nav_h;
    float nav_gap;
    float content_x;
    float content_y;
    float content_w;
    float content_h;
    float title_x;
    float title_y;
    float title_w;
    float title_h;
    float path_x;
    float path_y;
    float path_w;
    float path_h;
    float browse_x;
    float browse_y;
    float browse_w;
    float browse_h;
    float exe_x;
    float exe_y;
    float exe_w;
    float exe_h;
    float exe_browse_x;
    float exe_browse_y;
    float exe_browse_w;
    float exe_browse_h;
    float app_lang_x;
    float app_lang_y;
    float app_lang_w;
    float app_lang_h;
    float app_menu_x;
    float app_menu_y;
    float app_menu_w;
    float app_menu_h;
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
    float avatar_x;
    float avatar_y;
    float avatar_s;
    float copy_x;
    float copy_y;
    float copy_w;
    float copy_h;
} SettingsLayout;

static int g_open;
static int g_block_mouse;
static int g_arm_download;
static int g_page;
static int g_ui_lang_open;
static int g_lang_open;
static int g_lang_scroll;
static int g_lang_drag;
static int g_lang_drag_scroll;
static float g_lang_drag_y;
static float g_anim;
static float g_hover_close;
static float g_hover_browse;
static float g_hover_exe;
static float g_hover_ui_lang;
static float g_hover_lang;
static float g_hover_ok;
static float g_hover_item;
static float g_hover_nav[SETTINGS_PAGE_COUNT];
static float g_hover_copy;
static int g_over_item;
static uint32_t g_copied_ms;
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

static const I18nId k_lang_i18n[] = {
    I18N_STEAM_ENGLISH,
    I18N_STEAM_FRENCH,
    I18N_STEAM_GERMAN,
    I18N_STEAM_ITALIAN,
    I18N_STEAM_JAPANESE,
    I18N_STEAM_BRAZILIAN,
    I18N_STEAM_SPANISH,
    I18N_STEAM_RUSSIAN,
    I18N_STEAM_POLISH,
    I18N_STEAM_SCHINESE,
    I18N_STEAM_TCHINESE,
    I18N_STEAM_LATAM,
    I18N_STEAM_KOREAN
};

static const I18nId k_page_i18n[] = {
    I18N_USER,
    I18N_GENERAL,
    I18N_INSTALLATION
};

static const IconId k_page_icon[] = {
    ICON_USER,
    ICON_GLOBE,
    ICON_DOWNLOAD
};

#define LANG_OPTION_COUNT ((int)(sizeof(k_lang_steam) / sizeof(k_lang_steam[0])))
#define LANG_VISIBLE 5

static void
layout_modal(Platform *platform, SettingsLayout *out)
{
    float s = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float inset = (float)modal_px(16, s);
    float gap = (float)modal_px(12, s);
    float inner = (float)modal_px(6, s);
    float browse_px = (float)modal_px(12, s);
    float tw = 0.0f;

    out->overlay_w = (float)platform->width;
    out->overlay_h = (float)platform->height;
    out->w = (float)modal_px(560, s);
    out->h = (float)modal_px(500, s);
    if (out->w > (float)platform->width - 32.0f) {
        out->w = (float)platform->width - 32.0f;
    }
    if (out->h > (float)platform->height - 32.0f) {
        out->h = (float)platform->height - 32.0f;
    }
    out->x = (float)((int)(((float)platform->width - out->w) * 0.5f + 0.5f));
    out->y = (float)((int)(((float)platform->height - out->h) * 0.5f + 0.5f));
    out->close_s = (float)modal_px(28, s);
    out->close_x = out->x + out->w - inset - out->close_s;
    out->close_y = out->y + inset;
    out->side_w = (float)modal_px(148, s);
    if (out->side_w > out->w * 0.36f) {
        out->side_w = out->w * 0.36f;
    }
    out->side_x = out->x;
    out->side_y = out->y;
    out->side_h = out->h;
    out->nav_x = out->side_x + (float)modal_px(10, s);
    out->nav_y = out->y + (float)modal_px(56, s);
    out->nav_w = out->side_w - (float)modal_px(20, s);
    out->nav_h = (float)modal_px(30, s);
    out->nav_gap = (float)modal_px(4, s);
    out->content_x = out->x + out->side_w + (float)modal_px(20, s);
    out->content_y = out->y + (float)modal_px(56, s);
    out->content_w = out->x + out->w - inset - out->content_x;
    out->content_h = out->y + out->h - inset - out->content_y;
    out->title_x = out->content_x;
    out->title_y = out->y + inset;
    out->title_w = out->close_x - out->title_x - (float)modal_px(8, s);
    out->title_h = (float)modal_px(20, s);

    out->path_h = (float)modal_px(36, s);
    out->path_x = out->content_x;
    out->path_y = out->content_y + (float)modal_px(36, s);
    out->path_w = out->content_w;
    if (platform->hdc) {
        static float browse_cache_px;
        static float browse_cache_w;
        static int browse_cache_lang = -1;
        if (browse_cache_w > 0.0f && browse_cache_px == browse_px && browse_cache_lang == (int)i18n_lang()) {
            tw = browse_cache_w;
        } else {
            tw = icon_measure_label(platform->hdc, i18n_t(I18N_BROWSE), browse_px, 600);
            browse_cache_px = browse_px;
            browse_cache_w = tw;
            browse_cache_lang = (int)i18n_lang();
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
    out->exe_h = out->path_h;
    out->exe_x = out->content_x;
    out->exe_y = out->path_y + out->path_h + (float)modal_px(38, s);
    out->exe_w = out->content_w;
    out->exe_browse_w = out->browse_w;
    out->exe_browse_h = out->browse_h;
    out->exe_browse_x = out->path_x + out->path_w - inner - out->exe_browse_w;
    out->exe_browse_y = out->exe_y + (out->exe_h - out->exe_browse_h) * 0.5f;

    out->item_h = (float)modal_px(26, s);
    out->app_lang_x = out->content_x;
    out->app_lang_y = out->content_y + (float)modal_px(22, s);
    out->app_lang_w = out->content_w;
    out->app_lang_h = out->path_h;
    out->app_menu_w = out->app_lang_w;
    out->app_menu_h = out->item_h * 2.0f + (float)modal_px(10, s);
    out->app_menu_x = out->app_lang_x;
    out->app_menu_y = out->app_lang_y + out->app_lang_h + (float)modal_px(4, s);
    out->lang_x = out->content_x;
    out->lang_y = out->app_lang_y + out->app_lang_h +
        (g_ui_lang_open ? out->app_menu_h + (float)modal_px(36, s) : (float)modal_px(56, s));
    out->lang_w = out->content_w;
    out->lang_h = out->path_h;
    out->menu_w = out->lang_w;
    out->menu_h = out->item_h * (float)LANG_VISIBLE + (float)modal_px(10, s);
    out->menu_x = out->lang_x;
    out->menu_y = out->lang_y + out->lang_h + (float)modal_px(4, s);
    if (out->menu_y + out->menu_h > out->y + out->h - inset) {
        out->menu_h = out->y + out->h - inset - out->menu_y;
        if (out->menu_h < out->item_h + (float)modal_px(10, s)) {
            out->menu_h = out->item_h + (float)modal_px(10, s);
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
    out->ok_x = out->content_x + out->content_w - out->ok_w;
    out->ok_y = out->y + out->h - inset - out->ok_h;
    out->status_x = out->content_x;
    out->status_y = out->exe_y + out->exe_h + gap;
    out->status_w = out->content_w;
    out->status_h = (float)modal_px(36, s);

    out->avatar_s = (float)modal_px(40, s);
    out->avatar_x = out->content_x;
    out->avatar_y = out->content_y + (float)modal_px(8, s);
    out->copy_h = (float)modal_px(28, s);
    if (platform->hdc) {
        tw = icon_measure_label(platform->hdc, i18n_t(I18N_COPY_ID), (float)modal_px(12, s), 600);
    } else {
        tw = 48.0f;
    }
    out->copy_w = tw + (float)modal_px(20, s);
    out->copy_x = out->content_x + out->content_w - out->copy_w;
    out->copy_y = out->avatar_y + (out->avatar_s - out->copy_h) * 0.5f;
    if (out->copy_y < out->avatar_y) {
        out->copy_y = out->avatar_y;
    }
}

static const char *
current_dir(Platform *platform)
{
    const char *dir = platform->install_dir ? platform->install_dir() : "";
    return dir ? dir : "";
}

static const char *
current_exe(Platform *platform)
{
    const char *exe = platform->install_exe ? platform->install_exe() : "";
    return exe ? exe : "";
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
        wcsncpy(out, i18n_t(I18N_NOT_SET), (size_t)max - 1);
        out[max - 1] = 0;
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

static void
choose_exe(Platform *platform)
{
    char picked[MAX_PATH];

    if (!platform->pick_file || is_busy(platform)) {
        return;
    }
    picked[0] = '\0';
    if (!platform->pick_file(picked, (int)sizeof(picked)) || picked[0] == '\0') {
        return;
    }
    if (platform->install_set_exe) {
        platform->install_set_exe(picked);
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

static float
nav_row_y(const SettingsLayout *L, int page)
{
    return L->nav_y + (float)page * (L->nav_h + L->nav_gap);
}

static int
nav_at(const SettingsLayout *L, int mx, int my)
{
    int i;
    for (i = 0; i < SETTINGS_PAGE_COUNT; i++) {
        if (modal_hit(mx, my, L->nav_x, nav_row_y(L, i), L->nav_w, L->nav_h)) {
            return i;
        }
    }
    return -1;
}

static void
draw_section_label(
    Platform *platform,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    float scale,
    float anim
)
{
    icon_draw_label_alpha(
        platform->hdc,
        x,
        y,
        w,
        h,
        text,
        theme_chrome()->muted,
        (float)modal_px(12, scale),
        600,
        modal_alpha_local(anim)
    );
}

static void
draw_path_row(
    Platform *platform,
    float x,
    float y,
    float w,
    float h,
    float bx,
    float by,
    float bw,
    float bh,
    const wchar_t *path,
    float hover_browse,
    int busy,
    float anim,
    float scale
)
{
    const ThemeChrome *chrome = theme_chrome();
    float radius = h * 0.28f;
    float pad = (float)modal_px(12, scale);
    float px = (float)modal_px(13, scale);
    float chip_r = bh * 0.36f;
    float text_w;
    int alpha = (int)(anim * 255.0f);
    uint32_t field = modal_field_color();
    uint32_t chip = modal_mix(field, 0xffffff, 18 + (int)(hover_browse * 36.0f));
    uint32_t stroke = hover_browse > 0.2f ? chrome->hover : chrome->muted;
    uint32_t fg = (path && path[0] && wcscmp(path, i18n_t(I18N_NOT_SET)) != 0) ? chrome->title_color : chrome->muted;
    uint32_t chip_fg = busy ? chrome->muted : (hover_browse > 0.2f ? chrome->hover : chrome->title_color);

    icon_round_rect(platform->hdc, x, y, w, h, radius, field, alpha);
    icon_round_stroke(
        platform->hdc,
        x,
        y,
        w,
        h,
        radius,
        stroke,
        (int)(anim * (hover_browse > 0.2f ? 70.0f : 28.0f)),
        1.0f
    );
    text_w = bx - x - pad - (float)modal_px(6, scale);
    if (text_w < 8.0f) {
        text_w = 8.0f;
    }
    icon_draw_label_alpha(
        platform->hdc,
        x + pad,
        y,
        text_w,
        h,
        path,
        fg,
        px,
        600,
        modal_alpha_local(anim)
    );
    icon_round_rect(
        platform->hdc,
        bx,
        by,
        bw,
        bh,
        chip_r,
        chip,
        (int)(anim * (busy ? 140.0f : 255.0f))
    );
    icon_draw_label_center_alpha(
        platform->hdc,
        bx,
        by,
        bw,
        bh,
        i18n_t(I18N_BROWSE),
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
            return i18n_t(k_lang_i18n[i]);
        }
    }
    if (platform->install_language_label) {
        static wchar_t wide[48];
        os_utf8_to_wide(platform->install_language_label(), wide, 48);
        if (wide[0]) {
            return wide;
        }
    }
    return i18n_t(I18N_STEAM_ENGLISH);
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
draw_select_field(
    Platform *platform,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *label,
    float hover,
    int open,
    int busy,
    float anim,
    float scale
)
{
    const ThemeChrome *chrome = theme_chrome();
    float radius = h * 0.28f;
    float pad = (float)modal_px(12, scale);
    float px = (float)modal_px(13, scale);
    int alpha = (int)(anim * 255.0f);
    uint32_t field = modal_field_color();
    uint32_t stroke = (hover > 0.2f || open) ? chrome->hover : chrome->muted;
    uint32_t fg = busy ? chrome->muted : chrome->title_color;
    float chev = (float)modal_px(16, scale);

    icon_round_rect(platform->hdc, x, y, w, h, radius, field, alpha);
    icon_round_stroke(
        platform->hdc,
        x,
        y,
        w,
        h,
        radius,
        stroke,
        (int)(anim * ((hover > 0.2f || open) ? 70.0f : 28.0f)),
        1.0f
    );
    icon_draw_label_alpha(
        platform->hdc,
        x + pad,
        y,
        w - pad * 2.0f - chev,
        h,
        label,
        fg,
        px,
        600,
        modal_alpha_local(anim)
    );
    icon_draw(
        platform->hdc,
        open ? ICON_CHEVRON_UP : ICON_CHEVRON_DOWN,
        x + w - pad - chev * 0.5f,
        y + h * 0.5f,
        chev,
        fg,
        0.0f
    );
}

static void
draw_app_lang_menu(Platform *platform, const SettingsLayout *L, int over, float anim, float scale)
{
    const ThemeChrome *chrome = theme_chrome();
    const I18nId ids[2] = { I18N_UI_EN, I18N_UI_DE };
    float y = L->app_menu_y + 5.0f;
    float inset = (float)modal_px(10, scale);
    int i;
    int alpha = modal_alpha_local(anim);
    float radius = (float)modal_px(10, scale);
    int selected = i18n_lang() == I18N_DE ? 1 : 0;

    icon_round_rect(platform->hdc, L->app_menu_x, L->app_menu_y + 2.0f, L->app_menu_w, L->app_menu_h, radius, 0x000000, alpha * 40 / 255);
    icon_round_rect(platform->hdc, L->app_menu_x, L->app_menu_y, L->app_menu_w, L->app_menu_h, radius, modal_panel_color(), alpha);
    icon_round_stroke(platform->hdc, L->app_menu_x, L->app_menu_y, L->app_menu_w, L->app_menu_h, radius, chrome->muted, alpha * 28 / 255, 1.0f);
    for (i = 0; i < 2; i++) {
        uint32_t fg = (i == selected) ? chrome->title_color : chrome->muted;
        if (i == over) {
            icon_round_rect(
                platform->hdc,
                L->app_menu_x + 4.0f,
                y,
                L->app_menu_w - 8.0f,
                L->item_h,
                6.0f,
                modal_mix(modal_field_color(), 0xffffff, 22),
                alpha
            );
            fg = chrome->title_color;
        }
        icon_draw_label_alpha(
            platform->hdc,
            L->app_menu_x + inset,
            y,
            L->app_menu_w - inset * 2.0f,
            L->item_h,
            i18n_t(ids[i]),
            fg,
            (float)modal_px(13, scale),
            600,
            alpha
        );
        y += L->item_h;
    }
}

static int
app_lang_item_at(const SettingsLayout *L, int mx, int my)
{
    int i;
    float y = L->app_menu_y + 5.0f;

    for (i = 0; i < 2; i++) {
        if (modal_hit(mx, my, L->app_menu_x, y, L->app_menu_w, L->item_h)) {
            return i;
        }
        y += L->item_h;
    }
    return -1;
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
    float radius = (float)modal_px(10, scale);

    icon_round_rect(platform->hdc, L->menu_x, L->menu_y + 2.0f, L->menu_w, L->menu_h, radius, 0x000000, alpha * 40 / 255);
    icon_round_rect(platform->hdc, L->menu_x, L->menu_y, L->menu_w, L->menu_h, radius, modal_panel_color(), alpha);
    icon_round_stroke(platform->hdc, L->menu_x, L->menu_y, L->menu_w, L->menu_h, radius, chrome->muted, alpha * 28 / 255, 1.0f);
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
            i18n_t(k_lang_i18n[idx]),
            fg,
            (float)modal_px(13, scale),
            idx == selected ? 600 : 600,
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
        i18n_t(I18N_OK),
        fg,
        (float)modal_px(13, scale),
        600,
        alpha
    );
}

static void
draw_card_outline(Platform *platform, const SettingsLayout *L, float anim, float scale)
{
    const ThemeChrome *chrome = theme_chrome();
    float radius = (float)modal_px(MODAL_RADIUS, scale);
    icon_round_stroke(
        platform->hdc,
        L->x,
        L->y,
        L->w,
        L->h,
        radius,
        chrome->muted,
        modal_alpha_local(anim) * 40 / 255,
        1.0f
    );
}

static void
draw_sidebar(Platform *platform, const SettingsLayout *L, float anim, float scale)
{
    const ThemeChrome *chrome = theme_chrome();
    int i;
    int alpha = modal_alpha_local(anim);
    float pad = (float)modal_px(10, scale);
    float icon_s = (float)modal_px(16, scale);
    float radius = (float)modal_px(MODAL_RADIUS, scale);
    float edge = 1.0f;
    float inner_r = radius - edge;

    if (inner_r < 0.0f) {
        inner_r = 0.0f;
    }
    icon_round_rect_corners(
        platform->hdc,
        L->side_x + edge,
        L->y + edge,
        L->side_w - edge,
        L->h - edge * 2.0f,
        inner_r,
        0.0f,
        0.0f,
        inner_r,
        modal_mix(modal_panel_color(), 0x000000, 22),
        alpha
    );
    icon_fill_rect(
        platform->hdc,
        L->side_x + L->side_w,
        L->y + (float)modal_px(14, scale),
        1.0f,
        L->h - (float)modal_px(28, scale),
        chrome->muted,
        alpha * 28 / 255
    );
    icon_draw_label_alpha(
        platform->hdc,
        L->nav_x,
        L->y + (float)modal_px(16, scale),
        L->nav_w,
        (float)modal_px(20, scale),
        i18n_t(I18N_SETTINGS),
        chrome->title_color,
        (float)modal_px(13, scale),
        600,
        alpha
    );
    for (i = 0; i < SETTINGS_PAGE_COUNT; i++) {
        float y = nav_row_y(L, i);
        float hover = (i == g_page) ? 1.0f : g_hover_nav[i];
        uint32_t fg = (i == g_page || hover > 0.2f) ? chrome->hover : chrome->muted;
        float icon_cx = L->nav_x + pad + icon_s * 0.5f;
        float text_x = L->nav_x + pad + icon_s + (float)modal_px(8, scale);

        if (i == g_page || hover > 0.01f) {
            uint32_t fill = modal_mix(modal_panel_color(), 0xffffff, i == g_page ? 22 : 10 + (int)(hover * 16.0f));
            icon_round_rect(
                platform->hdc,
                L->nav_x,
                y,
                L->nav_w,
                L->nav_h,
                (float)modal_px(7, scale),
                fill,
                (int)(anim * (i == g_page ? 255.0f : hover * 255.0f))
            );
        }
        icon_set_alpha(alpha);
        icon_draw(platform->hdc, k_page_icon[i], icon_cx, y + L->nav_h * 0.5f, icon_s, fg, 0.0f);
        icon_set_alpha(255);
        icon_draw_label_alpha(
            platform->hdc,
            text_x,
            y,
            L->nav_x + L->nav_w - text_x - pad,
            L->nav_h,
            i18n_t(k_page_i18n[i]),
            fg,
            (float)modal_px(13, scale),
            600,
            alpha
        );
    }
}

static const wchar_t *
dlc_state_label(int owned)
{
    if (owned == 1) {
        return i18n_t(I18N_OWNED);
    }
    if (owned == 0) {
        return i18n_t(I18N_MISSING);
    }
    return i18n_t(I18N_UNKNOWN);
}

static uint32_t
dlc_state_color(int owned, const ThemeChrome *chrome)
{
    if (owned == 1) {
        return 0x3ccf6e;
    }
    if (owned == 0) {
        return 0xe81123;
    }
    return chrome->muted;
}

static void
draw_dlc_row(
    Platform *platform,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *name,
    int owned,
    float anim,
    float scale
)
{
    const ThemeChrome *chrome = theme_chrome();
    int alpha = modal_alpha_local(anim);
    uint32_t color = dlc_state_color(owned, chrome);
    float status_w = (float)modal_px(72, scale);

    icon_draw_label_alpha(
        platform->hdc,
        x,
        y,
        w - status_w - (float)modal_px(8, scale),
        h,
        name,
        chrome->title_color,
        (float)modal_px(12, scale),
        600,
        alpha
    );
    icon_draw_label_end_alpha(
        platform->hdc,
        x + w - status_w,
        y,
        status_w,
        h,
        dlc_state_label(owned),
        color,
        (float)modal_px(12, scale),
        600,
        alpha
    );
}

static void
draw_user_page(Platform *platform, const SettingsLayout *L, float anim, float scale)
{
    const ThemeChrome *chrome = theme_chrome();
    int signed_in = login_modal_signed_in();
    const char *user = login_modal_username();
    const char *avatar = login_modal_avatar_path();
    wchar_t name_w[64];
    float text_x = L->avatar_x + L->avatar_s + (float)modal_px(12, scale);
    float text_w = L->copy_x - text_x - (float)modal_px(8, scale);
    if (text_w < 8.0f) {
        text_w = 8.0f;
    }
    int copied = g_copied_ms && (os_tick_ms() - g_copied_ms) < 1400u;
    float use = g_hover_copy;
    uint32_t fg = use > 0.2f || copied ? chrome->hover : chrome->muted;
    int alpha = modal_alpha_local(anim);

    name_w[0] = 0;
    if (signed_in && user && user[0]) {
        os_utf8_to_wide(user, name_w, 64);
    }
    icon_round_rect(
        platform->hdc,
        L->avatar_x,
        L->avatar_y,
        L->avatar_s,
        L->avatar_s,
        L->avatar_s * 0.5f,
        chrome->hover,
        alpha * 18 / 255
    );
    if (signed_in && avatar && avatar[0]) {
        icon_draw_avatar(
            platform->hdc,
            avatar,
            L->avatar_x + L->avatar_s * 0.5f,
            L->avatar_y + L->avatar_s * 0.5f,
            L->avatar_s
        );
    } else {
        icon_draw(
            platform->hdc,
            ICON_USER,
            L->avatar_x + L->avatar_s * 0.5f,
            L->avatar_y + L->avatar_s * 0.5f,
            L->avatar_s * 0.46f,
            chrome->muted,
            0.0f
        );
    }
    icon_draw_label_alpha(
        platform->hdc,
        text_x,
        L->avatar_y,
        text_w,
        L->avatar_s * 0.55f,
        signed_in && name_w[0] ? name_w : i18n_t(I18N_GUEST),
        chrome->title_color,
        (float)modal_px(13, scale),
        600,
        alpha
    );
    icon_draw_label_alpha(
        platform->hdc,
        text_x,
        L->avatar_y + L->avatar_s * 0.48f,
        text_w,
        L->avatar_s * 0.52f,
        signed_in ? i18n_t(I18N_SIGNED_IN_STEAM) : i18n_t(I18N_NOT_SIGNED_IN),
        chrome->muted,
        (float)modal_px(12, scale),
        600,
        alpha
    );
    if (use > 0.01f || copied) {
        icon_round_rect(
            platform->hdc,
            L->copy_x,
            L->copy_y,
            L->copy_w,
            L->copy_h,
            (float)modal_px(6, scale),
            modal_mix(modal_panel_color(), 0xffffff, copied ? 22 : 10 + (int)(use * 16.0f)),
            alpha
        );
    }
    icon_draw_label_center_alpha(
        platform->hdc,
        L->copy_x,
        L->copy_y,
        L->copy_w,
        L->copy_h,
        copied ? i18n_t(I18N_COPIED) : i18n_t(I18N_COPY_ID),
        fg,
        (float)modal_px(12, scale),
        600,
        alpha
    );
    {
        int forsaken = platform->steam_owns_forsaken ? platform->steam_owns_forsaken() : -1;
        int shadowkeep = platform->steam_owns_shadowkeep ? platform->steam_owns_shadowkeep() : -1;
        int busy = platform->steam_busy && platform->steam_busy();
        float card_y = L->avatar_y + L->avatar_s + (float)modal_px(22, scale);
        float label_h = (float)modal_px(18, scale);
        float card_h = (float)modal_px(104, scale);
        float row_h = (float)modal_px(26, scale);
        float pad = (float)modal_px(12, scale);
        float radius = (float)modal_px(10, scale);
        float row_y;
        const wchar_t *summary;

        if (!signed_in) {
            summary = i18n_t(I18N_DLC_SIGN_IN);
            forsaken = -1;
            shadowkeep = -1;
        } else if (busy) {
            summary = i18n_t(I18N_DLC_CHECKING);
        } else if (forsaken == 1 && shadowkeep == 1) {
            summary = i18n_t(I18N_DLC_ALL_OWNED);
        } else if (forsaken == 0 || shadowkeep == 0) {
            summary = i18n_t(I18N_DLC_MISSING);
        } else {
            summary = i18n_t(I18N_DLC_CANT_VERIFY);
        }
        draw_section_label(
            platform,
            L->content_x,
            card_y,
            L->content_w,
            label_h,
            i18n_t(I18N_DLC_OWNERSHIP),
            scale,
            anim
        );
        card_y += label_h + (float)modal_px(6, scale);
        icon_round_rect(
            platform->hdc,
            L->content_x,
            card_y,
            L->content_w,
            card_h,
            radius,
            modal_field_color(),
            alpha
        );
        icon_round_stroke(
            platform->hdc,
            L->content_x,
            card_y,
            L->content_w,
            card_h,
            radius,
            chrome->muted,
            (int)(anim * 28.0f),
            1.0f
        );
        icon_draw_label_alpha(
            platform->hdc,
            L->content_x + pad,
            card_y + (float)modal_px(8, scale),
            L->content_w - pad * 2.0f,
            (float)modal_px(18, scale),
            summary,
            (forsaken == 1 && shadowkeep == 1) ? chrome->title_color : chrome->muted,
            (float)modal_px(12, scale),
            600,
            alpha
        );
        row_y = card_y + (float)modal_px(32, scale);
        draw_dlc_row(
            platform,
            L->content_x + pad,
            row_y,
            L->content_w - pad * 2.0f,
            row_h,
            L"Forsaken",
            forsaken,
            anim,
            scale
        );
        draw_dlc_row(
            platform,
            L->content_x + pad,
            row_y + row_h + (float)modal_px(4, scale),
            L->content_w - pad * 2.0f,
            row_h,
            L"Shadowkeep",
            shadowkeep,
            anim,
            scale
        );
    }
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
    if (start && platform->steam_license_block && platform->steam_license_block()) {
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
    g_page = SETTINGS_PAGE_USER;
    g_ui_lang_open = 0;
    g_lang_open = 0;
}

void
settings_modal_open_for_download(void)
{
    login_modal_hide();
    g_open = 1;
    g_block_mouse = 1;
    g_arm_download = 1;
    g_page = SETTINGS_PAGE_INSTALL;
    g_ui_lang_open = 0;
    g_lang_open = 0;
}

void
settings_modal_close(void)
{
    g_open = 0;
    g_anim = 0.0f;
    g_block_mouse = 0;
    g_arm_download = 0;
    g_ui_lang_open = 0;
    g_lang_open = 0;
    g_lang_drag = 0;
}

void
settings_modal_hide(void)
{
    int i;

    g_open = 0;
    g_anim = 0.0f;
    g_block_mouse = 0;
    g_arm_download = 0;
    g_ui_lang_open = 0;
    g_lang_open = 0;
    g_lang_scroll = 0;
    g_lang_drag = 0;
    g_hover_close = 0.0f;
    g_hover_browse = 0.0f;
    g_hover_exe = 0.0f;
    g_hover_ui_lang = 0.0f;
    g_hover_lang = 0.0f;
    g_hover_ok = 0.0f;
    g_hover_copy = 0.0f;
    for (i = 0; i < SETTINGS_PAGE_COUNT; i++) {
        g_hover_nav[i] = 0.0f;
    }
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
    int mx;
    int my;
    int over_close;
    int over_browse;
    int over_exe;
    int over_lang;
    int over_ui_lang;
    int over_ok;
    int over_card;
    int over_menu;
    int over_ui_menu;
    int over_copy;
    int over_nav;
    int busy;
    int can_ok;
    int item;
    int app_item;
    int selected;
    int i;
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
    can_ok = !busy && (!g_arm_download || has_dir(platform));
    item = (g_open && g_lang_open && g_page == SETTINGS_PAGE_GENERAL) ? lang_item_at(&L, mx, my) : -1;
    app_item = (g_open && g_ui_lang_open && g_page == SETTINGS_PAGE_GENERAL) ? app_lang_item_at(&L, mx, my) : -1;
    over_nav = nav_at(&L, mx, my);
    over_close = modal_hit(mx, my, L.close_x, L.close_y, L.close_s, L.close_s);
    over_browse = !busy && g_page == SETTINGS_PAGE_INSTALL &&
        modal_hit(mx, my, L.path_x, L.path_y, L.path_w, L.path_h);
    over_exe = !busy && g_page == SETTINGS_PAGE_INSTALL &&
        modal_hit(mx, my, L.exe_x, L.exe_y, L.exe_w, L.exe_h);
    over_ui_lang = g_page == SETTINGS_PAGE_GENERAL &&
        modal_hit(mx, my, L.app_lang_x, L.app_lang_y, L.app_lang_w, L.app_lang_h);
    over_lang = !busy && g_page == SETTINGS_PAGE_GENERAL &&
        modal_hit(mx, my, L.lang_x, L.lang_y, L.lang_w, L.lang_h);
    over_ui_menu = g_ui_lang_open && g_page == SETTINGS_PAGE_GENERAL &&
        modal_hit(mx, my, L.app_menu_x, L.app_menu_y, L.app_menu_w, L.app_menu_h);
    over_menu = g_lang_open && g_page == SETTINGS_PAGE_GENERAL &&
        modal_hit(mx, my, L.menu_x, L.menu_y, L.menu_w, L.menu_h);
    over_copy = g_page == SETTINGS_PAGE_USER &&
        modal_hit(mx, my, L.copy_x, L.copy_y, L.copy_w, L.copy_h);
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
    over_ok = can_ok && !g_lang_open && !g_ui_lang_open && g_page == SETTINGS_PAGE_INSTALL &&
        modal_hit(mx, my, L.ok_x, L.ok_y, L.ok_w, L.ok_h);
    over_card = modal_hit(mx, my, L.x, L.y, L.w, L.h) || over_menu || over_ui_menu;
    g_hover_close = modal_approach(g_hover_close, (g_open && over_close) ? 1.0f : 0.0f, dt);
    g_hover_browse = modal_approach(g_hover_browse, (g_open && over_browse) ? 1.0f : 0.0f, dt);
    g_hover_exe = modal_approach(g_hover_exe, (g_open && over_exe) ? 1.0f : 0.0f, dt);
    g_hover_ui_lang = modal_approach(g_hover_ui_lang, (g_open && (over_ui_lang || g_ui_lang_open)) ? 1.0f : 0.0f, dt);
    g_hover_lang = modal_approach(g_hover_lang, (g_open && (over_lang || g_lang_open)) ? 1.0f : 0.0f, dt);
    g_hover_ok = modal_approach(g_hover_ok, (g_open && over_ok) ? 1.0f : 0.0f, dt);
    g_hover_copy = modal_approach(g_hover_copy, (g_open && over_copy) ? 1.0f : 0.0f, dt);
    g_over_item = item;
    g_hover_item = modal_approach(g_hover_item, item >= 0 ? 1.0f : 0.0f, dt);
    for (i = 0; i < SETTINGS_PAGE_COUNT; i++) {
        g_hover_nav[i] = modal_approach(g_hover_nav[i], (g_open && over_nav == i) ? 1.0f : 0.0f, dt);
    }

    modal_draw_overlay(platform->hdc, L.overlay_w, L.overlay_h, g_anim);
    modal_draw_card(platform->hdc, L.x, L.y, L.w, L.h, g_anim, s);
    draw_sidebar(platform, &L, g_anim, s);
    icon_draw_label_alpha(
        platform->hdc,
        L.title_x,
        L.title_y,
        L.title_w,
        L.title_h,
        i18n_t(k_page_i18n[g_page]),
        theme_chrome()->title_color,
        (float)modal_px(15, s),
        600,
        modal_alpha_local(g_anim)
    );
    modal_draw_close(platform->hdc, L.close_x, L.close_y, L.close_s, g_hover_close, g_anim);

    if (g_page == SETTINGS_PAGE_USER) {
        draw_user_page(platform, &L, g_anim, s);
    } else if (g_page == SETTINGS_PAGE_GENERAL) {
        draw_section_label(
            platform,
            L.content_x,
            L.content_y,
            L.content_w,
            (float)modal_px(20, s),
            i18n_t(I18N_APP_LANGUAGE),
            s,
            g_anim
        );
        draw_select_field(
            platform,
            L.app_lang_x,
            L.app_lang_y,
            L.app_lang_w,
            L.app_lang_h,
            i18n_t(i18n_lang() == I18N_DE ? I18N_UI_DE : I18N_UI_EN),
            g_hover_ui_lang,
            g_ui_lang_open,
            0,
            g_anim,
            s
        );
        if (g_ui_lang_open && g_anim > 0.02f) {
            draw_app_lang_menu(platform, &L, app_item, g_anim, s);
        }
        icon_draw_label_alpha(
            platform->hdc,
            L.content_x,
            L.app_lang_y + L.app_lang_h + (g_ui_lang_open ? L.app_menu_h + (float)modal_px(8, s) : (float)modal_px(8, s)),
            L.content_w,
            (float)modal_px(24, s),
            i18n_t(I18N_APP_LANGUAGE_HINT),
            theme_chrome()->muted,
            (float)modal_px(12, s),
            600,
            modal_alpha_local(g_anim)
        );
        draw_section_label(
            platform,
            L.content_x,
            L.lang_y - (float)modal_px(22, s),
            L.content_w,
            (float)modal_px(20, s),
            i18n_t(I18N_GAME_LANGUAGE),
            s,
            g_anim
        );
        lang_w = current_lang_label(platform);
        draw_select_field(
            platform,
            L.lang_x,
            L.lang_y,
            L.lang_w,
            L.lang_h,
            lang_w,
            g_hover_lang,
            g_lang_open,
            busy,
            g_anim,
            s
        );
        selected = 0;
        {
            const char *cur = platform->install_language ? platform->install_language() : "";
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
        icon_draw_label_alpha(
            platform->hdc,
            L.content_x,
            L.lang_y + L.lang_h + (g_lang_open ? L.menu_h + (float)modal_px(10, s) : (float)modal_px(10, s)),
            L.content_w,
            (float)modal_px(32, s),
            i18n_t(I18N_GAME_LANGUAGE_HINT),
            theme_chrome()->muted,
            (float)modal_px(12, s),
            600,
            modal_alpha_local(g_anim)
        );
    } else {
        draw_section_label(
            platform,
            L.content_x,
            L.content_y,
            L.content_w,
            (float)modal_px(20, s),
            i18n_t(I18N_INSTALL_FOLDER),
            s,
            g_anim
        );
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
            draw_path_row(
                platform,
                L.path_x,
                L.path_y,
                L.path_w,
                L.path_h,
                L.browse_x,
                L.browse_y,
                L.browse_w,
                L.browse_h,
                path_w,
                g_hover_browse,
                busy,
                g_anim,
                s
            );
            draw_section_label(
                platform,
                L.content_x,
                L.path_y + L.path_h + (float)modal_px(10, s),
                L.content_w,
                (float)modal_px(20, s),
                i18n_t(I18N_GAME_EXE),
                s,
                g_anim
            );
            path_label(current_exe(platform), path_w, max_chars);
            draw_path_row(
                platform,
                L.exe_x,
                L.exe_y,
                L.exe_w,
                L.exe_h,
                L.exe_browse_x,
                L.exe_browse_y,
                L.exe_browse_w,
                L.exe_browse_h,
                path_w,
                g_hover_exe,
                busy,
                g_anim,
                s
            );
        }
        if (busy) {
            wcscpy(g_status_w, i18n_t(I18N_STATUS_FOLDER_LOCKED));
        } else if (g_arm_download && !has_dir(platform)) {
            wcscpy(g_status_w, i18n_t(I18N_STATUS_BROWSE_FIRST));
        } else if (g_arm_download) {
            wcscpy(g_status_w, i18n_t(I18N_STATUS_OK_STARTS));
        } else if (is_ready(platform)) {
            wcscpy(g_status_w, i18n_t(I18N_STATUS_DAWN_INSTALLED));
        } else {
            wcscpy(g_status_w, i18n_t(I18N_STATUS_DOWNLOADS_GO));
        }
        icon_draw_label_alpha(
            platform->hdc,
            L.status_x,
            L.status_y,
            L.status_w,
            L.status_h,
            g_status_w,
            theme_chrome()->muted,
            (float)modal_px(12, s),
            600,
            modal_alpha_local(g_anim)
        );
        draw_ok_pill(platform, &L, g_hover_ok, !can_ok, g_anim, s);
    }
    draw_card_outline(platform, &L, g_anim, s);

    if (!g_open) {
        return;
    }
    if (platform->key == PLATFORM_KEY_ESCAPE) {
        if (g_lang_open) {
            g_lang_open = 0;
        } else if (g_ui_lang_open) {
            g_ui_lang_open = 0;
        } else {
            settings_modal_close();
        }
    } else if (platform->key == PLATFORM_KEY_ENTER && !g_lang_open && !g_ui_lang_open && g_page == SETTINGS_PAGE_INSTALL) {
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
        if (over_nav >= 0) {
            g_page = over_nav;
            g_ui_lang_open = 0;
            g_lang_open = 0;
            g_lang_drag = 0;
        } else if (g_ui_lang_open && app_item >= 0) {
            i18n_set(app_item == 1 ? I18N_DE : I18N_EN);
            g_ui_lang_open = 0;
        } else if (over_ui_lang) {
            g_ui_lang_open = !g_ui_lang_open;
            g_lang_open = 0;
            g_lang_drag = 0;
        } else if (g_ui_lang_open && !over_ui_menu) {
            g_ui_lang_open = 0;
        } else if (g_lang_open && over_lang_thumb(&L, mx, my)) {
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
            g_ui_lang_open = 0;
            g_lang_drag = 0;
            if (g_lang_open) {
                const char *cur = platform->install_language ? platform->install_language() : "";
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
        } else if (over_copy) {
            if (user_id_copy()) {
                g_copied_ms = os_tick_ms();
                if (g_copied_ms == 0) {
                    g_copied_ms = 1;
                }
            }
        } else if (over_close || !over_card) {
            settings_modal_close();
        } else if (over_browse) {
            choose_folder(platform);
        } else if (over_exe) {
            choose_exe(platform);
        } else if (over_ok) {
            confirm_ok(platform);
        }
        platform->mouse_pressed = 0;
    }
}
