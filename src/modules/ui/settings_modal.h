#ifndef MODULES_UI_SETTINGS_MODAL_H
#define MODULES_UI_SETTINGS_MODAL_H

#include "shared/api.h"

void settings_modal_open(void);
void settings_modal_close(void);
void settings_modal_hide(void);
int settings_modal_visible(void);
int settings_modal_wants_mouse(Platform *platform);
void settings_modal_tick(Platform *platform, float dt);

#endif
