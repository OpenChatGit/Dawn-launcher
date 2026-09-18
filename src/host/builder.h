#ifndef HOST_BUILDER_H
#define HOST_BUILDER_H

typedef enum BuildStatus {
    BUILD_IDLE = 0,
    BUILD_RUNNING,
    BUILD_OK,
    BUILD_FAILED
} BuildStatus;

void builder_prepare_path(void);
int builder_start(void);
void builder_poll(void);
BuildStatus builder_status(void);
const char *builder_log(void);
void builder_reset(void);

#endif
