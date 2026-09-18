#ifndef MODULES_STATE_H
#define MODULES_STATE_H

#include "shared/api.h"

#define APP_STATE_MAGIC 0x41505035u

typedef struct AppState {
    uint32_t magic;
    float time;
    uint32_t frames;
    char theme_id[64];
    int steam_signed_in;
    char steam_user[64];
    char steam_id[32];
    char steam_avatar[260];
} AppState;

AppState *state_from_memory(AppMemory *memory);
void state_init(AppState *state);
int state_valid(const AppState *state);

#endif
