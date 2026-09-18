#ifndef HOST_HOT_RELOAD_H
#define HOST_HOT_RELOAD_H

#include "shared/api.h"

int hot_reload_init(AppMemory *memory);
void hot_reload_shutdown(void);
int hot_reload_swap(void);
const AppApi *hot_reload_api(void);
const char *hot_reload_error(void);

#endif
