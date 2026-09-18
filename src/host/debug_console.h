#ifndef HOST_DEBUG_CONSOLE_H
#define HOST_DEBUG_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

void debug_console_init(void);
void debug_console_shutdown(void);
void debug_console_toggle(void);
int debug_console_open(void);
void debug_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
