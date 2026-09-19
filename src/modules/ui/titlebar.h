#ifndef MODULES_UI_TITLEBAR_H
#define MODULES_UI_TITLEBAR_H

#include "shared/api.h"

#define TITLEBAR_HEIGHT 46

int titlebar_wants_mouse(Platform *platform);
int titlebar_modal_visible(void);
void titlebar_tick(Platform *platform, float dt);

#endif
