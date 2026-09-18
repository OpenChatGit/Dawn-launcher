#include "app.h"
#include "state/state.h"
#include "ui/ui.h"
#include "shared/theme.h"

void
app_init(AppMemory *memory)
{
    AppState *state = state_from_memory(memory);
    state_init(state);
    memory->initialized = 1;
}

void
app_reload(AppMemory *memory)
{
    AppState *state = state_from_memory(memory);
    if (!state_valid(state)) {
        state_init(state);
    }
}

void
app_tick(AppMemory *memory, Platform *platform, float dt)
{
    AppState *state = state_from_memory(memory);
    if (!state_valid(state)) {
        state_init(state);
    }

    state->time += dt;
    state->frames += 1;
    ui_tick(state, platform, memory->reload_count, dt);
}

void
app_shutdown(AppMemory *memory)
{
    (void)memory;
    theme_manager_shutdown();
}

APP_EXPORT const AppApi *
app_get_api(void)
{
    static const AppApi api = {
        .version = APP_API_VERSION,
        .init = app_init,
        .reload = app_reload,
        .tick = app_tick,
        .shutdown = app_shutdown,
    };
    return &api;
}
