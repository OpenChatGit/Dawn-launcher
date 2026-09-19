#include "login_modal.h"
#include "modal_skin.h"
#include "settings_modal.h"
#include "shared/icons.h"
#include "shared/os.h"

#include <stdio.h>
#include <string.h>

#define LOGIN_USER_MAX 64

typedef struct LoginLayout {
    float overlay_x;
    float overlay_y;
    float overlay_w;
    float overlay_h;
    float x;
    float y;
    float w;
    float h;
    float steam_x;
    float steam_y;
    float steam_w;
    float steam_h;
    float close_x;
    float close_y;
    float close_s;
} LoginLayout;

static AppState *g_state;
static int g_open;
static int g_block_mouse;
static int g_signed_in;
static float g_anim;
static float g_hover_steam;
static float g_hover_close;
static char g_user[LOGIN_USER_MAX];
static char g_avatar[260];
static char g_steam_id[32];

static void
layout_modal(Platform *platform, LoginLayout *out)
{
    float s = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    float inset = (float)modal_px(MODAL_INSET, s);
    out->overlay_x = 0.0f;
    out->overlay_y = 0.0f;
    out->overlay_w = (float)platform->width;
    out->overlay_h = (float)platform->height;
    out->w = (float)modal_px(360, s);
    out->h = (float)modal_px(148, s);
    if (out->w > (float)platform->width - 32.0f) {
        out->w = (float)platform->width - 32.0f;
    }
    out->x = ((float)platform->width - out->w) * 0.5f;
    out->y = ((float)platform->height - out->h) * 0.5f;
    out->steam_w = out->w - inset * 2.0f;
    out->steam_h = (float)modal_px(MODAL_BTN_H, s);
    out->steam_x = out->x + inset;
    out->steam_y = out->y + (float)modal_px(78, s);
    modal_place_close(out->x, out->y, out->w, s, &out->close_x, &out->close_y, &out->close_s);
}

static void
persist(void)
{
    if (!g_state) {
        return;
    }
    g_state->steam_signed_in = g_signed_in && g_user[0] != '\0';
    if (g_state->steam_signed_in) {
        snprintf(g_state->steam_user, sizeof(g_state->steam_user), "%s", g_user);
        snprintf(g_state->steam_id, sizeof(g_state->steam_id), "%s", g_steam_id);
        snprintf(g_state->steam_avatar, sizeof(g_state->steam_avatar), "%s", g_avatar);
    } else {
        g_state->steam_user[0] = '\0';
        g_state->steam_id[0] = '\0';
        g_state->steam_avatar[0] = '\0';
    }
}

static void
begin_steam_sign_in(Platform *platform)
{
    if (platform->steam_sign_in) {
        platform->steam_sign_in();
    } else if (platform->log) {
        platform->log("steam: sign in is not available");
    }
}

void
login_modal_open(void)
{
    settings_modal_hide();
    g_open = 1;
    g_block_mouse = 1;
}

void
login_modal_close(void)
{
    g_open = 0;
    g_anim = 0.0f;
    g_block_mouse = 0;
}

void
login_modal_hide(void)
{
    g_open = 0;
    g_anim = 0.0f;
    g_block_mouse = 0;
    g_hover_steam = 0.0f;
    g_hover_close = 0.0f;
}

void
login_modal_sign_out(void)
{
    g_signed_in = 0;
    g_user[0] = '\0';
    g_avatar[0] = '\0';
    g_steam_id[0] = '\0';
    g_open = 0;
    persist();
}

void
login_modal_sync(AppState *state, Platform *platform)
{
    g_state = state;
    if (platform && platform->steam_signed_in) {
        const char *persona = platform->steam_persona ? platform->steam_persona() : "";
        if (platform->steam_signed_in() && persona && persona[0]) {
            g_signed_in = 1;
            snprintf(g_user, sizeof(g_user), "%s", persona);
            if (platform->steam_avatar_path) {
                snprintf(g_avatar, sizeof(g_avatar), "%s", platform->steam_avatar_path());
            }
            if (platform->steam_id) {
                snprintf(g_steam_id, sizeof(g_steam_id), "%s", platform->steam_id());
            }
            persist();
            if (state) {
                state->login_prompted = 1;
            }
            if (g_open && platform->steam_busy && !platform->steam_busy()) {
                login_modal_hide();
            }
            return;
        }
        if (!g_open) {
            g_signed_in = 0;
            g_user[0] = '\0';
            g_avatar[0] = '\0';
            persist();
        }
        if (state && !state->login_prompted) {
            int busy = platform->steam_busy && platform->steam_busy();
            if (!busy) {
                state->login_prompted = 1;
                login_modal_open();
            }
        }
        return;
    }
    if (!state || g_open) {
        return;
    }
    g_signed_in = state->steam_signed_in && state->steam_user[0] != '\0';
    if (g_signed_in) {
        snprintf(g_user, sizeof(g_user), "%s", state->steam_user);
        snprintf(g_steam_id, sizeof(g_steam_id), "%s", state->steam_id);
        snprintf(g_avatar, sizeof(g_avatar), "%s", state->steam_avatar);
    }
}

