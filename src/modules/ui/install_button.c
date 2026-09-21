#include "install_button.h"
#include "login_modal.h"
#include "settings_modal.h"
#include "modal_skin.h"
#include "i18n/i18n.h"
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
#define MENU_W 240.0f
#define MENU_INSET 8.0f
#define MENU_ITEM_H 30.0f
#define MENU_ITEM_GAP 4.0f
#define MENU_RADIUS 10.0f

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
    float dawn_y;
    float sunrise_y;
    float full_y;
    float uninstall_y;
    float sim_y;
    int show_dawn;
    int show_sunrise;
    int show_full;
    int show_plain;
    int show_sim;
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
static float g_hover_dawn;
static float g_hover_sunrise;
static float g_hover_full;
static float g_hover_uninstall;
static float g_hover_sim;
static float g_menu;
static float g_hint;
static float g_shimmer;
static int g_open;
static int g_confirm;
static char g_secret[160];
static int g_secret_len;
static float g_hover_secret;
static float g_hover_secret_close;
static float g_caret;
static char g_hint_key[192];
static float g_hint_need;
static float g_hint_px_key;

static float
px_snap(float v)
{
    return floorf(v + 0.5f);
}

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
    float next = current + (target - current) * (1.0f - expf(-22.0f * dt));
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

static void
append_secret(const char *text, int len)
{
    int n;

    if (!text || len <= 0) {
        return;
    }
    for (n = 0; n < len && g_secret_len < (int)sizeof(g_secret) - 1; n++) {
        unsigned char ch = (unsigned char)text[n];
        if (ch < 32 || ch == 127) {
            continue;
        }
        g_secret[g_secret_len++] = (char)ch;
    }
    g_secret[g_secret_len] = '\0';
}

static void
trim_secret_utf8(void)
{
    if (g_secret_len <= 0) {
        g_secret_len = 0;
        g_secret[0] = '\0';
        return;
    }
    g_secret_len -= 1;
    while (g_secret_len > 0 && ((unsigned char)g_secret[g_secret_len] & 0xc0) == 0x80) {
        g_secret_len -= 1;
    }
    g_secret[g_secret_len] = '\0';
}

static void
submit_secret(Platform *platform)
{
    if (g_secret_len <= 0 || !platform->install_submit_secret) {
        return;
    }
    platform->install_submit_secret(g_secret);
    clear_secret();
}

