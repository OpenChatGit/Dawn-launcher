#ifndef HOST_SELF_UPDATE_H
#define HOST_SELF_UPDATE_H

void self_update_init(void);
void self_update_shutdown(void);
void self_update_poll(void);

int self_update_available(void);
const char *self_update_version(void);
int self_update_busy(void);
const char *self_update_status(void);
int self_update_begin(void);
void self_update_cancel(void);
int self_update_should_quit(void);

#endif
