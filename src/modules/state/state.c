#include "state.h"

#include <string.h>

AppState *
state_from_memory(AppMemory *memory)
{
    return (AppState *)memory->data;
}

void
state_init(AppState *state)
{
    memset(state, 0, sizeof(*state));
    state->magic = APP_STATE_MAGIC;
    state->theme_id[0] = '\0';
}

int
state_valid(const AppState *state)
{
    return state && state->magic == APP_STATE_MAGIC;
}