static int
secret_tick(Platform *platform, float dt)
{
    int need = install_need(platform);
    float s = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float inset;
    float x;
    float y;
    float w;
    float h;
    float field_x;
    float field_y;
    float field_w;
    float field_h;
    float btn_x;
    float btn_y;
    float btn_w;
    float btn_h;
    float close_x;
    float close_y;
    float close_s;
    wchar_t mask[160];
    int i;
    int over_btn;
    int over_field;
    int over_close;
    int caret_on;

    if (need < 1 || need > 3) {
        if (g_secret_len) {
            clear_secret();
        }
        g_hover_secret = 0.0f;
        g_hover_secret_close = 0.0f;
        g_caret = 0.0f;
        return 0;
    }

    append_secret(platform->text, platform->text_len);
    if (platform->key == PLATFORM_KEY_BACKSPACE) {
        trim_secret_utf8();
    }
    if (platform->key == PLATFORM_KEY_ENTER) {
        submit_secret(platform);
    }

    inset = (float)modal_px(MODAL_INSET, s);
    w = (float)modal_px(360, s);
    h = (float)modal_px(212, s);
    if (w > (float)platform->width - 32.0f) {
        w = (float)platform->width - 32.0f;
    }
    x = ((float)platform->width - w) * 0.5f;
    y = ((float)platform->height - h) * 0.5f;
    field_h = (float)modal_px(MODAL_FIELD_H, s);
    btn_h = (float)modal_px(MODAL_BTN_H, s);
    field_x = x + inset;
    field_w = w - inset * 2.0f;
    field_y = y + (float)modal_px(108, s);
    btn_x = field_x;
    btn_w = field_w;
    btn_y = field_y + field_h + (float)modal_px(MODAL_GAP, s);

    modal_place_close(x, y, w, s, &close_x, &close_y, &close_s);
    over_btn = hit(platform->mouse_x, platform->mouse_y, btn_x, btn_y, btn_w, btn_h);
    over_field = hit(platform->mouse_x, platform->mouse_y, field_x, field_y, field_w, field_h);
    over_close = hit(platform->mouse_x, platform->mouse_y, close_x, close_y, close_s, close_s);
    platform->want_text_cursor = over_field && !over_close;
    g_hover_secret = approach(g_hover_secret, over_btn ? 1.0f : 0.0f, dt);
    g_hover_secret_close = approach(g_hover_secret_close, over_close ? 1.0f : 0.0f, dt);
    g_caret += dt;
    if (g_caret > 1.0f) {
        g_caret -= (float)((int)g_caret);
    }
    caret_on = g_caret < 0.55f;

    if (need == 2 || need == 3) {
        os_utf8_to_wide(g_secret, mask, 160);
    } else {
        const unsigned char *p = (const unsigned char *)g_secret;
        i = 0;
        while (*p && i < 159) {
            if ((*p & 0xc0) != 0x80) {
                mask[i++] = 0x2022;
            }
            p++;
        }
        mask[i] = 0;
    }

    modal_draw_overlay(platform->hdc, (float)platform->width, (float)platform->height, 1.0f);
    modal_draw_card(platform->hdc, x, y, w, h, 1.0f, s);
    modal_draw_title(
        platform->hdc,
        x + inset,
        y + (float)modal_px(14, s),
        w - inset * 2.0f - close_s,
        (float)modal_px(20, s),
        need == 2 ? i18n_t(I18N_STEAM_GUARD) : (need == 3 ? i18n_t(I18N_STEAM_USERNAME) : i18n_t(I18N_STEAM_PASSWORD)),
        s,
        1.0f
    );
    modal_draw_close(platform->hdc, close_x, close_y, close_s, g_hover_secret_close, 1.0f);
    modal_draw_subtitle(
        platform->hdc,
        x + inset,
        y + (float)modal_px(36, s),
        w - inset * 2.0f,
        (float)modal_px(18, s),
        need == 2 ? i18n_t(I18N_GUARD_HINT) : (need == 3 ? i18n_t(I18N_USERNAME_HINT) : i18n_t(I18N_PASSWORD_HINT)),
        s,
        1.0f
    );
    modal_draw_subtitle(
        platform->hdc,
        x + inset,
        y + (float)modal_px(54, s),
        w - inset * 2.0f,
        (float)modal_px(36, s),
        need == 2 ? i18n_t(I18N_GUARD_HELP) : (need == 3 ? i18n_t(I18N_USERNAME_HELP) : i18n_t(I18N_PASSWORD_HELP)),
        s,
        1.0f
    );
    modal_draw_input(
        platform->hdc,
        field_x,
        field_y,
        field_w,
        field_h,
        mask[0] ? mask : L"",
        need == 2 ? i18n_t(I18N_GUARD_CODE) : (need == 3 ? i18n_t(I18N_USERNAME) : i18n_t(I18N_PASSWORD)),
        1,
        caret_on,
        1.0f,
        s
    );
    modal_draw_button(
        platform->hdc,
        btn_x,
        btn_y,
        btn_w,
        btn_h,
        i18n_t(I18N_CONTINUE),
        ICON_LOG_IN,
        g_hover_secret,
        1.0f,
        g_secret_len <= 0,
        s,
        -1.0f
    );

    if (platform->mouse_pressed && over_close && platform->install_cancel) {
        platform->install_cancel();
        clear_secret();
        return 1;
    }
    if (platform->mouse_pressed && over_btn) {
        submit_secret(platform);
    }
    return 1;
}

static int
is_ready(Platform *platform)
{
    return platform->install_ready && platform->install_ready();
}

static int
dawn_needs_update(Platform *platform)
{
    const char *local;
    const char *latest;

    if (!platform || !is_ready(platform)) {
        return 0;
    }
    local = platform->dawn_version ? platform->dawn_version() : "";
    latest = platform->dawn_latest ? platform->dawn_latest() : "";
    if (!latest || !latest[0]) {
        return 0;
    }
    if (!local || !local[0]) {
        return 1;
    }
    return os_stricmp(local, latest) != 0;
}

static int
is_busy(Platform *platform)
{
    return platform->install_busy && platform->install_busy();
}

static int
game_state(Platform *platform)
{
    return platform && platform->game_state ? platform->game_state() : 0;
}

static int
is_game_active(Platform *platform)
{
    return game_state(platform) > 0;
}

static int
is_paused(Platform *platform)
{
    return platform && platform->install_paused && platform->install_paused();
}

static int
can_pause(Platform *platform)
{
    return platform && platform->install_can_pause && platform->install_can_pause();
}

#if APP_DEV
static int
can_simulate(Platform *platform)
{
    return platform && platform->install_can_simulate && platform->install_can_simulate();
}
#endif

