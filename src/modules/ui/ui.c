#include "ui.h"
#include "titlebar.h"
#include "login_modal.h"
#include "settings_modal.h"
#include "install_button.h"
#include "shared/theme.h"

#include <string.h>
#include <stdio.h>

void
ui_tick(AppState *state, Platform *platform, uint32_t reload_count, float dt)
{
    (void)reload_count;

    if (state->theme_id[0] == '\0') {
        snprintf(state->theme_id, sizeof(state->theme_id), "%s", theme_standard_id());
    }
    if (strcmp(theme_current_id(), state->theme_id) != 0) {
        theme_set(state->theme_id);
    }

    login_modal_sync(state, platform);

    int chrome_hit = titlebar_wants_mouse(platform) ||
        login_modal_visible() ||
        settings_modal_visible() ||
        install_button_wants_mouse(platform);
    int pressed = platform->mouse_pressed;
    int released = platform->mouse_released;
    if (chrome_hit) {
        platform->mouse_pressed = 0;
        platform->mouse_released = 0;
    }

    theme_draw(platform, state->time);
    theme_ui_tick(platform, dt);

    if (chrome_hit) {
        platform->mouse_pressed = pressed;
        platform->mouse_released = released;
    }

    if (theme_current_id()[0] && strcmp(theme_current_id(), state->theme_id) != 0) {
        snprintf(state->theme_id, sizeof(state->theme_id), "%s", theme_current_id());
    }

    titlebar_tick(platform, dt);
    install_button_tick(platform, dt);
    login_modal_tick(platform, dt);
    settings_modal_tick(platform, dt);
}