int
login_modal_visible(void)
{
    return g_open || g_anim > 0.0f;
}

int
login_modal_signed_in(void)
{
    return g_signed_in && g_user[0] != '\0';
}

const char *
login_modal_username(void)
{
    return g_signed_in ? g_user : "";
}

const char *
login_modal_avatar_path(void)
{
    return g_signed_in ? g_avatar : "";
}

int
login_modal_wants_mouse(Platform *platform)
{
    (void)platform;
    return g_open;
}

void
login_modal_tick(Platform *platform, float dt)
{
    if (!platform) {
        return;
    }
    g_anim = g_open ? 1.0f : 0.0f;
    if (g_anim <= 0.0f) {
        return;
    }

    LoginLayout L;
    layout_modal(platform, &L);
    float s = platform->dpi_scale > 0.1f ? platform->dpi_scale : 1.0f;
    int mx = platform->mouse_x;
    int my = platform->mouse_y;
    float inset = (float)modal_px(MODAL_INSET, s);
    float x = L.x;
    float y = L.y;
    int over_steam = modal_hit(mx, my, L.steam_x, L.steam_y, L.steam_w, L.steam_h);
    int over_close = modal_hit(mx, my, L.close_x, L.close_y, L.close_s, L.close_s);
    int over_card = modal_hit(mx, my, x, y, L.w, L.h);

    g_hover_steam = modal_approach(g_hover_steam, (g_open && over_steam) ? 1.0f : 0.0f, dt);
    g_hover_close = modal_approach(g_hover_close, (g_open && over_close) ? 1.0f : 0.0f, dt);

    modal_draw_overlay(platform->hdc, L.overlay_w, L.overlay_h, g_anim);
    modal_draw_card(platform->hdc, x, y, L.w, L.h, g_anim, s);
    modal_draw_title(
        platform->hdc,
        x + inset,
        y + (float)modal_px(14, s),
        L.w - inset * 2.0f - L.close_s,
        (float)modal_px(20, s),
        L"Sign in",
        s,
        g_anim
    );
    {
        const char *status = platform->steam_status ? platform->steam_status() : NULL;
        wchar_t status_w[160];
        status_w[0] = 0;
        if (status && status[0]) {
            os_utf8_to_wide(status, status_w, 160);
        }
        modal_draw_subtitle(
            platform->hdc,
            x + inset,
            y + (float)modal_px(36, s),
            L.w - inset * 2.0f - L.close_s,
            (float)modal_px(18, s),
            status_w[0] ? status_w : L"Continue with Steam",
            s,
            g_anim
        );
    }
    modal_draw_close(platform->hdc, L.close_x, L.close_y, L.close_s, g_hover_close, g_anim);
    modal_draw_button(
        platform->hdc,
        L.steam_x,
        L.steam_y,
        L.steam_w,
        L.steam_h,
        L"Sign in with Steam",
        ICON_STEAM,
        g_hover_steam,
        g_anim,
        0,
        s
    );

    if (!g_open) {
        return;
    }

    if (platform->key == PLATFORM_KEY_ENTER) {
        begin_steam_sign_in(platform);
    } else if (platform->key == PLATFORM_KEY_ESCAPE) {
        if (platform->steam_cancel) {
            platform->steam_cancel();
        }
        login_modal_close();
    }

    if (g_block_mouse) {
        if (!platform->mouse_down && !platform->mouse_pressed) {
            g_block_mouse = 0;
        }
        platform->mouse_pressed = 0;
        return;
    }

    if (platform->mouse_pressed) {
        if (over_close || (!over_card)) {
            if (platform->steam_cancel) {
                platform->steam_cancel();
            }
            login_modal_close();
        } else if (over_steam) {
            begin_steam_sign_in(platform);
        }
        platform->mouse_pressed = 0;
    }
}