static int
is_signed_in(Platform *platform)
{
    return platform && platform->steam_signed_in && platform->steam_signed_in();
}

static const char *
action_block(Platform *platform)
{
    if (!platform) {
        return "Sign in first";
    }
    if (platform->steam_license_block) {
        return platform->steam_license_block();
    }
    return is_signed_in(platform) ? NULL : "Sign in first";
}

static void
copy_label(Platform *platform, char *out, int max, IconId *icon)
{
    int gs = game_state(platform);

    if (gs == 2) {
        snprintf(out, (size_t)max, "%s", i18n_tu(I18N_STOP));
        *icon = ICON_X;
        return;
    }
    if (gs == 1) {
        snprintf(out, (size_t)max, "%s", i18n_tu(I18N_CANCEL));
        *icon = ICON_X;
        return;
    }
    if (is_paused(platform) && can_pause(platform)) {
        snprintf(out, (size_t)max, "%s", i18n_tu(I18N_RESUME));
        *icon = ICON_PLAY;
        return;
    }
    if (is_busy(platform) && can_pause(platform)) {
        snprintf(out, (size_t)max, "%s", i18n_tu(I18N_PAUSE));
        *icon = ICON_PAUSE;
        return;
    }
    if (is_busy(platform)) {
        snprintf(out, (size_t)max, "%s", i18n_tu(I18N_CANCEL));
        *icon = ICON_X;
        return;
    }
    if (is_ready(platform)) {
        if (dawn_needs_update(platform)) {
            snprintf(out, (size_t)max, "%s", i18n_tu(I18N_UPDATE));
            *icon = ICON_DOWNLOAD;
            return;
        }
        snprintf(out, (size_t)max, "%s", i18n_tu(I18N_PLAY));
        *icon = ICON_PLAY;
        return;
    }
    {
        int parts = platform->install_parts ? platform->install_parts() : 0;
        if ((parts & INSTALL_PART_DEPOTS) && !(parts & INSTALL_PART_DAWN)) {
            snprintf(out, (size_t)max, "%s", i18n_tu(I18N_INSTALL));
            *icon = ICON_DOWNLOAD;
            return;
        }
    }
    snprintf(out, (size_t)max, "%s", i18n_tu(I18N_DOWNLOAD));
    *icon = ICON_DOWNLOAD;
}

