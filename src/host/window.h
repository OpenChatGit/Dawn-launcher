#ifndef HOST_WINDOW_H
#define HOST_WINDOW_H

#include "shared/api.h"
#include "shared/window_types.h"

int window_create(HostWindow *window, const char *title, int width, int height);
void window_destroy(HostWindow *window);
void window_pump(HostWindow *window);
void window_end_frame_input(HostWindow *window);
void window_present(HostWindow *window);
void window_bind_platform(HostWindow *window, Platform *platform, const char *project_root);

#endif
