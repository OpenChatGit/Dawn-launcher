#ifndef MODULES_APP_H
#define MODULES_APP_H

#include "shared/api.h"

void app_init(AppMemory *memory);
void app_reload(AppMemory *memory);
void app_tick(AppMemory *memory, Platform *platform, float dt);
void app_shutdown(AppMemory *memory);

#endif