static int
status_is_quiet(const char *status)
{
    return !status ||
        status[0] == '\0' ||
        strcmp(status, "Ready") == 0 ||
        strcmp(status, "Dawn is ready") == 0 ||
        strcmp(status, "Steam is ready to download") == 0 ||
        strcmp(status, "Ready to download") == 0 ||
        strcmp(status, "Login saved") == 0;
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
    {
        int gs = game_state(platform);
        if (gs == 1) {
            *shimmer = 1;
            snprintf(out, (size_t)max, "%s", status[0] ? i18n_status(status) : i18n_tu(I18N_STARTING));
            return;
        }
        if (gs == 2) {
            if (status[0] && !status_is_quiet(status)) {
                snprintf(out, (size_t)max, "%s", i18n_status(status));
            }
            return;
        }
    }
    if (busy) {
        *shimmer = !is_paused(platform);
        progress = platform->install_progress ? platform->install_progress() : 0.0f;
        if (!status[0]) {
            snprintf(out, (size_t)max, "%s", i18n_tu(I18N_WORKING));
            return;
        }
        if (progress < 0.0f) {
            progress = 0.0f;
        }
        if (progress > 1.0f) {
            progress = 1.0f;
        }
        if (status[0] && progress > 0.01f && progress < 0.995f && !strchr(status, '%')) {
            snprintf(out, (size_t)max, "%s · %d%%", i18n_status(status), (int)(progress * 100.0f + 0.5f));
        } else if (status[0]) {
            snprintf(out, (size_t)max, "%s", i18n_status(status));
        } else {
            snprintf(out, (size_t)max, "%s", i18n_tu(I18N_WORKING));
        }
        return;
    }
    if (!status_is_quiet(status)) {
        snprintf(out, (size_t)max, "%s", i18n_status(status));
        return;
    }
    {
        const char *block = action_block(platform);
        if (block) {
            snprintf(out, (size_t)max, "%s", i18n_status(block));
        }
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
    out->pad = 16.0f * dpi;
    out->icon_s = 16.0f * dpi;
    if (out->icon_s > out->h * 0.48f) {
        out->icon_s = out->h * 0.48f;
    }
    out->text_px = (float)((int)(13.0f * dpi + 0.5f));
    if (out->text_px < 1.0f) {
        out->text_px = 1.0f;
    }
    {
        static char measure_key[32];
        static float measure_px;
        static float measure_tw;
        wchar_t wide[32];
        const char *use = label && label[0] ? label : i18n_tu(I18N_DOWNLOAD);
        float tw = 0.0f;
        if (measure_tw > 0.0f && measure_px == out->text_px && strcmp(measure_key, use) == 0) {
            tw = measure_tw;
        } else {
            os_utf8_to_wide(use, wide, 32);
            tw = icon_measure_label(platform->hdc, wide, out->text_px, 600);
            if (tw <= 0.0f && platform->measure_label) {
                int iw = 0;
                int ih = 0;
                platform->measure_label(use, (int)(out->text_px + 0.5f), 600, &iw, &ih, NULL);
                tw = (float)iw;
            }
            if (tw <= 0.0f) {
                tw = (float)strlen(use) * out->text_px * 0.62f;
            }
            snprintf(measure_key, sizeof(measure_key), "%s", use);
            measure_px = out->text_px;
            measure_tw = tw;
        }
        gap = 8.0f * dpi;
        out->main_w = out->pad + out->icon_s + gap + ceilf(tw + 8.0f * dpi) + out->pad;
    }
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
    {
        int parts = platform->install_parts ? platform->install_parts() : 0;
        int busy = platform->install_busy && platform->install_busy();
        int stop = platform->game_state && platform->game_state() > 0;
        int rows = 1;
        float y;

        out->show_dawn = 0;
        out->show_sunrise = 0;
        out->show_full = 0;
        out->show_plain = 0;
        out->show_sim = 0;
        if (busy || stop) {
            out->show_plain = 1;
            rows += 1;
        } else {
            out->show_dawn = (parts & INSTALL_PART_DAWN) != 0;
            out->show_sunrise = (parts & INSTALL_PART_SUNRISE) != 0;
#if APP_DEV
            if (can_simulate(platform)) {
                out->show_sim = 1;
                rows += 1;
            }
#endif
            if (out->show_dawn) {
                rows += 1;
            }
            if (out->show_sunrise) {
                rows += 1;
            }
            if (out->show_dawn || out->show_sunrise) {
                out->show_full = 1;
                rows += 1;
            } else {
                out->show_plain = 1;
                rows += 1;
            }
        }
        out->menu_h = MENU_INSET * dpi + (float)rows * out->item_h +
            (float)(rows - 1) * MENU_ITEM_GAP * dpi + MENU_INSET * dpi;
        out->menu_x = out->caret_x + out->caret_w - out->menu_w;
        if (out->menu_x < inset) {
            out->menu_x = inset;
        }
        out->menu_y = out->y - 6.0f * dpi - out->menu_h;
        if (out->menu_y < inset) {
            out->menu_y = inset;
        }
        out->item_x = out->menu_x + MENU_INSET * dpi;
        out->item_w = out->menu_w - MENU_INSET * dpi * 2.0f;
        y = out->menu_y + MENU_INSET * dpi;
        out->verify_y = y;
        y += out->item_h + MENU_ITEM_GAP * dpi;
        out->sim_y = out->show_sim ? y : -1000.0f;
        if (out->show_sim) {
            y += out->item_h + MENU_ITEM_GAP * dpi;
        }
        out->dawn_y = out->show_dawn ? y : -1000.0f;
        if (out->show_dawn) {
            y += out->item_h + MENU_ITEM_GAP * dpi;
        }
        out->sunrise_y = out->show_sunrise ? y : -1000.0f;
        if (out->show_sunrise) {
            y += out->item_h + MENU_ITEM_GAP * dpi;
        }
        out->full_y = out->show_full ? y : -1000.0f;
        out->uninstall_y = out->show_plain ? y : -1000.0f;
        out->menu_x = px_snap(out->menu_x);
        out->menu_y = px_snap(out->menu_y);
        out->menu_w = px_snap(out->menu_w);
        out->menu_h = px_snap(out->menu_h);
        out->item_x = px_snap(out->item_x);
        out->item_w = px_snap(out->item_w);
        out->item_h = px_snap(out->item_h);
        out->verify_y = px_snap(out->verify_y);
        out->sim_y = px_snap(out->sim_y);
        out->dawn_y = px_snap(out->dawn_y);
        out->sunrise_y = px_snap(out->sunrise_y);
        out->full_y = px_snap(out->full_y);
        out->uninstall_y = px_snap(out->uninstall_y);
    }
}

static void
layout_hint(Platform *platform, InstallLayout *out, const char *hint)
{
    float dpi = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float inset = 20.0f * dpi;
    float gap = 14.0f * dpi;
    float max_w;
    float need = 0.0f;
    wchar_t wide[192];

    if (platform->corner_radius > inset) {
        inset = platform->corner_radius * 0.55f + 14.0f * dpi;
    }
    out->hint_px = (float)((int)(12.0f * dpi + 0.5f));
    if (out->hint_px < 11.0f) {
        out->hint_px = 11.0f;
    }
    out->hint_y = out->y;
    out->hint_h = out->h;
    max_w = out->x - gap - inset;
    if (max_w < 8.0f) {
        out->hint_x = inset;
        out->hint_w = 0.0f;
        return;
    }
    if (hint && hint[0]) {
        if (g_hint_need > 0.0f &&
            g_hint_px_key == out->hint_px &&
            strcmp(g_hint_key, hint) == 0) {
            need = g_hint_need;
        } else {
            os_utf8_to_wide(hint, wide, 192);
            need = icon_measure_label(platform->hdc, wide, out->hint_px, 600);
            snprintf(g_hint_key, sizeof(g_hint_key), "%s", hint);
            g_hint_need = need;
            g_hint_px_key = out->hint_px;
        }
    }
    if (need <= 0.0f && hint && hint[0]) {
        need = (float)strlen(hint) * out->hint_px * 0.62f;
    }
    out->hint_w = need + 12.0f * dpi;
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
draw_menu_card(Platform *platform, float x, float y, float w, float h, float open)
{
    const ThemeChrome *chrome = theme_chrome();
    float dpi = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float radius = px_snap(MENU_RADIUS * dpi);
    int alpha = (int)(open * 255.0f);

    icon_round_rect(platform->hdc, px_snap(x), px_snap(y + 2.0f * dpi), px_snap(w), px_snap(h), radius, 0x000000, alpha * 40 / 255);
    icon_round_rect(platform->hdc, px_snap(x), px_snap(y), px_snap(w), px_snap(h), radius, modal_panel_color(), alpha);
    icon_round_stroke(platform->hdc, px_snap(x), px_snap(y), px_snap(w), px_snap(h), radius, chrome->muted, alpha * 28 / 255, 1.0f);
}

static void
draw_menu_divider(Platform *platform, float x, float y, float w, float open)
{
    const ThemeChrome *chrome = theme_chrome();
    float dpi = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;

    icon_fill_rect(
        platform->hdc,
        x,
        y,
        w,
        1.0f,
        chrome->muted,
        (int)(open * 28.0f)
    );
    (void)dpi;
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
    float pad = px_snap(10.0f * dpi);
    float icon_s = px_snap(16.0f * dpi);
    float text_px = px_snap(13.0f * dpi);
    float icon_cx = px_snap(x + pad + icon_s * 0.5f);
    float text_x = px_snap(x + pad + icon_s + 8.0f * dpi);
    float text_w = px_snap(x + w - text_x - pad);
    float radius = px_snap(7.0f * dpi);
    uint32_t fg = disabled ? chrome->muted : chrome->title_color;
    int alpha = open >= 0.98f ? (disabled ? 160 : 255) : (int)(open * (disabled ? 130.0f : 255.0f));

    if (use_hover > 0.01f) {
        uint32_t fill = modal_mix(modal_panel_color(), 0xffffff, 10 + (int)(use_hover * 16.0f));
        if (danger && !disabled) {
            uint32_t red = chrome->close ? chrome->close : 0xe81123;
            fill = modal_mix(fill, red, 28 + (int)(use_hover * 50.0f));
            fg = modal_mix(fg, red, 80 + (int)(use_hover * 120.0f));
        }
        icon_round_rect(platform->hdc, x, y, w, h, radius, fill, (int)(open * use_hover * 255.0f));
    } else if (danger && !disabled) {
        fg = modal_mix(fg, chrome->close ? chrome->close : 0xe81123, 36);
    }

    icon_set_alpha(alpha);
    icon_draw(platform->hdc, icon, icon_cx, px_snap(y + h * 0.5f), icon_s, fg, 0.0f);
    icon_set_alpha(255);
    icon_draw_label_alpha(platform->hdc, text_x, px_snap(y), px_snap(text_w), px_snap(h), label, fg, text_px, 600, alpha);
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
    int paused = is_paused(platform);
    int pausable = can_pause(platform);
    int stop = is_game_active(platform);
    int danger = stop || (busy && !pausable);
    const char *lock_reason = (!busy && !stop) ? action_block(platform) : NULL;
    int locked = lock_reason != NULL;
    int mx = platform->mouse_x;
    int my = platform->mouse_y;
    int over_main = !blocked && hit(mx, my, L.main_x, L.y, L.main_w, L.h);
    int over_caret = !blocked && hit(mx, my, L.caret_x, L.y, L.caret_w, L.h);
    int over_split = over_main || over_caret;
    int over_menu = !blocked && (g_open || g_menu > 0.02f) &&
        hit(mx, my, L.menu_x, L.menu_y, L.menu_w, L.menu_h);
    int over_verify = !blocked && g_open && hit(mx, my, L.item_x, L.verify_y, L.item_w, L.item_h);
    int over_sim = !blocked && g_open && L.show_sim && hit(mx, my, L.item_x, L.sim_y, L.item_w, L.item_h);
    int over_dawn = !blocked && g_open && L.show_dawn && hit(mx, my, L.item_x, L.dawn_y, L.item_w, L.item_h);
    int over_sunrise = !blocked && g_open && L.show_sunrise && hit(mx, my, L.item_x, L.sunrise_y, L.item_w, L.item_h);
    int over_full = !blocked && g_open && L.show_full && hit(mx, my, L.item_x, L.full_y, L.item_w, L.item_h);
    int over_uninstall = !blocked && g_open && L.show_plain && hit(mx, my, L.item_x, L.uninstall_y, L.item_w, L.item_h);
    int can_remove = L.show_dawn || L.show_sunrise || L.show_full || (L.show_plain && (ready || (platform->install_parts && platform->install_parts())));

    if ((blocked || stop) && g_open) {
        g_open = 0;
        g_confirm = 0;
    }

    if (locked) {
        g_hover_main = approach(g_hover_main, 0.0f, dt);
        g_hover_caret = approach(g_hover_caret, (over_caret || g_open) ? 1.0f : 0.0f, dt);
    } else if (danger) {
        g_hover_main = approach(g_hover_main, over_split ? 1.0f : 0.0f, dt);
        g_hover_caret = g_hover_main;
    } else {
        g_hover_main = approach(g_hover_main, over_main ? 1.0f : 0.0f, dt);
        g_hover_caret = approach(g_hover_caret, (over_caret || g_open) ? 1.0f : 0.0f, dt);
    }
    g_hover_verify = approach(g_hover_verify, over_verify ? 1.0f : 0.0f, dt);
    g_hover_sim = approach(g_hover_sim, over_sim ? 1.0f : 0.0f, dt);
    g_hover_dawn = approach(g_hover_dawn, over_dawn ? 1.0f : 0.0f, dt);
    g_hover_sunrise = approach(g_hover_sunrise, over_sunrise ? 1.0f : 0.0f, dt);
    g_hover_full = approach(g_hover_full, over_full ? 1.0f : 0.0f, dt);
    g_hover_uninstall = approach(g_hover_uninstall, over_uninstall ? 1.0f : 0.0f, dt);
    g_menu = g_open ? 1.0f : 0.0f;
    g_hint = (hint[0] && L.hint_w > 8.0f) ? 1.0f : 0.0f;
    if (shimmer && g_hint > 0.02f) {
        g_shimmer = (float)(os_tick_ms() % 1700u) / 1700.0f;
    } else {
        g_shimmer = 0.0f;
    }

    uint32_t fg = locked ? 0x9aa3b2 : BTN_TEXT;
    if (danger && g_hover_main > 0.02f) {
        fg = modal_mix(BTN_TEXT, 0xe81123, (int)(g_hover_main * 180.0f));
    }
    if (g_hint > 0.02f && L.hint_w > 8.0f) {
        const ThemeChrome *chrome = theme_chrome();
        uint32_t dim = modal_mix(chrome->muted, 0x000000, 24);
        uint32_t shine = modal_mix(chrome->title_color, 0xffffff, 40);
        int alpha = (int)(g_hint * 255.0f);
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
                600,
                alpha,
                g_shimmer
            );
        } else {
            icon_draw_label_end_alpha(
                platform->hdc,
                L.hint_x,
                L.hint_y,
                L.hint_w,
                L.hint_h,
                hint_w,
                dim,
                L.hint_px,
                600,
                alpha
            );
        }
    }
    float radius = L.h * 0.5f;
    float progress = (busy || paused) && !stop && platform->install_progress ? platform->install_progress() : 0.0f;
    if (progress < 0.0f) {
        progress = 0.0f;
    }
    if (progress > 1.0f) {
        progress = 1.0f;
    }

    icon_round_rect(platform->hdc, L.x, L.y + 1.5f, L.w, L.h, radius, 0x000000, 28);
    icon_round_rect(
        platform->hdc,
        L.x,
        L.y,
        L.w,
        L.h,
        radius,
        locked ? modal_mix(BTN_FILL, 0xb8bec8, 70) : BTN_FILL,
        locked ? 200 : 255
    );
    {
        float fill_w = L.w * progress;
        if ((busy || paused) && !stop && !danger && fill_w >= radius) {
            icon_round_rect_corners(
                platform->hdc,
                L.x,
                L.y,
                fill_w,
                L.h,
                radius,
                fill_w >= L.w - 0.5f ? radius : 0.0f,
                fill_w >= L.w - 0.5f ? radius : 0.0f,
                radius,
                BTN_PROGRESS,
                255
            );
        }
    }
    if (danger && g_hover_main > 0.02f) {
        icon_round_rect(
            platform->hdc,
            L.x,
            L.y,
            L.w,
            L.h,
            radius,
            modal_mix(BTN_FILL, 0xe81123, 48 + (int)(g_hover_main * 90.0f)),
            (int)(g_hover_main * 255.0f)
        );
    } else if (g_hover_main > 0.02f) {
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
    if (!danger && g_hover_caret > 0.02f) {
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

    float div_h = L.h * 0.46f;
    int div_a = 28;
    if (danger) {
        div_a = (int)(28.0f * (1.0f - g_hover_main) + 0.5f);
    }
    if (div_a > 0) {
        icon_fill_rect(
            platform->hdc,
            L.caret_x,
            L.y + (L.h - div_h) * 0.5f,
            1.0f,
            div_h,
            0x111827,
            div_a
        );
    }

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
        float dpi = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
        float danger_y = -1.0f;
        draw_menu_card(platform, L.menu_x, L.menu_y, L.menu_w, L.menu_h, g_menu);
        draw_menu_item(
            platform,
            L.item_x,
            L.verify_y,
            L.item_w,
            L.item_h,
            g_menu,
            g_hover_verify,
            platform->mouse_down && over_verify,
            busy || stop,
            0,
            ICON_SEARCH,
            i18n_t(I18N_VERIFY_FILES)
        );
#if APP_DEV
        if (L.show_sim) {
            draw_menu_item(
                platform,
                L.item_x,
                L.sim_y,
                L.item_w,
                L.item_h,
                g_menu,
                g_hover_sim,
                platform->mouse_down && over_sim,
                0,
                0,
                ICON_DOWNLOAD,
                i18n_t(I18N_SIMULATE)
            );
        }
#endif
        if (L.show_dawn) {
            danger_y = L.dawn_y;
        } else if (L.show_sunrise) {
            danger_y = L.sunrise_y;
        } else if (L.show_full) {
            danger_y = L.full_y;
        } else if (L.show_plain) {
            danger_y = L.uninstall_y;
        }
        if (danger_y > 0.0f) {
            draw_menu_divider(
                platform,
                L.item_x + 4.0f * dpi,
                danger_y - MENU_ITEM_GAP * dpi * 0.5f,
                L.item_w - 8.0f * dpi,
                g_menu
            );
        }
        if (L.show_dawn) {
            draw_menu_item(
                platform,
                L.item_x,
                L.dawn_y,
                L.item_w,
                L.item_h,
                g_menu,
                g_confirm == 1 && g_hover_dawn < 0.5f ? 0.5f : g_hover_dawn,
                platform->mouse_down && over_dawn,
                0,
                1,
                ICON_X,
                g_confirm == 1 ? i18n_t(I18N_CONFIRM_UNINSTALL) : i18n_t(I18N_UNINSTALL_DAWN)
            );
        }
        if (L.show_sunrise) {
            draw_menu_item(
                platform,
                L.item_x,
                L.sunrise_y,
                L.item_w,
                L.item_h,
                g_menu,
                g_confirm == 2 && g_hover_sunrise < 0.5f ? 0.5f : g_hover_sunrise,
                platform->mouse_down && over_sunrise,
                0,
                1,
                ICON_X,
                g_confirm == 2 ? i18n_t(I18N_CONFIRM_UNINSTALL) : i18n_t(I18N_UNINSTALL_SUNRISE)
            );
        }
        if (L.show_full) {
            draw_menu_item(
                platform,
                L.item_x,
                L.full_y,
                L.item_w,
                L.item_h,
                g_menu,
                g_confirm == 3 && g_hover_full < 0.5f ? 0.5f : g_hover_full,
                platform->mouse_down && over_full,
                0,
                1,
                ICON_X,
                g_confirm == 3 ? i18n_t(I18N_CONFIRM_UNINSTALL) : i18n_t(I18N_UNINSTALL_ALL)
            );
        }
        if (L.show_plain) {
            draw_menu_item(
                platform,
                L.item_x,
                L.uninstall_y,
                L.item_w,
                L.item_h,
                g_menu,
                (busy || stop) ? g_hover_uninstall :
                    (g_confirm == 4 && can_remove && g_hover_uninstall < 0.5f ? 0.5f : g_hover_uninstall),
                platform->mouse_down && over_uninstall,
                (busy || stop) ? 0 : !can_remove,
                1,
                ICON_X,
                stop ? i18n_t(I18N_STOP_D2) : (busy ? i18n_t(I18N_CANCEL_DOWNLOAD) :
                    (g_confirm == 4 ? i18n_t(I18N_CONFIRM_UNINSTALL) : i18n_t(I18N_UNINSTALL)))
            );
        }
    }

    if (!platform->mouse_pressed || blocked) {
        return;
    }
    if (over_main) {
        g_open = 0;
        g_confirm = 0;
        if (stop) {
            if (platform->game_stop) {
                platform->game_stop();
            }
        } else if (pausable) {
            if (platform->install_pause) {
                platform->install_pause();
            }
        } else if (busy) {
            if (platform->install_cancel) {
                platform->install_cancel();
            }
        } else if (locked) {
            if (!is_signed_in(platform)) {
                login_modal_open();
            }
        } else if (ready) {
            if (dawn_needs_update(platform) && platform->install_start) {
                platform->install_start();
            } else if (platform->install_launch) {
                platform->install_launch();
            }
        } else if (!is_signed_in(platform)) {
            login_modal_open();
        } else {
            settings_modal_open_for_download();
        }
        return;
    }
    if (over_caret) {
        if (stop) {
            g_open = 0;
            g_confirm = 0;
            if (platform->game_stop) {
                platform->game_stop();
            }
            return;
        }
        if (busy && !pausable) {
            g_open = 0;
            g_confirm = 0;
            if (platform->install_cancel) {
                platform->install_cancel();
            }
            return;
        }
        g_open = !g_open;
        if (!g_open) {
            g_confirm = 0;
        }
        return;
    }
    if (over_verify && !busy && !stop) {
        g_open = 0;
        g_confirm = 0;
        if (action_block(platform)) {
            if (!is_signed_in(platform)) {
                login_modal_open();
            }
        } else if (platform->install_verify) {
            platform->install_verify();
        }
        return;
    }
#if APP_DEV
    if (over_sim && !busy && !stop && can_simulate(platform)) {
        g_open = 0;
        g_confirm = 0;
        if (platform->install_simulate) {
            platform->install_simulate();
        }
        return;
    }
#endif
    if (over_uninstall && stop) {
        g_open = 0;
        g_confirm = 0;
        if (platform->game_stop) {
            platform->game_stop();
        }
        return;
    }
    if (over_uninstall && busy) {
        g_open = 0;
        g_confirm = 0;
        if (platform->install_cancel) {
            platform->install_cancel();
        }
        return;
    }
    if (over_dawn && !busy && !stop) {
        if (g_confirm != 1) {
            g_confirm = 1;
            return;
        }
        g_open = 0;
        g_confirm = 0;
        if (platform->install_uninstall_part) {
            platform->install_uninstall_part(INSTALL_PART_DAWN);
        } else if (platform->install_uninstall) {
            platform->install_uninstall();
        }
        return;
    }
    if (over_sunrise && !busy && !stop) {
        if (g_confirm != 2) {
            g_confirm = 2;
            return;
        }
        g_open = 0;
        g_confirm = 0;
        if (platform->install_uninstall_part) {
            platform->install_uninstall_part(INSTALL_PART_SUNRISE);
        }
        return;
    }
    if (over_full && !busy && !stop) {
        if (g_confirm != 3) {
            g_confirm = 3;
            return;
        }
        g_open = 0;
        g_confirm = 0;
        if (platform->install_uninstall) {
            platform->install_uninstall();
        }
        return;
    }
    if (over_uninstall && !busy && !stop && can_remove) {
        if (g_confirm != 4) {
            g_confirm = 4;
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
