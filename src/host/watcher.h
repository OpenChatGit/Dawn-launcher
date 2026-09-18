#ifndef HOST_WATCHER_H
#define HOST_WATCHER_H

int watcher_init(const char *project_root);
void watcher_shutdown(void);
int watcher_poll(void);

#endif
