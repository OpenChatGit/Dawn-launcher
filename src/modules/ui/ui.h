#ifndef MODULES_UI_H
#define MODULES_UI_H

#include "shared/api.h"
#include "state/state.h"

void ui_tick(AppState *state, Platform *platform, uint32_t reload_count, float dt);

#endif
