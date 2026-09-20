#ifndef HOST_APP_ICON_H
#define HOST_APP_ICON_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
void app_icon_apply_win(HWND hwnd, HINSTANCE inst);
#else
void app_icon_apply_x11(void *display, unsigned long window);
#endif

#endif
