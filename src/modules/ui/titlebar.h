#ifndef MODULES_UI_TITLEBAR_H
#define MODULES_UI_TITLEBAR_H

#include "shared/api.h"

#define TITLEBAR_HEIGHT 46

int titlebar_wants_mouse(Platform *platform);
void titlebar_tick(Platform *platform, float dt);

#endif
