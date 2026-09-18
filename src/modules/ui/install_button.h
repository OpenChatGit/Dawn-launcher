#ifndef MODULES_UI_INSTALL_BUTTON_H
#define MODULES_UI_INSTALL_BUTTON_H

#include "shared/api.h"

int install_button_wants_mouse(Platform *platform);
void install_button_tick(Platform *platform, float dt);

#endif
